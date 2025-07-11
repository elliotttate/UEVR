/*
UEVR TurnChanges Plugin - Replicates TurnChangesToPlugin functionality
Based on actual TurnChangesToPlugin implementation analysis

Features:
1. L3+R3 Aim Method Toggle (hold 1 second)
2. New DPad Methods: RIGHT_JOYSTICK_CLICK (6) and LEFT_JOYSTICK_CLICK (7)
3. Proper XInput handling with conflict prevention
*/

#include <memory>
#include <chrono>
#include <unordered_map>
#include <string>

#include <Windows.h>
#include <Xinput.h>

#include "uevr/Plugin.hpp"

using namespace uevr;

class TurnChangesPlugin : public uevr::Plugin {
private:
    // Aim method enum matching UEVR
    enum class AimMethod : int32_t {
        GAME = 0,
        HEAD = 1,
        RIGHT_CONTROLLER = 2,
        LEFT_CONTROLLER = 3,
        TWO_HANDED_RIGHT = 4,
        TWO_HANDED_LEFT = 5
    };

    // DPad method enum with new methods
    enum class DPadMethod : int32_t {
        RIGHT_TOUCH = 0,
        LEFT_TOUCH = 1,
        LEFT_JOYSTICK = 2,
        RIGHT_JOYSTICK = 3,
        GESTURE_HEAD = 4,
        GESTURE_HEAD_RIGHT = 5,
        RIGHT_JOYSTICK_CLICK = 6,    // NEW: Right Joystick Press + Left Joystick (Disables R3)
        LEFT_JOYSTICK_CLICK = 7      // NEW: Left Joystick Press + Right Joystick (Disables L3)
    };

    // State tracking (similar to TurnChangesToPlugin's XInputContext)
    struct PluginState {
        // L3+R3 aim toggle state
        bool headlocked_begin_held = false;
        std::chrono::steady_clock::time_point headlocked_begin{};
        AimMethod previous_aim_method = AimMethod::HEAD;
        
        // DPad method state
        DPadMethod current_dpad_method = DPadMethod::RIGHT_TOUCH;
        bool left_joystick_click_down = false;
        bool right_joystick_click_down = false;
        
        // Button state tracking
        bool l3_pressed = false;
        bool r3_pressed = false;
        bool l3_was_pressed = false;
        bool r3_was_pressed = false;
        
        // VR controller axis storage
        float left_stick_x = 0.0f;
        float left_stick_y = 0.0f;
        float right_stick_x = 0.0f;
        float right_stick_y = 0.0f;
        
        // Notification state
        bool show_notification = false;
        std::string notification_text;
    } m_state;

    // Method name lookup for logging
    std::unordered_map<int32_t, std::string> m_aim_method_names = {
        {0, "Game"}, {1, "Head"}, {2, "Right Controller"},
        {3, "Left Controller"}, {4, "Two-Handed Right"}, {5, "Two-Handed Left"}
    };

    std::unordered_map<int32_t, std::string> m_dpad_method_names = {
        {0, "Right Thumbrest + Left Joystick"},
        {1, "Left Thumbrest + Right Joystick"},
        {2, "Left Joystick (Disables Standard Joystick Input)"},
        {3, "Right Joystick (Disables Standard Joystick Input)"},
        {4, "Gesture (Head) + Left Joystick"},
        {5, "Gesture (Head) + Right Joystick"},
        {6, "Right Joystick Press + Left Joystick (Disables R3)"},
        {7, "Left Joystick Press + Right Joystick (Disables L3)"}
    };

public:
    TurnChangesPlugin() = default;

    void on_initialize() override {
        API::get()->log_info("[TurnChanges] Plugin initialized");
        API::get()->log_info("[TurnChanges] Features: L3+R3 Aim Toggle + New DPad Methods (6&7)");
        
        // Initialize state
        m_state.previous_aim_method = get_current_aim_method();
        if (m_state.previous_aim_method == AimMethod::GAME) {
            m_state.previous_aim_method = AimMethod::HEAD;
        }
        
        m_state.current_dpad_method = get_current_dpad_method();
    }

    void on_xinput_get_state(uint32_t* retval, uint32_t user_index, XINPUT_STATE* state) override {
        if (state == nullptr || user_index != 0) return;

        // Track button state changes
        m_state.l3_was_pressed = m_state.l3_pressed;
        m_state.r3_was_pressed = m_state.r3_pressed;
        m_state.l3_pressed = (state->Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
        m_state.r3_pressed = (state->Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;

        // Handle L3+R3 aim method toggle
        handle_l3_r3_aim_toggle(state);

        // Handle new DPad methods
        handle_dpad_methods(state);
    }

    void on_present() override {
        // Update DPad method if it changed
        m_state.current_dpad_method = get_current_dpad_method();
        
        // Draw notification if needed
        if (m_state.show_notification) {
            draw_notification();
        }
    }

private:
    AimMethod get_current_aim_method() {
        return static_cast<AimMethod>(API::VR::get_aim_method());
    }

    void set_aim_method(AimMethod method) {
        API::VR::set_aim_method(static_cast<API::VR::AimMethod>(method));
        API::VR::set_mod_value("VR_AimMethod", static_cast<int32_t>(method));
        
        auto it = m_aim_method_names.find(static_cast<int32_t>(method));
        if (it != m_aim_method_names.end()) {
            API::get()->log_info("[TurnChanges] Aim method changed to: %s", it->second.c_str());
        }
    }

    DPadMethod get_current_dpad_method() {
        try {
            std::string value = API::VR::get_mod_value<std::string>("VR_DPadShiftingMethod");
            return static_cast<DPadMethod>(std::stoi(value));
        } catch (...) {
            return DPadMethod::RIGHT_TOUCH;
        }
    }

    void handle_l3_r3_aim_toggle(XINPUT_STATE* state) {
        const bool both_pressed = m_state.l3_pressed && m_state.r3_pressed;
        const auto now = std::chrono::steady_clock::now();

        if (both_pressed && !m_state.headlocked_begin_held) {
            // Start L3+R3 hold timer
            m_state.headlocked_begin_held = true;
            m_state.headlocked_begin = now;
            m_state.show_notification = true;
            m_state.notification_text = "Continue holding down L3 + R3 to toggle aim method";
            
            // Save current aim method for restoration
            AimMethod current = get_current_aim_method();
            if (current != AimMethod::GAME) {
                m_state.previous_aim_method = current;
            } else if (m_state.previous_aim_method == AimMethod::GAME) {
                m_state.previous_aim_method = AimMethod::HEAD;
            }
            
            API::get()->log_info("[TurnChanges] L3+R3 hold started, aim toggle pending...");
            
        } else if (both_pressed && m_state.headlocked_begin_held) {
            // Check if held for 1 second
            if (now - m_state.headlocked_begin >= std::chrono::seconds(1)) {
                // Toggle aim method
                AimMethod current = get_current_aim_method();
                if (current == AimMethod::GAME) {
                    set_aim_method(m_state.previous_aim_method);
                } else {
                    set_aim_method(AimMethod::GAME);
                }
                
                // Reset state
                m_state.headlocked_begin_held = false;
                m_state.show_notification = false;
                
                API::get()->log_info("[TurnChanges] Aim method toggled successfully!");
            }
            
        } else if (!both_pressed && m_state.headlocked_begin_held) {
            // Released early, cancel toggle
            m_state.headlocked_begin_held = false;
            m_state.show_notification = false;
            API::get()->log_info("[TurnChanges] L3+R3 released early, aim toggle cancelled");
        }
    }

    void handle_dpad_methods(XINPUT_STATE* state) {
        // Update joystick click states
        m_state.left_joystick_click_down = m_state.l3_pressed;
        m_state.right_joystick_click_down = m_state.r3_pressed;

        // Get current joystick axes (we'll simulate this for now)
        float left_x = state->Gamepad.sThumbLX / 32768.0f;
        float left_y = state->Gamepad.sThumbLY / 32768.0f;
        float right_x = state->Gamepad.sThumbRX / 32768.0f;
        float right_y = state->Gamepad.sThumbRY / 32768.0f;

        bool dpad_active = false;
        bool use_left_stick = false;

        // Check if we're using the new DPad methods
        if (m_state.current_dpad_method == DPadMethod::RIGHT_JOYSTICK_CLICK) {
            dpad_active = m_state.right_joystick_click_down && !m_state.left_joystick_click_down;
            use_left_stick = true;
            
        } else if (m_state.current_dpad_method == DPadMethod::LEFT_JOYSTICK_CLICK) {
            dpad_active = m_state.left_joystick_click_down && !m_state.right_joystick_click_down;
            use_left_stick = false;
        }

        if (dpad_active) {
            // Convert joystick to DPad
            float tx = use_left_stick ? left_x : right_x;
            float ty = use_left_stick ? left_y : right_y;

            // Clear existing DPad state
            state->Gamepad.wButtons &= ~(XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | 
                                       XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT);

            // Apply DPad based on joystick
            if (ty >= 0.5f) {
                state->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP;
            }
            if (ty <= -0.5f) {
                state->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
            }
            if (tx >= 0.5f) {
                state->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
            }
            if (tx <= -0.5f) {
                state->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
            }

            // Disable the joystick click to prevent conflicts
            if (m_state.current_dpad_method == DPadMethod::RIGHT_JOYSTICK_CLICK) {
                state->Gamepad.wButtons &= ~XINPUT_GAMEPAD_RIGHT_THUMB;
            } else if (m_state.current_dpad_method == DPadMethod::LEFT_JOYSTICK_CLICK) {
                state->Gamepad.wButtons &= ~XINPUT_GAMEPAD_LEFT_THUMB;
            }

            // Zero out the joystick values being used for DPad
            if (use_left_stick) {
                state->Gamepad.sThumbLX = 0;
                state->Gamepad.sThumbLY = 0;
            } else {
                state->Gamepad.sThumbRX = 0;
                state->Gamepad.sThumbRY = 0;
            }
        }
    }

    void draw_notification() {
        // Simple notification display
        // Note: This is a simplified version - the full implementation would need proper ImGui setup
        if (m_state.show_notification && !m_state.notification_text.empty()) {
            // In a real implementation, this would draw an ImGui window
            // For now, we'll just log the progress
            static auto last_log = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            
            if (now - last_log >= std::chrono::milliseconds(200)) {
                float elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - m_state.headlocked_begin).count() / 1000.0f;
                
                API::get()->log_info("[TurnChanges] L3+R3 hold progress: %.1f/1.0 seconds", elapsed);
                last_log = now;
            }
        }
    }
};

// Create the plugin instance
std::unique_ptr<TurnChangesPlugin> g_plugin{new TurnChangesPlugin()}; 