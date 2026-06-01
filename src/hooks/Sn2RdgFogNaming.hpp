#pragma once
// =====================================================================
// Sn2RdgFogNaming — SetName fog RDG textures so RenderDoc captures show
// readable names instead of anonymous resource IDs.
// =====================================================================
//
// Problem: UE5 RDG allocates volumetric-fog textures as PLACED resources
// on aliasing heaps. CreatePlacedResource fires once at pool creation;
// the RDG then re-uses the same ID3D12Resource* across frames and aliases
// heap memory across logical passes.  sn2_rdoc_tags::name_resource_wellknown
// (called at CreateCommittedResource/CreatePlacedResource) names each
// resource exactly once at birth — but the name is attached to the pointer
// (via SetPrivateData / SetName internally), not the memory.  When an
// aliased resource is re-bound with a DIFFERENT RDG identity (e.g. the
// pool entry that was IntegratedLightScattering this frame is FogHistory
// next frame), the stale creation-time name appears in the capture.
//
// This module re-calls SetName on the BOUND fog resources at each fog CS
// dispatch, using the CRC to assign the correct semantic role.  Because
// transient resources are alive and bound at dispatch time, the name we
// stamp here is the one RenderDoc records in the capture.
//
// Design rules (non-negotiables):
//   - Observer-only except SetName. No resource barriers, no copies, no
//     descriptor-heap mutation, no CopyTextureRegion.
//   - Gated by UEVR_SN2_RDOC_TAGS (same master flag as Sn2RenderDocTags.hpp).
//     Zero cost when the flag is off.
//   - Idempotent PER FRAME: track a per-frame generation counter and skip
//     re-naming a pointer that was already named this frame (to avoid one
//     SetName call per dispatch when the same resource is bound repeatedly).
//   - Match existing Sn2*.hpp style: header-only inline, sn2_* namespace,
//     static locals for persistent state, atomic/mutex for thread safety.
//
// Call-site (D3D12Hook.cpp, inside the fog CS dispatch observer, after the
// "sn2_rdoc_fog_region_opened" block around line 28794, before the
// ::sn2_capture_truth::emit_target_dispatch call at line 28795):
//
//   if (sn2_rdoc_tags::name_resources_enabled() && current_cs_crc != 0) {
//       sn2_rdg_fog_naming::on_fog_dispatch(
//           current_cs_crc,
//           dispatch_eye_bucket,
//           dispatch_state.last_compute_root_desc_tables,
//           [](uint64_t gpu_ptr, SIZE_T& cpu_base, UINT& stride) {
//               return bindless_heap_registry().resolve(gpu_ptr, cpu_base, stride);
//           },
//           [](SIZE_T cpu_ptr) {
//               return sn2_descriptor_registry::lookup_resource_by_cpu_ptr_or_hash(cpu_ptr);
//           });
//   }
//
// That one block handles all fog CS CRCs and all roles.  The registry
// objects (bindless_heap_registry, sn2_descriptor_registry) are file-scope
// in D3D12Hook.cpp and cannot be referenced from here; the caller passes
// them via lambdas so this header stays self-contained.
//
// Also add one call to advance_frame() in D3D12Hook::present() (same block as
// the sn2_rdoc_tags::name_swapchain_backbuffers call around line 4749) so the
// per-frame idempotency gate resets each frame:
//
//   if (sn2_rdoc_tags::name_resources_enabled())
//       sn2_rdg_fog_naming::advance_frame();   // once per Present
//
// Fog textures named (when bound as UAV at the producing dispatch):
//   SN2_IntegratedLightScattering — UAV output of LightScatteringCS (0xd1f85c42)
//                                   and FinalIntegrationCS (0x3402487c).
//   SN2_ResolvedFog               — UAV output of UWEFogResolveCS (0x0930dd4e).
//   SN2_UWEFogScattering          — UAV output of UWEFogReconstructCS (0xf996b96b).
//   SN2_FogProducerUAV_<crc8>     — fallback for any other known fog CS writing
//                                   a Texture3D UAV we cannot name more precisely.
//
// Fog textures that cannot be named at dispatch time (and why):
//   FogHistory (temporal reprojection read-back):
//     Bound as SRV (not UAV) at the LightScatteringCS dispatch. The SRV
//     slots live in a different root table and the descriptor-registry may
//     not have an entry for every transient SRV (it's filtered to reduce
//     overhead). Named at CreatePlacedResource time by name_resource_wellknown
//     using the "no-UAV flag" heuristic (SN2_FogHistory); that static name
//     is the best available for history inputs.
//   MaterialSetupCS (0xd1d94ed1) UAV output:
//     Writes an intermediate material-param volume whose role is unclear
//     from CRC alone. Named conservatively as SN2_FogMaterialSetupUAV.
//   FogCS_*/MainCS variants (0x5af52812, 0x06c053c9, etc.):
//     These UWE-specific compute shaders write intermediate volumes whose
//     exact RDG names are not yet RE-confirmed. Named as
//     SN2_FogCS_UAV_<crc8hex> so they appear in the capture with a unique,
//     traceable tag rather than an anonymous ID.
// =====================================================================

#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include <windows.h>
#include <d3d12.h>

#include "Sn2RenderDocTags.hpp"  // enabled(), name_resources_enabled(), classify_resource_wellknown()

namespace sn2_rdg_fog_naming {

// ---------------------------------------------------------------------------
// Frame generation — bumped once per Present (or per logical frame start).
// Resources already named in the current frame generation are skipped so we
// pay at most one SetName per resource per frame. Callers outside this header
// (e.g. D3D12Hook::Present path) call advance_frame() once per frame.
// ---------------------------------------------------------------------------
namespace detail {
    inline std::atomic<uint64_t>& frame_gen() {
        static std::atomic<uint64_t> g{0};
        return g;
    }
    inline std::mutex& named_mu() {
        static std::mutex m;
        return m;
    }
    // Map: resource ptr -> last frame generation at which SetName was called.
    inline std::unordered_map<ID3D12Resource*, uint64_t>& named_set() {
        static std::unordered_map<ID3D12Resource*, uint64_t> s;
        return s;
    }

    // Try to SetName res with the given narrow string.
    // Returns true if SetName was attempted (res non-null, name non-empty).
    inline bool do_set_name(ID3D12Resource* res, const char* narrow_name) {
        if (res == nullptr || narrow_name == nullptr || narrow_name[0] == '\0') return false;
        wchar_t wide[128];
        const int n = MultiByteToWideChar(CP_UTF8, 0, narrow_name, -1, wide, 128);
        if (n > 0) {
            res->SetName(wide);
            return true;
        }
        return false;
    }

    // Name res if it hasn't been named this frame yet. Returns true if named.
    inline bool name_if_new(ID3D12Resource* res, const char* narrow_name) {
        if (res == nullptr || narrow_name == nullptr) return false;
        const uint64_t gen = frame_gen().load(std::memory_order_acquire);
        {
            std::scoped_lock _{named_mu()};
            auto& last = named_set()[res];
            if (last == gen) return false;  // already named this frame
            last = gen;
        }
        return do_set_name(res, narrow_name);
    }
} // namespace detail

// Advance the frame counter. Call once per frame (e.g. at Present).
// After the advance, all resources are eligible for re-naming so the name
// always reflects the RDG role assigned in the current frame.
inline void advance_frame() {
    if (!sn2_rdoc_tags::name_resources_enabled()) return;
    detail::frame_gen().fetch_add(1, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Role name for a fog CS CRC (producer-side).
// Returns a stable string literal; nullptr for unknown CRCs.
// This is the authoritative mapping for naming purposes.
// ---------------------------------------------------------------------------
inline const char* producer_role_name(uint32_t cs_crc) {
    switch (cs_crc) {
        case 0xd1d94ed1u: return "SN2_FogMaterialSetupUAV";
        // LightScatteringCS fills the IntegratedLightScattering (ILS) froxel.
        case 0xd1f85c42u: return "SN2_IntegratedLightScattering";
        // FinalIntegrationCS integrates the scattered-light volume (same ILS target).
        case 0x3402487cu: return "SN2_IntegratedLightScattering";
        // UWEFogReconstructCS reconstructs scattered fog into an intermediate volume.
        case 0xf996b96bu: return "SN2_UWEFogScattering";
        // UWEFogResolveCS writes the 2D resolved-fog output (the store-coord bug target).
        case 0x0930dd4eu: return "SN2_ResolvedFog";
        // UWE MainCS — writes an intermediate fog volume.
        case 0x5af52812u: return "SN2_FogMainCS_UAV";
        // FogCS_* variants — intermediate volumes, role not RE-confirmed.
        case 0xe43fdb2fu: return "SN2_FogCS_25x28_UAV";
        case 0x7e37e4f7u: return "SN2_FogCS_80x90A_UAV";
        case 0x74d7969fu: return "SN2_FogCS_5x6_UAV";
        case 0xe19ff864u: return "SN2_FogCS_3x3_UAV";
        case 0x4a7c0537u: return "SN2_FogCS_80x90B_UAV";
        case 0xe488e8fbu: return "SN2_FogCS_80x90C_UAV";
        case 0x06c053c9u: return "SN2_FogCS_64x64_UAV";
        case 0x571a5618u: return "SN2_FogCS_8x32_UAV";
        default:          return nullptr;
    }
}

// ---------------------------------------------------------------------------
// on_fog_dispatch — the main entry point.
//
// Called once per fog CS Dispatch, BEFORE the original Dispatch fires.
// Gate: name_resources_enabled() && cs_crc is a known fog producer.
//
// Arguments:
//   cs_crc          : CRC32 of the currently-bound compute shader.
//   eye_bucket      : 1=left, 2=right, 0=unknown (from cmdlist_eye_bucket).
//   root_desc_tables: CommandListCorrelationState::RootSlotArray — GPU handles
//                     of compute root descriptor tables (index 0..N-1).
//   resolve_gpu_va  : callable matching BindlessHeapRegistry::resolve() —
//                       bool(uint64_t gpu_ptr, SIZE_T& cpu_base, UINT& stride)
//                     Provided by the caller so this header doesn't reach into
//                     the file-scope bindless_heap_registry() singleton.
//   lookup_resource : callable matching
//                       sn2_descriptor_registry::lookup_resource_by_cpu_ptr_or_hash —
//                       ID3D12Resource*(SIZE_T cpu_ptr)
//                     Resolves a CPU descriptor address to the resource it
//                     describes. Provided by the caller for the same reason.
//
// Strategy:
//   Walk root descriptor tables. For each table:
//     resolve GPU→CPU via resolve_gpu_va.
//     For each slot (up to kMaxSlotsPerTable):
//       lookup_resource → ID3D12Resource*.
//       GetDesc() → if Texture3D + UAV flag → call name_if_new with role_name.
//   Also name any Texture2D UAV found at UWEFogResolveCS (SN2_ResolvedFog).
//   All other resource types/dimensions are skipped (no-op / observer-only).
// ---------------------------------------------------------------------------
template <typename RootSlotArray, typename ResolveGpuVa, typename LookupResource>
inline void on_fog_dispatch(
    uint32_t          cs_crc,
    int               eye_bucket,
    const RootSlotArray& root_desc_tables,
    ResolveGpuVa&&    resolve_gpu_va,
    LookupResource&&  lookup_resource)
{
    if (!sn2_rdoc_tags::name_resources_enabled()) return;

    const char* role = producer_role_name(cs_crc);
    if (role == nullptr) return;  // not a known fog producer — skip

    // Max descriptor slots per root table to walk. Keep it cheap.
    static constexpr UINT kMaxSlotsPerTable  = 32u;
    static constexpr UINT kMaxRootTables     = 16u;

    // UWEFogResolveCS writes a Texture2D (not Texture3D). Both 2D and 3D UAVs
    // are accepted to cover it. All others are filtered to 3D only.
    const bool accept_tex2d_uav = (cs_crc == 0x0930dd4eu);

    const UINT num_tables = static_cast<UINT>(
        std::min<size_t>(root_desc_tables.size(), kMaxRootTables));

    for (UINT ti = 0; ti < num_tables; ++ti) {
        const uint64_t table_gpu = root_desc_tables[ti];
        if (table_gpu == 0) continue;

        SIZE_T cpu_base = 0;
        UINT   stride   = 0;
        if (!resolve_gpu_va(table_gpu, cpu_base, stride) || stride == 0) continue;

        for (UINT slot = 0; slot < kMaxSlotsPerTable; ++slot) {
            const SIZE_T slot_cpu = cpu_base + static_cast<SIZE_T>(slot) * stride;
            ID3D12Resource* res = lookup_resource(slot_cpu);
            if (res == nullptr) continue;

            // Read the resource description (read-only, never mutates state).
            const D3D12_RESOURCE_DESC rdesc = res->GetDesc();

            bool is_fog_uav = false;
            if (rdesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D &&
                (rdesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) {
                is_fog_uav = true;
            } else if (accept_tex2d_uav &&
                       rdesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
                       (rdesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) {
                is_fog_uav = true;
            }
            if (!is_fog_uav) continue;

            // Build name: role + eye suffix so L/R are distinguishable.
            char name[128];
            const char* eye_suffix = (eye_bucket == 1) ? "_eyeL"
                                   : (eye_bucket == 2) ? "_eyeR"
                                   :                     "_eyeX";
            std::snprintf(name, sizeof(name), "%s%s", role, eye_suffix);

            detail::name_if_new(res, name);
        }
    }
}

// ---------------------------------------------------------------------------
// on_fog_dispatch_simple — convenience overload for callers that have already
// resolved a set of candidate ID3D12Resource* pointers from the descriptor
// tables (e.g. from an existing walk that also serves another purpose, such
// as the sn2_dispatch_inspector block in D3D12Hook.cpp).
//
// Accepts a lightweight span-like pair (pointer + count). The caller fills
// resources[] with up to N Texture3D/Texture2D UAV resource pointers already
// resolved from the root descriptor tables.
// ---------------------------------------------------------------------------
inline void on_fog_dispatch_simple(
    uint32_t         cs_crc,
    int              eye_bucket,
    ID3D12Resource** resources,
    size_t           resource_count)
{
    if (!sn2_rdoc_tags::name_resources_enabled()) return;
    if (resources == nullptr || resource_count == 0) return;

    const char* role = producer_role_name(cs_crc);
    if (role == nullptr) return;

    const char* eye_suffix = (eye_bucket == 1) ? "_eyeL"
                           : (eye_bucket == 2) ? "_eyeR"
                           :                     "_eyeX";
    char name[128];
    std::snprintf(name, sizeof(name), "%s%s", role, eye_suffix);

    for (size_t i = 0; i < resource_count; ++i) {
        if (resources[i] != nullptr) {
            detail::name_if_new(resources[i], name);
        }
    }
}

} // namespace sn2_rdg_fog_naming
