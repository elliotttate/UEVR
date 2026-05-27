#pragma once
// Sn2ViewUbDecode.hpp — StereoScope bindless View-UB decoder (env UEVR_SN2_VIEWUB_DECODE=1).
//
// WHY: the SN2 SLW/fog consumer reads the IntegratedLightScattering froxel volume via the
// bindless View Uniform Buffer (a uint index into ResourceDescriptorHeap), NOT a root SRV
// slot — which is exactly why every SRV/root-slot redirect this project tried was silently
// bypassed (proven: UEVR_SN2_FOG_RIGHT_VOL_REDIRECT fully engaged yet the right eye stayed
// washed). To fix the right eye we must reach the resource the bindless read actually lands
// on.
//
// WHAT: at the fog consumer (SLW basepass 0xDE7C3822 draw; UWEFogReconstruct/Resolve
// dispatches), scan each bound root CBV as an array of uint32. For each value v that is <
// the bound bindless heap's descriptor count, compute the heap CPU handle
// (heap.cpu_base + v*stride) and resolve it via the descriptor registry; if it lands on an
// ILS fog volume (TEXTURE3D, R11G11B10_FLOAT, ~40..192 x 20..128 x 32..80) log
// {eye, cbv_param, byte_offset, bindless_index, resource, dims}.
//
// FOLLOW-UP: if those root CBVs do not map (the View UB is itself bindless), enable
// UEVR_SN2_VIEWUB_CHAIN_SCAN=1. That bounded scanner walks bindless CBV descriptors in
// small chunks, maps each CBV's upload memory, and scans those bytes for ILS-volume
// bindless indices. This locates the one-level-deeper View-UB chain without broad
// million-descriptor scans.
//
// Default OFF; zero cost when the env is unset. The scan functions take the cmdlist
// correlation state, so they are DECLARED+DEFINED inside D3D12Hook.cpp (where that type and
// tls_bindless_heap / sn2_upload_buf_map / sn2_descriptor_registry are visible); this header
// only exposes the env gate + tunables.
#include <cstdint>
#include <cstdlib>

namespace sn2_viewub_decode {

inline bool enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_VIEWUB_DECODE");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

// How many bytes of each root CBV to scan for bindless indices (default 16 KiB; the View UB
// is large and the fog index can sit deep in the fog-uniform sub-block). Override with
// UEVR_SN2_VIEWUB_DECODE_WINDOW.
inline uint32_t scan_window_bytes() {
    static const uint32_t w = []() {
        const char* v = std::getenv("UEVR_SN2_VIEWUB_DECODE_WINDOW");
        if (v == nullptr || v[0] == '\0') return 16384u;
        const long n = std::strtol(v, nullptr, 0);
        return (n >= 256 && n <= (1 << 20)) ? static_cast<uint32_t>(n) : 16384u;
    }();
    return w;
}

}  // namespace sn2_viewub_decode
