#include <uevr/Plugin.hpp>
#include <string>

using namespace uevr;

class ImGuiHookPlugin : public Plugin {
private:
    int current_dpad_method = 0;
    uint64_t last_check_time = 0;
    bool initialized = false;
    
    void log_info(const std::string& message) {
        API::get()->log_info(("[ImGui Hook] " + message).c_str());
    }
    
    void update_current_method() {
        try {
            std::string value = API::VR::get_mod_value<std::string>("VR_DPadShiftingMethod");
            int new_method = std::stoi(value);
            if (new_method != current_dpad_method) {
                current_dpad_method = new_method;
                if (new_method >= 6 && new_method <= 7) {
                    log_info("Extended DPad method " + std::to_string(new_method) + " activated");
                }
            }
        } catch (...) {
            // Ignore config errors
        }
    }
    
public:
    void on_dllmain() override {
        // Called when DLL is loaded
    }
    
    void on_initialize() override {
        log_info("ImGui Hook Plugin initializing...");
        log_info("Extended DPad methods 6 & 7 are now available");
        log_info("Set VR_DPadShiftingMethod to 6 or 7 to use them");
        log_info("Method 6: R3 + Left Stick");
        log_info("Method 7: L3 + Right Stick");
        
        update_current_method();
        initialized = true;
        
        log_info("ImGui Hook Plugin initialized successfully!");
    }
    
    void on_present() override {
        if (!initialized) return;
        
        // Update current method periodically (every second)
        uint64_t current_time = GetTickCount64();
        if (current_time - last_check_time > 1000) {
            update_current_method();
            last_check_time = current_time;
        }
    }
    
    void on_xinput_get_state(uint32_t* retval, uint32_t user_index, XINPUT_STATE* state) override {
        if (!initialized || !state) return;
        
        if (current_dpad_method == 6) {
            // Method 6: R3 + Left Stick
            if (state->Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) {
                float lx = state->Gamepad.sThumbLX / 32767.0f;
                float ly = state->Gamepad.sThumbLY / 32767.0f;
                
                // Clear R3 to prevent conflicts
                state->Gamepad.wButtons &= ~XINPUT_GAMEPAD_RIGHT_THUMB;
                
                // Apply DPad based on left stick (50% deadzone)
                if (lx > 0.5f || lx < -0.5f || ly > 0.5f || ly < -0.5f) {
                    if (lx > 0.5f || lx < -0.5f) {
                        state->Gamepad.wButtons |= (lx > 0) ? XINPUT_GAMEPAD_DPAD_RIGHT : XINPUT_GAMEPAD_DPAD_LEFT;
                    } else {
                        state->Gamepad.wButtons |= (ly > 0) ? XINPUT_GAMEPAD_DPAD_UP : XINPUT_GAMEPAD_DPAD_DOWN;
                    }
                }
            }
        }
        else if (current_dpad_method == 7) {
            // Method 7: L3 + Right Stick  
            if (state->Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) {
                float rx = state->Gamepad.sThumbRX / 32767.0f;
                float ry = state->Gamepad.sThumbRY / 32767.0f;
                
                // Clear L3 to prevent conflicts
                state->Gamepad.wButtons &= ~XINPUT_GAMEPAD_LEFT_THUMB;
                
                // Apply DPad based on right stick (50% deadzone)
                if (rx > 0.5f || rx < -0.5f || ry > 0.5f || ry < -0.5f) {
                    if (rx > 0.5f || rx < -0.5f) {
                        state->Gamepad.wButtons |= (rx > 0) ? XINPUT_GAMEPAD_DPAD_RIGHT : XINPUT_GAMEPAD_DPAD_LEFT;
                    } else {
                        state->Gamepad.wButtons |= (ry > 0) ? XINPUT_GAMEPAD_DPAD_UP : XINPUT_GAMEPAD_DPAD_DOWN;
                    }
                }
            }
        }
    }
};

// Create global plugin instance
ImGuiHookPlugin g_plugin_instance; 