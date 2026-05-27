// Sn2PerViewLightInject.hpp
//
// SN2 right-eye volumetric-fog LIGHT INJECTION: per-view light-scatter dispatch
// duplication (the deep root-cause fix attempt).
//
// DIAGNOSIS (from the long-running SN2 RE; see moddingkit MEMORY.md)
// -----------------------------------------------------------------
// The right eye's froxel/IntegratedLightScattering volume EXISTS but is
// UNDER-LIT: the per-view light-injection / light-scatter-grid compute does not
// run (or runs with wrong inputs) for view 1, so the volume the right eye later
// samples is dark while the left eye's is lit teal. A StereoScope heap scan
// confirmed LIT teal volumes exist alongside dark ones -> the lighting CAN be
// produced; view 1 simply isn't getting its own light-scatter dispatch.
//
// ENGINE RE (verified 2026-05-25, image base 0x140000000):
//   ComputeVolumetricFog @ 0x142FBED20 builds the fog. It calls
//   SetupVolumetricFogIntegrationParameters @ 0x142FD6170 (the function prior
//   memory labeled "sub_142FD6170 / per-view dispatcher" — it is actually the
//   per-view PARAMETER SETUP, not the dispatch loop) three times, and the
//   light-injection path SetupInjectShadowedLocalLightCommonParameters
//   @ 0x142FD53D0 also calls it. The actual light-scatter / light-injection
//   work is issued as DIRECT compute Dispatches (LightScatteringCS, light-grid
//   fill, inject-shadowed-local-light) that the engine emits per view; the
//   secondary view's are either skipped or dispatched with X=0 (consistent with
//   prior GPU-readback findings).
//
// APPROACH (b) — light-injection dispatch DUPLICATION
// ---------------------------------------------------
// At the D3D12 compute Dispatch hook, when we record a LEFT-eye dispatch whose
// CS CRC32 matches a configured light-injection / light-scatter CS, re-issue
// that same Dispatch immediately on the same command list with the View CB at
// the configured compute root param(s) swapped from the LEFT range to the RIGHT
// range (left_va + byte_delta), so view 1's froxel volume gets lit. This mirrors
// the proven mechanism in Sn2UweFogComputeDupHook (which targets the fog
// reconstruct/resolve consumers); this module instead targets the LIGHTING
// PRODUCERS named by the RE.
//
// HONEST EXPECTATION
// ------------------
// This is the long-unsolved core. A re-issued dispatch with a right-eye View CB
// swap LIGHTS view 1's volume ONLY IF (a) the right-eye froxel UAV is bound to a
// resource distinct from the left's (else we re-light the left volume), and
// (b) the right View CB really lives at left_va + delta. Both are the same
// assumptions Sn2UweFogComputeDupHook relies on; they hold for the fog
// consumers but are UNVERIFIED for the lighting producers here. So this is a
// best-effort scaffold at the correct hook point with correct logging; it likely
// needs one live iteration (tune CRCs / roots / delta / add a UAV mirror
// redirect) to actually light view 1. It is DEFAULT-OFF and reuses the same root
// state the rest of the dispatch hook already captures, so it cannot destabilise
// the baseline when disabled.
//
// GATING (all default OFF)
// ------------------------
//   UEVR_SN2_FORCE_PERVIEW_LIGHTINJECT=1     master enable (this module)
//   UEVR_SN2_PERVIEW_LIGHTINJECT_PROBE=1     log every compute dispatch's CRC +
//                                            root/eye, to identify the live
//                                            light-scatter CS CRCs in this build
//   UEVR_SN2_PERVIEW_LIGHTINJECT_CRCS=0x..,..  override CRC list (default =
//                                            the current LightScatteringCS plus
//                                            older RE-named light-scatter probes)
//   UEVR_SN2_PERVIEW_LIGHTINJECT_VIEW_DELTA=+3840  LEFT->RIGHT View CB byte delta
//   UEVR_SN2_PERVIEW_LIGHTINJECT_VIEW_CB_ROOTS=3   compute root param(s) w/ View CB
//   UEVR_SN2_PERVIEW_LIGHTINJECT_LOG_MAX=64   first N duplications logged

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace sn2_perview_lightinject {

// LEFT->RIGHT View CB pool stride, same empirically-verified value used by the
// fog-consumer duplicator (Sn2UweFogComputeDupHook k_default_view_cb_offset_delta).
inline constexpr int64_t k_default_view_cb_offset_delta = 3840;

// View CB sits at compute root 3 in the SN2 fog/light CSes (per binding ledger).
inline constexpr std::array<uint32_t, 1> k_default_view_cb_roots = {3u};

// The active transient-off RenderDoc producer trace (2026-05-26) identifies
// FVolumetricFogLightScatteringCS as CRC 0xD1F85C42, dispatch 7x8x12. Keep the
// older RE-named light-scatter/grid probes below for compatibility, but the
// current fog producer of interest is the first entry.
inline constexpr std::array<uint32_t, 8> k_default_cs_crcs = {
    0xD1F85C42u,  // FVolumetricFogLightScatteringCS, current producer trace
    0x1BE34186u,  // light-scatter grid / Lumen ScreenProbe variant
    0xE550B4F0u,  // light-scatter grid / Lumen ScreenProbe variant
    0x3C0BA343u,  // volumetric fog tile fill / light-scatter
    0x571A5618u,  // light-scatter grid
    0x7B2B4D6Bu,  // light-scatter grid
    0xEFB7385Eu,  // light-scatter grid
    0xF10723BFu,  // light-scatter grid / Lumen ScreenProbe variant
};

inline bool env_on(const char* n) {
    const char* v = std::getenv(n);
    return v != nullptr && v[0] != '\0' && v[0] != '0';
}

// Master enable.
inline bool enabled() {
    static const bool e = env_on("UEVR_SN2_FORCE_PERVIEW_LIGHTINJECT");
    return e;
}

inline bool probe_enabled() {
    static const bool e = env_on("UEVR_SN2_PERVIEW_LIGHTINJECT_PROBE");
    return e;
}

inline int64_t view_cb_offset_delta() {
    static const int64_t d = []() {
        const char* v = std::getenv("UEVR_SN2_PERVIEW_LIGHTINJECT_VIEW_DELTA");
        if (v == nullptr || *v == '\0') return k_default_view_cb_offset_delta;
        char* end = nullptr;
        const auto n = std::strtoll(v, &end, 0);
        if (end == v) return k_default_view_cb_offset_delta;
        return static_cast<int64_t>(n);
    }();
    return d;
}

inline int log_max_rows() {
    static const int n = []() {
        const char* v = std::getenv("UEVR_SN2_PERVIEW_LIGHTINJECT_LOG_MAX");
        if (v == nullptr || *v == '\0') return 64;
        char* end = nullptr;
        const auto n2 = std::strtol(v, &end, 0);
        if (end == v) return 64;
        long clamped = n2;
        if (clamped < 0) clamped = 0;
        if (clamped > 4096) clamped = 4096;
        return static_cast<int>(clamped);
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
        const char* env = std::getenv("UEVR_SN2_PERVIEW_LIGHTINJECT_CRCS");
        if (env != nullptr && *env != '\0') {
            auto parsed = parse_csv_u32(env);
            if (!parsed.empty()) return parsed;
        }
        return std::vector<uint32_t>{k_default_cs_crcs.begin(), k_default_cs_crcs.end()};
    }();
    return set;
}

inline const std::vector<uint32_t>& active_view_cb_roots() {
    static const std::vector<uint32_t> set = []() {
        const char* env = std::getenv("UEVR_SN2_PERVIEW_LIGHTINJECT_VIEW_CB_ROOTS");
        std::vector<uint32_t> out;
        if (env != nullptr && *env != '\0') {
            out = parse_csv_u32(env);
        }
        if (out.empty()) {
            out.assign(k_default_view_cb_roots.begin(), k_default_view_cb_roots.end());
        }
        return out;
    }();
    return set;
}

inline bool is_lightinject_cs_crc(uint32_t crc) {
    if (crc == 0) return false;
    for (const uint32_t c : active_cs_crcs()) {
        if (c == crc) return true;
    }
    return false;
}

// Compute the right-eye View CB VA from a left-eye one by adding the offset
// delta. Returns 0 if the result would underflow (so callers skip the swap).
inline uint64_t compute_right_view_va(uint64_t left_va) {
    if (left_va == 0) return 0;
    const int64_t delta = view_cb_offset_delta();
    if (delta < 0 && left_va < static_cast<uint64_t>(-delta)) return 0;
    return static_cast<uint64_t>(static_cast<int64_t>(left_va) + delta);
}

// Re-entry guard so the duplicator's re-issued Dispatch doesn't recurse into
// itself when the Dispatch hook re-enters.
inline thread_local int g_depth = 0;

}  // namespace sn2_perview_lightinject
