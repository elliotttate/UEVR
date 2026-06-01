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

// Emit an .rdc-keyed manifest next to a completed RenderDoc capture:
// "<rdc_path>.uevr.json". Unlike emit(), this is keyed to the capture file (not
// a seq) and is GATED ONLY on a non-empty rdc_path (NOT on
// UEVR_SN2_CAPTURE_SIDECAR_DIR), so replay python can always find the manifest
// beside the .rdc. The manifest carries three static-classification maps:
//   (a) crc -> {name, role}          (SN2 fog producer/consumer/composite CRCs)
//   (b) eye -> {view_index, expected_froxel_role, froxel_dims}
//   (c) resource_name_prefix -> role (the SN2_* SetName prefixes the naming
//                                      agent emits, for regex classification)
// plus the live UEVR_SN2_* env snapshot and the working hypothesis / success
// criterion. CPU-only; safe to call from the RenderDoc watcher thread.
// Returns the written manifest path, or empty on failure / empty rdc_path.
std::string emit_rdc_manifest(const std::string& rdc_path);

}  // namespace sn2_capture_sidecar
