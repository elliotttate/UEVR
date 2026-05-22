// Sn2EyeScreenshot.hpp
//
// File-trigger driven per-eye screenshot capture for headless tooling.
//
// USE
// ---
// 1. Set env (game restart):
//      UEVR_SN2_EYE_SCREENSHOT_TRIGGER_FILE=C:\tmp\uevr_shot_req.txt
//      UEVR_SN2_EYE_SCREENSHOT_OUTPUT_DIR=C:\tmp\uevr_screenshots
// 2. While game runs, create the trigger file (any content, even empty).
// 3. On next Present, UEVR:
//      - Captures LEFT-eye scene-color, RIGHT-eye scene-color, and the
//        final swapchain backbuffer.
//      - Writes left.ppm, right.ppm, backbuffer.ppm into OUTPUT_DIR.
//      - Writes done.txt as a "ready" sentinel.
//      - Deletes the trigger file (request is consumed).
// 4. Tooling polls for done.txt, reads PPMs, deletes done.txt to start a new
//    capture cycle.
//
// INTERNAL DESIGN
// ---------------
// Reuses sn2_rt_snapshot's readback infrastructure (READBACK heap +
// CopyTextureRegion + fence). Our hook into draw_indexed_instanced (the
// SAME path that sn2_rt_diff uses) caches the LATEST per-eye scene-color
// resource pointers as draws fly past. On Present, if the trigger file is
// present, we:
//   1. Schedule snapshot copies on a private persistent command list,
//   2. Execute that list on the device's command queue,
//   3. Signal the rt_snapshot fence and let drain_to_disk write the PPMs
//      across the next few frames (READBACK heap requires fence wait),
//   4. Poll for the expected output filenames on disk; once both eye PPMs
//      are present, rename them to the stable output path and write
//      done.txt, then delete the trigger.
//
// The whole thing is one-shot per trigger file. Idempotent: re-creating the
// trigger file after done.txt is read will start a fresh capture cycle.

#pragma once

#include <atomic>
#include <cstdint>

#include <d3d12.h>
#include <dxgi1_4.h>

namespace sn2_eye_screenshot {

// True if the user has configured both required env vars. Cached after first
// call; module is otherwise inert.
bool env_enabled();

// Called from draw_indexed_instanced to record the most recent scene-color
// RT pointer seen for each eye in the current frame. Lightweight, lock-guarded.
void note_scene_color_write(ID3D12Resource* scene_color, int eye_bucket);

// Called from D3D12Hook::present / present1 once per frame BEFORE
// present_internal so the trigger is consumed in the same frame the user
// requested it. Needs the swap chain to grab the backbuffer.
void on_present(IDXGISwapChain3* swap_chain);

}  // namespace sn2_eye_screenshot
