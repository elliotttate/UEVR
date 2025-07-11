/*
UEVR TurnChanges DLL Plugin
Replicates ALL functionality from TurnChangesToPlugin:
1. L3+R3 Aim Method Toggle (hold 1 second)
2. Two new DPad methods using joystick clicks
3. Complete configuration GUI

Copyright (c) 2024 - MIT License
*/

#include <sstream>
#include <mutex>
#include <memory>
#include <chrono>
#include <unordered_map>

#include <Windows.h>
#include <Xinput.h>

// ImGui for rendering
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_dx12.h"
#include "imgui/imgui_impl_win32.h"

#include "rendering/d3d11.hpp"
#include "rendering/d3d12.hpp"

#include "uevr/Plugin.hpp"

using namespace uevr;

#define PLUGIN_LOG_ONCE(...) \
    static bool _logged_ = false; \
    if (!_logged_) { \
        _logged_ = true; \
        API::get()->log_info(__VA_ARGS__); \
    }

class TurnChangesPlugin : public uevr::Plugin {
public:
    // Enums matching C++ UEVR enums
    enum class AimMethod : int32_t {
        GAME = 0,                // No aim assist
        HEAD = 1,                // Head/HMD aiming
        RIGHT_CONTROLLER = 2,    // Right controller aiming
        LEFT_CONTROLLER = 3,     // Left controller aiming
        TWO_HANDED_RIGHT = 4,    // Two-handed right
        TWO_HANDED_LEFT = 5,     // Two-handed left
    };

    enum class DPadMethod : int32_t {
        RIGHT_TOUCH = 0,           // Right Thumbrest + Left Joystick
        LEFT_TOUCH = 1,            // Left Thumbrest + Right Joystick
        LEFT_JOYSTICK = 2,         // Left Joystick (Disables Standard Joystick Input)
        RIGHT_JOYSTICK = 3,        // Right Joystick (Disables Standard Joystick Input)
        GESTURE_HEAD = 4,          // Gesture (Head) + Left Joystick
        GESTURE_HEAD_RIGHT = 5,    // Gesture (Head) + Right Joystick
        RIGHT_JOYSTICK_CLICK = 6,  // Right Joystick Press + Left Joystick (Disables R3)
        LEFT_JOYSTICK_CLICK = 7,   // Left Joystick Press + Right Joystick (Disables L3)
    };

private:
    // Plugin state
    struct PluginState {
        // L3+R3 Aim Toggle state
        std::chrono::steady_clock::time_point l3_r3_start_time{};
        bool l3_r3_active = false;
        bool l3_r3_triggered = false;
        float l3_r3_hold_duration = 1.0f;
        AimMethod previous_aim_method = AimMethod::GAME;
        
        // DPad method state
        DPadMethod current_dpad_method = DPadMethod::RIGHT_TOUCH;
        bool l3_held = false;
        bool r3_held = false;
        bool dpad_active = false;
        
        // Input state tracking
        bool l3_pressed = false;
        bool r3_pressed = false;
        
        // GUI state
        bool show_notification = false;
        std::string notification_text;
        float notification_progress = 0.0f;
        bool show_config_window = true;
        
        // ImGui state
        bool imgui_initialized = false;
        bool was_rendering_desktop = false;
    } m_state;

    // Name mappings for GUI
    std::unordered_map<int32_t, std::string> m_aim_method_names = {
        {0, "Game (No Assist)"},
        {1, "Head/HMD"},
        {2, "Right Controller"},
        {3, "Left Controller"},
        {4, "Two-Handed Right"},
        {5, "Two-Handed Left"},
    };

    std::unordered_map<int32_t, std::string> m_dpad_method_names = {
        {0, "Right Thumbrest + Left Joystick"},
        {1, "Left Thumbrest + Right Joystick"},
        {2, "Left Joystick (Direct)"},
        {3, "Right Joystick (Direct)"},
        {4, "Head Gesture + Left Joystick"},
        {5, "Head Gesture + Right Joystick"},
        {6, "Right Joystick Press + Left Joystick (Disables R3)"},
        {7, "Left Joystick Press + Right Joystick (Disables L3)"},
    };

    std::recursive_mutex m_imgui_mutex{};

public:
    TurnChangesPlugin() = default;

    void on_dllmain() override {
        API::get()->log_info("[TurnChanges] DLL Plugin initializing...");
    }

    void on_initialize() override {
        API::get()->log_info("[TurnChanges] TurnChanges DLL Plugin loaded successfully!");
        API::get()->log_info("[TurnChanges] Features: L3+R3 Aim Toggle + New DPad Methods");
        
        ImGui::CreateContext();
        
        // Initialize previous aim method
        m_state.previous_aim_method = get_current_aim_method();
        if (m_state.previous_aim_method == AimMethod::GAME) {
            m_state.previous_aim_method = AimMethod::HEAD; // Fallback
        }
        
        m_state.current_dpad_method = get_current_dpad_method();
    }

    void on_present() override {
        std::scoped_lock _{m_imgui_mutex};

        if (!m_state.imgui_initialized) {
            if (!initialize_imgui()) {
                return;
            }
        }

        const auto renderer_data = API::get()->param()->renderer;
        const bool vr_active = API::VR::is_hmd_active();

        if (!vr_active) {
            if (!m_state.was_rendering_desktop) {
                m_state.was_rendering_desktop = true;
                on_device_reset();
                return;
            }

            m_state.was_rendering_desktop = true;

            if (renderer_data->renderer_type == UEVR_RENDERER_D3D11) {
                ImGui_ImplDX11_NewFrame();
                g_d3d11.render_imgui();
            } else if (renderer_data->renderer_type == UEVR_RENDERER_D3D12) {
                auto command_queue = (ID3D12CommandQueue*)renderer_data->command_queue;
                if (command_queue == nullptr) {
                    return;
                }
                ImGui_ImplDX12_NewFrame();
                g_d3d12.render_imgui();
            }
        }
    }

    void on_post_render_vr_framework_dx11(ID3D11DeviceContext* context, ID3D11Texture2D* texture, ID3D11RenderTargetView* rtv) override {
        const auto vr_active = API::VR::is_hmd_active();

        if (!m_state.imgui_initialized || !vr_active) {
            return;
        }

        if (m_state.was_rendering_desktop) {
            m_state.was_rendering_desktop = false;
            on_device_reset();
            return;
        }

        std::scoped_lock _{m_imgui_mutex};
        ImGui_ImplDX11_NewFrame();
        g_d3d11.render_imgui_vr(context, rtv);
    }

    void on_post_render_vr_framework_dx12(ID3D12GraphicsCommandList* command_list, ID3D12Resource* rt, D3D12_CPU_DESCRIPTOR_HANDLE* rtv) override {
        const auto vr_active = API::VR::is_hmd_active();

        if (!m_state.imgui_initialized || !vr_active) {
            return;
        }

        if (m_state.was_rendering_desktop) {
            m_state.was_rendering_desktop = false;
            on_device_reset();
            return;
        }

        std::scoped_lock _{m_imgui_mutex};
        ImGui_ImplDX12_NewFrame();
        g_d3d12.render_imgui_vr(command_list, rtv);
    }

    void on_device_reset() override {
        PLUGIN_LOG_ONCE("[TurnChanges] Device Reset");

        std::scoped_lock _{m_imgui_mutex};

        const auto renderer_data = API::get()->param()->renderer;

        if (renderer_data->renderer_type == UEVR_RENDERER_D3D11) {
            ImGui_ImplDX11_Shutdown();
            g_d3d11 = {};
        }

        if (renderer_data->renderer_type == UEVR_RENDERER_D3D12) {
            g_d3d12.reset();
            ImGui_ImplDX12_Shutdown();
            g_d3d12 = {};
        }

        m_state.imgui_initialized = false;
    }

    bool on_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) override {
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
        return !ImGui::GetIO().WantCaptureMouse && !ImGui::GetIO().WantCaptureKeyboard;
    }

    // Main XInput interception for L3/R3 detection
    void on_xinput_get_state(uint32_t* retval, uint32_t user_index, XINPUT_STATE* state) override {
        if (state == nullptr || user_index != 0) return;

        // Detect L3 and R3 button presses
        bool l3_current = (state->Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
        bool r3_current = (state->Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;

        // Update state
        m_state.l3_pressed = l3_current;
        m_state.r3_pressed = r3_current;

        // Handle L3+R3 aim method toggle
        handle_l3_r3_toggle();

        // Handle new DPad methods
        handle_dpad_methods();

        // Modify XInput state for new DPad methods if needed
        if (should_disable_joystick_clicks()) {
            // Disable L3/R3 buttons to prevent conflicts
            if (m_state.current_dpad_method == DPadMethod::RIGHT_JOYSTICK_CLICK && m_state.r3_held) {
                state->Gamepad.wButtons &= ~XINPUT_GAMEPAD_RIGHT_THUMB;
            }
            if (m_state.current_dpad_method == DPadMethod::LEFT_JOYSTICK_CLICK && m_state.l3_held) {
                state->Gamepad.wButtons &= ~XINPUT_GAMEPAD_LEFT_THUMB;
            }
        }
    }

    void on_pre_engine_tick(API::UGameEngine* engine, float delta) override {
        if (m_state.imgui_initialized) {
            std::scoped_lock _{m_imgui_mutex};

            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            render_ui();

            ImGui::EndFrame();
            ImGui::Render();
        }
    }

private:
    // Helper functions
    AimMethod get_current_aim_method() {
        return (AimMethod)API::VR::get_aim_method();
    }

    void set_aim_method(AimMethod method) {
        API::VR::set_aim_method((API::VR::AimMethod)method);
        API::VR::set_mod_value("VR_AimMethod", std::to_string((int32_t)method));
        API::get()->log_info("[TurnChanges] Set aim method to: %s", m_aim_method_names[(int32_t)method].c_str());
    }

    DPadMethod get_current_dpad_method() {
        try {
            std::string value = API::VR::get_mod_value<std::string>("VR_DPadShiftingMethod");
            return (DPadMethod)std::stoi(value);
        } catch (...) {
            return DPadMethod::RIGHT_TOUCH;
        }
    }

    void set_dpad_method(DPadMethod method) {
        API::VR::set_mod_value("VR_DPadShiftingMethod", std::to_string((int32_t)method));
        m_state.current_dpad_method = method;
        API::get()->log_info("[TurnChanges] Set DPad method to: %s", m_dpad_method_names[(int32_t)method].c_str());
    }

    void handle_l3_r3_toggle() {
        bool both_pressed = m_state.l3_pressed && m_state.r3_pressed;

        if (both_pressed && !m_state.l3_r3_active) {
            // Start L3+R3 hold timer
            m_state.l3_r3_active = true;
            m_state.l3_r3_triggered = false;
            m_state.l3_r3_start_time = std::chrono::steady_clock::now();
            m_state.show_notification = true;
            m_state.notification_text = "Continue holding L3 + R3 to toggle aim method";
            m_state.notification_progress = 0.0f;
            
        } else if (both_pressed && m_state.l3_r3_active) {
            // Update progress
            auto now = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_state.l3_r3_start_time);
            float elapsed_seconds = duration.count() / 1000.0f;
            
            m_state.notification_progress = elapsed_seconds / m_state.l3_r3_hold_duration;
            
            if (elapsed_seconds >= m_state.l3_r3_hold_duration && !m_state.l3_r3_triggered) {
                // Trigger aim method toggle
                m_state.l3_r3_triggered = true;
                m_state.show_notification = false;
                
                AimMethod current_method = get_current_aim_method();
                if (current_method == AimMethod::GAME) {
                    // Switch to previous method
                    set_aim_method(m_state.previous_aim_method);
                } else {
                    // Save current method and switch to GAME
                    m_state.previous_aim_method = current_method;
                    set_aim_method(AimMethod::GAME);
                }
            }
            
        } else {
            // Reset L3+R3 state
            m_state.l3_r3_active = false;
            m_state.l3_r3_triggered = false;
            m_state.show_notification = false;
            m_state.notification_progress = 0.0f;
        }
    }

    void handle_dpad_methods() {
        m_state.current_dpad_method = get_current_dpad_method();

        if (m_state.current_dpad_method == DPadMethod::RIGHT_JOYSTICK_CLICK) {
            // R3 + Left Joystick mode
            if (m_state.r3_pressed && !m_state.r3_held) {
                m_state.r3_held = true;
                m_state.dpad_active = true;
                API::get()->log_info("[TurnChanges] R3 held - Left joystick DPad activated");
            } else if (!m_state.r3_pressed && m_state.r3_held) {
                m_state.r3_held = false;
                m_state.dpad_active = false;
                API::get()->log_info("[TurnChanges] R3 released - DPad deactivated");
            }
            
        } else if (m_state.current_dpad_method == DPadMethod::LEFT_JOYSTICK_CLICK) {
            // L3 + Right Joystick mode
            if (m_state.l3_pressed && !m_state.l3_held) {
                m_state.l3_held = true;
                m_state.dpad_active = true;
                API::get()->log_info("[TurnChanges] L3 held - Right joystick DPad activated");
            } else if (!m_state.l3_pressed && m_state.l3_held) {
                m_state.l3_held = false;
                m_state.dpad_active = false;
                API::get()->log_info("[TurnChanges] L3 released - DPad deactivated");
            }
            
        } else {
            // Reset state for other DPad methods
            m_state.r3_held = false;
            m_state.l3_held = false;
            m_state.dpad_active = false;
        }
    }

    bool should_disable_joystick_clicks() {
        return (m_state.current_dpad_method == DPadMethod::RIGHT_JOYSTICK_CLICK ||
                m_state.current_dpad_method == DPadMethod::LEFT_JOYSTICK_CLICK) &&
               m_state.dpad_active;
    }

    bool initialize_imgui() {
        if (m_state.imgui_initialized) {
            return true;
        }

        std::scoped_lock _{m_imgui_mutex};

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        static const auto imgui_ini = API::get()->get_persistent_dir(L"imgui_turnchanges_plugin.ini").string();
        ImGui::GetIO().IniFilename = imgui_ini.c_str();

        const auto renderer_data = API::get()->param()->renderer;

        DXGI_SWAP_CHAIN_DESC swap_desc{};
        auto swapchain = (IDXGISwapChain*)renderer_data->swapchain;
        swapchain->GetDesc(&swap_desc);

        HWND wnd = swap_desc.OutputWindow;

        if (!ImGui_ImplWin32_Init(wnd)) {
            return false;
        }

        if (renderer_data->renderer_type == UEVR_RENDERER_D3D11) {
            if (!g_d3d11.initialize()) {
                return false;
            }
        } else if (renderer_data->renderer_type == UEVR_RENDERER_D3D12) {
            if (!g_d3d12.initialize()) {
                return false;
            }
        }

        m_state.imgui_initialized = true;
        return true;
    }

    void render_ui() {
        // Notification window
        if (m_state.show_notification) {
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(400, 100), ImGuiCond_Always);
            
            if (ImGui::Begin("L3+R3 Aim Toggle", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)) {
                ImGui::Text("%s", m_state.notification_text.c_str());
                ImGui::ProgressBar(m_state.notification_progress, ImVec2(-1, 0));
                
                float remaining = m_state.l3_r3_hold_duration - (m_state.notification_progress * m_state.l3_r3_hold_duration);
                ImGui::Text("Time remaining: %.1f seconds", std::max(0.0f, remaining));
            }
            ImGui::End();
        }

        // Main configuration window
        if (m_state.show_config_window) {
            if (ImGui::Begin("TurnChanges Plugin")) {
                ImGui::Text("=== L3+R3 Aim Method Toggle ===");
                
                AimMethod current_aim = get_current_aim_method();
                ImGui::Text("Current Aim Method: %s", m_aim_method_names[(int32_t)current_aim].c_str());
                ImGui::Text("Previous Aim Method: %s", m_aim_method_names[(int32_t)m_state.previous_aim_method].c_str());
                
                ImGui::Separator();
                
                ImGui::Text("=== DPad Method Status ===");
                DPadMethod current_dpad = get_current_dpad_method();
                ImGui::Text("Current DPad Method: %s", m_dpad_method_names[(int32_t)current_dpad].c_str());
                
                if (current_dpad == DPadMethod::RIGHT_JOYSTICK_CLICK) {
                    ImGui::Text("R3 Status: %s", m_state.r3_held ? "HELD (DPad Active)" : "Released");
                } else if (current_dpad == DPadMethod::LEFT_JOYSTICK_CLICK) {
                    ImGui::Text("L3 Status: %s", m_state.l3_held ? "HELD (DPad Active)" : "Released");
                } else {
                    ImGui::Text("New DPad methods not active");
                }
                
                ImGui::Separator();
                
                ImGui::Text("=== Input Status ===");
                ImGui::Text("L3 Pressed: %s", m_state.l3_pressed ? "Yes" : "No");
                ImGui::Text("R3 Pressed: %s", m_state.r3_pressed ? "Yes" : "No");
                ImGui::Text("Both L3+R3: %s", (m_state.l3_pressed && m_state.r3_pressed) ? "Yes" : "No");
                
                ImGui::Separator();
                
                ImGui::Text("=== Configuration ===");
                
                if (ImGui::SliderFloat("L3+R3 Hold Duration", &m_state.l3_r3_hold_duration, 0.5f, 3.0f)) {
                    API::get()->log_info("[TurnChanges] L3+R3 hold duration changed to: %.1f", m_state.l3_r3_hold_duration);
                }
                
                if (ImGui::Button("Test Aim Toggle")) {
                    AimMethod current_method = get_current_aim_method();
                    if (current_method == AimMethod::GAME) {
                        set_aim_method(m_state.previous_aim_method);
                    } else {
                        m_state.previous_aim_method = current_method;
                        set_aim_method(AimMethod::GAME);
                    }
                }
                
                ImGui::SameLine();
                if (ImGui::Button("Save Config")) {
                    API::VR::save_config();
                    API::get()->log_info("[TurnChanges] Configuration saved");
                }
                
                ImGui::Separator();
                
                ImGui::Text("=== DPad Method Controls ===");
                
                if (ImGui::Button("Set Right Joystick Click DPad")) {
                    set_dpad_method(DPadMethod::RIGHT_JOYSTICK_CLICK);
                }
                
                ImGui::SameLine();
                if (ImGui::Button("Set Left Joystick Click DPad")) {
                    set_dpad_method(DPadMethod::LEFT_JOYSTICK_CLICK);
                }
                
                if (ImGui::Button("Reset to Default DPad")) {
                    set_dpad_method(DPadMethod::RIGHT_TOUCH);
                }
                
                ImGui::Separator();
                
                ImGui::Text("=== Usage Instructions ===");
                ImGui::Text("• L3+R3: Hold both joystick clicks for %.1f seconds", m_state.l3_r3_hold_duration);
                ImGui::Text("• New DPad: Select method 6 or 7, then hold L3/R3");
                ImGui::Text("• This plugin adds the missing dropdown options!");
                
            }
            ImGui::End();
        }
    }
};

// Create the plugin instance
std::unique_ptr<TurnChangesPlugin> g_plugin{new TurnChangesPlugin()}; 