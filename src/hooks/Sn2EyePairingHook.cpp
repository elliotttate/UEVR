// Sn2EyePairingHook.cpp

#include "Sn2EyePairingHook.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Windows.h>
#include <wrl/client.h>
#include <spdlog/spdlog.h>

namespace sn2_eye_pairing {

namespace {

inline bool env_flag(const char* name) {
    char buf[8]{};
    const auto len = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return false;
    return buf[0] && buf[0] != '0';
}

inline std::string env_str(const char* name) {
    char buf[2048]{};
    const auto len = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return {};
    return std::string{buf, len};
}

struct ObsKey {
    uintptr_t pso;
    uint32_t root_idx;
    uint32_t slot_idx;
    bool operator==(const ObsKey& o) const noexcept {
        return pso == o.pso && root_idx == o.root_idx && slot_idx == o.slot_idx;
    }
};

struct ObsKeyHash {
    size_t operator()(const ObsKey& k) const noexcept {
        const auto h1 = std::hash<uintptr_t>{}(k.pso);
        const auto h2 = std::hash<uint32_t>{}(k.root_idx) * 0x9e3779b1u;
        const auto h3 = std::hash<uint32_t>{}(k.slot_idx) * 0x85ebca6bu;
        return h1 ^ h2 ^ h3;
    }
};

struct Observation {
    // Resources we've seen at this (pso, root, slot) per eye bucket.
    std::unordered_set<ID3D12Resource*> left;
    std::unordered_set<ID3D12Resource*> right;
    // Most-recent CPU handle observed for each resource (lets us copy the
    // game's own SRV descriptor when we redirect, preserving format/mip
    // settings).
    std::unordered_map<ID3D12Resource*, SIZE_T> resource_to_cpu_handle;
};

// Desc-based resource profile. Used as a fingerprint when pairing.
struct ResProfile {
    uint64_t width = 0;
    uint32_t height = 0;
    uint16_t depth_or_array = 0;
    uint8_t  dimension = 0;  // D3D12_RESOURCE_DIMENSION (0..4)
    uint8_t  mip_levels = 0;
    uint16_t sample_count = 1;
    uint32_t sample_quality = 0;
    uint32_t format = 0;     // DXGI_FORMAT
    uint32_t flags = 0;      // D3D12_RESOURCE_FLAGS
    uint32_t layout = 0;     // D3D12_TEXTURE_LAYOUT
    uint64_t alignment = 0;
    uint32_t buckets = 0;    // bit 1 = LEFT, bit 2 = RIGHT
    SIZE_T cpu_handle = 0;   // most-recent observed CPU handle
    uint64_t first_seen_frame_hash = 0;  // observation-order tiebreaker
    // creation/observation sequence number (monotonic)
    uint64_t seq = 0;
    // Last natural draw observation per eye bucket. Index 1 = LEFT, 2 = RIGHT.
    // UE5 pooled render targets can reuse one ID3D12Resource* for different
    // logical eyes on later frames; these fields let lookups reject stale role
    // mappings and prefer the current slot-local owner.
    uint64_t last_seen_seq[3]{};
    uint64_t last_seen_present[3]{};
    uint32_t last_seen_root[3]{};
    uint32_t last_seen_slot[3]{};
    uintptr_t last_seen_pso[3]{};
    SIZE_T last_seen_cpu_handle[3]{};
    // Broader eye-role signal. Natural SRV-table observations update this
    // alongside last_seen_seq; aliasing barriers update only this, preserving
    // the slot-local data used by desc-slot fallback.
    uint64_t role_seq[3]{};
    uint64_t role_present[3]{};
};

struct DescKey {
    uint64_t width;
    uint32_t height;
    uint16_t depth_or_array;
    uint8_t  dimension;
    uint8_t  mip_levels;
    uint16_t sample_count;
    uint32_t sample_quality;
    uint32_t format;
    uint32_t flags;
    uint32_t layout;
    uint64_t alignment;
    bool operator==(const DescKey& o) const noexcept {
        return width == o.width && height == o.height &&
               depth_or_array == o.depth_or_array &&
               dimension == o.dimension && mip_levels == o.mip_levels &&
               sample_count == o.sample_count &&
               sample_quality == o.sample_quality &&
               format == o.format && flags == o.flags &&
               layout == o.layout && alignment == o.alignment;
    }
};

struct DescKeyHash {
    size_t operator()(const DescKey& k) const noexcept {
        size_t h = std::hash<uint64_t>{}(k.width);
        h = (h * 1315423911u) ^ std::hash<uint32_t>{}(k.height);
        h = (h * 1315423911u) ^ std::hash<uint16_t>{}(k.depth_or_array);
        h = (h * 1315423911u) ^ std::hash<uint8_t>{}(k.dimension);
        h = (h * 1315423911u) ^ std::hash<uint8_t>{}(k.mip_levels);
        h = (h * 1315423911u) ^ std::hash<uint16_t>{}(k.sample_count);
        h = (h * 1315423911u) ^ std::hash<uint32_t>{}(k.sample_quality);
        h = (h * 1315423911u) ^ std::hash<uint32_t>{}(k.format);
        h = (h * 1315423911u) ^ std::hash<uint32_t>{}(k.flags);
        h = (h * 1315423911u) ^ std::hash<uint32_t>{}(k.layout);
        h = (h * 1315423911u) ^ std::hash<uint64_t>{}(k.alignment);
        return h;
    }
};

struct DescSlotKey {
    DescKey desc;
    uint32_t root_idx;
    uint32_t slot_idx;
    bool operator==(const DescSlotKey& o) const noexcept {
        return desc == o.desc && root_idx == o.root_idx && slot_idx == o.slot_idx;
    }
};

struct DescSlotKeyHash {
    size_t operator()(const DescSlotKey& k) const noexcept {
        size_t h = DescKeyHash{}(k.desc);
        h = (h * 1315423911u) ^ std::hash<uint32_t>{}(k.root_idx);
        h = (h * 1315423911u) ^ std::hash<uint32_t>{}(k.slot_idx);
        return h;
    }
};

struct DescSlotObservation {
    std::unordered_set<ID3D12Resource*> left;
    std::unordered_set<ID3D12Resource*> right;
    ID3D12Resource* latest_left = nullptr;
    ID3D12Resource* latest_right = nullptr;
    uint64_t latest_left_seq = 0;
    uint64_t latest_right_seq = 0;
};

struct SrvViewKey {
    ID3D12Resource* resource = nullptr;
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    uint32_t format = 0;
    uint32_t view_dimension = 0;
    uint32_t component_mapping = 0;
    std::array<uint64_t, 8> fields{};

    bool operator==(const SrvViewKey& o) const noexcept {
        return resource == o.resource &&
               format == o.format &&
               view_dimension == o.view_dimension &&
               component_mapping == o.component_mapping &&
               fields == o.fields;
    }
};

struct SrvViewKeyHash {
    size_t operator()(const SrvViewKey& k) const noexcept {
        size_t h = std::hash<ID3D12Resource*>{}(k.resource);
        h = (h * 1315423911u) ^ std::hash<uint32_t>{}(k.format);
        h = (h * 1315423911u) ^ std::hash<uint32_t>{}(k.view_dimension);
        h = (h * 1315423911u) ^ std::hash<uint32_t>{}(k.component_mapping);
        for (uint64_t v : k.fields) {
            h = (h * 1315423911u) ^ std::hash<uint64_t>{}(v);
        }
        return h;
    }
};

struct SrvProfile {
    uint32_t buckets = 0;    // bit 1 = LEFT, bit 2 = RIGHT
    SIZE_T cpu_handle = 0;
    uint64_t seq = 0;
};

struct SrvDescriptorObservation {
    std::unordered_map<SrvViewKey, SIZE_T, SrvViewKeyHash> left;
    std::unordered_map<SrvViewKey, SIZE_T, SrvViewKeyHash> right;
};

uint32_t float_bits(float v) {
    uint32_t out = 0;
    std::memcpy(&out, &v, sizeof(out));
    return out;
}

SrvViewKey make_srv_view_key(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc) {
    SrvViewKey k{};
    k.resource = resource;
    k.desc = desc;
    k.format = static_cast<uint32_t>(desc.Format);
    k.view_dimension = static_cast<uint32_t>(desc.ViewDimension);
    k.component_mapping = desc.Shader4ComponentMapping;
    switch (desc.ViewDimension) {
    case D3D12_SRV_DIMENSION_BUFFER:
        k.fields[0] = desc.Buffer.FirstElement;
        k.fields[1] = desc.Buffer.NumElements;
        k.fields[2] = desc.Buffer.StructureByteStride;
        k.fields[3] = desc.Buffer.Flags;
        break;
    case D3D12_SRV_DIMENSION_TEXTURE1D:
        k.fields[0] = desc.Texture1D.MostDetailedMip;
        k.fields[1] = desc.Texture1D.MipLevels;
        k.fields[2] = float_bits(desc.Texture1D.ResourceMinLODClamp);
        break;
    case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
        k.fields[0] = desc.Texture1DArray.MostDetailedMip;
        k.fields[1] = desc.Texture1DArray.MipLevels;
        k.fields[2] = desc.Texture1DArray.FirstArraySlice;
        k.fields[3] = desc.Texture1DArray.ArraySize;
        k.fields[4] = float_bits(desc.Texture1DArray.ResourceMinLODClamp);
        break;
    case D3D12_SRV_DIMENSION_TEXTURE2D:
        k.fields[0] = desc.Texture2D.MostDetailedMip;
        k.fields[1] = desc.Texture2D.MipLevels;
        k.fields[2] = desc.Texture2D.PlaneSlice;
        k.fields[3] = float_bits(desc.Texture2D.ResourceMinLODClamp);
        break;
    case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
        k.fields[0] = desc.Texture2DArray.MostDetailedMip;
        k.fields[1] = desc.Texture2DArray.MipLevels;
        k.fields[2] = desc.Texture2DArray.FirstArraySlice;
        k.fields[3] = desc.Texture2DArray.ArraySize;
        k.fields[4] = desc.Texture2DArray.PlaneSlice;
        k.fields[5] = float_bits(desc.Texture2DArray.ResourceMinLODClamp);
        break;
    case D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY:
        k.fields[0] = desc.Texture2DMSArray.FirstArraySlice;
        k.fields[1] = desc.Texture2DMSArray.ArraySize;
        break;
    case D3D12_SRV_DIMENSION_TEXTURE3D:
        k.fields[0] = desc.Texture3D.MostDetailedMip;
        k.fields[1] = desc.Texture3D.MipLevels;
        k.fields[2] = float_bits(desc.Texture3D.ResourceMinLODClamp);
        break;
    case D3D12_SRV_DIMENSION_TEXTURECUBE:
        k.fields[0] = desc.TextureCube.MostDetailedMip;
        k.fields[1] = desc.TextureCube.MipLevels;
        k.fields[2] = float_bits(desc.TextureCube.ResourceMinLODClamp);
        break;
    case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
        k.fields[0] = desc.TextureCubeArray.MostDetailedMip;
        k.fields[1] = desc.TextureCubeArray.MipLevels;
        k.fields[2] = desc.TextureCubeArray.First2DArrayFace;
        k.fields[3] = desc.TextureCubeArray.NumCubes;
        k.fields[4] = float_bits(desc.TextureCubeArray.ResourceMinLODClamp);
        break;
    case D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE:
        k.fields[0] = desc.RaytracingAccelerationStructure.Location;
        break;
    default:
        break;
    }
    return k;
}

bool is_array_slice_srv_key(const SrvViewKey& k) {
    switch (static_cast<D3D12_SRV_DIMENSION>(k.view_dimension)) {
    case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
    case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
    case D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY:
    case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
        return true;
    default:
        return false;
    }
}

uint64_t srv_first_array_slice(const SrvViewKey& k) {
    switch (static_cast<D3D12_SRV_DIMENSION>(k.view_dimension)) {
    case D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY:
        return k.fields[0];
    case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
    case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
    case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
        return k.fields[2];
    default:
        return 0;
    }
}

SrvViewKey make_srv_base_key(SrvViewKey k) {
    if (is_array_slice_srv_key(k)) {
        // Group per-slice SRVs over the same resource/mips/plane by ignoring
        // the slice origin. Texture2DArray slice 0 and slice 1 are then a
        // pairable descriptor-level L/R candidate.
        if (static_cast<D3D12_SRV_DIMENSION>(k.view_dimension) == D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY) {
            k.fields[0] = 0;
        } else {
            k.fields[2] = 0;
        }
    }
    return k;
}

constexpr uint32_t kPairableTextureFlags =
    D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
    D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL |
    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

bool is_pairable_texture_desc(const D3D12_RESOURCE_DESC& desc) {
    if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) return false;
    return (static_cast<uint32_t>(desc.Flags) & kPairableTextureFlags) != 0;
}

bool is_pairable_profile(const ResProfile& p) {
    if (p.dimension == static_cast<uint8_t>(D3D12_RESOURCE_DIMENSION_BUFFER)) return false;
    return (p.flags & kPairableTextureFlags) != 0;
}

DescKey make_desc_key(const D3D12_RESOURCE_DESC& desc) {
    return DescKey{
        desc.Width,
        desc.Height,
        static_cast<uint16_t>(desc.DepthOrArraySize),
        static_cast<uint8_t>(desc.Dimension),
        static_cast<uint8_t>(desc.MipLevels),
        static_cast<uint16_t>(desc.SampleDesc.Count),
        static_cast<uint32_t>(desc.SampleDesc.Quality),
        static_cast<uint32_t>(desc.Format),
        static_cast<uint32_t>(desc.Flags),
        static_cast<uint32_t>(desc.Layout),
        desc.Alignment
    };
}

DescKey make_desc_key(const ResProfile& p) {
    return DescKey{
        p.width,
        p.height,
        p.depth_or_array,
        p.dimension,
        p.mip_levels,
        p.sample_count,
        p.sample_quality,
        p.format,
        p.flags,
        p.layout,
        p.alignment
    };
}

void fill_profile_from_desc(ResProfile& prof, const D3D12_RESOURCE_DESC& desc) {
    prof.width = desc.Width;
    prof.height = desc.Height;
    prof.depth_or_array = desc.DepthOrArraySize;
    prof.dimension = static_cast<uint8_t>(desc.Dimension);
    prof.mip_levels = static_cast<uint8_t>(desc.MipLevels);
    prof.sample_count = static_cast<uint16_t>(desc.SampleDesc.Count);
    prof.sample_quality = static_cast<uint32_t>(desc.SampleDesc.Quality);
    prof.format = static_cast<uint32_t>(desc.Format);
    prof.flags = static_cast<uint32_t>(desc.Flags);
    prof.layout = static_cast<uint32_t>(desc.Layout);
    prof.alignment = desc.Alignment;
}

struct State {
    std::mutex mu;
    std::unordered_map<ObsKey, Observation, ObsKeyHash> observations;
    std::unordered_map<ObsKey, SrvDescriptorObservation, ObsKeyHash> srv_observations;
    // Same descriptor + same root/slot observations, deliberately ignoring PSO.
    // UE5 can pick different PSO permutations per view bucket, so (pso,root,slot)
    // is often too strict once INSTANCED_STEREO / MULTI_VIEW variants diverge.
    std::unordered_map<DescSlotKey, DescSlotObservation, DescSlotKeyHash> desc_slot_observations;
    // Per-resource profile.
    std::unordered_map<ID3D12Resource*, ResProfile> resource_profile;
    // Per-SRV-view profile. This covers ISR/multiview bindings where both
    // eyes use the same Texture2DArray resource but different FirstArraySlice.
    std::unordered_map<SrvViewKey, SrvProfile, SrvViewKeyHash> srv_profile;
    // Stable L→R map. Populated by pair formation either from same-(pso,
    // root, slot) observations OR from desc-matched L-only/R-only resources
    // attempted on periodic basis.
    std::unordered_map<ID3D12Resource*, ID3D12Resource*> left_to_right;
    std::unordered_map<SrvViewKey, SrvViewKey, SrvViewKeyHash> srv_left_to_right;
    // Reverse map for diagnostics.
    std::unordered_map<ID3D12Resource*, ID3D12Resource*> right_to_left;
    std::unordered_map<SrvViewKey, SrvViewKey, SrvViewKeyHash> srv_right_to_left;
    // Best-known CPU descriptor handle for each tracked resource (the most
    // recent one we observed in any draw).
    std::unordered_map<ID3D12Resource*, SIZE_T> latest_cpu_handle;

    // Sequence counter for ordering observations (used for tiebreaker when
    // multiple L-only / R-only resources of same desc exist).
    std::atomic<uint64_t> seq_counter{0};

    // Twin-detector: per-desc ring of "recently created, not yet paired"
    // resources. Adding a 2nd resource of same desc within the window pops
    // the first off and registers a pair.
    std::atomic<uint64_t> create_seq{0};
    std::atomic<uint64_t> present_seq{0};
    // Twin detector heuristic tuning. UE5 creates many textures between per-
    // eye twins during init, so we accept twins within a generous create-seq
    // window. We still require same-Present-frame (most twins are created
    // during scene transitions or first-frame init).
    static constexpr uint64_t kTwinMaxCreateGap = 4096;
    static constexpr uint64_t kTwinMaxPresentGap = 0;  // must be same Present frame
    struct Placement { ID3D12Heap* heap = nullptr; UINT64 offset = 0; bool valid = false; };
    struct TwinCandidate {
        ID3D12Resource* res;
        uint64_t create_seq;
        uint64_t present_seq;
        Placement placement;
    };
    std::unordered_map<DescKey, TwinCandidate, DescKeyHash> twin_candidates;

    // Scratch SRV cache for right-eye resources.
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> scratch_heap;
    UINT scratch_capacity = 0;
    UINT scratch_stride = 0;
    std::atomic<UINT> scratch_next{0};
    SIZE_T scratch_cpu_base = 0;
    UINT64 scratch_gpu_base = 0;
    // Bottom region: dedicated SRV-for-right-resource cache (one descriptor
    // per unique right resource).
    static constexpr UINT kRightSrvReserve = 1024;
    std::atomic<UINT> right_srv_next{0};
    std::unordered_map<ID3D12Resource*, UINT> right_srv_index;
    std::unordered_map<SrvViewKey, UINT, SrvViewKeyHash> right_srv_view_index;

    struct UeViewAnchor {
        uintptr_t scene_renderer = 0;
        uintptr_t view_info = 0;
        uint64_t count = 0;
    };
    UeViewAnchor ue_views[3]{};
};

State& state() {
    static State s;
    return s;
}

bool resource_currently_looks_like_left(const ResProfile& p) {
    const uint64_t left_seq = p.role_seq[1] > p.last_seen_seq[1] ? p.role_seq[1] : p.last_seen_seq[1];
    const uint64_t right_seq = p.role_seq[2] > p.last_seen_seq[2] ? p.role_seq[2] : p.last_seen_seq[2];
    return right_seq == 0 || left_seq >= right_seq;
}

bool resource_currently_looks_like_right(const ResProfile& p) {
    const uint64_t left_seq = p.role_seq[1] > p.last_seen_seq[1] ? p.role_seq[1] : p.last_seen_seq[1];
    const uint64_t right_seq = p.role_seq[2] > p.last_seen_seq[2] ? p.role_seq[2] : p.last_seen_seq[2];
    return left_seq == 0 || right_seq >= left_seq;
}

bool resource_seen_at_slot_as_bucket(const ResProfile& p, int bucket, uint32_t root_idx, uint32_t slot_idx) {
    if (bucket != 1 && bucket != 2) return false;
    return p.last_seen_seq[bucket] != 0 &&
           p.last_seen_root[bucket] == root_idx &&
           p.last_seen_slot[bucket] == slot_idx;
}

void add_pair_locked(State& s, ID3D12Resource* left, ID3D12Resource* right,
                     const char* reason, uint32_t root_idx, uint32_t slot_idx) {
    if (left == nullptr || right == nullptr || left == right) return;
    if (s.left_to_right.find(left) != s.left_to_right.end()) return;
    if (s.right_to_left.find(left) != s.right_to_left.end()) return;
    if (s.left_to_right.find(right) != s.left_to_right.end()) return;
    if (s.right_to_left.find(right) != s.right_to_left.end()) return;

    const auto lit = s.resource_profile.find(left);
    const auto rit = s.resource_profile.find(right);
    if (lit == s.resource_profile.end() || rit == s.resource_profile.end()) return;
    if (!is_pairable_profile(lit->second) || !is_pairable_profile(rit->second)) return;
    if (!(make_desc_key(lit->second) == make_desc_key(rit->second))) return;

    s.left_to_right[left] = right;
    s.right_to_left[right] = left;
    SPDLOG_WARN("[SN2-EyePairing] new pair ({}) L=0x{:x}->R=0x{:x} root={} slot={} total_pairs={}",
                reason,
                reinterpret_cast<uintptr_t>(left),
                reinterpret_cast<uintptr_t>(right),
                root_idx,
                slot_idx,
                s.left_to_right.size());
}

void try_form_desc_slot_pair_locked(State& s, const DescSlotKey& key, DescSlotObservation& obs) {
    if (obs.left.size() != 1 || obs.right.size() != 1) return;

    ID3D12Resource* left = *obs.left.begin();
    ID3D12Resource* right = *obs.right.begin();
    add_pair_locked(s, left, right, "desc-slot", key.root_idx, key.slot_idx);
}

}  // namespace

bool env_enabled() {
    return env_flag("UEVR_SN2_EYE_PAIRING");
}

bool log_verbose_enabled() {
    return env_flag("UEVR_SN2_EYE_PAIRING_LOG");
}

bool is_target_ps_crc(uint32_t crc) {
    // Default: {0xDE7C3822}. Override with CSV env var.
    static const auto targets = []() {
        std::unordered_set<uint32_t> out;
        const auto s = env_str("UEVR_SN2_EYE_PAIRING_TARGET_PS_CRCS");
        if (s.empty()) {
            out.insert(0xDE7C3822u);
            return out;
        }
        size_t pos = 0;
        while (pos < s.size()) {
            while (pos < s.size() && (s[pos] == ',' || s[pos] == ' ' || s[pos] == ';')) ++pos;
            if (pos >= s.size()) break;
            size_t end = pos;
            while (end < s.size() && s[end] != ',' && s[end] != ' ' && s[end] != ';') ++end;
            const std::string tok = s.substr(pos, end - pos);
            pos = end;
            if (tok.empty()) continue;
            char* tail = nullptr;
            const auto v = std::strtoul(tok.c_str(), &tail, 0);
            if (tail != tok.c_str()) out.insert(static_cast<uint32_t>(v));
        }
        return out;
    }();
    return targets.find(crc) != targets.end();
}

bool ensure_scratch_heap(ID3D12Device* device) {
    auto& s = state();
    if (s.scratch_heap.Get() != nullptr) return true;
    if (device == nullptr) return false;
    static std::mutex init_mu;
    std::scoped_lock _{init_mu};
    if (s.scratch_heap.Get() != nullptr) return true;

    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 16384;  // 16k descriptors: 1024 reserved for right-eye SRV cache, 15360 for scratch tables
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    const HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap));
    if (FAILED(hr)) {
        SPDLOG_WARN("[SN2-EyePairing] CreateDescriptorHeap failed hr=0x{:08x}",
                    static_cast<uint32_t>(hr));
        return false;
    }
    s.scratch_heap = heap;
    s.scratch_capacity = desc.NumDescriptors;
    s.scratch_stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    s.scratch_cpu_base = static_cast<SIZE_T>(heap->GetCPUDescriptorHandleForHeapStart().ptr);
    s.scratch_gpu_base = heap->GetGPUDescriptorHandleForHeapStart().ptr;
    s.scratch_next.store(State::kRightSrvReserve, std::memory_order_release);
    s.right_srv_next.store(0, std::memory_order_release);
    SPDLOG_WARN("[SN2-EyePairing] scratch heap ready capacity={} stride={} cpu_base=0x{:x} gpu_base=0x{:x}",
                s.scratch_capacity, s.scratch_stride,
                static_cast<uintptr_t>(s.scratch_cpu_base), s.scratch_gpu_base);
    return true;
}

ID3D12DescriptorHeap* scratch_descriptor_heap() {
    return state().scratch_heap.Get();
}

void reset_scratch_for_frame() {
    auto& s = state();
    if (s.scratch_capacity == 0) return;
    s.scratch_next.store(State::kRightSrvReserve, std::memory_order_release);
}

SIZE_T alloc_scratch(UINT count, UINT64* gpu_out) {
    auto& s = state();
    if (s.scratch_heap.Get() == nullptr || count == 0 ||
        count > s.scratch_capacity - State::kRightSrvReserve) {
        if (gpu_out) *gpu_out = 0;
        return 0;
    }
    // Bump allocator. Wrap when we'd overflow capacity.
    UINT base = s.scratch_next.fetch_add(count, std::memory_order_acq_rel);
    if (base + count > s.scratch_capacity) {
        // Wrap: reset and re-grab.
        s.scratch_next.store(State::kRightSrvReserve + count, std::memory_order_release);
        base = State::kRightSrvReserve;
    }
    if (gpu_out) *gpu_out = s.scratch_gpu_base + static_cast<UINT64>(base) * s.scratch_stride;
    return s.scratch_cpu_base + static_cast<SIZE_T>(base) * s.scratch_stride;
}

UINT alloc_right_srv_cache_slot(State& s) {
    const UINT slot = s.right_srv_next.fetch_add(1, std::memory_order_acq_rel);
    if (slot >= State::kRightSrvReserve) {
        SPDLOG_WARN("[SN2-EyePairing] right SRV cache exhausted at slot={}", slot);
        return UINT32_MAX;
    }
    return slot;
}

SIZE_T ensure_right_srv_cpu_handle(ID3D12Device* device, ID3D12Resource* right) {
    auto& s = state();
    if (right == nullptr || device == nullptr) return 0;
    if (!ensure_scratch_heap(device)) return 0;
    {
        std::scoped_lock _{s.mu};
        const auto it = s.right_srv_index.find(right);
        if (it != s.right_srv_index.end()) {
            return s.scratch_cpu_base + static_cast<SIZE_T>(it->second) * s.scratch_stride;
        }
    }
    // Look up a previously-captured CPU handle for this resource. If we have
    // one, copy the game's SRV (preserves format/swizzle/mip). Otherwise
    // create one with defaults from the resource desc.
    SIZE_T cached_handle = 0;
    {
        std::scoped_lock _{s.mu};
        const auto it = s.latest_cpu_handle.find(right);
        if (it != s.latest_cpu_handle.end()) cached_handle = it->second;
    }
    // Allocate a slot in the right-SRV cache (bottom kRightSrvReserve range).
    const UINT slot = alloc_right_srv_cache_slot(s);
    if (slot == UINT32_MAX) {
        return 0;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE dst{
        s.scratch_cpu_base + static_cast<SIZE_T>(slot) * s.scratch_stride};
    if (cached_handle != 0) {
        D3D12_CPU_DESCRIPTOR_HANDLE src{cached_handle};
        device->CopyDescriptorsSimple(1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    } else {
        // Fallback: create default SRV from the resource desc.
        const auto rdesc = right->GetDesc();
        D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = rdesc.Format;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        switch (rdesc.Dimension) {
            case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
                if (rdesc.DepthOrArraySize > 1) {
                    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                    sv.Texture2DArray.MipLevels = rdesc.MipLevels;
                    sv.Texture2DArray.ArraySize = rdesc.DepthOrArraySize;
                } else {
                    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    sv.Texture2D.MipLevels = rdesc.MipLevels;
                }
                break;
            case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
                sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                sv.Texture3D.MipLevels = rdesc.MipLevels;
                break;
            case D3D12_RESOURCE_DIMENSION_BUFFER:
                sv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                sv.Buffer.NumElements = static_cast<UINT>(rdesc.Width / 4);
                sv.Buffer.StructureByteStride = 0;
                break;
            default:
                return 0;
        }
        device->CreateShaderResourceView(right, &sv, dst);
    }
    {
        std::scoped_lock _{s.mu};
        s.right_srv_index[right] = slot;
    }
    return dst.ptr;
}

SIZE_T ensure_right_srv_for_view(ID3D12Device* device,
                                 ID3D12Resource* left_resource,
                                 const D3D12_SHADER_RESOURCE_VIEW_DESC& left_srv_desc) {
    if (device == nullptr || left_resource == nullptr) return 0;
    if (!ensure_scratch_heap(device)) return 0;

    SrvViewKey right_key{};
    const SrvViewKey left_key = make_srv_view_key(left_resource, left_srv_desc);
    {
        auto& s = state();
        std::scoped_lock _{s.mu};
        const auto it = s.srv_left_to_right.find(left_key);
        if (it == s.srv_left_to_right.end()) return 0;
        right_key = it->second;
        if (right_key.resource == nullptr) return 0;
        const auto cached = s.right_srv_view_index.find(right_key);
        if (cached != s.right_srv_view_index.end()) {
            return s.scratch_cpu_base + static_cast<SIZE_T>(cached->second) * s.scratch_stride;
        }
    }

    auto& s = state();
    const UINT slot = alloc_right_srv_cache_slot(s);
    if (slot == UINT32_MAX) return 0;
    D3D12_CPU_DESCRIPTOR_HANDLE dst{
        s.scratch_cpu_base + static_cast<SIZE_T>(slot) * s.scratch_stride};
    device->CreateShaderResourceView(right_key.resource, &right_key.desc, dst);

    {
        std::scoped_lock _{s.mu};
        s.right_srv_view_index[right_key] = slot;
    }

    static std::atomic<uint64_t> created{0};
    const auto n = created.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 32 || (n % 256) == 0) {
        SPDLOG_WARN("[SN2-EyePairing] cached descriptor-view SRV #{} res=0x{:x} view_dim={} first_slice={}",
                    n,
                    reinterpret_cast<uintptr_t>(right_key.resource),
                    right_key.view_dimension,
                    srv_first_array_slice(right_key));
    }
    return dst.ptr;
}

static void try_form_srv_pairs_by_base_locked(State& s) {
    std::unordered_map<SrvViewKey, std::vector<SrvViewKey>, SrvViewKeyHash> left_only;
    std::unordered_map<SrvViewKey, std::vector<SrvViewKey>, SrvViewKeyHash> right_only;

    for (const auto& [key, p] : s.srv_profile) {
        if (!is_array_slice_srv_key(key)) continue;
        if (s.srv_left_to_right.find(key) != s.srv_left_to_right.end()) continue;
        if (s.srv_right_to_left.find(key) != s.srv_right_to_left.end()) continue;
        if (key.resource == nullptr) continue;
        const auto rdesc = key.resource->GetDesc();
        if (!is_pairable_texture_desc(rdesc)) continue;

        const bool L = (p.buckets & 1u) != 0;
        const bool R = (p.buckets & 2u) != 0;
        const SrvViewKey base = make_srv_base_key(key);
        if (L && !R) left_only[base].push_back(key);
        else if (R && !L) right_only[base].push_back(key);
    }

    size_t pairs_added = 0;
    for (auto& [base, lefts] : left_only) {
        auto it = right_only.find(base);
        if (it == right_only.end()) continue;
        auto& rights = it->second;
        if (lefts.size() != rights.size()) continue;

        auto sorter = [&](const SrvViewKey& a, const SrvViewKey& b) {
            const uint64_t as = srv_first_array_slice(a);
            const uint64_t bs = srv_first_array_slice(b);
            if (as != bs) return as < bs;
            return s.srv_profile[a].seq < s.srv_profile[b].seq;
        };
        std::sort(lefts.begin(), lefts.end(), sorter);
        std::sort(rights.begin(), rights.end(), sorter);

        for (size_t i = 0; i < lefts.size(); ++i) {
            const SrvViewKey& L = lefts[i];
            const SrvViewKey& R = rights[i];
            if (L == R) continue;
            if (srv_first_array_slice(L) == srv_first_array_slice(R)) continue;
            if (s.srv_left_to_right.find(L) != s.srv_left_to_right.end()) continue;
            if (s.srv_right_to_left.find(R) != s.srv_right_to_left.end()) continue;
            s.srv_left_to_right[L] = R;
            s.srv_right_to_left[R] = L;
            ++pairs_added;
            if (s.srv_left_to_right.size() <= 64 || log_verbose_enabled()) {
                const auto rdesc = L.resource->GetDesc();
                SPDLOG_WARN("[SN2-EyePairing] descriptor-pair L=0x{:x}[slice={}]→R=0x{:x}[slice={}] ({}x{}x{} fmt={} view_dim={}) total_desc_pairs={}",
                            reinterpret_cast<uintptr_t>(L.resource),
                            srv_first_array_slice(L),
                            reinterpret_cast<uintptr_t>(R.resource),
                            srv_first_array_slice(R),
                            static_cast<unsigned>(rdesc.Width),
                            static_cast<unsigned>(rdesc.Height),
                            static_cast<unsigned>(rdesc.DepthOrArraySize),
                            static_cast<unsigned>(rdesc.Format),
                            L.view_dimension,
                            s.srv_left_to_right.size());
            }
        }
    }

    if (pairs_added > 0) {
        SPDLOG_WARN("[SN2-EyePairing] form_srv_pairs_by_base: added {} descriptor pairs, total={}",
                    pairs_added, s.srv_left_to_right.size());
    }
}

static void try_form_pairs_by_desc_locked(State& s) {
    // Walk every tracked resource. Bucket-classify as L-only / R-only / both.
    // Group L-only and R-only into desc-keyed lists. For each desc with
    // matching counts, pair by seq order.
    std::unordered_map<DescKey, std::vector<ID3D12Resource*>, DescKeyHash> left_only;
    std::unordered_map<DescKey, std::vector<ID3D12Resource*>, DescKeyHash> right_only;
    for (const auto& [res, p] : s.resource_profile) {
        if (s.left_to_right.find(res) != s.left_to_right.end()) continue;  // already paired
        if (s.right_to_left.find(res) != s.right_to_left.end()) continue;
        if (!is_pairable_profile(p)) continue;
        DescKey k = make_desc_key(p);
        const bool L = (p.buckets & 1u) != 0;
        const bool R = (p.buckets & 2u) != 0;
        if (L && !R) left_only[k].push_back(res);
        else if (R && !L) right_only[k].push_back(res);
    }
    // For each desc key present in both lists with the SAME count, pair by
    // seq order. Different counts → ambiguous, skip.
    size_t pairs_added = 0;
    for (auto& [key, lefts] : left_only) {
        auto it = right_only.find(key);
        if (it == right_only.end()) continue;
        auto& rights = it->second;
        if (lefts.size() != rights.size()) continue;
        // Sort by seq number for stable pairing
        std::sort(lefts.begin(), lefts.end(), [&](ID3D12Resource* a, ID3D12Resource* b) {
            return s.resource_profile[a].seq < s.resource_profile[b].seq;
        });
        std::sort(rights.begin(), rights.end(), [&](ID3D12Resource* a, ID3D12Resource* b) {
            return s.resource_profile[a].seq < s.resource_profile[b].seq;
        });
        for (size_t i = 0; i < lefts.size(); ++i) {
            ID3D12Resource* L = lefts[i];
            ID3D12Resource* R = rights[i];
            if (L == R) continue;
            s.left_to_right[L] = R;
            s.right_to_left[R] = L;
            ++pairs_added;
            if (s.left_to_right.size() <= 64 || log_verbose_enabled()) {
                SPDLOG_WARN("[SN2-EyePairing] desc-pair L=0x{:x}→R=0x{:x} ({}x{}x{} fmt={} flags=0x{:x} dim={}) total_pairs={}",
                            reinterpret_cast<uintptr_t>(L),
                            reinterpret_cast<uintptr_t>(R),
                            static_cast<unsigned>(key.width),
                            static_cast<unsigned>(key.height),
                            static_cast<unsigned>(key.depth_or_array),
                            static_cast<unsigned>(key.format),
                            static_cast<unsigned>(key.flags),
                            static_cast<unsigned>(key.dimension),
                            s.left_to_right.size());
            }
        }
    }
    if (pairs_added > 0) {
        SPDLOG_WARN("[SN2-EyePairing] form_pairs_by_desc: added {} new pairs, total={}",
                    pairs_added, s.left_to_right.size());
    }
}

void record(int bucket, uintptr_t pso, uint32_t root_idx, uint32_t slot_idx,
            ID3D12Resource* res, SIZE_T cpu_handle, ID3D12Device* /*device*/)
{
    if (!env_enabled()) return;
    if (bucket != 1 && bucket != 2) return;
    if (res == nullptr || pso == 0) return;
    // CRITICAL: skip observations made during UEVR's right-eye dup. Those
    // draws bind LEFT-eye resources at right viewport and would otherwise
    // pollute the L-only/R-only classification.
    if (in_synthetic_draw()) return;

    auto& s = state();
    std::scoped_lock _{s.mu};

    if (cpu_handle != 0) s.latest_cpu_handle[res] = cpu_handle;

    // Update per-resource profile.
    auto& prof = s.resource_profile[res];
    const uint64_t obs_seq = s.seq_counter.fetch_add(1, std::memory_order_relaxed) + 1;
    if (prof.seq == 0) {
        prof.seq = obs_seq;
        const auto rdesc = res->GetDesc();
        fill_profile_from_desc(prof, rdesc);
    }
    prof.buckets |= (bucket == 1) ? 1u : 2u;
    if (cpu_handle != 0) prof.cpu_handle = cpu_handle;
    prof.last_seen_seq[bucket] = obs_seq;
    prof.last_seen_present[bucket] = s.present_seq.load(std::memory_order_relaxed);
    prof.last_seen_root[bucket] = root_idx;
    prof.last_seen_slot[bucket] = slot_idx;
    prof.last_seen_pso[bucket] = pso;
    prof.last_seen_cpu_handle[bucket] = cpu_handle;
    prof.role_seq[bucket] = obs_seq;
    prof.role_present[bucket] = prof.last_seen_present[bucket];

    if (is_pairable_profile(prof)) {
        const DescSlotKey desc_slot_key{make_desc_key(prof), root_idx, slot_idx};
        auto& desc_slot = s.desc_slot_observations[desc_slot_key];
        if (bucket == 1) {
            desc_slot.left.insert(res);
            desc_slot.latest_left = res;
            desc_slot.latest_left_seq = obs_seq;
        } else {
            desc_slot.right.insert(res);
            desc_slot.latest_right = res;
            desc_slot.latest_right_seq = obs_seq;
        }
        try_form_desc_slot_pair_locked(s, desc_slot_key, desc_slot);
    }

    // Same-(pso, root, slot) observation (kept for traditional pair detection)
    const ObsKey key{pso, root_idx, slot_idx};
    auto& obs = s.observations[key];
    if (bucket == 1) obs.left.insert(res);
    else obs.right.insert(res);
    if (cpu_handle != 0) obs.resource_to_cpu_handle[res] = cpu_handle;

    // Same-PSO/slot pair formation (rare under -emulatestereo, but handle it).
    if (obs.left.size() == 1 && obs.right.size() == 1) {
        ID3D12Resource* L = *obs.left.begin();
        ID3D12Resource* R = *obs.right.begin();
        const auto lit = s.resource_profile.find(L);
        const auto rit = s.resource_profile.find(R);
        if (L != R &&
            lit != s.resource_profile.end() &&
            rit != s.resource_profile.end() &&
            is_pairable_profile(lit->second) &&
            is_pairable_profile(rit->second) &&
            s.left_to_right.find(L) == s.left_to_right.end() &&
            s.right_to_left.find(L) == s.right_to_left.end() &&
            s.left_to_right.find(R) == s.left_to_right.end() &&
            s.right_to_left.find(R) == s.right_to_left.end()) {
            add_pair_locked(s, L, R, "same-pso", root_idx, slot_idx);
        }
    }

    // Periodic desc-based pair formation. Cheap when nothing's changed because
    // the matching only adds new pairs. Trigger every K observations.
    static std::atomic<uint64_t> obs_count{0};
    const auto n = obs_count.fetch_add(1, std::memory_order_relaxed) + 1;
    constexpr uint64_t kRebuildInterval = 5000;
    if ((n % kRebuildInterval) == 0) {
        try_form_pairs_by_desc_locked(s);
    }
}

void record_srv_descriptor(int bucket, uintptr_t pso, uint32_t root_idx, uint32_t slot_idx,
                           ID3D12Resource* res, SIZE_T cpu_handle,
                           const D3D12_SHADER_RESOURCE_VIEW_DESC& srv_desc,
                           ID3D12Device* /*device*/)
{
    if (!env_enabled()) return;
    if (bucket != 1 && bucket != 2) return;
    if (res == nullptr || pso == 0 || cpu_handle == 0) return;
    if (in_synthetic_draw()) return;

    const auto rdesc = res->GetDesc();
    if (!is_pairable_texture_desc(rdesc)) return;

    const SrvViewKey key = make_srv_view_key(res, srv_desc);
    if (!is_array_slice_srv_key(key)) return;

    auto& s = state();
    std::scoped_lock _{s.mu};

    if (cpu_handle != 0) s.latest_cpu_handle[res] = cpu_handle;

    auto& prof = s.resource_profile[res];
    const uint64_t obs_seq = s.seq_counter.fetch_add(1, std::memory_order_relaxed) + 1;
    if (prof.seq == 0) {
        prof.seq = obs_seq;
        fill_profile_from_desc(prof, rdesc);
    }
    prof.buckets |= (bucket == 1) ? 1u : 2u;
    prof.cpu_handle = cpu_handle;
    prof.last_seen_seq[bucket] = obs_seq;
    prof.last_seen_present[bucket] = s.present_seq.load(std::memory_order_relaxed);
    prof.last_seen_root[bucket] = root_idx;
    prof.last_seen_slot[bucket] = slot_idx;
    prof.last_seen_pso[bucket] = pso;
    prof.last_seen_cpu_handle[bucket] = cpu_handle;
    prof.role_seq[bucket] = obs_seq;
    prof.role_present[bucket] = prof.last_seen_present[bucket];

    auto& srv_prof = s.srv_profile[key];
    if (srv_prof.seq == 0) {
        srv_prof.seq = s.seq_counter.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    srv_prof.buckets |= (bucket == 1) ? 1u : 2u;
    srv_prof.cpu_handle = cpu_handle;

    const ObsKey obs_key{pso, root_idx, slot_idx};
    auto& obs = s.srv_observations[obs_key];
    auto& eye = (bucket == 1) ? obs.left : obs.right;
    eye[key] = cpu_handle;

    if (obs.left.size() == 1 && obs.right.size() == 1) {
        const SrvViewKey& L = obs.left.begin()->first;
        const SrvViewKey& R = obs.right.begin()->first;
        if (!(L == R) &&
            is_array_slice_srv_key(L) &&
            make_srv_base_key(L) == make_srv_base_key(R) &&
            srv_first_array_slice(L) != srv_first_array_slice(R) &&
            s.srv_left_to_right.find(L) == s.srv_left_to_right.end() &&
            s.srv_right_to_left.find(R) == s.srv_right_to_left.end()) {
            s.srv_left_to_right[L] = R;
            s.srv_right_to_left[R] = L;
            SPDLOG_WARN("[SN2-EyePairing] new descriptor-pair (same-pso) L=0x{:x}[slice={}]→R=0x{:x}[slice={}] from (pso=0x{:x} root={} slot={}) total_desc_pairs={}",
                        reinterpret_cast<uintptr_t>(L.resource),
                        srv_first_array_slice(L),
                        reinterpret_cast<uintptr_t>(R.resource),
                        srv_first_array_slice(R),
                        pso, root_idx, slot_idx,
                        s.srv_left_to_right.size());
        }
    }

    static std::atomic<uint64_t> srv_obs_count{0};
    const auto n = srv_obs_count.fetch_add(1, std::memory_order_relaxed) + 1;
    constexpr uint64_t kSrvRebuildInterval = 1024;
    if ((n % kSrvRebuildInterval) == 0) {
        try_form_srv_pairs_by_base_locked(s);
    }
}

ID3D12Resource* right_for(ID3D12Resource* left) {
    if (left == nullptr) return nullptr;
    auto& s = state();
    std::scoped_lock _{s.mu};
    const auto it = s.left_to_right.find(left);
    if (it == s.left_to_right.end()) return nullptr;
    ID3D12Resource* right = it->second;

    // UE5's render-target pool can later recycle the same resource pointer for
    // the opposite eye. Keep early learned pairs cached, but do not apply one
    // when recent natural observations say either endpoint has changed roles.
    const auto lit = s.resource_profile.find(left);
    if (lit != s.resource_profile.end() && !resource_currently_looks_like_left(lit->second)) {
        return nullptr;
    }
    const auto rit = s.resource_profile.find(right);
    if (rit != s.resource_profile.end() && !resource_currently_looks_like_right(rit->second)) {
        return nullptr;
    }
    return right;
}

ID3D12Resource* right_for_desc_slot(ID3D12Resource* left, uint32_t root_idx, uint32_t slot_idx) {
    if (left == nullptr) return nullptr;
    auto& s = state();
    std::scoped_lock _{s.mu};

    const auto lit = s.resource_profile.find(left);
    if (lit == s.resource_profile.end() || !is_pairable_profile(lit->second)) return nullptr;
    if (!resource_currently_looks_like_left(lit->second)) return nullptr;

    const DescSlotKey key{make_desc_key(lit->second), root_idx, slot_idx};
    const auto oit = s.desc_slot_observations.find(key);
    if (oit == s.desc_slot_observations.end()) return nullptr;
    const DescSlotObservation& obs = oit->second;

    ID3D12Resource* candidate = nullptr;
    if (obs.right.size() == 1) {
        candidate = *obs.right.begin();
    } else if (obs.latest_right != nullptr) {
        candidate = obs.latest_right;
    }

    if (candidate == nullptr || candidate == left) return nullptr;
    const auto rit = s.resource_profile.find(candidate);
    if (rit == s.resource_profile.end()) return nullptr;
    if (!is_pairable_profile(rit->second)) return nullptr;
    if (!(make_desc_key(rit->second) == key.desc)) return nullptr;
    if (!resource_currently_looks_like_right(rit->second)) return nullptr;
    if (!resource_seen_at_slot_as_bucket(rit->second, 2, root_idx, slot_idx)) return nullptr;

    return candidate;
}

size_t pair_count() {
    auto& s = state();
    std::scoped_lock _{s.mu};
    return s.left_to_right.size() + s.srv_left_to_right.size();
}

namespace {
thread_local int g_synth_depth = 0;
thread_local int g_ue_view_depth = 0;
thread_local int g_ue_view_stack[16]{};
thread_local int g_ue_view_current_bucket = 0;
constexpr int kUeViewStackCapacity = sizeof(g_ue_view_stack) / sizeof(g_ue_view_stack[0]);
}

void enter_synthetic_draw() { ++g_synth_depth; }
void exit_synthetic_draw() { if (g_synth_depth > 0) --g_synth_depth; }
bool in_synthetic_draw() { return g_synth_depth > 0; }

void enter_ue_view_scope(int bucket, uintptr_t scene_renderer, uintptr_t view_info, const char* pass_tag) {
    if (bucket != 1 && bucket != 2) return;
    if (g_ue_view_depth < kUeViewStackCapacity) {
        g_ue_view_stack[g_ue_view_depth] = bucket;
    }
    ++g_ue_view_depth;
    g_ue_view_current_bucket = bucket;

    if (!env_enabled()) return;

    auto& s = state();
    std::scoped_lock _{s.mu};
    auto& anchor = s.ue_views[bucket];
    anchor.scene_renderer = scene_renderer;
    anchor.view_info = view_info;
    ++anchor.count;

    static std::atomic<uint64_t> logs{0};
    const auto n = logs.fetch_add(1, std::memory_order_relaxed);
    if (n < 16 || (n % 600) == 0 || log_verbose_enabled()) {
        SPDLOG_WARN("[SN2-EyePairing][UEView] {} bucket={} scene_renderer=0x{:x} view=0x{:x} count={}",
                    pass_tag != nullptr ? pass_tag : "?",
                    bucket,
                    scene_renderer,
                    view_info,
                    anchor.count);
    }
}

void exit_ue_view_scope() {
    if (g_ue_view_depth <= 0) {
        g_ue_view_current_bucket = 0;
        return;
    }
    --g_ue_view_depth;
    if (g_ue_view_depth > 0) {
        const int idx = (g_ue_view_depth - 1 < kUeViewStackCapacity - 1)
            ? (g_ue_view_depth - 1)
            : (kUeViewStackCapacity - 1);
        g_ue_view_current_bucket = g_ue_view_stack[idx];
    } else {
        g_ue_view_current_bucket = 0;
    }
}

int current_ue_view_bucket() {
    return g_ue_view_current_bucket;
}

void on_present() {
    if (!env_enabled()) return;
    state().present_seq.fetch_add(1, std::memory_order_relaxed);
    // Reset the scratch ring each frame: prevents stale descriptor handles
    // from being reused after their source resources are recycled by RDG.
    reset_scratch_for_frame();
}

static void record_creation_impl(ID3D12Resource* res, State::Placement placement) {
    if (!env_enabled() || res == nullptr) return;
    auto& s = state();

    const auto rdesc = res->GetDesc();
    // Diagnostic: count all calls + tex calls to verify the hook is wired.
    static std::atomic<uint64_t> rc_total{0};
    static std::atomic<uint64_t> rc_tex{0};
    const auto rct = rc_total.fetch_add(1, std::memory_order_relaxed) + 1;
    if (rdesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
        const auto rcx = rc_tex.fetch_add(1, std::memory_order_relaxed) + 1;
        if (rcx <= 32 || (rcx % 256) == 0) {
            SPDLOG_WARN("[SN2-EyePairing-Create] tex_create#{} (total_calls={}) dim={}x{}x{} fmt={} flags=0x{:x}",
                rcx, rct,
                static_cast<unsigned>(rdesc.Width),
                static_cast<unsigned>(rdesc.Height),
                static_cast<unsigned>(rdesc.DepthOrArraySize),
                static_cast<unsigned>(rdesc.Format),
                static_cast<unsigned>(rdesc.Flags));
        }
    }
    // Only track textures (skip buffers) — buffers have huge size variance
    // and false-pair too easily. Also skip ordinary SRV-only textures: the
    // twin detector is meant for UE render graph products, not asset textures
    // that happen to share dimensions.
    if (!is_pairable_texture_desc(rdesc)) return;

    // Diagnostic: count creation invocations + buckets.
    static std::atomic<uint64_t> total_creates{0};
    const auto tc = total_creates.fetch_add(1, std::memory_order_relaxed) + 1;
    if (tc == 1 || (tc % 256) == 0) {
        SPDLOG_WARN("[SN2-EyePairing-Create] tex_create#{} dim={}x{}x{} fmt={} flags=0x{:x} (pair_count={})",
                    tc,
                    static_cast<unsigned>(rdesc.Width),
                    static_cast<unsigned>(rdesc.Height),
                    static_cast<unsigned>(rdesc.DepthOrArraySize),
                    static_cast<unsigned>(rdesc.Format),
                    static_cast<unsigned>(rdesc.Flags),
                    pair_count());
    }

    DescKey key = make_desc_key(rdesc);

    const uint64_t cs = s.create_seq.fetch_add(1, std::memory_order_relaxed);
    const uint64_t ps = s.present_seq.load(std::memory_order_relaxed);

    std::scoped_lock _{s.mu};

    // Seed the profile entry so the resource is known to the pair map.
    auto& prof = s.resource_profile[res];
    if (prof.seq == 0) {
        prof.seq = s.seq_counter.fetch_add(1, std::memory_order_relaxed) + 1;
        fill_profile_from_desc(prof, rdesc);
    }

    auto it = s.twin_candidates.find(key);
    if (it == s.twin_candidates.end()) {
        // First creation of this desc → store as twin candidate.
        s.twin_candidates[key] = State::TwinCandidate{res, cs, ps, placement};
        return;
    }

    State::TwinCandidate cand = it->second;
    // If the candidate is too far in the past (different Present frame OR
    // wider than the create-seq window), this new creation is a fresh first
    // half. Replace.
    if (cand.present_seq != ps ||
        cs - cand.create_seq > State::kTwinMaxCreateGap) {
        it->second = State::TwinCandidate{res, cs, ps, placement};
        return;
    }

    // UE5 RDG placed resources can alias the same transient heap range across
    // barrier boundaries. Same desc + same (heap, offset) is sequential reuse,
    // not a left/right twin, so keep the newer owner as the candidate.
    if (cand.placement.valid &&
        placement.valid &&
        cand.placement.heap == placement.heap &&
        cand.placement.offset == placement.offset) {
        static std::atomic<uint64_t> alias_skips{0};
        const auto n = alias_skips.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 16 || (n % 256) == 0) {
            SPDLOG_WARN("[SN2-EyePairing] twin-skip alias #{} resA=0x{:x} resB=0x{:x} heap=0x{:x} offset=0x{:x} ({}x{}x{} fmt={} flags=0x{:x})",
                        n,
                        reinterpret_cast<uintptr_t>(cand.res),
                        reinterpret_cast<uintptr_t>(res),
                        reinterpret_cast<uintptr_t>(placement.heap),
                        placement.offset,
                        static_cast<unsigned>(key.width),
                        static_cast<unsigned>(key.height),
                        static_cast<unsigned>(key.depth_or_array),
                        static_cast<unsigned>(key.format),
                        static_cast<unsigned>(key.flags));
        }
        it->second = State::TwinCandidate{res, cs, ps, placement};
        return;
    }

    // Pair: cand.res = LEFT, res = RIGHT.
    if (cand.res == res) return;  // dedup
    if (s.left_to_right.find(cand.res) != s.left_to_right.end() ||
        s.right_to_left.find(cand.res) != s.right_to_left.end()) {
        // Candidate already paired (shouldn't happen normally) — replace.
        it->second = State::TwinCandidate{res, cs, ps, placement};
        return;
    }
    if (s.right_to_left.find(res) != s.right_to_left.end() ||
        s.left_to_right.find(res) != s.left_to_right.end()) {
        // This new resource already paired as R for something else.
        // Drop candidate, advance.
        it->second = State::TwinCandidate{res, cs, ps, placement};
        return;
    }
    s.left_to_right[cand.res] = res;
    s.right_to_left[res] = cand.res;
    s.twin_candidates.erase(it);

    static std::atomic<uint64_t> twin_pairs{0};
    const auto tp = twin_pairs.fetch_add(1, std::memory_order_relaxed) + 1;
    if (tp <= 64 || log_verbose_enabled() || (tp % 100) == 0) {
        SPDLOG_WARN("[SN2-EyePairing] twin-pair #{} L=0x{:x}→R=0x{:x} ({}x{}x{} fmt={} flags=0x{:x} dim={}) total_pairs={}",
                    tp,
                    reinterpret_cast<uintptr_t>(cand.res),
                    reinterpret_cast<uintptr_t>(res),
                    static_cast<unsigned>(key.width),
                    static_cast<unsigned>(key.height),
                    static_cast<unsigned>(key.depth_or_array),
                    static_cast<unsigned>(key.format),
                    static_cast<unsigned>(key.flags),
                    static_cast<unsigned>(key.dimension),
                    s.left_to_right.size());
    }
}

void record_creation(ID3D12Resource* res) {
    record_creation_impl(res, State::Placement{});
}

void record_placed_creation(ID3D12Resource* res, ID3D12Heap* heap, UINT64 heap_offset) {
    State::Placement placement{};
    placement.heap = heap;
    placement.offset = heap_offset;
    placement.valid = heap != nullptr;
    record_creation_impl(res, placement);
}

void record_aliasing_barrier(int bucket, ID3D12Resource* /*before*/, ID3D12Resource* after) {
    if (!env_enabled()) return;
    if (bucket != 1 && bucket != 2) return;
    if (after == nullptr) return;

    const auto desc = after->GetDesc();
    if (!is_pairable_texture_desc(desc)) return;

    auto& s = state();
    std::scoped_lock _{s.mu};

    auto& prof = s.resource_profile[after];
    const uint64_t seq = s.seq_counter.fetch_add(1, std::memory_order_relaxed) + 1;
    if (prof.seq == 0) {
        prof.seq = seq;
        fill_profile_from_desc(prof, desc);
    }

    prof.buckets |= (bucket == 1) ? 1u : 2u;
    prof.role_seq[bucket] = seq;
    prof.role_present[bucket] = s.present_seq.load(std::memory_order_relaxed);

    static std::atomic<uint64_t> alias_updates{0};
    const auto n = alias_updates.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 32 || log_verbose_enabled() || (n % 600) == 0) {
        SPDLOG_WARN("[SN2-EyePairing][Alias] role#{} bucket={} after=0x{:x} ({}x{}x{} fmt={} flags=0x{:x} dim={})",
                    n,
                    bucket,
                    reinterpret_cast<uintptr_t>(after),
                    static_cast<unsigned>(desc.Width),
                    static_cast<unsigned>(desc.Height),
                    static_cast<unsigned>(desc.DepthOrArraySize),
                    static_cast<unsigned>(desc.Format),
                    static_cast<unsigned>(desc.Flags),
                    static_cast<unsigned>(desc.Dimension));
    }
}

}  // namespace sn2_eye_pairing
