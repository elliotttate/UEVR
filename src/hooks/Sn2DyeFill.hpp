// Sn2DyeFill.hpp
//
// "Dye GPU tap" — a forensic probe that floods a single, caller-named bindless
// 3D fog volume (R11G11B10_FLOAT) with a solid dye color once per frame, so the
// host `stereoscope.dye` tool can SEE where that bindless volume index surfaces
// on screen. This proves which on-screen region a given bindless index feeds —
// without a (crash-prone) million-entry heap scan: we resolve the single target
// index directly through the existing bindless heap + descriptor registry.
//
// MECHANISM (implementation lives in D3D12Hook.cpp where the bindless heap +
// descriptor registry + device are in scope):
//   cpu = tls_bindless_heap.cpu_base + idx*stride
//   res = sn2_descriptor_registry::lookup_resource_by_cpu_ptr_or_hash(cpu)
//   if res is a 3D R11G11B10_FLOAT ILS volume:
//     barrier -> UNORDERED_ACCESS
//     CreateUnorderedAccessView(res, ...) into a persistent shader-visible heap
//     ClearUnorderedAccessViewFloat(gpuHandle, cpuHandle, res, color, 0, null)
//     barrier -> NON_PIXEL_SHADER_RESOURCE
//   (ClearUAVFloat needs BOTH a shader-visible GPU handle and a matching CPU
//   handle for the SAME descriptor — so the descriptor is created in a single
//   shader-visible heap that serves both.)
//
// DEFAULT OFF — zero behavior change unless UEVR_SN2_DYE_FILL is set.
//
// CONFIG
//   UEVR_SN2_DYE_FILL=1            enable
//   UEVR_SN2_DYE_IDX=<n>          target bindless descriptor index
//                                 (decimal or 0x-prefixed hex)
//   UEVR_SN2_DYE_IDX_SEQUENCE=... comma/semicolon-separated indices to cycle
//   UEVR_SN2_DYE_AUTO_SCAN=1     discover current ILS bindless indices live
//   UEVR_SN2_DYE_AUTO_SCAN_CAP   descriptors per chunk (default 16384)
//   UEVR_SN2_DYE_AUTO_SCAN_MAX_CHUNKS chunks to rotate through (default 8)
//   UEVR_SN2_DYE_AUTO_SCAN_MAX_SCANS finite scans before stopping (default: one full sweep)
//                                      set 0 for unlimited active debugging
//   UEVR_SN2_DYE_AUTO_SCAN_MAX_CANDIDATES cap auto candidates (default 1024)
//   UEVR_SN2_DYE_AUTO_SCAN_LOG_MAX max auto-scan log lines (default 16)
//   UEVR_SN2_DYE_PRUNE_AFTER_MISSES remove auto candidates that stop resolving
//                                 after this many misses (default 24; 0 disables)
//   UEVR_SN2_DYE_PREFER_NEWEST   in auto mode, try newest discoveries first
//                                 (default 1; better for recycled UE bindless heaps)
//   UEVR_SN2_DYE_CYCLE_LOG_MAX    max cycle-target log lines (default 16)
//   UEVR_SN2_DYE_CANDIDATE_FILE   optional JSONL file recording candidate and
//                                 cycle/dye_result events for the host-side
//                                 screenshot judge; dye_result means the UAV
//                                 clear was actually recorded
//   UEVR_SN2_DYE_FRAMES_PER_INDEX samples per sequence index (default 120)
//   UEVR_SN2_DYE_MIN_INTERVAL_MS minimum time between dye attempts (default 50)
//   UEVR_SN2_DYE_COLOR=r,g,b      dye color floats (default cyan: 0,1,1)
//   UEVR_SN2_DYE_SHADER_READ_STATE resource state to restore after dye
//                                 (default PIXEL_SHADER_RESOURCE)
//
// WIRING: at the SLW basepass draw (PS crc 0xDE7C3822) in draw_indexed_instanced:
//   if (sn2_dye_fill::enabled()) sn2_dye_fill::fill(command_list);

#pragma once

#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>

#include <d3d12.h>

namespace sn2_dye_fill {

// Enabled iff UEVR_SN2_DYE_FILL is set to a non-empty, non-"0" value.
inline bool enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_DYE_FILL");
        return v && v[0] && v[0] != '0';
    }();
    return e;
}

// Target bindless descriptor index from UEVR_SN2_DYE_IDX (decimal or hex).
// Returns -1 if unset/invalid (fill() then no-ops).
inline int64_t target_index() {
    static const int64_t idx = []() -> int64_t {
        const char* v = std::getenv("UEVR_SN2_DYE_IDX");
        if (v == nullptr || v[0] == '\0') return -1;
        char* end = nullptr;
        // strtoll with base 0 auto-detects 0x hex and decimal.
        const long long n = std::strtoll(v, &end, 0);
        if (end == v || n < 0) return -1;
        return static_cast<int64_t>(n);
    }();
    return idx;
}

// Dye color from UEVR_SN2_DYE_COLOR ("r,g,b" floats). Default cyan (0,1,1).
// Alpha is always 1.0.
inline const std::array<float, 4>& color() {
    static const std::array<float, 4> c = []() -> std::array<float, 4> {
        std::array<float, 4> out{0.0f, 1.0f, 1.0f, 1.0f};
        const char* v = std::getenv("UEVR_SN2_DYE_COLOR");
        if (v == nullptr || v[0] == '\0') return out;
        // Parse up to 3 comma-separated floats.
        const std::string s{v};
        size_t pos = 0;
        for (int i = 0; i < 3; ++i) {
            if (pos >= s.size()) break;
            size_t next = s.find(',', pos);
            const std::string tok = s.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
            try {
                out[i] = std::stof(tok);
            } catch (...) {
                // leave default for this channel
            }
            if (next == std::string::npos) break;
            pos = next + 1;
        }
        return out;
    }();
    return c;
}

// Resolve the target bindless index to its 3D R11G11B10_FLOAT fog volume and
// flood it with the dye color, at most once per frame (gated on the render-
// thread frame counter). No-op if disabled, index unset, the index does not
// resolve to a resource, or the resource is not a matching 3D volume.
//
// Defined in D3D12Hook.cpp where tls_bindless_heap, sn2_descriptor_registry,
// and g_d3d12_hook are in scope.
void fill(ID3D12GraphicsCommandList* cl);

}  // namespace sn2_dye_fill
