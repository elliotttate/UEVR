// Sn2OverlayUI.hpp
//
// ImGui overlay for live SN2 draw / override diagnostics.
//
// Provides a small in-process ring buffer of recent [SN2-DrawV2] +
// [SN2-OverrideFired] events, plus an ImGui::Begin/End block that renders
// them as a sortable table. Intended to be called from UEVR's existing
// on_draw_ui() / on_present_ui hooks.
//
// USAGE (in any UEVR ImGui context, e.g. OverlayComponent::on_draw_ui):
//   #include "Sn2OverlayUI.hpp"
//   sn2_overlay_ui::render();
//
// USAGE (in D3D12 draw-log emit sites):
//   sn2_overlay_ui::record_draw(...);
//   sn2_overlay_ui::record_override(...);
//
// Gated by UEVR_SN2_OVERLAY_UI=1 (separate from UEVR_SN2_DRAWLOG_V2 so the
// overlay can be toggled without enabling all log output).
//
#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

// render() is declared here and defined in Sn2OverlayUI.cpp (which is the only
// TU that pulls in imgui.h). Files that only call record_draw() /
// record_override() include this header without dragging in ImGui.

namespace sn2_overlay_ui {

inline bool env_enabled() {
    static const bool e = [](){
        const char* v = std::getenv("UEVR_SN2_OVERLAY_UI");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

// --- Recent draw row ---
struct DrawRow {
    uint64_t seq = 0;
    uint64_t frame_idx = 0;
    int      view_idx = -1;
    const char* eye_name = "?";
    uint32_t ps_crc = 0;
    const char* entry_function = nullptr;
    const char* pass_tag = "?";
    uint64_t cb0_fp = 0;
    uint64_t cmdlist_id = 0;
    uint64_t cb0_fp_paired_left = 0; // For per-eye comparison
    bool     cb0_matches_left = false;
    bool     override_fired = false;
};

inline std::mutex& draw_mu() { static std::mutex m; return m; }
inline constexpr size_t k_max_rows = 256;

inline std::array<DrawRow, k_max_rows>& draw_ring() {
    static std::array<DrawRow, k_max_rows> ring{};
    return ring;
}

inline std::atomic<size_t>& draw_ring_head() {
    static std::atomic<size_t> h{0};
    return h;
}

inline std::atomic<size_t>& draw_ring_count() {
    static std::atomic<size_t> c{0};
    return c;
}

inline void record_draw(const DrawRow& row) {
    if (!env_enabled()) return;
    std::scoped_lock _{draw_mu()};
    auto& ring = draw_ring();
    auto& head = draw_ring_head();
    auto& cnt = draw_ring_count();
    size_t idx = head.fetch_add(1, std::memory_order_relaxed) % k_max_rows;
    ring[idx] = row;
    if (cnt.load() < k_max_rows) cnt.fetch_add(1, std::memory_order_relaxed);
}

// --- Recent override row ---
struct OverrideRow {
    uint64_t seq = 0;
    uint64_t frame = 0;
    const char* kind = "?";
    int      view_idx = -1;
    bool     scratch_bound = false;
    uint32_t target_ps_crc = 0;
    uint64_t cmdlist_id = 0;
};

inline std::mutex& ov_mu() { static std::mutex m; return m; }
inline std::array<OverrideRow, k_max_rows>& ov_ring() {
    static std::array<OverrideRow, k_max_rows> ring{};
    return ring;
}
inline std::atomic<size_t>& ov_head() { static std::atomic<size_t> h{0}; return h; }
inline std::atomic<size_t>& ov_count() { static std::atomic<size_t> c{0}; return c; }

inline void record_override(const OverrideRow& row) {
    if (!env_enabled()) return;
    std::scoped_lock _{ov_mu()};
    auto& r = ov_ring();
    size_t idx = ov_head().fetch_add(1, std::memory_order_relaxed) % k_max_rows;
    r[idx] = row;
    if (ov_count().load() < k_max_rows) ov_count().fetch_add(1, std::memory_order_relaxed);
}

// Defined in Sn2OverlayUI.cpp. Caller must already be in an active ImGui frame.
void render();

} // namespace sn2_overlay_ui
