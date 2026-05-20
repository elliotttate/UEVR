#pragma once

#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi")

#include <d3d12.h>
#include <dxgi1_4.h>

#include "utility/PointerHook.hpp"
#include "utility/VtableHook.hpp"

// 2026-05-17 SN2 FOG-FIX: cross-TU accessor for the GPU VA → SRV info map
// populated in D3D12Hook::create_shader_resource_view. Used by the SLW per-
// view hook in FFakeStereoRenderingHook to look up view 1's fog volume's
// SRV CPU descriptor handle by the GPU VA we read from its FViewInfo wrapper.
namespace sn2_fog_srv_map {
    struct Entry {
        ID3D12Resource* resource;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
        UINT64 gpu_va;
        UINT64 last_seen_tick;
        int view_id; // -1 unknown, 0 view0 (left), 1 view1 (right)
        uint64_t creation_index; // 0-based creation order
    };
    bool lookup_by_gpu_va(UINT64 gpu_va, Entry& out);
    bool lookup_by_resource(ID3D12Resource* resource, Entry& out);
    // Returns the most-recently-recorded Entry tagged with the given view_id.
    bool lookup_first_by_view_id(int view_id, Entry& out);
    // Returns the Entry created at the given 0-based index in creation order.
    bool lookup_by_creation_index(uint64_t index, Entry& out);
    // Sets view_id on the entry for the given resource (used for backfill).
    void tag_resource_view(ID3D12Resource* resource, int view_id);
    size_t size();
    size_t count_by_view(int view_id);
    bool tag_by_cpu_handle(D3D12_CPU_DESCRIPTOR_HANDLE cpu, int view_id);
    bool lookup_by_cpu_handle(D3D12_CPU_DESCRIPTOR_HANDLE cpu, Entry& out);
}

// 2026-05-17 SN2 FOG-FIX (Task #43): UAV→view_id tagging map. Populated by
// D3D12Hook::create_unordered_access_view when a UAV is created for a Texture3D
// during a per-view fog compute dispatcher scope (view tag taken from the
// thread-atomic sn2_get_current_fog_view()). The recorded ID3D12Resource* is
// the actual fog volume each view's compute writes into. Use these to find
// view 1's compute-output resource, then look up its SRV cpu_handle via
// sn2_fog_srv_map::lookup_by_resource to drive CopyDescriptorsSimple at the
// view 1 SLW basepass binding slot.
namespace sn2_fog_uav_map {
    struct Entry {
        ID3D12Resource* resource;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
        int view_id;          // 0 view0, 1 view1, -1 unknown
        uint64_t creation_index;
        UINT64 last_seen_tick;
    };
    void record(ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, int view_id);
    bool lookup_by_resource(ID3D12Resource* resource, Entry& out);
    bool lookup_first_by_view_id(int view_id, Entry& out);
    bool lookup_last_by_view_id(int view_id, Entry& out);
    size_t size();
    size_t count_by_view(int view_id);
    bool tag_by_cpu_handle(D3D12_CPU_DESCRIPTOR_HANDLE cpu, int view_id);
    bool lookup_by_cpu_handle(D3D12_CPU_DESCRIPTOR_HANDLE cpu, Entry& out);
    // Tag the entry whose resource pointer matches. Linear scan. Preserves
    // an existing positive view_id (won't overwrite 0/1 with -1).
    bool tag_resource_view(ID3D12Resource* resource, int view_id);
}

// 2026-05-17 SN2 FOG-FIX Task #46: maps CopyDescriptorsSimple destinations
// (bindless heap CPU handles) to their staging-heap sources. UE5 builds
// SRVs/UAVs in non-shader-visible CPU staging heaps then copies them into
// the shader-visible bindless heap each frame; without this map we can't
// connect view 1's fog UAV (staging cpu_handle) to the bindless slot the
// GPU shader actually reads from.
namespace sn2_bindless_slot_map {
    struct Entry {
        D3D12_CPU_DESCRIPTOR_HANDLE bindless_cpu_handle;
        D3D12_CPU_DESCRIPTOR_HANDLE src_cpu_handle;
        ID3D12Resource* src_resource;
        int src_view_id;
        UINT64 last_seen_tick;
    };
    void record(D3D12_CPU_DESCRIPTOR_HANDLE bindless_cpu, D3D12_CPU_DESCRIPTOR_HANDLE src_cpu);
    bool lookup(D3D12_CPU_DESCRIPTOR_HANDLE bindless_cpu, Entry& out);
    // Find a bindless slot whose source is a UAV/SRV recorded for view_id.
    // returns the most-recent matching slot.
    bool find_slot_for_view(int view_id, D3D12_CPU_DESCRIPTOR_HANDLE& bindless_cpu_out, ID3D12Resource** resource_out);
    size_t size();
    size_t count_by_view(int view_id);
    // Iterate every entry whose bindless cpu_handle falls inside the
    // half-open range [cpu_lo, cpu_hi), calling visitor(entry). Used by the
    // SetComputeRootDescriptorTable retro-tagger to walk only the relevant
    // slice of bindless slots without iterating millions of empty slots.
    void for_each_in_range(SIZE_T cpu_lo, SIZE_T cpu_hi, void (*visitor)(const Entry& e, void* ctx), void* ctx);
    // Bulk-tag the source resource for every entry whose bindless cpu_handle
    // is in [cpu_lo, cpu_hi). Returns the number of entries whose source got
    // newly tagged (view_id was -1 → view_id_out).
    size_t bulk_tag_sources_in_range(SIZE_T cpu_lo, SIZE_T cpu_hi, int view_id);
    // Re-resolve src_view_id + src_resource for entries whose source was
    // untagged at record time. Returns the number of slot entries that
    // newly got a positive view_id.
    size_t resolve_pending_tags();
}

// 2026-05-17 SN2 fog Path A — UEVR-owned upload buffer that replaces SN2's
// shared FFogUniformParameters cbuffer for right-eye basepass draws. See
// project_sn2_fog_path_a_cbuffer_redirect_plan_2026-05-17 memory.
//
// Buffer lifecycle: lazily created on first right-eye binding via
// `ensure_buffer(device)` inside `D3D12Hook::set_graphics_root_constant_buffer_view`.
// Stays mapped and alive for process lifetime (256 bytes, UPLOAD heap, GENERIC_READ).
//
// Cross-TU population: `subnautica2_setup_fog_uniform_params_hook` calls
// `try_write_view1_cbuffer(...)` once per frame, after FixV5 finishes
// patching SN2's CPU-side OutParameters. This pushes view-1-correct content
// (or v0-fallback if view 1's data is null) into our buffer BEFORE the
// right-eye basepass executes on the GPU.
namespace sn2_fog_path_a {
    // Returns true if UEVR_SUBNAUTICA2_FOG_PATH_A=1 is set in the env.
    bool enabled_env();
    // Returns true once the device-allocated buffer exists and is mapped.
    bool buffer_ready();
    // Copy `size` bytes from `data` into our mapped upload buffer. No-op
    // until the redirect hook has lazily created the buffer.
    void write_buffer(const void* data, size_t size);
    // Arm the next N matching cbuffer bindings for redirect. Called from
    // FixV5 hook (FFakeStereoRenderingHook) after view 1's setup completes,
    // so the immediately-following basepass binding of the shared fog
    // cbuffer GPU_VA gets redirected to UEVR's buffer.
    void arm_redirect(int count);
}

namespace d3d12_stereo_trace {
struct CountersSnapshot {
    uint64_t viewport_unknown{}, viewport_left{}, viewport_right{}, viewport_full{}, viewport_multi{};
    uint64_t draw_unknown{},     draw_left{},     draw_right{},     draw_full{},     draw_multi{};
    uint64_t draw_indexed_unknown{}, draw_indexed_left{}, draw_indexed_right{}, draw_indexed_full{}, draw_indexed_multi{};
    uint64_t clear_unknown{},    clear_left{},    clear_right{},    clear_full{},    clear_multi{};
    uint64_t om_set_render_targets{};
    uint64_t resource_barriers{};
};
void set_ffi_enabled(bool enabled);
bool is_ffi_enabled();
CountersSnapshot peek();
void reset();
} // namespace d3d12_stereo_trace

class D3D12Hook
{
public:
	typedef std::function<void(D3D12Hook&)> OnPresentFn;
	typedef std::function<void(D3D12Hook&, uint32_t w, uint32_t h)> OnResizeBuffersFn;
    typedef std::function<void(D3D12Hook&, uint32_t w, uint32_t h)> OnResizeTargetFn;
    typedef std::function<void(D3D12Hook&)> OnCreateSwapChainFn;

	D3D12Hook() = default;
	virtual ~D3D12Hook();

	bool hook();
	bool unhook();

    bool is_hooked() {
        return m_hooked;
    }

    void on_present(OnPresentFn fn) {
        m_on_present = fn;
    }

    void on_post_present(OnPresentFn fn) {
        m_on_post_present = fn;
    }

    void on_resize_buffers(OnResizeBuffersFn fn) {
        m_on_resize_buffers = fn;
    }

    void on_resize_target(OnResizeTargetFn fn) {
        m_on_resize_target = fn;
    }

    /*void on_create_swap_chain(OnCreateSwapChainFn fn) {
        m_on_create_swap_chain = fn;
    }*/

    ID3D12Device4* get_device() const {
        return m_device;
    }

    IDXGISwapChain3* get_swap_chain() const {
        return m_swap_chain;
    }

    auto get_swapchain_0() { return m_swapchain_0; }
    auto get_swapchain_1() { return m_swapchain_1; }

    ID3D12CommandQueue* get_command_queue() const {
        return m_command_queue;
    }

    UINT get_display_width() const {
        return m_display_width;
    }

    UINT get_display_height() const {
        return m_display_height;
    }

    UINT get_render_width() const {
        return m_render_width;
    }

    UINT get_render_height() const {
        return m_render_height;
    }

    bool is_inside_present() const {
        return m_inside_present;
    }

    bool is_proton_swapchain() const {
        return m_using_proton_swapchain;
    }

    bool is_framegen_swapchain() const {
        return m_using_frame_generation_swapchain;
    }

    void ignore_next_present() {
        m_ignore_next_present = true;
    }

    void set_next_present_interval(uint32_t interval) {
        m_next_present_interval = interval;
    }

protected:
    ID3D12Device4* m_device{ nullptr };
    IDXGISwapChain3* m_swap_chain{ nullptr };
    IDXGISwapChain3* m_swapchain_0{};
    IDXGISwapChain3* m_swapchain_1{};
    ID3D12CommandQueue* m_command_queue{ nullptr };
    UINT m_display_width{ NULL };
    UINT m_display_height{ NULL };
    UINT m_render_width{ NULL };
    UINT m_render_height{ NULL };

    uint32_t m_command_queue_offset{};
    uint32_t m_proton_swapchain_offset{};

    std::optional<uint32_t> m_next_present_interval{};

    bool m_using_proton_swapchain{ false };
    bool m_using_frame_generation_swapchain{ false };
    bool m_skip_dummy_swapchain_type_info_probe{ false };
    bool m_hooked{ false };
    bool m_is_phase_1{ true };
    bool m_inside_present{false};
    bool m_ignore_next_present{false};
    std::unordered_set<uintptr_t> m_swapchains_requiring_original_present_params{};
    std::unordered_set<uintptr_t> m_original_present_param_skip_logged_swapchains{};

    std::unique_ptr<PointerHook> m_present_hook{};
    std::unique_ptr<PointerHook> m_present1_hook{};
    std::vector<std::unique_ptr<PointerHook>> m_create_graphics_pipeline_state_hooks{};
    std::vector<std::unique_ptr<PointerHook>> m_create_compute_pipeline_state_hooks{};
    std::vector<std::unique_ptr<PointerHook>> m_create_command_list_hooks{};
    std::vector<std::unique_ptr<PointerHook>> m_create_command_list1_hooks{};
    std::vector<std::unique_ptr<PointerHook>> m_create_command_signature_hooks{};
    std::vector<std::unique_ptr<PointerHook>> m_create_pipeline_state_hooks{};
    std::vector<std::unique_ptr<PointerHook>> m_create_root_signature_hooks{};
    std::vector<std::unique_ptr<PointerHook>> m_create_constant_buffer_view_hooks{};
    std::vector<std::unique_ptr<PointerHook>> m_create_render_target_view_hooks{};
    std::vector<std::unique_ptr<PointerHook>> m_create_depth_stencil_view_hooks{};
    std::vector<std::unique_ptr<PointerHook>> m_create_shader_resource_view_hooks{};
    std::vector<std::unique_ptr<PointerHook>> m_create_unordered_access_view_hooks{};
    std::vector<std::unique_ptr<PointerHook>> m_copy_descriptors_simple_hooks{};
    std::vector<std::unique_ptr<PointerHook>> m_copy_descriptors_hooks{};
    std::vector<std::unique_ptr<PointerHook>> m_set_pipeline_state_hooks{};
    std::vector<std::unique_ptr<PointerHook>> m_command_list_diagnostic_hooks{};
    std::unordered_map<uintptr_t, PointerHook*> m_create_graphics_pipeline_state_hook_lookup{};
    std::unordered_map<uintptr_t, PointerHook*> m_create_compute_pipeline_state_hook_lookup{};
    std::unordered_map<uintptr_t, PointerHook*> m_create_command_list_hook_lookup{};
    std::unordered_map<uintptr_t, PointerHook*> m_create_command_list1_hook_lookup{};
    std::unordered_map<uintptr_t, PointerHook*> m_create_command_signature_hook_lookup{};
    std::unordered_map<uintptr_t, PointerHook*> m_create_pipeline_state_hook_lookup{};
    std::unordered_map<uintptr_t, PointerHook*> m_create_root_signature_hook_lookup{};
    std::unordered_map<uintptr_t, PointerHook*> m_create_constant_buffer_view_hook_lookup{};
    std::unordered_map<uintptr_t, PointerHook*> m_create_render_target_view_hook_lookup{};
    std::unordered_map<uintptr_t, PointerHook*> m_create_depth_stencil_view_hook_lookup{};
    std::unordered_map<uintptr_t, PointerHook*> m_create_shader_resource_view_hook_lookup{};
    std::unordered_map<uintptr_t, PointerHook*> m_create_unordered_access_view_hook_lookup{};
    std::unordered_map<uintptr_t, PointerHook*> m_copy_descriptors_simple_hook_lookup{};
    std::unordered_map<uintptr_t, PointerHook*> m_copy_descriptors_hook_lookup{};
    std::unordered_map<uintptr_t, PointerHook*> m_set_pipeline_state_hook_lookup{};
    std::unordered_map<uintptr_t, PointerHook*> m_command_list_diagnostic_hook_lookup{};
    std::mutex m_command_list_hook_mutex{};
    std::unordered_set<uintptr_t> m_set_pipeline_state_slots{};
    std::unordered_set<uintptr_t> m_command_list_diagnostic_slots{};
    std::unique_ptr<VtableHook> m_swapchain_hook{};
    //std::unique_ptr<FunctionHook> m_create_swap_chain_hook{};

    OnPresentFn m_on_present{ nullptr };
    OnPresentFn m_on_post_present{ nullptr };
    OnResizeBuffersFn m_on_resize_buffers{ nullptr };
    OnResizeTargetFn m_on_resize_target{ nullptr };
    //OnCreateSwapChainFn m_on_create_swap_chain{ nullptr };
    
    static HRESULT present_internal(IDXGISwapChain3* swap_chain, UINT sync_interval, UINT flags, DXGI_PRESENT_PARAMETERS* params, bool present1 = false);

    static HRESULT WINAPI present(IDXGISwapChain3* swap_chain, UINT sync_interval, UINT flags);
    static HRESULT WINAPI present1(IDXGISwapChain3* swap_chain, UINT sync_interval, UINT flags, DXGI_PRESENT_PARAMETERS* params);
    static HRESULT WINAPI create_graphics_pipeline_state(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, REFIID riid, void** pipeline_state);
    static HRESULT WINAPI create_compute_pipeline_state(ID3D12Device* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, REFIID riid, void** pipeline_state);
    static HRESULT WINAPI create_command_list(ID3D12Device* device, UINT node_mask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator* command_allocator, ID3D12PipelineState* initial_state, REFIID riid, void** command_list);
    static HRESULT WINAPI create_command_list1(ID3D12Device4* device, UINT node_mask, D3D12_COMMAND_LIST_TYPE type, D3D12_COMMAND_LIST_FLAGS flags, REFIID riid, void** command_list);
    static HRESULT WINAPI create_command_signature(ID3D12Device* device, const D3D12_COMMAND_SIGNATURE_DESC* desc, ID3D12RootSignature* root_signature, REFIID riid, void** command_signature);
    static HRESULT WINAPI create_pipeline_state(ID3D12Device2* device, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, REFIID riid, void** pipeline_state);
    static HRESULT WINAPI create_root_signature(ID3D12Device* device, UINT node_mask, const void* blob, SIZE_T blob_length_in_bytes, REFIID riid, void** root_signature);
    static void WINAPI create_constant_buffer_view(ID3D12Device* device, const D3D12_CONSTANT_BUFFER_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor);
    static void WINAPI create_render_target_view(ID3D12Device* device, ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor);
    static void WINAPI create_shader_resource_view(ID3D12Device* device, ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor);
    static void WINAPI create_unordered_access_view(ID3D12Device* device, ID3D12Resource* resource, ID3D12Resource* counter_resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor);
    static void WINAPI create_depth_stencil_view(ID3D12Device* device, ID3D12Resource* resource, const D3D12_DEPTH_STENCIL_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor);
    static HRESULT WINAPI close_command_list(ID3D12GraphicsCommandList* command_list);
    static HRESULT WINAPI reset_command_list(ID3D12GraphicsCommandList* command_list, ID3D12CommandAllocator* allocator, ID3D12PipelineState* initial_state);
    static void WINAPI clear_state(ID3D12GraphicsCommandList* command_list, ID3D12PipelineState* pipeline_state);
    static void WINAPI set_pipeline_state(ID3D12GraphicsCommandList* command_list, ID3D12PipelineState* pipeline_state);
    static void WINAPI draw_instanced(ID3D12GraphicsCommandList* command_list, UINT vertex_count_per_instance, UINT instance_count, UINT start_vertex_location, UINT start_instance_location);
    static void WINAPI draw_indexed_instanced(ID3D12GraphicsCommandList* command_list, UINT index_count_per_instance, UINT instance_count, UINT start_index_location, INT base_vertex_location, UINT start_instance_location);
    static void WINAPI dispatch(ID3D12GraphicsCommandList* command_list, UINT thread_group_count_x, UINT thread_group_count_y, UINT thread_group_count_z);
    static void WINAPI execute_bundle(ID3D12GraphicsCommandList* command_list, ID3D12GraphicsCommandList* bundle);
    static void WINAPI execute_indirect(ID3D12GraphicsCommandList* command_list, ID3D12CommandSignature* command_signature, UINT max_command_count, ID3D12Resource* argument_buffer, UINT64 argument_buffer_offset, ID3D12Resource* count_buffer, UINT64 count_buffer_offset);
    static void WINAPI dispatch_mesh(ID3D12GraphicsCommandList6* command_list, UINT thread_group_count_x, UINT thread_group_count_y, UINT thread_group_count_z);
    static void WINAPI rs_set_viewports(ID3D12GraphicsCommandList* command_list, UINT num_viewports, const D3D12_VIEWPORT* viewports);
    static void WINAPI om_set_render_targets(ID3D12GraphicsCommandList* command_list, UINT num_render_target_descriptors, const D3D12_CPU_DESCRIPTOR_HANDLE* render_target_descriptors, BOOL rts_single_handle_to_descriptor_range, const D3D12_CPU_DESCRIPTOR_HANDLE* depth_stencil_descriptor);
    static void WINAPI clear_render_target_view(ID3D12GraphicsCommandList* command_list, D3D12_CPU_DESCRIPTOR_HANDLE render_target_view, const FLOAT color_rgba[4], UINT num_rects, const D3D12_RECT* rects);
    static void WINAPI resource_barrier(ID3D12GraphicsCommandList* command_list, UINT num_barriers, const D3D12_RESOURCE_BARRIER* barriers);
    static void WINAPI set_graphics_root_descriptor_table(ID3D12GraphicsCommandList* command_list, UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor);
    static void WINAPI set_compute_root_descriptor_table(ID3D12GraphicsCommandList* command_list, UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor);
    static void WINAPI set_compute_root_32bit_constant(ID3D12GraphicsCommandList* command_list, UINT root_parameter_index, UINT src_data, UINT dest_offset_in_32bit_values);
    static void WINAPI set_graphics_root_32bit_constant(ID3D12GraphicsCommandList* command_list, UINT root_parameter_index, UINT src_data, UINT dest_offset_in_32bit_values);
    static void WINAPI set_compute_root_32bit_constants(ID3D12GraphicsCommandList* command_list, UINT root_parameter_index, UINT num_32bit_values_to_set, const void* src_data, UINT dest_offset_in_32bit_values);
    static void WINAPI set_graphics_root_32bit_constants(ID3D12GraphicsCommandList* command_list, UINT root_parameter_index, UINT num_32bit_values_to_set, const void* src_data, UINT dest_offset_in_32bit_values);
    static void WINAPI set_compute_root_constant_buffer_view(ID3D12GraphicsCommandList* command_list, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS gpu_va);
    static void WINAPI set_compute_root_shader_resource_view(ID3D12GraphicsCommandList* command_list, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS gpu_va);
    static void WINAPI set_graphics_root_shader_resource_view(ID3D12GraphicsCommandList* command_list, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS gpu_va);
    static void WINAPI set_compute_root_unordered_access_view(ID3D12GraphicsCommandList* command_list, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS gpu_va);
    static void WINAPI set_graphics_root_unordered_access_view(ID3D12GraphicsCommandList* command_list, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS gpu_va);
    static void WINAPI set_descriptor_heaps(ID3D12GraphicsCommandList* command_list, UINT num_descriptor_heaps, ID3D12DescriptorHeap* const* descriptor_heaps);
    // 2026-05-17 SN2 fog Path A: redirect view 1's root_param 3 (FFogUniformParameters cbuffer)
    // to a UEVR-owned upload buffer so right-eye basepass can read view-1-correct content
    // independent of view 0. See project_sn2_fog_path_a_cbuffer_redirect_plan_2026-05-17 memory.
    static void WINAPI set_graphics_root_constant_buffer_view(ID3D12GraphicsCommandList* command_list, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS gpu_va);
    static void WINAPI copy_descriptors_simple(ID3D12Device* device, UINT num_descriptors, D3D12_CPU_DESCRIPTOR_HANDLE dst_start, D3D12_CPU_DESCRIPTOR_HANDLE src_start, D3D12_DESCRIPTOR_HEAP_TYPE type);
    static void WINAPI copy_descriptors(ID3D12Device* device, UINT num_dst_ranges, const D3D12_CPU_DESCRIPTOR_HANDLE* dst_starts, const UINT* dst_sizes, UINT num_src_ranges, const D3D12_CPU_DESCRIPTOR_HANDLE* src_starts, const UINT* src_sizes, D3D12_DESCRIPTOR_HEAP_TYPE type);
    static HRESULT WINAPI resize_buffers(IDXGISwapChain3* swap_chain, UINT buffer_count, UINT width, UINT height, DXGI_FORMAT new_format, UINT swap_chain_flags);
    static HRESULT WINAPI resize_target(IDXGISwapChain3* swap_chain, const DXGI_MODE_DESC* new_target_parameters);
    //static HRESULT WINAPI create_swap_chain(IDXGIFactory4* factory, IUnknown* device, HWND hwnd, const DXGI_SWAP_CHAIN_DESC* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* p_fullscreen_desc, IDXGIOutput* p_restrict_to_output, IDXGISwapChain** swap_chain);

    PointerHook* find_create_graphics_pipeline_state_hook(void* slot) const;
    PointerHook* find_create_compute_pipeline_state_hook(void* slot) const;
    PointerHook* find_create_command_list_hook(void* slot) const;
    PointerHook* find_create_command_list1_hook(void* slot) const;
    PointerHook* find_create_command_signature_hook(void* slot) const;
    PointerHook* find_create_pipeline_state_hook(void* slot) const;
    PointerHook* find_create_root_signature_hook(void* slot) const;
    PointerHook* find_create_constant_buffer_view_hook(void* slot) const;
    PointerHook* find_create_render_target_view_hook(void* slot) const;
    PointerHook* find_create_depth_stencil_view_hook(void* slot) const;
    PointerHook* find_create_shader_resource_view_hook(void* slot) const;
    PointerHook* find_create_unordered_access_view_hook(void* slot) const;
    PointerHook* find_copy_descriptors_simple_hook(void* slot) const;
    PointerHook* find_copy_descriptors_hook(void* slot) const;
    PointerHook* find_set_pipeline_state_hook(void* slot) const;
    PointerHook* find_command_list_diagnostic_hook(void* slot) const;
    void install_command_list_hooks(ID3D12GraphicsCommandList* command_list);
    void install_command_list_hooks_from_unknown(IUnknown* command_list);
};

