#pragma once

// C-style FFI surface for UEVR's render diagnostics. Lets external consumers
// (the uevr-mcp plugin, primarily) read the same data that the Render
// Inspector sidebar shows in the overlay — without needing to reach into
// internal C++ classes.
//
// All functions returning const char* return JSON encoded as UTF-8. The
// returned buffer is owned by UEVRBackend and remains valid until the next
// call to the same function on the same thread; callers should copy it
// immediately. NULL is returned on hard errors (e.g. framework not ready).

#include <cstdint>

#if defined(_WIN32)
#define UEVR_RENDER_CAPI __declspec(dllexport)
#else
#define UEVR_RENDER_CAPI
#endif

extern "C" {

// ── Aggregate / category snapshots ───────────────────────────────────

// Big-bundle snapshot covering resources, dx12 diagnostics, shader registry
// (current bound, distinct pairs, pso aggregates, override entries) and a
// small "context" header (renderer, frame, backend, force-sampling state).
UEVR_RENDER_CAPI const char* uevr_render_diag_snapshot_json(
    int max_resources,
    int max_d3d12_events,
    int max_distinct_pairs,
    int max_pso_aggregates);

UEVR_RENDER_CAPI const char* uevr_render_diag_resources_json(int max_resources);
UEVR_RENDER_CAPI const char* uevr_render_diag_d3d12_json(int max_heaps, int max_events);
UEVR_RENDER_CAPI const char* uevr_render_diag_shaders_json(int max_distinct_pairs, int max_pso_aggregates);
UEVR_RENDER_CAPI const char* uevr_render_diag_shader_bytecode_json(
    const char* stage,
    const char* hash,
    int disassemble,
    int max_disassembly_chars);
UEVR_RENDER_CAPI const char* uevr_render_diag_hunter_capture_active_override_stub(int stage);
UEVR_RENDER_CAPI const char* uevr_render_diag_hunter_highlight_hash_json(const char* hash, int enabled);
UEVR_RENDER_CAPI const char* uevr_render_diag_hunter_skip_eye_hash_json(const char* hash, int eye, int enabled);
UEVR_RENDER_CAPI const char* uevr_render_diag_preview_info_json();
UEVR_RENDER_CAPI const char* uevr_render_diag_context_json();

// ── Mutators / actions ───────────────────────────────────────────────

// Sets the FrameResourceInspector's currently-selected resource key. Pass 0
// to clear. Causes the next on_present to refresh the preview SRV.
UEVR_RENDER_CAPI void uevr_render_diag_set_selected_resource(uint64_t key);

// Force-enable sampling regardless of which sidebar tab is open.
UEVR_RENDER_CAPI void uevr_render_diag_set_force_resources_sampling(int enabled);
UEVR_RENDER_CAPI void uevr_render_diag_set_force_shader_tracking(int enabled);
UEVR_RENDER_CAPI void uevr_render_diag_set_force_d3d12_diagnostics(int enabled);

// Re-scan shader override directories on the next on_present.
UEVR_RENDER_CAPI void uevr_render_diag_request_shader_reload();

// Runtime A/B switch for all shader overrides. Keeps manifests loaded but
// resolves original shaders/PSOs while disabled.
UEVR_RENDER_CAPI const char* uevr_render_diag_set_runtime_overrides_enabled(int enabled);

// Arm/disarm the "capture next DX12 pipeline change" trigger.
UEVR_RENDER_CAPI void uevr_render_diag_capture_next_d3d12_change();
UEVR_RENDER_CAPI void uevr_render_diag_clear_captured_d3d12_change();

// Reset the D3D12Diagnostics accumulator (heap/resource maps, recent events).
UEVR_RENDER_CAPI void uevr_render_diag_reset_d3d12();

// ── Disk exports ─────────────────────────────────────────────────────

// Returns JSON {ok, path|error}. as_csv=0 → .json, !=0 → .csv.
UEVR_RENDER_CAPI const char* uevr_render_diag_export_d3d12_pairs(int as_csv);

// Writes the current D3D12 draw/bind snapshot, including symmetry oracle and
// descriptor lineage, under <persistent>/render_inspector/frame_diffs.
UEVR_RENDER_CAPI const char* uevr_render_diag_export_frame_pair_diff_json(int max_events);

// Returns JSON {ok, bundle_dir, files[], error}. profile_name and backend
// may be NULL (the FFI fills sensible defaults from Framework).
UEVR_RENDER_CAPI const char* uevr_render_diag_export_bundle(
    const char* profile_name,
    const char* backend);

// Stereo Forensics status and latest bundle paths. Returns
// {enabled, experiments_enabled, session_dir, manifest, eye_diff, lineage}.
UEVR_RENDER_CAPI const char* uevr_render_diag_stereo_forensics_json();

// ── Stereo / one-eye-bug diagnostics ─────────────────────────────────

// Walks the resource list + PSO aggregates and surfaces per-eye stats with
// asymmetry warnings — written for diagnosing "left eye works / right eye
// black" kinds of bugs. Returns JSON with:
//   { left: { resources: [...], pso_aggregates: [...], totals }, right: {...},
//     asymmetries: [...], current_bind_classification, vr: {...} }
UEVR_RENDER_CAPI const char* uevr_render_diag_stereo_summary_json();

// Select the L (0) or R (1) eye resource for preview. Picks the most-recently
// touched eye-tagged texture matching the side. Returns JSON with the chosen
// resource key + name, or {selected: false, reason}.
UEVR_RENDER_CAPI const char* uevr_render_diag_select_eye(int side);

// ── RenderDoc integration (only active if renderdoc.dll is loaded) ───

// Returns JSON describing the RenderDoc API status:
// { loaded, version, num_captures, is_target_control_connected,
//   is_frame_capturing, capture_path_template, captures: [paths] }
// If RenderDoc isn't loaded, returns { loaded: false }.
UEVR_RENDER_CAPI const char* uevr_render_diag_renderdoc_status_json();

// Queue a RenderDoc trigger-capture for the next `num_frames` frames (1 if
// not specified). Returns JSON {ok, queued_frames, error}.
UEVR_RENDER_CAPI const char* uevr_render_diag_renderdoc_trigger_capture(int num_frames);

// Launch the RenderDoc replay UI on the most recent capture, if any. Returns
// JSON {ok, pid, error}.
UEVR_RENDER_CAPI const char* uevr_render_diag_renderdoc_launch_ui();

// Override the RenderDoc capture file template (.../prefix). Pass NULL/empty
// to leave unchanged. Returns JSON {ok, template, error}.
UEVR_RENDER_CAPI const char* uevr_render_diag_renderdoc_set_capture_template(const char* path_template);

// Internal API — used by Framework startup to proactively load and configure
// renderdoc.dll if present (and optionally load it from disk if not). Mirrors
// the PIX bootstrap pattern: returns whether the DLL was preloaded by the
// launcher (capture safety) or LoadLibrary'd late (degraded mode).
struct UevrRenderDocBootstrapResult {
    void* module;            // HMODULE for renderdoc.dll
    bool  was_preloaded;     // true if renderdoc.dll was in the process before UEVR ran
    bool  api_loaded;        // true if RENDERDOC_GetAPI succeeded
    int   api_version_major;
    int   api_version_minor;
    int   api_version_patch;
};

// Proactively load renderdoc.dll if not already loaded, then initialize the
// in-app API. Skips load if UEVR_DISABLE_RENDERDOC_BOOTSTRAP=1 or if Nsight
// mode is active (Nsight and RenderDoc can both work but PIX is the exclusive
// one). Logs status via spdlog. Safe to call multiple times.
UEVR_RENDER_CAPI UevrRenderDocBootstrapResult uevr_renderdoc_bootstrap();

// Returns true iff the API is currently loaded.
UEVR_RENDER_CAPI bool uevr_renderdoc_is_api_loaded();

// Trigger a wildcard capture (StartFrameCapture+sleep+EndFrameCapture).
// Returns true if EndFrameCapture returned nonzero (capture written).
// No JSON wrapping — for use from Framework / hotkey paths.
UEVR_RENDER_CAPI bool uevr_renderdoc_capture_wildcard();

// Start the sentinel-file watcher for triggering captures from outside the
// process. Writes a thread that polls `%TEMP%/uevr_renderdoc_capture.req`
// every 250ms. File format:
//   line 1: capture file template path (optional — empty = use default)
//   line 2: "frames=N" (optional, default 1)
// On detection: SetCaptureFilePathTemplate(template) then capture N frames.
// Stops automatically on process exit.
UEVR_RENDER_CAPI void uevr_renderdoc_start_capture_watcher();

// ── VR mod state, cvars, frame timing, eye pixel sampling/dumps ──────

// Snapshot of VR-mod state most relevant to render bugs: stereo on/off,
// active runtime (OpenXR/OpenVR/none), AFR / synchronized-AFR / native
// stereo fix flags, depth_enabled, world_to_meters, render resolution,
// HMD active, ShfSceneMode (Stereo3D/Mono2D/Unknown), backbuffer size,
// has_game_and_ui_textures.
UEVR_RENDER_CAPI const char* uevr_render_diag_vr_state_json();

// Dump every cvar the CVarManager is tracking, optionally filtered by
// substring (case-insensitive). Pass NULL/empty for no filter.
UEVR_RENDER_CAPI const char* uevr_render_diag_cvars_json(const char* filter);

// D3D12Component per-render-path timing stats (count, avg ms, max ms) for:
// on_frame, ui_copy, swapchain_copy, openxr_submit, spectator_mirror,
// post_present. Returns {error} if running on D3D11.
UEVR_RENDER_CAPI const char* uevr_render_diag_frame_timing_json();

// CPU readback of an NxN region centered on the eye texture. Returns
// {available, side, width, height, sampled_w, sampled_h, format, channels,
//  rgba_min, rgba_max, rgba_mean, is_black, is_uniform}.
// side: 0=L, 1=R. sample_w/h are clamped to texture dimensions. D3D12 only.
UEVR_RENDER_CAPI const char* uevr_render_diag_eye_pixel_sample_json(
    int side, int sample_w, int sample_h);

// CPU readback of an eye-relative region. sample_x/y are relative to the
// resolved eye region, not the full texture. Values are clamped.
UEVR_RENDER_CAPI const char* uevr_render_diag_eye_region_sample_json(
    int side, int sample_x, int sample_y, int sample_w, int sample_h);

// Save the current-frame eye texture to disk. fmt: 0=PNG, 1=JPG (smaller,
// best for LLM context), 2=BMP. out_path may be NULL to auto-pick under
// <persistent_dir>/render_inspector/eye_dumps/<timestamp>_<side>.<ext>.
// Returns {ok, side, path, width, height, format, error}.
UEVR_RENDER_CAPI const char* uevr_render_diag_eye_dump_json(
    int side, const char* out_path, int fmt);

// ── Per-eye D3D12 trace (viewport / draw / clear classification) ─────

// Enable/disable the low-level D3D12 stereo trace via FFI. The trace is also
// active for any game where the per-game heuristic enables it
// (e.g. Subnautica2). Returns the resulting JSON status.
UEVR_RENDER_CAPI const char* uevr_render_diag_set_stereo_trace_enabled(int enabled);

// Snapshot the stereo-trace counters: per-bucket counts of RSSetViewports,
// DrawInstanced, DrawIndexedInstanced, ClearRenderTargetView (each bucketed
// as Left/Right/Full/Multi/Unknown), OMSetRenderTargets, and ResourceBarrier.
// reset=1 zeroes counters after reading. Returns total + per-bucket counts.
UEVR_RENDER_CAPI const char* uevr_render_diag_stereo_trace_json(int reset);

// Subnautica 2 targeted diagnostics. Returns the active SN2/UEVR render-test
// environment, key hook/map counters, and enough run-state to tell whether a
// test is clean or contaminated by an older WIP mutation.
UEVR_RENDER_CAPI const char* uevr_render_diag_sn2_state_json();

} // extern "C"
