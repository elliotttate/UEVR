#include <windows.h>
#include <Xinput.h>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <d3d11.h>
#include <d3d12.h>

#include "imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_dx12.h"
#include "imgui/imgui_impl_win32.h"
#include "rendering/d3d11.hpp"
#include "rendering/d3d12.hpp"
#include "uevr/Plugin.hpp"

using namespace uevr;

static D3D11 g_d3d11;
static D3D12 g_d3d12;

struct ButtonInfo {
    uint16_t mask;
    const char* name;
    const char* display;
};

static const ButtonInfo g_buttons[] = {
    {XINPUT_GAMEPAD_A, "XINPUT_GAMEPAD_A", "A"},
    {XINPUT_GAMEPAD_B, "XINPUT_GAMEPAD_B", "B"},
    {XINPUT_GAMEPAD_X, "XINPUT_GAMEPAD_X", "X"},
    {XINPUT_GAMEPAD_Y, "XINPUT_GAMEPAD_Y", "Y"},
    {XINPUT_GAMEPAD_LEFT_SHOULDER, "XINPUT_GAMEPAD_LEFT_SHOULDER", "LB"},
    {XINPUT_GAMEPAD_RIGHT_SHOULDER, "XINPUT_GAMEPAD_RIGHT_SHOULDER", "RB"},
    {XINPUT_GAMEPAD_BACK, "XINPUT_GAMEPAD_BACK", "Back"},
    {XINPUT_GAMEPAD_START, "XINPUT_GAMEPAD_START", "Start"},
    {XINPUT_GAMEPAD_LEFT_THUMB, "XINPUT_GAMEPAD_LEFT_THUMB", "L3"},
    {XINPUT_GAMEPAD_RIGHT_THUMB, "XINPUT_GAMEPAD_RIGHT_THUMB", "R3"},
    {XINPUT_GAMEPAD_DPAD_UP, "XINPUT_GAMEPAD_DPAD_UP", "DPad Up"},
    {XINPUT_GAMEPAD_DPAD_DOWN, "XINPUT_GAMEPAD_DPAD_DOWN", "DPad Down"},
    {XINPUT_GAMEPAD_DPAD_LEFT, "XINPUT_GAMEPAD_DPAD_LEFT", "DPad Left"},
    {XINPUT_GAMEPAD_DPAD_RIGHT, "XINPUT_GAMEPAD_DPAD_RIGHT", "DPad Right"}
};

struct KeyInfo {
    WORD vk;
    const char* name;
    const char* display;
};

static const KeyInfo g_keys[] = {
    {VK_SPACE, "VK_SPACE", "Space"},
    {VK_RETURN, "VK_RETURN", "Enter"},
    {VK_ESCAPE, "VK_ESCAPE", "Escape"},
    {VK_TAB, "VK_TAB", "Tab"},
    {VK_CONTROL, "VK_CONTROL", "Ctrl"},
    {VK_SHIFT, "VK_SHIFT", "Shift"},
    {VK_MENU, "VK_MENU", "Alt"},
    {VK_F1, "VK_F1", "F1"},
    {VK_F2, "VK_F2", "F2"},
    {VK_F3, "VK_F3", "F3"},
    {VK_F4, "VK_F4", "F4"},
    {VK_F5, "VK_F5", "F5"},
    {VK_F6, "VK_F6", "F6"},
    {VK_F7, "VK_F7", "F7"},
    {VK_F8, "VK_F8", "F8"},
    {VK_F9, "VK_F9", "F9"},
    {VK_F10, "VK_F10", "F10"},
    {VK_F11, "VK_F11", "F11"},
    {VK_F12, "VK_F12", "F12"},
    {'Q', "Q", "Q"},
    {'W', "W", "W"},
    {'E', "E", "E"},
    {'R', "R", "R"},
    {'T', "T", "T"},
    {'Y', "Y", "Y"},
    {'U', "U", "U"},
    {'I', "I", "I"},
    {'O', "O", "O"},
    {'P', "P", "P"},
    {'A', "A", "A"},
    {'S', "S", "S"},
    {'D', "D", "D"},
    {'F', "F", "F"},
    {'G', "G", "G"},
    {'H', "H", "H"},
    {'J', "J", "J"},
    {'K', "K", "K"},
    {'L', "L", "L"},
    {'Z', "Z", "Z"},
    {'X', "X", "X"},
    {'C', "C", "C"},
    {'V', "V", "V"},
    {'B', "B", "B"},
    {'N', "N", "N"},
    {'M', "M", "M"},
    {'1', "1", "1"},
    {'2', "2", "2"},
    {'3', "3", "3"},
    {'4', "4", "4"},
    {'5', "5", "5"},
    {'6', "6", "6"},
    {'7', "7", "7"},
    {'8', "8", "8"},
    {'9', "9", "9"},
    {'0', "0", "0"}
};

enum class GestureType {
    None,
    ControllerUpsideDown,
    FlickLeft,
    FlickRight,
    FlickUp,
    FlickDown,
    TwistClockwise,
    TwistCounterClockwise,
    Shake,
    Jab,
    Hold
};

struct VRControllerState {
    UEVR_Vector3f position{};
    UEVR_Quaternionf rotation{};
    UEVR_Vector3f last_position{};
    UEVR_Quaternionf last_rotation{};
    UEVR_Vector3f velocity{};
    UEVR_Vector3f angular_velocity{};
    float last_update_time = 0.0f;
    bool is_valid = false;
};

struct GestureDetection {
    GestureType type = GestureType::None;
    float threshold = 0.5f;
    float hold_time = 0.5f;  // For hold gestures
    float cooldown = 0.3f;   // Prevent repeated triggers
    float last_trigger_time = 0.0f;
    bool is_active = false;
    float activation_start_time = 0.0f;
};

struct Mapping {
    std::vector<uint16_t> combo_buttons;
    std::optional<GestureType> gesture_type;  // New: gesture trigger
    bool use_left_controller = false;  // Which controller for gesture
    std::optional<uint16_t> output_button;
    std::optional<WORD> output_key;
    bool was_pressed = false;
    bool enabled = true;
    bool block_original = true;
    std::string name;
};

class ButtonRemapperPlugin : public Plugin {
public:
    ButtonRemapperPlugin() = default;

    void on_initialize() override {
        API::get()->log_info("[ButtonRemapper] Initializing...");
        m_initialized = false;
        m_show_ui = true;  // Start with UI visible
        m_config_dirty = false;
        m_selected_mapping = -1;
        m_recording_combo = false;
        m_last_buttons = 0;
        m_show_ui_hotkey = VK_F9;  // Default hotkey for showing UI
        m_first_present = true;
        initialize_gestures();
        load_config();
        API::get()->log_info("[ButtonRemapper] Initialization complete. UI should be visible. Press F9 to toggle.");
    }

    void on_present() override {
        // NOTE: Due to a bug in UEVR where PluginLoader::on_present() is missing the override keyword,
        // this callback may not be called when the UEVR overlay is closed. This is a known issue
        // in UEVR that needs to be fixed in the framework itself.
        // Bug: PluginLoader.hpp line 34 needs 'override' keyword added to on_present()
        
        if (m_first_present) {
            API::get()->log_info("[ButtonRemapper] First on_present call - if UI disappears when UEVR closes, it's due to a UEVR framework bug");
            m_first_present = false;
        }
        
        std::scoped_lock _{m_imgui_mutex};
        if (!m_initialized) {
            if (!initialize_imgui()) {
                API::get()->log_error("[ButtonRemapper] Failed to initialize ImGui");
                return;
            }
        }
        
        const auto renderer_data = API::get()->param()->renderer;
        ImGui_ImplWin32_NewFrame();
        if (renderer_data->renderer_type == UEVR_RENDERER_D3D11) {
            ImGui_ImplDX11_NewFrame();
        } else if (renderer_data->renderer_type == UEVR_RENDERER_D3D12) {
            ImGui_ImplDX12_NewFrame();
        }
        ImGui::NewFrame();
        
        draw_ui();
        
        ImGui::EndFrame();
        ImGui::Render();
        if (renderer_data->renderer_type == UEVR_RENDERER_D3D11) {
            g_d3d11.render_imgui();
        } else if (renderer_data->renderer_type == UEVR_RENDERER_D3D12) {
            g_d3d12.render_imgui();
        }
    }

    void on_device_reset() override {
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
        m_initialized = false;
    }

    void on_post_render_vr_framework_dx11(ID3D11DeviceContext* ctx, ID3D11Texture2D* tex, ID3D11RenderTargetView* rtv) override {
        std::scoped_lock _{m_imgui_mutex};
        if (!m_initialized) {
            if (!initialize_imgui()) {
                return;
            }
        }
        
        ImGui_ImplWin32_NewFrame();
        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
        
        draw_ui();
        
        ImGui::EndFrame();
        ImGui::Render();
        g_d3d11.render_imgui_vr(ctx, rtv);
    }

    void on_post_render_vr_framework_dx12(ID3D12GraphicsCommandList* cmd_list, ID3D12Resource* tex, D3D12_CPU_DESCRIPTOR_HANDLE* rtv) override {
        std::scoped_lock _{m_imgui_mutex};
        if (!m_initialized) {
            if (!initialize_imgui()) {
                return;
            }
        }
        
        ImGui_ImplWin32_NewFrame();
        ImGui_ImplDX12_NewFrame();
        ImGui::NewFrame();
        
        draw_ui();
        
        ImGui::EndFrame();
        ImGui::Render();
        g_d3d12.render_imgui_vr(cmd_list, rtv);
    }

    void on_pre_engine_tick(API::UGameEngine* engine, float delta) override {
        // Check for UI toggle hotkey
        static bool hotkey_was_pressed = false;
        bool hotkey_pressed = (GetAsyncKeyState(m_show_ui_hotkey) & 0x8000) != 0;
        
        if (hotkey_pressed && !hotkey_was_pressed) {
            m_show_ui = !m_show_ui;
            API::get()->log_info("[ButtonRemapper] UI toggled to: %s", m_show_ui ? "visible" : "hidden");
        }
        hotkey_was_pressed = hotkey_pressed;
        
        // Update VR controller states if VR is active
        if (API::get()->param()->vr->is_runtime_ready()) {
            update_vr_controller_states(delta);
            detect_gestures(delta);
        }
    }

    bool on_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) override {
        if (m_initialized) {
            ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
            // Always pass through controller input so we can record combos
            // Only block mouse/keyboard when ImGui wants them
            if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) {
                return !ImGui::GetIO().WantCaptureMouse;
            }
            if (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) {
                return !ImGui::GetIO().WantCaptureKeyboard;
            }
        }
        return true;
    }

    void on_xinput_get_state(uint32_t* retval, uint32_t user_index, XINPUT_STATE* state) override {
        if (*retval != ERROR_SUCCESS || !state) {
            return;
        }

        std::scoped_lock _{m_mapping_mutex};

        // Store original button state
        uint16_t original_buttons = state->Gamepad.wButtons;
        
        // Update current buttons for combo recording
        m_current_buttons = original_buttons;
        
        // Handle UI navigation with controller when our UI is open
        if (m_show_ui && m_initialized) {
            handle_controller_navigation(state);
        }

        // Process button-based mappings
        for (auto& mapping : m_mappings) {
            if (!mapping.enabled) continue;
            if (mapping.gesture_type.has_value()) continue; // Skip gesture mappings here

            // Check if all buttons in the combo are pressed
            bool combo_pressed = true;
            for (uint16_t button : mapping.combo_buttons) {
                if (!(original_buttons & button)) {
                    combo_pressed = false;
                    break;
                }
            }

            // Handle button press/release
            if (combo_pressed && !mapping.was_pressed) {
                // Combo just pressed
                mapping.was_pressed = true;

                if (mapping.output_button) {
                    // Add the output button to the state
                    state->Gamepad.wButtons |= *mapping.output_button;
                }
                if (mapping.output_key) {
                    // Send key press
                    send_key(*mapping.output_key, true);
                }

                // Remove original combo buttons from state if block_original is enabled
                if (mapping.block_original) {
                    for (uint16_t button : mapping.combo_buttons) {
                        state->Gamepad.wButtons &= ~button;
                    }
                }
            } else if (!combo_pressed && mapping.was_pressed) {
                // Combo just released
                mapping.was_pressed = false;

                if (mapping.output_key) {
                    // Send key release
                    send_key(*mapping.output_key, false);
                }
            }
        }
    }

private:
    bool initialize_imgui() {
        if (m_initialized) return true;
        
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        
        const auto renderer_data = API::get()->param()->renderer;
        DXGI_SWAP_CHAIN_DESC swap_desc{};
        auto swapchain = (IDXGISwapChain*)renderer_data->swapchain;
        if (!swapchain || FAILED(swapchain->GetDesc(&swap_desc))) {
            return false;
        }
        m_wnd = swap_desc.OutputWindow;
        
        if (!ImGui_ImplWin32_Init(m_wnd)) {
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
        
        // Enable gamepad navigation
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        
        m_initialized = true;
        API::get()->log_info("[ButtonRemapper] ImGui initialized successfully with gamepad support");
        return true;
    }

    void draw_ui() {
        if (!m_show_ui) return;

        // Check if UEVR is drawing its own UI
        bool uevr_ui_active = API::get()->param()->functions->is_drawing_ui();
        
        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
        
        if (ImGui::Begin("Button Remapper Configuration##ButtonRemapper", &m_show_ui)) {
            // Header
            ImGui::Text("Configure button remappings for your controller");
            ImGui::Text("Press F9 to toggle this window (works even when UEVR overlay is closed)");
            if (API::get()->param()->vr->is_runtime_ready()) {
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "VR Mode Active - UI is rendered in headset");
            }
            if (uevr_ui_active) {
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "UEVR overlay is active");
            }
            ImGui::Separator();

            // Left panel - Mapping list
            ImGui::BeginChild("MappingList", ImVec2(300, -50), true);
            ImGui::Text("Mappings:");
            ImGui::Separator();

            for (int i = 0; i < m_mappings.size(); i++) {
                auto& mapping = m_mappings[i];
                
                ImGui::PushID(i);
                
                // Checkbox to enable/disable mapping
                ImGui::Checkbox("##enabled", &mapping.enabled);
                ImGui::SameLine();
                
                // Mapping name
                bool selected = (m_selected_mapping == i);
                if (ImGui::Selectable(mapping.name.c_str(), selected)) {
                    m_selected_mapping = i;
                }
                
                // Delete button
                ImGui::SameLine(250);
                if (ImGui::Button("X")) {
                    m_mappings.erase(m_mappings.begin() + i);
                    m_config_dirty = true;
                    if (m_selected_mapping >= m_mappings.size()) {
                        m_selected_mapping = m_mappings.size() - 1;
                    }
                    ImGui::PopID();
                    break;
                }
                
                ImGui::PopID();
            }

            ImGui::EndChild();

            // Right panel - Mapping editor
            ImGui::SameLine();
            ImGui::BeginChild("MappingEditor", ImVec2(0, -50), true);
            
            if (m_selected_mapping >= 0 && m_selected_mapping < m_mappings.size()) {
                auto& mapping = m_mappings[m_selected_mapping];
                
                ImGui::Text("Edit Mapping:");
                ImGui::Separator();
                
                // Mapping name
                char name_buf[256];
                strcpy_s(name_buf, mapping.name.c_str());
                if (ImGui::InputText("Name", name_buf, sizeof(name_buf))) {
                    mapping.name = name_buf;
                    m_config_dirty = true;
                }
                
                // Block original buttons option
                if (ImGui::Checkbox("Block original buttons when combo is active", &mapping.block_original)) {
                    m_config_dirty = true;
                }
                
                // Combo buttons
                ImGui::Text("Button Combination:");
                std::string combo_str = get_combo_string(mapping.combo_buttons);
                ImGui::Text("%s", combo_str.c_str());
                ImGui::SameLine();
                if (ImGui::Button(m_recording_combo ? "Stop Recording" : "Record Combo")) {
                    m_recording_combo = !m_recording_combo;
                    if (m_recording_combo) {
                        m_recorded_combo.clear();
                    } else if (!m_recorded_combo.empty()) {
                        mapping.combo_buttons = m_recorded_combo;
                        m_config_dirty = true;
                    }
                }
                
                if (m_recording_combo) {
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "Press buttons to record combination...");
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1), "(Close UEVR overlay if inputs aren't detected)");
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1), "Press B to cancel recording");
                    update_combo_recording();
                    
                    // Cancel recording with B button
                    if (m_current_buttons & XINPUT_GAMEPAD_B) {
                        m_recording_combo = false;
                        m_recorded_combo.clear();
                    }
                }
                
                ImGui::Separator();
                
                // Trigger type selection
                ImGui::Text("Trigger Type:");
                int trigger_type = 0;
                if (mapping.gesture_type.has_value()) trigger_type = 2;
                else if (!mapping.combo_buttons.empty()) trigger_type = 1;
                
                if (ImGui::RadioButton("None", trigger_type == 0)) {
                    mapping.combo_buttons.clear();
                    mapping.gesture_type.reset();
                    m_config_dirty = true;
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Button Combo", trigger_type == 1)) {
                    if (trigger_type != 1) {
                        mapping.combo_buttons.push_back(XINPUT_GAMEPAD_A);
                        mapping.gesture_type.reset();
                        m_config_dirty = true;
                    }
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("VR Gesture", trigger_type == 2)) {
                    if (trigger_type != 2) {
                        mapping.combo_buttons.clear();
                        mapping.gesture_type = GestureType::ControllerUpsideDown;
                        m_config_dirty = true;
                    }
                }
                
                // Show appropriate trigger configuration
                if (trigger_type == 1) {
                    // Button combo configuration is already shown above
                } else if (trigger_type == 2) {
                    // VR gesture configuration
                    ImGui::Text("VR Gesture:");
                    const char* current_gesture = gesture_to_string(mapping.gesture_type.value_or(GestureType::None));
                    if (ImGui::BeginCombo("##gesture", current_gesture)) {
                        for (int i = 1; i <= (int)GestureType::Hold; i++) {
                            GestureType gesture = (GestureType)i;
                            bool selected = mapping.gesture_type && *mapping.gesture_type == gesture;
                            if (ImGui::Selectable(gesture_to_string(gesture), selected)) {
                                mapping.gesture_type = gesture;
                                m_config_dirty = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    
                    ImGui::Checkbox("Use Left Controller", &mapping.use_left_controller);
                    if (ImGui::IsItemClicked()) m_config_dirty = true;
                    
                    // Show gesture status
                    if (mapping.gesture_type && API::get()->param()->vr->is_runtime_ready()) {
                        auto& gesture = m_gestures[*mapping.gesture_type];
                        if (gesture.is_active) {
                            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Gesture Active!");
                        } else {
                            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "Gesture Inactive");
                        }
                    }
                }
                
                ImGui::Separator();
                
                // Output type selection
                ImGui::Text("Output Type:");
                bool is_button = mapping.output_button.has_value();
                bool is_key = mapping.output_key.has_value();
                
                if (ImGui::RadioButton("Controller Button", is_button)) {
                    if (!is_button) {
                        mapping.output_button = XINPUT_GAMEPAD_A;
                        mapping.output_key.reset();
                        m_config_dirty = true;
                    }
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Keyboard Key", is_key)) {
                    if (!is_key) {
                        mapping.output_key = VK_SPACE;
                        mapping.output_button.reset();
                        m_config_dirty = true;
                    }
                }
                
                // Output selection
                if (mapping.output_button) {
                    ImGui::Text("Output Button:");
                    const char* current_button = button_to_display(*mapping.output_button);
                    if (ImGui::BeginCombo("##outputbutton", current_button)) {
                        for (const auto& btn : g_buttons) {
                            bool selected = (*mapping.output_button == btn.mask);
                            if (ImGui::Selectable(btn.display, selected)) {
                                mapping.output_button = btn.mask;
                                m_config_dirty = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                } else if (mapping.output_key) {
                    ImGui::Text("Output Key:");
                    const char* current_key = key_to_display(*mapping.output_key);
                    if (ImGui::BeginCombo("##outputkey", current_key)) {
                        for (const auto& key : g_keys) {
                            bool selected = (*mapping.output_key == key.vk);
                            if (ImGui::Selectable(key.display, selected)) {
                                mapping.output_key = key.vk;
                                m_config_dirty = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
            } else {
                ImGui::Text("Select a mapping to edit or create a new one");
            }
            
            ImGui::EndChild();

            // Bottom buttons
            if (ImGui::Button("New Mapping")) {
                Mapping new_mapping;
                new_mapping.name = "New Mapping " + std::to_string(m_mappings.size() + 1);
                new_mapping.combo_buttons.push_back(XINPUT_GAMEPAD_A);
                new_mapping.output_button = XINPUT_GAMEPAD_B;
                new_mapping.block_original = true;
                m_mappings.push_back(new_mapping);
                m_selected_mapping = m_mappings.size() - 1;
                m_config_dirty = true;
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Save Config")) {
                save_config();
                m_config_dirty = false;
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Load Config")) {
                load_config();
                m_config_dirty = false;
            }
            
            ImGui::Separator();
            ImGui::Text("Controller Navigation: DPad/Left Stick to navigate, A to select, B to cancel");
            
            if (API::get()->param()->vr->is_runtime_ready()) {
                ImGui::Separator();
                if (ImGui::CollapsingHeader("VR Gesture Settings")) {
                    ImGui::Text("Adjust gesture detection thresholds:");
                    
                    if (ImGui::SliderFloat("Flick Speed Threshold", &m_gestures[GestureType::FlickLeft].threshold, 1.0f, 10.0f)) {
                        m_gestures[GestureType::FlickRight].threshold = m_gestures[GestureType::FlickLeft].threshold;
                        m_gestures[GestureType::FlickUp].threshold = m_gestures[GestureType::FlickLeft].threshold;
                        m_gestures[GestureType::FlickDown].threshold = m_gestures[GestureType::FlickLeft].threshold;
                    }
                    
                    ImGui::SliderFloat("Jab Speed Threshold", &m_gestures[GestureType::Jab].threshold, 3.0f, 15.0f);
                    ImGui::SliderFloat("Shake Threshold", &m_gestures[GestureType::Shake].threshold, 3.0f, 10.0f);
                    ImGui::SliderFloat("Upside Down Threshold", &m_gestures[GestureType::ControllerUpsideDown].threshold, -1.0f, 0.0f);
                    ImGui::SliderFloat("Hold Time (seconds)", &m_gestures[GestureType::Hold].hold_time, 0.1f, 3.0f);
                    
                    ImGui::Separator();
                    ImGui::Text("Gesture Status:");
                    ImGui::Text("Left Controller:");
                    show_controller_status(m_left_controller, false);
                    ImGui::Text("Right Controller:");
                    show_controller_status(m_right_controller, true);
                }
            }
            
            if (m_config_dirty) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "Unsaved changes!");
            }
        }
        ImGui::End();
    }

    void update_combo_recording() {
        // Check for newly pressed buttons
        for (const auto& btn : g_buttons) {
            bool was_pressed = (m_last_buttons & btn.mask) != 0;
            bool is_pressed = (m_current_buttons & btn.mask) != 0;
            
            if (is_pressed && !was_pressed) {
                // Button just pressed
                bool already_in_combo = false;
                for (uint16_t b : m_recorded_combo) {
                    if (b == btn.mask) {
                        already_in_combo = true;
                        break;
                    }
                }
                
                if (!already_in_combo) {
                    m_recorded_combo.push_back(btn.mask);
                }
            }
        }
        
        // Update last buttons for next frame
        m_last_buttons = m_current_buttons;
    }

    std::string get_combo_string(const std::vector<uint16_t>& combo) {
        std::stringstream ss;
        for (size_t i = 0; i < combo.size(); i++) {
            if (i > 0) ss << " + ";
            ss << button_to_display(combo[i]);
        }
        return ss.str();
    }

    const char* button_to_display(uint16_t mask) {
        for (const auto& btn : g_buttons) {
            if (btn.mask == mask) return btn.display;
        }
        return "Unknown";
    }

    const char* key_to_display(WORD vk) {
        for (const auto& key : g_keys) {
            if (key.vk == vk) return key.display;
        }
        return "Unknown";
    }
    
    const char* gesture_to_string(GestureType type) {
        switch (type) {
            case GestureType::None: return "None";
            case GestureType::ControllerUpsideDown: return "Controller Upside Down";
            case GestureType::FlickLeft: return "Flick Left";
            case GestureType::FlickRight: return "Flick Right";
            case GestureType::FlickUp: return "Flick Up";
            case GestureType::FlickDown: return "Flick Down";
            case GestureType::TwistClockwise: return "Twist Clockwise";
            case GestureType::TwistCounterClockwise: return "Twist Counter-Clockwise";
            case GestureType::Shake: return "Shake";
            case GestureType::Jab: return "Jab Forward";
            case GestureType::Hold: return "Hold Position";
            default: return "Unknown";
        }
    }

    void send_key(WORD vk, bool down) {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = vk;
        input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
        input.ki.dwExtraInfo = 0;
        ::SendInput(1, &input, sizeof(INPUT));
    }

    void load_config() {
        auto path = API::get()->get_persistent_dir(L"button_remapper.txt");
        
        // Convert wide string to string
        char path_str[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, path_str, MAX_PATH, nullptr, nullptr);
        
        std::ifstream file(path_str);
        if (!file.is_open()) {
            API::get()->log_info("[ButtonRemapper] No config file found, using defaults");
            create_default_mappings();
            return;
        }

        m_mappings.clear();
        std::string line;
        
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue; // Skip empty lines and comments
            
            Mapping mapping;
            std::istringstream iss(line);
            std::string token;
            
            // Parse format: name|enabled|block_original|combo_buttons|output_type|output_value
            // Example: "A to Space|1|1|XINPUT_GAMEPAD_A|key|VK_SPACE"
            
            if (!std::getline(iss, mapping.name, '|')) continue;
            
            if (!std::getline(iss, token, '|')) continue;
            mapping.enabled = (token == "1");
            
            if (!std::getline(iss, token, '|')) continue;
            mapping.block_original = (token == "1");
            
            // Parse combo buttons (comma separated)
            if (!std::getline(iss, token, '|')) continue;
            std::istringstream combo_stream(token);
            std::string btn_name;
            while (std::getline(combo_stream, btn_name, ',')) {
                for (const auto& btn : g_buttons) {
                    if (btn_name == btn.name) {
                        mapping.combo_buttons.push_back(btn.mask);
                        break;
                    }
                }
            }
            
            // Parse output type
            if (!std::getline(iss, token, '|')) continue;
            
            // Parse output value
            std::string output_value;
            if (!std::getline(iss, output_value, '|')) continue;
            
            if (token == "button") {
                for (const auto& btn : g_buttons) {
                    if (output_value == btn.name) {
                        mapping.output_button = btn.mask;
                        break;
                    }
                }
            } else if (token == "key") {
                for (const auto& key : g_keys) {
                    if (output_value == key.name) {
                        mapping.output_key = key.vk;
                        break;
                    }
                }
            }
            
            if (!mapping.combo_buttons.empty() && 
                (mapping.output_button || mapping.output_key)) {
                m_mappings.push_back(mapping);
            }
        }
        
        API::get()->log_info("[ButtonRemapper] Loaded %d mappings from config", m_mappings.size());
    }

    void save_config() {
        auto path = API::get()->get_persistent_dir(L"button_remapper.txt");
        
        // Convert wide string to string
        char path_str[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, path_str, MAX_PATH, nullptr, nullptr);
        
        std::ofstream file(path_str);
        if (!file.is_open()) {
            API::get()->log_error("[ButtonRemapper] Failed to save config file");
            return;
        }
        
        // Write header
        file << "# Button Remapper Configuration\n";
        file << "# Format: name|enabled|block_original|combo_buttons|output_type|output_value\n";
        file << "# Example: A to Space|1|1|XINPUT_GAMEPAD_A|key|VK_SPACE\n\n";
        
        for (const auto& mapping : m_mappings) {
            // Write name
            file << mapping.name << "|";
            
            // Write enabled state
            file << (mapping.enabled ? "1" : "0") << "|";
            
            // Write block_original state
            file << (mapping.block_original ? "1" : "0") << "|";
            
            // Write combo buttons (comma separated)
            for (size_t i = 0; i < mapping.combo_buttons.size(); i++) {
                if (i > 0) file << ",";
                for (const auto& btn : g_buttons) {
                    if (btn.mask == mapping.combo_buttons[i]) {
                        file << btn.name;
                        break;
                    }
                }
            }
            file << "|";
            
            // Write output type and value
            if (mapping.output_button) {
                file << "button|";
                for (const auto& btn : g_buttons) {
                    if (btn.mask == *mapping.output_button) {
                        file << btn.name;
                        break;
                    }
                }
            } else if (mapping.output_key) {
                file << "key|";
                for (const auto& key : g_keys) {
                    if (key.vk == *mapping.output_key) {
                        file << key.name;
                        break;
                    }
                }
            }
            
            file << "\n";
        }
        
        API::get()->log_info("[ButtonRemapper] Saved %d mappings to config", m_mappings.size());
    }

    void create_default_mappings() {
        // Example default mappings
        Mapping example1;
        example1.name = "Example: A to Space";
        example1.combo_buttons.push_back(XINPUT_GAMEPAD_A);
        example1.output_key = VK_SPACE;
        example1.enabled = false;
        example1.block_original = true;
        m_mappings.push_back(example1);
        
        Mapping example3;
        example3.name = "Example: Upside Down to Tab";
        example3.gesture_type = GestureType::ControllerUpsideDown;
        example3.use_left_controller = false;
        example3.output_key = VK_TAB;
        example3.enabled = false;
        m_mappings.push_back(example3);
        
        Mapping example4;
        example4.name = "Example: Flick Right to Next";
        example4.gesture_type = GestureType::FlickRight;
        example4.use_left_controller = true;
        example4.output_key = VK_RIGHT;
        example4.enabled = false;
        m_mappings.push_back(example4);
        
        Mapping example2;
        example2.name = "Example: Start+Back to F5";
        example2.combo_buttons.push_back(XINPUT_GAMEPAD_START);
        example2.combo_buttons.push_back(XINPUT_GAMEPAD_BACK);
        example2.output_key = VK_F5;
        example2.enabled = false;
        example2.block_original = true;
        m_mappings.push_back(example2);
    }

    HWND m_wnd{};
    bool m_initialized{false};
    bool m_show_ui{true};
    std::recursive_mutex m_imgui_mutex{};
    std::mutex m_mapping_mutex{};
    
    std::vector<Mapping> m_mappings;
    int m_selected_mapping{-1};
    bool m_config_dirty{false};
    
    // Combo recording
    bool m_recording_combo{false};
    std::vector<uint16_t> m_recorded_combo;
    uint16_t m_last_buttons{0};
    uint16_t m_current_buttons{0};
    
    // UI hotkey
    WORD m_show_ui_hotkey{VK_F9};
    bool m_first_present{true};
    
    // VR gesture detection
    VRControllerState m_left_controller;
    VRControllerState m_right_controller;
    std::unordered_map<GestureType, GestureDetection> m_gestures;
    float m_current_time = 0.0f;
    
    void initialize_gestures() {
        // Initialize gesture thresholds
        m_gestures[GestureType::ControllerUpsideDown] = {GestureType::ControllerUpsideDown, -0.7f, 0.0f, 0.1f};
        m_gestures[GestureType::FlickLeft] = {GestureType::FlickLeft, 3.0f, 0.0f, 0.5f};
        m_gestures[GestureType::FlickRight] = {GestureType::FlickRight, 3.0f, 0.0f, 0.5f};
        m_gestures[GestureType::FlickUp] = {GestureType::FlickUp, 3.0f, 0.0f, 0.5f};
        m_gestures[GestureType::FlickDown] = {GestureType::FlickDown, 3.0f, 0.0f, 0.5f};
        m_gestures[GestureType::TwistClockwise] = {GestureType::TwistClockwise, 3.0f, 0.0f, 0.5f};
        m_gestures[GestureType::TwistCounterClockwise] = {GestureType::TwistCounterClockwise, 3.0f, 0.0f, 0.5f};
        m_gestures[GestureType::Shake] = {GestureType::Shake, 5.0f, 0.0f, 1.0f};
        m_gestures[GestureType::Jab] = {GestureType::Jab, 6.0f, 0.0f, 0.5f};
        m_gestures[GestureType::Hold] = {GestureType::Hold, -0.5f, 1.0f, 0.1f};
    }
    
    void update_vr_controller_states(float delta) {
        const auto vr = API::get()->param()->vr;
        if (!vr) return;
        
        m_current_time += delta;
        
        // Update left controller
        update_single_controller_state(vr->get_left_controller_index(), m_left_controller, delta);
        
        // Update right controller
        update_single_controller_state(vr->get_right_controller_index(), m_right_controller, delta);
    }
    
    void update_single_controller_state(uint32_t index, VRControllerState& state, float delta) {
        const auto vr = API::get()->param()->vr;
        if (!vr || index == vr->get_hmd_index()) return;
        
        // Store previous state
        state.last_position = state.position;
        state.last_rotation = state.rotation;
        
        // Get current pose
        vr->get_pose(index, &state.position, &state.rotation);
        
        // Calculate velocities if we have a valid previous state
        if (state.is_valid && delta > 0.0f) {
            // Linear velocity
            state.velocity.x = (state.position.x - state.last_position.x) / delta;
            state.velocity.y = (state.position.y - state.last_position.y) / delta;
            state.velocity.z = (state.position.z - state.last_position.z) / delta;
            
            // Angular velocity (simplified - proper calculation would use quaternion difference)
            // For now, we'll track rotation changes in euler angles for gesture detection
        }
        
        state.is_valid = true;
        state.last_update_time = m_current_time;
    }
    
    void detect_gestures(float delta) {
        // Detect gestures for both controllers
        detect_controller_gestures(m_left_controller, false);
        detect_controller_gestures(m_right_controller, true);
    }
    
    void detect_controller_gestures(const VRControllerState& controller, bool is_right) {
        if (!controller.is_valid) return;
        
        // Get controller transform for orientation detection
        const auto vr = API::get()->param()->vr;
        if (!vr) return;
        
        uint32_t controller_index = is_right ? vr->get_right_controller_index() : vr->get_left_controller_index();
        UEVR_Matrix4x4f transform{};
        vr->get_transform(controller_index, &transform);
        
        // Extract up vector (Y axis) from transform
        float up_x = transform.m[0][1];
        float up_y = transform.m[1][1];
        float up_z = transform.m[2][1];
        
        // Detect upside down
        auto& upside_down = m_gestures[GestureType::ControllerUpsideDown];
        bool is_upside_down = up_z < upside_down.threshold;
        update_gesture_state(upside_down, is_upside_down);
        
        // Calculate velocity magnitude
        float velocity_magnitude = sqrtf(controller.velocity.x * controller.velocity.x + 
                                       controller.velocity.y * controller.velocity.y + 
                                       controller.velocity.z * controller.velocity.z);
        
        // Detect flick gestures based on velocity direction
        if (velocity_magnitude > 2.0f) {  // Minimum velocity for flick detection
            // Normalize velocity for direction
            float vx = controller.velocity.x / velocity_magnitude;
            float vy = controller.velocity.y / velocity_magnitude;
            float vz = controller.velocity.z / velocity_magnitude;
            
            // Horizontal flicks (left/right)
            if (fabsf(vx) > 0.7f) {
                if (vx < -0.7f) {
                    auto& flick_left = m_gestures[GestureType::FlickLeft];
                    update_gesture_state(flick_left, velocity_magnitude > flick_left.threshold);
                } else if (vx > 0.7f) {
                    auto& flick_right = m_gestures[GestureType::FlickRight];
                    update_gesture_state(flick_right, velocity_magnitude > flick_right.threshold);
                }
            }
            
            // Vertical flicks (up/down)
            if (fabsf(vy) > 0.7f) {
                if (vy > 0.7f) {
                    auto& flick_up = m_gestures[GestureType::FlickUp];
                    update_gesture_state(flick_up, velocity_magnitude > flick_up.threshold);
                } else if (vy < -0.7f) {
                    auto& flick_down = m_gestures[GestureType::FlickDown];
                    update_gesture_state(flick_down, velocity_magnitude > flick_down.threshold);
                }
            }
            
            // Forward jab (Z axis)
            if (vz < -0.7f && velocity_magnitude > m_gestures[GestureType::Jab].threshold) {
                auto& jab = m_gestures[GestureType::Jab];
                update_gesture_state(jab, true);
            }
        }
        
        // Detect shake (rapid oscillation)
        static float shake_accumulator = 0.0f;
        if (velocity_magnitude > 2.0f) {
            shake_accumulator += velocity_magnitude * 0.1f;
        } else {
            shake_accumulator *= 0.9f;  // Decay
        }
        auto& shake = m_gestures[GestureType::Shake];
        update_gesture_state(shake, shake_accumulator > shake.threshold);
        
        // Process gesture-based mappings
        process_gesture_mappings(is_right);
    }
    
    void update_gesture_state(GestureDetection& gesture, bool is_active) {
        // Check cooldown
        if (m_current_time - gesture.last_trigger_time < gesture.cooldown) {
            return;
        }
        
        if (is_active && !gesture.is_active) {
            // Gesture just activated
            gesture.is_active = true;
            gesture.activation_start_time = m_current_time;
            
            // For non-hold gestures, trigger immediately
            if (gesture.type != GestureType::Hold && gesture.type != GestureType::ControllerUpsideDown) {
                gesture.last_trigger_time = m_current_time;
            }
        } else if (!is_active && gesture.is_active) {
            // Gesture deactivated
            gesture.is_active = false;
        } else if (is_active && gesture.is_active && gesture.type == GestureType::Hold) {
            // Check if hold time reached
            if (m_current_time - gesture.activation_start_time >= gesture.hold_time && 
                gesture.last_trigger_time < gesture.activation_start_time) {
                gesture.last_trigger_time = m_current_time;
            }
        }
    }
    
    void process_gesture_mappings(bool is_right_controller) {
        for (auto& mapping : m_mappings) {
            if (!mapping.enabled || !mapping.gesture_type) continue;
            if (mapping.use_left_controller == is_right_controller) continue;  // Wrong controller
            
            auto& gesture = m_gestures[*mapping.gesture_type];
            bool should_trigger = gesture.is_active;
            
            // For non-continuous gestures, only trigger once
            if (gesture.type != GestureType::ControllerUpsideDown && gesture.type != GestureType::Hold) {
                should_trigger = gesture.is_active && 
                               (gesture.last_trigger_time > m_current_time - 0.1f);
            }
            
            if (should_trigger && !mapping.was_pressed) {
                // Trigger output
                mapping.was_pressed = true;
                
                if (mapping.output_key) {
                    send_key(*mapping.output_key, true);
                }
            } else if (!should_trigger && mapping.was_pressed) {
                // Release output
                mapping.was_pressed = false;
                
                if (mapping.output_key) {
                    send_key(*mapping.output_key, false);
                }
            }
        }
    }
    
    void handle_controller_navigation(XINPUT_STATE* state) {
        // Map controller buttons to ImGui navigation inputs
        ImGuiIO& io = ImGui::GetIO();
        
        // DPad navigation
        io.NavInputs[ImGuiNavInput_DpadUp] = (state->Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) ? 1.0f : 0.0f;
        io.NavInputs[ImGuiNavInput_DpadDown] = (state->Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) ? 1.0f : 0.0f;
        io.NavInputs[ImGuiNavInput_DpadLeft] = (state->Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) ? 1.0f : 0.0f;
        io.NavInputs[ImGuiNavInput_DpadRight] = (state->Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) ? 1.0f : 0.0f;
        
        // Face buttons
        io.NavInputs[ImGuiNavInput_Activate] = (state->Gamepad.wButtons & XINPUT_GAMEPAD_A) ? 1.0f : 0.0f;
        io.NavInputs[ImGuiNavInput_Cancel] = (state->Gamepad.wButtons & XINPUT_GAMEPAD_B) ? 1.0f : 0.0f;
        io.NavInputs[ImGuiNavInput_Menu] = (state->Gamepad.wButtons & XINPUT_GAMEPAD_Y) ? 1.0f : 0.0f;
        io.NavInputs[ImGuiNavInput_Input] = (state->Gamepad.wButtons & XINPUT_GAMEPAD_X) ? 1.0f : 0.0f;
        
        // Shoulder buttons for tab navigation
        io.NavInputs[ImGuiNavInput_FocusPrev] = (state->Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) ? 1.0f : 0.0f;
        io.NavInputs[ImGuiNavInput_FocusNext] = (state->Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) ? 1.0f : 0.0f;
        
        // Left stick navigation
        float deadzone = 0.3f;
        float lx = state->Gamepad.sThumbLX / 32768.0f;
        float ly = state->Gamepad.sThumbLY / 32768.0f;
        
        if (fabsf(lx) > deadzone) {
            io.NavInputs[ImGuiNavInput_LStickLeft] = lx < 0.0f ? -lx : 0.0f;
            io.NavInputs[ImGuiNavInput_LStickRight] = lx > 0.0f ? lx : 0.0f;
        }
        if (fabsf(ly) > deadzone) {
            io.NavInputs[ImGuiNavInput_LStickUp] = ly > 0.0f ? ly : 0.0f;
            io.NavInputs[ImGuiNavInput_LStickDown] = ly < 0.0f ? -ly : 0.0f;
        }
    }
    
    void show_controller_status(const VRControllerState& controller, bool is_right) {
        if (!controller.is_valid) {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "  Not tracking");
            return;
        }
        
        float velocity_mag = sqrtf(controller.velocity.x * controller.velocity.x + 
                                  controller.velocity.y * controller.velocity.y + 
                                  controller.velocity.z * controller.velocity.z);
        ImGui::Text("  Velocity: %.2f m/s", velocity_mag);
        
        // Show active gestures
        for (auto& [type, gesture] : m_gestures) {
            if (gesture.is_active) {
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "  %s ACTIVE", gesture_to_string(type));
            }
        }
    }
};

static ButtonRemapperPlugin g_plugin;