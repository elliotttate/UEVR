// Sn2EyePairingHook.hpp
//
// LEFT↔RIGHT resource pairing for per-eye SRV table swap on right-eye draw
// duplications (e.g. SLW basepass MainPS 0xDE7C3822).
//
// PROBLEM
// -------
// Native SN2 basepass PSOs (DE7C3822 et al.) only render LEFT eye. UEVR's
// WaterBasepassDup re-issues them on the right viewport with a swapped View
// CB. That fixes the projection matrix, BUT the bound SRV table still holds
// LEFT-eye textures (VBufferA/B, VSM page, sky LUT, fog volume) so the
// right-eye dup samples LEFT-projected lighting and produces a wrong tint.
//
// APPROACH
// --------
// Observe natural draws: every PS draw, walk its bound descriptor tables and
// for each (PSO, root_idx, slot_idx) record the resource pointer per eye
// bucket. After enough observations, (PSO, root, slot) with EXACTLY one
// LEFT resource and EXACTLY one RIGHT resource yields a LEFT→RIGHT pair.
//
// At right-eye duplicate draws of the target PSO, walk slots, look up each
// resource in the pair map, allocate a scratch table copy, overwrite slots
// whose resource has a known right counterpart, then SetGraphicsRoot-
// DescriptorTable to the scratch range.
//
// CONFIG
//   UEVR_SN2_EYE_PAIRING=1                    enable observation + redirect
//   UEVR_SN2_EYE_PAIRING_TARGET_PS_CRCS=...   CSV of PS CRC32s to redirect
//                                              (default 0xDE7C3822)
//   UEVR_SN2_EYE_PAIRING_LOG=1                verbose log of every pair learned
//
// USE
//   Set the env vars, run game normally. Pairing accumulates as natural
//   draws cycle through both eye buckets. The redirect activates as soon
//   as any pair is learned that applies to the target PSO's bound tables.

#pragma once

#include <atomic>
#include <cstdint>

#include <d3d12.h>

namespace sn2_eye_pairing {

// True if UEVR_SN2_EYE_PAIRING=1. Module is otherwise inert.
bool env_enabled();

bool log_verbose_enabled();

// Returns true if the given PS CRC32 is in the target set (default {0xDE7C3822}).
bool is_target_ps_crc(uint32_t crc);

// Observation callback. bucket: 1=LEFT viewport, 2=RIGHT viewport, anything
// else = ignored. PSO = ID3D12PipelineState* cast to uintptr_t. root_idx is
// the root parameter index for the descriptor table. slot_idx is the offset
// within that table.
void record(int bucket, uintptr_t pso, uint32_t root_idx, uint32_t slot_idx,
            ID3D12Resource* res, SIZE_T cpu_handle, ID3D12Device* device);

// Thread-local marker: when true, all observation calls from this thread are
// treated as "synthetic" (i.e., part of UEVR's right-eye duplication) and
// should NOT contribute to the LEFT/RIGHT bucket classification. The natural
// left-eye draw resources would otherwise get observed in both buckets
// because the dup re-issues them on right viewport.
void enter_synthetic_draw();
void exit_synthetic_draw();
bool in_synthetic_draw();

// Look up the right-eye counterpart for `left`. Returns nullptr if not yet
// paired.
ID3D12Resource* right_for(ID3D12Resource* left);

// Pool-aware fallback for UE5 PSO permutations. Looks for the most reliable
// right-eye resource recently observed at the same descriptor fingerprint and
// root/slot, ignoring the PSO pointer.
ID3D12Resource* right_for_desc_slot(ID3D12Resource* left, uint32_t root_idx, uint32_t slot_idx);

// Returns a CPU descriptor handle in the scratch heap to an SRV for the given
// right resource. Lazy-creates the SRV on first call. Returns 0 on failure.
SIZE_T ensure_right_srv_cpu_handle(ID3D12Device* device, ID3D12Resource* right);

// Number of L→R pairs known.
size_t pair_count();

// Make sure the scratch heap is created. Returns false if device is null or
// heap creation fails.
bool ensure_scratch_heap(ID3D12Device* device);

// Shader-visible CBV/SRV/UAV heap backing scratch descriptor tables. Valid
// after ensure_scratch_heap() succeeds.
ID3D12DescriptorHeap* scratch_descriptor_heap();

// Allocate a contiguous range of N CBV/SRV/UAV descriptors in the scratch
// heap. Returns base CPU handle pointer and writes GPU handle to *gpu_out.
// Returns 0 on failure. Bump allocator with ring-buffer wraparound; the
// caller MUST consume these descriptors in the same frame the call was made
// (they may be overwritten ~4096 / N draws later).
SIZE_T alloc_scratch(UINT count, UINT64* gpu_out);

// Reset scratch ring allocator. Optional, but call between frames for
// determinism. Otherwise the ring just wraps.
void reset_scratch_for_frame();

// Twin-detector hook: call from CreateCommittedResource (and similar) right
// after the resource is created. Records the creation in a per-desc ring;
// when a second creation with the SAME desc lands within a small window of
// adjacent creations, the two are registered as a LEFT→RIGHT pair (first =
// LEFT, second = RIGHT).
//
// Rationale: UE5 typically allocates per-view textures back-to-back inside
// FViewInfo::Init or FRDGBuilder::AllocatePooledRenderTargets. Twin pattern
// gives us a deterministic pairing signal without waiting for runtime
// binding observations.
void record_creation(ID3D12Resource* res);

// Placed-resource variant of record_creation. UE5 RDG transient resources can
// alias the same heap offset across barrier-delimited lifetimes; same-desc
// resources at the same (heap, offset) are sequential reuse, not L/R twins.
void record_placed_creation(ID3D12Resource* res, ID3D12Heap* heap, UINT64 heap_offset);

// Runtime aliasing-barrier signal. bucket is 1=LEFT, 2=RIGHT when known.
// pResourceAfter is treated as the newly published logical owner for that eye;
// this helps suppress stale cached pairs after UE5 RDG transient alias reuse.
void record_aliasing_barrier(int bucket, ID3D12Resource* before, ID3D12Resource* after);

// Descriptor-level observation for instanced-stereo/multiview cases where both
// eyes bind the same Texture2DArray resource but different SRV array slices.
void record_srv_descriptor(int bucket, uintptr_t pso, uint32_t root_idx, uint32_t slot_idx,
                           ID3D12Resource* res, SIZE_T cpu_handle,
                           const D3D12_SHADER_RESOURCE_VIEW_DESC& srv_desc,
                           ID3D12Device* device);

// Returns a stable CPU descriptor for the right-eye SRV view corresponding to
// (left_resource, left_srv_desc), or 0 if no descriptor-level pair is known.
SIZE_T ensure_right_srv_for_view(ID3D12Device* device,
                                 ID3D12Resource* left_resource,
                                 const D3D12_SHADER_RESOURCE_VIEW_DESC& left_srv_desc);

// UE-side view anchors. FSceneRenderer::Views[0/1] is the authoritative eye
// ordering; scopes are set by UE render-pass hooks while their per-view work is
// executing so D3D12 observations do not have to rely only on viewport shape.
void enter_ue_view_scope(int bucket, uintptr_t scene_renderer, uintptr_t view_info, const char* pass_tag);
void exit_ue_view_scope();
int current_ue_view_bucket();

// Called at Present time to advance the "creation frame" used by the twin
// detector. Optional but improves the heuristic: creations across a Present
// boundary stop being eligible to pair as twins.
void on_present();

}  // namespace sn2_eye_pairing
