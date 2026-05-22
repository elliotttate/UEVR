// Sn2CaptureSidecar.hpp
//
// UEVR ↔ RenderDoc bridge — emits a JSON sidecar describing UEVR's current
// live state at moment of capture. The sidecar can be loaded by Python
// tooling alongside an RD capture, so RD's replay-time mutation APIs can
// be driven to reproduce UEVR's live patches in the replay.
//
// USAGE
//   UEVR_SN2_CAPTURE_SIDECAR_DIR=C:\tmp\sidecars
//   Sn2EyeScreenshot trigger emits a sidecar alongside the PPM dumps.
//
// SIDECAR JSON SHAPE
//   {
//     "schema_version": 1,
//     "emitted_at_iso8601": "2026-05-22T14:33:00Z",
//     "session_id": "...",
//     "frame_counter": 12345,
//     "env_vars": { "UEVR_SN2_DUP_CONFIG_FILE": "...", ... },
//     "dup_cfg_entries": { "0x9d14fcf0": {"mode": "synth_right_cb", ...}, ... },
//     "active_mirrors": [
//       {"game_ptr": "0x...", "mirror_ptr": "0x...", "dim": [54,30,48],
//        "format": 26, "flags": 5}
//     ],
//     "magic_ink_skips": ["0xXX", ...],
//     "synth_donor": {"crc": "0x4d44ce74", "root": 3, "gpu_va": "0x..."}
//   }

#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace sn2_capture_sidecar {

bool env_enabled();
const std::string& output_dir();

// Emit a sidecar JSON to <output_dir>/sidecar_<seq>.json snapshotting current
// UEVR state. Called from Sn2EyeScreenshot when capture completes (or
// standalone via trigger file).
void emit(uint64_t seq);

}  // namespace sn2_capture_sidecar
