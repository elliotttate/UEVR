/*
FPS Plotter Plugin for UEVR
Adds advanced plotting capabilities including ImGui PlotLines and PlotHistogram functions

Copyright (c) 2024 UEVR Community

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <uevr/Plugin.hpp>
#include <uevr/API.hpp>
#include <vector>
#include <deque>
#include <chrono>
#include <fstream>
#include <sstream>

using namespace uevr;

class FPSPlotterPlugin : public Plugin {
private:
    // FPS tracking data
    std::deque<float> fps_history;
    std::deque<std::chrono::high_resolution_clock::time_point> frame_times;
    std::chrono::high_resolution_clock::time_point last_frame_time;
    
    // Configuration
    size_t max_history_size = 300; // 5 minutes at 60 FPS
    bool show_window = true;
    bool auto_scale = true;
    float min_fps = 0.0f;
    float max_fps = 120.0f;
    
    // UI state
    bool show_line_plot = true;
    bool show_histogram = true;
    bool show_stats = true;
    bool show_controls = true;
    
    // Export data
    std::string export_filename;
    bool export_data = false;

public:
    FPSPlotterPlugin() {
        last_frame_time = std::chrono::high_resolution_clock::now();
    }

    virtual ~FPSPlotterPlugin() = default;

    virtual void on_frame() override {
        // Calculate FPS
        auto current_time = std::chrono::high_resolution_clock::now();
        auto delta_time = std::chrono::duration<float>(current_time - last_frame_time).count();
        float fps = delta_time > 0.0f ? 1.0f / delta_time : 0.0f;
        
        // Store data
        fps_history.push_back(fps);
        frame_times.push_back(current_time);
        
        // Maintain history size
        if (fps_history.size() > max_history_size) {
            fps_history.pop_front();
            frame_times.pop_front();
        }
        
        last_frame_time = current_time;
        
        // Auto-scale if enabled
        if (auto_scale && !fps_history.empty()) {
            auto min_max = std::minmax_element(fps_history.begin(), fps_history.end());
            min_fps = *min_max.first;
            max_fps = *min_max.second;
            if (max_fps - min_fps < 1.0f) {
                max_fps = min_fps + 1.0f;
            }
        }
    }

    virtual void on_draw_ui() override {
        if (!show_window) return;

        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
        
        if (ImGui::Begin("FPS Plotter Plugin", &show_window)) {
            // Controls
            if (show_controls) {
                ImGui::BeginGroup();
                ImGui::Text("Controls");
                
                ImGui::Checkbox("Auto Scale", &auto_scale);
                ImGui::SameLine();
                ImGui::Checkbox("Show Line Plot", &show_line_plot);
                ImGui::SameLine();
                ImGui::Checkbox("Show Histogram", &show_histogram);
                ImGui::SameLine();
                ImGui::Checkbox("Show Stats", &show_stats);
                
                if (!auto_scale) {
                    ImGui::DragFloat("Min FPS", &min_fps, 1.0f, 0.0f, 1000.0f);
                    ImGui::DragFloat("Max FPS", &max_fps, 1.0f, 0.0f, 1000.0f);
                }
                
                ImGui::DragInt("History Size", (int*)&max_history_size, 10.0f, 60, 1800);
                
                ImGui::Separator();
                ImGui::EndGroup();
            }
            
            // Statistics
            if (show_stats && !fps_history.empty()) {
                ImGui::BeginGroup();
                ImGui::Text("Statistics");
                
                float current_fps = fps_history.back();
                float avg_fps = 0.0f;
                float min_fps_actual = std::numeric_limits<float>::max();
                float max_fps_actual = 0.0f;
                
                for (float fps : fps_history) {
                    avg_fps += fps;
                    min_fps_actual = std::min(min_fps_actual, fps);
                    max_fps_actual = std::max(max_fps_actual, fps);
                }
                avg_fps /= fps_history.size();
                
                ImGui::Text("Current FPS: %.1f", current_fps);
                ImGui::Text("Average FPS: %.1f", avg_fps);
                ImGui::Text("Min FPS: %.1f", min_fps_actual);
                ImGui::Text("Max FPS: %.1f", max_fps_actual);
                ImGui::Text("Frame Time: %.2f ms", 1000.0f / current_fps);
                ImGui::Text("History: %zu frames", fps_history.size());
                
                ImGui::Separator();
                ImGui::EndGroup();
            }
            
            // Line Plot
            if (show_line_plot && !fps_history.empty()) {
                ImGui::BeginGroup();
                ImGui::Text("FPS Over Time");
                
                std::vector<float> plot_data(fps_history.begin(), fps_history.end());
                ImGui::PlotLines("FPS", plot_data.data(), (int)plot_data.size(), 0, 
                                nullptr, min_fps, max_fps, ImVec2(400, 150));
                
                ImGui::EndGroup();
            }
            
            // Histogram
            if (show_histogram && !fps_history.empty()) {
                ImGui::BeginGroup();
                ImGui::Text("FPS Distribution");
                
                std::vector<float> plot_data(fps_history.begin(), fps_history.end());
                ImGui::PlotHistogram("FPS Histogram", plot_data.data(), (int)plot_data.size(), 0,
                                    nullptr, min_fps, max_fps, ImVec2(400, 150));
                
                ImGui::EndGroup();
            }
            
            // Export functionality
            ImGui::Separator();
            ImGui::Text("Export Data");
            
            static char filename[256] = "fps_data.csv";
            ImGui::InputText("Filename", filename, sizeof(filename));
            
            if (ImGui::Button("Export to CSV")) {
                export_to_csv(filename);
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Clear Data")) {
                fps_history.clear();
                frame_times.clear();
            }
        }
        ImGui::End();
    }

private:
    void export_to_csv(const std::string& filename) {
        std::ofstream file(filename);
        if (!file.is_open()) return;
        
        file << "Frame,Timestamp,FPS\n";
        
        for (size_t i = 0; i < fps_history.size(); ++i) {
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                frame_times[i].time_since_epoch()).count();
            file << i << "," << duration << "," << fps_history[i] << "\n";
        }
        
        file.close();
    }
};

extern "C" __declspec(dllexport) Plugin* create_plugin() {
    return new FPSPlotterPlugin();
}

extern "C" __declspec(dllexport) void destroy_plugin(Plugin* plugin) {
    delete plugin;
} 