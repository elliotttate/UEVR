// Sn2FrameCapture.hpp
//
// Self-contained, interference-free frame capture orchestrator. On file
// trigger, atomically captures EVERYTHING UEVR knows about one frame to a
// single output directory:
//
//   <out_dir>/capture_<seq>/
//     manifest.json           pointers to all artifacts + frame state
//     events.json             D3D12 event stream (from Sn2FrameCppExport)
//     left.ppm                LEFT scene-color (from Sn2EyeScreenshot)
//     right.ppm               RIGHT scene-color
//     backbuffer.ppm          swapchain backbuffer
//     sidecar.json            UEVR live state (from Sn2CaptureSidecar)
//     synth_donor_cb.bin      synth donor CB bytes
//     state_*.json            per-PSO state snapshots (from Sn2StateInspector)
//     cb_dumps/G_*.bin/.json  CB byte dumps (from Sn2CbDumper)
//     shaders/pso_*.dxbc      PSO bytecode (from Sn2PsoBytecodeDumper)
//     gpu_counters.json       Per-PSO timing (from Sn2GpuCounters)
//     mesh_dumps/             VB/IB metadata (from Sn2MeshDump)
//
// NO renderdoc.dll loaded — zero interference risk with UEVR's hooks.
//
// Python tools then analyze the capture as a unified bundle.
//
// CONFIG
//   UEVR_SN2_FRAME_CAPTURE_DIR=C:\tmp\uevr_captures   — base output dir
//   UEVR_SN2_FRAME_CAPTURE_TRIGGER_FILE=C:\tmp\uevr_frame_cap.txt
//
// USAGE
//   Set the env vars + ALSO enable the underlying modules you want included.
//   E.g., to capture per-eye PPMs in the bundle, also set
//   UEVR_SN2_EYE_SCREENSHOT_TRIGGER_FILE etc.
//
//   Touch C:\tmp\uevr_frame_cap.txt
//
//   On the next Present, the orchestrator:
//     1. Creates capture dir
//     2. Triggers all enabled sub-captures (eye screenshot, state inspector,
//        sidecar, frame cpp export, etc.) using the same sequence number
//     3. After all sub-captures complete, emits manifest.json

#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace sn2_frame_capture {

bool env_enabled();
const std::string& base_dir();
const std::string& trigger_file_path();

// Per-Present hook. Polls trigger file. On trigger, starts a capture.
void on_present(uint64_t frame_count);

// Programmatic trigger.
void request_capture_next_frame();

// Total captures completed.
uint64_t capture_count();

// Path of the last completed capture's manifest (empty if none).
const std::string& last_manifest_path();

}  // namespace sn2_frame_capture
