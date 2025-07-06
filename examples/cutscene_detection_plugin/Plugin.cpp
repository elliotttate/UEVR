#include <memory>
#include <mutex>
#include <string>
#include <optional>
#include <cmath>
#include <Windows.h>

#include "imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_dx12.h"
#include "imgui/imgui_impl_win32.h"

#include "rendering/d3d11.hpp"
#include "rendering/d3d12.hpp"

#include "uevr/Plugin.hpp"

using namespace uevr;

class CutsceneDetectionPlugin : public uevr::Plugin {
public:
    CutsceneDetectionPlugin() = default;

    void on_dllmain() override {}

    void on_initialize() override {
        API::get()->log_info("[CutsceneDetectionPlugin] Initializing cutscene detection plugin");
        // Note: ImGui context will be created in initialize_imgui when needed
    }

    void on_present() override {
        std::scoped_lock _{m_imgui_mutex};
        
        if (!m_initialized) {
            if (!initialize_imgui()) {
                API::get()->log_error("[CutsceneDetectionPlugin] Failed to initialize ImGui");
                return;
            }
        }

        const auto renderer_data = API::get()->param()->renderer;

        // Handle desktop mode rendering (when VR is not active)
        if (!API::get()->param()->vr->is_hmd_active()) {
            if (!m_was_rendering_desktop) {
                m_was_rendering_desktop = true;
                on_device_reset();
                return;
            }

            m_was_rendering_desktop = true;

            // Proper ImGui frame management for desktop
            ImGui_ImplWin32_NewFrame();
            
            if (renderer_data->renderer_type == UEVR_RENDERER_D3D11) {
                ImGui_ImplDX11_NewFrame();
            } else if (renderer_data->renderer_type == UEVR_RENDERER_D3D12) {
                auto command_queue = (ID3D12CommandQueue*)renderer_data->command_queue;
                if (command_queue == nullptr) {
                    return;
                }
                ImGui_ImplDX12_NewFrame();
            }

            ImGui::NewFrame();

            // Render our UI
            render_cutscene_ui();

            ImGui::EndFrame();
            ImGui::Render();

            // Submit to renderer
            if (renderer_data->renderer_type == UEVR_RENDERER_D3D11) {
                g_d3d11.render_imgui();
            } else if (renderer_data->renderer_type == UEVR_RENDERER_D3D12) {
                g_d3d12.render_imgui();
            }
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

    void on_post_render_vr_framework_dx11(ID3D11DeviceContext* context, ID3D11Texture2D*, ID3D11RenderTargetView* rtv) override {
        const auto vr_active = API::get()->param()->vr->is_hmd_active();
        
        if (!m_initialized || !vr_active) {
            return;
        }
        
        if (m_was_rendering_desktop) {
            m_was_rendering_desktop = false;
            on_device_reset();
            return;
        }
        
        std::scoped_lock _{m_imgui_mutex};
        
        // Proper ImGui frame management for VR
        ImGui_ImplWin32_NewFrame();
        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
        
        render_cutscene_ui();
        
        ImGui::EndFrame();
        ImGui::Render();
        
        g_d3d11.render_imgui_vr(context, rtv);
    }

    void on_post_render_vr_framework_dx12(ID3D12GraphicsCommandList* command_list, ID3D12Resource*, D3D12_CPU_DESCRIPTOR_HANDLE* rtv) override {
        const auto vr_active = API::get()->param()->vr->is_hmd_active();
        
        if (!m_initialized || !vr_active) {
            return;
        }
        
        if (m_was_rendering_desktop) {
            m_was_rendering_desktop = false;
            on_device_reset();
            return;
        }
        
        std::scoped_lock _{m_imgui_mutex};
        
        // Proper ImGui frame management for VR
        ImGui_ImplWin32_NewFrame();
        ImGui_ImplDX12_NewFrame();
        ImGui::NewFrame();
        
        render_cutscene_ui();
        
        ImGui::EndFrame();
        ImGui::Render();
        
        g_d3d12.render_imgui_vr(command_list, rtv);
    }

    void on_pre_engine_tick(API::UGameEngine*, float) override {
        update_cutscene_status();
        dampen_camera_shake();
    }

    bool on_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) override { 
        if (m_initialized) {
            ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
            return !ImGui::GetIO().WantCaptureMouse && !ImGui::GetIO().WantCaptureKeyboard;
        }
        return true;
    }

private:
    bool initialize_imgui() {
        if (m_initialized) {
            return true;
        }

        API::get()->log_info("[CutsceneDetectionPlugin] Starting ImGui initialization");

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        const auto renderer_data = API::get()->param()->renderer;
        DXGI_SWAP_CHAIN_DESC swap_desc{};
        auto swapchain = (IDXGISwapChain*)renderer_data->swapchain;
        if (FAILED(swapchain->GetDesc(&swap_desc))) {
            API::get()->log_error("[CutsceneDetectionPlugin] Failed to get swapchain description");
            return false;
        }
        m_wnd = swap_desc.OutputWindow;

        if (!ImGui_ImplWin32_Init(m_wnd)) {
            API::get()->log_error("[CutsceneDetectionPlugin] Failed to initialize ImGui Win32");
            return false;
        }

        // Initialize renderer-specific ImGui
        if (renderer_data->renderer_type == UEVR_RENDERER_D3D11) {
            if (!g_d3d11.initialize()) {
                API::get()->log_error("[CutsceneDetectionPlugin] Failed to initialize D3D11 renderer");
                return false;
            }
        } else if (renderer_data->renderer_type == UEVR_RENDERER_D3D12) {
            if (!g_d3d12.initialize()) {
                API::get()->log_error("[CutsceneDetectionPlugin] Failed to initialize D3D12 renderer");
                return false;
            }
        }

        m_initialized = true;
        API::get()->log_info("[CutsceneDetectionPlugin] ImGui initialized successfully");
        return true;
    }

    void render_cutscene_ui() {
        // Always show the window (even if detection fails)
        if (ImGui::Begin("Cutscene Status", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Status: %s", m_in_cutscene ? "IN CUTSCENE" : "Not in cutscene");
            
            // Add some visual feedback
            if (m_in_cutscene) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), " [ACTIVE]");
            }
            
            ImGui::Separator();
            ImGui::Text("Camera Target: %s", m_camera_target_name.c_str());
            ImGui::Text("Target Class: %s", m_target_class_name.c_str());
            
            // Add debug info
            ImGui::Separator();
            ImGui::Text("Plugin Status: Loaded");
            ImGui::Text("Frame Count: %d", ImGui::GetFrameCount());

            ImGui::Separator();
            if (ImGui::Checkbox("Disable UObjectHook in cutscenes", &m_disable_uobjecthook)) {
                update_uobjecthook_state();
            }
            ImGui::Checkbox("Dampen camera in cutscenes", &m_dampen_camera);
            ImGui::SliderFloat("Shake threshold", &m_dampen_threshold, 0.1f, 5.0f, "%.2f deg");
        }
        ImGui::End();
    }

    void update_cutscene_status() {
        auto& api = API::get();
        
        // Get player controller
        auto pc = api->get_player_controller(0);
        if (pc == nullptr) {
            set_cutscene_status(false, "No Player Controller", "N/A");
            return;
        }

        // Get player camera manager
        auto pcm_ptr = pc->get_property_data<API::UObject*>(L"PlayerCameraManager");
        auto pcm = pcm_ptr ? *pcm_ptr : nullptr;
        if (pcm == nullptr) {
            set_cutscene_status(false, "No Camera Manager", "N/A");
            return;
        }

        // Get ViewTarget structure
        struct ViewTargetStruct { 
            API::UObject* Target; 
        };
        
        auto vt = pcm->get_property_data<ViewTargetStruct>(L"ViewTarget");
        if (vt == nullptr || vt->Target == nullptr) {
            set_cutscene_status(false, "No ViewTarget", "N/A");
            return;
        }

        // Get target object info
        auto target_fname = vt->Target->get_fname();
        auto target_class = vt->Target->get_class();
        
        // Get wide strings
        std::wstring target_name_wide = target_fname ? target_fname->to_string() : L"Unknown";
        std::wstring target_class_name_wide = target_class && target_class->get_fname() ? target_class->get_fname()->to_string() : L"Unknown";

        // Convert wide strings to narrow strings
        std::string target_name_str;
        std::string target_class_name_str;
        
        if (!target_name_wide.empty()) {
            int len = WideCharToMultiByte(CP_UTF8, 0, target_name_wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                target_name_str.resize(len - 1);
                WideCharToMultiByte(CP_UTF8, 0, target_name_wide.c_str(), -1, &target_name_str[0], len, nullptr, nullptr);
            }
        }
        
        if (!target_class_name_wide.empty()) {
            int len = WideCharToMultiByte(CP_UTF8, 0, target_class_name_wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                target_class_name_str.resize(len - 1);
                WideCharToMultiByte(CP_UTF8, 0, target_class_name_wide.c_str(), -1, &target_class_name_str[0], len, nullptr, nullptr);
            }
        }

        // Check if target is a CineCameraActor
        static auto cine_class = api->find_uobject<API::UClass>(L"Class /Script/CinematicCamera.CineCameraActor");
        if (cine_class == nullptr) {
            set_cutscene_status(false, target_name_str, "CineCameraActor class not found");
            return;
        }

        bool is_cutscene = vt->Target->is_a(cine_class);
        set_cutscene_status(is_cutscene, target_name_str, target_class_name_str);
    }

    void set_cutscene_status(bool in_cutscene, const std::string& target_name, const std::string& class_name) {
        if (m_in_cutscene != in_cutscene) {
            m_in_cutscene = in_cutscene;
            API::get()->log_info("[CutsceneDetectionPlugin] Cutscene status changed: %s",
                in_cutscene ? "ENTERED CUTSCENE" : "EXITED CUTSCENE");
            update_uobjecthook_state();
            if (!m_in_cutscene) {
                m_last_control_rotation.reset();
            }
        }

        m_camera_target_name = target_name;
        m_target_class_name = class_name;
    }

    void update_uobjecthook_state() {
        if (!m_disable_uobjecthook) {
            if (m_uobjecthook_disabled_due_to_cutscene) {
                API::UObjectHook::set_disabled(false);
                m_uobjecthook_disabled_due_to_cutscene = false;
            }
            return;
        }

        if (m_in_cutscene && !m_uobjecthook_disabled_due_to_cutscene) {
            API::UObjectHook::set_disabled(true);
            m_uobjecthook_disabled_due_to_cutscene = true;
        } else if (!m_in_cutscene && m_uobjecthook_disabled_due_to_cutscene) {
            API::UObjectHook::set_disabled(false);
            m_uobjecthook_disabled_due_to_cutscene = false;
        }
    }

    void dampen_camera_shake() {
        if (!m_dampen_camera || !m_in_cutscene) {
            return;
        }

        auto pc = API::get()->get_player_controller(0);
        if (pc == nullptr) {
            return;
        }

        struct GetRotParams { UEVR_Rotatorf return_value; } get_params{};
        pc->call_function(L"GetControlRotation", &get_params);
        auto current = get_params.return_value;

        if (m_last_control_rotation.has_value()) {
            auto last = m_last_control_rotation.value();
            auto diff_pitch = std::abs(current.pitch - last.pitch);
            auto diff_yaw = std::abs(current.yaw - last.yaw);
            auto diff_roll = std::abs(current.roll - last.roll);

            if (diff_pitch <= m_dampen_threshold && diff_yaw <= m_dampen_threshold && diff_roll <= m_dampen_threshold) {
                struct SetRotParams { UEVR_Rotatorf rot; } set_params{ last };
                pc->call_function(L"SetControlRotation", &set_params);
            } else {
                m_last_control_rotation = current;
            }
        } else {
            m_last_control_rotation = current;
        }
    }

private:
    HWND m_wnd{};
    bool m_initialized{false};
    bool m_was_rendering_desktop{false};
    bool m_in_cutscene{false};
    std::string m_camera_target_name{"Unknown"};
    std::string m_target_class_name{"Unknown"};
    bool m_disable_uobjecthook{true};
    bool m_uobjecthook_disabled_due_to_cutscene{false};
    bool m_dampen_camera{true};
    float m_dampen_threshold{1.0f};
    std::optional<UEVR_Rotatorf> m_last_control_rotation{};
    std::recursive_mutex m_imgui_mutex{};
};

// Plugin entry point
std::unique_ptr<CutsceneDetectionPlugin> g_plugin{std::make_unique<CutsceneDetectionPlugin>()};

