// Sn2UweFogMirrorHook.hpp
//
// Parallel per-eye UAV infrastructure for the UWE fog reconstruct/denoise/
// resolve compute chain. Solves the "shared single-writer UAV" problem that
// made the prior duplicator (Sn2UweFogComputeDupHook.hpp) corrupt LEFT eye
// when re-issuing the dispatch with right-eye View CB.
//
// FLOW
// ----
//
// P1 — CreateCommittedResource hook: detect every texture that matches the
//      UWE fog UAV signature (UAV-capable, fog-typical format + dim).
//
// P2 — At detection time, allocate a parallel ID3D12Resource with identical
//      D3D12_RESOURCE_DESC, plus UAV and SRV descriptors in a UEVR-owned
//      GPU-visible heap.
//
// P3 — On LEFT-eye UWE fog compute dispatch:
//      - Original dispatch runs untouched (writes to game's UAV)
//      - Build a per-frame scratch descriptor table = copy of game's table
//        with the UAV slot replaced by mirror_uav
//      - Bind scratch + right-eye View CB
//      - Re-issue dispatch (writes to mirror UAV)
//      - Restore
//
// P4 — On RIGHT-eye basepass draw whose SRV table contains the tracked SRV:
//      - Build scratch table = copy of game's with SRV slot replaced by
//        mirror_srv
//      - Bind, draw, restore
//
// Memory cost: ~6 MB per mirror at 588x616 R16G16B16A16F. ~5 candidates
// max = ~30 MB.
//
// GATING
// ------
//
//   UEVR_SN2_UWE_FOG_MIRROR=1    — enable mirror allocation at detection
//   UEVR_SN2_UWE_FOG_MIRROR_LOG=1 — verbose logging of every match/alloc
//
// (P3/P4 reuse Sn2UweFogComputeDupHook's UEVR_SN2_DUPLICATE_UWE_FOG_COMPUTE_RIGHT
//  but with the mirror-UAV-redirect path.)

#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <unordered_map>

#include <d3d12.h>
#include <wrl/client.h>

namespace sn2_uwe_fog_mirror {

inline bool env_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_UWE_FOG_MIRROR");
        return v && v[0] && v[0] != '0';
    }();
    return e;
}

inline bool env_log_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_UWE_FOG_MIRROR_LOG");
        return v && v[0] && v[0] != '0';
    }();
    return e;
}

// Aliasing-SRV-redirect (Approach C from sn2_aliasing_CONFIRMED_definitive_2026_05_21.md).
// When right-eye UWE Fog consumer dispatch reads an SRV pointing at an aliased
// fog volume, replace with a shadow SRV that holds per-eye-correct data.
inline bool env_alias_redirect_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_ALIAS_SRV_REDIRECT");
        return v && v[0] && v[0] != '0';
    }();
    return e;
}

inline bool env_alias_redirect_log_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_ALIAS_SRV_REDIRECT_LOG");
        return v && v[0] && v[0] != '0';
    }();
    return e;
}

// Thread-local re-entry guard. The mirror allocation calls
// device->CreateCommittedResource(mirror), which routes back through our
// hook — without this guard the hook would detect the mirror as a candidate
// and recurse infinitely (stack overflow).
inline thread_local int g_create_mirror_depth = 0;

struct CreateMirrorScope {
    CreateMirrorScope() { ++g_create_mirror_depth; }
    ~CreateMirrorScope() { --g_create_mirror_depth; }
};

inline bool in_mirror_create() { return g_create_mirror_depth > 0; }

// Targeted UWE fog UAV signature.
//
// Iteration history:
//   v1 — broad (any UAV-capable R11G11B10F/R16G16B16A16F texture 32..4096):
//        matched 8000+ scratch 512×512 textures per session → recursion + OOM.
//   v2 — TEXTURE3D only: rejected all real fog UAVs (which are 2D outputs).
//   v3 — narrow 3D-only with 3 specific shape buckets.
//   v4 (2026-05-22) — adds Lumen ScreenProbe atlas 2D shape per agent task 9.
inline bool matches_uwe_fog_signature(const D3D12_RESOURCE_DESC& desc) {
    // Exclude depth — mirror create doesn't provide a clear value.
    // RT flag is OK now — VoxelizePS targets have RT+UAV flags (0x5).
    if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) {
        return false;
    }
    // Must be UAV-writable for any of our target producer shaders to bind.
    if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0) {
        return false;
    }

    const auto w = desc.Width;
    const auto h = desc.Height;
    const auto d = desc.DepthOrArraySize;

    if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
        // 1) IntegratedLightScattering / TLV filter: 30×36×28 R11G11B10F
        //    (TranslucencyVolumeSpatialSeparableFilterCS, ResourceId 141197)
        if (desc.Format == DXGI_FORMAT_R11G11B10_FLOAT &&
            w == 30 && h == 36 && d == 28) {
            return true;
        }
        // 2) Aerial perspective: 32×32×8 R16G16B16A16F
        //    (RenderCameraAerialPerspectiveVolumeCS, ResourceId 9991)
        if (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT &&
            w == 32 && h == 32 && d == 8) {
            return true;
        }
        // 3) VoxelizePS-target IntegratedLightScattering 3D 54×30×48 R11G11B10F.
        //    CONFIRMED via live RTV dims diagnostic (2026-05-22 phase V3b).
        //    Has flags 0x5 (RT|UAV).
        if (desc.Format == DXGI_FORMAT_R11G11B10_FLOAT &&
            w == 54 && h == 30 && d == 48) {
            return true;
        }
        // 4) Distant sky light LUT: small R16G16B16A16F cubemap-ish 3D.
        if (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT &&
            w >= 4 && w <= 64 && h >= 4 && h <= 64 && d >= 4 && d <= 64) {
            return true;
        }
        return false;
    }

    if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        // Per 2026-05-22 agent guidance (round 5): DROP absolute pixel dims.
        // Match by (format ∈ Lumen-producer set) + UAV-capable + non-square.
        // Lumen producer formats: R11G11B10F, R8_UNORM, R8_UINT, R16G16B16A16F.
        // Sizes vary per-resolution and render-scale — agent-captured 1264×712
        // is only correct for one specific capture config.
        const bool fmt_ok =
            desc.Format == DXGI_FORMAT_R11G11B10_FLOAT ||
            desc.Format == DXGI_FORMAT_R8_UNORM ||
            desc.Format == DXGI_FORMAT_R8_UINT ||
            desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
        if (!fmt_ok) return false;

        // Lumen atlas is always non-square (octahedral tile packing) — reject
        // square scratch textures.
        if (w == h) return false;

        // Reject sub-256 in either dim (excludes small lookup tables) and
        // > 8192 (excludes oversized scratch).
        if (w < 256 || w > 8192 || h < 64 || h > 8192) return false;

        return true;
    }

    return false;
}

// Per-mirror state.
struct Mirror {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    D3D12_RESOURCE_DESC desc{};
    D3D12_CPU_DESCRIPTOR_HANDLE uav_cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE uav_gpu{};
    D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu{};
    // 2026-05-22 Stage V4: RTV descriptor for RT-flagged mirrors.
    // VoxelizePS writes to a 3D texture via RTV slots (rasterizer to slice
    // via GS expansion). For graphics redirect we bind this RTV instead of
    // the original game resource's RTV.
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu{};
    bool has_rtv{false};
    D3D12_RESOURCE_STATES current_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    uint64_t seq = 0;   // sequence number for logging / debugging
};

// Game resource → mirror mapping. Lookup is hot-path in P3/P4.
struct MirrorRegistry {
    std::mutex mu;
    std::unordered_map<ID3D12Resource*, Mirror> map;

    bool has(ID3D12Resource* key) {
        std::scoped_lock _{mu};
        return map.find(key) != map.end();
    }

    const Mirror* find(ID3D12Resource* key) {
        std::scoped_lock _{mu};
        const auto it = map.find(key);
        return it != map.end() ? &it->second : nullptr;
    }
};

inline MirrorRegistry& registry() {
    static MirrorRegistry r;
    return r;
}

// Reverse lookup: CPU descriptor handle (.ptr) → ID3D12Resource* that was
// bound there by CreateUnorderedAccessView. Populated by our UAV-create
// hook (separate from the mirror's UAVs which live in OUR heap).
//
// At compute dispatch time, given a GPU descriptor handle = (heap.gpu_base
// + offset), we recover the CPU handle = (heap.cpu_base + offset) and look
// up which game resource it points at. If that resource is in MirrorRegistry,
// we know to redirect this dispatch's writes to the mirror.
struct UavBindingMap {
    std::mutex mu;
    std::unordered_map<SIZE_T /*cpu_handle.ptr*/, ID3D12Resource*> map;

    void set(SIZE_T cpu_ptr, ID3D12Resource* res) {
        if (cpu_ptr == 0) return;
        std::scoped_lock _{mu};
        if (res != nullptr) {
            map[cpu_ptr] = res;
        } else {
            map.erase(cpu_ptr);
        }
    }

    ID3D12Resource* find(SIZE_T cpu_ptr) {
        if (cpu_ptr == 0) return nullptr;
        std::scoped_lock _{mu};
        const auto it = map.find(cpu_ptr);
        return it != map.end() ? it->second : nullptr;
    }
};

inline UavBindingMap& uav_bindings() {
    static UavBindingMap m;
    return m;
}

// === Aliasing-bucket tracker ===========================================
//
// UE5 places transient render-graph textures into a shared heap and aliases
// them via D3D12 placed-resource aliasing barriers. From capture analysis
// (heap 26937 at offsets +8978432 / +9502720 / +10027008 / etc) the same
// (heap, offset) slot is reused by multiple ID3D12Resource* — and the fog
// 3D volumes (54×31×64 R11G11B10F) are exactly such resources.
//
// We bucket resources by (heap, offset). When a bucket gets 2+ members and
// the shape matches a fog volume, we allocate a single shadow resource per
// bucket and create SRV descriptors of it. At right-eye UWE Fog consumer
// dispatches we redirect SRV table entries pointing at bucket members to
// the shadow SRV.

struct AliasBucketKey {
    ID3D12Heap* heap{};
    UINT64 offset{};
    bool operator==(const AliasBucketKey& o) const noexcept { return heap == o.heap && offset == o.offset; }
};

struct AliasBucketKeyHash {
    std::size_t operator()(const AliasBucketKey& k) const noexcept {
        return std::hash<void*>{}(reinterpret_cast<void*>(k.heap)) ^ (std::hash<UINT64>{}(k.offset) << 1);
    }
};

struct AliasBucket {
    std::vector<ID3D12Resource*> members;  // raw pointers; we don't own
    D3D12_RESOURCE_DESC representative_desc{};  // first member's desc
    const Mirror* shadow = nullptr;  // shared shadow allocated on 2nd insert
    bool fog_shape = false;
    uint64_t seq = 0;
};

struct AliasBucketRegistry {
    std::mutex mu;
    std::unordered_map<AliasBucketKey, AliasBucket, AliasBucketKeyHash> buckets;
    std::unordered_map<ID3D12Resource*, AliasBucketKey> resource_to_key;
};

inline AliasBucketRegistry& bucket_registry() {
    static AliasBucketRegistry r;
    return r;
}

// Called from CreatePlacedResource_Hook. Adds the new resource to its bucket;
// if the bucket reaches >=2 members of fog shape, allocates a shared shadow.
// Returns the bucket's shadow Mirror* (may be nullptr if shadow not yet
// allocated, e.g. bucket has only 1 member).
const Mirror* register_placed(
    ID3D12Device* device,
    ID3D12Resource* game_resource,
    ID3D12Heap* heap,
    UINT64 heap_offset,
    const D3D12_RESOURCE_DESC& desc);

// Lookup the shadow Mirror for a game resource that's in a tracked bucket.
// Returns nullptr if the resource isn't bucket-tracked or its bucket has no
// shadow yet.
const Mirror* find_bucket_shadow(ID3D12Resource* game_resource);

// CBV_SRV_UAV heap GPU↔CPU base tracking. When a heap is bound via
// SetDescriptorHeaps we need to know its CPU base so we can convert a
// dispatch's GPU descriptor handle back to a CPU handle for lookup.
struct DescriptorHeapBase {
    SIZE_T cpu_base = 0;
    UINT64 gpu_base = 0;
    UINT stride = 0;
    UINT capacity = 0;
};

struct DescriptorHeapBaseMap {
    std::mutex mu;
    std::unordered_map<ID3D12DescriptorHeap*, DescriptorHeapBase> map;

    void set(ID3D12DescriptorHeap* heap, const DescriptorHeapBase& info) {
        if (heap == nullptr) return;
        std::scoped_lock _{mu};
        map[heap] = info;
    }

    DescriptorHeapBase find(ID3D12DescriptorHeap* heap) {
        if (heap == nullptr) return {};
        std::scoped_lock _{mu};
        const auto it = map.find(heap);
        return it != map.end() ? it->second : DescriptorHeapBase{};
    }

    // Given a GPU descriptor address, find the heap whose GPU range contains it
    // and return the offset to CPU base.
    bool resolve_gpu_to_cpu(UINT64 gpu_ptr, SIZE_T& out_cpu_ptr) {
        if (gpu_ptr == 0) return false;
        std::scoped_lock _{mu};
        for (const auto& [heap, info] : map) {
            if (info.gpu_base == 0 || info.stride == 0 || info.capacity == 0) continue;
            const UINT64 end = info.gpu_base + static_cast<UINT64>(info.stride) * info.capacity;
            if (gpu_ptr >= info.gpu_base && gpu_ptr < end) {
                const UINT64 offset = gpu_ptr - info.gpu_base;
                out_cpu_ptr = info.cpu_base + static_cast<SIZE_T>(offset);
                return true;
            }
        }
        return false;
    }
};

inline DescriptorHeapBaseMap& descriptor_heaps() {
    static DescriptorHeapBaseMap m;
    return m;
}

// Given a GPU handle pointing into a CBV/SRV/UAV heap, walk the table and
// look up which game UAV is at each slot. Returns the index where a tracked
// mirror resource is found, or -1 if none.
//
// search_count: how many consecutive descriptor slots to scan (typical
// table range = 8..16; we cap at 32 to bound cost).
int find_tracked_uav_slot(UINT64 gpu_table_base, int search_count);

// UEVR-owned descriptor heap holding mirror UAV+SRV pairs. GPU-visible.
struct MirrorHeapState {
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    UINT descriptor_size = 0;
    UINT capacity = 0;
    std::atomic<UINT> next_index{0};

    bool initialized() const { return heap != nullptr; }
};

// Separate scratch heap used to build per-dispatch patched descriptor tables.
// Allocated as a ring buffer; old entries get overwritten after a frame, so we
// only need enough slots to cover ~all the dispatches in-flight at once.
struct ScratchHeapState {
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    UINT descriptor_size = 0;
    UINT capacity = 0;
    std::atomic<UINT> next_index{0};

    bool initialized() const { return heap != nullptr; }
};

inline MirrorHeapState& mirror_heap() {
    static MirrorHeapState s;
    return s;
}

inline ScratchHeapState& scratch_heap() {
    static ScratchHeapState s;
    return s;
}

// V4: RTV heap for mirror RTV descriptors. Separate from mirror_heap (which
// is CBV/SRV/UAV). RTV heaps are non-shader-visible CPU-only.
struct MirrorRtvHeapState {
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    UINT descriptor_size = 0;
    UINT capacity = 0;
    std::atomic<UINT> next_index{0};
    bool initialized() const { return heap != nullptr; }
};

inline MirrorRtvHeapState& mirror_rtv_heap() {
    static MirrorRtvHeapState s;
    return s;
}

bool ensure_mirror_rtv_heap(ID3D12Device* device);
UINT alloc_rtv_slot();

// Initialize the descriptor heap on first detection. Capacity = 256 slots
// (== 128 mirrors × 2 descriptors each). Should be ample for SN2's fog chain.
bool ensure_mirror_heap(ID3D12Device* device);

// Initialize the scratch heap on first dispatch redirect. Capacity = 4096
// slots = 256 patched tables × 16 slots each. Wraps modulo so old tables
// are clobbered (safe — the GPU has consumed them by then).
bool ensure_scratch_heap(ID3D12Device* device);

// Allocate two consecutive descriptor slots; returns the base index.
// Returns UINT32_MAX on overflow.
UINT alloc_two_slots();

// Allocate N consecutive scratch slots; returns the base index modulo
// capacity. Wraps automatically.
UINT alloc_scratch_range(UINT count);

// Create a parallel resource + UAV + SRV for a detected fog-candidate game
// resource. Registers the mapping. Idempotent — returns existing mirror if
// already created. Returns nullptr on failure.
const Mirror* create_mirror_for(
    ID3D12Device* device,
    ID3D12Resource* game_resource,
    const D3D12_RESOURCE_DESC& desc);

}  // namespace sn2_uwe_fog_mirror
