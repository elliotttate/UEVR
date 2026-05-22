// Sn2RightCbSynth.hpp
//
// Right-eye View CB synthesizer. Solves the "LEFT-only PSO can't be dup'd"
// problem (no engine-bound right CBV to use as ref delta) by SNAPSHOTTING the
// right-eye View CB bytes from a "donor" both-eye PSO and reusing them for
// every LEFT-only PSO's dup.
//
// FLOW
// ----
//
// 1. Game runs. A few PSOs (like 0x4d44ce74) fire on BOTH eyes — these are
//    "donor" PSOs.
// 2. When a donor PSO fires its RIGHT-eye View CB binding, we copy the 4096
//    bytes at that GPU_VA into a UEVR-owned upload buffer (persistent CPU map).
// 3. For LEFT-only PSO dups, we bind the donor's GPU_VA from step 2 instead
//    of trying to compute a pool-relative delta (which crashes).
//
// CONFIG
//   UEVR_SN2_RIGHT_CB_SYNTH=1               — enable
//   UEVR_SN2_RIGHT_CB_SYNTH_DONOR=0x4d44ce74 — PS CRC of donor PSO (default)
//   UEVR_SN2_RIGHT_CB_SYNTH_ROOT=3          — root index where donor binds View CB
//   UEVR_SN2_RIGHT_CB_SYNTH_SIZE=4096       — bytes to snapshot
//
// USE
// ---
// In dup_cfg, set:
//   "mode": "synthesize_right_cb"
//   "view_cb_roots": [3]
//
// The dup hook will, on right-eye re-issue, bind the donor's snapshotted
// GPU_VA instead of LEFT+delta. SAFE — the synthesized CB is the actual
// right-eye CB content the engine itself uses for a both-eye PSO.

#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <mutex>

#include <d3d12.h>

namespace sn2_right_cb_synth {

inline bool env_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_RIGHT_CB_SYNTH");
        return v && v[0] && v[0] != '0';
    }();
    return e;
}

inline uint32_t donor_crc() {
    static const uint32_t v = []() -> uint32_t {
        char buf[32]{};
        const auto len = GetEnvironmentVariableA("UEVR_SN2_RIGHT_CB_SYNTH_DONOR", buf, sizeof(buf));
        if (len == 0 || len >= sizeof(buf)) return 0x4d44ce74u;
        char* tail = nullptr;
        const auto x = std::strtoul(buf, &tail, 0);
        return x == 0 ? 0x4d44ce74u : static_cast<uint32_t>(x);
    }();
    return v;
}

inline uint32_t donor_root() {
    static const uint32_t v = []() -> uint32_t {
        char buf[32]{};
        const auto len = GetEnvironmentVariableA("UEVR_SN2_RIGHT_CB_SYNTH_ROOT", buf, sizeof(buf));
        if (len == 0 || len >= sizeof(buf)) return 3;
        char* tail = nullptr;
        const auto x = std::strtoul(buf, &tail, 0);
        return static_cast<uint32_t>(x);
    }();
    return v;
}

inline uint32_t snapshot_size() {
    static const uint32_t v = []() -> uint32_t {
        char buf[32]{};
        const auto len = GetEnvironmentVariableA("UEVR_SN2_RIGHT_CB_SYNTH_SIZE", buf, sizeof(buf));
        if (len == 0 || len >= sizeof(buf)) return 4096;
        char* tail = nullptr;
        const auto x = std::strtoul(buf, &tail, 0);
        return x == 0 ? 4096u : static_cast<uint32_t>(x);
    }();
    return v;
}

// One-time initialization. Creates the upload buffer + persistent map.
bool init(ID3D12Device* device);
bool initialized();

// Called on the donor PSO's RIGHT-eye View CB binding. Snapshots bytes if
// we haven't yet, or refreshes the snapshot periodically.
void update_donor(uint32_t crc, int eye, uint32_t root, D3D12_GPU_VIRTUAL_ADDRESS gpu_va);

// Returns the synthesized right-eye GPU_VA, or 0 if no donor snapshot yet.
D3D12_GPU_VIRTUAL_ADDRESS get_right_va();

// Phase AA: bridge accessors. Copy out the current snapshot bytes for sidecar
// emission. Returns number of bytes copied (0 if no snapshot yet).
size_t copy_snapshot_bytes(void* out_buf, size_t buf_size);

// Snapshot count + last-update tick (for diagnostics)
uint64_t snapshot_count();

}  // namespace sn2_right_cb_synth
