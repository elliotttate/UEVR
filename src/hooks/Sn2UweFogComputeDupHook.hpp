// Sn2UweFogComputeDupHook.hpp
//
// Duplicate LEFT-eye UWE Fog compute dispatches (UWEFogReconstruct / Denoise /
// Resolve) for the right eye, so the IntegratedLightScattering volume +
// downstream fog atmosphere is populated for both eyes.
//
// CONTEXT
// -------
//
// Visible bug confirmed via Meta XR Simulator stereo capture: right eye is
// missing the volumetric fog atmosphere (sky/cave look beige/bright instead
// of teal/atmospheric).
//
// Per 2026-05-22 agent investigation the producer chain is:
//   1. Lumen ScreenProbe atlas: CS CRCs 0xE550B4F0, 0x1BE34186, 0xF10723BF
//   2. RenderCameraAerialPerspectiveVolumeCS: 0xBA4C0D07, 0x0119F5DD
//        → 32x32x8 R16G16B16A16F (ResourceId 9989/9991/10015)
//   3. TranslucencyVolumeIntegrateCS: 0x10C83733
//        → ~30x36x28 R11G11B10F (ResourceId 141197/10064/10065)
//
// All write LEFT-only and are read by both eyes via SRV. The fix duplicates
// each writer dispatch with right-eye View CB + UAV redirected to a shadow
// resource (allocated by Sn2UweFogMirrorHook on the same heap+offset bucket).
// Consumer-side SRV redirect at right-eye basepass draws then samples the
// shadow instead of the LEFT-projected original.
//
// APPROACH
// --------
//
// At each compute Dispatch whose CS CRC32 matches a configured UWE fog CS,
// re-issue the Dispatch with the View CB at the configured root param swapped
// to the RIGHT range (byte_delta = +3840 = pool-stride between LEFT and RIGHT
// View CBs, empirically verified 2026-05-22).
//
// View CB lives at compute root 3 in the targeted CSes (verified by binding
// ledger). Configurable via env in case a future engine update moves it.
//
// GATING
// ------
//
//   UEVR_SN2_UWE_FOG_COMPUTE_PROBE=1   — log every compute dispatch's CRC32 +
//                                       root state, to identify the live
//                                       UWE fog CS CRCs.
//   UEVR_SN2_DUPLICATE_UWE_FOG_COMPUTE_RIGHT=1 — actually duplicate matched
//                                                dispatches.
//   UEVR_SN2_UWE_FOG_CS_CRCS=0x...,...  — override CRC list to duplicate.
//                                          Default = built-in Lumen + AerialPersp + TLV set.
//   UEVR_SN2_UWE_FOG_VIEW_DELTA=+3840  — byte delta from LEFT to RIGHT View CB.
//   UEVR_SN2_UWE_FOG_VIEW_CB_ROOTS=3   — compute root params holding View CB.
//   UEVR_SN2_UWE_FOG_LOG_MAX=64        — first N duplications logged.

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace sn2_uwe_fog_compute_dup {

inline constexpr int64_t k_default_view_cb_offset_delta = 3840;
inline constexpr std::array<uint32_t, 1> k_default_view_cb_roots = {3u};

// Built-in CRC list for the 6 producers identified by 2026-05-22 agent task 9.
// Override via UEVR_SN2_UWE_FOG_CS_CRCS=...
inline constexpr std::array<uint32_t, 6> k_default_cs_crcs = {
    0xE550B4F0u,  // Lumen ScreenProbe variant 1
    0x1BE34186u,  // Lumen ScreenProbe variant 2
    0xF10723BFu,  // Lumen ScreenProbe variant 3
    0xBA4C0D07u,  // RenderCameraAerialPerspectiveVolumeCS variant 1
    0x0119F5DDu,  // RenderCameraAerialPerspectiveVolumeCS variant 2
    0x10C83733u,  // TranslucencyVolumeIntegrateCS
};

inline bool env_probe_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_UWE_FOG_COMPUTE_PROBE");
        return v && v[0] && v[0] != '0';
    }();
    return e;
}

inline bool env_dup_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_DUPLICATE_UWE_FOG_COMPUTE_RIGHT");
        return v && v[0] && v[0] != '0';
    }();
    return e;
}

inline int64_t view_cb_offset_delta() {
    static const int64_t d = []() {
        const char* v = std::getenv("UEVR_SN2_UWE_FOG_VIEW_DELTA");
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
        const char* v = std::getenv("UEVR_SN2_UWE_FOG_LOG_MAX");
        if (!v || !*v) return 64;
        char* end = nullptr;
        const auto n2 = std::strtol(v, &end, 0);
        if (end == v) return 64;
        return static_cast<int>(std::clamp<long>(n2, 0, 4096));
    }();
    return n;
}

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

inline const std::vector<uint32_t>& active_cs_crcs() {
    static const std::vector<uint32_t> set = []() {
        const char* env = std::getenv("UEVR_SN2_UWE_FOG_CS_CRCS");
        if (env && *env) {
            auto parsed = parse_csv_u32(env);
            if (!parsed.empty()) return parsed;
        }
        // Fall back to built-in producer list (Lumen + AerialPersp + TLV).
        return std::vector<uint32_t>{k_default_cs_crcs.begin(), k_default_cs_crcs.end()};
    }();
    return set;
}

inline const std::vector<uint32_t>& active_view_cb_roots() {
    static const std::vector<uint32_t> set = []() {
        const char* env = std::getenv("UEVR_SN2_UWE_FOG_VIEW_CB_ROOTS");
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

inline bool is_uwe_fog_cs_crc(uint32_t crc) {
    if (crc == 0) return false;
    for (const uint32_t c : active_cs_crcs()) {
        if (c == crc) return true;
    }
    return false;
}

// Compute the right-eye CBV VA from a left-eye one by adding the offset delta.
// Returns 0 if the result would underflow.
inline uint64_t compute_right_view_va(uint64_t left_va) {
    if (left_va == 0) return 0;
    const int64_t delta = view_cb_offset_delta();
    if (delta < 0 && left_va < static_cast<uint64_t>(-delta)) return 0;
    return static_cast<uint64_t>(static_cast<int64_t>(left_va) + delta);
}

// Re-entry guard so the duplicator's re-issue Dispatch doesn't loop into
// itself.
inline thread_local int g_dup_depth = 0;

}  // namespace sn2_uwe_fog_compute_dup
