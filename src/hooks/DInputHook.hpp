#pragma once

#include <unordered_map>
#include <mutex>
#include <memory>
#include <array>
#include <vector>
#include <chrono>

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif

#include <dinput.h>

#include <safetyhook.hpp>

#include <utility/PointerHook.hpp>

class DInputHook {
public:
    DInputHook();
    virtual ~DInputHook() = default;

private:
    static HRESULT WINAPI create_hooked(
        HINSTANCE hinst,
        DWORD dwVersion,
        REFIID riidltf,
        LPVOID* ppvOut,
        LPUNKNOWN punkOuter
    );

    static HRESULT enum_devices_hooked(
        LPDIRECTINPUT8W This,
        DWORD dwDevType,
        LPDIENUMDEVICESCALLBACKW lpCallback,
        LPVOID pvRef,
        DWORD dwFlags
    );

    static HRESULT create_device_hooked(
        LPDIRECTINPUT8W This,
        REFGUID rguid,
        LPDIRECTINPUTDEVICE8W* device,
        LPUNKNOWN punkOuter
    );

    static HRESULT get_device_state_hooked(
        LPDIRECTINPUTDEVICE8W This,
        DWORD cbData,
        LPVOID lpvData
    );

    // This is recursive because apparently EnumDevices
    // can call DirectInput8Create again... wHAT?
    std::recursive_mutex m_mutex{};

    safetyhook::InlineHook m_create_hook{};
    std::unique_ptr<PointerHook> m_enum_devices_hook{};
    std::unique_ptr<PointerHook> m_create_device_hook{};

    struct Device {
        LPDIRECTINPUTDEVICE8W ptr{};
        std::unique_ptr<PointerHook> get_state_hook{};
    };

    std::vector<Device> m_devices{};

    struct {
        bool menu_longpress_begin_held{false};
        std::chrono::steady_clock::time_point menu_longpress_begin{};
    } m_di_context{};

    std::chrono::steady_clock::time_point m_last_di_l3_r3_menu_open{};
};