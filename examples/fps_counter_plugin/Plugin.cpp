#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <algorithm>
#include <Windows.h>
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

class FPSCounterPlugin : public Plugin {
public:
    FPSCounterPlugin() = default;

    void on_initialize() override {
        API::get()->log_info("[FPSCounterPlugin] Initialized!");
        m_initialized = false;
        m_window_open = true;
        m_show_graph = true;
        m_graph_height = 200.0f;
        m_graph_width = 600.0f;
        m_target_fps = 60.0f;
        m_warning_fps = 45.0f;
        m_critical_fps = 30.0f;
        m_show_performance_zones = true;
        m_auto_scale = true;
        m_graph_max_fps = 120.0f;
        m_graph_min_fps = 0.0f;
        reset_stats();
    }

    void on_present() override {
        std::scoped_lock _{m_imgui_mutex};
        if (!m_initialized) {
            if (!initialize_imgui()) {
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
        
        setup_imgui_style();
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

    void on_pre_engine_tick(API::UGameEngine* engine, float delta) override {
        m_frame_count++;
        m_elapsed_time += delta;
        if (m_elapsed_time >= 1.0f) {
            m_current_fps = static_cast<float>(m_frame_count) / m_elapsed_time;
            m_fps_history.push_back(m_current_fps);
            if (m_fps_history.size() > 60) {
                m_fps_history.erase(m_fps_history.begin());
            }
            update_stats();
            m_frame_count = 0;
            m_elapsed_time = 0.0f;
        }
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
        
        m_initialized = true;
        return true;
    }

    void setup_imgui_style() {
        ImGuiStyle& style = ImGui::GetStyle();
        
        // Beautiful theme from ImGUI Advanced Cheat Menu with dark colors and red accents
        
        // ROUNDINGS - Matching the advanced cheat menu
        style.WindowRounding = 6.0f;
        style.ChildRounding = 6.0f;
        style.FrameRounding = 2.0f;
        style.GrabRounding = 2.0f;
        style.PopupRounding = 2.0f;
        style.ScrollbarRounding = 2.0f;
        style.TabRounding = 2.0f;
        
        // SPACING - Matching the advanced cheat menu
        style.ScrollbarSize = 9.0f;
        style.FramePadding = ImVec2(6, 3);
        style.ItemSpacing = ImVec2(4, 4);
        style.WindowPadding = ImVec2(8, 8);
        style.ItemInnerSpacing = ImVec2(4, 4);
        style.IndentSpacing = 21.0f;
        style.GrabMinSize = 10.0f;
        
        // BORDERS
        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.TabBorderSize = 0.0f;
        
        // COLORS - Beautiful dark theme with red accents from Advanced Cheat Menu
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.90f);                    // winCol: rgba(0, 0, 0, 230)
        style.Colors[ImGuiCol_Border] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);                       // Transparent border
        style.Colors[ImGuiCol_Button] = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);                    // bgCol: rgba(31, 30, 31, 255)
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.94f, 0.29f, 0.35f, 1.0f);              // btnActiveCol: rgba(239, 73, 88, 255)
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.68f, 0.22f, 0.25f, 1.0f);             // btnHoverCol: rgba(173, 55, 65, 255)
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);                   // bgCol: rgba(31, 30, 31, 255)
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.17f, 0.17f, 0.17f, 1.0f);             // frameCol: rgba(44, 43, 44, 255)
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.14f, 0.15f, 1.0f);            // hoverCol: rgba(37, 36, 37, 255)
        style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);                         // textCol: rgba(255, 255, 255, 255)
        style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);              // notSelectedTextColor: rgba(140, 140, 140, 255)
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.13f, 0.13f, 0.18f, 1.0f);                   // childCol: rgba(33, 34, 45, 255)
        style.Colors[ImGuiCol_CheckMark] = ImVec4(0.94f, 0.20f, 0.26f, 1.0f);                 // itemActiveCol: rgba(240, 50, 66, 255)
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.94f, 0.29f, 0.35f, 1.0f);                // itemCol: rgba(240, 74, 88, 255)
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.94f, 0.20f, 0.26f, 1.0f);          // itemActiveCol: rgba(240, 50, 66, 255)
        style.Colors[ImGuiCol_Header] = ImVec4(0.94f, 0.20f, 0.26f, 1.0f);                    // itemActiveCol: rgba(240, 50, 66, 255)
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.94f, 0.29f, 0.35f, 1.0f);             // itemCol: rgba(240, 74, 88, 255)
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.94f, 0.20f, 0.26f, 1.0f);              // itemActiveCol: rgba(240, 50, 66, 255)
        style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.86f, 0.20f, 0.26f, 0.47f);               // resizeGripCol: rgba(220, 50, 66, 120)
        style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.98f, 0.20f, 0.26f, 0.55f);        // resizeGripHoverCol: rgba(250, 50, 66, 140)
        style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.94f, 0.20f, 0.26f, 1.0f);          // itemActiveCol: rgba(240, 50, 66, 255)
        style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.98f, 0.20f, 0.26f, 0.55f);         // resizeGripHoverCol: rgba(250, 50, 66, 140)
        style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.94f, 0.20f, 0.26f, 1.0f);           // itemActiveCol: rgba(240, 50, 66, 255)
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.94f, 0.20f, 0.26f, 1.0f);             // itemActiveCol: rgba(240, 50, 66, 255)
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.0f);                   // Dark title background
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.13f, 0.13f, 0.18f, 0.94f);                  // childCol with alpha
        style.Colors[ImGuiCol_PlotLines] = ImVec4(0.94f, 0.29f, 0.35f, 1.0f);                 // Red accent for plot lines
        style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.98f, 0.40f, 0.45f, 1.0f);          // Lighter red for hover
        style.Colors[ImGuiCol_Separator] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);                // Subtle separator
        style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);              // Dark scrollbar background
        style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.0f);             // Scrollbar grab
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.0f);      // Scrollbar grab hovered
        style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.0f);       // Scrollbar grab active
        style.Colors[ImGuiCol_Tab] = ImVec4(0.18f, 0.18f, 0.23f, 1.0f);                       // Tab background
        style.Colors[ImGuiCol_TabHovered] = ImVec4(0.94f, 0.29f, 0.35f, 0.80f);               // Tab hovered
        style.Colors[ImGuiCol_TabActive] = ImVec4(0.94f, 0.20f, 0.26f, 1.0f);                 // Tab active
        style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.15f, 0.15f, 0.20f, 1.0f);              // Tab unfocused
        style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.20f, 0.20f, 0.25f, 1.0f);        // Tab unfocused active
    }

    void draw_ui() {
        if (!m_window_open) return;
        
        ImGui::SetNextWindowSize(ImVec2(m_graph_width + 50, m_graph_height + 300), ImGuiCond_FirstUseEver);
        
        if (ImGui::Begin("🎯 FPS Counter & Performance Monitor", &m_window_open, ImGuiWindowFlags_AlwaysAutoResize)) {
            // Performance status indicator
            draw_performance_status();
            
            ImGui::Separator();
            
            // Current FPS display with color coding
            draw_fps_display();
            
            ImGui::Separator();
            
            // Configuration controls
            draw_configuration_panel();
            
            ImGui::Separator();
            
            // Main FPS graph
            if (m_show_graph) {
                draw_fps_graph();
            }
            
            ImGui::Separator();
            
            // Statistics panel
            draw_statistics_panel();
            
            ImGui::Separator();
            
            // Control buttons
            draw_control_buttons();
        }
        ImGui::End();
    }

    void draw_performance_status() {
        ImGui::Text("Performance Status:");
        ImGui::SameLine();
        
        ImVec4 status_color;
        const char* status_text;
        const char* status_icon;
        
        if (m_current_fps >= m_target_fps) {
            status_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // Green
            status_text = "EXCELLENT";
            status_icon = "✓";
        } else if (m_current_fps >= m_warning_fps) {
            status_color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow
            status_text = "GOOD";
            status_icon = "⚠";
        } else if (m_current_fps >= m_critical_fps) {
            status_color = ImVec4(1.0f, 0.5f, 0.0f, 1.0f); // Orange
            status_text = "WARNING";
            status_icon = "⚠";
        } else {
            status_color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // Red
            status_text = "CRITICAL";
            status_icon = "⚠";
        }
        
        ImGui::TextColored(status_color, "%s %s", status_icon, status_text);
    }

    void draw_fps_display() {
        ImGui::Text("Current FPS:");
        ImGui::SameLine();
        
        // Color code the FPS value
        ImVec4 fps_color;
        if (m_current_fps >= m_target_fps) {
            fps_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // Green
        } else if (m_current_fps >= m_warning_fps) {
            fps_color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow
        } else if (m_current_fps >= m_critical_fps) {
            fps_color = ImVec4(1.0f, 0.5f, 0.0f, 1.0f); // Orange
        } else {
            fps_color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // Red
        }
        
        ImGui::TextColored(fps_color, "%.1f", m_current_fps);
        
        // Frame time
        ImGui::SameLine();
        ImGui::Text("  |  Frame Time: %.2f ms", m_current_fps > 0 ? 1000.0f / m_current_fps : 0.0f);
    }

    void draw_configuration_panel() {
        if (ImGui::CollapsingHeader("Configuration", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Columns(2, "ConfigColumns", false);
            
            ImGui::Checkbox("Show Graph", &m_show_graph);
            ImGui::Checkbox("Show Performance Zones", &m_show_performance_zones);
            ImGui::Checkbox("Auto Scale", &m_auto_scale);
            
            ImGui::NextColumn();
            
            ImGui::SliderFloat("Target FPS", &m_target_fps, 30.0f, 120.0f, "%.0f");
            ImGui::SliderFloat("Warning FPS", &m_warning_fps, 20.0f, 60.0f, "%.0f");
            ImGui::SliderFloat("Critical FPS", &m_critical_fps, 10.0f, 40.0f, "%.0f");
            
            ImGui::Columns(1);
        }
    }

    void draw_fps_graph() {
        if (m_fps_history.empty()) return;
        
        ImGui::Text("FPS Graph (Last 60 seconds)");
        
        // Auto-scale the graph
        if (m_auto_scale && !m_fps_history.empty()) {
            float min_fps = m_fps_history[0];
            float max_fps = m_fps_history[0];
            for (float fps : m_fps_history) {
                if (fps < min_fps) min_fps = fps;
                if (fps > max_fps) max_fps = fps;
            }
            m_graph_min_fps = (min_fps > 10.0f) ? min_fps - 10.0f : 0.0f;
            m_graph_max_fps = max_fps + 10.0f;
        }
        
        // Create a copy of the data for plotting
        std::vector<float> plot_data = m_fps_history;
        
        // Custom plot function with beautiful colored background zones
        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        ImVec2 canvas_size = ImVec2(m_graph_width, m_graph_height);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        
        // Background with beautiful gradient matching the Advanced Cheat Menu theme
        draw_list->AddRectFilledMultiColor(
            canvas_pos, 
            ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
            IM_COL32(33, 34, 45, 255),    // Top-left: childCol
            IM_COL32(38, 39, 50, 255),    // Top-right: childCol1
            IM_COL32(31, 30, 31, 255),    // Bottom-right: bgCol
            IM_COL32(44, 43, 44, 255)     // Bottom-left: frameCol
        );
        
        // Performance zones with beautiful gradients
        if (m_show_performance_zones) {
            draw_performance_zones_beautiful(draw_list, canvas_pos, canvas_size);
        }
        
        // Grid lines with subtle glow
        draw_grid_lines_beautiful(draw_list, canvas_pos, canvas_size);
        
        // FPS curve with enhanced styling
        draw_fps_curve_beautiful(draw_list, canvas_pos, canvas_size, plot_data);
        
        // Beautiful border with red accent theme
        draw_list->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), 
                          IM_COL32(240, 74, 88, 180), 0.0f, 0, 2.0f);
        
        // Invisible button for interaction
        ImGui::SetCursorScreenPos(canvas_pos);
        ImGui::InvisibleButton("fps_graph", canvas_size);
        
        // Enhanced tooltip on hover
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("🎯 Performance Monitor");
            ImGui::Separator();
            ImGui::Text("Current FPS: %.1f", m_current_fps);
            ImGui::Text("Min FPS: %.1f", m_min_fps);
            ImGui::Text("Max FPS: %.1f", m_max_fps);
            ImGui::Text("Average FPS: %.1f", m_avg_fps);
            ImGui::Text("Samples: %d", static_cast<int>(m_fps_history.size()));
            ImGui::EndTooltip();
        }
        
        ImGui::SetCursorScreenPos(ImVec2(canvas_pos.x, canvas_pos.y + canvas_size.y + 10));
    }

    void draw_performance_zones_beautiful(ImDrawList* draw_list, ImVec2 canvas_pos, ImVec2 canvas_size) {
        // Calculate Y positions for performance thresholds
        float target_y = canvas_pos.y + canvas_size.y - ((m_target_fps - m_graph_min_fps) / (m_graph_max_fps - m_graph_min_fps)) * canvas_size.y;
        float warning_y = canvas_pos.y + canvas_size.y - ((m_warning_fps - m_graph_min_fps) / (m_graph_max_fps - m_graph_min_fps)) * canvas_size.y;
        float critical_y = canvas_pos.y + canvas_size.y - ((m_critical_fps - m_graph_min_fps) / (m_graph_max_fps - m_graph_min_fps)) * canvas_size.y;
        
        // Excellent zone (green gradient)
        if (target_y >= canvas_pos.y) {
            draw_list->AddRectFilledMultiColor(
                canvas_pos, 
                ImVec2(canvas_pos.x + canvas_size.x, target_y),
                IM_COL32(0, 255, 100, 40),    // Top-left: Bright green
                IM_COL32(50, 255, 150, 40),   // Top-right: Cyan-green
                IM_COL32(100, 255, 200, 20),  // Bottom-right: Light cyan-green
                IM_COL32(0, 255, 100, 20)     // Bottom-left: Green
            );
        }
        
        // Good zone (yellow-green gradient)
        if (warning_y >= canvas_pos.y && target_y < canvas_pos.y + canvas_size.y) {
            draw_list->AddRectFilledMultiColor(
                ImVec2(canvas_pos.x, target_y), 
                ImVec2(canvas_pos.x + canvas_size.x, warning_y),
                IM_COL32(255, 255, 0, 30),    // Top-left: Yellow
                IM_COL32(255, 200, 0, 30),    // Top-right: Orange-yellow
                IM_COL32(255, 150, 0, 15),    // Bottom-right: Light orange
                IM_COL32(255, 255, 50, 15)    // Bottom-left: Light yellow
            );
        }
        
        // Warning zone (orange gradient)
        if (critical_y >= canvas_pos.y && warning_y < canvas_pos.y + canvas_size.y) {
            draw_list->AddRectFilledMultiColor(
                ImVec2(canvas_pos.x, warning_y), 
                ImVec2(canvas_pos.x + canvas_size.x, critical_y),
                IM_COL32(255, 150, 0, 35),    // Top-left: Orange
                IM_COL32(255, 100, 0, 35),    // Top-right: Red-orange
                IM_COL32(255, 50, 0, 20),     // Bottom-right: Light red-orange
                IM_COL32(255, 120, 0, 20)     // Bottom-left: Light orange
            );
        }
        
        // Critical zone (red gradient)
        if (critical_y < canvas_pos.y + canvas_size.y) {
            draw_list->AddRectFilledMultiColor(
                ImVec2(canvas_pos.x, critical_y), 
                ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
                IM_COL32(255, 0, 0, 40),      // Top-left: Red
                IM_COL32(200, 0, 50, 40),     // Top-right: Dark red
                IM_COL32(150, 0, 100, 25),    // Bottom-right: Purple-red
                IM_COL32(255, 50, 50, 25)     // Bottom-left: Light red
            );
        }
        
        // Threshold lines with glow effect
        for (int i = 0; i < 3; ++i) {
            float alpha = (3 - i) * 0.3f;
            float thickness = 1.0f + i * 0.5f;
            
            // Target line (green)
            draw_list->AddLine(ImVec2(canvas_pos.x, target_y), ImVec2(canvas_pos.x + canvas_size.x, target_y), 
                              IM_COL32(0, 255, 100, static_cast<int>(255 * alpha)), thickness);
            
            // Warning line (yellow)
            draw_list->AddLine(ImVec2(canvas_pos.x, warning_y), ImVec2(canvas_pos.x + canvas_size.x, warning_y), 
                              IM_COL32(255, 255, 0, static_cast<int>(255 * alpha)), thickness);
            
            // Critical line (orange)
            draw_list->AddLine(ImVec2(canvas_pos.x, critical_y), ImVec2(canvas_pos.x + canvas_size.x, critical_y), 
                              IM_COL32(255, 128, 0, static_cast<int>(255 * alpha)), thickness);
        }
    }

    void draw_grid_lines_beautiful(ImDrawList* draw_list, ImVec2 canvas_pos, ImVec2 canvas_size) {
        // Horizontal grid lines with red accent theme
        for (int i = 1; i < 5; ++i) {
            float y = canvas_pos.y + (canvas_size.y / 5) * i;
            
            // Main line
            draw_list->AddLine(ImVec2(canvas_pos.x, y), ImVec2(canvas_pos.x + canvas_size.x, y), 
                              IM_COL32(140, 140, 140, 80), 1.0f);
            
            // Subtle red glow
            draw_list->AddLine(ImVec2(canvas_pos.x, y), ImVec2(canvas_pos.x + canvas_size.x, y), 
                              IM_COL32(240, 74, 88, 30), 3.0f);
        }
        
        // Vertical grid lines with red accent theme
        for (int i = 1; i < 10; ++i) {
            float x = canvas_pos.x + (canvas_size.x / 10) * i;
            
            // Main line
            draw_list->AddLine(ImVec2(x, canvas_pos.y), ImVec2(x, canvas_pos.y + canvas_size.y), 
                              IM_COL32(140, 140, 140, 80), 1.0f);
            
            // Subtle red glow
            draw_list->AddLine(ImVec2(x, canvas_pos.y), ImVec2(x, canvas_pos.y + canvas_size.y), 
                              IM_COL32(240, 74, 88, 30), 3.0f);
        }
    }

    void draw_fps_curve_beautiful(ImDrawList* draw_list, ImVec2 canvas_pos, ImVec2 canvas_size, const std::vector<float>& data) {
        if (data.size() < 2) return;
        
        // Draw the main FPS curve with enhanced styling
        for (size_t i = 1; i < data.size(); ++i) {
            float x1 = canvas_pos.x + ((float)(i - 1) / (data.size() - 1)) * canvas_size.x;
            float y1 = canvas_pos.y + canvas_size.y - ((data[i - 1] - m_graph_min_fps) / (m_graph_max_fps - m_graph_min_fps)) * canvas_size.y;
            float x2 = canvas_pos.x + ((float)i / (data.size() - 1)) * canvas_size.x;
            float y2 = canvas_pos.y + canvas_size.y - ((data[i] - m_graph_min_fps) / (m_graph_max_fps - m_graph_min_fps)) * canvas_size.y;
            
            // Color based on FPS value with enhanced colors
            ImU32 color;
            ImU32 glow_color;
            if (data[i] >= m_target_fps) {
                color = IM_COL32(0, 255, 100, 255);        // Bright green
                glow_color = IM_COL32(0, 255, 100, 80);    // Green glow
            } else if (data[i] >= m_warning_fps) {
                color = IM_COL32(255, 255, 0, 255);        // Yellow
                glow_color = IM_COL32(255, 255, 0, 80);    // Yellow glow
            } else if (data[i] >= m_critical_fps) {
                color = IM_COL32(255, 150, 0, 255);        // Orange
                glow_color = IM_COL32(255, 150, 0, 80);    // Orange glow
            } else {
                color = IM_COL32(255, 50, 50, 255);        // Red
                glow_color = IM_COL32(255, 50, 50, 80);    // Red glow
            }
            
            // Draw glow effect
            draw_list->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), glow_color, 4.0f);
            
            // Draw main line
            draw_list->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), color, 2.0f);
        }
        
        // Current FPS indicator with pulsing effect
        if (!data.empty()) {
            float x = canvas_pos.x + canvas_size.x - 5;
            float y = canvas_pos.y + canvas_size.y - ((data.back() - m_graph_min_fps) / (m_graph_max_fps - m_graph_min_fps)) * canvas_size.y;
            
            // Pulsing outer ring
            static float pulse_time = 0.0f;
            pulse_time += 0.05f;
            float pulse_alpha = (sinf(pulse_time) + 1.0f) * 0.5f;
            
            draw_list->AddCircleFilled(ImVec2(x, y), 6.0f, IM_COL32(255, 255, 255, static_cast<int>(100 * pulse_alpha)));
            draw_list->AddCircleFilled(ImVec2(x, y), 4.0f, IM_COL32(255, 255, 255, 255));
            draw_list->AddCircleFilled(ImVec2(x, y), 2.0f, IM_COL32(100, 150, 255, 255));
        }
        
        // Add subtle area fill under the curve
        if (data.size() >= 2) {
            // Create points for filled area
            std::vector<ImVec2> points;
            points.reserve(data.size() + 2);
            
            // Start from bottom-left
            points.push_back(ImVec2(canvas_pos.x, canvas_pos.y + canvas_size.y));
            
            // Add all curve points
            for (size_t i = 0; i < data.size(); ++i) {
                float x = canvas_pos.x + ((float)i / (data.size() - 1)) * canvas_size.x;
                float y = canvas_pos.y + canvas_size.y - ((data[i] - m_graph_min_fps) / (m_graph_max_fps - m_graph_min_fps)) * canvas_size.y;
                points.push_back(ImVec2(x, y));
            }
            
            // End at bottom-right
            points.push_back(ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y));
            
            // Draw filled area with gradient-like effect
            draw_list->AddConvexPolyFilled(points.data(), static_cast<int>(points.size()), IM_COL32(100, 150, 255, 30));
        }
    }

    void draw_statistics_panel() {
        if (ImGui::CollapsingHeader("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Columns(2, "StatsColumns", false);
            
            ImGui::Text("Min FPS: %.1f", m_min_fps);
            ImGui::Text("Max FPS: %.1f", m_max_fps);
            ImGui::Text("Average FPS: %.1f", m_avg_fps);
            
            ImGui::NextColumn();
            
            ImGui::Text("Samples: %d", static_cast<int>(m_fps_history.size()));
            ImGui::Text("Frame Time: %.2f ms", m_current_fps > 0 ? 1000.0f / m_current_fps : 0.0f);
            
            ImGui::Columns(1);
        }
    }

    void draw_control_buttons() {
        if (ImGui::Button("Reset Stats")) {
            reset_stats();
        }
    }

    void reset_stats() {
        m_frame_count = 0;
        m_elapsed_time = 0.0f;
        m_current_fps = 0.0f;
        m_fps_history.clear();
        m_min_fps = 9999.0f;
        m_max_fps = 0.0f;
        m_avg_fps = 0.0f;
    }

    void update_stats() {
        if (m_fps_history.empty()) {
            m_min_fps = 0.0f;
            m_max_fps = 0.0f;
            m_avg_fps = 0.0f;
            return;
        }
        
        m_min_fps = m_fps_history[0];
        m_max_fps = m_fps_history[0];
        float sum = 0.0f;
        
        for (float fps : m_fps_history) {
            if (fps < m_min_fps) m_min_fps = fps;
            if (fps > m_max_fps) m_max_fps = fps;
            sum += fps;
        }
        
        m_avg_fps = sum / m_fps_history.size();
    }

    HWND m_wnd{};
    bool m_initialized{false};
    bool m_window_open{true};
    std::recursive_mutex m_imgui_mutex{};
    
    int m_frame_count{0};
    float m_elapsed_time{0.0f};
    float m_current_fps{0.0f};
    std::vector<float> m_fps_history;
    float m_min_fps{9999.0f};
    float m_max_fps{0.0f};
    float m_avg_fps{0.0f};
    
    // Graph settings
    bool m_show_graph{true};
    bool m_show_performance_zones{true};
    bool m_auto_scale{true};
    float m_graph_height{200.0f};
    float m_graph_width{600.0f};
    float m_graph_max_fps{120.0f};
    float m_graph_min_fps{0.0f};
    
    // Performance thresholds
    float m_target_fps{60.0f};
    float m_warning_fps{45.0f};
    float m_critical_fps{30.0f};
};

static FPSCounterPlugin g_plugin; 