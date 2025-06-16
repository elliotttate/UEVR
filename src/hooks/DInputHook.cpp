#include <chrono>
#include <algorithm>

#include <spdlog/spdlog.h>
#include <utility/String.hpp>

#include "Framework.hpp"
#include "mods/VR.hpp"
#include "mods/FrameworkConfig.hpp"
#include "utility/Logging.hpp"

#include "DInputHook.hpp"

DInputHook* g_dinput_hook{nullptr};

constexpr size_t ENUM_DEVICES_VTABLE_INDEX = 4;
constexpr size_t CREATE_DEVICE_VTABLE_INDEX = 3;
constexpr size_t GET_STATE_VTABLE_INDEX = 9;

DInputHook::DInputHook() {
    SPDLOG_INFO("[DInputHook] Constructing DInputHook");

    g_dinput_hook = this;

    SPDLOG_INFO("[DInputHook] Creating thread");

    std::thread([this]() {
        SPDLOG_INFO("[DInputHook] Entering thread");

        const auto start_time = std::chrono::steady_clock::now();
        HMODULE dinput8{nullptr};

        while (dinput8 == nullptr) {
            dinput8 = GetModuleHandleA("dinput8.dll");

            if (dinput8) {
                SPDLOG_INFO("[DInputHook] dinput8.dll loaded");
                break;
            }

            const auto now = std::chrono::steady_clock::now();

            if (now - start_time > std::chrono::seconds(10)) {
                SPDLOG_ERROR("[DInputHook] Timed out waiting for dinput8.dll to load, aborting hook");
                return;
            }

            SPDLOG_INFO("[DInputHook] Waiting for dinput8.dll to load...");
            Sleep(1000);
        }

        auto create_addr = GetProcAddress(dinput8, "DirectInput8Create");

        if (create_addr == nullptr) {
            SPDLOG_ERROR("[DInputHook] Failed to find DirectInput8Create, aborting hook");
            return;
        }

        SPDLOG_INFO("[DInputHook] Found DirectInput8Create at {:x}", (uintptr_t)create_addr);

        m_create_hook = safetyhook::create_inline(create_addr, (uintptr_t)create_hooked);

        if (!m_create_hook) {
            SPDLOG_ERROR("[DInputHook] Failed to hook DirectInput8Create, aborting hook");
            return;
        }

        SPDLOG_INFO("[DInputHook] Hooked DirectInput8Create");
    }).detach();

    SPDLOG_INFO("[DInputHook] Exiting constructor");
}

HRESULT WINAPI DInputHook::create_hooked(
    HINSTANCE hinst,
    DWORD dwVersion,
    REFIID riidltf,
    LPVOID* ppvOut,
    LPUNKNOWN punkOuter
) 
{
    SPDLOG_INFO_EVERY_N_SEC(5, "[DInputHook] DirectInput8Create called {:x} {} {:x} {:x}", (uintptr_t)hinst, dwVersion, (uintptr_t)ppvOut, (uintptr_t)punkOuter);

    const auto og = g_dinput_hook->m_create_hook.original<decltype(&create_hooked)>();
    const auto result = og(hinst, dwVersion, riidltf, ppvOut, punkOuter);

    if (result == DI_OK) {
        if (ppvOut == nullptr) {
            SPDLOG_INFO("[DInputHook] ppvOut is null");
            return result;
        }

        std::scoped_lock _{g_dinput_hook->m_mutex};

        auto iface = (LPDIRECTINPUT8W)*ppvOut;

        if (iface != nullptr && memcmp(&riidltf, &IID_IDirectInput8W, sizeof(GUID)) == 0) {
            if (g_dinput_hook->m_enum_devices_hook == nullptr) {
                SPDLOG_INFO("[DInputHook] Hooking IDirectInput8::EnumDevices");
                void** enum_devices_ptr = (void**)&(*(uintptr_t**)iface)[ENUM_DEVICES_VTABLE_INDEX];
                g_dinput_hook->m_enum_devices_hook = std::make_unique<PointerHook>(enum_devices_ptr, (void*)&enum_devices_hooked);
                SPDLOG_INFO("[DInputHook] Hooked IDirectInput8::EnumDevices");
            }

            if (g_dinput_hook->m_create_device_hook == nullptr) {
                SPDLOG_INFO("[DInputHook] Hooking IDirectInput8::CreateDevice");
                void** create_device_ptr = (void**)&(*(uintptr_t**)iface)[CREATE_DEVICE_VTABLE_INDEX];
                g_dinput_hook->m_create_device_hook = std::make_unique<PointerHook>(create_device_ptr, (void*)&create_device_hooked);
                SPDLOG_INFO("[DInputHook] Hooked IDirectInput8::CreateDevice");
            }
        }
    } else {
        SPDLOG_INFO("[DInputHook] DirectInput8Create failed");
    }

    return result;
}

HRESULT DInputHook::enum_devices_hooked(
    LPDIRECTINPUT8W This,
    DWORD dwDevType,
    LPDIENUMDEVICESCALLBACKW lpCallback,
    LPVOID pvRef,
    DWORD dwFlags
)
{
    SPDLOG_INFO_EVERY_N_SEC(5, "[DInputHook] IDirectInput8::EnumDevices called");

    std::scoped_lock _{g_dinput_hook->m_mutex};

    const auto og = g_dinput_hook->m_enum_devices_hook->get_original<decltype(&enum_devices_hooked)>();

    if (og == nullptr) {
        SPDLOG_INFO("[DInputHook] IDirectInput8::EnumDevices original method is null");
        return DI_OK;
    }

    // We dont care about these other ones, so just call the original
    if (dwDevType != DI8DEVCLASS_GAMECTRL) {
        auto result = og(This, dwDevType, lpCallback, pvRef, dwFlags);
        return result;
    }

    // the purpose of this is to stop some games from spamming calls to EnumDevices
    // without a real controller connected, which causes the game to drop to single digit FPS
    auto should_call_original = g_framework->is_ready() && !VR::get()->is_using_controllers_within(std::chrono::seconds(5));

    if (should_call_original) {
        auto result = og(This, dwDevType, lpCallback, pvRef, dwFlags);
        return result;
    }

    return DI_OK;
}

HRESULT DInputHook::create_device_hooked(
    LPDIRECTINPUT8W This,
    REFGUID rguid,
    LPDIRECTINPUTDEVICE8W* device,
    LPUNKNOWN punkOuter
)
{
    std::scoped_lock _{g_dinput_hook->m_mutex};

    const auto og = g_dinput_hook->m_create_device_hook->get_original<decltype(&create_device_hooked)>();

    if (og == nullptr) {
        return DIERR_GENERIC;
    }

    const auto result = og(This, rguid, device, punkOuter);

    if (result == DI_OK && device != nullptr && *device != nullptr) {
        auto dev_ptr = *device;
        void** vtable = *(void***)dev_ptr;
        void** get_state_ptr = &vtable[GET_STATE_VTABLE_INDEX];

        Device dev{};
        dev.ptr = dev_ptr;
        dev.get_state_hook = std::make_unique<PointerHook>(get_state_ptr, (void*)&get_device_state_hooked);

        m_devices.push_back(std::move(dev));
        SPDLOG_INFO("[DInputHook] Hooked IDirectInputDevice8::GetDeviceState {:x}", (uintptr_t)get_state_ptr);
    }

    return result;
}

HRESULT DInputHook::get_device_state_hooked(
    LPDIRECTINPUTDEVICE8W This,
    DWORD cbData,
    LPVOID lpvData
)
{
    auto it = std::find_if(g_dinput_hook->m_devices.begin(), g_dinput_hook->m_devices.end(), [This](const Device& d) { return d.ptr == This; });

    if (it == g_dinput_hook->m_devices.end()) {
        return DIERR_GENERIC;
    }

    const auto og = it->get_state_hook->get_original<decltype(&get_device_state_hooked)>();
    auto ret = og(This, cbData, lpvData);

    if (SUCCEEDED(ret) && lpvData != nullptr && cbData >= sizeof(DIJOYSTATE2)) {
        auto js = reinterpret_cast<DIJOYSTATE2*>(lpvData);

        const bool l3 = (js->rgbButtons[8] & 0x80) != 0;
        const bool r3 = (js->rgbButtons[9] & 0x80) != 0;

        if (l3 && r3 && FrameworkConfig::get()->is_enable_directinput_l3_r3_toggle()) {
            bool should_open = true;
            const auto now = std::chrono::steady_clock::now();

            if (FrameworkConfig::get()->is_l3_r3_long_press() && !g_framework->is_drawing_ui()) {
                if (!g_dinput_hook->m_di_context.menu_longpress_begin_held) {
                    g_dinput_hook->m_di_context.menu_longpress_begin = now;
                }

                g_dinput_hook->m_di_context.menu_longpress_begin_held = true;
                should_open = (now - g_dinput_hook->m_di_context.menu_longpress_begin) >= std::chrono::seconds(1);
            } else {
                g_dinput_hook->m_di_context.menu_longpress_begin_held = false;
            }

            if (should_open && now - g_dinput_hook->m_last_di_l3_r3_menu_open >= std::chrono::seconds(1)) {
                g_dinput_hook->m_last_di_l3_r3_menu_open = std::chrono::steady_clock::now();
                g_framework->set_draw_ui(!g_framework->is_drawing_ui());
            }

            js->rgbButtons[8] &= ~0x80;
            js->rgbButtons[9] &= ~0x80;
        } else if (!l3 && !r3) {
            g_dinput_hook->m_di_context.menu_longpress_begin_held = false;
        }
    }

    return ret;
}