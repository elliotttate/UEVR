// Sn2UweFogMirrorHook.cpp — implementation of mirror-resource alloc + heap mgmt.

#include "Sn2UweFogMirrorHook.hpp"

#include <spdlog/spdlog.h>

namespace sn2_uwe_fog_mirror {

bool ensure_mirror_heap(ID3D12Device* device) {
    if (device == nullptr) return false;
    auto& s = mirror_heap();
    if (s.initialized()) return true;

    static std::mutex init_mu;
    std::scoped_lock _{init_mu};
    if (s.initialized()) return true;

    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 4096;  // 2048 mirrors × 2 desc each; SN2 creates ~600 fog-shaped UAVs over a session
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    desc.NodeMask = 0;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    const HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap));
    if (FAILED(hr)) {
        SPDLOG_WARN("[SN2-UweFogMirror] CreateDescriptorHeap failed hr=0x{:08x}", static_cast<uint32_t>(hr));
        return false;
    }

    s.heap = heap;
    s.descriptor_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    s.capacity = desc.NumDescriptors;
    s.next_index.store(0, std::memory_order_release);

    SPDLOG_WARN("[SN2-UweFogMirror] mirror heap ready capacity={} stride={}",
        s.capacity, s.descriptor_size);
    return true;
}

UINT alloc_two_slots() {
    auto& s = mirror_heap();
    if (!s.initialized()) return UINT32_MAX;
    const UINT base = s.next_index.fetch_add(2, std::memory_order_acq_rel);
    if (base + 2 > s.capacity) {
        SPDLOG_WARN("[SN2-UweFogMirror] descriptor heap exhausted base={} cap={}", base, s.capacity);
        return UINT32_MAX;
    }
    return base;
}

bool ensure_scratch_heap(ID3D12Device* device) {
    if (device == nullptr) return false;
    auto& s = scratch_heap();
    if (s.initialized()) return true;

    static std::mutex init_mu;
    std::scoped_lock _{init_mu};
    if (s.initialized()) return true;

    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 4096;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    const HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap));
    if (FAILED(hr)) {
        SPDLOG_WARN("[SN2-UweFogMirror] CreateDescriptorHeap(scratch) failed hr=0x{:08x}", static_cast<uint32_t>(hr));
        return false;
    }

    s.heap = heap;
    s.descriptor_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    s.capacity = desc.NumDescriptors;
    s.next_index.store(0, std::memory_order_release);

    SPDLOG_WARN("[SN2-UweFogMirror] scratch heap ready capacity={} stride={}",
        s.capacity, s.descriptor_size);
    return true;
}

bool ensure_mirror_rtv_heap(ID3D12Device* device) {
    if (device == nullptr) return false;
    auto& s = mirror_rtv_heap();
    if (s.initialized()) return true;
    static std::mutex init_mu;
    std::scoped_lock _{init_mu};
    if (s.initialized()) return true;
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    desc.NumDescriptors = 1024;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    const HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap));
    if (FAILED(hr)) {
        SPDLOG_WARN("[SN2-UweFogMirror] CreateDescriptorHeap(rtv) failed hr=0x{:08x}",
                    static_cast<uint32_t>(hr));
        return false;
    }
    s.heap = heap;
    s.descriptor_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    s.capacity = desc.NumDescriptors;
    s.next_index.store(0, std::memory_order_release);
    SPDLOG_WARN("[SN2-UweFogMirror] RTV heap ready capacity={} stride={}",
                s.capacity, s.descriptor_size);
    return true;
}

UINT alloc_rtv_slot() {
    auto& s = mirror_rtv_heap();
    if (!s.initialized()) return UINT32_MAX;
    const UINT idx = s.next_index.fetch_add(1, std::memory_order_acq_rel);
    if (idx >= s.capacity) return UINT32_MAX;
    return idx;
}

UINT alloc_scratch_range(UINT count) {
    auto& s = scratch_heap();
    if (!s.initialized() || count == 0 || s.capacity == 0) return UINT32_MAX;
    // Fixed-size ranges, aligned to count boundaries. capacity / count =
    // number of slots in the ring. fetch_add + modulo gives a free ring
    // bucket. Caller must use the SAME count each call (we use 16 always).
    constexpr UINT kRangeSize = 16;
    if (count > kRangeSize) return UINT32_MAX;
    const UINT total_ranges = s.capacity / kRangeSize;
    if (total_ranges == 0) return UINT32_MAX;
    const UINT range_idx = s.next_index.fetch_add(1, std::memory_order_relaxed) % total_ranges;
    return range_idx * kRangeSize;
}

const Mirror* create_mirror_for(
    ID3D12Device* device,
    ID3D12Resource* game_resource,
    const D3D12_RESOURCE_DESC& desc)
{
    if (device == nullptr || game_resource == nullptr) return nullptr;
    if (!ensure_mirror_heap(device)) return nullptr;

    // Idempotent: return existing if already mapped.
    if (const auto* existing = registry().find(game_resource)) {
        return existing;
    }

    // Allocate the parallel resource. Use the same desc.
    // 2026-05-22 Stage V5: initial state = COMMON so D3D12 implicit promotion
    // can lift it to UAV (compute) or RENDER_TARGET (OMSetRenderTargets) on
    // first use without explicit ResourceBarrier. RT-flagged 3D textures
    // crashed in KERNELBASE when bound as RTV from UAV state.
    Mirror m{};
    m.desc = desc;
    m.current_state = D3D12_RESOURCE_STATE_COMMON;

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr;
    {
        // Re-entry guard: device->CreateCommittedResource routes through our
        // own hook. Without this scope, the mirror creation would trigger
        // another candidate detection → another create_mirror_for →
        // infinite recursion → stack overflow.
        CreateMirrorScope guard;
        hr = device->CreateCommittedResource(
            &heap_props,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            m.current_state,
            nullptr,
            IID_PPV_ARGS(&m.resource));
    }
    if (FAILED(hr) || m.resource == nullptr) {
        SPDLOG_WARN("[SN2-UweFogMirror] CreateCommittedResource(mirror) failed hr=0x{:08x} dim={}x{}x{} fmt={}",
            static_cast<uint32_t>(hr),
            static_cast<unsigned>(desc.Width),
            static_cast<unsigned>(desc.Height),
            static_cast<unsigned>(desc.DepthOrArraySize),
            static_cast<unsigned>(desc.Format));
        return nullptr;
    }

    // Allocate UAV + SRV descriptor slots.
    const UINT base_idx = alloc_two_slots();
    if (base_idx == UINT32_MAX) {
        SPDLOG_WARN("[SN2-UweFogMirror] no descriptor slots — heap exhausted");
        return nullptr;
    }

    auto& s = mirror_heap();
    const auto cpu_start = s.heap->GetCPUDescriptorHandleForHeapStart();
    const auto gpu_start = s.heap->GetGPUDescriptorHandleForHeapStart();
    m.uav_cpu.ptr = cpu_start.ptr + static_cast<SIZE_T>(base_idx) * s.descriptor_size;
    m.uav_gpu.ptr = gpu_start.ptr + static_cast<UINT64>(base_idx) * s.descriptor_size;
    m.srv_cpu.ptr = cpu_start.ptr + static_cast<SIZE_T>(base_idx + 1) * s.descriptor_size;
    m.srv_gpu.ptr = gpu_start.ptr + static_cast<UINT64>(base_idx + 1) * s.descriptor_size;

    // Create UAV.
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format = desc.Format;
        if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
            uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
            uav.Texture3D.MipSlice = 0;
            uav.Texture3D.FirstWSlice = 0;
            uav.Texture3D.WSize = desc.DepthOrArraySize;
        } else if (desc.DepthOrArraySize > 1) {
            uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            uav.Texture2DArray.MipSlice = 0;
            uav.Texture2DArray.FirstArraySlice = 0;
            uav.Texture2DArray.ArraySize = desc.DepthOrArraySize;
        } else {
            uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            uav.Texture2D.MipSlice = 0;
        }
        device->CreateUnorderedAccessView(m.resource.Get(), nullptr, &uav, m.uav_cpu);
    }

    // Create SRV.
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = desc.Format;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
            srv.Texture3D.MostDetailedMip = 0;
            srv.Texture3D.MipLevels = desc.MipLevels ? desc.MipLevels : 1;
            srv.Texture3D.ResourceMinLODClamp = 0.0f;
        } else if (desc.DepthOrArraySize > 1) {
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            srv.Texture2DArray.MostDetailedMip = 0;
            srv.Texture2DArray.MipLevels = desc.MipLevels ? desc.MipLevels : 1;
            srv.Texture2DArray.FirstArraySlice = 0;
            srv.Texture2DArray.ArraySize = desc.DepthOrArraySize;
            srv.Texture2DArray.ResourceMinLODClamp = 0.0f;
        } else {
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Texture2D.MostDetailedMip = 0;
            srv.Texture2D.MipLevels = desc.MipLevels ? desc.MipLevels : 1;
            srv.Texture2D.ResourceMinLODClamp = 0.0f;
        }
        device->CreateShaderResourceView(m.resource.Get(), &srv, m.srv_cpu);
    }

    static std::atomic<uint64_t> seq{0};
    m.seq = seq.fetch_add(1, std::memory_order_relaxed) + 1;

    // Register.
    {
        auto& reg = registry();
        std::scoped_lock _{reg.mu};
        reg.map.emplace(game_resource, std::move(m));
    }

    if (env_log_enabled()) {
        SPDLOG_WARN(
            "[SN2-UweFogMirror] created mirror#{} game_ptr=0x{:x} mirror_ptr=0x{:x} "
            "dim={}x{}x{} fmt={} flags=0x{:x} uav_idx={} uav_gpu=0x{:x} srv_gpu=0x{:x}",
            seq.load(std::memory_order_relaxed),
            reinterpret_cast<uintptr_t>(game_resource),
            reinterpret_cast<uintptr_t>(m.resource.Get()),
            static_cast<unsigned>(desc.Width),
            static_cast<unsigned>(desc.Height),
            static_cast<unsigned>(desc.DepthOrArraySize),
            static_cast<unsigned>(desc.Format),
            static_cast<unsigned>(desc.Flags),
            base_idx,
            m.uav_gpu.ptr,
            m.srv_gpu.ptr);
    }

    // Look up the registered Mirror to return a stable pointer.
    return registry().find(game_resource);
}

int find_tracked_uav_slot(UINT64 gpu_table_base, int search_count) {
    if (gpu_table_base == 0 || search_count <= 0) return -1;
    auto& heaps = descriptor_heaps();
    SIZE_T cpu_base_for_table = 0;
    if (!heaps.resolve_gpu_to_cpu(gpu_table_base, cpu_base_for_table)) {
        return -1;
    }
    // Stride is per-heap; assume it's the standard CBV/SRV/UAV stride
    // (returned via Device::GetDescriptorHandleIncrementSize at heap-bind
    // time). We need the same stride here — use the heap's stored value.
    UINT stride = 0;
    {
        std::scoped_lock _{heaps.mu};
        for (const auto& [_h, info] : heaps.map) {
            if (info.cpu_base <= cpu_base_for_table &&
                cpu_base_for_table < info.cpu_base + static_cast<SIZE_T>(info.stride) * info.capacity) {
                stride = info.stride;
                break;
            }
        }
    }
    if (stride == 0) return -1;

    auto& reg = registry();
    auto& bindings = uav_bindings();
    for (int i = 0; i < search_count; ++i) {
        const SIZE_T cpu_ptr = cpu_base_for_table + static_cast<SIZE_T>(i) * stride;
        ID3D12Resource* res = bindings.find(cpu_ptr);
        if (res != nullptr && reg.has(res)) {
            return i;
        }
    }
    return -1;
}

// =====================================================================
// Aliasing-bucket tracker — Approach C.
//
// Bucket placed resources by (heap, offset). When two or more matching-shape
// resources land in the same bucket they're aliased — the engine's render
// graph reuses the same heap memory under different ID3D12Resource* names.
// We allocate ONE shadow committed resource per fog-shaped bucket. SRV reads
// from any bucket member can be redirected to the shadow's SRV at right-eye
// dispatch time.
// =====================================================================

const Mirror* register_placed(
    ID3D12Device* device,
    ID3D12Resource* game_resource,
    ID3D12Heap* heap,
    UINT64 heap_offset,
    const D3D12_RESOURCE_DESC& desc)
{
    if (device == nullptr || game_resource == nullptr || heap == nullptr) return nullptr;
    if (in_mirror_create()) return nullptr;  // avoid recursion when allocating shadow

    AliasBucketKey key{heap, heap_offset};

    // Add to bucket. Outside the mu we may need to allocate the shadow,
    // which itself can recurse into our hooks; do that outside the lock.
    bool need_shadow_alloc = false;
    ID3D12Resource* first_member = nullptr;
    D3D12_RESOURCE_DESC first_desc{};

    {
        auto& reg = bucket_registry();
        std::scoped_lock _{reg.mu};
        auto& bucket = reg.buckets[key];
        if (bucket.members.empty()) {
            bucket.representative_desc = desc;
            bucket.fog_shape = matches_uwe_fog_signature(desc);
            bucket.seq = static_cast<uint64_t>(reg.buckets.size());
        }
        bucket.members.push_back(game_resource);
        reg.resource_to_key[game_resource] = key;

        // Allocate shadow on first fog-shape member. We previously waited for
        // 2+ members (aliasing collision proof) but in practice the dispatch
        // can fire before the 2nd member is created. Eager allocation covers
        // all fog-shaped placed resources up-front.
        if (bucket.fog_shape && bucket.shadow == nullptr && !bucket.members.empty()) {
            need_shadow_alloc = true;
            first_member = bucket.members.front();
            first_desc = bucket.representative_desc;
        }
    }

    if (!need_shadow_alloc) {
        return nullptr;
    }

    // Allocate shadow OUTSIDE the bucket mutex (create_mirror_for can recurse
    // through D3D12 hooks).
    const Mirror* shadow = create_mirror_for(device, first_member, first_desc);
    if (shadow == nullptr) {
        SPDLOG_WARN(
            "[SN2-AliasBucket] shadow alloc FAILED for bucket (heap=0x{:x} offset=0x{:x} dim={}x{}x{} fmt={})",
            reinterpret_cast<uintptr_t>(heap), heap_offset,
            static_cast<unsigned>(first_desc.Width),
            static_cast<unsigned>(first_desc.Height),
            static_cast<unsigned>(first_desc.DepthOrArraySize),
            static_cast<unsigned>(first_desc.Format));
        return nullptr;
    }

    {
        auto& reg = bucket_registry();
        std::scoped_lock _{reg.mu};
        auto it = reg.buckets.find(key);
        if (it != reg.buckets.end()) {
            it->second.shadow = shadow;
            if (env_alias_redirect_log_enabled()) {
                SPDLOG_WARN(
                    "[SN2-AliasBucket] shadow ready bucket#{} heap=0x{:x} offset=0x{:x} members={} "
                    "dim={}x{}x{} fmt={} shadow_srv_gpu=0x{:x}",
                    it->second.seq,
                    reinterpret_cast<uintptr_t>(heap), heap_offset,
                    it->second.members.size(),
                    static_cast<unsigned>(first_desc.Width),
                    static_cast<unsigned>(first_desc.Height),
                    static_cast<unsigned>(first_desc.DepthOrArraySize),
                    static_cast<unsigned>(first_desc.Format),
                    shadow->srv_gpu.ptr);
            }
        }
    }

    return shadow;
}

const Mirror* find_bucket_shadow(ID3D12Resource* game_resource) {
    if (game_resource == nullptr) return nullptr;

    // Fall through 1: bucket-tracked placed resource → shadow
    {
        auto& reg = bucket_registry();
        std::scoped_lock _{reg.mu};
        const auto it = reg.resource_to_key.find(game_resource);
        if (it != reg.resource_to_key.end()) {
            const auto bit = reg.buckets.find(it->second);
            if (bit != reg.buckets.end() && bit->second.shadow != nullptr) {
                return bit->second.shadow;
            }
        }
    }

    // Fall through 2: per-resource mirror (committed path) → its own mirror
    // Each fog-shape committed resource gets a 1:1 mirror; treat that as the
    // shadow for SRV redirect purposes.
    if (const auto* m = registry().find(game_resource)) {
        return m;
    }
    return nullptr;
}

}  // namespace sn2_uwe_fog_mirror
