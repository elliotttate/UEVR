// Sn2RdCapture.hpp
//
// In-process RenderDoc capture trigger. Uses UEVR's shared RenderDoc capture
// service to drive RenderDoc's in-app API (RENDERDOC_GetAPI) from inside the
// game process. Triggers full .rdc captures on file-trigger from outside.
//
// SOLVES: RenderDoc's injector can't attach to a UEVR-loaded process. The
// in-app API finds RD's capture machinery in-process and we drive it.
//
// LIMITATIONS
// -----------
// - RD's in-app API initialized AFTER device creation = "reduced fidelity"
//   per RD docs. Late-loading renderdoc.dll is opt-in only; the complete path
//   is still to preload or embedded-initialize RenderDoc before D3D12 creation.
// - Both UEVR and RD hook D3D12 methods. RD's layer initialization may
//   conflict with UEVR's. We probe + report; user adjusts accordingly.
//
// CONFIG
//   UEVR_SN2_RD_CAPTURE=1                                    enable
//   UEVR_SN2_RD_CAPTURE_DLL=<path>                            renderdoc.dll path
//                                                            (also permits
//                                                            degraded late-load)
//   UEVR_SN2_RD_CAPTURE_LOAD_DLL=1                            permit degraded
//                                                            late-load fallback
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
//   -> UEVR detects, calls StartFrameCapture(device, hwnd)
//   -> UEVR ends the capture on the next Present
//   -> .rdc lands under OUT_TEMPLATE
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

// Initialize: find preloaded RenderDoc or opt-in late-load, then fetch API.
// Idempotent. Returns true if successfully initialized.
bool init();

// True if RD's in-app API was successfully loaded.
bool is_loaded();

// Called from Present hook every frame. Checks for trigger file existence.
// If present, calls Start/EndFrameCapture on the supplied device/window pair
// and optionally emits sidecar data.
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
