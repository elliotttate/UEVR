// Sn2OverlayUI.cpp
//
// ImGui body for sn2_overlay_ui::render(). The header declares it; this is the
// single TU that pulls in imgui.h, which avoids ODR mismatches between
// per-TU "stub" and "real" inline definitions.

#include "Sn2OverlayUI.hpp"

#include <imgui.h>
#include <mutex>

namespace sn2_overlay_ui {

void render() {
    if (!env_enabled()) return;

    bool open = true;
    if (!ImGui::Begin("SN2 Diagnostics", &open, 0)) {
        ImGui::End();
        return;
    }

    ImGui::Text("UEVR_SN2_OVERLAY_UI=1");
    ImGui::Separator();

    // ===== Draws =====
    ImGui::Text("Recent pso3069 draws (ring buffer, newest last)");
    {
        std::scoped_lock _{draw_mu()};
        auto& ring = draw_ring();
        size_t cnt = draw_ring_count().load();
        size_t head = draw_ring_head().load();
        if (ImGui::BeginTable("draws", 8, 0)) {
            ImGui::TableSetupColumn("seq");
            ImGui::TableSetupColumn("frame");
            ImGui::TableSetupColumn("eye");
            ImGui::TableSetupColumn("ps_crc");
            ImGui::TableSetupColumn("pass");
            ImGui::TableSetupColumn("cb0_fp");
            ImGui::TableSetupColumn("match?");
            ImGui::TableSetupColumn("ovr?");
            ImGui::TableHeadersRow();
            size_t start = (head >= cnt) ? (head - cnt) : 0;
            for (size_t i = 0; i < cnt; ++i) {
                const auto& r = ring[(start + i) % k_max_rows];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("%llu", (unsigned long long)r.seq);
                ImGui::TableSetColumnIndex(1); ImGui::Text("%llu", (unsigned long long)r.frame_idx);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%s", r.eye_name);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%08x", r.ps_crc);
                ImGui::TableSetColumnIndex(4); ImGui::Text("%s", r.pass_tag);
                ImGui::TableSetColumnIndex(5); ImGui::Text("%016llx", (unsigned long long)r.cb0_fp);
                ImGui::TableSetColumnIndex(6);
                if (r.view_idx == 1) {
                    if (r.cb0_matches_left) ImGui::Text("YES");
                    else                    ImGui::Text("NO");
                } else { ImGui::Text("-"); }
                ImGui::TableSetColumnIndex(7); ImGui::Text("%s", r.override_fired ? "YES" : "no");
            }
            ImGui::EndTable();
        }
    }

    ImGui::Separator();
    ImGui::Text("Recent override fires");
    {
        std::scoped_lock _{ov_mu()};
        auto& r = ov_ring();
        size_t cnt = ov_count().load();
        size_t head = ov_head().load();
        if (ImGui::BeginTable("overrides", 6, 0)) {
            ImGui::TableSetupColumn("seq");
            ImGui::TableSetupColumn("frame");
            ImGui::TableSetupColumn("kind");
            ImGui::TableSetupColumn("view");
            ImGui::TableSetupColumn("bound?");
            ImGui::TableSetupColumn("ps_crc");
            ImGui::TableHeadersRow();
            size_t start = (head >= cnt) ? (head - cnt) : 0;
            for (size_t i = 0; i < cnt; ++i) {
                const auto& rr = r[(start + i) % k_max_rows];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("%llu", (unsigned long long)rr.seq);
                ImGui::TableSetColumnIndex(1); ImGui::Text("%llu", (unsigned long long)rr.frame);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%s", rr.kind);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%d", rr.view_idx);
                ImGui::TableSetColumnIndex(4); ImGui::Text("%s", rr.scratch_bound ? "YES" : "no");
                ImGui::TableSetColumnIndex(5); ImGui::Text("%08x", rr.target_ps_crc);
            }
            ImGui::EndTable();
        }
    }

    ImGui::End();
}

} // namespace sn2_overlay_ui
