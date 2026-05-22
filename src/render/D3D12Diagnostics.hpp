#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <d3d12.h>
#include <dxgi1_4.h>

namespace render {
class D3D12Diagnostics {
public:
    static constexpr size_t MAX_ROOT_BIND_SLOTS = 32;
    using RootSlotArray = std::array<uintptr_t, MAX_ROOT_BIND_SLOTS>;
    using RootHashArray = std::array<uint64_t, MAX_ROOT_BIND_SLOTS>;

    struct HeapInfo {
        uintptr_t pointer{};
        std::string name{};
        std::string source{};
        std::string type{};
        uint32_t total_descriptors{};
        uint32_t estimated_in_use{};
        uint64_t first_seen_frame{};
        uint64_t last_seen_frame{};
        uint64_t bind_count{};
        bool shader_visible{};
        bool transient{};
        bool is_active{};
    };

    struct BarrierEvent {
        uint64_t frame{};
        std::string source{};
        uintptr_t resource{};
        std::string type{};
        std::string before_state{};
        std::string after_state{};
        uint32_t subresource{};
        std::string note{};
    };

    struct BoundTargetInfo {
        uintptr_t handle{};
        uintptr_t resource{};
        std::string name{};
        std::string descriptor_type{};
    };

    struct RootDescriptorRangeInfo {
        std::string type{};
        uint32_t base_shader_register{};
        uint32_t num_descriptors{};
        uint32_t register_space{};
        uint32_t offset_from_table_start{};
    };

    struct RootParameterInfo {
        uint32_t index{};
        std::string parameter_type{};
        std::string visibility{};
        uint32_t shader_register{};
        uint32_t register_space{};
        uint32_t num_32bit_values{};
        std::vector<RootDescriptorRangeInfo> ranges{};
    };

    struct RootSignatureInfo {
        uintptr_t pointer{};
        uint64_t first_seen_frame{};
        uint64_t last_seen_frame{};
        uint32_t blob_size{};
        std::string version{};
        std::string flags{};
        uint32_t static_sampler_count{};
        std::vector<RootParameterInfo> parameters{};
        std::string decode_error{};
    };

    struct DescriptorReadInfo {
        uint32_t root_parameter{};
        uint32_t descriptor_index{};
        uintptr_t descriptor_cpu{};
        uintptr_t descriptor_source_cpu{};
        uint64_t descriptor_source_frame{};
        uintptr_t resource{};
        std::string descriptor_type{};
        uint64_t producer_frame{};
        uint64_t producer_draw{};
        uintptr_t producer_pso{};
        uintptr_t producer_command_list{};
        std::string producer_kind{};
        uintptr_t producer_descriptor{};
        uint32_t producer_target_index{};
        int32_t producer_eye_bucket{-1};
    };

    struct ResourceProducerSnapshot {
        uint64_t frame{};
        uint64_t draw_index{};
        uintptr_t pipeline_state{};
        uintptr_t command_list{};
        std::string kind{};
        uintptr_t descriptor{};
        uint32_t target_index{};
        int32_t eye_bucket{-1};
        std::string name{};
    };

    struct BindingEvent {
        uint64_t frame{};
        std::string source{};
        std::string kind{};
        std::string detail{};
        // Structured per-bind info. Populated for OMSetRenderTargets events;
        // empty for SetDescriptorHeaps / other event kinds.
        std::vector<BoundTargetInfo> render_targets{};
        std::optional<BoundTargetInfo> depth_target{};
    };

    struct WarningEvent {
        uint64_t frame{};
        std::string source{};
        std::string message{};
    };

    struct PipelineCacheEvent {
        uint64_t frame{};
        std::string source{};
        std::string action{};
        uintptr_t device{};
        uintptr_t library{};
        uintptr_t pipeline_state{};
        std::string name{};
        uint64_t cached_blob_size{};
        bool has_cached_pso{};
        bool stripped_cached_pso{};
        uint32_t result{};
        std::string note{};
    };

    struct RootBindEvent {
        uint64_t frame{};
        uint64_t sequence{};
        std::string source{};
        std::string pipeline{};
        std::string kind{};
        uintptr_t command_list{};
        uintptr_t pipeline_state{};
        int32_t eye_bucket{-1};
        uint32_t root_parameter{};
        uintptr_t value{};
        uint32_t value_count{};
        uint64_t value_hash{};
    };

    struct ResourceWriteInfo {
        uint32_t target_index{};
        uintptr_t descriptor{};
        uintptr_t resource{};
        std::string name{};
        std::string kind{};
        uint64_t prior_producer_frame{};
        uint64_t prior_producer_draw{};
        uintptr_t prior_producer_pso{};
        uintptr_t prior_producer_command_list{};
        std::string prior_producer_kind{};
        uintptr_t prior_producer_descriptor{};
        uint32_t prior_producer_target_index{};
        int32_t prior_producer_eye_bucket{-1};
    };

    struct DrawEvent {
        uint64_t frame{};
        uint64_t draw_index{};
        std::string source{};
        std::string kind{};
        uintptr_t command_list{};
        uintptr_t pipeline_state{};
        uintptr_t root_signature{};
        int32_t eye_bucket{-1};
        bool executed{true};
        bool has_viewport{};
        float viewport_top_left_x{};
        float viewport_top_left_y{};
        float viewport_width{};
        float viewport_height{};
        uint32_t viewport_count{};
        bool has_scissor{};
        int32_t scissor_left{};
        int32_t scissor_top{};
        int32_t scissor_right{};
        int32_t scissor_bottom{};
        uint32_t scissor_count{};
        uint32_t arg0{};
        uint32_t arg1{};
        uint32_t arg2{};
        int32_t arg3{};
        uint32_t arg4{};
        // For copy_buffer_region the byte offsets are 64-bit and don't fit in
        // the subresource-sized arg0/arg1 slots; they're recorded here instead.
        // 0 for non-buffer copies.
        uint64_t copy_dst_byte_offset{};
        uint64_t copy_src_byte_offset{};
        uint64_t copy_byte_count{};
        uintptr_t rtv0{};
        uintptr_t rtv0_resource{};
        uint64_t prior_rtv0_producer_frame{};
        uint64_t prior_rtv0_producer_draw{};
        uintptr_t prior_rtv0_producer_pso{};
        std::vector<ResourceWriteInfo> render_target_writes{};
        std::vector<ResourceWriteInfo> uav_writes{};
        RootSlotArray graphics_root_descriptor_tables{};
        RootSlotArray compute_root_descriptor_tables{};
        RootSlotArray graphics_root_cbvs{};
        RootSlotArray compute_root_cbvs{};
        RootSlotArray graphics_root_srvs{};
        RootSlotArray compute_root_srvs{};
        RootSlotArray graphics_root_uavs{};
        RootSlotArray compute_root_uavs{};
        RootHashArray graphics_root_cbv_hash{};
        RootHashArray compute_root_cbv_hash{};
        RootHashArray graphics_root_constants_hash{};
        RootHashArray compute_root_constants_hash{};
        RootHashArray graphics_root_descriptor_table_resource_hash{};
        RootHashArray compute_root_descriptor_table_resource_hash{};
        std::vector<DescriptorReadInfo> descriptor_reads{};
    };

    struct GpuTimingInfo {
        uintptr_t pipeline_state{};
        std::string kind{};
        int32_t eye_bucket{-1};
        uint64_t samples{};
        double avg_ms{};
        double max_ms{};
        uint64_t last_frame{};
    };

    struct CurrentBindContext {
        uint64_t frame{};
        std::string source{};
        std::vector<BoundTargetInfo> render_targets{};
        std::optional<BoundTargetInfo> depth_target{};
        bool exact_this_frame{};
    };

    struct Snapshot {
        bool available{};
        uint64_t frame{};
        uintptr_t device{};
        uintptr_t swapchain{};
        uintptr_t command_queue{};
        uint32_t render_width{};
        uint32_t render_height{};
        uint32_t display_width{};
        uint32_t display_height{};
        bool proton_swapchain{};
        bool framegen_swapchain{};
        uintptr_t active_cbv_srv_uav_heap{};
        uintptr_t active_sampler_heap{};
        uint32_t descriptor_heap_sets_this_frame{};
        uint32_t descriptor_heap_switches_this_frame{};
        uint32_t resource_barriers_this_frame{};
        uint32_t rtv_binds_this_frame{};
        uint32_t root_binds_this_frame{};
        uint32_t draw_events_this_frame{};
        uint32_t transient_heap_creations_this_frame{};
        uint32_t transient_resource_creations_this_frame{};
        uint64_t transient_resource_bytes_this_frame{};
        uint64_t tracked_resource_bytes_total{};
        uint64_t tracked_transient_resource_bytes_total{};
        std::optional<CurrentBindContext> current_bind_context{};
        std::vector<HeapInfo> heaps{};
        std::vector<RootSignatureInfo> root_signatures{};
        std::vector<BindingEvent> recent_bindings{};
        std::vector<RootBindEvent> recent_root_binds{};
        std::vector<DrawEvent> recent_draw_events{};
        std::vector<GpuTimingInfo> gpu_timings{};
        std::vector<PipelineCacheEvent> recent_pipeline_cache_events{};
        std::vector<BarrierEvent> recent_barriers{};
        std::vector<WarningEvent> recent_warnings{};
    };

    static D3D12Diagnostics& get();

    void set_enabled(bool enabled);
    bool is_enabled() const;

    void begin_frame(
        ID3D12Device* device,
        IDXGISwapChain3* swapchain,
        ID3D12CommandQueue* queue,
        uint32_t render_width,
        uint32_t render_height,
        uint32_t display_width,
        uint32_t display_height,
        bool proton_swapchain,
        bool framegen_swapchain
    );

    void register_descriptor_heap(
        std::string_view source,
        ID3D12DescriptorHeap* heap,
        uint32_t estimated_in_use = 0,
        bool transient = false,
        std::string_view name = {}
    );

    void register_resource(
        std::string_view source,
        ID3D12Resource* resource,
        bool transient = false,
        std::string_view name = {}
    );

    void register_rtv_descriptor(
        std::string_view source,
        ID3D12Resource* resource,
        D3D12_CPU_DESCRIPTOR_HANDLE handle,
        std::string_view name = {}
    );

    void register_dsv_descriptor(
        std::string_view source,
        ID3D12Resource* resource,
        D3D12_CPU_DESCRIPTOR_HANDLE handle,
        std::string_view name = {}
    );

    void register_srv_descriptor(
        std::string_view source,
        ID3D12Resource* resource,
        D3D12_CPU_DESCRIPTOR_HANDLE handle,
        std::string_view name = {}
    );

    void register_uav_descriptor(
        std::string_view source,
        ID3D12Resource* resource,
        D3D12_CPU_DESCRIPTOR_HANDLE handle,
        std::string_view name = {}
    );

    void record_descriptor_copy(
        std::string_view source,
        D3D12_CPU_DESCRIPTOR_HANDLE dst,
        D3D12_CPU_DESCRIPTOR_HANDLE src
    );

    void register_root_signature(
        std::string_view source,
        ID3D12RootSignature* root_signature,
        const void* blob,
        size_t blob_size
    );

    void register_pipeline_root_signature(
        std::string_view source,
        ID3D12PipelineState* pipeline_state,
        ID3D12RootSignature* root_signature
    );

    // Lockless-friendly accessors used by Sn2RootSigDumpHook to export a
    // per-PSO root-signature JSON for the dup-config loader. Both maps are
    // populated unconditionally (do not gate on is_enabled()), so callers
    // get the full set even when global diagnostics is off.
    std::vector<RootSignatureInfo> snapshot_root_signatures() const;
    std::vector<std::pair<uintptr_t, uintptr_t>> snapshot_pso_root_signature_pairs() const;

    void record_descriptor_heaps_set(
        std::string_view source,
        uint32_t count,
        ID3D12DescriptorHeap* const* heaps
    );

    void record_resource_barriers(
        std::string_view source,
        uint32_t count,
        const D3D12_RESOURCE_BARRIER* barriers
    );

    void record_rtv_write(
        std::string_view source,
        uintptr_t command_list,
        uintptr_t pipeline_state,
        int32_t eye_bucket,
        D3D12_CPU_DESCRIPTOR_HANDLE rtv,
        std::string_view kind
    );

    void record_rtv_bind(
        std::string_view source,
        uint32_t rtv_count,
        const D3D12_CPU_DESCRIPTOR_HANDLE* rtvs,
        const D3D12_CPU_DESCRIPTOR_HANDLE* dsv
    );

    void record_root_bind(
        std::string_view source,
        uintptr_t command_list,
        uintptr_t pipeline_state,
        int32_t eye_bucket,
        std::string_view pipeline,
        std::string_view kind,
        uint32_t root_parameter,
        uintptr_t value,
        uint32_t value_count = 0,
        uint64_t value_hash = 0
    );

    void record_draw_event(
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
        uintptr_t rtv0,
        const RootSlotArray& graphics_root_descriptor_tables,
        const RootSlotArray& compute_root_descriptor_tables,
        const RootSlotArray& graphics_root_cbvs,
        const RootSlotArray& compute_root_cbvs,
        const RootSlotArray& graphics_root_srvs,
        const RootSlotArray& compute_root_srvs,
        const RootSlotArray& graphics_root_uavs,
        const RootSlotArray& compute_root_uavs,
        const RootHashArray& graphics_root_cbv_hash,
        const RootHashArray& compute_root_cbv_hash,
        const RootHashArray& graphics_root_constants_hash,
        const RootHashArray& compute_root_constants_hash,
        const RootHashArray& graphics_root_descriptor_table_resource_hash,
        const RootHashArray& compute_root_descriptor_table_resource_hash,
        const std::vector<DescriptorReadInfo>& descriptor_reads
    );

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
        uint64_t dst_byte_offset = 0,
        uint64_t src_byte_offset = 0
    );

    void record_extra_descriptor_read(
        std::string_view source,
        uintptr_t command_list,
        uintptr_t pipeline_state,
        DescriptorReadInfo read
    );

    std::optional<DescriptorReadInfo> resolve_descriptor_read(
        uint32_t root_parameter,
        uint32_t descriptor_index,
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor
    ) const;

    std::optional<ResourceProducerSnapshot> last_resource_producer(uintptr_t resource) const;

    void record_gpu_timing_sample(
        std::string_view source,
        uintptr_t pipeline_state,
        int32_t eye_bucket,
        double milliseconds
    );

    void record_pipeline_cache_event(
        std::string_view source,
        std::string_view action,
        uintptr_t device,
        uintptr_t library,
        uintptr_t pipeline_state,
        std::string_view name,
        uint64_t cached_blob_size,
        bool has_cached_pso,
        bool stripped_cached_pso,
        uint32_t result,
        std::string_view note = {}
    );

    Snapshot snapshot() const;
    std::optional<CurrentBindContext> current_bind_context() const;
    std::optional<RootSignatureInfo> root_signature_for_pipeline(uintptr_t pipeline_state) const;
    std::optional<RootSignatureInfo> root_signature(uintptr_t root_signature) const;
    void reset();

private:
    struct ResourceInfo {
        uintptr_t pointer{};
        std::string name{};
        std::string source{};
        std::string format{};
        uint32_t width{};
        uint32_t height{};
        uint64_t approx_bytes{};
        uint64_t first_seen_frame{};
        uint64_t last_seen_frame{};
        bool transient{};
    };

    struct DescriptorInfo {
        uintptr_t handle{};
        uintptr_t source_handle{};
        uint64_t source_frame{};
        uintptr_t resource{};
        std::string name{};
        std::string source{};
        std::string descriptor_type{};
        uint64_t first_seen_frame{};
        uint64_t last_seen_frame{};
    };

    struct ResourceProducerInfo {
        uint64_t frame{};
        uint64_t draw_index{};
        uintptr_t pipeline_state{};
        uintptr_t command_list{};
        std::string kind{};
        uintptr_t descriptor{};
        uint32_t target_index{};
        int32_t eye_bucket{-1};
        std::string name{};
    };

    struct GpuTimingAggregate {
        uintptr_t pipeline_state{};
        std::string kind{};
        int32_t eye_bucket{-1};
        uint64_t samples{};
        double total_ms{};
        double max_ms{};
        uint64_t last_frame{};
    };

    void push_warning(std::string_view source, std::string message);
    void note_frame_warning_if_needed();
    void clear_state_locked();

    std::atomic_bool m_enabled{false};
    mutable std::recursive_mutex m_mutex{};
    std::unordered_map<uintptr_t, HeapInfo> m_heaps{};
    std::unordered_map<uintptr_t, ResourceInfo> m_resources{};
    std::unordered_map<uintptr_t, DescriptorInfo> m_rtv_descriptors{};
    std::unordered_map<uintptr_t, DescriptorInfo> m_dsv_descriptors{};
    std::unordered_map<uintptr_t, DescriptorInfo> m_srv_descriptors{};
    std::unordered_map<uintptr_t, DescriptorInfo> m_uav_descriptors{};
    std::unordered_map<uintptr_t, RootSignatureInfo> m_root_signatures{};
    std::unordered_map<uintptr_t, uintptr_t> m_pso_root_signatures{};
    std::unordered_map<uintptr_t, ResourceProducerInfo> m_last_resource_writes{};
    std::unordered_map<std::string, GpuTimingAggregate> m_gpu_timings{};
    std::vector<BindingEvent> m_recent_bindings{};
    std::vector<RootBindEvent> m_recent_root_binds{};
    std::vector<DrawEvent> m_recent_draw_events{};
    std::vector<PipelineCacheEvent> m_recent_pipeline_cache_events{};
    std::vector<BarrierEvent> m_recent_barriers{};
    std::vector<WarningEvent> m_recent_warnings{};
    std::optional<CurrentBindContext> m_current_bind_context{};

    uint64_t m_frame{};
    uintptr_t m_device{};
    uintptr_t m_swapchain{};
    uintptr_t m_command_queue{};
    uint32_t m_render_width{};
    uint32_t m_render_height{};
    uint32_t m_display_width{};
    uint32_t m_display_height{};
    bool m_proton_swapchain{};
    bool m_framegen_swapchain{};
    uintptr_t m_active_cbv_srv_uav_heap{};
    uintptr_t m_active_sampler_heap{};
    uint32_t m_descriptor_heap_sets_this_frame{};
    uint32_t m_descriptor_heap_switches_this_frame{};
    uint32_t m_resource_barriers_this_frame{};
    uint32_t m_rtv_binds_this_frame{};
    uint32_t m_root_binds_this_frame{};
    uint32_t m_draw_events_this_frame{};
    uint64_t m_root_bind_sequence{};
    uint32_t m_transient_heap_creations_this_frame{};
    uint32_t m_transient_resource_creations_this_frame{};
    uint64_t m_transient_resource_bytes_this_frame{};
};
} // namespace render
