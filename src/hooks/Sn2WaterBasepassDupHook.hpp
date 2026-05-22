// Sn2WaterBasepassDupHook.hpp
//
// Duplicate LEFT-eye water-material basepass draws for the right eye, so
// MRT slot 6 (the SingleLayerWater GBuffer auxiliary RT — ResourceId::30085
// in the May 16 RDC, the heap dh_4345_30 slot 6 in the May 21 Cpp2025) gets
// filled for the right eye too.
//
// CONTEXT
// -------
//
// Both eyes' SLW composite reads MRT slot 6 of the 7-RTV basepass setup, but
// in SN2 under -emulatestereo the right-eye basepass mesh-batch list excludes
// water materials. As a result, slot 6 is populated only by left-eye draws,
// and the right-eye composite reads mostly-uninitialised data. See:
//   - moddingkit/render_names/sn2_fviewinfo_offsets.json
//   - UEVRJ/src/hooks/SN2_WATER_BASEPASS_DUP_PLAN.md
//   - colleague's water_replay_params.json (14 left-only water draws)
//   - colleague's right_view_cbuffer.json (per-eye View CB offset delta)
//
// APPROACH
// --------
//
// Pattern mirrors the existing sn2_try_duplicate_fog_voxelize_right in
// D3D12Hook.cpp (GS-voxelize fog dup) — duplicate the LEFT draw with the
// viewport shifted right.
//
// Per-eye remapping (from colleague's right_view_cbuffer.json, both opaque
// and water basepass share this layout):
//
//   PS root[4] (b0 = View CB):  LEFT = base+3166208 / RIGHT = base+3155968
//                               Δ = -10,240 bytes (RIGHT block before LEFT)
//   VS root[6] (b0 = View CB):  LEFT = base+3166208 / RIGHT = base+3155968
//                               Same delta — VS reads View at b0 too.
//
// All other CBVs (Pass/Material/etc.) are shared across eyes — no swap
// needed.
//
// For each LEFT-eye DrawIndexedInstanced whose:
//   1. PSO PS CRC32 ∈ k_water_basepass_ps_crcs
//   2. State has 7-MRT setup (the SLW-extended GBuffer)
//   3. Viewport is LEFT
//
// After the original draw fires:
//   1. Snapshot the LEFT viewport / scissor
//   2. Snapshot the LEFT CBVs at root[4] and root[6]
//   3. Compute right viewport (TopLeftX += Width)
//   4. Compute right CBVs (left_va + view_delta_bytes; default -10240)
//   5. RSSetViewports(right) + SetGraphicsRootConstantBufferView per root
//   6. Re-issue the same draw
//   7. Restore LEFT state
//
// GATING
// ------
//
//   UEVR_SN2_DUPLICATE_SLW_BASEPASS_RIGHT=1 — enable.
//   UEVR_SN2_WATER_BASEPASS_PS_CRCS=...     — override PS CRC list (csv).
//   UEVR_SN2_WATER_BASEPASS_VIEW_DELTA=-10240 — bytes from LEFT to RIGHT view CB.
//   UEVR_SN2_WATER_BASEPASS_VIEW_ROOTS=4,6   — root params to remap (csv).
//   UEVR_SN2_WATER_BASEPASS_LOG_MAX=32       — first N duplicates logged.
//
// CAVEATS
// -------
//
// 1. PS CRC list is binary-specific (May 16 and May 21 captures had different
//    bytecode MD5s for the same materials). Override via env if the defaults
//    don't match the current binary.
// 2. The -10,240 byte delta is from the May 16 capture and was confirmed
//    consistent for both the opaque basepass and the SLW composite there.
//    If a binary update changes UE5's UB allocator layout, the delta may
//    differ. Override via env.
// 3. The View CB roots (4 = PS b0, 6 = VS b0) come from the colleague's
//    right_view_cbuffer.json. Per-shader root sig can differ; the override
//    env covers that case.
// 4. Existing UEVR overrides (sn2_begin_pso3069_fog_scratch_table,
//    sn2_begin_water_chain_scratch_table, etc.) modify CL state for the
//    LEFT draw. The duplicate runs AFTER the LEFT draw inside the same
//    scope so the overrides are still active — duplicate gets the same
//    modified bindings. Intentional: right-eye re-issue should reflect the
//    user's override choices.

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace sn2_water_basepass_dup {

// Default PS CRC32 list — identified in the May 21 Cpp2025 capture as the
// LEFT-only PSOs in the 7-MRT basepass region (CommandList04.cpp lines
// 4148-4626).
inline constexpr std::array<uint32_t, 4> k_default_water_basepass_ps_crcs = {
    0x0182D735u,  // PSO 1100 (RS 218)  PS size 2580
    0x4528BE0Fu,  // PSO 1169 (RS 162)  PS size 10168
    0xDE7C3822u,  // PSO 1435 (RS 245)  PS size 28596 — likely main water material
    0xF1D1132Cu,  // PSO 1514 (RS 252)  PS size 6468
};

// Default per-eye View CB offset delta (RIGHT - LEFT), in bytes. From the
// May 16 capture analysis: RIGHT block sits 10,240 bytes BEFORE LEFT block.
inline constexpr int64_t k_default_view_cb_offset_delta = -10240;

// Default View CB root params to remap. Root 4 = PS b0, Root 6 = VS b0,
// both pointing at the View CB in the opaque/water basepass root sig.
inline constexpr std::array<uint32_t, 2> k_default_view_cb_roots = {4u, 6u};

inline bool env_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_DUPLICATE_SLW_BASEPASS_RIGHT");
        return v && v[0] && v[0] != '0';
    }();
    return e;
}

// Permissive logger: when set, the duplicator entry logs every 7-MRT LEFT-eye
// graphics draw with its PS CRC32 + RTV slots + viewport, WITHOUT enforcing
// the PS CRC filter and WITHOUT performing the duplicate. Used to identify
// the live water-basepass PS CRCs on a binary whose shader compilation
// produces different CRC32 values than the captured-frame analysis.
inline bool log_7mrt_left_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_WATER_BASEPASS_LOG_7MRT_LEFT");
        return v && v[0] && v[0] != '0';
    }();
    return e;
}

inline int64_t view_cb_offset_delta() {
    static const int64_t d = []() {
        const char* v = std::getenv("UEVR_SN2_WATER_BASEPASS_VIEW_DELTA");
        if (!v || !*v) return k_default_view_cb_offset_delta;
        char* end = nullptr;
        const auto n = std::strtoll(v, &end, 0);
        if (end == v) return k_default_view_cb_offset_delta;
        return static_cast<int64_t>(n);
    }();
    return d;
}

inline int log_max_rows() {
    static const int n = []() {
        const char* v = std::getenv("UEVR_SN2_WATER_BASEPASS_LOG_MAX");
        if (!v || !*v) return 32;
        char* end = nullptr;
        const auto n2 = std::strtol(v, &end, 0);
        if (end == v) return 32;
        return static_cast<int>(std::clamp<long>(n2, 0, 4096));
    }();
    return n;
}

// CSV → vector<uint32_t> parser (supports 0xHEX and decimal).
inline std::vector<uint32_t> parse_csv_u32(std::string_view raw) {
    std::vector<uint32_t> out;
    size_t pos = 0;
    while (pos < raw.size()) {
        while (pos < raw.size() && (raw[pos] == ',' || raw[pos] == ' ' || raw[pos] == ';')) ++pos;
        if (pos >= raw.size()) break;
        size_t end = pos;
        while (end < raw.size() && raw[end] != ',' && raw[end] != ' ' && raw[end] != ';') ++end;
        std::string tok{raw.substr(pos, end - pos)};
        if (!tok.empty()) {
            char* tail = nullptr;
            const auto v = std::strtoul(tok.c_str(), &tail, 0);
            if (tail != tok.c_str()) out.push_back(static_cast<uint32_t>(v));
        }
        pos = end;
    }
    return out;
}

inline const std::vector<uint32_t>& active_ps_crcs() {
    static const std::vector<uint32_t> set = []() {
        const char* env = std::getenv("UEVR_SN2_WATER_BASEPASS_PS_CRCS");
        std::vector<uint32_t> out;
        if (env && *env) {
            out = parse_csv_u32(env);
        }
        if (out.empty()) {
            out.assign(k_default_water_basepass_ps_crcs.begin(), k_default_water_basepass_ps_crcs.end());
        }
        return out;
    }();
    return set;
}

// 2026-05-22: "any-MRT" PS CRC list. For these CRCs, the duplicator skips
// the rtv_count==7 check so we can duplicate single-MRT mesh draws (e.g.
// LEFT-only basepass meshes that write to scene color).
// Default empty; populate via UEVR_SN2_DUP_ANY_MRT_PS_CRCS=0x13B00F0C,...
inline const std::vector<uint32_t>& any_mrt_ps_crcs() {
    static const std::vector<uint32_t> set = []() {
        const char* env = std::getenv("UEVR_SN2_DUP_ANY_MRT_PS_CRCS");
        std::vector<uint32_t> out;
        if (env && *env) {
            out = parse_csv_u32(env);
        }
        return out;
    }();
    return set;
}

inline bool is_any_mrt_dup_ps_crc(uint32_t crc) {
    if (crc == 0) return false;
    for (const uint32_t c : any_mrt_ps_crcs()) {
        if (c == crc) return true;
    }
    return false;
}

// 2026-05-22: PSes for which the dup should NOT shift the viewport. Used
// for 3D-texture-writer passes like VoxelizePS where the "viewport" is a
// slice of a 3D fog volume (e.g. 27x30), not a screen-space stereo split.
// Viewport shift on these draws would push the output OFF the destination
// 3D texture. We still want the View CB swap so the geometry shader
// projects froxels from the right-eye camera position.
inline const std::vector<uint32_t>& no_viewport_shift_ps_crcs() {
    static const std::vector<uint32_t> set = []() {
        const char* env = std::getenv("UEVR_SN2_DUP_NO_VIEWPORT_SHIFT_PS_CRCS");
        std::vector<uint32_t> out;
        if (env && *env) {
            out = parse_csv_u32(env);
        }
        return out;
    }();
    return set;
}

inline bool is_no_viewport_shift_ps_crc(uint32_t crc) {
    if (crc == 0) return false;
    for (const uint32_t c : no_viewport_shift_ps_crcs()) {
        if (c == crc) return true;
    }
    return false;
}

inline const std::vector<uint32_t>& active_view_cb_roots() {
    static const std::vector<uint32_t> set = []() {
        const char* env = std::getenv("UEVR_SN2_WATER_BASEPASS_VIEW_ROOTS");
        std::vector<uint32_t> out;
        if (env && *env) {
            out = parse_csv_u32(env);
        }
        if (out.empty()) {
            out.assign(k_default_view_cb_roots.begin(), k_default_view_cb_roots.end());
        }
        return out;
    }();
    return set;
}

inline bool is_water_basepass_ps_crc(uint32_t crc) {
    if (crc == 0) return false;
    for (const uint32_t c : active_ps_crcs()) {
        if (c == crc) return true;
    }
    return false;
}

// Compute the right-eye View CBV VA from a left-eye one by adding the offset
// delta. Returns 0 if the result would underflow.
inline uint64_t compute_right_view_va(uint64_t left_va) {
    if (left_va == 0) return 0;
    const int64_t delta = view_cb_offset_delta();
    if (delta < 0 && left_va < static_cast<uint64_t>(-delta)) return 0;
    return static_cast<uint64_t>(static_cast<int64_t>(left_va) + delta);
}

}  // namespace sn2_water_basepass_dup
