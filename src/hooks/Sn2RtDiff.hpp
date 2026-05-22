// Sn2RtDiff.hpp
//
// Captures LEFT-eye and RIGHT-eye scene-color RT contents in the SAME frame
// and emits a per-pixel diff PPM showing exactly where the two eyes differ
// and by how much. Tells us "the right eye fog region differs by intensity X
// in this screen area" — quantifies the remaining gap and shows its location.
//
// Reuses sn2_rt_snapshot's readback infrastructure. Adds:
//   1. LEFT-pass + RIGHT-pass scene-color schedule per frame
//   2. Per-pixel diff computation at drain time
//   3. PPM emission showing diff intensity as heatmap
//
// CONFIG
//   UEVR_SN2_RT_DIFF_DIR=C:\tmp\rt_diff   — output dir, missing = disabled
//   UEVR_SN2_RT_DIFF_EVERY_N_FRAMES=60    — capture frequency (default ~1s)
//
// OUTPUTS
//   rt_diff_<seq>_left.ppm      — LEFT scene color
//   rt_diff_<seq>_right.ppm     — RIGHT scene color
//   rt_diff_<seq>_delta.ppm     — per-pixel |L - R| as red heatmap
//   rt_diff_<seq>_stats.json    — per-region diff statistics
//
// USE
// ---
// 1. Set env, run game
// 2. Inspect delta.ppm files to see WHERE the right eye differs
// 3. Pair with magic-ink to identify the PSO contributing to that region

#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>

#include <d3d12.h>

namespace sn2_rt_diff {

inline bool env_enabled() {
    static const bool e = []() {
        return GetEnvironmentVariableW(L"UEVR_SN2_RT_DIFF_DIR", nullptr, 0) > 0;
    }();
    return e;
}

inline uint32_t every_n_frames() {
    static const uint32_t n = []() -> uint32_t {
        char buf[32]{};
        const auto len = GetEnvironmentVariableA("UEVR_SN2_RT_DIFF_EVERY_N_FRAMES", buf, sizeof(buf));
        if (len == 0 || len >= sizeof(buf)) return 60;
        char* tail = nullptr;
        const auto v = std::strtoul(buf, &tail, 0);
        return v == 0 ? 60 : v;
    }();
    return n;
}

// Hook from draw-completion path. Tag the current scene-color RT with the
// eye-bucket the draw was on. Each frame, the first LEFT + first RIGHT
// observed scene-color RT becomes the diff inputs.
void note_scene_color_write(ID3D12GraphicsCommandList* cl,
                            ID3D12Resource* scene_color,
                            int eye_bucket);

// On Present, finalize: schedule readback for L + R, queue a CPU-side diff.
void on_present();

}  // namespace sn2_rt_diff
