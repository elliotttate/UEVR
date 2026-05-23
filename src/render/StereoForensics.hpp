#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <d3d12.h>
#include <dxgi1_4.h>

#include "render/D3D12Diagnostics.hpp"

namespace render {

class StereoForensics {
public:
    enum class DescriptorKind : uint8_t {
        Unknown,
        CBV,
        SRV,
        UAV,
        RTV,
        DSV
    };

    static StereoForensics& get();

    StereoForensics();
    ~StereoForensics();

    bool is_enabled() const;
    bool experiments_enabled() const;
    std::filesystem::path session_dir() const;

    void begin_frame(
        ID3D12Device* device,
        IDXGISwapChain3* swapchain,
        ID3D12CommandQueue* queue,
        uint32_t render_width,
        uint32_t render_height,
        uint32_t display_width,
        uint32_t display_height,
        bool proton_swapchain,
        bool framegen_swapchain);

    void record_descriptor_heap_created(
        std::string_view source,
        ID3D12DescriptorHeap* heap,
        const D3D12_DESCRIPTOR_HEAP_DESC* desc,
        UINT descriptor_stride);

    void record_resource_created(
        std::string_view source,
        ID3D12Resource* resource,
        const D3D12_RESOURCE_DESC* desc,
        const D3D12_HEAP_PROPERTIES* heap_props,
        D3D12_HEAP_FLAGS heap_flags,
        D3D12_RESOURCE_STATES initial_state);

    void record_placed_resource_created(
        std::string_view source,
        ID3D12Resource* resource,
        ID3D12Heap* heap,
        uint64_t heap_offset,
        const D3D12_RESOURCE_DESC* desc,
        D3D12_RESOURCE_STATES initial_state);

    void record_cbv_descriptor(
        std::string_view source,
        const D3D12_CONSTANT_BUFFER_VIEW_DESC* desc,
        D3D12_CPU_DESCRIPTOR_HANDLE handle);

    void record_srv_descriptor(
        std::string_view source,
        ID3D12Resource* resource,
        const D3D12_SHADER_RESOURCE_VIEW_DESC* desc,
        D3D12_CPU_DESCRIPTOR_HANDLE handle);

    void record_uav_descriptor(
        std::string_view source,
        ID3D12Resource* resource,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc,
        D3D12_CPU_DESCRIPTOR_HANDLE handle);

    void record_rtv_descriptor(
        std::string_view source,
        ID3D12Resource* resource,
        const D3D12_RENDER_TARGET_VIEW_DESC* desc,
        D3D12_CPU_DESCRIPTOR_HANDLE handle);

    void record_dsv_descriptor(
        std::string_view source,
        ID3D12Resource* resource,
        const D3D12_DEPTH_STENCIL_VIEW_DESC* desc,
        D3D12_CPU_DESCRIPTOR_HANDLE handle);

    void record_descriptor_copy(
        std::string_view source,
        D3D12_CPU_DESCRIPTOR_HANDLE dst,
        D3D12_CPU_DESCRIPTOR_HANDLE src);

    void record_descriptor_heaps_set(
        std::string_view source,
        uintptr_t command_list,
        uint32_t count,
        ID3D12DescriptorHeap* const* heaps);

    void record_render_targets_set(
        std::string_view source,
        uintptr_t command_list,
        uint32_t rtv_count,
        const D3D12_CPU_DESCRIPTOR_HANDLE* rtvs,
        bool single_handle_range,
        uint32_t rtv_stride,
        const D3D12_CPU_DESCRIPTOR_HANDLE* dsv);

    void record_root_bind(
        std::string_view source,
        uintptr_t command_list,
        uintptr_t pipeline_state,
        int32_t eye_bucket,
        bool graphics,
        std::string_view kind,
        uint32_t root_parameter,
        uintptr_t value,
        uint32_t value_count,
        uint64_t value_hash);

    void record_pso_bind(
        std::string_view source,
        uintptr_t command_list,
        uintptr_t requested_pipeline_state,
        uintptr_t bound_pipeline_state,
        uintptr_t graphics_root_signature,
        uintptr_t compute_root_signature,
        int32_t eye_bucket);

    void record_resource_barriers(
        std::string_view source,
        uintptr_t command_list,
        uint32_t count,
        const D3D12_RESOURCE_BARRIER* barriers);

    void record_rtv_clear(
        std::string_view source,
        uintptr_t command_list,
        uintptr_t pipeline_state,
        int32_t eye_bucket,
        D3D12_CPU_DESCRIPTOR_HANDLE rtv,
        const FLOAT color_rgba[4],
        uint32_t rect_count);

    void record_resource_copy(
        std::string_view source,
        std::string_view kind,
        uintptr_t command_list,
        uintptr_t dst_resource,
        uintptr_t src_resource,
        uint32_t dst_subresource,
        uint32_t src_subresource,
        uint64_t byte_count,
        uint32_t width,
        uint32_t height,
        uint32_t depth,
        uint64_t dst_byte_offset,
        uint64_t src_byte_offset);

    void record_draw_or_dispatch(
        std::string_view source,
        std::string_view kind,
        uintptr_t command_list,
        uintptr_t pipeline_state,
        uintptr_t root_signature,
        int32_t eye_bucket,
        bool executed,
        bool has_viewport,
        float viewport_top_left_x,
        float viewport_top_left_y,
        float viewport_width,
        float viewport_height,
        uint32_t viewport_count,
        bool has_scissor,
        int32_t scissor_left,
        int32_t scissor_top,
        int32_t scissor_right,
        int32_t scissor_bottom,
        uint32_t scissor_count,
        uint32_t arg0,
        uint32_t arg1,
        uint32_t arg2,
        int32_t arg3,
        uint32_t arg4,
        const D3D12Diagnostics::RootSlotArray& graphics_root_descriptor_tables,
        const D3D12Diagnostics::RootSlotArray& compute_root_descriptor_tables,
        const D3D12Diagnostics::RootSlotArray& graphics_root_cbvs,
        const D3D12Diagnostics::RootSlotArray& compute_root_cbvs,
        const D3D12Diagnostics::RootSlotArray& graphics_root_srvs,
        const D3D12Diagnostics::RootSlotArray& compute_root_srvs,
        const D3D12Diagnostics::RootSlotArray& graphics_root_uavs,
        const D3D12Diagnostics::RootSlotArray& compute_root_uavs,
        const D3D12Diagnostics::RootHashArray& graphics_root_cbv_hash,
        const D3D12Diagnostics::RootHashArray& compute_root_cbv_hash,
        const D3D12Diagnostics::RootHashArray& graphics_root_constants_hash,
        const D3D12Diagnostics::RootHashArray& compute_root_constants_hash,
        const D3D12Diagnostics::RootHashArray& graphics_root_descriptor_table_resource_hash,
        const D3D12Diagnostics::RootHashArray& compute_root_descriptor_table_resource_hash,
        const std::vector<D3D12Diagnostics::DescriptorReadInfo>& descriptor_reads);

    bool should_skip_event(
        std::string_view kind,
        uint32_t ps_crc,
        uint32_t cs_crc,
        int32_t eye_bucket);

    void record_experiment_observation(
        std::string_view name,
        std::string_view action,
        std::string_view outcome,
        std::string_view kind,
        uint32_t ps_crc,
        uint32_t cs_crc,
        int32_t eye_bucket);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace render
