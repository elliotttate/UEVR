// Sn2RdCapture.hpp
//
// In-process RenderDoc capture trigger. Loads RenderDoc's `renderdoc.dll`
// via the in-app API (RENDERDOC_GetAPI) from inside the game process.
// Triggers full .rdc captures on file-trigger from outside.
//
// SOLVES: RenderDoc's injector can't attach to a UEVR-loaded process. The
// in-app API loads RD's capture machinery INTO our process and we drive it.
//
// LIMITATIONS
// -----------
// - RD's in-app API initialized AFTER device creation = "reduced fidelity"
//   per RD docs — initial contents of resources may not be captured. The
//   frame's D3D12 calls + new resources created during the captured frame
//   are still recorded fully.
// - Both UEVR and RD hook D3D12 methods. RD's layer initialization may
//   conflict with UEVR's. We probe + report; user adjusts accordingly.
//
// CONFIG
//   UEVR_SN2_RD_CAPTURE=1                                    enable
//   UEVR_SN2_RD_CAPTURE_DLL=<path>                            renderdoc.dll path
//                                                            (default: auto-probe)
//   UEVR_SN2_RD_CAPTURE_TRIGGER_FILE=C:\tmp\rd_capture.txt    create this file
//                                                            to trigger one capture
//   UEVR_SN2_RD_CAPTURE_OUT_TEMPLATE=C:\tmp\uevr_captures\sn2 output template
//   UEVR_SN2_RD_CAPTURE_ALSO_EMIT_SIDECAR=1                   on capture, also
//                                                            invoke Sn2CaptureSidecar
//                                                            so UEVR↔RD bridge has
//                                                            both halves automatically
//   UEVR_SN2_RDC_AUTOCAPTURE=N                                automatically trigger a
//                                                            capture on internal frame N
//                                                            (no trigger file needed)
//   UEVR_SN2_RDC_AUTOCAPTURE_EVERY=K                          additionally trigger a
//                                                            capture every K frames
//
// USAGE
//   while game running:
//     touch C:\tmp\rd_capture.txt
//   → UEVR detects, calls api->TriggerCapture()
//   → RD captures the NEXT frame
//   → .rdc lands at OUT_TEMPLATE + "_frameN.rdc"
//   → If ALSO_EMIT_SIDECAR, sidecar emitted alongside
//   → Open .rdc in qrenderdoc

#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace sn2_rd_capture {

bool env_enabled();
const std::string& trigger_file_path();
const std::string& output_template();
bool also_emit_sidecar();

// Autocapture config (frame-N + every-K). Returns 0 when unset/disabled.
uint64_t autocapture_frame();        // UEVR_SN2_RDC_AUTOCAPTURE  (one-shot on frame N)
uint64_t autocapture_every();        // UEVR_SN2_RDC_AUTOCAPTURE_EVERY (every K frames)

// Initialize: probe + load renderdoc.dll, fetch RENDERDOC_API_1_6_0.
// Idempotent. Returns true if successfully initialized.
bool init();

// True if RD's in-app API was successfully loaded.
bool is_loaded();

// Called from Present hook every frame. Checks for trigger file existence.
// If present, calls api->TriggerCapture() and (optionally) emits sidecar.
//
// d3d12_device: the ID3D12Device* used by the present chain. RD's
//   in-app API for D3D12 expects the device as its "device pointer".
// hwnd:        the window the swap chain targets.
// Either may be null — wildcard capture fallback still attempts but is
// less reliable.
void on_present(uint64_t frame_count, void* d3d12_device = nullptr, void* hwnd = nullptr);

// Programmatic trigger (no file). Call when you want to capture from C++.
void request_capture_next_frame();

// Status counters for diagnostics.
uint64_t capture_count();
const std::string& last_capture_file();

}  // namespace sn2_rd_capture
