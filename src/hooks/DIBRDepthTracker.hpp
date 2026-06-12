#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <d3d12.h>
#include <wrl.h>

// Scene-depth discovery for the DIBR synthetic-stereo pass.
//
// UEVR's render-target-pool hook (the normal SceneDepthZ source) cannot
// install in every title - SN2's UE5 build defeats the FindFreeElement
// signature scan.
//
// Candidates feed from the always-installed device-level
// CreateDepthStencilView hook: every depth target the game creates is
// remembered (with vrmod depth_select-style rejects - MSAA, square
// shadow-atlas aspect, tiny surfaces) holding a reference. LIVENESS feeds
// from always-installed bind hooks (OMSetRenderTargets + the GCL4
// BeginRenderPass path UE5's RHI actually uses): a candidate counts as live
// when its DSV was bound within the last couple of presented frames.
// Selection prefers live candidates first - creation-time ranking alone
// proved non-deterministic (it could latch onto a frozen loading-screen
// depth that matched the swapchain extent) - then exact backbuffer extent,
// area, depth-format tier, and recency.
namespace dibr_depth_tracker {
void record_dsv(ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE descriptor,
    const D3D12_DEPTH_STENCIL_VIEW_DESC* desc = nullptr);

// RTV creation record (descriptor -> resource shape), for the bind census
// below. Shapes are snapshotted at creation so the census never has to call
// GetDesc on a possibly-dead resource.
void record_rtv(ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE descriptor);

// Bind-time liveness signal (OMSetRenderTargets / BeginRenderPass). Cheap:
// one mutex-guarded map probe; unknown descriptors are ignored.
void record_dsv_bind(D3D12_CPU_DESCRIPTOR_HANDLE descriptor);

// Translucency forensics: when UEVR_DIBR_BIND_CENSUS=1, accumulate one
// present-window's ordered (RTV0, DSV) binds every few seconds. The report
// exposes each bind's resource shape/format and the DSV's read-only flags -
// the signature data a pre-translucency scene-color capture keys on.
void record_census_bind(const SIZE_T* rtvs, uint32_t rtv_count, SIZE_T dsv);

// Returns the formatted census once per armed window (empty otherwise) and
// re-arms the interval timer. Call once per presented frame.
std::string take_census_report();

// Translucency probe (UEVR_DIBR_PRETRANS_DUMP=1): at every qualifying bind
// (eye-sized RGBA16F RTV0 + read-only DSV - the SceneColor signature) of one
// armed frame, record a copy of the RTV resource into a numbered slot
// directly inside the game's command list (execution-order exact). Call
// BEFORE forwarding to the original bind function (render passes must not be
// open around the copy).
void record_probe_bind(ID3D12GraphicsCommandList* cmd_list, SIZE_T rtv0, uint32_t rtv_count, SIZE_T dsv);

// Per-present driver for the probe: arms a frame every few seconds, then two
// presents later reads the slots back and writes uevr_dibr_pretrans_<N>.ppm
// to %TEMP% (blocking; forensics only). Call once per presented frame.
// Returns a summary of saved files (empty when nothing was flushed).
std::string probe_flush();

// Best LIVE candidate for the backbuffer: accepts BOTH double-wide
// (full_width x height, two packed views) and single-eye (eye_width x height)
// shaped depth targets - in single-view rendering UE allocates SceneDepthZ at
// the lone view's extent, so an aspect filter against the double-wide target
// alone rejects the real depth. Falls back to stale candidates only when no
// live one qualifies. Returns nullptr when nothing matches (caller should
// skip synthesis for the frame). Also advances the per-present liveness
// window - call once per presented frame.
Microsoft::WRL::ComPtr<ID3D12Resource> select_scene_depth(uint32_t full_width, uint32_t eye_width, uint32_t height);

// Diagnostic snapshot of the current candidates (extent/format/bind stats),
// for periodic logging by the consumer.
std::string describe_candidates();

// === Motion-vector (velocity GBuffer) snapshot ===
// Select/bind/sample wiring for UE's SceneVelocity (PF_A16B16G16R16 on
// Lumen/ray-tracing platforms like SN2; object-motion-only by default, zero
// texel = "not written" sentinel, gamma-encoded xy on SM5+, prev device depth
// packed in zw). The pool hook's signature scan does not fire on SN2, so the
// target is identified at its RTV BIND by format + largest non-square extent.
// The bind itself precedes the frame's velocity draws, so it only REMEMBERS
// the resource; the CopyResource into a private texture is recorded at the
// later post-opaque depth-signature bind (record_afw_depth_bind), capturing
// THIS frame's completed velocity inside the game's command list -
// deterministic in stream order, immune to the next frame's DLSS/TSR pass
// overwriting the pooled texture before present-time use (PureDark's
// MV-backup failure class). The snapshot is left in
// NON_PIXEL_SHADER_RESOURCE for the synthesis to sample directly.
void record_velocity_bind(ID3D12GraphicsCommandList* cmd_list, SIZE_T rtv0, uint32_t rtv_count);
Microsoft::WRL::ComPtr<ID3D12Resource> get_velocity_snapshot();

// The last qualifying SOURCE resource the shape tracker selected (raw pointer
// for the pool-name confirmatory vote / logging only - do NOT dereference;
// pooled targets ping-pong and may be released by the engine).
void* get_velocity_source();

// === AFW per-frame depth snapshots ===
// Under AFW the engine's eye alternates per frame, so a depth that is off by
// ONE frame in either direction belongs to the OTHER eye - the present-time
// pool selection (which can pick a texture the in-flight next frame's
// recording has already bound, still holding stale content at our GPU
// execution point) turns into IPD-scale warp misregistration. PureDark hit
// the same class in his AFW: depth/MV backups taken in one pass were
// overwritten by the next frame before the warp consumed them.
//
// Fix: at the FIRST qualifying read-only depth bind of each recording frame
// (the SceneColor + read-only-DSV signature - opaque depth is complete
// there), record a CopyResource of the depth into a small per-frame ring
// directly inside the game's command list. GPU stream order then guarantees
// the snapshot holds exactly that frame's opaque depth by the time the
// present-time synthesis reads it.
void set_afw_depth_snapshot_enabled(bool enabled);

// The engine frame whose views/passes are about to be recorded (game thread,
// BeginRenderViewFamily - same source as the AFW view-record keying).
void set_recording_frame(uint32_t engine_frame);

// Bind-site hook: call BEFORE forwarding the bind (render passes must not be
// open around the copy). No-op unless enabled and this recording frame has
// not been snapshotted yet.
void record_afw_depth_bind(ID3D12GraphicsCommandList* cmd_list, SIZE_T rtv0, uint32_t rtv_count, SIZE_T dsv);

// The snapshot for an engine frame (nullptr if none). The returned texture is
// in COPY_DEST state and remains valid until its ring slot is reused
// (kAfwDepthSlots frames later).
Microsoft::WRL::ComPtr<ID3D12Resource> get_afw_depth_snapshot(uint32_t engine_frame);

// === AFW sequence-paired depth identity ===
// The depth frame-phase fix that needs NO copies and NO game-thread tag: the
// bind hook observes each recording frame's SceneColor<->depth pairing in
// recording order, and RDG ping-pongs SceneDepthZ between pooled textures
// every frame - so a change of the bound depth RESOURCE between qualifying
// binds delimits a new recording frame. Each delimited frame pushes its depth
// pointer with a monotonically increasing sequence number; present k then
// consumes seq k + offset, a structural pairing that stays exact under camera
// motion (where the present-time pool heuristics measurably go one frame
// stale = the OTHER eye's depth under AFW). The offset is anchored per run by
// the consumer (majority vote against the at-rest pool selection - the regime
// where the pool is measured correct). The game-thread frame tag is captured
// per entry as a tie-breaker only: it races recording by +1, which is exactly
// why it can never be the pairing key (the snapshot lesson).
struct AfwDepthSeqEntry {
    uint64_t seq{};       // monotonic recording-frame index
    uint32_t frame_tag{}; // game-thread frame at push time (races by +1)
    Microsoft::WRL::ComPtr<ID3D12Resource> resource{};
};

// Enable pushing (the synthesis pass sets it while AFW is active). Disabling
// drops the held resource references.
void set_afw_depth_sequence_enabled(bool enabled);

// Copy of the retained window, oldest first; total_pushed reports the
// lifetime push count (== the seq the NEXT push will get).
std::vector<AfwDepthSeqEntry> get_afw_depth_sequence(uint64_t& total_pushed);
} // namespace dibr_depth_tracker
