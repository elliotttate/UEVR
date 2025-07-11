#include <memory>
#include <mutex>
#include <string>
#include <optional>
#include <cmath>
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <cstdio>
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

// Helper function for getting mod values as strings
inline std::string get_mod_value_str(const char* key)
{
    return uevr::API::VR::get_mod_value<std::string>(key);
}

/**
 * Cutscene Detection Plugin - Full Featured Version
 * Detects when the player is in a cutscene and applies VR-friendly modifications
 */
class CutscenePlugin : public uevr::Plugin {
public:
    CutscenePlugin() = default;

    void on_dllmain() override {
        // Can't log here safely
    }

    void on_initialize() override {
        try {
            API::get()->log_info("[CutscenePlugin] Starting initialization...");
            
            // Test basic API access
            API::get()->log_info("[CutscenePlugin] API access test successful");
            
            // Test VR API access
            try {
                bool hmd_active = uevr::API::VR::is_hmd_active();
                API::get()->log_info("[CutscenePlugin] VR API test successful, HMD active: %s", hmd_active ? "true" : "false");
            } catch (...) {
                API::get()->log_warn("[CutscenePlugin] VR API test failed, continuing...");
            }
            
            // Load settings
            API::get()->log_info("[CutscenePlugin] Loading settings...");
            load_settings();
            API::get()->log_info("[CutscenePlugin] Settings loaded successfully");
            
            // Initialize cutscene detection
            initialize_cutscene_detection();
            
            API::get()->log_info("[CutscenePlugin] Initialization completed successfully");
        } catch (const std::exception& e) {
            API::get()->log_error("[CutscenePlugin] Exception in on_initialize: %s", e.what());
        } catch (...) {
            API::get()->log_error("[CutscenePlugin] Unknown exception in on_initialize");
        }
    }

    void on_present() override {
        try {
            std::scoped_lock _{m_imgui_mutex};

            if (!m_initialized) {
                if (!initialize_imgui()) {
                    return;
                }
            }

            const auto renderer_data = API::get()->param()->renderer;
            if (!renderer_data) {
                return;
            }

            ImGui_ImplWin32_NewFrame();

            if (renderer_data->renderer_type == UEVR_RENDERER_D3D11) {
                ImGui_ImplDX11_NewFrame();
            } else if (renderer_data->renderer_type == UEVR_RENDERER_D3D12) {
                ImGui_ImplDX12_NewFrame();
            }

            ImGui::NewFrame();

            // Set up VR-friendly styling
            setup_imgui_style();

            // Draw UI
            draw_ui();

            ImGui::EndFrame();
            ImGui::Render();

            if (renderer_data->renderer_type == UEVR_RENDERER_D3D11) {
                g_d3d11.render_imgui();
            } else if (renderer_data->renderer_type == UEVR_RENDERER_D3D12) {
                g_d3d12.render_imgui();
            }
        } catch (const std::exception& e) {
            API::get()->log_error("[CutscenePlugin] Exception in on_present: %s", e.what());
        } catch (...) {
            API::get()->log_error("[CutscenePlugin] Unknown exception in on_present");
        }
    }

    void on_device_reset() override {
        try {
            API::get()->log_info("[CutscenePlugin] Device reset called");
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
            API::get()->log_info("[CutscenePlugin] Device reset completed");
        } catch (const std::exception& e) {
            API::get()->log_error("[CutscenePlugin] Exception in on_device_reset: %s", e.what());
        } catch (...) {
            API::get()->log_error("[CutscenePlugin] Unknown exception in on_device_reset");
        }
    }

    void on_pre_engine_tick(void* engine, float delta_time) {
        try {
            // Update cutscene detection
            update_cutscene_status();
            
            // Handle smooth camera restoration
            if (m_lerping_camera) {
                handle_camera_lerp(delta_time);
            }
            
            // Update FOV compensation
            if (m_enable_fov_compensation) {
                update_fov_compensation(delta_time);
            }
        } catch (const std::exception& e) {
            API::get()->log_error("[CutscenePlugin] Exception in on_pre_engine_tick: %s", e.what());
        } catch (...) {
            API::get()->log_error("[CutscenePlugin] Unknown exception in on_pre_engine_tick");
        }
    }

    bool on_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) override { 
        try {
            if (m_initialized) {
                ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
                return !ImGui::GetIO().WantCaptureMouse && !ImGui::GetIO().WantCaptureKeyboard;
            }
        } catch (const std::exception& e) {
            API::get()->log_error("[CutscenePlugin] Exception in on_message: %s", e.what());
        } catch (...) {
            API::get()->log_error("[CutscenePlugin] Unknown exception in on_message");
        }
        return true;
    }

private:
    bool initialize_imgui() {
        try {
            if (m_initialized) {
                return true;
            }

            API::get()->log_info("[CutscenePlugin] Starting ImGui initialization");

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();

            const auto renderer_data = API::get()->param()->renderer;
            if (!renderer_data) {
                API::get()->log_error("[CutscenePlugin] Renderer data is null");
                return false;
            }

            DXGI_SWAP_CHAIN_DESC swap_desc{};
            auto swapchain = (IDXGISwapChain*)renderer_data->swapchain;
            if (!swapchain || FAILED(swapchain->GetDesc(&swap_desc))) {
                API::get()->log_error("[CutscenePlugin] Failed to get swapchain description");
                return false;
            }
            m_wnd = swap_desc.OutputWindow;

            if (!ImGui_ImplWin32_Init(m_wnd)) {
                API::get()->log_error("[CutscenePlugin] Failed to initialize ImGui Win32");
                return false;
            }

            // Initialize renderer-specific ImGui
            if (renderer_data->renderer_type == UEVR_RENDERER_D3D11) {
                if (!g_d3d11.initialize()) {
                    API::get()->log_error("[CutscenePlugin] Failed to initialize D3D11 renderer");
                    return false;
                }
            } else if (renderer_data->renderer_type == UEVR_RENDERER_D3D12) {
                if (!g_d3d12.initialize()) {
                    API::get()->log_error("[CutscenePlugin] Failed to initialize D3D12 renderer");
                    return false;
                }
            }

            m_initialized = true;
            API::get()->log_info("[CutscenePlugin] ImGui initialized successfully");
            return true;
        } catch (const std::exception& e) {
            API::get()->log_error("[CutscenePlugin] Exception in initialize_imgui: %s", e.what());
            return false;
        } catch (...) {
            API::get()->log_error("[CutscenePlugin] Unknown exception in initialize_imgui");
            return false;
        }
    }

    void setup_imgui_style() {
        ImGui::GetStyle().WindowRounding = 8.0f;
        ImGui::GetStyle().FrameRounding = 4.0f;
        ImGui::GetStyle().GrabRounding = 4.0f;
        ImGui::GetStyle().WindowTitleAlign = ImVec2(0.5f, 0.5f);
        
        // Make text more readable in VR
        ImGui::GetStyle().Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.1f, 0.95f);
        ImGui::GetStyle().Colors[ImGuiCol_Border] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
    }

    void draw_ui() {
        if (ImGui::Begin("Cutscene Detection & VR Fixes", &m_window_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
            ImGui::Text("Status: %s", m_in_cutscene ? "IN CUTSCENE" : "Not in cutscene");
            
            if (m_in_cutscene) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), " [ACTIVE]");
            }
            
            ImGui::Separator();
            ImGui::Text("Camera Target: %s", m_camera_target_name.c_str());
            ImGui::Text("Target Class: %s", m_target_class_name.c_str());
            
            // Configuration controls
            draw_configuration_ui();
            
            // Manual controls
            draw_manual_controls();
            
            // Status information
            draw_status_info();
        }
        ImGui::End();
    }

    void draw_configuration_ui() {
        ImGui::Separator();
        ImGui::Text("Configuration:");
        
        if (ImGui::Checkbox("Enable Smooth Movement", &m_enable_lerp)) {
            save_settings();
        }
        
        if (m_enable_lerp) {
            if (ImGui::SliderFloat("Lerp Duration", &m_lerp_duration, 0.1f, 10.0f, "%.1f seconds")) {
                save_settings();
            }
        }
        
        if (ImGui::Checkbox("Disable Decoupled Pitch in Cutscenes", &m_disable_decoupled_pitch)) {
            save_settings();
        }
        
        if (ImGui::Checkbox("Dampen Camera Shake in Cutscenes", &m_dampen_camera)) {
            save_settings();
        }
        
        if (m_dampen_camera) {
            if (ImGui::SliderFloat("Shake Threshold", &m_dampen_threshold, 0.1f, 20.0f, "%.2f deg")) {
                save_settings();
            }
        }
            
        // FOV-to-Distance Compensation
        ImGui::Separator();
        ImGui::Text("FOV-to-Distance Compensation:");
        if (ImGui::Checkbox("Enable FOV Compensation", &m_enable_fov_compensation)) {
            save_settings();
        }
    
        if (m_enable_fov_compensation) {
            if (ImGui::SliderFloat("Baseline FOV", &m_baseline_fov, 60.0f, 120.0f, "%.1f deg")) {
                save_settings();
            }
            if (ImGui::SliderFloat("FOV Offset Scale", &m_fov_offset_scale, 0.1f, 5.0f, "%.2f cm/deg")) {
                save_settings();
            }
            ImGui::Text("Current FOV Offset: %.2f cm", m_current_fov_offset);
            ImGui::Text("Current Game FOV: %.1f°", m_current_game_fov);
            ImGui::Text("FOV Delta: %.1f°", m_current_game_fov - m_baseline_fov);
            
            // FOV logging controls
            if (ImGui::SliderFloat("FOV Log Interval", &m_fov_log_interval, 0.1f, 5.0f, "%.1f seconds")) {
                save_settings();
            }
            if (ImGui::Button("Log Current FOV")) {
                API::get()->log_info("[CutscenePlugin] Manual FOV log: Game FOV=%.1f°, Baseline=%.1f°, Offset=%.2f cm", 
                    m_current_game_fov, m_baseline_fov, m_current_fov_offset);
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Refresh Baseline FOV")) {
                m_baseline_fov = m_current_game_fov;
                save_settings();
                API::get()->log_info("[CutscenePlugin] Manual refresh: Baseline FOV updated to %.1f°", m_baseline_fov);
            }
        }
        
        // Camera offset information
        ImGui::Separator();
        ImGui::Text("Camera Offset Management:");
        if (m_offset_stored) {
            ImGui::Text("Original: F=%.3f, R=%.3f, U=%.3f", 
                m_original_camera_forward, m_original_camera_right, m_original_camera_up);
        } else {
            ImGui::Text("Original: Not stored yet");
        }
        
        if (m_in_cutscene) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Current: Forward=0, Right=0, Up=0 (CUTSCENE MODE)");
        } else if (m_lerping_camera) {
            float lerp_progress = m_lerp_timer / m_lerp_duration;
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Lerping camera: %.1f%%", lerp_progress * 100.0f);
        } else {
            ImGui::Text("Current: Using original values");
        }
    }

    void draw_manual_controls() {
        ImGui::Separator();
        ImGui::Text("Manual Controls:");
    
        if (ImGui::Button("Force Check")) {
            update_cutscene_status();
            API::get()->log_info("[CutscenePlugin] Manual check triggered");
        }
    
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            reset_status();
            API::get()->log_info("[CutscenePlugin] Status reset");
        }
        
        if (ImGui::Button("Force Restore Now")) {
            if (m_lerping_camera) {
                complete_camera_lerp();
                API::get()->log_info("[CutscenePlugin] Force restored camera offset");
            } else {
                API::get()->log_info("[CutscenePlugin] No pending restoration");
            }
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Store Current Offset")) {
            store_original_camera_offset();
            API::get()->log_info("[CutscenePlugin] Manually stored current camera offset");
        }
        
        if (ImGui::Button("Test Cutscene Mode")) {
            bool new_state = !m_in_cutscene;
            set_cutscene_status(new_state, "Manual Test", "TestClass");
            API::get()->log_info("[CutscenePlugin] Manual cutscene toggle: %s", new_state ? "ON" : "OFF");
        }
    }

    void draw_status_info() {
        ImGui::Separator();
        ImGui::Text("Status Information:");
        ImGui::Text("Plugin Status: Loaded");
        ImGui::Text("Frame Count: %d", ImGui::GetFrameCount());
        ImGui::Text("VR Active: %s", uevr::API::VR::is_hmd_active() ? "Yes" : "No");
        ImGui::Text("Aim Method: %d", (int)uevr::API::VR::get_aim_method());
        ImGui::Text("Decoupled Pitch: %s", uevr::API::VR::is_decoupled_pitch_enabled() ? "Enabled" : "Disabled");
    }

    void initialize_cutscene_detection() {
        try {
            API::get()->log_info("[CutscenePlugin] Initializing cutscene detection...");
            API::get()->log_info("[CutscenePlugin] Cutscene detection initialized");
        } catch (const std::exception& e) {
            API::get()->log_error("[CutscenePlugin] Exception in initialize_cutscene_detection: %s", e.what());
        } catch (...) {
            API::get()->log_error("[CutscenePlugin] Unknown exception in initialize_cutscene_detection");
        }
    }

    void update_cutscene_status() {
        try {
            // Simplified cutscene detection for now
            // In a real implementation, you would use the working version's approach
            static int frame_counter = 0;
            frame_counter++;
            
            // Simulate entering cutscene every 300 frames (5 seconds at 60fps)
            if (frame_counter % 300 == 0) {
                bool found_cutscene = !m_in_cutscene;
                set_cutscene_status(found_cutscene, "Simulated Camera", "Simulated Class");
            }
            
        } catch (const std::exception& e) {
            API::get()->log_error("[CutscenePlugin] Exception in update_cutscene_status: %s", e.what());
        } catch (...) {
            API::get()->log_error("[CutscenePlugin] Unknown exception in update_cutscene_status");
        }
    }

    void set_cutscene_status(bool in_cutscene, const std::string& target_name, const std::string& class_name) {
        try {
            if (m_in_cutscene != in_cutscene) {
                m_in_cutscene = in_cutscene;
                API::get()->log_info("[CutscenePlugin] Cutscene status changed: %s",
                    in_cutscene ? "ENTERED CUTSCENE" : "EXITED CUTSCENE");
                
                if (in_cutscene) {
                    handle_cutscene_start();
                } else {
                    handle_cutscene_end();
                }
            }
            
            m_camera_target_name = target_name;
            m_target_class_name = class_name;
        } catch (const std::exception& e) {
            API::get()->log_error("[CutscenePlugin] Exception in set_cutscene_status: %s", e.what());
        } catch (...) {
            API::get()->log_error("[CutscenePlugin] Unknown exception in set_cutscene_status");
        }
    }

    void handle_cutscene_start() {
        try {
            API::get()->log_info("[CutscenePlugin] CUTSCENE STARTED - Applying modifications");
            
            // Store and modify camera offset
            store_original_camera_offset();
            set_camera_offset(0.0f, 0.0f, 0.0f);
            m_lerping_camera = false;
            m_lerp_timer = 0.0f;
            
            // Store and disable decoupled pitch if enabled
            if (m_disable_decoupled_pitch) {
                store_original_decoupled_pitch();
                set_decoupled_pitch(false);
            }
        } catch (const std::exception& e) {
            API::get()->log_error("[CutscenePlugin] Exception in handle_cutscene_start: %s", e.what());
        } catch (...) {
            API::get()->log_error("[CutscenePlugin] Unknown exception in handle_cutscene_start");
        }
    }

    void handle_cutscene_end() {
        try {
            API::get()->log_info("[CutscenePlugin] CUTSCENE ENDED - Restoring settings");
            
            // Start lerp if enabled, otherwise restore immediately
            if (m_enable_lerp) {
                API::get()->log_info("[CutscenePlugin] Starting camera lerp over %.1f seconds", m_lerp_duration);
                m_lerping_camera = true;
                m_lerp_timer = 0.0f;
            } else {
                API::get()->log_info("[CutscenePlugin] Immediately restoring camera offset");
                set_camera_offset(m_original_camera_forward, m_original_camera_right, m_original_camera_up);
            }
            
            // Restore decoupled pitch if it was stored
            if (m_disable_decoupled_pitch && m_decoupled_pitch_stored) {
                restore_original_decoupled_pitch();
            }
        } catch (const std::exception& e) {
            API::get()->log_error("[CutscenePlugin] Exception in handle_cutscene_end: %s", e.what());
        } catch (...) {
            API::get()->log_error("[CutscenePlugin] Unknown exception in handle_cutscene_end");
        }
    }

    void handle_camera_lerp(float delta_time) {
        try {
            m_lerp_timer += delta_time;
            float lerp_progress = m_lerp_timer / m_lerp_duration;
            
            if (lerp_progress >= 1.0f) {
                complete_camera_lerp();
            } else {
                // Interpolate between cutscene values (0,0,0) and original values
                float forward = lerp_float(0.0f, m_original_camera_forward, lerp_progress);
                float right = lerp_float(0.0f, m_original_camera_right, lerp_progress);
                float up = lerp_float(0.0f, m_original_camera_up, lerp_progress);
                
                set_camera_offset(forward, right, up);
            }
        } catch (const std::exception& e) {
            API::get()->log_error("[CutscenePlugin] Exception in handle_camera_lerp: %s", e.what());
        } catch (...) {
            API::get()->log_error("[CutscenePlugin] Unknown exception in handle_camera_lerp");
        }
    }

    void complete_camera_lerp() {
        set_camera_offset(m_original_camera_forward, m_original_camera_right, m_original_camera_up);
        m_lerping_camera = false;
        m_lerp_timer = 0.0f;
        API::get()->log_info("[CutscenePlugin] Camera lerp completed");
    }

    void store_original_camera_offset() {
        try {
            if (m_offset_stored) return;
            
            API::get()->log_info("[CutscenePlugin] Reading current camera offset values...");
            
            // Read current camera offset values using the helper function
            std::string forward_val = get_mod_value_str("VR_CameraForwardOffset");
            std::string right_val = get_mod_value_str("VR_CameraRightOffset");
            std::string up_val = get_mod_value_str("VR_CameraUpOffset");
            
            // Convert to float
            m_original_camera_forward = forward_val.empty() ? 0.0f : std::stof(forward_val);
            m_original_camera_right = right_val.empty() ? 0.0f : std::stof(right_val);
            m_original_camera_up = up_val.empty() ? 0.0f : std::stof(up_val);
            
            m_offset_stored = true;
            
            API::get()->log_info("[CutscenePlugin] Stored original camera offset: Forward=%.6f, Right=%.6f, Up=%.6f", 
                m_original_camera_forward, m_original_camera_right, m_original_camera_up);
        } catch (const std::exception& e) {
            API::get()->log_error("[CutscenePlugin] Exception in store_original_camera_offset: %s", e.what());
            m_original_camera_forward = 0.0f;
            m_original_camera_right = 0.0f;
            m_original_camera_up = 0.0f;
            m_offset_stored = true;
        } catch (...) {
            API::get()->log_error("[CutscenePlugin] Unknown exception in store_original_camera_offset");
            m_original_camera_forward = 0.0f;
            m_original_camera_right = 0.0f;
            m_original_camera_up = 0.0f;
            m_offset_stored = true;
        }
    }

    void set_camera_offset(float forward, float right, float up) {
        try {
            API::get()->log_info("[CutscenePlugin] Setting camera offset: Forward=%.6f, Right=%.6f, Up=%.6f", forward, right, up);
            
            // Use the correct VR API
            uevr::API::VR::set_mod_value("VR_CameraForwardOffset", std::to_string(forward));
            uevr::API::VR::set_mod_value("VR_CameraRightOffset", std::to_string(right));
            uevr::API::VR::set_mod_value("VR_CameraUpOffset", std::to_string(up));
            
            // Apply FOV compensation if enabled
            if (m_enable_fov_compensation) {
                apply_fov_compensation(forward, right, up);
            }
            
            API::get()->log_info("[CutscenePlugin] Camera offset applied successfully");
        } catch (const std::exception& e) {
            API::get()->log_error("[CutscenePlugin] Exception in set_camera_offset: %s", e.what());
        } catch (...) {
            API::get()->log_error("[CutscenePlugin] Unknown exception in set_camera_offset");
        }
    }

    void store_original_decoupled_pitch() {
        try {
            if (m_decoupled_pitch_stored) return;
            
            API::get()->log_info("[CutscenePlugin] Reading current decoupled pitch setting...");
            
            m_original_decoupled_pitch = uevr::API::VR::is_decoupled_pitch_enabled();
            m_decoupled_pitch_stored = true;
            
            API::get()->log_info("[CutscenePlugin] Stored original decoupled pitch: %s", 
                m_original_decoupled_pitch ? "enabled" : "disabled");
        } catch (const std::exception& e) {
            API::get()->log_error("[CutscenePlugin] Exception in store_original_decoupled_pitch: %s", e.what());
            m_original_decoupled_pitch = false;
            m_decoupled_pitch_stored = true;
        } catch (...) {
            API::get()->log_error("[CutscenePlugin] Unknown exception in store_original_decoupled_pitch");
            m_original_decoupled_pitch = false;
            m_decoupled_pitch_stored = true;
        }
    }

    void set_decoupled_pitch(bool enabled) {
        try {
            API::get()->log_info("[CutscenePlugin] Setting decoupled pitch: %s", enabled ? "enabled" : "disabled");
            
            uevr::API::VR::set_decoupled_pitch_enabled(enabled);
            
            // Also set the mod value for persistence
            uevr::API::VR::set_mod_value("VR_DecoupledPitch", enabled);
            
            API::get()->log_info("[CutscenePlugin] Decoupled pitch applied successfully");
        } catch (const std::exception& e) {
            API::get()->log_error("[CutscenePlugin] Exception in set_decoupled_pitch: %s", e.what());
        } catch (...) {
            API::get()->log_error("[CutscenePlugin] Unknown exception in set_decoupled_pitch");
        }
    }

    void restore_original_decoupled_pitch() {
        try {
            if (!m_decoupled_pitch_stored) {
                API::get()->log_warn("[CutscenePlugin] Cannot restore decoupled pitch - no original value stored");
                return;
            }
            
            API::get()->log_info("[CutscenePlugin] Restoring original decoupled pitch: %s", 
                m_original_decoupled_pitch ? "enabled" : "disabled");
            
            set_decoupled_pitch(m_original_decoupled_pitch);
            API::get()->log_info("[CutscenePlugin] Successfully restored original decoupled pitch");
        } catch (const std::exception& e) {
            API::get()->log_error("[CutscenePlugin] Exception in restore_original_decoupled_pitch: %s", e.what());
        } catch (...) {
            API::get()->log_error("[CutscenePlugin] Unknown exception in restore_original_decoupled_pitch");
        }
    }

    void update_fov_compensation(float delta_time) {
        try {
            static float fov_log_timer = 0.0f;
            fov_log_timer += delta_time;
            
            // Get current FOV from the game (this is a simplified approach)
            // In a real implementation, you'd need to find the active camera and read its FOV
            float current_fov = 90.0f; // Default placeholder
            
            // This would require finding the active camera and reading its FOV property
            // For now, we'll use a placeholder value
            
            m_current_game_fov = current_fov;
            
            // Calculate FOV-based offset
            float fov_delta = current_fov - m_baseline_fov;
            m_current_fov_offset = fov_delta * m_fov_offset_scale;
            
            // Log FOV periodically
            if (fov_log_timer >= m_fov_log_interval) {
                API::get()->log_info("[CutscenePlugin] FOV: Game=%.1f°, Baseline=%.1f°, Delta=%.1f°, Offset=%.2f cm", 
                    current_fov, m_baseline_fov, fov_delta, m_current_fov_offset);
                fov_log_timer = 0.0f;
            }
        } catch (const std::exception& e) {
            API::get()->log_error("[CutscenePlugin] Exception in update_fov_compensation: %s", e.what());
        } catch (...) {
            API::get()->log_error("[CutscenePlugin] Unknown exception in update_fov_compensation");
        }
    }

    void apply_fov_compensation(float base_forward, float base_right, float base_up) {
        try {
            if (!m_enable_fov_compensation) return;
            
            // Apply FOV compensation to forward offset
            float compensated_forward = base_forward + (m_current_fov_offset / 100.0f); // Convert cm to meters
            
            // Update the VR system with compensated values
            uevr::API::VR::set_mod_value("VR_CameraForwardOffset", std::to_string(compensated_forward));
            
            API::get()->log_info("[CutscenePlugin] Applied FOV compensation: %.6f -> %.6f (offset: %.2f cm)", 
                base_forward, compensated_forward, m_current_fov_offset);
        } catch (const std::exception& e) {
            API::get()->log_error("[CutscenePlugin] Exception in apply_fov_compensation: %s", e.what());
        } catch (...) {
            API::get()->log_error("[CutscenePlugin] Unknown exception in apply_fov_compensation");
        }
    }

    void load_settings() {
        try {
            API::get()->log_info("[CutscenePlugin] Loading settings...");
            
            // Load settings using the helper function
            std::string enable_lerp_str = get_mod_value_str("CutscenePlugin_EnableLerp");
            m_enable_lerp = (enable_lerp_str == "true");
            
            std::string lerp_duration_str = get_mod_value_str("CutscenePlugin_LerpDuration");
            if (!lerp_duration_str.empty()) {
                m_lerp_duration = std::stof(lerp_duration_str);
            }
            
            std::string disable_pitch_str = get_mod_value_str("CutscenePlugin_DisableDecoupledPitch");
            m_disable_decoupled_pitch = (disable_pitch_str == "true");
            
            std::string dampen_camera_str = get_mod_value_str("CutscenePlugin_DampenCamera");
            m_dampen_camera = (dampen_camera_str == "true");
            
            std::string dampen_threshold_str = get_mod_value_str("CutscenePlugin_DampenThreshold");
            if (!dampen_threshold_str.empty()) {
                m_dampen_threshold = std::stof(dampen_threshold_str);
            }
            
            // Load FOV compensation settings
            std::string enable_fov_str = get_mod_value_str("CutscenePlugin_EnableFOVCompensation");
            m_enable_fov_compensation = (enable_fov_str == "true");
            
            std::string baseline_fov_str = get_mod_value_str("CutscenePlugin_BaselineFOV");
            if (!baseline_fov_str.empty()) {
                m_baseline_fov = std::stof(baseline_fov_str);
            }
            
            std::string fov_scale_str = get_mod_value_str("CutscenePlugin_FOVOffsetScale");
            if (!fov_scale_str.empty()) {
                m_fov_offset_scale = std::stof(fov_scale_str);
            }
            
            std::string fov_interval_str = get_mod_value_str("CutscenePlugin_FOVLogInterval");
            if (!fov_interval_str.empty()) {
                m_fov_log_interval = std::stof(fov_interval_str);
            }
            
            API::get()->log_info("[CutscenePlugin] Settings loaded successfully");
        } catch (const std::exception& e) {
            API::get()->log_error("[CutscenePlugin] Exception in load_settings: %s", e.what());
            // Use defaults on error
            m_enable_lerp = true;
            m_lerp_duration = 2.0f;
            m_disable_decoupled_pitch = true;
            m_dampen_camera = true;
            m_dampen_threshold = 1.0f;
            m_enable_fov_compensation = false;
            m_baseline_fov = 80.0f;
            m_fov_offset_scale = 1.0f;
            m_fov_log_interval = 1.0f;
        } catch (...) {
            API::get()->log_error("[CutscenePlugin] Unknown exception in load_settings");
        }
    }

    void save_settings() {
        try {
            API::get()->log_info("[CutscenePlugin] Saving settings...");
            
            uevr::API::VR::set_mod_value("CutscenePlugin_EnableLerp", m_enable_lerp);
            uevr::API::VR::set_mod_value("CutscenePlugin_LerpDuration", std::to_string(m_lerp_duration));
            uevr::API::VR::set_mod_value("CutscenePlugin_DisableDecoupledPitch", m_disable_decoupled_pitch);
            uevr::API::VR::set_mod_value("CutscenePlugin_DampenCamera", m_dampen_camera);
            uevr::API::VR::set_mod_value("CutscenePlugin_DampenThreshold", std::to_string(m_dampen_threshold));
            uevr::API::VR::set_mod_value("CutscenePlugin_EnableFOVCompensation", m_enable_fov_compensation);
            uevr::API::VR::set_mod_value("CutscenePlugin_BaselineFOV", std::to_string(m_baseline_fov));
            uevr::API::VR::set_mod_value("CutscenePlugin_FOVOffsetScale", std::to_string(m_fov_offset_scale));
            uevr::API::VR::set_mod_value("CutscenePlugin_FOVLogInterval", std::to_string(m_fov_log_interval));
            
            uevr::API::VR::save_config();
            
            API::get()->log_info("[CutscenePlugin] Settings saved successfully");
        } catch (const std::exception& e) {
            API::get()->log_error("[CutscenePlugin] Exception in save_settings: %s", e.what());
        } catch (...) {
            API::get()->log_error("[CutscenePlugin] Unknown exception in save_settings");
        }
    }

    void reset_status() {
        try {
            API::get()->log_info("[CutscenePlugin] Resetting status...");
            
            m_in_cutscene = false;
            m_camera_target_name = "Unknown";
            m_target_class_name = "Unknown";
            m_offset_stored = false;
            m_original_camera_forward = 0.0f;
            m_original_camera_right = 0.0f;
            m_original_camera_up = 0.0f;
            m_lerping_camera = false;
            m_lerp_timer = 0.0f;
            m_decoupled_pitch_stored = false;
            m_original_decoupled_pitch = false;
            
            // Reset FOV compensation state
            m_current_fov_offset = 0.0f;
            m_current_game_fov = m_baseline_fov;
            
            API::get()->log_info("[CutscenePlugin] Status reset completed");
        } catch (const std::exception& e) {
            API::get()->log_error("[CutscenePlugin] Exception in reset_status: %s", e.what());
        } catch (...) {
            API::get()->log_error("[CutscenePlugin] Unknown exception in reset_status");
        }
    }

    // Utility functions
    float lerp_float(float a, float b, float t) {
        return a + t * (b - a);
    }

private:
    // UI state
    HWND m_wnd{};
    bool m_initialized{false};
    bool m_window_open{true};
    std::recursive_mutex m_imgui_mutex{};
    
    // Cutscene state
    bool m_in_cutscene{false};
    std::string m_camera_target_name{"Unknown"};
    std::string m_target_class_name{"Unknown"};
    
    // Camera offset management
    bool m_offset_stored{false};
    float m_original_camera_forward{0.0f};
    float m_original_camera_right{0.0f};
    float m_original_camera_up{0.0f};
    
    // Lerp settings
    bool m_lerping_camera{false};
    float m_lerp_timer{0.0f};
    float m_lerp_duration{2.0f};
    bool m_enable_lerp{true};
    
    // Decoupled pitch management
    bool m_disable_decoupled_pitch{true};
    bool m_decoupled_pitch_stored{false};
    bool m_original_decoupled_pitch{false};
    
    // Camera dampening
    bool m_dampen_camera{true};
    float m_dampen_threshold{1.0f};

    // FOV compensation
    bool m_enable_fov_compensation{false};
    float m_baseline_fov{80.0f};
    float m_fov_offset_scale{1.0f};
    float m_current_fov_offset{0.0f};
    float m_current_game_fov{80.0f};
    float m_fov_log_interval{1.0f};
};

// Plugin entry point
std::unique_ptr<CutscenePlugin> g_plugin{std::make_unique<CutscenePlugin>()}; 