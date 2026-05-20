#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string_view>
#include <thread>
#include <future>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <spdlog/spdlog.h>
#include <wrl/client.h>
#include <utility/Thread.hpp>
#include <utility/Module.hpp>
#include <utility/RTTI.hpp>

#include "WindowFilter.hpp"
#include "Framework.hpp"
#include "render/D3D12Diagnostics.hpp"
#include "render/ShaderOverrideRegistry.hpp"

#include "D3D12Hook.hpp"

static D3D12Hook* g_d3d12_hook = nullptr;

// 2026-05-17 evening: published by FFakeStereoRenderingHook's lightscat
// midhook on every view-0 compute_volumetric_fog call. Read here by the
// fog descriptor swap to identify "view 0's currently-correct fog texture"
// when choosing a pool entry to mirror into view 1's basepass binding.
extern std::atomic<uintptr_t> g_subnautica2_view0_lightscat;

// 2026-05-17 evening: tiny VirtualQuery wrapper for the byte-scan path —
// avoids importing FFakeStereoRenderingHook's helpers. Returns true iff
// [address, address+size) is fully inside a single committed, readable region.
static inline bool is_readable_process_range_d3d12(uintptr_t address, size_t size) noexcept {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    const DWORD ok = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ |
                     PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;
    if ((mbi.Protect & ok) == 0) return false;
    const uintptr_t region_end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    return (address + size) <= region_end;
}

namespace {
constexpr size_t CREATE_GRAPHICS_PIPELINE_STATE_VTABLE_INDEX = 10;
constexpr size_t CREATE_COMPUTE_PIPELINE_STATE_VTABLE_INDEX = 11;
constexpr size_t CREATE_COMMAND_LIST_VTABLE_INDEX = 12;
constexpr size_t CREATE_COMMAND_SIGNATURE_VTABLE_INDEX = 41;
constexpr size_t CREATE_PIPELINE_STATE_VTABLE_INDEX = 47;
constexpr size_t CREATE_COMMAND_LIST1_VTABLE_INDEX = 51;
constexpr size_t CREATE_CONSTANT_BUFFER_VIEW_VTABLE_INDEX = 17;
constexpr size_t CREATE_SHADER_RESOURCE_VIEW_VTABLE_INDEX = 18;
constexpr size_t CREATE_UNORDERED_ACCESS_VIEW_VTABLE_INDEX = 19;
constexpr size_t CREATE_RENDER_TARGET_VIEW_VTABLE_INDEX = 20;
constexpr size_t CREATE_DEPTH_STENCIL_VIEW_VTABLE_INDEX = 21;
constexpr size_t COPY_DESCRIPTORS_VTABLE_INDEX = 23;
constexpr size_t COPY_DESCRIPTORS_SIMPLE_VTABLE_INDEX = 24;
constexpr size_t CLOSE_VTABLE_INDEX = 9;
constexpr size_t RESET_VTABLE_INDEX = 10;
constexpr size_t CLEAR_STATE_VTABLE_INDEX = 11;
constexpr size_t DRAW_INSTANCED_VTABLE_INDEX = 12;
constexpr size_t DRAW_INDEXED_INSTANCED_VTABLE_INDEX = 13;
constexpr size_t DISPATCH_VTABLE_INDEX = 14;
constexpr size_t RS_SET_VIEWPORTS_VTABLE_INDEX = 21;
constexpr size_t SET_PIPELINE_STATE_VTABLE_INDEX = 25;
constexpr size_t RESOURCE_BARRIER_VTABLE_INDEX = 26;
constexpr size_t EXECUTE_BUNDLE_VTABLE_INDEX = 27;
constexpr size_t SET_DESCRIPTOR_HEAPS_VTABLE_INDEX = 28;
constexpr size_t SET_COMPUTE_ROOT_DESCRIPTOR_TABLE_VTABLE_INDEX = 31;
constexpr size_t SET_GRAPHICS_ROOT_DESCRIPTOR_TABLE_VTABLE_INDEX = 32;
// 2026-05-17 BUG FIX: was 35, that's SetComputeRoot32BitConstants — wrong
// signature, caused SN2 to crash and our diagnostic to show garbage
// (root_param=10, gpu_va=0x4 == num_32bit_values=4). Correct index per
// d3d12.h vtable order is 38 (3 IUnknown + 4 ID3D12Object + 1 ID3D12DeviceChild
// + 1 ID3D12CommandList + 29th method of ID3D12GraphicsCommandList).
constexpr size_t SET_GRAPHICS_ROOT_CONSTANT_BUFFER_VIEW_VTABLE_INDEX = 38;
constexpr size_t OM_SET_RENDER_TARGETS_VTABLE_INDEX = 46;
constexpr size_t CLEAR_RENDER_TARGET_VIEW_VTABLE_INDEX = 48;
constexpr size_t EXECUTE_INDIRECT_VTABLE_INDEX = 59;
constexpr size_t DISPATCH_MESH_VTABLE_INDEX = 79;

enum class StereoTraceBucket : uint8_t {
    Unknown,
    Left,
    Right,
    Full,
    Multi
};

struct StereoTraceCounters {
    std::atomic<uint64_t> viewport_unknown{};
    std::atomic<uint64_t> viewport_left{};
    std::atomic<uint64_t> viewport_right{};
    std::atomic<uint64_t> viewport_full{};
    std::atomic<uint64_t> viewport_multi{};
    std::atomic<uint64_t> draw_unknown{};
    std::atomic<uint64_t> draw_left{};
    std::atomic<uint64_t> draw_right{};
    std::atomic<uint64_t> draw_full{};
    std::atomic<uint64_t> draw_multi{};
    std::atomic<uint64_t> draw_indexed_unknown{};
    std::atomic<uint64_t> draw_indexed_left{};
    std::atomic<uint64_t> draw_indexed_right{};
    std::atomic<uint64_t> draw_indexed_full{};
    std::atomic<uint64_t> draw_indexed_multi{};
    std::atomic<uint64_t> om_set_render_targets{};
    std::atomic<uint64_t> clear_unknown{};
    std::atomic<uint64_t> clear_left{};
    std::atomic<uint64_t> clear_right{};
    std::atomic<uint64_t> clear_full{};
    std::atomic<uint64_t> clear_multi{};
    std::atomic<uint64_t> resource_barriers{};
};

StereoTraceCounters g_stereo_trace_counters{};
thread_local StereoTraceBucket g_current_stereo_trace_bucket = StereoTraceBucket::Unknown;
// External-consumer toggle so the FFI (uevr_render_diag_set_stereo_trace_enabled)
// can enable the trace for *any* game, not just Subnautica2.
std::atomic<bool> g_stereo_trace_ffi_enabled{false};

// 2026-05-16 SN2 fog descriptor-swap correlation state.
// 2026-05-17 REWRITE: was thread_local, but UE5's parallel rendering means
// RSSetViewports can fire on one translate thread while SetGraphicsRootDescriptorTable
// fires on another for the SAME command list. thread_local missed the viewport
// set (caused all `has_viewport=0` in earlier Path A test). Per-cmdlist map fixes this.
struct CommandListCorrelationState {
    void*    current_pso = nullptr;
    float    viewport_top_left_x = 0.0f;
    float    viewport_top_left_y = 0.0f;
    float    viewport_width      = 0.0f;
    float    viewport_height     = 0.0f;
    bool     has_viewport        = false;
    // === Eye-Diff fingerprints (captured on bind, read at draw) ===
    StereoTraceBucket last_viewport_bucket = StereoTraceBucket::Unknown;
    uint64_t last_rtv0_handle = 0;                  // CPU descriptor handle of RTV slot 0
    uint64_t last_graphics_root_desc_table0 = 0;    // GPU descriptor handle of root parameter 0 (graphics)
    std::array<uint64_t, 16> last_graphics_root_cbv{}; // Root CBVs by root parameter index.
};
// Per-cmdlist state map. Keyed on raw ID3D12GraphicsCommandList* pointer.
// Entries persist for the lifetime of the cmdlist. UE5 creates a bounded
// number of cmdlists (~16-64), so the map stays small.
static std::mutex g_cmdlist_state_mutex;
static std::unordered_map<ID3D12GraphicsCommandList*, CommandListCorrelationState> g_cmdlist_state_map;
// Empty sentinel for read_cmdlist_state() when no entry exists yet.
static const CommandListCorrelationState g_cmdlist_state_empty{};
inline void update_cmdlist_pso(ID3D12GraphicsCommandList* cl, ID3D12PipelineState* pso);
inline void clear_cmdlist_state(ID3D12GraphicsCommandList* cl);

// 2026-05-17 night: SN2 sky-atmosphere PSO fingerprint.
// Set at CreateGraphicsPipelineState time when we scan the PS bytecode
// and find the entry-point name "RenderSkyAtmosphereRayMarchingPS".
// Used by set_graphics_root_constant_buffer_view to gate cbuffer redirects
// strictly to that specific PSO (instead of the unreliable cbv-stride
// heuristic that fired on unrelated PSOs).
std::atomic<void*> g_sky_atmos_pso{nullptr};

bool env_flag_enabled_a(const char* name) {
    char value[32]{};
    const auto len = GetEnvironmentVariableA(name, value, static_cast<DWORD>(sizeof(value)));
    if (len == 0 || len >= sizeof(value)) return false;
    std::string_view raw{value, std::min<DWORD>(len, static_cast<DWORD>(sizeof(value) - 1))};
    return raw != "0" && raw != "false" && raw != "FALSE" && raw != "off" && raw != "OFF";
}

std::string env_value_a(const char* name) {
    char value[512]{};
    const auto len = GetEnvironmentVariableA(name, value, static_cast<DWORD>(sizeof(value)));
    if (len == 0) return "(unset)";
    if (len >= sizeof(value)) return "(too-long)";
    return std::string(value, value + len);
}

int env_int_a(const char* name, int fallback) {
    char value[32]{};
    const auto len = GetEnvironmentVariableA(name, value, static_cast<DWORD>(sizeof(value)));
    if (len == 0 || len >= sizeof(value)) return fallback;
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    return end != value ? static_cast<int>(parsed) : fallback;
}

bool sn2_diag_strict_enabled() {
    static const bool enabled = env_flag_enabled_a("UEVR_SN2_DIAG_STRICT");
    return enabled;
}

bool sn2_diag_clean_enabled() {
    static const bool enabled = env_flag_enabled_a("UEVR_SN2_DIAG_CLEAN") || sn2_diag_strict_enabled();
    return enabled;
}

bool sn2_pso3069_diag_enabled() {
    static const bool enabled = sn2_diag_clean_enabled() || env_flag_enabled_a("UEVR_SN2_PSO3069_DIAG");
    return enabled;
}

bool is_subnautica2_process() {
    static const bool result = []() {
        // Honor UEVR_DISABLE_SN2_HOOKS=1 as an escape hatch so we can test
        // the generic Shader Hunter / override paths without SN2-specific
        // stereo trace and SingleLayerWater patches interfering.
        char disable[8]{};
        if (GetEnvironmentVariableA("UEVR_DISABLE_SN2_HOOKS", disable, sizeof(disable)) > 0
                && disable[0] == '1') {
            return false;
        }
        const auto exe_path = utility::get_module_pathw(utility::get_executable());
        return exe_path && exe_path->find(L"Subnautica2-Win64-Shipping") != std::wstring::npos;
    }();

    return result;
}

bool is_stereo_trace_enabled() {
    return g_stereo_trace_ffi_enabled.load(std::memory_order_relaxed) || is_subnautica2_process();
}

bool enable_d3d12_diagnostic_command_list_hooks() {
    static const bool enabled = []() {
        char value[32]{};
        const auto len = GetEnvironmentVariableA(
            "UEVR_ENABLE_D3D12_DIAGNOSTIC_COMMAND_LIST_HOOKS",
            value,
            static_cast<DWORD>(sizeof(value)));

        if (len == 0) {
            return false;
        }

        std::string_view raw{value, std::min<DWORD>(len, static_cast<DWORD>(sizeof(value) - 1))};
        return raw != "0" && raw != "false" && raw != "FALSE" && raw != "off" && raw != "OFF";
    }();

    return enabled;
}

// Separate env-var so the descriptor-table hook (which adds non-trivial cost
// to every draw call on the render thread) is opt-in even when the regular
// diagnostic hooks are enabled. Default: OFF. Set
// UEVR_ENABLE_D3D12_DESCRIPTOR_TABLE_HOOK=1 to enable.
bool enable_d3d12_descriptor_table_hook() {
    static const bool enabled = []() {
        if (sn2_pso3069_diag_enabled()) return true;
        char value[32]{};
        const auto len = GetEnvironmentVariableA(
            "UEVR_ENABLE_D3D12_DESCRIPTOR_TABLE_HOOK",
            value,
            static_cast<DWORD>(sizeof(value)));
        if (len == 0) return false;
        std::string_view raw{value, std::min<DWORD>(len, static_cast<DWORD>(sizeof(value) - 1))};
        return raw != "0" && raw != "false" && raw != "FALSE" && raw != "off" && raw != "OFF";
    }();
    return enabled;
}

// 2026-05-17 SN2 FOG-FIX Task #45: enable the per-view-fog-scope compute
// descriptor-table tagging hooks. Captures bound CBV_SRV_UAV heap CPU/GPU
// base via SetDescriptorHeaps (vtable 28); on SetComputeRootDescriptorTable
// (vtable 31), if the per-view fog atomic is 0 or 1, walks a bounded number
// of descriptors from the bound base, looks each up in sn2_fog_uav_map and
// sn2_fog_srv_map by cpu_handle, and tags the entry with the current
// view_id. Default OFF — overhead is non-trivial when on.
bool enable_d3d12_compute_root_table_hook() {
    static const bool enabled = []() {
        char value[32]{};
        const auto len = GetEnvironmentVariableA(
            "UEVR_SUBNAUTICA2_ENABLE_FOG_COMPUTE_BIND_HOOK",
            value,
            static_cast<DWORD>(sizeof(value)));
        if (len == 0) return false;
        std::string_view raw{value, std::min<DWORD>(len, static_cast<DWORD>(sizeof(value) - 1))};
        return raw != "0" && raw != "false" && raw != "FALSE" && raw != "off" && raw != "OFF";
    }();
    return enabled;
}
} // anonymous namespace

// 2026-05-18: forward declarations for the upload-buffer Map vtable hook
// system defined later in this file. Placed at GLOBAL scope (outside the
// anonymous namespace above) so the linker resolves to the global-scope
// definitions in `namespace sn2_upload_buf_map { ... }` later in this file.
namespace sn2_upload_buf_map {
    bool install_map_hook(ID3D12Resource* sample_res);
    uint8_t* gpu_va_to_cpu(D3D12_GPU_VIRTUAL_ADDRESS gpu_va, uint64_t min_size);
    uint64_t scan_tracked_buffers_for_bad_cb0_tail();
    size_t entry_count();
    // SEH-safe in-place cb0 bug patch. Returns true if the buffer matched the
    // broken view-1 signature and was rewritten to the view-0 layout.
    bool try_inline_patch_cb0(uint8_t* cpu);
    // Install hook on ID3D12Device::CreateCommittedResource (vtable index 27).
    // Once installed, every newly created UPLOAD heap buffer is Map'd immediately
    // and registered in our tracking, so the inline cb0 patcher can find it.
    bool install_device_hook(ID3D12Device* device);
}
namespace sn2_fixed_cb0 {
    // Lazily creates a 512-byte UPLOAD heap buffer with view-0's correct cb0
    // contents (zeros at rows 0..29, then (0,0,0,1.0) × 4 at rows 28..31).
    // Returns the GPU VA. Used to REDIRECT cb0 bindings whose original GPU VA
    // is in the pre-injection pool range (engine pool created before UEVR hook).
    D3D12_GPU_VIRTUAL_ADDRESS get_or_create(ID3D12Device* device);
}
namespace sn2_descriptor_registry {
    void record_cbv(const D3D12_CONSTANT_BUFFER_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE cpu);
    void record_srv(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE cpu);
    void record_uav(ID3D12Resource* resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE cpu);
}
namespace sn2_command_signature_registry {
    struct ArgumentInfo {
        D3D12_INDIRECT_ARGUMENT_TYPE type{D3D12_INDIRECT_ARGUMENT_TYPE_DRAW};
        UINT root_parameter_index{0};
        UINT dest_offset_words{0};
        UINT num_values{0};
        UINT slot{0};
    };
    struct Entry {
        uint64_t id{0};
        UINT byte_stride{0};
        std::vector<ArgumentInfo> args{};
        bool has_dispatch{false};
        bool has_draw{false};
        bool has_draw_indexed{false};
        bool has_dispatch_mesh{false};
        std::string layout{};
    };
    void record(ID3D12CommandSignature* signature, const D3D12_COMMAND_SIGNATURE_DESC* desc);
    bool lookup(ID3D12CommandSignature* signature, Entry& out);
    const char* kind(const Entry& entry);
}
namespace sn2_gpu_readback {
    // Slot metadata exposed for the polling callback.
    struct Slot {
        D3D12_GPU_VIRTUAL_ADDRESS source_va{0};
        uint64_t                  size{0};
        uint64_t                  scheduled_frame{0};
        const char*               tag{nullptr};
        uint64_t                  fence_value{0};
        bool                      data_ready{false};
        bool                      dumped{false};
    };
    bool init(ID3D12Device* device);
    bool initialized();
    uint64_t schedule_copy(ID3D12GraphicsCommandList* cl, ID3D12Resource* src,
                           UINT64 src_offset, UINT64 size, const char* tag,
                           uint64_t scheduled_frame);
    void signal_after_execute(ID3D12CommandQueue* queue);
    void shutdown();
    // poll() is a template defined in the implementation namespace below.
    // To call it from outside, use the wrapper drain_to_log().
    void drain_to_log();
}
namespace sn2_indirect_arg_inject {
    // Allocates an UPLOAD-heap arg buffer once, exposes a CPU write API and
    // returns the resource pointer. Used to substitute right-eye Main-
    // IndirectDispatchCS arg buffers with LEFT eye's captured workgroup counts,
    // so the downstream Nanite culls actually run for right eye.
    bool init(ID3D12Device* device);
    bool initialized();
    // Write `size` bytes to the upload buffer at `offset`. Caller-side mutex
    // since multiple captures may race.
    void write_bytes(uint64_t offset, const void* data, size_t size);
    // Returns the underlying resource (for parameter substitution) or nullptr.
    ID3D12Resource* resource();
    // Returns the GPU VA the substitute will be read at (offset 0 of our buf).
    D3D12_GPU_VIRTUAL_ADDRESS base_gpu_va();
}
namespace sn2_rt_snapshot {
    // 2D render-target snapshot. Captures pixel data of any 2D texture into a
    // permanently-mapped READBACK buffer, then saves to PPM on disk on the
    // next frame's drain.
    bool init(ID3D12Device* device);
    bool initialized();
    uint64_t schedule_capture(ID3D12GraphicsCommandList* cl,
                              ID3D12Resource* src_tex,
                              D3D12_RESOURCE_STATES src_current_state,
                              const char* tag);
    void signal_after_execute(ID3D12CommandQueue* queue);
    void drain_to_disk();
    bool enabled();
    // Phase 3: queue capture-intent from a hot draw hook. Present hook drains
    // intents using its own persistent CL (no mid-recording state race).
    void queue_intent(ID3D12Resource* res, char eye_char, uint64_t seq);
    // Tagged variant for non-basepass passes (SKY-L, VOL-R, POST-L, ...).
    void queue_intent_named(ID3D12Resource* res, const char* tag_prefix,
        char eye_char, uint64_t seq);
    struct Intent { ID3D12Resource* res; char tag[32]; };
    bool dequeue_intent(Intent& out);
    // Lightweight RTV CPU-handle → resource map populated from
    // CreateRenderTargetView. Independent of D3D12Diagnostics::is_enabled().
    void record_rtv(uint64_t cpu_handle, ID3D12Resource* res);
    ID3D12Resource* lookup_rtv(uint64_t cpu_handle);
}
namespace sn2_compute_capture {
    // 2026-05-19: Capture of LEFT-eye compute call state so it can later be
    // replayed for RIGHT eye. Target PSOs are the Nanite + VSM passes that
    // diagnostic UEVR_SN2_EI_DIAG=1 showed don't fire on right eye:
    //   0x6E901F3C VirtualShadowMapProjection
    //   0x1A15E3D7 / 0x794CF7AE RasterBinBuild
    //   0x67BAD483 MergeStaticPhysicalPagesIndirectCS
    //   0xBE95FE40 InstanceCull
    //   0xBD3584EE NodeAndClusterCull
    struct CapturedCall {
        void*                       pso{nullptr};
        uint32_t                    cs_crc32{0};
        // 16 root CBVs (compute) by root_parameter_index
        std::array<D3D12_GPU_VIRTUAL_ADDRESS, 16> root_cbvs{};
        // 16 root descriptor tables (compute)
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 16> root_tables{};
        // Indirect args (if applicable)
        ID3D12Resource*             arg_buffer{nullptr};
        UINT64                      arg_buffer_offset{0};
        // Direct args (if dispatch not indirect)
        UINT                        thread_group_count_x{0};
        UINT                        thread_group_count_y{0};
        UINT                        thread_group_count_z{0};
        bool                        is_indirect{false};
        uint64_t                    frame{0};
    };

    // Returns true if cs_crc32 is in our target list of Nanite/VSM shaders.
    bool is_target_compute(uint32_t cs_crc32);

    // Hook entry points (called from execute_indirect / dispatch). Capture
    // happens only when sn2_compute_capture_enabled() returns true AND the
    // call's PSO matches is_target_compute().
    void capture_indirect(ID3D12GraphicsCommandList* cl,
                          void* pso, uint32_t cs_crc32,
                          ID3D12Resource* arg_buffer, UINT64 arg_offset,
                          int view_id);
    void capture_direct(ID3D12GraphicsCommandList* cl,
                        void* pso, uint32_t cs_crc32,
                        UINT x, UINT y, UINT z, int view_id);

    // Diagnostic: log the per-frame summary of left-eye captured calls
    // grouped by cs_crc32.
    void log_frame_summary();
    void clear_frame();

    bool enabled();
    bool is_main_indirect_compute(uint32_t cs_crc32);
    uint64_t main_indirect_inject_offset(uint32_t cs_crc32);
    void on_left_main_args_ready(uint32_t cs_crc32, const uint8_t* bytes, size_t size);
}

namespace d3d12_stereo_trace {
void set_ffi_enabled(bool enabled) { g_stereo_trace_ffi_enabled.store(enabled, std::memory_order_relaxed); }
bool is_ffi_enabled() { return g_stereo_trace_ffi_enabled.load(std::memory_order_relaxed); }

CountersSnapshot peek() {
    CountersSnapshot s{};
    s.viewport_unknown = g_stereo_trace_counters.viewport_unknown.load();
    s.viewport_left    = g_stereo_trace_counters.viewport_left.load();
    s.viewport_right   = g_stereo_trace_counters.viewport_right.load();
    s.viewport_full    = g_stereo_trace_counters.viewport_full.load();
    s.viewport_multi   = g_stereo_trace_counters.viewport_multi.load();
    s.draw_unknown     = g_stereo_trace_counters.draw_unknown.load();
    s.draw_left        = g_stereo_trace_counters.draw_left.load();
    s.draw_right       = g_stereo_trace_counters.draw_right.load();
    s.draw_full        = g_stereo_trace_counters.draw_full.load();
    s.draw_multi       = g_stereo_trace_counters.draw_multi.load();
    s.draw_indexed_unknown = g_stereo_trace_counters.draw_indexed_unknown.load();
    s.draw_indexed_left    = g_stereo_trace_counters.draw_indexed_left.load();
    s.draw_indexed_right   = g_stereo_trace_counters.draw_indexed_right.load();
    s.draw_indexed_full    = g_stereo_trace_counters.draw_indexed_full.load();
    s.draw_indexed_multi   = g_stereo_trace_counters.draw_indexed_multi.load();
    s.clear_unknown    = g_stereo_trace_counters.clear_unknown.load();
    s.clear_left       = g_stereo_trace_counters.clear_left.load();
    s.clear_right      = g_stereo_trace_counters.clear_right.load();
    s.clear_full       = g_stereo_trace_counters.clear_full.load();
    s.clear_multi      = g_stereo_trace_counters.clear_multi.load();
    s.om_set_render_targets = g_stereo_trace_counters.om_set_render_targets.load();
    s.resource_barriers     = g_stereo_trace_counters.resource_barriers.load();
    return s;
}

void reset() {
    auto z = [](std::atomic<uint64_t>& c) { c.store(0); };
    z(g_stereo_trace_counters.viewport_unknown); z(g_stereo_trace_counters.viewport_left);
    z(g_stereo_trace_counters.viewport_right);   z(g_stereo_trace_counters.viewport_full);
    z(g_stereo_trace_counters.viewport_multi);   z(g_stereo_trace_counters.draw_unknown);
    z(g_stereo_trace_counters.draw_left);        z(g_stereo_trace_counters.draw_right);
    z(g_stereo_trace_counters.draw_full);        z(g_stereo_trace_counters.draw_multi);
    z(g_stereo_trace_counters.draw_indexed_unknown); z(g_stereo_trace_counters.draw_indexed_left);
    z(g_stereo_trace_counters.draw_indexed_right);   z(g_stereo_trace_counters.draw_indexed_full);
    z(g_stereo_trace_counters.draw_indexed_multi);   z(g_stereo_trace_counters.clear_unknown);
    z(g_stereo_trace_counters.clear_left);       z(g_stereo_trace_counters.clear_right);
    z(g_stereo_trace_counters.clear_full);       z(g_stereo_trace_counters.clear_multi);
    z(g_stereo_trace_counters.om_set_render_targets);
    z(g_stereo_trace_counters.resource_barriers);
}
} // namespace d3d12_stereo_trace

namespace {

StereoTraceBucket classify_viewports(UINT num_viewports, const D3D12_VIEWPORT* viewports) {
    if (viewports == nullptr || num_viewports == 0) {
        return StereoTraceBucket::Unknown;
    }

    bool saw_left = false;
    bool saw_right = false;
    bool saw_full = false;
    bool saw_unknown = false;

    // Resolution-agnostic classification: a stereo right-eye viewport has
    // TopLeftX > 0 (offset to right half); left-eye has TopLeftX == 0 and
    // a Width that's less than viewport.TopLeftX + Width would be for full.
    // Without knowing the backbuffer width up front, we use the heuristic
    // "TopLeftX > Width" → definitely right eye (offset is at least the
    // width of the eye), and "TopLeftX <= 1.0" → left or full.
    for (UINT i = 0; i < num_viewports; ++i) {
        const auto& viewport = viewports[i];
        if (viewport.Width <= 0.0f || viewport.Height <= 0.0f) {
            saw_unknown = true;
            continue;
        }
        // Right eye: TopLeftX is at least one full eye-width to the right of 0.
        // Equivalently, TopLeftX is roughly equal to or greater than Width.
        // Add a small tolerance for sub-pixel placement.
        if (viewport.TopLeftX >= viewport.Width - 4.0f && viewport.TopLeftX > 4.0f) {
            saw_right = true;
        } else if (viewport.TopLeftX <= 4.0f) {
            // Left or full eye — distinguish via aspect / size if needed; for
            // per-eye-skip purposes Left vs Full doesn't matter as long as
            // we don't accidentally classify Right as Left.
            saw_left = true;
        } else {
            saw_unknown = true;
        }
    }

    if ((saw_left && saw_right) || num_viewports > 1) {
        return StereoTraceBucket::Multi;
    }

    if (saw_right) {
        return StereoTraceBucket::Right;
    }

    if (saw_left) {
        return StereoTraceBucket::Left;
    }

    if (saw_full) {
        return StereoTraceBucket::Full;
    }

    return saw_unknown ? StereoTraceBucket::Unknown : StereoTraceBucket::Unknown;
}

StereoTraceBucket classify_rects(UINT num_rects, const D3D12_RECT* rects) {
    if (rects == nullptr || num_rects == 0) {
        return g_current_stereo_trace_bucket;
    }

    bool saw_left = false;
    bool saw_right = false;
    bool saw_full = false;
    bool saw_unknown = false;

    for (UINT i = 0; i < num_rects; ++i) {
        const auto& rect = rects[i];
        const auto width = rect.right - rect.left;

        if (rect.left <= 4 && width >= 2400) {
            saw_full = true;
        } else if (rect.left >= 1200) {
            saw_right = true;
        } else if (rect.left <= 64 && width <= 2200) {
            saw_left = true;
        } else {
            saw_unknown = true;
        }
    }

    if ((saw_left && saw_right) || num_rects > 1) {
        return StereoTraceBucket::Multi;
    }

    if (saw_right) {
        return StereoTraceBucket::Right;
    }

    if (saw_left) {
        return StereoTraceBucket::Left;
    }

    if (saw_full) {
        return StereoTraceBucket::Full;
    }

    return saw_unknown ? StereoTraceBucket::Unknown : StereoTraceBucket::Unknown;
}

void increment_bucket(
    StereoTraceBucket bucket,
    std::atomic<uint64_t>& unknown,
    std::atomic<uint64_t>& left,
    std::atomic<uint64_t>& right,
    std::atomic<uint64_t>& full,
    std::atomic<uint64_t>& multi
) {
    switch (bucket) {
    case StereoTraceBucket::Left:
        ++left;
        break;
    case StereoTraceBucket::Right:
        ++right;
        break;
    case StereoTraceBucket::Full:
        ++full;
        break;
    case StereoTraceBucket::Multi:
        ++multi;
        break;
    default:
        ++unknown;
        break;
    }
}

uint64_t take_counter(std::atomic<uint64_t>& value) {
    return value.exchange(0);
}

void log_stereo_trace_if_needed() {
    if (!is_subnautica2_process()) {
        return;
    }

    static auto last_log = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();

    if (now - last_log < std::chrono::seconds(2)) {
        return;
    }

    last_log = now;

    const auto vp_unknown = take_counter(g_stereo_trace_counters.viewport_unknown);
    const auto vp_left = take_counter(g_stereo_trace_counters.viewport_left);
    const auto vp_right = take_counter(g_stereo_trace_counters.viewport_right);
    const auto vp_full = take_counter(g_stereo_trace_counters.viewport_full);
    const auto vp_multi = take_counter(g_stereo_trace_counters.viewport_multi);
    const auto draw_unknown = take_counter(g_stereo_trace_counters.draw_unknown);
    const auto draw_left = take_counter(g_stereo_trace_counters.draw_left);
    const auto draw_right = take_counter(g_stereo_trace_counters.draw_right);
    const auto draw_full = take_counter(g_stereo_trace_counters.draw_full);
    const auto draw_multi = take_counter(g_stereo_trace_counters.draw_multi);
    const auto draw_indexed_unknown = take_counter(g_stereo_trace_counters.draw_indexed_unknown);
    const auto draw_indexed_left = take_counter(g_stereo_trace_counters.draw_indexed_left);
    const auto draw_indexed_right = take_counter(g_stereo_trace_counters.draw_indexed_right);
    const auto draw_indexed_full = take_counter(g_stereo_trace_counters.draw_indexed_full);
    const auto draw_indexed_multi = take_counter(g_stereo_trace_counters.draw_indexed_multi);
    const auto om_set_render_targets = take_counter(g_stereo_trace_counters.om_set_render_targets);
    const auto clear_unknown = take_counter(g_stereo_trace_counters.clear_unknown);
    const auto clear_left = take_counter(g_stereo_trace_counters.clear_left);
    const auto clear_right = take_counter(g_stereo_trace_counters.clear_right);
    const auto clear_full = take_counter(g_stereo_trace_counters.clear_full);
    const auto clear_multi = take_counter(g_stereo_trace_counters.clear_multi);
    const auto resource_barriers = take_counter(g_stereo_trace_counters.resource_barriers);

    const auto total =
        vp_unknown + vp_left + vp_right + vp_full + vp_multi +
        draw_unknown + draw_left + draw_right + draw_full + draw_multi +
        draw_indexed_unknown + draw_indexed_left + draw_indexed_right + draw_indexed_full + draw_indexed_multi +
        om_set_render_targets + clear_unknown + clear_left + clear_right + clear_full + clear_multi + resource_barriers;

    if (total == 0) {
        return;
    }

    spdlog::info(
        "[D3D12][StereoTrace] viewports L/R/F/M/U={}/{}/{}/{}/{} draws L/R/F/M/U={}/{}/{}/{}/{} indexed L/R/F/M/U={}/{}/{}/{}/{} clears L/R/F/M/U={}/{}/{}/{}/{} om={} barriers={}",
        vp_left,
        vp_right,
        vp_full,
        vp_multi,
        vp_unknown,
        draw_left,
        draw_right,
        draw_full,
        draw_multi,
        draw_unknown,
        draw_indexed_left,
        draw_indexed_right,
        draw_indexed_full,
        draw_indexed_multi,
        draw_indexed_unknown,
        clear_left,
        clear_right,
        clear_full,
        clear_multi,
        clear_unknown,
        om_set_render_targets,
        resource_barriers);
}

bool should_preserve_present_params_for_current_game() {
    static const bool result = []() {
        const auto exe_path = utility::get_module_pathw(utility::get_executable());
        return exe_path && exe_path->find(L"MafiaTheOldCountry") != std::wstring::npos;
    }();

    return result;
}

template <typename TInterface>
void add_unique_pointer_hook(
    TInterface* iface,
    size_t vtable_index,
    void* detour,
    std::vector<std::unique_ptr<PointerHook>>& storage,
    std::unordered_map<uintptr_t, PointerHook*>& lookup,
    std::unordered_set<uintptr_t>& seen_slots
) {
    if (iface == nullptr) {
        return;
    }

    auto** slot = &(*(void***)iface)[vtable_index];
    const auto slot_key = reinterpret_cast<uintptr_t>(slot);

    if (!seen_slots.emplace(slot_key).second) {
        return;
    }

    auto hook = std::make_unique<PointerHook>(slot, detour);
    lookup.emplace(slot_key, hook.get());
    storage.emplace_back(std::move(hook));
}
}

D3D12Hook::~D3D12Hook() {
    unhook();
}

bool D3D12Hook::hook() {
    spdlog::info("Hooking D3D12");

    g_d3d12_hook = this;

    IDXGISwapChain1* swap_chain1{ nullptr };
    IDXGISwapChain3* swap_chain{ nullptr };
    ID3D12Device* device{ nullptr };
    ID3D12CommandAllocator* command_allocator{ nullptr };
    ID3D12GraphicsCommandList* command_list{ nullptr };

    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
    DXGI_SWAP_CHAIN_DESC1 swap_chain_desc1;

    ZeroMemory(&swap_chain_desc1, sizeof(swap_chain_desc1));

    swap_chain_desc1.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swap_chain_desc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_desc1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swap_chain_desc1.BufferCount = 2;
    swap_chain_desc1.SampleDesc.Count = 1;
    swap_chain_desc1.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    swap_chain_desc1.Width = 1;
    swap_chain_desc1.Height = 1;

    // Manually get D3D12CreateDevice export because the user may be running Windows 7
    const auto d3d12_module = LoadLibraryA("d3d12.dll");
    if (d3d12_module == nullptr) {
        spdlog::error("Failed to load d3d12.dll");
        return false;
    }

    auto d3d12_create_device = (decltype(D3D12CreateDevice)*)GetProcAddress(d3d12_module, "D3D12CreateDevice");
    if (d3d12_create_device == nullptr) {
        spdlog::error("Failed to get D3D12CreateDevice export");
        return false;
    }

    spdlog::info("Creating dummy device");

    // Get the original on-disk bytes of the D3D12CreateDevice export
    const auto original_bytes = utility::get_original_bytes(d3d12_create_device);

    // Temporarily unhook D3D12CreateDevice
    // it allows compatibility with ReShade and other overlays that hook it
    // this is just a dummy device anyways, we don't want the other overlays to be able to use it
    if (original_bytes) {
        spdlog::info("D3D12CreateDevice appears to be hooked, temporarily unhooking");

        std::vector<uint8_t> hooked_bytes(original_bytes->size());
        memcpy(hooked_bytes.data(), d3d12_create_device, original_bytes->size());

        ProtectionOverride protection_override{ d3d12_create_device, original_bytes->size(), PAGE_EXECUTE_READWRITE };
        memcpy(d3d12_create_device, original_bytes->data(), original_bytes->size());
        
        if (FAILED(d3d12_create_device(nullptr, feature_level, IID_PPV_ARGS(&device)))) {
            spdlog::error("Failed to create D3D12 Dummy device");
            memcpy(d3d12_create_device, hooked_bytes.data(), hooked_bytes.size());
            return false;
        }

        spdlog::info("Restoring hooked bytes for D3D12CreateDevice");
        memcpy(d3d12_create_device, hooked_bytes.data(), hooked_bytes.size());
    } else { // D3D12CreateDevice is not hooked
        if (FAILED(d3d12_create_device(nullptr, feature_level, IID_PPV_ARGS(&device)))) {
            spdlog::error("Failed to create D3D12 Dummy device");
            return false;
        }
    }

    spdlog::info("Dummy device: {:x}", (uintptr_t)device);

    // Manually get CreateDXGIFactory export because the user may be running Windows 7
    const auto dxgi_module = LoadLibraryA("dxgi.dll");
    if (dxgi_module == nullptr) {
        spdlog::error("Failed to load dxgi.dll");
        return false;
    }

    auto create_dxgi_factory = (decltype(CreateDXGIFactory)*)GetProcAddress(dxgi_module, "CreateDXGIFactory");

    if (create_dxgi_factory == nullptr) {
        spdlog::error("Failed to get CreateDXGIFactory export");
        return false;
    }

    spdlog::info("Creating dummy DXGI factory");

    IDXGIFactory4* factory{ nullptr };
    if (FAILED(create_dxgi_factory(IID_PPV_ARGS(&factory)))) {
        spdlog::error("Failed to create D3D12 Dummy DXGI Factory");
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue_desc.Priority = 0;
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queue_desc.NodeMask = 0;

    spdlog::info("Creating dummy command queue");

    ID3D12CommandQueue* command_queue{ nullptr };
    if (FAILED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue)))) {
        spdlog::error("Failed to create D3D12 Dummy Command Queue");
        return false;
    }

    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator)))) {
        spdlog::error("Failed to create D3D12 Dummy Command Allocator");
        return false;
    }

    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator, nullptr, IID_PPV_ARGS(&command_list)))) {
        spdlog::error("Failed to create D3D12 Dummy Graphics Command List");
        return false;
    }

    spdlog::info("Creating dummy swapchain");

    // used in CreateSwapChainForHwnd fallback
    HWND hwnd = 0;
    WNDCLASSEX wc{};

    auto init_dummy_window = [&]() {
        // fallback to CreateSwapChainForHwnd
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = DefWindowProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = GetModuleHandle(NULL);
        wc.hIcon = NULL;
        wc.hCursor = NULL;
        wc.hbrBackground = NULL;
        wc.lpszMenuName = NULL;
        wc.lpszClassName = TEXT("REFRAMEWORK_DX12_DUMMY");
        wc.hIconSm = NULL;

        ::RegisterClassEx(&wc);

        hwnd = ::CreateWindow(wc.lpszClassName, TEXT("REF DX Dummy Window"), WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);

        swap_chain_desc1.BufferCount = 3;
        swap_chain_desc1.Width = 0;
        swap_chain_desc1.Height = 0;
        swap_chain_desc1.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap_chain_desc1.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        swap_chain_desc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_chain_desc1.SampleDesc.Count = 1;
        swap_chain_desc1.SampleDesc.Quality = 0;
        swap_chain_desc1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swap_chain_desc1.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        swap_chain_desc1.Scaling = DXGI_SCALING_STRETCH;
        swap_chain_desc1.Stereo = FALSE;
    };

    std::vector<std::function<bool ()>> swapchain_attempts{
        // we call CreateSwapChainForComposition instead of CreateSwapChainForHwnd
        // because some overlays will have hooks on CreateSwapChainForHwnd
        // and all we're doing is creating a dummy swapchain
        // we don't want to screw up the overlay
        [&]() {
            return !FAILED(factory->CreateSwapChainForComposition(command_queue, &swap_chain_desc1, nullptr, &swap_chain1));
        },
        [&]() {
            init_dummy_window();

            return !FAILED(factory->CreateSwapChainForHwnd(command_queue, hwnd, &swap_chain_desc1, nullptr, nullptr, &swap_chain1));
        },
        [&]() {
            return !FAILED(factory->CreateSwapChainForHwnd(command_queue, GetDesktopWindow(), &swap_chain_desc1, nullptr, nullptr, &swap_chain1));
        },
    };

    bool any_succeed = false;

    for (auto i = 0; i < swapchain_attempts.size(); i++) {
        auto& attempt = swapchain_attempts[i];
        
        try {
            spdlog::info("Trying swapchain attempt {}", i);

            if (attempt()) {
                spdlog::info("Created dummy swapchain on attempt {}", i);
                any_succeed = true;
                break;
            }
        } catch (std::exception& e) {
            spdlog::error("Failed to create dummy swapchain on attempt {}: {}", i, e.what());
        } catch(...) {
            spdlog::error("Failed to create dummy swapchain on attempt {}: unknown exception", i);
        }

        spdlog::error("Attempt {} failed", i);
    }

    if (!any_succeed) {
        spdlog::error("Failed to create D3D12 Dummy Swap Chain");

        if (hwnd) {
            ::DestroyWindow(hwnd);
        }

        if (wc.lpszClassName != nullptr) {
            ::UnregisterClass(wc.lpszClassName, wc.hInstance);
        }

        return false;
    }

    spdlog::info("Querying dummy swapchain");

    if (FAILED(swap_chain1->QueryInterface(IID_PPV_ARGS(&swap_chain)))) {
        spdlog::error("Failed to retrieve D3D12 DXGI SwapChain");
        return false;
    }

    if (!m_skip_dummy_swapchain_type_info_probe) {
        try {
            const auto ti = utility::rtti::get_type_info(swap_chain1);
            const auto swapchain_classname = ti != nullptr && ti->name() != nullptr ? std::string_view{ti->name()} : "unknown";
            const auto raw_name = ti != nullptr && ti->raw_name() != nullptr ? std::string_view{ti->raw_name()} : "unknown";

            spdlog::info("Swapchain type info: {}", swapchain_classname);
            spdlog::info("Swapchain raw type info: {}", raw_name);
            
            if (swapchain_classname.contains("interposer::DXGISwapChain")) { // DLSS3
                spdlog::info("Found Streamline (DLSSFG) swapchain during dummy initialization: {:x}", (uintptr_t)swap_chain1);
                m_using_frame_generation_swapchain = true;
            }
            // Need to test this one to see if it actually has the same issues - disabling it for now
            /*else if (swapchain_classname.contains("FrameInterpolationSwapChain")) { // FSR3
                spdlog::info("Found FSR3 swapchain during dummy initialization: {:x}", (uintptr_t)swap_chain1);
                m_using_frame_generation_swapchain = true;
            }*/
        } catch (const std::exception& e) {
            spdlog::error("Failed to get type info: {}. Disabling dummy swapchain RTTI probe for this session.", e.what());
            m_skip_dummy_swapchain_type_info_probe = true;
        } catch (...) {
            spdlog::error("Failed to get type info: unknown exception. Disabling dummy swapchain RTTI probe for this session.");
            m_skip_dummy_swapchain_type_info_probe = true;
        }
    }

    spdlog::info("Finding command queue offset");

    m_command_queue_offset = 0;

    // Find the command queue offset in the swapchain
    for (auto i = 0; i < 512 * sizeof(void*); i += sizeof(void*)) {
        const auto base = (uintptr_t)swap_chain1 + i;

        // reached the end
        if (IsBadReadPtr((void*)base, sizeof(void*))) {
            break;
        }

        auto data = *(ID3D12CommandQueue**)base;

        if (data == command_queue) {
            m_command_queue_offset = i;
            spdlog::info("Found command queue offset: {:x}", i);
            break;
        }
    }

    auto target_swapchain = swap_chain;

    // Scan throughout the swapchain for a valid pointer to scan through
    // this is usually only necessary for Proton
    if (m_command_queue_offset == 0) {
        bool should_break = false;

        for (auto base = 0; base < 512 * sizeof(void*); base += sizeof(void*)) {
            const auto pre_scan_base = (uintptr_t)swap_chain1 + base;

            // reached the end
            if (IsBadReadPtr((void*)pre_scan_base, sizeof(void*))) {
                break;
            }

            const auto scan_base = *(uintptr_t*)pre_scan_base;

            if (scan_base == 0 || IsBadReadPtr((void*)scan_base, sizeof(void*))) {
                continue;
            }

            for (auto i = 0; i < 512 * sizeof(void*); i += sizeof(void*)) {
                const auto pre_data = scan_base + i;

                if (IsBadReadPtr((void*)pre_data, sizeof(void*))) {
                    break;
                }

                auto data = *(ID3D12CommandQueue**)pre_data;

                if (data == command_queue) {
                    // If we hook Streamline's Swapchain, the menu fails to render correctly/flickers
                    // So we switch out the swapchain with the internal one owned by Streamline
                    // Side note: Even though we are scanning for Proton here,
                    // this doubles as an offset scanner for the real swapchain inside Streamline (or FSR3)
                    if (m_using_frame_generation_swapchain) {
                        target_swapchain = (IDXGISwapChain3*)scan_base;
                    }

                    if (!m_using_frame_generation_swapchain) {
                        m_using_proton_swapchain = true;
                    }

                    m_command_queue_offset = i;
                    m_proton_swapchain_offset = base;
                    should_break = true;

                    spdlog::info("Proton potentially detected");
                    spdlog::info("Found command queue offset: {:x}", i);
                    break;
                }
            }

            if (m_using_proton_swapchain || should_break) {
                break;
            }
        }
    }

    if (m_command_queue_offset == 0) {
        spdlog::error("Failed to find command queue offset");
        return false;
    }

    try {
        spdlog::info("Initializing hooks");
        m_present_hook.reset();
        m_present1_hook.reset();
        m_create_graphics_pipeline_state_hooks.clear();
        m_create_compute_pipeline_state_hooks.clear();
        m_create_command_list_hooks.clear();
        m_create_command_list1_hooks.clear();
        m_create_command_signature_hooks.clear();
        m_create_pipeline_state_hooks.clear();
        m_create_constant_buffer_view_hooks.clear();
        m_create_render_target_view_hooks.clear();
        m_create_depth_stencil_view_hooks.clear();
        m_create_shader_resource_view_hooks.clear();
        m_set_pipeline_state_hooks.clear();
        m_command_list_diagnostic_hooks.clear();
        m_create_graphics_pipeline_state_hook_lookup.clear();
        m_create_compute_pipeline_state_hook_lookup.clear();
        m_create_command_list_hook_lookup.clear();
        m_create_command_list1_hook_lookup.clear();
        m_create_command_signature_hook_lookup.clear();
        m_create_pipeline_state_hook_lookup.clear();
        m_create_constant_buffer_view_hook_lookup.clear();
        m_create_render_target_view_hook_lookup.clear();
        m_create_depth_stencil_view_hook_lookup.clear();
        m_create_shader_resource_view_hook_lookup.clear();
        m_set_pipeline_state_hook_lookup.clear();
        m_command_list_diagnostic_hook_lookup.clear();
        m_set_pipeline_state_slots.clear();
        m_command_list_diagnostic_slots.clear();
        m_swapchain_hook.reset();

        m_is_phase_1 = true;

        auto& present_fn = (*(void***)target_swapchain)[8]; // Present
        auto& present1_fn = (*(void***)target_swapchain)[22]; // Present1
        m_present_hook = std::make_unique<PointerHook>(&present_fn, (void*)&D3D12Hook::present);
        m_present1_hook = std::make_unique<PointerHook>(&present1_fn, (void*)&D3D12Hook::present1);

        std::unordered_set<uintptr_t> graphics_pipeline_state_slots{};
        std::unordered_set<uintptr_t> compute_pipeline_state_slots{};
        std::unordered_set<uintptr_t> create_command_list_slots{};
        std::unordered_set<uintptr_t> create_command_list1_slots{};
        std::unordered_set<uintptr_t> create_command_signature_slots{};
        std::unordered_set<uintptr_t> pipeline_state_stream_slots{};
        std::unordered_set<uintptr_t> constant_buffer_view_slots{};
        std::unordered_set<uintptr_t> render_target_view_slots{};
        std::unordered_set<uintptr_t> depth_stencil_view_slots{};
        std::unordered_set<uintptr_t> shader_resource_view_slots{};
        std::unordered_set<uintptr_t> unordered_access_view_slots{};
        std::unordered_set<uintptr_t> copy_descriptors_simple_slots{};
        std::unordered_set<uintptr_t> copy_descriptors_slots{};
        const bool sn2_hooks_enabled = is_subnautica2_process();

        add_unique_pointer_hook(
            device,
            CREATE_GRAPHICS_PIPELINE_STATE_VTABLE_INDEX,
            reinterpret_cast<void*>(&D3D12Hook::create_graphics_pipeline_state),
            m_create_graphics_pipeline_state_hooks,
            m_create_graphics_pipeline_state_hook_lookup,
            graphics_pipeline_state_slots
        );

        add_unique_pointer_hook(
            device,
            CREATE_COMPUTE_PIPELINE_STATE_VTABLE_INDEX,
            reinterpret_cast<void*>(&D3D12Hook::create_compute_pipeline_state),
            m_create_compute_pipeline_state_hooks,
            m_create_compute_pipeline_state_hook_lookup,
            compute_pipeline_state_slots
        );

        add_unique_pointer_hook(
            device,
            CREATE_COMMAND_LIST_VTABLE_INDEX,
            reinterpret_cast<void*>(&D3D12Hook::create_command_list),
            m_create_command_list_hooks,
            m_create_command_list_hook_lookup,
            create_command_list_slots
        );

        Microsoft::WRL::ComPtr<ID3D12Device1> device1{};
        Microsoft::WRL::ComPtr<ID3D12Device2> device2{};
        Microsoft::WRL::ComPtr<ID3D12Device3> device3{};
        Microsoft::WRL::ComPtr<ID3D12Device4> device4{};
        Microsoft::WRL::ComPtr<ID3D12Device5> device5{};
        Microsoft::WRL::ComPtr<ID3D12Device6> device6{};
        Microsoft::WRL::ComPtr<ID3D12Device7> device7{};
        Microsoft::WRL::ComPtr<ID3D12Device8> device8{};
        Microsoft::WRL::ComPtr<ID3D12Device9> device9{};
        Microsoft::WRL::ComPtr<ID3D12Device10> device10{};

        device->QueryInterface(IID_PPV_ARGS(&device1));
        device->QueryInterface(IID_PPV_ARGS(&device2));
        device->QueryInterface(IID_PPV_ARGS(&device3));
        device->QueryInterface(IID_PPV_ARGS(&device4));
        device->QueryInterface(IID_PPV_ARGS(&device5));
        device->QueryInterface(IID_PPV_ARGS(&device6));
        device->QueryInterface(IID_PPV_ARGS(&device7));
        device->QueryInterface(IID_PPV_ARGS(&device8));
        device->QueryInterface(IID_PPV_ARGS(&device9));
        device->QueryInterface(IID_PPV_ARGS(&device10));

        const std::array<IUnknown*, 10> device_interfaces{
            device1.Get(),
            device2.Get(),
            device3.Get(),
            device4.Get(),
            device5.Get(),
            device6.Get(),
            device7.Get(),
            device8.Get(),
            device9.Get(),
            device10.Get()
        };

        for (auto* iface : device_interfaces) {
            add_unique_pointer_hook(
                iface,
                CREATE_GRAPHICS_PIPELINE_STATE_VTABLE_INDEX,
                reinterpret_cast<void*>(&D3D12Hook::create_graphics_pipeline_state),
                m_create_graphics_pipeline_state_hooks,
                m_create_graphics_pipeline_state_hook_lookup,
                graphics_pipeline_state_slots
            );

            add_unique_pointer_hook(
                iface,
                CREATE_COMPUTE_PIPELINE_STATE_VTABLE_INDEX,
                reinterpret_cast<void*>(&D3D12Hook::create_compute_pipeline_state),
                m_create_compute_pipeline_state_hooks,
                m_create_compute_pipeline_state_hook_lookup,
                compute_pipeline_state_slots
            );

            add_unique_pointer_hook(
                iface,
                CREATE_COMMAND_LIST_VTABLE_INDEX,
                reinterpret_cast<void*>(&D3D12Hook::create_command_list),
                m_create_command_list_hooks,
                m_create_command_list_hook_lookup,
                create_command_list_slots
            );

            add_unique_pointer_hook(
                iface,
                CREATE_RENDER_TARGET_VIEW_VTABLE_INDEX,
                reinterpret_cast<void*>(&D3D12Hook::create_render_target_view),
                m_create_render_target_view_hooks,
                m_create_render_target_view_hook_lookup,
                render_target_view_slots
            );

            if (sn2_hooks_enabled) {
                add_unique_pointer_hook(
                    iface,
                    CREATE_CONSTANT_BUFFER_VIEW_VTABLE_INDEX,
                    reinterpret_cast<void*>(&D3D12Hook::create_constant_buffer_view),
                    m_create_constant_buffer_view_hooks,
                    m_create_constant_buffer_view_hook_lookup,
                    constant_buffer_view_slots
                );

                add_unique_pointer_hook(
                    iface,
                    CREATE_SHADER_RESOURCE_VIEW_VTABLE_INDEX,
                    reinterpret_cast<void*>(&D3D12Hook::create_shader_resource_view),
                    m_create_shader_resource_view_hooks,
                    m_create_shader_resource_view_hook_lookup,
                    shader_resource_view_slots
                );

                add_unique_pointer_hook(
                    iface,
                    CREATE_UNORDERED_ACCESS_VIEW_VTABLE_INDEX,
                    reinterpret_cast<void*>(&D3D12Hook::create_unordered_access_view),
                    m_create_unordered_access_view_hooks,
                    m_create_unordered_access_view_hook_lookup,
                    unordered_access_view_slots
                );

                add_unique_pointer_hook(
                    iface,
                    COPY_DESCRIPTORS_SIMPLE_VTABLE_INDEX,
                    reinterpret_cast<void*>(&D3D12Hook::copy_descriptors_simple),
                    m_copy_descriptors_simple_hooks,
                    m_copy_descriptors_simple_hook_lookup,
                    copy_descriptors_simple_slots
                );

                add_unique_pointer_hook(
                    iface,
                    COPY_DESCRIPTORS_VTABLE_INDEX,
                    reinterpret_cast<void*>(&D3D12Hook::copy_descriptors),
                    m_copy_descriptors_hooks,
                    m_copy_descriptors_hook_lookup,
                    copy_descriptors_slots
                );

                add_unique_pointer_hook(
                    iface,
                    CREATE_COMMAND_SIGNATURE_VTABLE_INDEX,
                    reinterpret_cast<void*>(&D3D12Hook::create_command_signature),
                    m_create_command_signature_hooks,
                    m_create_command_signature_hook_lookup,
                    create_command_signature_slots
                );
            }

            add_unique_pointer_hook(
                iface,
                CREATE_DEPTH_STENCIL_VIEW_VTABLE_INDEX,
                reinterpret_cast<void*>(&D3D12Hook::create_depth_stencil_view),
                m_create_depth_stencil_view_hooks,
                m_create_depth_stencil_view_hook_lookup,
                depth_stencil_view_slots
            );
        }

        const std::array<IUnknown*, 9> pipeline_stream_interfaces{
            device2.Get(),
            device3.Get(),
            device4.Get(),
            device5.Get(),
            device6.Get(),
            device7.Get(),
            device8.Get(),
            device9.Get(),
            device10.Get()
        };

        for (auto* iface : pipeline_stream_interfaces) {
            add_unique_pointer_hook(
                iface,
                CREATE_PIPELINE_STATE_VTABLE_INDEX,
                reinterpret_cast<void*>(&D3D12Hook::create_pipeline_state),
                m_create_pipeline_state_hooks,
                m_create_pipeline_state_hook_lookup,
                pipeline_state_stream_slots
            );
        }

        const std::array<IUnknown*, 7> command_list1_device_interfaces{
            device4.Get(),
            device5.Get(),
            device6.Get(),
            device7.Get(),
            device8.Get(),
            device9.Get(),
            device10.Get()
        };

        for (auto* iface : command_list1_device_interfaces) {
            add_unique_pointer_hook(
                iface,
                CREATE_COMMAND_LIST1_VTABLE_INDEX,
                reinterpret_cast<void*>(&D3D12Hook::create_command_list1),
                m_create_command_list1_hooks,
                m_create_command_list1_hook_lookup,
                create_command_list1_slots
            );
        }

        const bool command_list_diagnostics_enabled = enable_d3d12_diagnostic_command_list_hooks();

        // Self-test: log all env vars that gate Shader Hunter behavior so we
        // can verify they actually reached the game process.
        {
            auto read = [](const char* name) {
                return env_value_a(name);
            };
            spdlog::info("[D3D12] Env vars: UEVR_DISABLE_SN2_HOOKS={} | UEVR_SN2_DIAG_CLEAN={} | UEVR_SN2_DIAG_STRICT={} | UEVR_SN2_PSO3069_DIAG={} | UEVR_ENABLE_D3D12_DIAGNOSTIC_COMMAND_LIST_HOOKS={} | UEVR_ENABLE_D3D12_DESCRIPTOR_TABLE_HOOK={} | UEVR_SUBNAUTICA2_ENABLE_FOG_COMPUTE_BIND_HOOK={} | UEVR_SN2_EI_DIAG={} | UEVR_SN2_PSO3069_CAPTURE_MARK_FILE={} | UEVR_DXC_PATH={} | UEVR_SHADER_HUNTER_PRETRACK={} | UEVR_SHADER_HUNTER_SUPPRESSION_BLOCKLIST={} | UEVR_SHADER_HUNTER_SKIP_LEFT_ONLY={} | UEVR_SHADER_HUNTER_SKIP_RIGHT_ONLY={}",
                read("UEVR_DISABLE_SN2_HOOKS"),
                read("UEVR_SN2_DIAG_CLEAN"),
                read("UEVR_SN2_DIAG_STRICT"),
                read("UEVR_SN2_PSO3069_DIAG"),
                read("UEVR_ENABLE_D3D12_DIAGNOSTIC_COMMAND_LIST_HOOKS"),
                read("UEVR_ENABLE_D3D12_DESCRIPTOR_TABLE_HOOK"),
                read("UEVR_SUBNAUTICA2_ENABLE_FOG_COMPUTE_BIND_HOOK"),
                read("UEVR_SN2_EI_DIAG"),
                read("UEVR_SN2_PSO3069_CAPTURE_MARK_FILE"),
                read("UEVR_DXC_PATH"),
                read("UEVR_SHADER_HUNTER_PRETRACK"),
                read("UEVR_SHADER_HUNTER_SUPPRESSION_BLOCKLIST"),
                read("UEVR_SHADER_HUNTER_SKIP_LEFT_ONLY"),
                read("UEVR_SHADER_HUNTER_SKIP_RIGHT_ONLY"));
            spdlog::info("[D3D12] is_subnautica2_process()={} command_list_diagnostics_enabled={}",
                is_subnautica2_process(), command_list_diagnostics_enabled);
            if (is_subnautica2_process() && (sn2_pso3069_diag_enabled() || sn2_diag_strict_enabled())) {
                auto warn_enabled = [&](const char* name) {
                    const auto value = read(name);
                    if (value != "(unset)" && value != "0" && value != "false" && value != "FALSE" && value != "off" && value != "OFF") {
                        SPDLOG_WARN("[SN2-DIAG-WARN] mutating_or_contaminating_env {}={}", name, value);
                    }
                };
                if (sn2_diag_strict_enabled() && !env_flag_enabled_a("UEVR_SN2_DIAG_CLEAN")) {
                    SPDLOG_WARN("[SN2-DIAG-WARN] UEVR_SN2_DIAG_STRICT=1 is promoting clean diagnostic behavior even though UEVR_SN2_DIAG_CLEAN is not set");
                } else if (!sn2_diag_clean_enabled()) {
                    SPDLOG_WARN("[SN2-DIAG-WARN] capture is not clean; set UEVR_SN2_DIAG_CLEAN=1 or UEVR_SN2_DIAG_STRICT=1");
                }
                warn_enabled("UEVR_SHADER_HUNTER_SKIP_LEFT_ONLY");
                warn_enabled("UEVR_SHADER_HUNTER_SKIP_RIGHT_ONLY");
                warn_enabled("UEVR_SHADER_HUNTER_KILL_LEFT_EYE");
                warn_enabled("UEVR_SHADER_HUNTER_KILL_RIGHT_EYE");
                warn_enabled("UEVR_SN2_FORCE_LEFT_CB0");
                warn_enabled("UEVR_SN2_FOG_SRV_REDIRECT");
                warn_enabled("UEVR_SN2_RIGHT_ARG_SUBSTITUTE");
                warn_enabled("UEVR_SN2_SKYATMOS_SKIP_RIGHT");
                warn_enabled("UEVR_SN2_SKYATMOS_CB_REDIRECT");
                warn_enabled("UEVR_SN2_SKYATMOS_RIGHT_TABLE_SWAP");
                warn_enabled("UEVR_SUBNAUTICA2_MIRROR_LEFT_TO_RIGHT_EYE");
            }
        }

        if (!command_list_diagnostics_enabled) {
            spdlog::info("[D3D12] Command-list diagnostic hooks disabled; set UEVR_ENABLE_D3D12_DIAGNOSTIC_COMMAND_LIST_HOOKS=1 to enable draw/viewport/barrier tracing");
        }

        install_command_list_hooks(command_list);

        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList1> command_list1{};
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> command_list2{};
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList3> command_list3{};
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> command_list4{};
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList5> command_list5{};
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList6> command_list6{};
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7> command_list7{};

        command_list->QueryInterface(IID_PPV_ARGS(&command_list1));
        command_list->QueryInterface(IID_PPV_ARGS(&command_list2));
        command_list->QueryInterface(IID_PPV_ARGS(&command_list3));
        command_list->QueryInterface(IID_PPV_ARGS(&command_list4));
        command_list->QueryInterface(IID_PPV_ARGS(&command_list5));
        command_list->QueryInterface(IID_PPV_ARGS(&command_list6));
        command_list->QueryInterface(IID_PPV_ARGS(&command_list7));

        const std::array<IUnknown*, 7> command_list_interfaces{
            command_list1.Get(),
            command_list2.Get(),
            command_list3.Get(),
            command_list4.Get(),
            command_list5.Get(),
            command_list6.Get(),
            command_list7.Get()
        };

        for (auto* iface : command_list_interfaces) {
            install_command_list_hooks_from_unknown(iface);
        }

        m_hooked = true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to initialize hooks: {}", e.what());
        m_hooked = false;
    }

    if (command_list != nullptr) {
        command_list->Release();
    }

    if (command_allocator != nullptr) {
        command_allocator->Release();
    }

    device->Release();
    command_queue->Release();
    factory->Release();
    swap_chain1->Release();
    swap_chain->Release();

    if (hwnd) {
        ::DestroyWindow(hwnd);
    }

    if (wc.lpszClassName != nullptr) {
        ::UnregisterClass(wc.lpszClassName, wc.hInstance);
    }

    return m_hooked;
}

bool D3D12Hook::unhook() {
    if (!m_hooked) {
        return true;
    }

    spdlog::info("Unhooking D3D12");

    m_present_hook.reset();
    m_present1_hook.reset();
    m_create_graphics_pipeline_state_hooks.clear();
    m_create_compute_pipeline_state_hooks.clear();
    m_create_command_list_hooks.clear();
    m_create_command_list1_hooks.clear();
    m_create_command_signature_hooks.clear();
    m_create_pipeline_state_hooks.clear();
    m_create_render_target_view_hooks.clear();
    m_create_depth_stencil_view_hooks.clear();
    m_set_pipeline_state_hooks.clear();
    m_command_list_diagnostic_hooks.clear();
    m_create_graphics_pipeline_state_hook_lookup.clear();
    m_create_compute_pipeline_state_hook_lookup.clear();
    m_create_command_list_hook_lookup.clear();
    m_create_command_list1_hook_lookup.clear();
    m_create_command_signature_hook_lookup.clear();
    m_create_pipeline_state_hook_lookup.clear();
    m_create_render_target_view_hook_lookup.clear();
    m_create_depth_stencil_view_hook_lookup.clear();
    m_set_pipeline_state_hook_lookup.clear();
    m_command_list_diagnostic_hook_lookup.clear();
    m_set_pipeline_state_slots.clear();
    m_command_list_diagnostic_slots.clear();
    m_swapchain_hook.reset();

    m_hooked = false;
    m_is_phase_1 = true;

    return true;
}

void D3D12Hook::install_command_list_hooks_from_unknown(IUnknown* command_list) {
    if (command_list == nullptr) {
        return;
    }

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> graphics_command_list{};
    if (SUCCEEDED(command_list->QueryInterface(IID_PPV_ARGS(&graphics_command_list)))) {
        install_command_list_hooks(graphics_command_list.Get());
    }
}

void D3D12Hook::install_command_list_hooks(ID3D12GraphicsCommandList* command_list) {
    if (command_list == nullptr) {
        return;
    }

    std::scoped_lock lock{m_command_list_hook_mutex};

    auto install_for_interface = [this](IUnknown* iface) {
        if (iface == nullptr) {
            return;
        }

        add_unique_pointer_hook(
            iface,
            SET_PIPELINE_STATE_VTABLE_INDEX,
            reinterpret_cast<void*>(&D3D12Hook::set_pipeline_state),
            m_set_pipeline_state_hooks,
            m_set_pipeline_state_hook_lookup,
            m_set_pipeline_state_slots
        );

        if (!enable_d3d12_diagnostic_command_list_hooks()) {
            return;
        }

        add_unique_pointer_hook(
            iface,
            CLOSE_VTABLE_INDEX,
            reinterpret_cast<void*>(&D3D12Hook::close_command_list),
            m_command_list_diagnostic_hooks,
            m_command_list_diagnostic_hook_lookup,
            m_command_list_diagnostic_slots
        );

        add_unique_pointer_hook(
            iface,
            RESET_VTABLE_INDEX,
            reinterpret_cast<void*>(&D3D12Hook::reset_command_list),
            m_command_list_diagnostic_hooks,
            m_command_list_diagnostic_hook_lookup,
            m_command_list_diagnostic_slots
        );

        add_unique_pointer_hook(
            iface,
            CLEAR_STATE_VTABLE_INDEX,
            reinterpret_cast<void*>(&D3D12Hook::clear_state),
            m_command_list_diagnostic_hooks,
            m_command_list_diagnostic_hook_lookup,
            m_command_list_diagnostic_slots
        );

        add_unique_pointer_hook(
            iface,
            DRAW_INSTANCED_VTABLE_INDEX,
            reinterpret_cast<void*>(&D3D12Hook::draw_instanced),
            m_command_list_diagnostic_hooks,
            m_command_list_diagnostic_hook_lookup,
            m_command_list_diagnostic_slots
        );

        add_unique_pointer_hook(
            iface,
            DRAW_INDEXED_INSTANCED_VTABLE_INDEX,
            reinterpret_cast<void*>(&D3D12Hook::draw_indexed_instanced),
            m_command_list_diagnostic_hooks,
            m_command_list_diagnostic_hook_lookup,
            m_command_list_diagnostic_slots
        );

        add_unique_pointer_hook(
            iface,
            DISPATCH_VTABLE_INDEX,
            reinterpret_cast<void*>(&D3D12Hook::dispatch),
            m_command_list_diagnostic_hooks,
            m_command_list_diagnostic_hook_lookup,
            m_command_list_diagnostic_slots
        );

        add_unique_pointer_hook(
            iface,
            EXECUTE_BUNDLE_VTABLE_INDEX,
            reinterpret_cast<void*>(&D3D12Hook::execute_bundle),
            m_command_list_diagnostic_hooks,
            m_command_list_diagnostic_hook_lookup,
            m_command_list_diagnostic_slots
        );

        add_unique_pointer_hook(
            iface,
            EXECUTE_INDIRECT_VTABLE_INDEX,
            reinterpret_cast<void*>(&D3D12Hook::execute_indirect),
            m_command_list_diagnostic_hooks,
            m_command_list_diagnostic_hook_lookup,
            m_command_list_diagnostic_slots
        );

        add_unique_pointer_hook(
            iface,
            RS_SET_VIEWPORTS_VTABLE_INDEX,
            reinterpret_cast<void*>(&D3D12Hook::rs_set_viewports),
            m_command_list_diagnostic_hooks,
            m_command_list_diagnostic_hook_lookup,
            m_command_list_diagnostic_slots
        );

        add_unique_pointer_hook(
            iface,
            RESOURCE_BARRIER_VTABLE_INDEX,
            reinterpret_cast<void*>(&D3D12Hook::resource_barrier),
            m_command_list_diagnostic_hooks,
            m_command_list_diagnostic_hook_lookup,
            m_command_list_diagnostic_slots
        );

        add_unique_pointer_hook(
            iface,
            OM_SET_RENDER_TARGETS_VTABLE_INDEX,
            reinterpret_cast<void*>(&D3D12Hook::om_set_render_targets),
            m_command_list_diagnostic_hooks,
            m_command_list_diagnostic_hook_lookup,
            m_command_list_diagnostic_slots
        );

        add_unique_pointer_hook(
            iface,
            CLEAR_RENDER_TARGET_VIEW_VTABLE_INDEX,
            reinterpret_cast<void*>(&D3D12Hook::clear_render_target_view),
            m_command_list_diagnostic_hooks,
            m_command_list_diagnostic_hook_lookup,
            m_command_list_diagnostic_slots
        );

        if (enable_d3d12_descriptor_table_hook()) {
            add_unique_pointer_hook(
                iface,
                SET_GRAPHICS_ROOT_DESCRIPTOR_TABLE_VTABLE_INDEX,
                reinterpret_cast<void*>(&D3D12Hook::set_graphics_root_descriptor_table),
                m_command_list_diagnostic_hooks,
                m_command_list_diagnostic_hook_lookup,
                m_command_list_diagnostic_slots
            );
        }

        if (is_subnautica2_process()) {
            add_unique_pointer_hook(
                iface,
                SET_GRAPHICS_ROOT_CONSTANT_BUFFER_VIEW_VTABLE_INDEX,
                reinterpret_cast<void*>(&D3D12Hook::set_graphics_root_constant_buffer_view),
                m_command_list_diagnostic_hooks,
                m_command_list_diagnostic_hook_lookup,
                m_command_list_diagnostic_slots
            );
        }

        if (enable_d3d12_compute_root_table_hook() || sn2_pso3069_diag_enabled()) {
            add_unique_pointer_hook(
                iface,
                SET_DESCRIPTOR_HEAPS_VTABLE_INDEX,
                reinterpret_cast<void*>(&D3D12Hook::set_descriptor_heaps),
                m_command_list_diagnostic_hooks,
                m_command_list_diagnostic_hook_lookup,
                m_command_list_diagnostic_slots
            );
        }

        if (enable_d3d12_compute_root_table_hook()) {
            add_unique_pointer_hook(
                iface,
                SET_COMPUTE_ROOT_DESCRIPTOR_TABLE_VTABLE_INDEX,
                reinterpret_cast<void*>(&D3D12Hook::set_compute_root_descriptor_table),
                m_command_list_diagnostic_hooks,
                m_command_list_diagnostic_hook_lookup,
                m_command_list_diagnostic_slots
            );
        }
    };

    install_for_interface(command_list);

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList1> command_list1{};
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> command_list2{};
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList3> command_list3{};
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> command_list4{};
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList5> command_list5{};
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList6> command_list6{};
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7> command_list7{};

    command_list->QueryInterface(IID_PPV_ARGS(&command_list1));
    command_list->QueryInterface(IID_PPV_ARGS(&command_list2));
    command_list->QueryInterface(IID_PPV_ARGS(&command_list3));
    command_list->QueryInterface(IID_PPV_ARGS(&command_list4));
    command_list->QueryInterface(IID_PPV_ARGS(&command_list5));
    command_list->QueryInterface(IID_PPV_ARGS(&command_list6));
    command_list->QueryInterface(IID_PPV_ARGS(&command_list7));

    const std::array<IUnknown*, 7> command_list_interfaces{
        command_list1.Get(),
        command_list2.Get(),
        command_list3.Get(),
        command_list4.Get(),
        command_list5.Get(),
        command_list6.Get(),
        command_list7.Get()
    };

    for (auto* iface : command_list_interfaces) {
        install_for_interface(iface);
    }

    if (enable_d3d12_diagnostic_command_list_hooks()) {
        const std::array<IUnknown*, 2> mesh_command_list_interfaces{
            command_list6.Get(),
            command_list7.Get()
        };

        for (auto* iface : mesh_command_list_interfaces) {
            add_unique_pointer_hook(
                iface,
                DISPATCH_MESH_VTABLE_INDEX,
                reinterpret_cast<void*>(&D3D12Hook::dispatch_mesh),
                m_command_list_diagnostic_hooks,
                m_command_list_diagnostic_hook_lookup,
                m_command_list_diagnostic_slots
            );
        }
    }
}

PointerHook* D3D12Hook::find_create_graphics_pipeline_state_hook(void* slot) const {
    if (const auto it = m_create_graphics_pipeline_state_hook_lookup.find(reinterpret_cast<uintptr_t>(slot)); it != m_create_graphics_pipeline_state_hook_lookup.end()) {
        return it->second;
    }

    return m_create_graphics_pipeline_state_hooks.empty() ? nullptr : m_create_graphics_pipeline_state_hooks.front().get();
}

PointerHook* D3D12Hook::find_create_compute_pipeline_state_hook(void* slot) const {
    if (const auto it = m_create_compute_pipeline_state_hook_lookup.find(reinterpret_cast<uintptr_t>(slot)); it != m_create_compute_pipeline_state_hook_lookup.end()) {
        return it->second;
    }

    return m_create_compute_pipeline_state_hooks.empty() ? nullptr : m_create_compute_pipeline_state_hooks.front().get();
}

PointerHook* D3D12Hook::find_create_command_list_hook(void* slot) const {
    if (const auto it = m_create_command_list_hook_lookup.find(reinterpret_cast<uintptr_t>(slot)); it != m_create_command_list_hook_lookup.end()) {
        return it->second;
    }

    return m_create_command_list_hooks.empty() ? nullptr : m_create_command_list_hooks.front().get();
}

PointerHook* D3D12Hook::find_create_command_list1_hook(void* slot) const {
    if (const auto it = m_create_command_list1_hook_lookup.find(reinterpret_cast<uintptr_t>(slot)); it != m_create_command_list1_hook_lookup.end()) {
        return it->second;
    }

    return m_create_command_list1_hooks.empty() ? nullptr : m_create_command_list1_hooks.front().get();
}

PointerHook* D3D12Hook::find_create_command_signature_hook(void* slot) const {
    if (const auto it = m_create_command_signature_hook_lookup.find(reinterpret_cast<uintptr_t>(slot)); it != m_create_command_signature_hook_lookup.end()) {
        return it->second;
    }

    return m_create_command_signature_hooks.empty() ? nullptr : m_create_command_signature_hooks.front().get();
}

PointerHook* D3D12Hook::find_create_pipeline_state_hook(void* slot) const {
    if (const auto it = m_create_pipeline_state_hook_lookup.find(reinterpret_cast<uintptr_t>(slot)); it != m_create_pipeline_state_hook_lookup.end()) {
        return it->second;
    }

    return m_create_pipeline_state_hooks.empty() ? nullptr : m_create_pipeline_state_hooks.front().get();
}

PointerHook* D3D12Hook::find_create_constant_buffer_view_hook(void* slot) const {
    if (const auto it = m_create_constant_buffer_view_hook_lookup.find(reinterpret_cast<uintptr_t>(slot)); it != m_create_constant_buffer_view_hook_lookup.end()) {
        return it->second;
    }

    return m_create_constant_buffer_view_hooks.empty() ? nullptr : m_create_constant_buffer_view_hooks.front().get();
}

PointerHook* D3D12Hook::find_create_render_target_view_hook(void* slot) const {
    if (const auto it = m_create_render_target_view_hook_lookup.find(reinterpret_cast<uintptr_t>(slot)); it != m_create_render_target_view_hook_lookup.end()) {
        return it->second;
    }

    return m_create_render_target_view_hooks.empty() ? nullptr : m_create_render_target_view_hooks.front().get();
}

PointerHook* D3D12Hook::find_create_depth_stencil_view_hook(void* slot) const {
    if (const auto it = m_create_depth_stencil_view_hook_lookup.find(reinterpret_cast<uintptr_t>(slot)); it != m_create_depth_stencil_view_hook_lookup.end()) {
        return it->second;
    }

    return m_create_depth_stencil_view_hooks.empty() ? nullptr : m_create_depth_stencil_view_hooks.front().get();
}

PointerHook* D3D12Hook::find_create_shader_resource_view_hook(void* slot) const {
    if (const auto it = m_create_shader_resource_view_hook_lookup.find(reinterpret_cast<uintptr_t>(slot)); it != m_create_shader_resource_view_hook_lookup.end()) {
        return it->second;
    }
    return m_create_shader_resource_view_hooks.empty() ? nullptr : m_create_shader_resource_view_hooks.front().get();
}

PointerHook* D3D12Hook::find_create_unordered_access_view_hook(void* slot) const {
    if (const auto it = m_create_unordered_access_view_hook_lookup.find(reinterpret_cast<uintptr_t>(slot)); it != m_create_unordered_access_view_hook_lookup.end()) {
        return it->second;
    }
    return m_create_unordered_access_view_hooks.empty() ? nullptr : m_create_unordered_access_view_hooks.front().get();
}

PointerHook* D3D12Hook::find_copy_descriptors_simple_hook(void* slot) const {
    if (const auto it = m_copy_descriptors_simple_hook_lookup.find(reinterpret_cast<uintptr_t>(slot)); it != m_copy_descriptors_simple_hook_lookup.end()) {
        return it->second;
    }
    return m_copy_descriptors_simple_hooks.empty() ? nullptr : m_copy_descriptors_simple_hooks.front().get();
}

PointerHook* D3D12Hook::find_copy_descriptors_hook(void* slot) const {
    if (const auto it = m_copy_descriptors_hook_lookup.find(reinterpret_cast<uintptr_t>(slot)); it != m_copy_descriptors_hook_lookup.end()) {
        return it->second;
    }
    return m_copy_descriptors_hooks.empty() ? nullptr : m_copy_descriptors_hooks.front().get();
}

PointerHook* D3D12Hook::find_set_pipeline_state_hook(void* slot) const {
    if (const auto it = m_set_pipeline_state_hook_lookup.find(reinterpret_cast<uintptr_t>(slot)); it != m_set_pipeline_state_hook_lookup.end()) {
        return it->second;
    }

    return m_set_pipeline_state_hooks.empty() ? nullptr : m_set_pipeline_state_hooks.front().get();
}

PointerHook* D3D12Hook::find_command_list_diagnostic_hook(void* slot) const {
    if (slot == nullptr) {
        return nullptr;
    }

    if (const auto it = m_command_list_diagnostic_hook_lookup.find(reinterpret_cast<uintptr_t>(slot)); it != m_command_list_diagnostic_hook_lookup.end()) {
        return it->second;
    }

    return nullptr;
}

thread_local int32_t g_present_depth = 0;

// 2026-05-18 SN2 SkyAtmos brute-force memory scanner.
// PSO 2993's view-1 cb0 has bytes 480..511 = 8 consecutive 1.0f. This is a
// distinctive pattern (8 consecutive 0x3F800000 at 16-byte alignment is rare
// in normal cbuffer data). Scan all CPU-writable committed memory pages
// per frame and overwrite occurrences with (0,0,0,1.0, 0,0,0,1.0) — the
// correct view-0 pattern.
//
// Gated by env UEVR_SN2_SKY_ATMOS_BYTEFIX = 1.
// Throttled: only scans 1 frame in N (controlled by env, default every frame).
namespace sn2_sky_atmos_scan {
    static const bool g_enabled = []() {
        const char* e = std::getenv("UEVR_SN2_SKY_ATMOS_BYTEFIX");
        return e != nullptr && e[0] == '1';
    }();

    static uintptr_t g_min_addr = 0;
    static uintptr_t g_max_addr = 0;

    void init_bounds() {
        if (g_min_addr != 0) return;
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        g_min_addr = (uintptr_t)si.lpMinimumApplicationAddress;
        g_max_addr = (uintptr_t)si.lpMaximumApplicationAddress;
    }

    // Cache of known-bad addresses found by the brute scan. Once populated,
    // subsequent calls just re-check these specific addresses (constant time).
    static std::mutex g_cache_mutex;
    static std::vector<uint8_t*> g_known_bad_addrs;

    // Returns true if [p..p+32) is the BAD 8x1.0f pattern surrounded by
    // zero-prefix and non-1.0f suffix (cb0 row 30/31 signature).
    static inline bool matches_bad_cb0_tail(const uint8_t* p) {
        const uint32_t one = 0x3F800000u;
        const uint32_t* prev = reinterpret_cast<const uint32_t*>(p - 16);
        if (prev[0] != 0 || prev[1] != 0 || prev[2] != 0 || prev[3] != 0) return false;
        const uint32_t* w = reinterpret_cast<const uint32_t*>(p);
        if (!(w[0] == one && w[1] == one && w[2] == one && w[3] == one &&
              w[4] == one && w[5] == one && w[6] == one && w[7] == one)) return false;
        const uint32_t* nxt = reinterpret_cast<const uint32_t*>(p + 32);
        if (nxt[0] == one && nxt[1] == one && nxt[2] == one && nxt[3] == one) return false;
        return true;
    }

    static inline void patch_at(uint8_t* p) {
        const uint32_t one = 0x3F800000u;
        uint32_t* mut = reinterpret_cast<uint32_t*>(p);
        mut[0] = 0; mut[1] = 0; mut[2] = 0; mut[3] = one;
        mut[4] = 0; mut[5] = 0; mut[6] = 0; mut[7] = one;
    }

    // SEH-safe helper (no C++ unwind required because no C++ objects in scope).
    static uint64_t try_repatch_one(uint8_t* p) {
        __try {
            if (matches_bad_cb0_tail(p)) { patch_at(p); return 1; }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return 0;
    }

    static void try_scan_region(const uint8_t* base, const uint8_t* end, std::vector<uint8_t*>& found) {
        __try {
            for (const uint8_t* p = base + 16; p <= end; p += 16) {
                if (matches_bad_cb0_tail(p)) {
                    patch_at(const_cast<uint8_t*>(p));
                    found.push_back(const_cast<uint8_t*>(p));
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void scan_and_patch_uploads() {
        if (!g_enabled) return;
        // PRIMARY FAST PATH (2026-05-18): launch the tracked-buffer scan on a
        // detached background thread so Present is never blocked.
        // The scan patches CPU-side bytes; GPU reads them next frame regardless
        // of whether we finished by THIS Present.
        if (sn2_upload_buf_map::entry_count() > 0) {
            static std::atomic<bool> scan_running{false};
            bool expected = false;
            if (scan_running.compare_exchange_strong(expected, true)) {
                std::thread([](){
                    const uint64_t patches = sn2_upload_buf_map::scan_tracked_buffers_for_bad_cb0_tail();
                    static std::atomic<uint64_t> tracked_log{0};
                    const auto tn = tracked_log.fetch_add(1, std::memory_order_relaxed);
                    if (tn < 16 || patches > 0 || (tn % 60) == 0) {
                        SPDLOG_WARN("[SN2-SkyAtmosScan] tracked#{} patched={} buffers={}",
                                    tn + 1, patches, sn2_upload_buf_map::entry_count());
                    }
                    scan_running.store(false, std::memory_order_release);
                }).detach();
            }
            return;
        }

        // LEGACY FAST PATH: re-patch every cached address from prior slow scan.
        {
            std::scoped_lock _{g_cache_mutex};
            uint64_t hits = 0;
            for (auto* p : g_known_bad_addrs) {
                hits += try_repatch_one(p);
            }
            static std::atomic<uint64_t> fast_log{0};
            const auto fn = fast_log.fetch_add(1, std::memory_order_relaxed);
            if (fn < 8 || (fn % 600) == 0) {
                SPDLOG_WARN("[SN2-SkyAtmosScan] fast#{} re-patched={} of {} cached addrs",
                            fn + 1, hits, g_known_bad_addrs.size());
            }
        }

        // SLOW PATH: re-run a full brute scan every 3 seconds to pick up
        // NEW bad addresses (the engine re-allocates upload cbuffer slots
        // every frame from a ring buffer; the addresses drift).
        static uint64_t last_slow_tick = 0;
        const uint64_t now_tick = GetTickCount64();
        if (now_tick - last_slow_tick < 3000) return;
        last_slow_tick = now_tick;

        init_bounds();
        uint64_t patches = 0;
        uint64_t pages_scanned = 0;
        uint64_t bytes_scanned = 0;
        std::vector<uint8_t*> found;
        uintptr_t addr = g_min_addr;
        while (addr < g_max_addr) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery((void*)addr, &mbi, sizeof(mbi)) != sizeof(mbi)) {
                addr += 0x1000;
                continue;
            }
            uintptr_t region_end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
            const DWORD protect = mbi.Protect;
            const bool writable = (protect & (PAGE_READWRITE | PAGE_WRITECOPY)) != 0;
            const bool guard = (protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0;
            const bool size_ok = (mbi.RegionSize >= (4 * 1024) && mbi.RegionSize <= (64 * 1024 * 1024));
            const bool type_ok = (mbi.Type == MEM_PRIVATE || mbi.Type == MEM_MAPPED);
            // Include WRITECOMBINE — D3D12 upload heaps on most drivers map
            // with PAGE_READWRITE | PAGE_WRITECOMBINE.
            if (mbi.State == MEM_COMMIT && writable && !guard && size_ok && type_ok) {
                ++pages_scanned;
                bytes_scanned += mbi.RegionSize;
                const uint8_t* base = (const uint8_t*)mbi.BaseAddress;
                const uint8_t* end = base + mbi.RegionSize - 48;
                const size_t before = found.size();
                try_scan_region(base, end, found);
                patches += (found.size() - before);
            }
            addr = region_end;
            if (addr <= (uintptr_t)mbi.BaseAddress) break;
        }
        if (!found.empty()) {
            std::scoped_lock _{g_cache_mutex};
            g_known_bad_addrs = std::move(found);
        }
        static std::atomic<uint64_t> log_n{0};
        const auto n = log_n.fetch_add(1, std::memory_order_relaxed);
        if (n < 8 || patches > 0 || (n % 30) == 0) {
            SPDLOG_WARN("[SN2-SkyAtmosScan] slow#{} patched={} priv_pages={} MB={} cached={}",
                        n + 1, patches, pages_scanned, bytes_scanned / (1024*1024),
                        g_known_bad_addrs.size());
        }
    }
} // namespace sn2_sky_atmos_scan

HRESULT D3D12Hook::present_internal(IDXGISwapChain3* swap_chain, UINT sync_interval, UINT flags, DXGI_PRESENT_PARAMETERS* params, bool present1) {
    sn2_sky_atmos_scan::scan_and_patch_uploads();
    auto d3d12 = g_d3d12_hook;
    const auto original_sync_interval = sync_interval;
    const auto original_flags = flags;

    HWND swapchain_wnd{nullptr};
    swap_chain->GetHwnd(&swapchain_wnd);

    using Present1Fn = HRESULT(*)(IDXGISwapChain3*, UINT, UINT, DXGI_PRESENT_PARAMETERS*);
    Present1Fn present_fn{nullptr};

    if (!present1) {
        present_fn = d3d12->m_present_hook->get_original<Present1Fn>();
    } else {
        present_fn = d3d12->m_present1_hook->get_original<Present1Fn>();
    }

    if (d3d12->m_is_phase_1 && WindowFilter::get().is_filtered(swapchain_wnd)) {
        return present_fn(swap_chain, sync_interval, flags, params);
    }

    if (!d3d12->m_is_phase_1 && swap_chain != d3d12->m_swapchain_hook->get_instance()) {
        const auto og_instance = d3d12->m_swapchain_hook->get_instance();

        // If the original swapchain instance is invalid, then we should not proceed, and rehook the swapchain
        if (IsBadReadPtr(og_instance, sizeof(void*)) || IsBadReadPtr(og_instance.deref(), sizeof(void*))) {
            spdlog::error("Bad read pointer for original swapchain instance, re-hooking");
            d3d12->m_is_phase_1 = true;
        }

        if (!d3d12->m_is_phase_1) {
            return present_fn(swap_chain, sync_interval, flags, params);
        }
    }

    if (d3d12->m_is_phase_1) {
        //d3d12->m_present_hook.reset();
        d3d12->m_swapchain_hook.reset();

        // vtable hook the swapchain instead of global hooking
        // this seems safer for whatever reason
        // if we globally hook the vtable pointers, it causes all sorts of weird conflicts with other hooks
        // dont hook present though via this hook so other hooks dont get confused
        d3d12->m_swapchain_hook = std::make_unique<VtableHook>(swap_chain);
        //d3d12->m_swapchain_hook->hook_method(8, (uintptr_t)&D3D12Hook::present);
        d3d12->m_swapchain_hook->hook_method(13, (uintptr_t)&D3D12Hook::resize_buffers);
        d3d12->m_swapchain_hook->hook_method(14, (uintptr_t)&D3D12Hook::resize_target);
        d3d12->m_is_phase_1 = false;
    }

    d3d12->m_inside_present = true;
    d3d12->m_swap_chain = swap_chain;

    swap_chain->GetDevice(IID_PPV_ARGS(&d3d12->m_device));

    if (d3d12->m_device != nullptr) {
        // 2026-05-18 SN2 SkyAtmosFix: install CreateCommittedResource hook EARLY
        // (right when we get the device) so we catch the cb0 backing buffer at
        // creation time. The Map vtable hook installs lazily via fog_path_a path,
        // but by then the engine's cbuffer pool is already created — this catches
        // future creations.
        if (is_subnautica2_process()) {
            sn2_upload_buf_map::install_device_hook(d3d12->m_device);
        }

        if (d3d12->m_using_proton_swapchain) {
            const auto real_swapchain = *(uintptr_t*)((uintptr_t)swap_chain + d3d12->m_proton_swapchain_offset);
            d3d12->m_command_queue = *(ID3D12CommandQueue**)(real_swapchain + d3d12->m_command_queue_offset);
        } else {
            d3d12->m_command_queue = *(ID3D12CommandQueue**)((uintptr_t)swap_chain + d3d12->m_command_queue_offset);
        }

        render::D3D12Diagnostics::get().begin_frame(
            d3d12->m_device,
            swap_chain,
            d3d12->m_command_queue,
            d3d12->m_render_width,
            d3d12->m_render_height,
            d3d12->m_display_width,
            d3d12->m_display_height,
            d3d12->m_using_proton_swapchain,
            d3d12->m_using_frame_generation_swapchain
        );
    }

    if (d3d12->m_swapchain_0 == nullptr) {
        d3d12->m_swapchain_0 = swap_chain;
    } else if (d3d12->m_swapchain_1 == nullptr && swap_chain != d3d12->m_swapchain_0) {
        d3d12->m_swapchain_1 = swap_chain;
    }
    
    // Restore the original bytes
    // if an infinite loop occurs, this will prevent the game from crashing
    // while keeping our hook intact
    if (g_present_depth > 0) {
        auto original_bytes = utility::get_original_bytes(Address{present_fn});

        if (original_bytes) {
            ProtectionOverride protection_override{present_fn, original_bytes->size(), PAGE_EXECUTE_READWRITE};

            memcpy(present_fn, original_bytes->data(), original_bytes->size());

            spdlog::info("Present fixed");
        }

        if ((uintptr_t)present_fn != (uintptr_t)D3D12Hook::present && g_present_depth == 1) {
            spdlog::info("Attempting to call real present function");

            ++g_present_depth;
            const auto result = present_fn(swap_chain, sync_interval, flags, params);
            --g_present_depth;

            if (result != S_OK) {
                spdlog::error("Present failed: {:x}", result);
            }

            return result;
        }

        spdlog::info("Just returning S_OK");
        return S_OK;
    }

    // 2026-05-19: GPU readback fence signal + drain. Per frame, signal the
    // readback fence after the game has presumably submitted its work, then
    // drain completed slots to log.
    {
        auto* queue = d3d12->m_command_queue;
        if (queue != nullptr && sn2_gpu_readback::initialized()) {
            sn2_gpu_readback::signal_after_execute(queue);
            sn2_gpu_readback::drain_to_log();
        }
    }
    // 2026-05-19 SN2 COMPUTE CAPTURE: log + clear per-frame captured calls
    sn2_compute_capture::log_frame_summary();
    sn2_compute_capture::clear_frame();

    // 2026-05-20 SN2 RT SNAPSHOT: lazy init + capture current backbuffer
    // (side-by-side stereo) every Nth frame so we can extract LEFT vs RIGHT
    // halves for comparison. Then signal + drain to disk.
    {
        auto* queue = d3d12->m_command_queue;
        if (sn2_rt_snapshot::enabled() && queue != nullptr) {
            ID3D12Device* device = d3d12->get_device();
            if (device != nullptr) {
                sn2_rt_snapshot::init(device);
            }
            if (sn2_rt_snapshot::initialized()) {
                static std::atomic<uint64_t> snap_counter{0};
                const uint64_t sc = snap_counter.fetch_add(1, std::memory_order_relaxed);
                // Snapshot every 600 frames (~10s @ 60fps) — avoid resource leaks.
                // Reuse a single persistent CL+allocator across all snapshots.
                static Microsoft::WRL::ComPtr<ID3D12CommandAllocator> s_alloc;
                static Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> s_cl;
                if (s_alloc == nullptr) {
                    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&s_alloc));
                    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, s_alloc.Get(), nullptr, IID_PPV_ARGS(&s_cl));
                    s_cl->Close();
                }
                // Capture backbuffer every 600 frames + drain Phase 3 intents
                bool do_bb = (sc % 600) == 0;
                sn2_rt_snapshot::Intent bp_intent{};
                bool have_bp = sn2_rt_snapshot::dequeue_intent(bp_intent);
                if ((do_bb || have_bp) && s_alloc != nullptr && s_cl != nullptr) {
                    s_alloc->Reset();
                    s_cl->Reset(s_alloc.Get(), nullptr);
                    if (do_bb) {
                        Microsoft::WRL::ComPtr<ID3D12Resource> backbuffer;
                        UINT idx = swap_chain->GetCurrentBackBufferIndex();
                        if (SUCCEEDED(swap_chain->GetBuffer(idx, IID_PPV_ARGS(&backbuffer)))) {
                            char tag[32];
                            std::snprintf(tag, sizeof(tag), "BB_f%llu", (unsigned long long)sc);
                            sn2_rt_snapshot::schedule_capture(s_cl.Get(), backbuffer.Get(),
                                D3D12_RESOURCE_STATE_COMMON, tag);
                        }
                    }
                    if (have_bp) {
                        // RTV resource at present time is in COMMON (decayed). Safe to copy.
                        sn2_rt_snapshot::schedule_capture(s_cl.Get(), bp_intent.res,
                            D3D12_RESOURCE_STATE_COMMON, bp_intent.tag);
                    }
                    s_cl->Close();
                    ID3D12CommandList* lists[] = { s_cl.Get() };
                    queue->ExecuteCommandLists(1, lists);
                }
                sn2_rt_snapshot::signal_after_execute(queue);
                sn2_rt_snapshot::drain_to_disk();
            }
        }
    }

    if (d3d12->m_on_present) {
        d3d12->m_on_present(*d3d12);

        if (d3d12->m_next_present_interval) {
            const auto requested_sync_interval = *d3d12->m_next_present_interval;
            d3d12->m_next_present_interval = std::nullopt;

            const auto swapchain_key = reinterpret_cast<uintptr_t>(swap_chain);
            const auto preserve_for_current_game = should_preserve_present_params_for_current_game();

            if (preserve_for_current_game || d3d12->m_swapchains_requiring_original_present_params.contains(swapchain_key)) {
                if (d3d12->m_original_present_param_skip_logged_swapchains.insert(swapchain_key).second) {
                    if (preserve_for_current_game) {
                        spdlog::warn(
                            "Skipping UEVR Present param override for MafiaTheOldCountry swapchain {:x}; preserving original sync={} flags={:x}",
                            swapchain_key,
                            original_sync_interval,
                            original_flags);
                    } else {
                        spdlog::warn(
                            "Skipping UEVR Present param override for swapchain {:x} after prior original-param recovery",
                            swapchain_key);
                    }
                }
            } else {
                sync_interval = requested_sync_interval;

                if (sync_interval == 0) {
                    BOOL is_fullscreen = 0;
                    swap_chain->GetFullscreenState(&is_fullscreen, nullptr);
                    flags &= ~DXGI_PRESENT_DO_NOT_SEQUENCE;

                    DXGI_SWAP_CHAIN_DESC swap_desc{};
                    swap_chain->GetDesc(&swap_desc);

                    if (!is_fullscreen && (swap_desc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0) {
                        flags |= DXGI_PRESENT_ALLOW_TEARING;
                    }
                }
            }
        }
    }

    ++g_present_depth;

    auto result = S_OK;
    
    if (!d3d12->m_ignore_next_present) {
        result = present_fn(swap_chain, sync_interval, flags, params);

        if (result == DXGI_ERROR_INVALID_CALL &&
            (sync_interval != original_sync_interval || flags != original_flags))
        {
            spdlog::warn(
                "Present failed with modified params, retrying original params. modified_sync={} modified_flags={:x} original_sync={} original_flags={:x}",
                sync_interval,
                flags,
                original_sync_interval,
                original_flags);

            result = present_fn(swap_chain, original_sync_interval, original_flags, params);

            if (result == S_OK) {
                spdlog::warn("Present retry with original params succeeded");
                const auto swapchain_key = reinterpret_cast<uintptr_t>(swap_chain);

                if (d3d12->m_swapchains_requiring_original_present_params.insert(swapchain_key).second) {
                    spdlog::warn(
                        "Marked swapchain {:x} to preserve original Present params after DXGI_ERROR_INVALID_CALL recovery",
                        swapchain_key);
                }
            } else {
                spdlog::error("Present retry with original params failed: {:x}", result);
            }
        }

        if (result != S_OK) {
            spdlog::error("Present failed: {:x}", result);
        }
    } else {
        d3d12->m_ignore_next_present = false;
    }

    --g_present_depth;

    if (d3d12->m_on_post_present) {
        d3d12->m_on_post_present(*d3d12);
    }

    log_stereo_trace_if_needed();

    d3d12->m_inside_present = false;

    return result;
}

HRESULT WINAPI D3D12Hook::present(IDXGISwapChain3* swap_chain, UINT sync_interval, UINT flags) {
    std::scoped_lock _{g_framework->get_hook_monitor_mutex()};
    
    return D3D12Hook::present_internal(swap_chain, sync_interval, flags, nullptr, false);
}

HRESULT WINAPI D3D12Hook::present1(IDXGISwapChain3* swap_chain, UINT sync_interval, UINT flags, DXGI_PRESENT_PARAMETERS* params) {
    std::scoped_lock _{g_framework->get_hook_monitor_mutex()};

    return D3D12Hook::present_internal(swap_chain, sync_interval, flags, params, true);
}

// 2026-05-20: pso3069_PS substitution. Hash of pso3069_PS.dxil DXBC container
// (first 16 bytes after "DXBC" magic = MD5-like). When create_graphics_pipeline_state
// is called with a PS matching this hash, swap to our patched DXBC instead.
// Gated by env UEVR_SN2_PSO3069_SUBST=1 so we can disable instantly.
namespace sn2_pso3069_subst {
    // pso3069_PS.dxil DXBC CONTAINER hash (first 16 bytes after "DXBC" magic).
    // CRITICAL: this is the DXBC container hash, NOT the DXIL bitcode hash. The
    // ".dxil.txt shader hash" e739f41c... is a different hash (DXIL bitcode).
    // The container hash is in the actual file at offset 4..19:
    //   a2 d2 f9 1d a7 9f 54 d6 8a dd 80 29 4f 8e 04 66
    static const uint8_t kPso3069PsHash[16] = {
        0xa2, 0xd2, 0xf9, 0x1d, 0xa7, 0x9f, 0x54, 0xd6,
        0x8a, 0xdd, 0x80, 0x29, 0x4f, 0x8e, 0x04, 0x66
    };

    static std::vector<uint8_t> g_patched_dxbc;
    static std::mutex g_load_mutex;

    static bool enabled() {
        static const bool e = []() {
            const char* env = std::getenv("UEVR_SN2_PSO3069_SUBST");
            return env != nullptr && env[0] != '\0' && env[0] != '0';
        }();
        return e;
    }

    static bool load_patched() {
        std::scoped_lock _{g_load_mutex};
        if (!g_patched_dxbc.empty()) return true;
        const char* path = "E:\\Github\\Subnautica 2\\moddingkit\\runs\\pso3069_subst.dxbc";
        FILE* f = std::fopen(path, "rb");
        if (f == nullptr) {
            SPDLOG_WARN("[SN2-PSO3069-Subst] could not open {}", path);
            return false;
        }
        std::fseek(f, 0, SEEK_END);
        const auto sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        g_patched_dxbc.resize(sz);
        const auto n = std::fread(g_patched_dxbc.data(), 1, sz, f);
        std::fclose(f);
        if (n != static_cast<size_t>(sz)) {
            SPDLOG_WARN("[SN2-PSO3069-Subst] short read {} of {}", n, sz);
            g_patched_dxbc.clear();
            return false;
        }
        SPDLOG_WARN("[SN2-PSO3069-Subst] loaded patched DXBC ({} bytes)", n);
        return true;
    }

    static bool matches_pso3069(const void* bc, size_t len) {
        if (bc == nullptr || len < 20) return false;
        const auto* b = static_cast<const uint8_t*>(bc);
        if (b[0] != 'D' || b[1] != 'X' || b[2] != 'B' || b[3] != 'C') return false;
        return std::memcmp(b + 4, kPso3069PsHash, 16) == 0;
    }
}

HRESULT WINAPI D3D12Hook::create_graphics_pipeline_state(
    ID3D12Device* device,
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,
    REFIID riid,
    void** pipeline_state
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = &(*(void***)device)[CREATE_GRAPHICS_PIPELINE_STATE_VTABLE_INDEX];
    auto* hook = d3d12->find_create_graphics_pipeline_state_hook(slot);
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::create_graphics_pipeline_state)*>() : nullptr;

    if (original == nullptr) {
        return E_FAIL;
    }

    // Phase 10: substitute pso3069 PS bytecode with patched version.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC patched_desc{};
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC* call_desc = desc;
    if (desc != nullptr && sn2_pso3069_subst::enabled() &&
        sn2_pso3069_subst::matches_pso3069(desc->PS.pShaderBytecode, desc->PS.BytecodeLength) &&
        sn2_pso3069_subst::load_patched())
    {
        patched_desc = *desc;
        patched_desc.PS.pShaderBytecode = sn2_pso3069_subst::g_patched_dxbc.data();
        patched_desc.PS.BytecodeLength = sn2_pso3069_subst::g_patched_dxbc.size();
        call_desc = &patched_desc;
        SPDLOG_WARN("[SN2-PSO3069-Subst] SUBSTITUTING pso3069 PS ({} -> {} bytes)",
            desc->PS.BytecodeLength, patched_desc.PS.BytecodeLength);
    }

    const auto result = original(device, call_desc, riid, pipeline_state);

    auto& shader_registry = render::ShaderOverrideRegistry::get();
    if (shader_registry.should_record_d3d12_pipeline_creations() &&
        SUCCEEDED(result) &&
        pipeline_state != nullptr &&
        *pipeline_state != nullptr &&
        riid == __uuidof(ID3D12PipelineState) &&
        desc != nullptr) {
        shader_registry.register_d3d12_graphics_pipeline_state_creation(
            device,
            static_cast<ID3D12PipelineState*>(*pipeline_state),
            desc
        );
    }

    // 2026-05-17 night: SN2 sky-atmosphere PSO fingerprint by entry-name scan.
    // The DXIL container has the entry-function name embedded as a C-string.
    // memmem-style scan for the known unique name pins the exact PSO* we
    // want to gate cb-redirects on.
    if (is_subnautica2_process() &&
        SUCCEEDED(result) &&
        pipeline_state != nullptr && *pipeline_state != nullptr &&
        riid == __uuidof(ID3D12PipelineState) &&
        desc != nullptr &&
        desc->PS.pShaderBytecode != nullptr && desc->PS.BytecodeLength > 0 &&
        g_sky_atmos_pso.load(std::memory_order_relaxed) == nullptr) {
        static constexpr char kSkyAtmosName[] = "RenderSkyAtmosphereRayMarchingPS";
        constexpr size_t name_len = sizeof(kSkyAtmosName) - 1;
        const auto* bytes = static_cast<const uint8_t*>(desc->PS.pShaderBytecode);
        const size_t blen = desc->PS.BytecodeLength;
        if (blen >= name_len) {
            for (size_t i = 0; i + name_len <= blen; ++i) {
                if (bytes[i] == 'R' && memcmp(bytes + i, kSkyAtmosName, name_len) == 0) {
                    auto* pso = static_cast<ID3D12PipelineState*>(*pipeline_state);
                    g_sky_atmos_pso.store(pso, std::memory_order_relaxed);
                    SPDLOG_WARN("[SN2-SkyAtmosFix] FINGERPRINTED RenderSkyAtmosphereRayMarchingPS PSO=0x{:x}",
                                (uint64_t)pso);
                    break;
                }
            }
        }
    }

    return result;
}

HRESULT WINAPI D3D12Hook::create_compute_pipeline_state(
    ID3D12Device* device,
    const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc,
    REFIID riid,
    void** pipeline_state
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = device != nullptr ? &(*(void***)device)[CREATE_COMPUTE_PIPELINE_STATE_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_create_compute_pipeline_state_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::create_compute_pipeline_state)*>() : nullptr;

    if (original == nullptr) {
        return E_FAIL;
    }

    const auto result = original(device, desc, riid, pipeline_state);

    auto& shader_registry = render::ShaderOverrideRegistry::get();
    if (shader_registry.should_record_d3d12_pipeline_creations() &&
        SUCCEEDED(result) &&
        pipeline_state != nullptr &&
        *pipeline_state != nullptr &&
        riid == __uuidof(ID3D12PipelineState) &&
        desc != nullptr) {
        shader_registry.register_d3d12_compute_pipeline_state_creation(
            device,
            static_cast<ID3D12PipelineState*>(*pipeline_state),
            desc
        );
    }

    return result;
}

HRESULT WINAPI D3D12Hook::create_command_list(
    ID3D12Device* device,
    UINT node_mask,
    D3D12_COMMAND_LIST_TYPE type,
    ID3D12CommandAllocator* command_allocator,
    ID3D12PipelineState* initial_state,
    REFIID riid,
    void** command_list
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = device != nullptr ? &(*(void***)device)[CREATE_COMMAND_LIST_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_create_command_list_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::create_command_list)*>() : nullptr;

    if (original == nullptr) {
        return E_FAIL;
    }

    auto& shader_registry = render::ShaderOverrideRegistry::get();
    auto* bound_initial_state = initial_state;
    if (shader_registry.should_track_d3d12_pipelines() && initial_state != nullptr) {
        bound_initial_state = shader_registry.resolve_d3d12_pipeline_state(initial_state);
        shader_registry.note_d3d12_pipeline_state_bound(initial_state, bound_initial_state);
    }

    const auto result = original(device, node_mask, type, command_allocator, bound_initial_state, riid, command_list);
    if (SUCCEEDED(result) && command_list != nullptr && *command_list != nullptr && d3d12 != nullptr) {
        auto* unknown = reinterpret_cast<IUnknown*>(*command_list);
        d3d12->install_command_list_hooks_from_unknown(unknown);

        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> graphics_command_list{};
        if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&graphics_command_list))) && initial_state != nullptr) {
            update_cmdlist_pso(graphics_command_list.Get(), initial_state);
            if (shader_registry.should_track_d3d12_pipelines()) {
                shader_registry.hunter_record_set_pipeline_state(graphics_command_list.Get(), initial_state);
            }
        }
    }

    return result;
}

HRESULT WINAPI D3D12Hook::create_command_list1(
    ID3D12Device4* device,
    UINT node_mask,
    D3D12_COMMAND_LIST_TYPE type,
    D3D12_COMMAND_LIST_FLAGS flags,
    REFIID riid,
    void** command_list
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = device != nullptr ? &(*(void***)device)[CREATE_COMMAND_LIST1_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_create_command_list1_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::create_command_list1)*>() : nullptr;

    if (original == nullptr) {
        return E_FAIL;
    }

    const auto result = original(device, node_mask, type, flags, riid, command_list);
    if (SUCCEEDED(result) && command_list != nullptr && *command_list != nullptr && d3d12 != nullptr) {
        d3d12->install_command_list_hooks_from_unknown(reinterpret_cast<IUnknown*>(*command_list));
    }

    return result;
}

HRESULT WINAPI D3D12Hook::create_command_signature(
    ID3D12Device* device,
    const D3D12_COMMAND_SIGNATURE_DESC* desc,
    ID3D12RootSignature* root_signature,
    REFIID riid,
    void** command_signature
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = device != nullptr ? &(*(void***)device)[CREATE_COMMAND_SIGNATURE_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_create_command_signature_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::create_command_signature)*>() : nullptr;

    if (original == nullptr) {
        return E_FAIL;
    }

    const auto result = original(device, desc, root_signature, riid, command_signature);
    if (SUCCEEDED(result) &&
            command_signature != nullptr &&
            *command_signature != nullptr &&
            riid == __uuidof(ID3D12CommandSignature)) {
        sn2_command_signature_registry::record(
            static_cast<ID3D12CommandSignature*>(*command_signature),
            desc);
    }
    return result;
}

HRESULT WINAPI D3D12Hook::create_pipeline_state(
    ID3D12Device2* device,
    const D3D12_PIPELINE_STATE_STREAM_DESC* desc,
    REFIID riid,
    void** pipeline_state
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = &(*(void***)device)[CREATE_PIPELINE_STATE_VTABLE_INDEX];
    auto* hook = d3d12->find_create_pipeline_state_hook(slot);
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::create_pipeline_state)*>() : nullptr;

    if (original == nullptr) {
        return E_FAIL;
    }

    // Phase 10: walk stream, substitute pso3069 PS bytecode pre-call.
    std::vector<uint8_t> stream_copy;
    D3D12_PIPELINE_STATE_STREAM_DESC patched_desc{};
    const D3D12_PIPELINE_STATE_STREAM_DESC* call_desc = desc;
    bool did_substitute = false;
    if (desc != nullptr && desc->pPipelineStateSubobjectStream != nullptr &&
        desc->SizeInBytes > 0 && sn2_pso3069_subst::enabled())
    {
        // Make a writable copy so we can patch the PS pointer in-place.
        stream_copy.assign(
            static_cast<const uint8_t*>(desc->pPipelineStateSubobjectStream),
            static_cast<const uint8_t*>(desc->pPipelineStateSubobjectStream) + desc->SizeInBytes);
        uint8_t* base = stream_copy.data();
        size_t pos = 0;
        while (pos + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) <= desc->SizeInBytes) {
            const auto type = *reinterpret_cast<const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(base + pos);
            pos += sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE);
            const size_t align = alignof(void*);
            pos = (pos + align - 1) & ~(align - 1);
            size_t value_size = 0;
            switch (type) {
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE:        value_size = sizeof(ID3D12RootSignature*); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS:                    value_size = sizeof(D3D12_SHADER_BYTECODE); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT:         value_size = sizeof(D3D12_STREAM_OUTPUT_DESC); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND:                 value_size = sizeof(D3D12_BLEND_DESC); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK:           value_size = sizeof(UINT); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER:            value_size = sizeof(D3D12_RASTERIZER_DESC); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL:         value_size = sizeof(D3D12_DEPTH_STENCIL_DESC); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT:          value_size = sizeof(D3D12_INPUT_LAYOUT_DESC); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE:    value_size = sizeof(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY:    value_size = sizeof(D3D12_PRIMITIVE_TOPOLOGY_TYPE); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS: value_size = sizeof(D3D12_RT_FORMAT_ARRAY); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT:  value_size = sizeof(DXGI_FORMAT); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC:           value_size = sizeof(DXGI_SAMPLE_DESC); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK:             value_size = sizeof(UINT); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO:            value_size = sizeof(D3D12_CACHED_PIPELINE_STATE); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS:                 value_size = sizeof(D3D12_PIPELINE_STATE_FLAGS); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1:        value_size = sizeof(D3D12_DEPTH_STENCIL_DESC1); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING:       value_size = sizeof(D3D12_VIEW_INSTANCING_DESC); break;
                default: value_size = 0; break;
            }
            if (value_size == 0 || pos + value_size > desc->SizeInBytes) break;
            if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS) {
                auto* bc = reinterpret_cast<D3D12_SHADER_BYTECODE*>(base + pos);
                if (sn2_pso3069_subst::matches_pso3069(bc->pShaderBytecode, bc->BytecodeLength) &&
                    sn2_pso3069_subst::load_patched())
                {
                    SPDLOG_WARN("[SN2-PSO3069-Subst] STREAM SUBSTITUTING pso3069 PS ({} -> {} bytes)",
                        bc->BytecodeLength, sn2_pso3069_subst::g_patched_dxbc.size());
                    bc->pShaderBytecode = sn2_pso3069_subst::g_patched_dxbc.data();
                    bc->BytecodeLength = sn2_pso3069_subst::g_patched_dxbc.size();
                    did_substitute = true;
                }
            }
            pos += value_size;
            pos = (pos + align - 1) & ~(align - 1);
        }
        if (did_substitute) {
            patched_desc.SizeInBytes = desc->SizeInBytes;
            patched_desc.pPipelineStateSubobjectStream = stream_copy.data();
            call_desc = &patched_desc;
        }
    }

    const auto result = original(device, call_desc, riid, pipeline_state);
    if (did_substitute) {
        SPDLOG_WARN("[SN2-PSO3069-Subst] post-call hr=0x{:08x} pso={:p}",
            (uint32_t)result, *pipeline_state);
    }

    auto& shader_registry = render::ShaderOverrideRegistry::get();
    if (shader_registry.should_record_d3d12_pipeline_creations() &&
        SUCCEEDED(result) &&
        pipeline_state != nullptr &&
        *pipeline_state != nullptr &&
        riid == __uuidof(ID3D12PipelineState) &&
        desc != nullptr) {
        shader_registry.register_d3d12_pipeline_state_stream_creation(
            device,
            static_cast<ID3D12PipelineState*>(*pipeline_state),
            desc
        );
    }

    // 2026-05-17 night: SkyAtmosFix PSO fingerprint — also scan stream variant.
    // SN2 uses D3D12_PIPELINE_STATE_STREAM_DESC. Parse the stream to extract
    // PS bytecode, then memmem-scan for entry name "RenderSkyAtmosphereRayMarchingPS".
    if (is_subnautica2_process() &&
        SUCCEEDED(result) &&
        pipeline_state != nullptr && *pipeline_state != nullptr &&
        riid == __uuidof(ID3D12PipelineState) &&
        desc != nullptr && desc->pPipelineStateSubobjectStream != nullptr &&
        desc->SizeInBytes > 0 &&
        g_sky_atmos_pso.load(std::memory_order_relaxed) == nullptr) {
        // Walk subobjects. Each starts with aligned D3D12_PIPELINE_STATE_SUBOBJECT_TYPE
        // (4 bytes, aligned to alignof(void*) for the following struct).
        const auto* base = static_cast<const uint8_t*>(desc->pPipelineStateSubobjectStream);
        size_t pos = 0;
        while (pos + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) <= desc->SizeInBytes) {
            const auto type = *reinterpret_cast<const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(base + pos);
            pos += sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE);
            // Align to pointer for the value struct.
            const size_t align = alignof(void*);
            pos = (pos + align - 1) & ~(align - 1);
            size_t value_size = 0;
            switch (type) {
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE:        value_size = sizeof(ID3D12RootSignature*); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS:                    value_size = sizeof(D3D12_SHADER_BYTECODE); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT:         value_size = sizeof(D3D12_STREAM_OUTPUT_DESC); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND:                 value_size = sizeof(D3D12_BLEND_DESC); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK:           value_size = sizeof(UINT); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER:            value_size = sizeof(D3D12_RASTERIZER_DESC); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL:         value_size = sizeof(D3D12_DEPTH_STENCIL_DESC); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT:          value_size = sizeof(D3D12_INPUT_LAYOUT_DESC); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE:    value_size = sizeof(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY:    value_size = sizeof(D3D12_PRIMITIVE_TOPOLOGY_TYPE); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS: value_size = sizeof(D3D12_RT_FORMAT_ARRAY); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT:  value_size = sizeof(DXGI_FORMAT); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC:           value_size = sizeof(DXGI_SAMPLE_DESC); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK:             value_size = sizeof(UINT); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO:            value_size = sizeof(D3D12_CACHED_PIPELINE_STATE); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS:                 value_size = sizeof(D3D12_PIPELINE_STATE_FLAGS); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1:        value_size = sizeof(D3D12_DEPTH_STENCIL_DESC1); break;
                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING:       value_size = sizeof(D3D12_VIEW_INSTANCING_DESC); break;
                default: value_size = 0; break;
            }
            if (value_size == 0 || pos + value_size > desc->SizeInBytes) break;
            if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS) {
                const auto* bc = reinterpret_cast<const D3D12_SHADER_BYTECODE*>(base + pos);
                if (bc->pShaderBytecode != nullptr && bc->BytecodeLength > 0) {
                    static constexpr char kSkyAtmosName[] = "RenderSkyAtmosphereRayMarchingPS";
                    constexpr size_t name_len = sizeof(kSkyAtmosName) - 1;
                    const auto* bytes = static_cast<const uint8_t*>(bc->pShaderBytecode);
                    const size_t blen = bc->BytecodeLength;
                    if (blen >= name_len) {
                        for (size_t i = 0; i + name_len <= blen; ++i) {
                            if (bytes[i] == 'R' && memcmp(bytes + i, kSkyAtmosName, name_len) == 0) {
                                auto* pso = static_cast<ID3D12PipelineState*>(*pipeline_state);
                                g_sky_atmos_pso.store(pso, std::memory_order_relaxed);
                                SPDLOG_WARN("[SN2-SkyAtmosFix] FINGERPRINTED (stream) RenderSkyAtmosphereRayMarchingPS PSO=0x{:x}",
                                            (uint64_t)pso);
                                break;
                            }
                        }
                    }
                }
            }
            pos += value_size;
            // Re-align to pointer.
            pos = (pos + align - 1) & ~(align - 1);
        }
    }

    return result;
}

void WINAPI D3D12Hook::create_constant_buffer_view(
    ID3D12Device* device,
    const D3D12_CONSTANT_BUFFER_VIEW_DESC* desc,
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = device != nullptr ? &(*(void***)device)[CREATE_CONSTANT_BUFFER_VIEW_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_create_constant_buffer_view_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::create_constant_buffer_view)*>() : nullptr;

    if (original != nullptr) {
        original(device, desc, descriptor);
    }

    sn2_descriptor_registry::record_cbv(desc, descriptor);
}

void WINAPI D3D12Hook::create_render_target_view(
    ID3D12Device* device,
    ID3D12Resource* resource,
    const D3D12_RENDER_TARGET_VIEW_DESC* desc,
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor
) {
    (void)desc;
    auto d3d12 = g_d3d12_hook;
    const auto slot = device != nullptr ? &(*(void***)device)[CREATE_RENDER_TARGET_VIEW_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_create_render_target_view_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::create_render_target_view)*>() : nullptr;

    if (original != nullptr) {
        original(device, resource, desc, descriptor);
    }

    render::D3D12Diagnostics::get().register_rtv_descriptor("D3D12Hook::CreateRenderTargetView", resource, descriptor);
    sn2_rt_snapshot::record_rtv(static_cast<uint64_t>(descriptor.ptr), resource);
}

// 2026-05-16 SN2 fog SRV tracker. Hooks ID3D12Device::CreateShaderResourceView
// (vtable 18) to log every SRV creation for 3D R11G11B10_FLOAT 141x74x64
// textures — the fog volumes. This identifies which ID3D12Resource gets bound
// to view 1's basepass PS slot 5, confirming whether RDG is creating distinct
// per-view SRVs or sharing one (the bug hypothesis).
// 2026-05-17 SN2 FOG-FIX: global maps populated by create_shader_resource_view.
// Used by the SLW per-view hook to identify view 1's fog volume's CPU
// descriptor handle by looking up the GPU VA we extract from its FViewInfo
// wrapper at +0x2630 → qw[8] → +0x70. Entry struct declared in D3D12Hook.hpp.

// 2026-05-17 Plan A.1 cross-TU accessor: read thread_local g_sn2_current_fog_view
// set by the volumetric fog per-view hook in FFakeStereoRenderingHook.cpp.
// Both TUs are in the same DLL — extern "C" alone is enough.
extern "C" int sn2_get_current_fog_view();
namespace sn2_fog_srv_map {
    static std::mutex g_mutex;
    // Map GPU VA → most-recent SRV info. Multiple SRVs can share a resource;
    // we just keep the last one created for each GPU VA (typical UE5 pattern
    // creates one SRV per pooled resource per frame).
    static std::unordered_map<UINT64, Entry> g_by_gpu_va;
    // Reverse map ID3D12Resource* → GPU VA (cached so we don't re-query).
    static std::unordered_map<ID3D12Resource*, UINT64> g_resource_to_va;

    // Also keyed by resource pointer so we can look up by ID3D12Resource* even
    // when GPU VA isn't available (texture3d returns 0 from GetGPUVirtualAddress).
    static std::unordered_map<ID3D12Resource*, Entry> g_by_resource;
    // Ordered list by creation order, so user can specify e.g. "SRV index 5" as
    // the source for the descriptor swap.
    static std::vector<Entry> g_by_index;

    void record(ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, int view_id_at_creation) {
        if (resource == nullptr) return;
        std::scoped_lock _{g_mutex};
        UINT64 va = 0;
        auto it_res = g_resource_to_va.find(resource);
        if (it_res != g_resource_to_va.end()) {
            va = it_res->second;
        } else {
            va = resource->GetGPUVirtualAddress();  // returns 0 for textures, OK
            g_resource_to_va[resource] = va;
        }

        // Preserve existing view tag if we already have one and the new one is -1.
        int final_view_id = view_id_at_creation;
        if (final_view_id == -1) {
            auto it_prev = g_by_resource.find(resource);
            if (it_prev != g_by_resource.end() && it_prev->second.view_id != -1) {
                final_view_id = it_prev->second.view_id;
            }
        }

        Entry e;
        e.resource = resource;
        e.cpu_handle = cpu_handle;
        e.gpu_va = va;
        e.last_seen_tick = GetTickCount64();
        e.view_id = final_view_id;

        // Track creation index. If this resource is already in by_index, reuse
        // that index (UE5 may recreate SRVs for the same resource).
        bool found_in_index = false;
        for (auto& existing : g_by_index) {
            if (existing.resource == resource) {
                e.creation_index = existing.creation_index;
                existing = e;
                found_in_index = true;
                break;
            }
        }
        if (!found_in_index) {
            e.creation_index = g_by_index.size();
            g_by_index.push_back(e);
        }

        g_by_resource[resource] = e;       // always populate by-resource map
        if (va != 0) {
            g_by_gpu_va[va] = e;
        }
    }

    bool lookup_by_creation_index(uint64_t index, Entry& out) {
        std::scoped_lock _{g_mutex};
        if (index >= g_by_index.size()) return false;
        out = g_by_index[index];
        return true;
    }

    bool lookup_by_resource(ID3D12Resource* resource, Entry& out) {
        std::scoped_lock _{g_mutex};
        auto it = g_by_resource.find(resource);
        if (it == g_by_resource.end()) return false;
        out = it->second;
        return true;
    }

    bool lookup_first_by_view_id(int view_id, Entry& out) {
        std::scoped_lock _{g_mutex};
        for (const auto& kv : g_by_resource) {
            if (kv.second.view_id == view_id) {
                out = kv.second;
                return true;
            }
        }
        return false;
    }

    void tag_resource_view(ID3D12Resource* resource, int view_id) {
        if (resource == nullptr) return;
        std::scoped_lock _{g_mutex};
        auto it = g_by_resource.find(resource);
        if (it != g_by_resource.end()) {
            it->second.view_id = view_id;
        }
    }

    size_t count_by_view(int view_id) {
        std::scoped_lock _{g_mutex};
        size_t n = 0;
        for (const auto& kv : g_by_resource) {
            if (kv.second.view_id == view_id) ++n;
        }
        return n;
    }

    bool lookup_by_gpu_va(UINT64 va, Entry& out) {
        std::scoped_lock _{g_mutex};
        auto it = g_by_gpu_va.find(va);
        if (it == g_by_gpu_va.end()) return false;
        out = it->second;
        return true;
    }

    size_t size() {
        std::scoped_lock _{g_mutex};
        // Report by-resource map size — for textures, by-gpu-va stays empty
        // (GetGPUVirtualAddress() returns 0), so this is the populated map.
        return g_by_resource.size();
    }

    // 2026-05-17 Task #45 — linear-scan entry by cpu_handle. Returns true and
    // sets view_id on the matching entry; preserves an existing valid tag
    // (don't downgrade view_id back to -1 here, but allow re-tagging if the
    // entry hasn't been positively tagged yet).
    bool tag_by_cpu_handle(D3D12_CPU_DESCRIPTOR_HANDLE cpu, int view_id) {
        std::scoped_lock _{g_mutex};
        // Linear scan — small map (~120 entries).
        for (auto& kv : g_by_resource) {
            if (kv.second.cpu_handle.ptr == cpu.ptr) {
                if (kv.second.view_id == -1) {
                    kv.second.view_id = view_id;
                }
                // Mirror into g_by_index too.
                for (auto& e : g_by_index) {
                    if (e.resource == kv.first) {
                        if (e.view_id == -1) e.view_id = view_id;
                        break;
                    }
                }
                return true;
            }
        }
        return false;
    }

    bool lookup_by_cpu_handle(D3D12_CPU_DESCRIPTOR_HANDLE cpu, Entry& out) {
        std::scoped_lock _{g_mutex};
        for (const auto& kv : g_by_resource) {
            if (kv.second.cpu_handle.ptr == cpu.ptr) {
                out = kv.second;
                return true;
            }
        }
        return false;
    }
} // namespace sn2_fog_srv_map

// 2026-05-17 Task #45 — TU-level free helpers that the
// set_compute_root_descriptor_table hook (above) calls during retroactive
// tagging. Free functions so D3D12Hook.cpp doesn't need to expose the
// namespace's internals beyond the existing public API.
extern "C" bool sn2_fog_srv_map_tag_by_cpu_handle(D3D12_CPU_DESCRIPTOR_HANDLE cpu, int view_id) {
    return sn2_fog_srv_map::tag_by_cpu_handle(cpu, view_id);
}

// 2026-05-17 SN2 FOG-FIX Task #43 — UAV resource→view tag map. Populated by
// D3D12Hook::create_unordered_access_view when the resource is a Texture3D
// being created during a per-view fog dispatcher scope (view tag taken from
// sn2_get_current_fog_view()). The recorded ID3D12Resource* is the actual
// compute output for that view — so view 1's resource is what we look up in
// sn2_fog_srv_map by pointer to retrieve the SRV cpu_handle to swap in at
// view 1's bindless basepass slot.
namespace sn2_fog_uav_map {
    static std::mutex g_mutex;
    static std::unordered_map<ID3D12Resource*, Entry> g_by_resource;
    static std::vector<Entry> g_by_index;

    void record(ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, int view_id) {
        if (resource == nullptr) return;
        std::scoped_lock _{g_mutex};

        // Preserve previous view tag if new one is -1 (unknown).
        int final_view_id = view_id;
        if (final_view_id == -1) {
            auto it_prev = g_by_resource.find(resource);
            if (it_prev != g_by_resource.end() && it_prev->second.view_id != -1) {
                final_view_id = it_prev->second.view_id;
            }
        }

        Entry e{};
        e.resource = resource;
        e.cpu_handle = cpu_handle;
        e.view_id = final_view_id;
        e.last_seen_tick = GetTickCount64();

        bool found = false;
        for (auto& existing : g_by_index) {
            if (existing.resource == resource) {
                e.creation_index = existing.creation_index;
                existing = e;
                found = true;
                break;
            }
        }
        if (!found) {
            e.creation_index = g_by_index.size();
            g_by_index.push_back(e);
        }
        g_by_resource[resource] = e;
    }

    bool lookup_by_resource(ID3D12Resource* resource, Entry& out) {
        std::scoped_lock _{g_mutex};
        auto it = g_by_resource.find(resource);
        if (it == g_by_resource.end()) return false;
        out = it->second;
        return true;
    }

    bool lookup_first_by_view_id(int view_id, Entry& out) {
        std::scoped_lock _{g_mutex};
        for (const auto& e : g_by_index) {
            if (e.view_id == view_id) { out = e; return true; }
        }
        return false;
    }

    bool lookup_last_by_view_id(int view_id, Entry& out) {
        std::scoped_lock _{g_mutex};
        for (auto it = g_by_index.rbegin(); it != g_by_index.rend(); ++it) {
            if (it->view_id == view_id) { out = *it; return true; }
        }
        return false;
    }

    size_t size() {
        std::scoped_lock _{g_mutex};
        return g_by_resource.size();
    }

    size_t count_by_view(int view_id) {
        std::scoped_lock _{g_mutex};
        size_t n = 0;
        for (const auto& kv : g_by_resource) {
            if (kv.second.view_id == view_id) ++n;
        }
        return n;
    }

    // 2026-05-17 Task #45 — same retro-tagging helper as the SRV map.
    bool tag_by_cpu_handle(D3D12_CPU_DESCRIPTOR_HANDLE cpu, int view_id) {
        std::scoped_lock _{g_mutex};
        for (auto& kv : g_by_resource) {
            if (kv.second.cpu_handle.ptr == cpu.ptr) {
                if (kv.second.view_id == -1) {
                    kv.second.view_id = view_id;
                }
                for (auto& e : g_by_index) {
                    if (e.resource == kv.first) {
                        if (e.view_id == -1) e.view_id = view_id;
                        break;
                    }
                }
                return true;
            }
        }
        return false;
    }

    bool lookup_by_cpu_handle(D3D12_CPU_DESCRIPTOR_HANDLE cpu, Entry& out) {
        std::scoped_lock _{g_mutex};
        for (const auto& kv : g_by_resource) {
            if (kv.second.cpu_handle.ptr == cpu.ptr) {
                out = kv.second;
                return true;
            }
        }
        return false;
    }

    bool tag_resource_view(ID3D12Resource* resource, int view_id) {
        if (resource == nullptr) return false;
        std::scoped_lock _{g_mutex};
        auto it = g_by_resource.find(resource);
        if (it == g_by_resource.end()) return false;
        if (it->second.view_id == -1) it->second.view_id = view_id;
        for (auto& e : g_by_index) {
            if (e.resource == resource) {
                if (e.view_id == -1) e.view_id = view_id;
                break;
            }
        }
        return true;
    }
} // namespace sn2_fog_uav_map

extern "C" bool sn2_fog_uav_map_tag_by_cpu_handle(D3D12_CPU_DESCRIPTOR_HANDLE cpu, int view_id) {
    return sn2_fog_uav_map::tag_by_cpu_handle(cpu, view_id);
}

// ============================================================================
// 2026-05-17 SN2 fog Path A: per-view FFogUniformParameters cbuffer redirect.
//
// Background: SN2 binds the same GPU_VA (pool 2362 + offset 2259712 in PIX
// capture) as cbuffer3 for BOTH view 0 and view 1 basepasses. View 1's
// SetupFogUniformParameters call OVERWRITES the same memory location, so
// the cbuffer ends up containing whichever view's data was written last.
// Combined with per-view UV math from a properly-per-view View cbuffer,
// right eye samples the shared fog volume at coordinates that miss the
// view-0-filled region, producing the bright-sky bug.
//
// Fix surface: allocate a UEVR-owned upload buffer; hook
// SetGraphicsRootConstantBufferView; when root_param == 3, viewport is
// right-eye, and the bound GPU_VA matches SN2's shared fog cbuffer, swap
// the GPU_VA for our buffer's. View 1's basepass now reads our buffer
// independent of what view 0 / view 1 setup writes to the shared one.
// ============================================================================

// 2026-05-17 SN2 fog descriptor-swap pivot (Step D-safe):
// UEVR-owned non-shader-visible CBV_SRV_UAV heap. We create SRVs for view-1
// fog Texture3D resources into this heap and AddRef the underlying
// ID3D12Resource so it can't be freed under us. At swap time we
// CopyDescriptorsSimple from our heap (always-valid) → the bindless slot.
// This avoids the UAF crash we saw when copying from UE5's recycled staging
// heaps.
namespace sn2_view1_fog_srv_pool {
    struct Entry {
        ID3D12Resource* resource = nullptr;   // we hold a ref
        UINT64          width = 0;
        UINT            height = 0;
        UINT16          depth = 0;
        DXGI_FORMAT     format = DXGI_FORMAT_UNKNOWN;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle{};  // into our heap
    };

    static std::mutex             g_mutex;
    static ID3D12DescriptorHeap*  g_heap = nullptr;
    static SIZE_T                 g_cpu_base = 0;
    static UINT                   g_stride = 0;
    static constexpr UINT         k_capacity = 128;
    static UINT                   g_used = 0;
    // (resource_pointer → entry index in g_entries); used to dedupe.
    static std::unordered_map<ID3D12Resource*, size_t> g_by_resource;
    static std::vector<Entry>     g_entries;

    static bool ensure_heap(ID3D12Device* device) {
        if (g_heap != nullptr) return true;
        if (device == nullptr) return false;
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = k_capacity;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;  // non-shader-visible OK as source
        desc.NodeMask = 1;
        HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_heap));
        if (FAILED(hr) || g_heap == nullptr) {
            SPDLOG_WARN("[SN2-Pool] CreateDescriptorHeap failed hr=0x{:08x}", (uint32_t)hr);
            return false;
        }
        g_cpu_base = g_heap->GetCPUDescriptorHandleForHeapStart().ptr;
        g_stride   = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        g_entries.reserve(k_capacity);
        SPDLOG_WARN("[SN2-Pool] view-1 fog SRV pool created capacity={} cpu_base=0x{:x} stride={}",
                    k_capacity, (uint64_t)g_cpu_base, g_stride);
        return true;
    }

    // Recursion guard: pool.add() calls device->CreateShaderResourceView,
    // which would re-enter our create_shader_resource_view hook → infinite
    // recursion → stack overflow → silent crash. The guard makes the
    // outer hook a passthrough when set.
    thread_local bool tl_in_pool_add = false;

    // Add (or no-op if already present) a fog Texture3D resource to our
    // pool. AddRef pins the resource; CreateShaderResourceView populates
    // our heap slot. Safe to call repeatedly for the same resource.
    // MUST be called only from within D3D12Hook::create_shader_resource_view
    // (or another path that handles the recursion guard).
    static void add(ID3D12Device* device, ID3D12Resource* resource) {
        if (resource == nullptr || device == nullptr) return;
        std::scoped_lock _{g_mutex};
        if (!ensure_heap(device)) return;
        if (g_by_resource.count(resource)) return;       // already pooled
        if (g_used >= k_capacity) return;                // pool full
        // Pin the resource so its descriptor stays valid forever.
        resource->AddRef();
        // Capture dims/format from the resource's desc (resource is live since
        // we just AddRef'd — but the desc was filled at creation time and
        // never changes).
        D3D12_RESOURCE_DESC rd = resource->GetDesc();
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = rd.Format;
        sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture3D.MostDetailedMip = 0;
        sd.Texture3D.MipLevels = rd.MipLevels ? rd.MipLevels : 1;
        sd.Texture3D.ResourceMinLODClamp = 0.0f;
        D3D12_CPU_DESCRIPTOR_HANDLE h{g_cpu_base + (SIZE_T)g_used * g_stride};
        tl_in_pool_add = true;
        device->CreateShaderResourceView(resource, &sd, h);
        tl_in_pool_add = false;

        Entry e{};
        e.resource = resource;
        e.width = rd.Width;
        e.height = rd.Height;
        e.depth = rd.DepthOrArraySize;
        e.format = rd.Format;
        e.cpu_handle = h;
        g_by_resource[resource] = g_entries.size();
        g_entries.push_back(e);
        ++g_used;
        SPDLOG_WARN("[SN2-Pool] +pool slot {}/{}  res={:p} dim={}x{}x{} fmt={} cpu=0x{:x}",
                    g_used, k_capacity, (void*)resource,
                    (int)rd.Width, (int)rd.Height, (int)rd.DepthOrArraySize, (int)rd.Format,
                    (uint64_t)h.ptr);
    }

    // Find a pooled SRV whose source resource has the given (width, height,
    // depth, format) AND is NOT the given `exclude` resource. Returns a CPU
    // handle suitable for CopyDescriptorsSimple as the source. {0} if no match.
    //
    // Use case: view 1's basepass bound `exclude` (view 0's fog vol) at a
    // bindless slot. We want to swap to a "different" matching-shape
    // Texture3D, which in practice will be view 1's own fog vol (the only
    // other pooled resource matching the fog dims+format in stereo mode).
    static D3D12_CPU_DESCRIPTOR_HANDLE find_match_excluding(
        UINT64 w, UINT h, UINT16 d, DXGI_FORMAT f, ID3D12Resource* exclude)
    {
        std::scoped_lock _{g_mutex};
        for (const auto& e : g_entries) {
            if (e.resource == exclude) continue;
            if (e.width == w && e.height == h && e.depth == d && e.format == f) {
                return e.cpu_handle;
            }
        }
        return D3D12_CPU_DESCRIPTOR_HANDLE{0};
    }

    // Format-based predicate matching the fog volume signature (per RenderDoc:
    // 54×30×48 R11G11B10_FLOAT). Loose dimension bounds for resolution-variance.
    static bool is_fog_3d(const D3D12_RESOURCE_DESC& d) {
        if (d.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D) return false;
        // 2026-05-17 evening: BROADENED. Bug isn't fog (R11G11B10F 30x36x28) —
        // it's sky atmosphere LUTs (likely R16G16B16A16F or R11G11B10F at
        // various small dims). Accept any float Texture3D in a wider size
        // range so the pool catches both fog volumes AND sky LUTs.
        const bool format_ok =
            d.Format == DXGI_FORMAT_R11G11B10_FLOAT ||
            d.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
            d.Format == DXGI_FORMAT_R16G16_FLOAT ||
            d.Format == DXGI_FORMAT_R32G32B32A32_FLOAT ||
            d.Format == DXGI_FORMAT_R10G10B10A2_UNORM;
        if (!format_ok) return false;
        if (d.Width  < 8  || d.Width  > 512) return false;
        if (d.Height < 8  || d.Height > 512) return false;
        if (d.DepthOrArraySize < 8 || d.DepthOrArraySize > 512) return false;
        return true;
    }

    static size_t size() {
        std::scoped_lock _{g_mutex};
        return g_entries.size();
    }
}

namespace sn2_fog_path_a {
    static bool                  g_enabled = false;          // env var gate
    static std::atomic<bool>     g_buffer_ready{false};
    static ID3D12Resource*       g_buffer = nullptr;         // UEVR-owned upload buffer
    static D3D12_GPU_VIRTUAL_ADDRESS g_buffer_gpu_va = 0;
    static uint8_t*              g_buffer_mapped = nullptr;
    static UINT                  g_buffer_size = 256;        // CBV alignment; FFogUB is 0x180 but 256 covers it
    static std::atomic<D3D12_GPU_VIRTUAL_ADDRESS> g_shared_fog_cbuffer_gpu_va{0};
    static std::atomic<uint64_t> g_redirect_count{0};
    // 2026-05-17 viewport-detection-doesn't-work fallback: FixV5 hook arms
    // this counter (set to N) when view 1's setup completes. The cbuffer
    // redirect hook decrements on each matching binding and only redirects
    // when value was > 0 before the decrement. This lets us redirect EXACTLY
    // the bindings that follow view 1's FixV5 within the same frame, no
    // viewport state required.
    static std::atomic<int>      g_redirect_arm_count{0};

    inline bool enabled_env() {
        static const bool en = []() {
            char v[8]{};
            const auto n = GetEnvironmentVariableA("UEVR_SUBNAUTICA2_FOG_PATH_A", v, sizeof(v));
            if (n == 0) return false;
            return v[0] == '1';
        }();
        return en;
    }

    bool ensure_buffer(ID3D12Device* device) {
        if (g_buffer_ready.load(std::memory_order_acquire)) return true;
        if (device == nullptr) return false;

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Alignment = 0;
        desc.Width = g_buffer_size;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        HRESULT hr = device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE,
            &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&g_buffer));
        if (FAILED(hr) || g_buffer == nullptr) {
            SPDLOG_WARN("[SN2-PathA] CreateCommittedResource for fog cbuffer failed hr=0x{:08x}", (uint32_t)hr);
            return false;
        }
        g_buffer_gpu_va = g_buffer->GetGPUVirtualAddress();

        D3D12_RANGE no_read{0, 0};
        hr = g_buffer->Map(0, &no_read, (void**)&g_buffer_mapped);
        if (FAILED(hr) || g_buffer_mapped == nullptr) {
            SPDLOG_WARN("[SN2-PathA] Map failed hr=0x{:08x}", (uint32_t)hr);
            g_buffer->Release();
            g_buffer = nullptr;
            return false;
        }
        // 2026-05-18 SN2 SkyAtmosFix: piggyback to install the global
        // ID3D12Resource::Map vtable hook using this freshly-created buffer
        // as the vtable sample. Subsequent Map() calls on ANY upload buffer
        // will route through our hook so we can register their CPU pointers.
        sn2_upload_buf_map::install_map_hook(g_buffer);

        // Initial content: zero. Caller is expected to populate before any
        // right-eye basepass runs (otherwise view 1 sees a black/zero fog
        // cbuffer — a valid diagnostic test for the redirect mechanism).
        memset(g_buffer_mapped, 0, g_buffer_size);

        g_buffer_ready.store(true, std::memory_order_release);
        SPDLOG_INFO("[SN2-PathA] view-1 fog cbuffer created gpu_va=0x{:x} size={}",
                    (uint64_t)g_buffer_gpu_va, g_buffer_size);
        return true;
    }

    // Capture the "shared" SN2 fog cbuffer GPU_VA the first time we see it
    // bound at root_param 3 on a left-eye basepass draw. Right-eye draws
    // binding the same GPU_VA are what we redirect.
    void note_left_eye_binding(D3D12_GPU_VIRTUAL_ADDRESS gpu_va) {
        D3D12_GPU_VIRTUAL_ADDRESS prev = g_shared_fog_cbuffer_gpu_va.load(std::memory_order_relaxed);
        if (prev == 0) {
            if (g_shared_fog_cbuffer_gpu_va.compare_exchange_strong(prev, gpu_va, std::memory_order_acq_rel)) {
                SPDLOG_INFO("[SN2-PathA] captured shared fog cbuffer gpu_va=0x{:x}", (uint64_t)gpu_va);
            }
        }
    }

    D3D12_GPU_VIRTUAL_ADDRESS shared_gpu_va() {
        return g_shared_fog_cbuffer_gpu_va.load(std::memory_order_relaxed);
    }
    D3D12_GPU_VIRTUAL_ADDRESS buffer_gpu_va() {
        return g_buffer_gpu_va;
    }

    void mark_redirected() { g_redirect_count.fetch_add(1, std::memory_order_relaxed); }

    // Cross-TU API: writes `size` bytes from `data` into the mapped upload
    // buffer. Safe to call on any thread (the mapped pointer is stable for
    // the buffer's lifetime once `g_buffer_ready` flips to true). No-op
    // until the redirect hook has lazily created the buffer on its first
    // right-eye binding capture — that's fine because FixV5 fires on the
    // render thread BEFORE the right-eye basepass binds the cbuffer, so
    // after frame 1's redirect creates the buffer, frame 2+'s FixV5 calls
    // will populate it before the GPU executes view 1's basepass.
    bool buffer_ready() {
        return g_buffer_ready.load(std::memory_order_acquire);
    }

    void write_buffer(const void* data, size_t size) {
        if (data == nullptr) return;
        if (!g_buffer_ready.load(std::memory_order_acquire)) return;
        if (g_buffer_mapped == nullptr) return;
        if (size > g_buffer_size) size = g_buffer_size;
        memcpy(g_buffer_mapped, data, size);
    }

    // 2026-05-17 viewport-detection fallback API. Called from FixV5 hook
    // (FFakeStereoRenderingHook) at the end of view 1's setup-fog call to
    // arm the next N matching cbuffer bindings for redirect. `count=2`
    // assumes UE5 binds the fog cbuffer once for view 1's basepass and we
    // want to redirect just that one binding (extra count is safety margin
    // for additional binds within the same per-view scope, e.g., translucent
    // pass). The cbuffer redirect hook decrements on each redirect.
    void arm_redirect(int count) {
        g_redirect_arm_count.store(count, std::memory_order_release);
    }
    // Returns true if redirect should happen, decrementing the arm count.
    bool try_consume_redirect() {
        int v = g_redirect_arm_count.load(std::memory_order_acquire);
        while (v > 0) {
            if (g_redirect_arm_count.compare_exchange_weak(v, v - 1,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
                return true;
            }
        }
        return false;
    }
} // namespace sn2_fog_path_a

// 2026-05-18 SN2 SkyAtmosFix — upload buffer registry populated by hooking
// ID3D12Resource::Map at the vtable level. All COM instances created by the
// same D3D12 device share the same vtable, so patching the Map slot once
// catches every future Map() call. This gives us the (gpu_va, cpu_ptr, size)
// tuple for every mapped upload buffer — exactly what we need to write the
// good cb0 pattern at the right location.
namespace sn2_upload_buf_map {
    struct Entry {
        ID3D12Resource* res;
        D3D12_GPU_VIRTUAL_ADDRESS gpu_va_base;
        uint64_t size;
        uint8_t* cpu_ptr;
    };
    static std::mutex g_mutex;
    static std::vector<Entry> g_entries; // small (~hundreds), linear scan is fine

    using MapFn = HRESULT (STDMETHODCALLTYPE*)(ID3D12Resource*, UINT, const D3D12_RANGE*, void**);
    static MapFn g_orig_map = nullptr;
    static bool g_hook_installed = false;

    HRESULT STDMETHODCALLTYPE Map_Hook(ID3D12Resource* self, UINT sub, const D3D12_RANGE* read_range, void** data) {
        HRESULT hr = g_orig_map(self, sub, read_range, data);
        if (SUCCEEDED(hr) && data && *data && sub == 0) {
            // Only track buffer resources (Dim == BUFFER).
            D3D12_RESOURCE_DESC desc = self->GetDesc();
            if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER && desc.Width >= 32) {
                D3D12_GPU_VIRTUAL_ADDRESS va = self->GetGPUVirtualAddress();
                if (va != 0) {
                    std::scoped_lock _{g_mutex};
                    bool dup = false;
                    for (auto& e : g_entries) {
                        if (e.res == self) {
                            e.cpu_ptr = (uint8_t*)*data;
                            e.gpu_va_base = va;
                            e.size = desc.Width;
                            dup = true;
                            break;
                        }
                    }
                    if (!dup) {
                        g_entries.push_back({self, va, desc.Width, (uint8_t*)*data});
                        static std::atomic<uint64_t> n{0};
                        const auto idx = n.fetch_add(1, std::memory_order_relaxed);
                        if (idx < 16 || (idx % 100) == 0) {
                            SPDLOG_WARN("[SN2-UploadBufMap] tracked buffer #{} gpu_va=0x{:x} size=0x{:x} cpu=0x{:x}",
                                        idx + 1, (uint64_t)va, desc.Width, (uintptr_t)*data);
                        }
                    }
                }
            }
        }
        return hr;
    }

    // Patch the Map slot in ID3D12Resource's vtable. All instances sharing
    // this vtable (which on a given device is ALL of them) will route Map
    // calls through Map_Hook.
    bool install_map_hook(ID3D12Resource* sample_res) {
        if (g_hook_installed || sample_res == nullptr) return g_hook_installed;
        void** vtbl = *(void***)sample_res;
        // ID3D12Resource::Map is at vtable index 8.
        void** slot = &vtbl[8];
        g_orig_map = (MapFn)*slot;
        if (g_orig_map == nullptr) return false;
        DWORD old_prot = 0;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old_prot)) return false;
        *slot = (void*)&Map_Hook;
        VirtualProtect(slot, sizeof(void*), old_prot, &old_prot);
        g_hook_installed = true;
        SPDLOG_WARN("[SN2-UploadBufMap] Map vtable hook installed: orig=0x{:x} new=0x{:x}",
                    (uintptr_t)g_orig_map, (uintptr_t)&Map_Hook);
        return true;
    }

    uint8_t* gpu_va_to_cpu(D3D12_GPU_VIRTUAL_ADDRESS gpu_va, uint64_t min_size) {
        std::scoped_lock _{g_mutex};
        for (auto& e : g_entries) {
            if (gpu_va >= e.gpu_va_base && gpu_va + min_size <= e.gpu_va_base + e.size) {
                return e.cpu_ptr + (gpu_va - e.gpu_va_base);
            }
        }
        return nullptr;
    }

    // SEH-safe helper. CORRECT cb0 row 30/31 bug signature, derived from
    // direct byte dump of view 1's broken cb0 (sky_event3714_rp2_cb0.bin):
    //
    //   bytes 464..479 (row 29 of broken cb0) = (0, 1.0f, 0, 0)
    //   bytes 480..511 (rows 30+31)           = 8 × 1.0f                ← THE BUG
    //   bytes 512..515 (start of row 32, if any) != 1.0f
    //
    // The (0, 1.0f, 0, 0) row-29 prefix is the unique fingerprint that
    // distinguishes the SkyAtmosphere PS's broken cb0 from other 8×1.0f
    // patterns in the engine's upload buffers.
    static uint64_t scan_one_buffer(uint8_t* cpu_ptr, uint64_t size) {
        uint64_t patches = 0;
        const uint32_t one = 0x3F800000u;
        if (size < 64) return 0;
        const size_t end = (size_t)size - 32;
        __try {
            for (size_t off = 16; off <= end; off += 16) {
                // Core: 8 consecutive 1.0f at offset..offset+31
                const uint32_t* w = reinterpret_cast<const uint32_t*>(cpu_ptr + off);
                if (!(w[0] == one && w[1] == one && w[2] == one && w[3] == one &&
                      w[4] == one && w[5] == one && w[6] == one && w[7] == one)) continue;
                // Prefix: row 29 must EXACTLY be (0, 1.0f, 0, 0)
                const uint32_t* prev = reinterpret_cast<const uint32_t*>(cpu_ptr + off - 16);
                if (prev[0] != 0 || prev[1] != one || prev[2] != 0 || prev[3] != 0) continue;
                // Suffix: byte off+32 must NOT be 1.0f
                if (off + 36 <= size) {
                    const uint32_t* nxt = reinterpret_cast<const uint32_t*>(cpu_ptr + off + 32);
                    if (*nxt == one) continue;
                }
                // PATCH: replace 8×1.0f with (0,0,0,1.0) × 2 — the view-0 layout
                uint32_t* mut = const_cast<uint32_t*>(w);
                mut[0] = 0; mut[1] = 0; mut[2] = 0; mut[3] = one;
                mut[4] = 0; mut[5] = 0; mut[6] = 0; mut[7] = one;
                ++patches;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return patches;
    }

    uint64_t scan_tracked_buffers_for_bad_cb0_tail() {
        std::scoped_lock _{g_mutex};
        uint64_t patches = 0;
        for (auto& e : g_entries) {
            if (e.cpu_ptr == nullptr || e.size < 512) continue;
            const uint64_t p = scan_one_buffer(e.cpu_ptr, e.size);
            if (p > 0) {
                SPDLOG_WARN("[SN2-SkyAtmosScan]   patched={} in buf gpu_va=0x{:x} size=0x{:x} (size_dec={})",
                            p, (uint64_t)e.gpu_va_base, e.size, e.size);
            }
            patches += p;
        }
        return patches;
    }

    size_t entry_count() {
        std::scoped_lock _{g_mutex};
        return g_entries.size();
    }

    // 2026-05-18 SN2 SkyAtmosFix v2: hook ID3D12Device::CreateCommittedResource
    // so we catch upload buffers at CREATION time (not just at Map time). The
    // engine maps its cbuffer pool ONCE at creation and keeps the pointer for
    // the lifetime, so the existing Map vtable hook misses pre-existing pools.
    // CreateCommittedResource fires for every NEW buffer including small per-frame
    // pool allocations, so this catches everything the engine creates after we
    // install (which includes the cb0 backing buffer at GPU VA ~0x14210000).
    using CreateCommittedResourceFn = HRESULT (STDMETHODCALLTYPE*)(
        ID3D12Device*, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS,
        const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES,
        const D3D12_CLEAR_VALUE*, REFIID, void**);
    static CreateCommittedResourceFn g_orig_create = nullptr;
    static bool g_device_hook_installed = false;

    HRESULT STDMETHODCALLTYPE CreateCommittedResource_Hook(
        ID3D12Device* self, const D3D12_HEAP_PROPERTIES* heap_props, D3D12_HEAP_FLAGS heap_flags,
        const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE* clear, REFIID riid, void** ppv) {
        HRESULT hr = g_orig_create(self, heap_props, heap_flags, desc, initial_state, clear, riid, ppv);
        if (SUCCEEDED(hr) && ppv && *ppv && heap_props != nullptr && desc != nullptr) {
            // Only UPLOAD heap buffers (where cb0 lives)
            if (heap_props->Type == D3D12_HEAP_TYPE_UPLOAD &&
                desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
                desc->Width >= 32) {
                ID3D12Resource* res = (ID3D12Resource*)*ppv;
                // Install Map vtable hook from THIS resource's vtable (idempotent).
                // All ID3D12Resource instances share the same vtable.
                install_map_hook(res);

                // DON'T call Map() ourselves — it interferes with UE5's ring-buffer
                // allocator (caused "Failed to get back buffer" errors in test).
                // The Map vtable hook installed above will catch the engine's own
                // Map() call when it happens.
                static std::atomic<uint64_t> ncreate{0};
                const auto idx = ncreate.fetch_add(1, std::memory_order_relaxed);
                if (idx < 16 || (idx % 200) == 0) {
                    SPDLOG_WARN("[SN2-CreateRes] hooked CreateCommittedResource #{} size=0x{:x}",
                                idx + 1, desc->Width);
                }
            }
        }
        return hr;
    }

    bool install_device_hook(ID3D12Device* device) {
        if (g_device_hook_installed || device == nullptr) return g_device_hook_installed;
        void** vtbl = *(void***)device;
        // ID3D12Device::CreateCommittedResource is at vtable index 27.
        void** slot = &vtbl[27];
        g_orig_create = (CreateCommittedResourceFn)*slot;
        if (g_orig_create == nullptr) return false;
        DWORD old_prot = 0;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old_prot)) return false;
        *slot = (void*)&CreateCommittedResource_Hook;
        VirtualProtect(slot, sizeof(void*), old_prot, &old_prot);
        g_device_hook_installed = true;
        SPDLOG_WARN("[SN2-CreateRes] CreateCommittedResource vtable hook installed: orig=0x{:x} new=0x{:x}",
                    (uintptr_t)g_orig_create, (uintptr_t)&CreateCommittedResource_Hook);
        return true;
    }

    // === sn2_fixed_cb0: own a 512-byte UPLOAD buffer with view-0 correct cb0 contents ===
    // (Defined here inside sn2_upload_buf_map for build-order convenience, then aliased
    // via the sn2_fixed_cb0 namespace below.)
    static ID3D12Resource* g_fixed_cb0_buf = nullptr;
    static D3D12_GPU_VIRTUAL_ADDRESS g_fixed_cb0_va = 0;
    static uint8_t* g_fixed_cb0_cpu = nullptr;
    static std::mutex g_fixed_cb0_mutex;

    D3D12_GPU_VIRTUAL_ADDRESS create_fixed_cb0(ID3D12Device* device) {
        std::scoped_lock _{g_fixed_cb0_mutex};
        if (g_fixed_cb0_va != 0) return g_fixed_cb0_va;
        if (device == nullptr) return 0;

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = 512;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&g_fixed_cb0_buf));
        if (FAILED(hr) || g_fixed_cb0_buf == nullptr) {
            SPDLOG_WARN("[SN2-FixedCB0] CreateCommittedResource failed hr=0x{:08x}", (uint32_t)hr);
            return 0;
        }
        g_fixed_cb0_va = g_fixed_cb0_buf->GetGPUVirtualAddress();
        D3D12_RANGE no_read{0, 0};
        hr = g_fixed_cb0_buf->Map(0, &no_read, (void**)&g_fixed_cb0_cpu);
        if (FAILED(hr) || g_fixed_cb0_cpu == nullptr) {
            SPDLOG_WARN("[SN2-FixedCB0] Map failed hr=0x{:08x}", (uint32_t)hr);
            g_fixed_cb0_buf->Release();
            g_fixed_cb0_buf = nullptr;
            g_fixed_cb0_va = 0;
            return 0;
        }
        // Write view-0 correct cb0 contents:
        // - bytes 0..447 = 0
        // - bytes 448..463 (row 28) = (0,0,0,1.0f)
        // - bytes 464..479 (row 29) = (0,0,0,1.0f)
        // - bytes 480..495 (row 30) = (0,0,0,1.0f)
        // - bytes 496..511 (row 31) = (0,0,0,1.0f)
        memset(g_fixed_cb0_cpu, 0, 512);
        const uint32_t one = 0x3F800000u;
        for (int row = 28; row <= 31; ++row) {
            *(uint32_t*)(g_fixed_cb0_cpu + row * 16 + 12) = one;
        }
        SPDLOG_WARN("[SN2-FixedCB0] CREATED fixed cb0 buffer at gpu_va=0x{:x} cpu=0x{:x}",
                    (uint64_t)g_fixed_cb0_va, (uintptr_t)g_fixed_cb0_cpu);
        return g_fixed_cb0_va;
    }

    // SEH-safe helper. Must live in its own function to avoid C2712.
    bool try_inline_patch_cb0(uint8_t* cpu) {
        const uint32_t one = 0x3F800000u;
        bool patched = false;
        __try {
            const uint32_t* row29 = reinterpret_cast<const uint32_t*>(cpu + 464);
            const uint32_t* row30 = reinterpret_cast<const uint32_t*>(cpu + 480);
            if (row29[0] == 0 && row29[1] == one && row29[2] == 0 && row29[3] == 0 &&
                row30[0] == one && row30[1] == one && row30[2] == one && row30[3] == one &&
                row30[4] == one && row30[5] == one && row30[6] == one && row30[7] == one) {
                uint32_t* mut = const_cast<uint32_t*>(row30);
                mut[0] = 0; mut[1] = 0; mut[2] = 0; mut[3] = one;
                mut[4] = 0; mut[5] = 0; mut[6] = 0; mut[7] = one;
                patched = true;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return patched;
    }
} // namespace sn2_upload_buf_map

namespace sn2_fixed_cb0 {
    D3D12_GPU_VIRTUAL_ADDRESS get_or_create(ID3D12Device* device) {
        return sn2_upload_buf_map::create_fixed_cb0(device);
    }
}

namespace sn2_command_signature_registry {
    static std::mutex g_mutex;
    static std::unordered_map<ID3D12CommandSignature*, Entry> g_entries;
    static std::atomic<uint64_t> g_next_id{1};

    static const char* arg_type_name(D3D12_INDIRECT_ARGUMENT_TYPE type) {
        switch (type) {
        case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW: return "DRAW";
        case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED: return "DRAW_INDEXED";
        case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH: return "DISPATCH";
        case D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW: return "VBV";
        case D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW: return "IBV";
        case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT: return "CONSTANT";
        case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW: return "CBV";
        case D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW: return "SRV";
        case D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW: return "UAV";
        case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS: return "DISPATCH_RAYS";
        case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH: return "DISPATCH_MESH";
        default: return "UNKNOWN";
        }
    }

    static void append_arg_layout(std::string& out, const D3D12_INDIRECT_ARGUMENT_DESC& arg) {
        if (!out.empty()) out += ",";
        out += arg_type_name(arg.Type);
        char tmp[96]{};
        switch (arg.Type) {
        case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT:
            std::snprintf(tmp, sizeof(tmp), "(rp=%u,off=%u,n=%u)",
                arg.Constant.RootParameterIndex,
                arg.Constant.DestOffsetIn32BitValues,
                arg.Constant.Num32BitValuesToSet);
            out += tmp;
            break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW:
            std::snprintf(tmp, sizeof(tmp), "(rp=%u)", arg.ConstantBufferView.RootParameterIndex);
            out += tmp;
            break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW:
            std::snprintf(tmp, sizeof(tmp), "(rp=%u)", arg.ShaderResourceView.RootParameterIndex);
            out += tmp;
            break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW:
            std::snprintf(tmp, sizeof(tmp), "(rp=%u)", arg.UnorderedAccessView.RootParameterIndex);
            out += tmp;
            break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW:
            std::snprintf(tmp, sizeof(tmp), "(slot=%u)", arg.VertexBuffer.Slot);
            out += tmp;
            break;
        default:
            break;
        }
    }

    void record(ID3D12CommandSignature* signature, const D3D12_COMMAND_SIGNATURE_DESC* desc) {
        if (signature == nullptr || desc == nullptr || desc->NumArgumentDescs == 0 || desc->pArgumentDescs == nullptr) {
            return;
        }

        Entry entry{};
        entry.id = g_next_id.fetch_add(1, std::memory_order_relaxed);
        entry.byte_stride = desc->ByteStride;
        entry.args.reserve(desc->NumArgumentDescs);
        for (UINT i = 0; i < desc->NumArgumentDescs; ++i) {
            const auto& src = desc->pArgumentDescs[i];
            ArgumentInfo dst{};
            dst.type = src.Type;
            switch (src.Type) {
            case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
                entry.has_dispatch = true;
                break;
            case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW:
                entry.has_draw = true;
                break;
            case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
                entry.has_draw_indexed = true;
                break;
            case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH:
                entry.has_dispatch_mesh = true;
                break;
            case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT:
                dst.root_parameter_index = src.Constant.RootParameterIndex;
                dst.dest_offset_words = src.Constant.DestOffsetIn32BitValues;
                dst.num_values = src.Constant.Num32BitValuesToSet;
                break;
            case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW:
                dst.root_parameter_index = src.ConstantBufferView.RootParameterIndex;
                break;
            case D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW:
                dst.root_parameter_index = src.ShaderResourceView.RootParameterIndex;
                break;
            case D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW:
                dst.root_parameter_index = src.UnorderedAccessView.RootParameterIndex;
                break;
            case D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW:
                dst.slot = src.VertexBuffer.Slot;
                break;
            default:
                break;
            }
            entry.args.push_back(dst);
            append_arg_layout(entry.layout, src);
        }

        {
            std::scoped_lock _{g_mutex};
            g_entries[signature] = entry;
        }

        static std::atomic<uint64_t> log_count{0};
        const auto n = log_count.fetch_add(1, std::memory_order_relaxed);
        if (sn2_diag_strict_enabled() || n < 64 || (n % 256) == 0) {
            SPDLOG_WARN("[SN2-CmdSig] id={} ptr={:p} stride={} count={} kind={} layout={}",
                entry.id, static_cast<void*>(signature), entry.byte_stride,
                entry.args.size(), kind(entry), entry.layout);
        }
    }

    bool lookup(ID3D12CommandSignature* signature, Entry& out) {
        if (signature == nullptr) return false;
        std::scoped_lock _{g_mutex};
        const auto it = g_entries.find(signature);
        if (it == g_entries.end()) return false;
        out = it->second;
        return true;
    }

    const char* kind(const Entry& entry) {
        if (entry.has_dispatch) return "DISPATCH";
        if (entry.has_dispatch_mesh) return "DISPATCH_MESH";
        if (entry.has_draw_indexed) return "DRAW_INDEXED";
        if (entry.has_draw) return "DRAW";
        return "UNKNOWN";
    }
} // namespace sn2_command_signature_registry

// 2026-05-19 GPU READBACK INFRASTRUCTURE
// ===========================================================================
// Lets us inspect the contents of GPU-only buffers (e.g. ExecuteIndirect
// argument buffers, indirect-dispatch arg buffers, constant buffers in
// DEFAULT heap, UAV-written buffers). The standard D3D12 pattern:
//   1. Create a READBACK heap buffer big enough for what we want to capture
//   2. Insert CopyBufferRegion(readback, src) into the GAME's command list
//   3. When the game submits and executes its command list, the copy runs
//   4. Map the readback buffer to access the data CPU-side
//   5. Use a fence to know when the copy is complete (don't read before then)
//
// Design: a ring of NUM_SLOTS small slots (256 bytes each) backed by a single
// big READBACK heap resource. Each schedule_copy() picks the next slot,
// issues a CopyBufferRegion, and remembers the slot's tag + caller info.
// Periodically (on Present), poll the fence: for any slot whose fence value
// has been reached, the data is ready and can be log-dumped.
//
// The READBACK heap requires the source to be in COPY_SOURCE state. For
// BUFFER resources, D3D12 allows implicit promotion from common/INDIRECT_
// ARGUMENT/etc. to COPY_SOURCE, then implicit decay back, so we can copy
// without explicit barriers if the buffer was in a promotable state.
namespace sn2_gpu_readback {
    constexpr size_t SLOT_SIZE  = 256;       // bytes per slot
    constexpr size_t NUM_SLOTS  = 1024;      // total ring capacity
    constexpr size_t TOTAL_SIZE = SLOT_SIZE * NUM_SLOTS;

    // Slot struct is forward-declared above; use the same definition.

    static std::mutex            g_mutex;
    static Microsoft::WRL::ComPtr<ID3D12Resource> g_buffer;
    static uint8_t*              g_mapped = nullptr;
    static Microsoft::WRL::ComPtr<ID3D12Fence> g_fence;
    static std::atomic<uint64_t> g_next_slot{0};
    static std::atomic<uint64_t> g_fence_counter{0};
    static std::atomic<uint64_t> g_last_signaled{0};
    static std::array<Slot, NUM_SLOTS> g_slots{};
    static bool                  g_initialized = false;
    static std::atomic<bool>     g_init_failed{false};

    bool init(ID3D12Device* device) {
        std::scoped_lock _{g_mutex};
        if (g_initialized) return true;
        if (g_init_failed.load(std::memory_order_relaxed)) return false;
        if (device == nullptr) return false;

        D3D12_HEAP_PROPERTIES heap_props{};
        heap_props.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Alignment          = 0;
        desc.Width              = TOTAL_SIZE;
        desc.Height             = 1;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

        HRESULT hr = device->CreateCommittedResource(
            &heap_props,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&g_buffer));
        if (FAILED(hr)) {
            SPDLOG_ERROR("[SN2-Readback] CreateCommittedResource failed hr=0x{:x}", (uint32_t)hr);
            g_init_failed.store(true);
            return false;
        }

        D3D12_RANGE map_range{0, TOTAL_SIZE};
        void* mapped = nullptr;
        hr = g_buffer->Map(0, &map_range, &mapped);
        if (FAILED(hr) || mapped == nullptr) {
            SPDLOG_ERROR("[SN2-Readback] Map failed hr=0x{:x}", (uint32_t)hr);
            g_buffer.Reset();
            g_init_failed.store(true);
            return false;
        }
        g_mapped = static_cast<uint8_t*>(mapped);

        hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence));
        if (FAILED(hr)) {
            SPDLOG_ERROR("[SN2-Readback] CreateFence failed hr=0x{:x}", (uint32_t)hr);
            g_buffer->Unmap(0, nullptr);
            g_buffer.Reset();
            g_mapped = nullptr;
            g_init_failed.store(true);
            return false;
        }

        g_initialized = true;
        SPDLOG_WARN("[SN2-Readback] Initialized: {} slots x {} bytes = {} bytes total at cpu={:p}",
            NUM_SLOTS, SLOT_SIZE, TOTAL_SIZE, (void*)g_mapped);
        return true;
    }

    bool initialized() {
        std::scoped_lock _{g_mutex};
        return g_initialized;
    }

    // Schedule a copy of `size` bytes from `src+src_offset` into our readback ring.
    // Returns the slot index, or UINT64_MAX on failure. Caller must then arrange
    // for the GAME's command queue to call queue->Signal(g_fence, fence_value)
    // after executing the command list — see signal_after_execute().
    //
    // The source buffer must be in a state promotable to COPY_SOURCE (true for
    // most buffers — D3D12 implicitly promotes BUFFER resources from common,
    // INDIRECT_ARGUMENT, etc., then implicitly decays back).
    uint64_t schedule_copy(ID3D12GraphicsCommandList* cl,
                           ID3D12Resource* src,
                           UINT64 src_offset,
                           UINT64 size,
                           const char* tag,
                           uint64_t scheduled_frame) {
        if (!g_initialized || src == nullptr || cl == nullptr) return UINT64_MAX;
        if (size == 0 || size > SLOT_SIZE) return UINT64_MAX;

        const uint64_t slot_index = g_next_slot.fetch_add(1, std::memory_order_relaxed) % NUM_SLOTS;
        const uint64_t dst_offset = slot_index * SLOT_SIZE;

        // Issue the copy. CopyBufferRegion is safe to call on any open command
        // list; D3D12 will implicitly promote the source buffer to COPY_SOURCE
        // and the dest (our READBACK buffer was created in COPY_DEST state).
        cl->CopyBufferRegion(g_buffer.Get(), dst_offset, src, src_offset, size);

        // Stamp the slot metadata. The fence_value is set by signal_after_execute.
        {
            std::scoped_lock _{g_mutex};
            Slot& s = g_slots[slot_index];
            s.source_va        = src->GetGPUVirtualAddress() + src_offset;
            s.size             = size;
            s.scheduled_frame  = scheduled_frame;
            s.tag              = tag;
            s.fence_value      = 0;  // will be set by signal_after_execute
            s.data_ready       = false;
            s.dumped           = false;
        }
        return slot_index;
    }

    // After the game submits a command list with scheduled copies, call this on
    // the command queue to issue a Signal that marks all earlier copies as ready.
    // We then poll g_fence to find completed slots.
    void signal_after_execute(ID3D12CommandQueue* queue) {
        if (!g_initialized || queue == nullptr) return;
        const uint64_t fv = g_fence_counter.fetch_add(1, std::memory_order_relaxed) + 1;
        // Stamp all pending (fence_value == 0) slots with this fence value
        {
            std::scoped_lock _{g_mutex};
            for (auto& s : g_slots) {
                if (s.size != 0 && s.fence_value == 0) {
                    s.fence_value = fv;
                }
            }
        }
        queue->Signal(g_fence.Get(), fv);
    }

    // Drain all completed slots and log their bytes to spdlog.
    void drain_to_log() {
        if (!g_initialized) return;
        const uint64_t completed = g_fence->GetCompletedValue();
        g_last_signaled.store(completed, std::memory_order_relaxed);
        std::scoped_lock _{g_mutex};
        for (uint64_t i = 0; i < NUM_SLOTS; ++i) {
            Slot& s = g_slots[i];
            if (s.size == 0 || s.dumped) continue;
            if (s.fence_value == 0 || s.fence_value > completed) continue;
            const uint8_t* cpu = g_mapped + (i * SLOT_SIZE);
            const uint32_t* u = reinterpret_cast<const uint32_t*>(cpu);

            // If this slot was tagged "MAIN-LEFT-<hex>", route the bytes to
            // sn2_compute_capture so the right-eye substitute path can use
            // them next frame.
            if (s.tag != nullptr && std::strncmp(s.tag, "MAIN-LEFT-", 10) == 0) {
                const uint32_t cs_crc = (uint32_t)std::strtoul(s.tag + 10, nullptr, 16);
                if (cs_crc != 0) {
                    sn2_compute_capture::on_left_main_args_ready(cs_crc, cpu, s.size);
                }
            }

            SPDLOG_WARN("[SN2-Readback-Done] tag={} src_va=0x{:x} sz={} dispatch=({},{},{}) words=[{:08x} {:08x} {:08x} {:08x} {:08x} {:08x} {:08x} {:08x}]",
                s.tag != nullptr ? s.tag : "?",
                (uint64_t)s.source_va, s.size,
                u[0], u[1], u[2],
                u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7]);
            s.dumped = true;
        }
    }

    void shutdown() {
        std::scoped_lock _{g_mutex};
        if (g_buffer != nullptr && g_mapped != nullptr) {
            g_buffer->Unmap(0, nullptr);
        }
        g_mapped = nullptr;
        g_buffer.Reset();
        g_fence.Reset();
        g_initialized = false;
    }
} // namespace sn2_gpu_readback

// 2026-05-19 SN2 INDIRECT ARG INJECT — UPLOAD-heap buffer we control to
// substitute right-eye indirect-dispatch arg buffers with LEFT-eye-captured
// values.
namespace sn2_indirect_arg_inject {
    constexpr size_t TOTAL_SIZE = 4096;  // up to 256 dispatches × 16 bytes (with alignment)
    static std::mutex g_mutex;
    static Microsoft::WRL::ComPtr<ID3D12Resource> g_buf;
    static uint8_t* g_mapped = nullptr;
    static bool g_init_done = false;
    static bool g_init_failed = false;

    bool init(ID3D12Device* device) {
        std::scoped_lock _{g_mutex};
        if (g_init_done) return true;
        if (g_init_failed) return false;
        if (device == nullptr) return false;
        D3D12_HEAP_PROPERTIES heap_props{};
        heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = TOTAL_SIZE;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        HRESULT hr = device->CreateCommittedResource(
            &heap_props, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_buf));
        if (FAILED(hr)) { g_init_failed = true; return false; }
        D3D12_RANGE r{0, 0};
        void* m = nullptr;
        hr = g_buf->Map(0, &r, &m);
        if (FAILED(hr) || m == nullptr) { g_buf.Reset(); g_init_failed = true; return false; }
        g_mapped = static_cast<uint8_t*>(m);
        std::memset(g_mapped, 0, TOTAL_SIZE);
        g_init_done = true;
        SPDLOG_WARN("[SN2-ArgInject] Initialized {} bytes UPLOAD buffer at cpu={:p} gpu_va=0x{:x}",
            TOTAL_SIZE, (void*)g_mapped, (uint64_t)g_buf->GetGPUVirtualAddress());
        return true;
    }

    bool initialized() {
        std::scoped_lock _{g_mutex};
        return g_init_done;
    }

    void write_bytes(uint64_t offset, const void* data, size_t size) {
        std::scoped_lock _{g_mutex};
        if (!g_init_done || g_mapped == nullptr) return;
        if (offset + size > TOTAL_SIZE) return;
        std::memcpy(g_mapped + offset, data, size);
    }

    ID3D12Resource* resource() {
        std::scoped_lock _{g_mutex};
        return g_buf.Get();
    }

    D3D12_GPU_VIRTUAL_ADDRESS base_gpu_va() {
        std::scoped_lock _{g_mutex};
        if (!g_init_done || g_buf == nullptr) return 0;
        return g_buf->GetGPUVirtualAddress();
    }
} // namespace sn2_indirect_arg_inject

// 2026-05-20 SN2 RT SNAPSHOT — capture 2D render-target pixels to disk for
// per-eye divergence analysis. Each captured texture is copied (via
// CopyTextureRegion to a buffer-laid-out readback resource) into a slot of
// a permanently-mapped READBACK heap buffer. After GPU completes the copy
// (fence-gated, drained per Present), we write the pixels to PPM (P6) on
// disk: C:\Users\ellio\AppData\Local\Temp\sn2_rt_<tag>_<frame>.ppm.
namespace sn2_rt_snapshot {
    constexpr size_t NUM_SLOTS = 8;
    constexpr size_t SLOT_BYTES = 8 * 1024 * 1024;  // 8 MB/slot; enough for 1280×720 RGBA + alignment
    constexpr size_t TOTAL_SIZE = NUM_SLOTS * SLOT_BYTES;

    struct Slot {
        uint32_t   width{};
        uint32_t   height{};
        uint32_t   row_pitch{};
        DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
        uint64_t   byte_size{};
        uint64_t   fence_value{};
        char       tag[64]{};
        uint64_t   frame{};
        bool       saved{};
    };

    static std::mutex g_mutex;
    static Microsoft::WRL::ComPtr<ID3D12Resource> g_buffer;
    static uint8_t* g_mapped = nullptr;
    static Microsoft::WRL::ComPtr<ID3D12Fence> g_fence;
    static std::atomic<uint64_t> g_next_slot{0};
    static std::atomic<uint64_t> g_fence_counter{0};
    static std::atomic<uint64_t> g_frame_counter{0};
    static std::array<Slot, NUM_SLOTS> g_slots{};
    static bool g_initialized = false;
    static bool g_init_failed = false;

    static const bool g_enabled = []() {
        const char* env = std::getenv("UEVR_SN2_RT_SNAPSHOT");
        return env != nullptr && env[0] != '\0' && env[0] != '0';
    }();
    bool enabled() { return g_enabled; }

    bool init(ID3D12Device* device) {
        std::scoped_lock _{g_mutex};
        if (g_initialized) return true;
        if (g_init_failed) return false;
        if (device == nullptr) return false;

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = TOTAL_SIZE;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&g_buffer));
        if (FAILED(hr)) { g_init_failed = true; return false; }

        D3D12_RANGE rmap{0, TOTAL_SIZE};
        void* m = nullptr;
        hr = g_buffer->Map(0, &rmap, &m);
        if (FAILED(hr) || m == nullptr) { g_buffer.Reset(); g_init_failed = true; return false; }
        g_mapped = static_cast<uint8_t*>(m);

        hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence));
        if (FAILED(hr)) { g_buffer->Unmap(0, nullptr); g_buffer.Reset(); g_mapped = nullptr; g_init_failed = true; return false; }

        g_initialized = true;
        SPDLOG_WARN("[SN2-RTSnap] Initialized: {} slots × {} bytes = {} bytes total",
            NUM_SLOTS, SLOT_BYTES, TOTAL_SIZE);
        return true;
    }
    bool initialized() {
        std::scoped_lock _{g_mutex};
        return g_initialized;
    }

    // Bytes-per-pixel for supported formats (return 0 if unsupported).
    static uint32_t bpp_for_format(DXGI_FORMAT f) {
        switch (f) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R11G11B10_FLOAT:
        case DXGI_FORMAT_R16G16_FLOAT:
            return 4;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_UNORM:
            return 8;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
            return 16;
        default:
            return 0;
        }
    }

    uint64_t schedule_capture(ID3D12GraphicsCommandList* cl,
                              ID3D12Resource* src_tex,
                              D3D12_RESOURCE_STATES src_current_state,
                              const char* tag) {
        if (!g_initialized || cl == nullptr || src_tex == nullptr) return UINT64_MAX;

        const D3D12_RESOURCE_DESC desc = src_tex->GetDesc();
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) return UINT64_MAX;

        const uint32_t bpp = bpp_for_format(desc.Format);
        if (bpp == 0) return UINT64_MAX;  // unsupported format

        // Row pitch must be aligned to D3D12_TEXTURE_DATA_PITCH_ALIGNMENT (256)
        constexpr uint32_t PITCH_ALIGN = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
        const uint32_t unaligned_row_pitch = static_cast<uint32_t>(desc.Width) * bpp;
        const uint32_t row_pitch = (unaligned_row_pitch + PITCH_ALIGN - 1) & ~(PITCH_ALIGN - 1);
        const uint64_t byte_size = static_cast<uint64_t>(row_pitch) * desc.Height;
        if (byte_size > SLOT_BYTES) return UINT64_MAX;  // too big

        const uint64_t slot_index = g_next_slot.fetch_add(1, std::memory_order_relaxed) % NUM_SLOTS;
        const uint64_t buffer_offset = slot_index * SLOT_BYTES;
        constexpr uint64_t PLACED_FOOTPRINT_ALIGN = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;  // 512
        // We need buffer_offset aligned to 512 — SLOT_BYTES (8MB) satisfies this trivially.

        D3D12_TEXTURE_COPY_LOCATION dst_loc{};
        dst_loc.pResource = g_buffer.Get();
        dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst_loc.PlacedFootprint.Offset = buffer_offset;
        dst_loc.PlacedFootprint.Footprint.Format = desc.Format;
        dst_loc.PlacedFootprint.Footprint.Width = static_cast<UINT>(desc.Width);
        dst_loc.PlacedFootprint.Footprint.Height = desc.Height;
        dst_loc.PlacedFootprint.Footprint.Depth = 1;
        dst_loc.PlacedFootprint.Footprint.RowPitch = row_pitch;

        D3D12_TEXTURE_COPY_LOCATION src_loc{};
        src_loc.pResource = src_tex;
        src_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src_loc.SubresourceIndex = 0;

        // Transition source to COPY_SOURCE if needed
        const bool need_barrier = (src_current_state & D3D12_RESOURCE_STATE_COPY_SOURCE) == 0;
        D3D12_RESOURCE_BARRIER barrier{};
        if (need_barrier) {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = src_tex;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = src_current_state;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            cl->ResourceBarrier(1, &barrier);
        }
        cl->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, nullptr);
        if (need_barrier) {
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            cl->ResourceBarrier(1, &barrier);
        }

        {
            std::scoped_lock _{g_mutex};
            Slot& s = g_slots[slot_index];
            s.width = static_cast<uint32_t>(desc.Width);
            s.height = desc.Height;
            s.row_pitch = row_pitch;
            s.format = desc.Format;
            s.byte_size = byte_size;
            s.fence_value = 0;  // set by signal_after_execute
            s.frame = g_frame_counter.load(std::memory_order_relaxed);
            s.saved = false;
            std::strncpy(s.tag, tag != nullptr ? tag : "?", sizeof(s.tag) - 1);
            s.tag[sizeof(s.tag) - 1] = '\0';
        }
        return slot_index;
    }

    void signal_after_execute(ID3D12CommandQueue* queue) {
        if (!g_initialized || queue == nullptr) return;
        const uint64_t fv = g_fence_counter.fetch_add(1, std::memory_order_relaxed) + 1;
        {
            std::scoped_lock _{g_mutex};
            for (auto& s : g_slots) {
                if (s.byte_size != 0 && s.fence_value == 0) s.fence_value = fv;
            }
        }
        queue->Signal(g_fence.Get(), fv);
        g_frame_counter.fetch_add(1, std::memory_order_relaxed);
    }

    // Save BGRA8 / RGBA8 / R11G11B10F slot data as PPM (P6) on disk.
    static void save_ppm(const Slot& s, const uint8_t* cpu) {
        char path[260]{};
        std::snprintf(path, sizeof(path),
            "C:\\Users\\ellio\\AppData\\Local\\Temp\\sn2_rt_%s_f%llu.ppm",
            s.tag, (unsigned long long)s.frame);
        FILE* f = nullptr;
        fopen_s(&f, path, "wb");
        if (f == nullptr) return;
        std::fprintf(f, "P6\n%u %u\n255\n", s.width, s.height);
        // Write RGB rows; convert from source format
        std::vector<uint8_t> rgb_row(static_cast<size_t>(s.width) * 3);
        for (uint32_t y = 0; y < s.height; ++y) {
            const uint8_t* src_row = cpu + (static_cast<size_t>(y) * s.row_pitch);
            for (uint32_t x = 0; x < s.width; ++x) {
                uint8_t r = 0, g = 0, b = 0;
                if (s.format == DXGI_FORMAT_B8G8R8A8_UNORM || s.format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
                    b = src_row[x * 4 + 0];
                    g = src_row[x * 4 + 1];
                    r = src_row[x * 4 + 2];
                } else if (s.format == DXGI_FORMAT_R8G8B8A8_UNORM || s.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
                    r = src_row[x * 4 + 0];
                    g = src_row[x * 4 + 1];
                    b = src_row[x * 4 + 2];
                } else if (s.format == DXGI_FORMAT_R10G10B10A2_UNORM) {
                    uint32_t packed = *(uint32_t*)(src_row + x * 4);
                    uint32_t r10 = packed & 0x3FF;
                    uint32_t g10 = (packed >> 10) & 0x3FF;
                    uint32_t b10 = (packed >> 20) & 0x3FF;
                    r = (uint8_t)(r10 * 255 / 1023);
                    g = (uint8_t)(g10 * 255 / 1023);
                    b = (uint8_t)(b10 * 255 / 1023);
                } else if (s.format == DXGI_FORMAT_R11G11B10_FLOAT) {
                    // R11G11B10F → simple HDR-to-LDR clamp
                    uint32_t packed = *(uint32_t*)(src_row + x * 4);
                    auto f11to_float = [](uint32_t e5m6) -> float {
                        if (e5m6 == 0) return 0.0f;
                        uint32_t exp_bits = (e5m6 >> 6) & 0x1F;
                        uint32_t mant_bits = e5m6 & 0x3F;
                        if (exp_bits == 0) return mant_bits / 64.0f / 16384.0f;
                        return std::ldexp(1.0f + mant_bits / 64.0f, (int)exp_bits - 15);
                    };
                    auto f10to_float = [](uint32_t e5m5) -> float {
                        if (e5m5 == 0) return 0.0f;
                        uint32_t exp_bits = (e5m5 >> 5) & 0x1F;
                        uint32_t mant_bits = e5m5 & 0x1F;
                        if (exp_bits == 0) return mant_bits / 32.0f / 16384.0f;
                        return std::ldexp(1.0f + mant_bits / 32.0f, (int)exp_bits - 15);
                    };
                    float rf = f11to_float(packed & 0x7FF);
                    float gf = f11to_float((packed >> 11) & 0x7FF);
                    float bf = f10to_float((packed >> 22) & 0x3FF);
                    r = (uint8_t)std::min(255.0f, rf * 255.0f);
                    g = (uint8_t)std::min(255.0f, gf * 255.0f);
                    b = (uint8_t)std::min(255.0f, bf * 255.0f);
                } else if (s.format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
                    // Half-floats — simplified: read low byte of each, clamp
                    uint16_t rh = *(uint16_t*)(src_row + x * 8 + 0);
                    uint16_t gh = *(uint16_t*)(src_row + x * 8 + 2);
                    uint16_t bh = *(uint16_t*)(src_row + x * 8 + 4);
                    auto half_to_float = [](uint16_t h) -> float {
                        uint32_t sign = (h >> 15) & 1;
                        uint32_t exp_bits = (h >> 10) & 0x1F;
                        uint32_t mant_bits = h & 0x3FF;
                        if (exp_bits == 0) return 0.0f;
                        if (exp_bits == 31) return mant_bits == 0 ? (sign ? -1e30f : 1e30f) : 0.0f;
                        float v = std::ldexp(1.0f + mant_bits / 1024.0f, (int)exp_bits - 15);
                        return sign ? -v : v;
                    };
                    float rf = half_to_float(rh);
                    float gf = half_to_float(gh);
                    float bf = half_to_float(bh);
                    r = (uint8_t)std::min(255.0f, std::max(0.0f, rf) * 255.0f);
                    g = (uint8_t)std::min(255.0f, std::max(0.0f, gf) * 255.0f);
                    b = (uint8_t)std::min(255.0f, std::max(0.0f, bf) * 255.0f);
                }
                rgb_row[x * 3 + 0] = r;
                rgb_row[x * 3 + 1] = g;
                rgb_row[x * 3 + 2] = b;
            }
            std::fwrite(rgb_row.data(), 1, rgb_row.size(), f);
        }
        std::fclose(f);
        SPDLOG_WARN("[SN2-RTSnap] saved {} ({}x{} fmt={}) → {}",
            s.tag, s.width, s.height, (int)s.format, path);
    }

    void drain_to_disk() {
        if (!g_initialized) return;
        const uint64_t completed = g_fence->GetCompletedValue();
        std::scoped_lock _{g_mutex};
        for (uint64_t i = 0; i < NUM_SLOTS; ++i) {
            Slot& s = g_slots[i];
            if (s.byte_size == 0 || s.saved) continue;
            if (s.fence_value == 0 || s.fence_value > completed) continue;
            // Unmap + remap with a proper read range to flush CPU cache for
            // this slot. READBACK heap requires this for the CPU to see
            // GPU-written data after fence signal.
            D3D12_RANGE empty_write{0, 0};
            g_buffer->Unmap(0, &empty_write);
            const uint64_t off = i * SLOT_BYTES;
            D3D12_RANGE read_range{(SIZE_T)off, (SIZE_T)(off + s.byte_size)};
            void* mapped = nullptr;
            HRESULT hr = g_buffer->Map(0, &read_range, &mapped);
            if (FAILED(hr) || mapped == nullptr) {
                SPDLOG_WARN("[SN2-RTSnap] remap failed for slot {} hr=0x{:x}", i, (uint32_t)hr);
                continue;
            }
            g_mapped = static_cast<uint8_t*>(mapped);
            const uint8_t* cpu = g_mapped + off;
            // Log first 16 bytes to verify
            SPDLOG_WARN("[SN2-RTSnap] drain slot {} first16: {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}",
                i, cpu[0],cpu[1],cpu[2],cpu[3],cpu[4],cpu[5],cpu[6],cpu[7],cpu[8],cpu[9],cpu[10],cpu[11],cpu[12],cpu[13],cpu[14],cpu[15]);
            save_ppm(s, cpu);
            s.saved = true;
        }
    }

    // Phase 3 intent queue (FIFO; capped to avoid memory bloat).
    static std::mutex g_intent_mutex;
    static std::vector<Intent> g_intents;

    void queue_intent(ID3D12Resource* res, char eye_char, uint64_t seq) {
        queue_intent_named(res, "BP", eye_char, seq);
    }

    void queue_intent_named(ID3D12Resource* res, const char* tag_prefix,
            char eye_char, uint64_t seq) {
        if (res == nullptr || tag_prefix == nullptr) return;
        std::scoped_lock _{g_intent_mutex};
        if (g_intents.size() >= 16) return;  // cap (raised for multi-pass)
        Intent it{};
        it.res = res;
        std::snprintf(it.tag, sizeof(it.tag), "%s-%c_n%llu",
            tag_prefix, eye_char, (unsigned long long)seq);
        g_intents.push_back(it);
    }

    bool dequeue_intent(Intent& out) {
        std::scoped_lock _{g_intent_mutex};
        if (g_intents.empty()) return false;
        out = g_intents.front();
        g_intents.erase(g_intents.begin());
        return true;
    }

    static std::mutex g_rtv_mutex;
    static std::unordered_map<uint64_t, ID3D12Resource*> g_rtv_map;

    void record_rtv(uint64_t cpu_handle, ID3D12Resource* res) {
        if (cpu_handle == 0) return;
        std::scoped_lock _{g_rtv_mutex};
        if (res == nullptr) {
            g_rtv_map.erase(cpu_handle);
        } else {
            g_rtv_map[cpu_handle] = res;
        }
    }

    ID3D12Resource* lookup_rtv(uint64_t cpu_handle) {
        if (cpu_handle == 0) return nullptr;
        std::scoped_lock _{g_rtv_mutex};
        auto it = g_rtv_map.find(cpu_handle);
        return it != g_rtv_map.end() ? it->second : nullptr;
    }
} // namespace sn2_rt_snapshot

// 2026-05-19 SN2 COMPUTE CAPTURE — record LEFT-eye Nanite/VSM compute call
// state to enable future RIGHT-eye replay. Per-frame ring; cleared each frame
// via clear_frame() from the Present hook.
namespace sn2_compute_capture {
    static const std::unordered_set<uint32_t> TARGET_CS_CRCS = {
        0x6E901F3Cu,  // VirtualShadowMapProjection (1324 wg on left)
        0x1A15E3D7u,  // RasterBinBuild (85 wg)
        0x794CF7AEu,  // RasterBinBuild (85 wg, variant)
        0x67BAD483u,  // MergeStaticPhysicalPagesIndirectCS (96 wg)
        0xBE95FE40u,  // InstanceCull (111 wg)
        0xBD3584EEu,  // NodeAndClusterCull (1 wg)
        0x8AC3B3B7u,  // NodeAndClusterCull (variant)
        0x6D2E303Fu,  // NodeAndClusterCull (variant)
        0xBAE7F459u,  // NodeAndClusterCull (variant)
    };

    // MainIndirectDispatchCS variants — these are the right-eye PSOs we want to
    // SUBSTITUTE the arg buffer of so they actually dispatch work.
    static const std::unordered_set<uint32_t> MAIN_INDIRECT_CS_CRCS = {
        0x6A935D86u,  // MainIndirectDispatchCS (right-eye variant 1)
        0x8495CBEAu,  // MainIndirectDispatchCS (right-eye variant 2)
    };
    bool is_main_indirect_compute(uint32_t cs_crc32) {
        return MAIN_INDIRECT_CS_CRCS.count(cs_crc32) > 0;
    }

    // Per-frame remembered left-eye Main-indirect arg values (latest seen),
    // keyed by PSO cs_crc32. Each entry stores 12 bytes (3 uint32 X,Y,Z).
    static std::mutex g_left_main_args_mutex;
    static std::unordered_map<uint32_t, std::array<uint8_t, 16>> g_left_main_args;
    static std::unordered_map<uint32_t, uint64_t> g_left_main_offsets;  // ring offset in UPLOAD buf

    // Returns the offset in our inject buffer where the captured args for this
    // cs_crc32 live, or UINT64_MAX if none yet. Caller must call init() first.
    uint64_t main_indirect_inject_offset(uint32_t cs_crc32) {
        std::scoped_lock _{g_left_main_args_mutex};
        auto it = g_left_main_offsets.find(cs_crc32);
        if (it == g_left_main_offsets.end()) return UINT64_MAX;
        return it->second;
    }

    // Called from the GPU readback drain when we get LEFT-eye MainIndirectCS
    // arg bytes back. Writes them into the UPLOAD buffer at a per-CS offset.
    void on_left_main_args_ready(uint32_t cs_crc32, const uint8_t* bytes, size_t size) {
        if (size < 12) return;
        // Assign each cs_crc32 a 64-byte slot in the inject buffer.
        std::scoped_lock _{g_left_main_args_mutex};
        auto it = g_left_main_offsets.find(cs_crc32);
        uint64_t offset;
        if (it == g_left_main_offsets.end()) {
            offset = g_left_main_offsets.size() * 64;
            g_left_main_offsets[cs_crc32] = offset;
        } else {
            offset = it->second;
        }
        sn2_indirect_arg_inject::write_bytes(offset, bytes, std::min<size_t>(size, 16));
        std::array<uint8_t, 16> buf{};
        std::memcpy(buf.data(), bytes, std::min<size_t>(size, 16));
        g_left_main_args[cs_crc32] = buf;
    }

    bool is_target_compute(uint32_t cs_crc32) {
        return TARGET_CS_CRCS.count(cs_crc32) > 0;
    }

    static const bool g_enabled = []() {
        const char* env = std::getenv("UEVR_SN2_COMPUTE_CAPTURE");
        return env != nullptr && env[0] != '\0' && env[0] != '0';
    }();
    bool enabled() { return g_enabled; }

    static std::mutex g_mutex;
    static std::vector<CapturedCall> g_calls;  // current frame
    static std::atomic<uint64_t> g_frame{0};
    static std::atomic<uint64_t> g_total_captured{0};

    // Pull current compute root state from the cmd-list state map. UEVR's
    // CommandListState lives below this point in the source — the actual
    // CBV pull is delegated to the inline-defined `capture_indirect` /
    // `capture_direct` wrappers in the call sites where read_cmdlist_state
    // is visible. The snapshot here is a no-op stub for the foundation.
    static void snapshot_compute_state(ID3D12GraphicsCommandList* /*cl*/, CapturedCall& /*c*/) {
        // root CBVs left zero; future enhancement will fill these via a
        // separate tracker for the compute pipeline.
    }

    void capture_indirect(ID3D12GraphicsCommandList* cl,
                          void* pso, uint32_t cs_crc32,
                          ID3D12Resource* arg_buffer, UINT64 arg_offset,
                          int view_id) {
        if (!g_enabled) return;
        if (view_id != 0) return;  // capture LEFT eye only
        if (!is_target_compute(cs_crc32)) return;
        CapturedCall c{};
        c.pso = pso;
        c.cs_crc32 = cs_crc32;
        c.arg_buffer = arg_buffer;
        c.arg_buffer_offset = arg_offset;
        c.is_indirect = true;
        c.frame = g_frame.load(std::memory_order_relaxed);
        snapshot_compute_state(cl, c);
        std::scoped_lock _{g_mutex};
        if (g_calls.size() < 4096) {
            g_calls.push_back(c);
            g_total_captured.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void capture_direct(ID3D12GraphicsCommandList* cl,
                        void* pso, uint32_t cs_crc32,
                        UINT x, UINT y, UINT z, int view_id) {
        if (!g_enabled) return;
        if (view_id != 0) return;
        if (!is_target_compute(cs_crc32)) return;
        CapturedCall c{};
        c.pso = pso;
        c.cs_crc32 = cs_crc32;
        c.thread_group_count_x = x;
        c.thread_group_count_y = y;
        c.thread_group_count_z = z;
        c.is_indirect = false;
        c.frame = g_frame.load(std::memory_order_relaxed);
        snapshot_compute_state(cl, c);
        std::scoped_lock _{g_mutex};
        if (g_calls.size() < 4096) {
            g_calls.push_back(c);
            g_total_captured.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void log_frame_summary() {
        if (!g_enabled) return;
        std::scoped_lock _{g_mutex};
        if (g_calls.empty()) return;
        // Group by cs_crc32
        std::unordered_map<uint32_t, int> per_crc;
        for (const auto& c : g_calls) per_crc[c.cs_crc32]++;
        static std::atomic<uint64_t> log_count{0};
        const auto lc = log_count.fetch_add(1, std::memory_order_relaxed);
        if (lc < 8 || (lc % 60) == 0) {
            std::string summary;
            for (const auto& [crc, n] : per_crc) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), " 0x%08x=%d", crc, n);
                summary += buf;
            }
            SPDLOG_WARN("[SN2-CC] frame={} captured {} LEFT-eye Nanite/VSM calls (total={}):{}",
                g_frame.load(), g_calls.size(), g_total_captured.load(), summary);
        }
    }

    void clear_frame() {
        if (!g_enabled) return;
        std::scoped_lock _{g_mutex};
        g_calls.clear();
        g_frame.fetch_add(1, std::memory_order_relaxed);
    }
} // namespace sn2_compute_capture

namespace sn2_descriptor_registry {
    enum class Kind : uint8_t {
        Unknown,
        CBV,
        SRV,
        UAV
    };

    struct Entry {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle{};
        D3D12_CPU_DESCRIPTOR_HANDLE source_cpu_handle{};
        ID3D12Resource* resource{};
        D3D12_RESOURCE_DESC resource_desc{};
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc{};
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
        Kind kind{Kind::Unknown};
        uint64_t descriptor_hash{};
        uint64_t last_seen_tick{};
        uint64_t generation{};
        bool has_resource_desc{};
        bool has_cbv_desc{};
        bool has_srv_desc{};
        bool has_uav_desc{};
    };

    static std::mutex g_mutex;
    static std::unordered_map<SIZE_T, Entry> g_by_cpu;
    static std::atomic<uint64_t> g_generation{0};

    uint64_t fnv1a64(const void* data, size_t size) {
        if (data == nullptr || size == 0) return 0;
        const auto* bytes = static_cast<const uint8_t*>(data);
        uint64_t h = 14695981039346656037ull;
        for (size_t i = 0; i < size; ++i) {
            h ^= bytes[i];
            h *= 1099511628211ull;
        }
        return h;
    }

    uint64_t descriptor_memory_hash(D3D12_CPU_DESCRIPTOR_HANDLE cpu) {
        if (cpu.ptr == 0 || !is_readable_process_range_d3d12(cpu.ptr, 32)) return 0;
        uint8_t bytes[32]{};
        std::memcpy(bytes, reinterpret_cast<const void*>(cpu.ptr), sizeof(bytes));
        return fnv1a64(bytes, sizeof(bytes));
    }

    const char* kind_name(Kind kind) {
        switch (kind) {
        case Kind::CBV: return "CBV";
        case Kind::SRV: return "SRV";
        case Kind::UAV: return "UAV";
        default: return "Unknown";
        }
    }

    void publish(Entry entry) {
        if (entry.cpu_handle.ptr == 0) return;
        entry.last_seen_tick = GetTickCount64();
        entry.generation = g_generation.fetch_add(1, std::memory_order_relaxed) + 1;
        if (entry.descriptor_hash == 0) {
            entry.descriptor_hash = descriptor_memory_hash(entry.cpu_handle);
        }
        std::scoped_lock _{g_mutex};
        g_by_cpu[entry.cpu_handle.ptr] = entry;
    }

    void record_cbv(const D3D12_CONSTANT_BUFFER_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE cpu) {
        if (!sn2_pso3069_diag_enabled() || desc == nullptr || cpu.ptr == 0) return;
        Entry e{};
        e.cpu_handle = cpu;
        e.kind = Kind::CBV;
        e.cbv_desc = *desc;
        e.has_cbv_desc = true;
        publish(e);
    }

    void record_srv(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE cpu) {
        if (!sn2_pso3069_diag_enabled() || resource == nullptr || cpu.ptr == 0) return;
        Entry e{};
        e.cpu_handle = cpu;
        e.resource = resource;
        e.kind = Kind::SRV;
        e.resource_desc = resource->GetDesc();
        e.has_resource_desc = true;
        if (desc != nullptr) {
            e.srv_desc = *desc;
            e.has_srv_desc = true;
        }
        publish(e);
    }

    void record_uav(ID3D12Resource* resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE cpu) {
        if (!sn2_pso3069_diag_enabled() || resource == nullptr || cpu.ptr == 0) return;
        Entry e{};
        e.cpu_handle = cpu;
        e.resource = resource;
        e.kind = Kind::UAV;
        e.resource_desc = resource->GetDesc();
        e.has_resource_desc = true;
        if (desc != nullptr) {
            e.uav_desc = *desc;
            e.has_uav_desc = true;
        }
        publish(e);
    }

    bool lookup(D3D12_CPU_DESCRIPTOR_HANDLE cpu, Entry& out) {
        if (cpu.ptr == 0) return false;
        std::scoped_lock _{g_mutex};
        const auto it = g_by_cpu.find(cpu.ptr);
        if (it == g_by_cpu.end()) return false;
        out = it->second;
        return true;
    }

    void record_copy(D3D12_CPU_DESCRIPTOR_HANDLE dst, D3D12_CPU_DESCRIPTOR_HANDLE src) {
        if (!sn2_pso3069_diag_enabled() || dst.ptr == 0 || src.ptr == 0) return;
        Entry src_entry{};
        if (!lookup(src, src_entry)) return;
        src_entry.cpu_handle = dst;
        src_entry.source_cpu_handle = src;
        src_entry.descriptor_hash = descriptor_memory_hash(dst);
        publish(src_entry);
    }

    size_t size() {
        std::scoped_lock _{g_mutex};
        return g_by_cpu.size();
    }
} // namespace sn2_descriptor_registry

// 2026-05-17 SN2 FOG-FIX Task #46 — CopyDescriptorsSimple → bindless slot
// tracking. UE5 builds SRVs/UAVs in non-shader-visible CPU staging heaps,
// then CopyDescriptorsSimple's them into the shader-visible bindless heap
// each frame. This map answers: "for a given bindless heap CPU handle, what
// staging-heap source was last copied in, and what resource/view_id does
// that source correspond to?" — which is the only way to find the bindless
// slot that view 1's fog volume ends up at.
namespace sn2_bindless_slot_map {
    static std::mutex g_mutex;
    static std::unordered_map<SIZE_T, Entry> g_by_bindless;
    static std::vector<Entry> g_by_index;  // for "most recent" search; bounded

    void record(D3D12_CPU_DESCRIPTOR_HANDLE bindless_cpu, D3D12_CPU_DESCRIPTOR_HANDLE src_cpu) {
        // Resolve source resource + view_id by looking the src cpu handle up
        // in our UAV / SRV maps (read-only). Bail early if the source isn't
        // anything we track — bindless heap copies happen for tons of
        // non-fog resources every frame, and we don't want to bloat this
        // map with those.
        sn2_fog_uav_map::Entry uav_e{};
        sn2_fog_srv_map::Entry srv_e{};
        ID3D12Resource* res = nullptr;
        int view_id = -1;
        if (sn2_fog_uav_map::lookup_by_cpu_handle(src_cpu, uav_e)) {
            res = uav_e.resource;
            view_id = uav_e.view_id;
        } else if (sn2_fog_srv_map::lookup_by_cpu_handle(src_cpu, srv_e)) {
            res = srv_e.resource;
            view_id = srv_e.view_id;
        } else {
            return;
        }

        Entry e{};
        e.bindless_cpu_handle = bindless_cpu;
        e.src_cpu_handle = src_cpu;
        e.src_resource = res;
        e.src_view_id = view_id;
        e.last_seen_tick = GetTickCount64();

        std::scoped_lock _{g_mutex};
        g_by_bindless[bindless_cpu.ptr] = e;
        if (g_by_index.size() < 65536) g_by_index.push_back(e);
    }

    bool lookup(D3D12_CPU_DESCRIPTOR_HANDLE bindless_cpu, Entry& out) {
        std::scoped_lock _{g_mutex};
        auto it = g_by_bindless.find(bindless_cpu.ptr);
        if (it == g_by_bindless.end()) return false;
        out = it->second;
        return true;
    }

    bool find_slot_for_view(int view_id, D3D12_CPU_DESCRIPTOR_HANDLE& bindless_cpu_out, ID3D12Resource** resource_out) {
        std::scoped_lock _{g_mutex};
        // Walk live g_by_bindless to pick a current entry; we want the
        // most-recently-recorded match for view_id.
        Entry* best = nullptr;
        for (auto& kv : g_by_bindless) {
            if (kv.second.src_view_id != view_id) continue;
            if (best == nullptr || kv.second.last_seen_tick > best->last_seen_tick) {
                best = &kv.second;
            }
        }
        if (best == nullptr) return false;
        bindless_cpu_out = best->bindless_cpu_handle;
        if (resource_out) *resource_out = best->src_resource;
        return true;
    }

    size_t size() {
        std::scoped_lock _{g_mutex};
        return g_by_bindless.size();
    }

    size_t count_by_view(int view_id) {
        std::scoped_lock _{g_mutex};
        size_t n = 0;
        for (const auto& kv : g_by_bindless) {
            if (kv.second.src_view_id == view_id) ++n;
        }
        return n;
    }

    void for_each_in_range(SIZE_T cpu_lo, SIZE_T cpu_hi, void (*visitor)(const Entry& e, void* ctx), void* ctx) {
        std::scoped_lock _{g_mutex};
        for (const auto& kv : g_by_bindless) {
            if (kv.first >= cpu_lo && kv.first < cpu_hi) {
                visitor(kv.second, ctx);
            }
        }
    }

    size_t bulk_tag_sources_in_range(SIZE_T cpu_lo, SIZE_T cpu_hi, int view_id) {
        // Make a snapshot to avoid holding g_mutex while we call into the
        // UAV/SRV map (which has its own mutex).
        std::vector<Entry> snapshot;
        {
            std::scoped_lock _{g_mutex};
            for (auto& kv : g_by_bindless) {
                if (kv.first >= cpu_lo && kv.first < cpu_hi) snapshot.push_back(kv.second);
            }
        }
        size_t tagged = 0;
        for (auto& e : snapshot) {
            // Only tag if source was untagged.
            if (e.src_view_id != -1) continue;
            bool ok = sn2_fog_uav_map::tag_by_cpu_handle(e.src_cpu_handle, view_id);
            if (!ok) ok = sn2_fog_srv_map::tag_by_cpu_handle(e.src_cpu_handle, view_id);
            if (ok) ++tagged;
        }
        return tagged;
    }

    // 2026-05-17 Task #46 — re-resolve src_view_id + src_resource for entries
    // whose source was untagged at record time. UE5 retroactively gets view
    // tags via the SetComputeRootDescriptorTable walker; this lets us
    // populate the bindless slot map with the now-tagged resources without
    // requiring the copy to happen AFTER the tagging.
    size_t resolve_pending_tags() {
        std::scoped_lock _{g_mutex};
        size_t resolved = 0;
        for (auto& kv : g_by_bindless) {
            if (kv.second.src_view_id != -1 && kv.second.src_resource != nullptr) continue;
            sn2_fog_uav_map::Entry uav_e{};
            sn2_fog_srv_map::Entry srv_e{};
            if (sn2_fog_uav_map::lookup_by_cpu_handle(kv.second.src_cpu_handle, uav_e)) {
                kv.second.src_resource = uav_e.resource;
                if (kv.second.src_view_id == -1) kv.second.src_view_id = uav_e.view_id;
                if (kv.second.src_view_id != -1) ++resolved;
            } else if (sn2_fog_srv_map::lookup_by_cpu_handle(kv.second.src_cpu_handle, srv_e)) {
                kv.second.src_resource = srv_e.resource;
                if (kv.second.src_view_id == -1) kv.second.src_view_id = srv_e.view_id;
                if (kv.second.src_view_id != -1) ++resolved;
            }
        }
        return resolved;
    }
} // namespace sn2_bindless_slot_map

void WINAPI D3D12Hook::create_shader_resource_view(
    ID3D12Device* device,
    ID3D12Resource* resource,
    const D3D12_SHADER_RESOURCE_VIEW_DESC* desc,
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = device != nullptr ? &(*(void***)device)[CREATE_SHADER_RESOURCE_VIEW_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_create_shader_resource_view_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::create_shader_resource_view)*>() : nullptr;

    // Recursion guard: if we're being called from inside pool.add(), skip all
    // tracking and just pass through to the original. Otherwise pool.add()'s
    // CreateShaderResourceView call would loop back here forever.
    if (sn2_view1_fog_srv_pool::tl_in_pool_add) {
        if (original != nullptr) original(device, resource, desc, descriptor);
        return;
    }

    // Filter for 3D fog volume SRVs — BROADENED to catch any Texture3D used as
    // SRV regardless of format. View 1's fog volume may have different signature
    // than view 0's (e.g. R16G16B16A16F, R8G8B8A8_UNORM_SRGB, etc.).
    if (resource != nullptr && desc != nullptr && desc->ViewDimension == D3D12_SRV_DIMENSION_TEXTURE3D) {
        D3D12_RESOURCE_DESC rd = resource->GetDesc();
        if (rd.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
        {
            // Record into runtime map BEFORE logging so the SN2 SLW hook can use it.
            // Pass the current fog-view thread-local (set by sub_142FD6170 hook).
            const int view_id_at_creation = sn2_get_current_fog_view();
            sn2_fog_srv_map::record(resource, descriptor, view_id_at_creation);

            // 2026-05-17 Step D-safe: if this Texture3D matches the fog volume
            // signature (R11G11B10F, fog-sized 3D), AddRef and create our own
            // SRV into the UEVR-owned pool. Done at SRV CREATE time when the
            // resource is definitely live; pool keeps the ref so swap source
            // descriptors stay valid even if UE5 recycles its own staging heap.
            if (sn2_view1_fog_srv_pool::is_fog_3d(rd)) {
                sn2_view1_fog_srv_pool::add(device, resource);
            }

            // Look up creation index of this SRV for log clarity.
            sn2_fog_srv_map::Entry e_lookup{};
            sn2_fog_srv_map::lookup_by_resource(resource, e_lookup);

            static std::atomic<uint64_t> n{0};
            const auto idx = n.fetch_add(1, std::memory_order_relaxed);
            if (idx < 64 || (idx % 600) == 0) {
                const UINT64 va = resource->GetGPUVirtualAddress();
                SPDLOG_WARN(
                    "[D3D12-FogSRV] log#{} tid={} crIdx={} resource={:p} gpuVA=0x{:x} cpuHandle=0x{:x} fmt={} dim={}x{}x{} view_id={} (map size={} v0={} v1={})",
                    idx + 1, (uint32_t)GetCurrentThreadId(),
                    e_lookup.creation_index,
                    (void*)resource, va, descriptor.ptr,
                    (int)rd.Format, (int)rd.Width, (int)rd.Height, (int)rd.DepthOrArraySize,
                    view_id_at_creation,
                    sn2_fog_srv_map::size(),
                    sn2_fog_srv_map::count_by_view(0),
                    sn2_fog_srv_map::count_by_view(1));
            }
        }
    }

    if (original != nullptr) {
        original(device, resource, desc, descriptor);
    }

    sn2_descriptor_registry::record_srv(resource, desc, descriptor);
}

// 2026-05-17 SN2 FOG-FIX Task #43: tag fog-volume UAVs by view at creation.
// View tag pulled from the sn2_get_current_fog_view() atomic which is set by
// the per-view fog dispatcher hook (sub_142FD6170) in
// FFakeStereoRenderingHook.cpp. Filtered the same way as the SRV path:
// any Texture3D-dimension UAV is recorded.
void WINAPI D3D12Hook::create_unordered_access_view(
    ID3D12Device* device,
    ID3D12Resource* resource,
    ID3D12Resource* counter_resource,
    const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc,
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = device != nullptr ? &(*(void***)device)[CREATE_UNORDERED_ACCESS_VIEW_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_create_unordered_access_view_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::create_unordered_access_view)*>() : nullptr;

    // Filter for Texture3D UAVs only — the fog compute writes to 3D volumes.
    if (resource != nullptr && desc != nullptr && desc->ViewDimension == D3D12_UAV_DIMENSION_TEXTURE3D) {
        D3D12_RESOURCE_DESC rd = resource->GetDesc();
        if (rd.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
            const int view_id = sn2_get_current_fog_view();
            sn2_fog_uav_map::record(resource, descriptor, view_id);

            // Lookup for log readability.
            sn2_fog_uav_map::Entry e_lookup{};
            sn2_fog_uav_map::lookup_by_resource(resource, e_lookup);

            static std::atomic<uint64_t> n{0};
            const auto idx = n.fetch_add(1, std::memory_order_relaxed);
            if (idx < 64 || (idx % 600) == 0) {
                SPDLOG_WARN(
                    "[D3D12-FogUAV] log#{} tid={} crIdx={} resource={:p} cpuHandle=0x{:x} fmt={} dim={}x{}x{} view_id={} (map size={} v0={} v1={})",
                    idx + 1, (uint32_t)GetCurrentThreadId(),
                    e_lookup.creation_index,
                    (void*)resource, descriptor.ptr,
                    (int)rd.Format, (int)rd.Width, (int)rd.Height, (int)rd.DepthOrArraySize,
                    view_id,
                    sn2_fog_uav_map::size(),
                    sn2_fog_uav_map::count_by_view(0),
                    sn2_fog_uav_map::count_by_view(1));
            }
        }
    }

    if (original != nullptr) {
        original(device, resource, counter_resource, desc, descriptor);
    }

    sn2_descriptor_registry::record_uav(resource, desc, descriptor);
}

// 2026-05-17 SN2 FOG-FIX Task #46 — CopyDescriptorsSimple hook. Each call
// copies one or more descriptors from a (source) CPU heap into the
// (destination) shader-visible bindless heap. Tracking these copies is the
// only way to know which bindless slot view 1's fog UAV ends up at, because
// UE5 builds descriptors in staging heaps and only the bindless heap copy
// is what the GPU shader reads at draw time.
void WINAPI D3D12Hook::copy_descriptors_simple(
    ID3D12Device* device,
    UINT num_descriptors,
    D3D12_CPU_DESCRIPTOR_HANDLE dst_start,
    D3D12_CPU_DESCRIPTOR_HANDLE src_start,
    D3D12_DESCRIPTOR_HEAP_TYPE type
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = device != nullptr ? &(*(void***)device)[COPY_DESCRIPTORS_SIMPLE_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_copy_descriptors_simple_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::copy_descriptors_simple)*>() : nullptr;

    if (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV && device != nullptr && num_descriptors > 0) {
        const UINT stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        // 2026-05-17 evening: cap REVERTED to 4096 — slot map mechanism turned
        // out to be unused (UE5 doesn't bulk-copy the CPU heap to GPU heap in
        // this game's config). Byte-scan in set_graphics_root_descriptor_table
        // is now the primary path for finding fog SRVs in the basepass binding,
        // so we don't need to record every descriptor copy. Lower cap reduces
        // CPU overhead on big copies.
        const UINT n = (num_descriptors > 4096u) ? 4096u : num_descriptors;
        for (UINT i = 0; i < n; ++i) {
            D3D12_CPU_DESCRIPTOR_HANDLE dst{dst_start.ptr + (SIZE_T)i * stride};
            D3D12_CPU_DESCRIPTOR_HANDLE src{src_start.ptr + (SIZE_T)i * stride};
            sn2_bindless_slot_map::record(dst, src);
        }

        static std::atomic<uint64_t> n_log{0};
        const auto idx = n_log.fetch_add(1, std::memory_order_relaxed);
        // First 8 calls log unconditionally so we can verify the hook fires.
        // Subsequent calls log only every 4096 with a resolve sweep.
        if (idx < 8 || (idx % 4096) == 0) {
            const size_t resolved = (idx % 4096) == 0 ? sn2_bindless_slot_map::resolve_pending_tags() : 0;
            SPDLOG_WARN(
                "[D3D12-CopyDesc] tick={} num_desc={} dst=0x{:x} src=0x{:x} bindless_map_size={} v0={} v1={} resolved={}",
                idx, num_descriptors, dst_start.ptr, src_start.ptr,
                sn2_bindless_slot_map::size(),
                sn2_bindless_slot_map::count_by_view(0),
                sn2_bindless_slot_map::count_by_view(1),
                resolved);
        }
    }

    if (original != nullptr) {
        original(device, num_descriptors, dst_start, src_start, type);
    }

    if (sn2_pso3069_diag_enabled() && type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
        device != nullptr && num_descriptors > 0) {
        const UINT stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const UINT n = (num_descriptors > 65536u) ? 65536u : num_descriptors;
        for (UINT i = 0; i < n; ++i) {
            D3D12_CPU_DESCRIPTOR_HANDLE dst{dst_start.ptr + (SIZE_T)i * stride};
            D3D12_CPU_DESCRIPTOR_HANDLE src{src_start.ptr + (SIZE_T)i * stride};
            sn2_descriptor_registry::record_copy(dst, src);
        }
    }
}

// 2026-05-17 SN2 FOG-FIX Task #46 — batched CopyDescriptors hook. UE5 in
// bindless mode often uses this batched variant for per-frame heap updates.
// Walks each (dst_range, src_range) pair and forwards individual descriptor
// pairs to sn2_bindless_slot_map::record like the Simple variant does.
void WINAPI D3D12Hook::copy_descriptors(
    ID3D12Device* device,
    UINT num_dst_ranges,
    const D3D12_CPU_DESCRIPTOR_HANDLE* dst_starts,
    const UINT* dst_sizes,
    UINT num_src_ranges,
    const D3D12_CPU_DESCRIPTOR_HANDLE* src_starts,
    const UINT* src_sizes,
    D3D12_DESCRIPTOR_HEAP_TYPE type
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = device != nullptr ? &(*(void***)device)[COPY_DESCRIPTORS_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_copy_descriptors_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::copy_descriptors)*>() : nullptr;

    if (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV && device != nullptr
        && dst_starts != nullptr && dst_sizes != nullptr
        && src_starts != nullptr && src_sizes != nullptr
        && num_dst_ranges > 0 && num_src_ranges > 0) {
        const UINT stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // The D3D12 spec for CopyDescriptors: walk dst and src ranges in
        // parallel, allowing differing range sizes — the total descriptor
        // count must match. We iterate each logical descriptor (i = 0 .. total-1).
        UINT total_dst = 0;
        for (UINT i = 0; i < num_dst_ranges; ++i) total_dst += dst_sizes[i];
        UINT total_src = 0;
        for (UINT i = 0; i < num_src_ranges; ++i) total_src += src_sizes[i];
        const UINT total = (total_dst < total_src) ? total_dst : total_src;
        const UINT bounded = (total > 4096u) ? 4096u : total;

        UINT dst_range_idx = 0; UINT dst_within = 0;
        UINT src_range_idx = 0; UINT src_within = 0;
        for (UINT i = 0; i < bounded; ++i) {
            // Advance to next dst range if needed.
            while (dst_range_idx < num_dst_ranges && dst_within >= dst_sizes[dst_range_idx]) {
                ++dst_range_idx; dst_within = 0;
            }
            if (dst_range_idx >= num_dst_ranges) break;

            while (src_range_idx < num_src_ranges && src_within >= src_sizes[src_range_idx]) {
                ++src_range_idx; src_within = 0;
            }
            if (src_range_idx >= num_src_ranges) break;

            D3D12_CPU_DESCRIPTOR_HANDLE dst{
                dst_starts[dst_range_idx].ptr + (SIZE_T)dst_within * stride};
            D3D12_CPU_DESCRIPTOR_HANDLE src{
                src_starts[src_range_idx].ptr + (SIZE_T)src_within * stride};

            sn2_bindless_slot_map::record(dst, src);

            ++dst_within;
            ++src_within;
        }

        static std::atomic<uint64_t> n_log{0};
        const auto idx = n_log.fetch_add(1, std::memory_order_relaxed);
        if (idx < 8 || (idx % 4096) == 0) {
            const size_t resolved = (idx % 4096) == 0 ? sn2_bindless_slot_map::resolve_pending_tags() : 0;
            const D3D12_CPU_DESCRIPTOR_HANDLE first_dst = (num_dst_ranges > 0 && dst_starts != nullptr) ? dst_starts[0] : D3D12_CPU_DESCRIPTOR_HANDLE{};
            const D3D12_CPU_DESCRIPTOR_HANDLE first_src = (num_src_ranges > 0 && src_starts != nullptr) ? src_starts[0] : D3D12_CPU_DESCRIPTOR_HANDLE{};
            SPDLOG_WARN(
                "[D3D12-CopyDescBatched] tick={} dst_ranges={} src_ranges={} total={} first_dst=0x{:x} first_src=0x{:x} bindless_map_size={} v0={} v1={} resolved={}",
                idx, num_dst_ranges, num_src_ranges, total,
                first_dst.ptr, first_src.ptr,
                sn2_bindless_slot_map::size(),
                sn2_bindless_slot_map::count_by_view(0),
                sn2_bindless_slot_map::count_by_view(1),
                resolved);
        }
    }

    if (original != nullptr) {
        original(device, num_dst_ranges, dst_starts, dst_sizes, num_src_ranges, src_starts, src_sizes, type);
    }

    if (sn2_pso3069_diag_enabled() && type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV && device != nullptr
        && dst_starts != nullptr && dst_sizes != nullptr
        && src_starts != nullptr && src_sizes != nullptr
        && num_dst_ranges > 0 && num_src_ranges > 0) {
        const UINT stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        uint64_t total_dst = 0;
        for (UINT i = 0; i < num_dst_ranges; ++i) total_dst += dst_sizes[i];
        uint64_t total_src = 0;
        for (UINT i = 0; i < num_src_ranges; ++i) total_src += src_sizes[i];
        const uint64_t total = (total_dst < total_src) ? total_dst : total_src;
        const uint64_t bounded = (total > 65536ull) ? 65536ull : total;

        UINT dst_range_idx = 0;
        UINT dst_within = 0;
        UINT src_range_idx = 0;
        UINT src_within = 0;
        for (uint64_t i = 0; i < bounded; ++i) {
            while (dst_range_idx < num_dst_ranges && dst_within >= dst_sizes[dst_range_idx]) {
                ++dst_range_idx;
                dst_within = 0;
            }
            if (dst_range_idx >= num_dst_ranges) break;

            while (src_range_idx < num_src_ranges && src_within >= src_sizes[src_range_idx]) {
                ++src_range_idx;
                src_within = 0;
            }
            if (src_range_idx >= num_src_ranges) break;

            D3D12_CPU_DESCRIPTOR_HANDLE dst{dst_starts[dst_range_idx].ptr + (SIZE_T)dst_within * stride};
            D3D12_CPU_DESCRIPTOR_HANDLE src{src_starts[src_range_idx].ptr + (SIZE_T)src_within * stride};
            sn2_descriptor_registry::record_copy(dst, src);

            ++dst_within;
            ++src_within;
        }
    }
}

void WINAPI D3D12Hook::create_depth_stencil_view(
    ID3D12Device* device,
    ID3D12Resource* resource,
    const D3D12_DEPTH_STENCIL_VIEW_DESC* desc,
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor
) {
    (void)desc;
    auto d3d12 = g_d3d12_hook;
    const auto slot = device != nullptr ? &(*(void***)device)[CREATE_DEPTH_STENCIL_VIEW_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_create_depth_stencil_view_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::create_depth_stencil_view)*>() : nullptr;

    if (original != nullptr) {
        original(device, resource, desc, descriptor);
    }

    render::D3D12Diagnostics::get().register_dsv_descriptor("D3D12Hook::CreateDepthStencilView", resource, descriptor);
}

namespace {
// 2026-05-17 REWRITE: per-cmdlist map (not thread_local). Path A correlation
// also enables this so the SN2 fog redirect logic gets a working viewport
// read; otherwise gate on the original descriptor-table-hook env var.
inline bool descriptor_table_correlation_enabled() {
    static const bool enabled = enable_d3d12_descriptor_table_hook() || []() {
        char v[8]{};
        const auto n = GetEnvironmentVariableA("UEVR_SUBNAUTICA2_FOG_PATH_A", v, sizeof(v));
        if (n != 0 && v[0] == '1') return true;
        const auto n2 = GetEnvironmentVariableA("UEVR_SUBNAUTICA2_FOG_DESCRIPTOR_SWAP", v, sizeof(v));
        if (n2 != 0 && v[0] == '1') return true;
        return false;
    }();
    return enabled;
}

inline void update_cmdlist_pso(ID3D12GraphicsCommandList* cl, ID3D12PipelineState* pso) {
    // Always store. The eye-diff tracker + the shader-hunter substitute path
    // both need this even when descriptor_table_correlation_enabled() is off.
    if (cl == nullptr) return;
    std::scoped_lock _{g_cmdlist_state_mutex};
    g_cmdlist_state_map[cl].current_pso = pso;
}

inline void update_cmdlist_viewport(ID3D12GraphicsCommandList* cl, UINT num_viewports, const D3D12_VIEWPORT* viewports) {
    if (cl == nullptr || num_viewports == 0 || viewports == nullptr) return;
    std::scoped_lock _{g_cmdlist_state_mutex};
    auto& s = g_cmdlist_state_map[cl];
    s.viewport_top_left_x = viewports[0].TopLeftX;
    s.viewport_top_left_y = viewports[0].TopLeftY;
    s.viewport_width = viewports[0].Width;
    s.viewport_height = viewports[0].Height;
    s.has_viewport = true;
    s.last_viewport_bucket = classify_viewports(num_viewports, viewports);
}

// === Eye-Diff per-CL captures ===
// Hot-path fingerprints used by the eye-diff tracker. Tiny stores, mutex-bound.
// Called from OMSetRenderTargets and SetGraphicsRootDescriptorTable hooks.
inline void update_cmdlist_rtv0(ID3D12GraphicsCommandList* cl, const D3D12_CPU_DESCRIPTOR_HANDLE* rtv0) {
    if (cl == nullptr || rtv0 == nullptr) return;
    std::scoped_lock _{g_cmdlist_state_mutex};
    g_cmdlist_state_map[cl].last_rtv0_handle = static_cast<uint64_t>(rtv0->ptr);
}
inline void update_cmdlist_root_desc_table0(ID3D12GraphicsCommandList* cl, UINT root_param, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) {
    if (cl == nullptr || root_param != 0) return; // root param 0 = "bindless heap" in UE5
    std::scoped_lock _{g_cmdlist_state_mutex};
    g_cmdlist_state_map[cl].last_graphics_root_desc_table0 = static_cast<uint64_t>(gpu_handle.ptr);
}

inline void update_cmdlist_root_cbv(ID3D12GraphicsCommandList* cl, UINT root_param, D3D12_GPU_VIRTUAL_ADDRESS gpu_va) {
    if (cl == nullptr || root_param >= g_cmdlist_state_empty.last_graphics_root_cbv.size()) return;
    std::scoped_lock _{g_cmdlist_state_mutex};
    g_cmdlist_state_map[cl].last_graphics_root_cbv[root_param] = static_cast<uint64_t>(gpu_va);
}

// Returns a copy (not a reference) since the map is mutex-protected.
inline CommandListCorrelationState read_cmdlist_state(ID3D12GraphicsCommandList* cl) {
    if (cl == nullptr) return g_cmdlist_state_empty;
    std::scoped_lock _{g_cmdlist_state_mutex};
    auto it = g_cmdlist_state_map.find(cl);
    if (it == g_cmdlist_state_map.end()) return g_cmdlist_state_empty;
    return it->second;
}

// Per-cmdlist view_id derivation from viewport. -1 if unknown.
// emulatestereo splits the backbuffer horizontally; left eye = TopLeftX 0,
// right eye = TopLeftX > 0 (typically half-width).
inline int cmdlist_view_id(const CommandListCorrelationState& s) {
    if (!s.has_viewport) return -1;
    // Threshold 1.0f tolerates floating-point precision around 0.
    return s.viewport_top_left_x > 1.0f ? 1 : 0;
}

// Clear cmdlist entry on Close()/Reset() to bound the map.
inline void clear_cmdlist_state(ID3D12GraphicsCommandList* cl) {
    if (cl == nullptr) return;
    std::scoped_lock _{g_cmdlist_state_mutex};
    g_cmdlist_state_map.erase(cl);
}
} // namespace

static void sn2_log_pso3069_draw_snapshot(
    ID3D12GraphicsCommandList* command_list,
    const CommandListCorrelationState& state,
    const char* draw_kind,
    UINT a,
    UINT b,
    UINT c,
    INT d,
    UINT e);

HRESULT WINAPI D3D12Hook::close_command_list(ID3D12GraphicsCommandList* command_list) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = command_list != nullptr ? &(*(void***)command_list)[CLOSE_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_command_list_diagnostic_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::close_command_list)*>() : nullptr;

    if (original == nullptr) {
        return E_FAIL;
    }

    const auto result = original(command_list);
    clear_cmdlist_state(command_list);
    render::ShaderOverrideRegistry::get().hunter_clear_command_list(command_list);
    return result;
}

HRESULT WINAPI D3D12Hook::reset_command_list(
    ID3D12GraphicsCommandList* command_list,
    ID3D12CommandAllocator* allocator,
    ID3D12PipelineState* initial_state
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = command_list != nullptr ? &(*(void***)command_list)[RESET_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_command_list_diagnostic_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::reset_command_list)*>() : nullptr;

    if (original == nullptr) {
        return E_FAIL;
    }

    clear_cmdlist_state(command_list);
    auto& shader_registry = render::ShaderOverrideRegistry::get();
    shader_registry.hunter_clear_command_list(command_list);

    auto* bound_initial_state = initial_state;
    if (shader_registry.should_track_d3d12_pipelines() && initial_state != nullptr) {
        bound_initial_state = shader_registry.resolve_d3d12_pipeline_state(initial_state);
        shader_registry.note_d3d12_pipeline_state_bound(initial_state, bound_initial_state);
    }

    const auto result = original(command_list, allocator, bound_initial_state);
    if (SUCCEEDED(result) && initial_state != nullptr) {
        update_cmdlist_pso(command_list, initial_state);
        if (shader_registry.should_track_d3d12_pipelines()) {
            shader_registry.hunter_record_set_pipeline_state(command_list, initial_state);
        }
    }

    return result;
}

void WINAPI D3D12Hook::clear_state(ID3D12GraphicsCommandList* command_list, ID3D12PipelineState* pipeline_state) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = command_list != nullptr ? &(*(void***)command_list)[CLEAR_STATE_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_command_list_diagnostic_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::clear_state)*>() : nullptr;

    clear_cmdlist_state(command_list);
    auto& shader_registry = render::ShaderOverrideRegistry::get();
    shader_registry.hunter_clear_command_list(command_list);

    auto* bound_pipeline_state = pipeline_state;
    if (shader_registry.should_track_d3d12_pipelines() && pipeline_state != nullptr) {
        bound_pipeline_state = shader_registry.resolve_d3d12_pipeline_state(pipeline_state);
        shader_registry.note_d3d12_pipeline_state_bound(pipeline_state, bound_pipeline_state);
    }

    if (original != nullptr) {
        original(command_list, bound_pipeline_state);
    }

    if (pipeline_state != nullptr) {
        update_cmdlist_pso(command_list, pipeline_state);
        if (shader_registry.should_track_d3d12_pipelines()) {
            shader_registry.hunter_record_set_pipeline_state(command_list, pipeline_state);
        }
    }
}

void WINAPI D3D12Hook::set_pipeline_state(ID3D12GraphicsCommandList* command_list, ID3D12PipelineState* pipeline_state) {
    update_cmdlist_pso(command_list, pipeline_state);
    auto d3d12 = g_d3d12_hook;
    const auto slot = &(*(void***)command_list)[SET_PIPELINE_STATE_VTABLE_INDEX];
    auto* hook = d3d12->find_set_pipeline_state_hook(slot);
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::set_pipeline_state)*>() : nullptr;

    if (original == nullptr) {
        return;
    }

    auto& shader_registry = render::ShaderOverrideRegistry::get();
    if (!shader_registry.should_track_d3d12_pipelines()) {
        original(command_list, pipeline_state);
        return;
    }

    auto bound_pipeline_state = shader_registry.resolve_d3d12_pipeline_state(pipeline_state);
    shader_registry.note_d3d12_pipeline_state_bound(pipeline_state, bound_pipeline_state);
    // Cache the Shader-Hunter draw-skip decision per command list so the
    // Draw* hooks can early-return without forwarding when the hunter wants
    // this PS hash suppressed. Also pass the current eye bucket so per-eye
    // selective skip (skip_left_only / skip_right_only) can fire.
    int eye_bucket = 0;
    if (command_list != nullptr) {
        const auto s = read_cmdlist_state(command_list);
        eye_bucket = static_cast<int>(s.last_viewport_bucket);
    }
    shader_registry.hunter_record_set_pipeline_state_with_eye(command_list, pipeline_state, eye_bucket);
    original(command_list, bound_pipeline_state);
}

// Forward declarations for draw-hook callees defined later in this TU.
static void sn2_capture_pass_rtv(
    ID3D12GraphicsCommandList* command_list,
    const CommandListCorrelationState& state);

// 2026-05-20 Phase 5 v4 (env-driven): when UEVR_SN2_SKYATMOS_SKIP_RIGHT=1 and
// the bound PSO is one of the 5 SkyAtmosphereRayMarchingPS variants and the
// command list's viewport is the right-eye bucket, return true so the caller
// SKIPS calling the original draw. Diagnostic: if right-eye blown-white sky
// disappears, SkyAtmos IS the producer. If still blown-white, the bug is in
// a different pass entirely. (Existing UEVR_SHADER_HUNTER_SKIP_RIGHT_ONLY
// env var is only LOGGED, never consumed — see
// [[sn2-shader-hunter-skip-env-is-noop]].)
// Parse comma/space/semicolon-separated hex CRC list (with or without 0x).
static std::unordered_set<uint32_t> sn2_parse_crc_set(const char* s) {
    std::unordered_set<uint32_t> out;
    if (s == nullptr) return out;
    std::string buf;
    auto flush = [&]() {
        if (buf.empty()) return;
        try { out.insert(static_cast<uint32_t>(std::stoul(buf, nullptr, 16))); }
        catch (...) {}
        buf.clear();
    };
    for (; *s; ++s) {
        char c = *s;
        if (c == ',' || c == ';' || c == ' ' || c == '\t' || c == '\r' || c == '\n') { flush(); }
        else if (c == '0' && (s[1] == 'x' || s[1] == 'X') && buf.empty()) { ++s; }
        else if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) buf.push_back(c);
        else flush();
    }
    flush();
    return out;
}

static bool sn2_should_skip_right_sky_atmos(const CommandListCorrelationState& state) {
    static const std::unordered_set<uint32_t> default_sky_atmos_crcs = {
        0x009f8918u, 0x1c5283f9u, 0x2bbaec7fu, 0x89fcbc93u, 0xbe2d188bu
    };
    static const bool builtin_enabled = []() {
        const char* env = std::getenv("UEVR_SN2_SKYATMOS_SKIP_RIGHT");
        return env != nullptr && env[0] != '\0' && env[0] != '0';
    }();
    static const std::unordered_set<uint32_t> extra_skip_crcs = []() {
        return sn2_parse_crc_set(std::getenv("UEVR_SN2_RIGHT_SKIP_CRCS"));
    }();
    if (state.current_pso == nullptr) return false;
    if (!builtin_enabled && extra_skip_crcs.empty()) return false;
    const int view = cmdlist_view_id(state);
    if (view != 1) return false;
    const uint32_t crc = render::ShaderOverrideRegistry::get().d3d12_pso_pixel_crc32(reinterpret_cast<uintptr_t>(state.current_pso));
    if (crc == 0) return false;
    const bool match = (builtin_enabled && default_sky_atmos_crcs.count(crc))
                      || extra_skip_crcs.count(crc);
    if (!match) return false;
    static std::atomic<uint64_t> skip_count{0};
    const auto sc = skip_count.fetch_add(1, std::memory_order_relaxed);
    if (sc < 16 || (sc % 200) == 0) {
        SPDLOG_WARN("[SN2-RIGHT-SKIP] right-eye draw skipped pso_crc=0x{:08x} (sc={})", crc, sc + 1);
    }
    return true;
}

void WINAPI D3D12Hook::draw_instanced(
    ID3D12GraphicsCommandList* command_list,
    UINT vertex_count_per_instance,
    UINT instance_count,
    UINT start_vertex_location,
    UINT start_instance_location
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = command_list != nullptr ? &(*(void***)command_list)[DRAW_INSTANCED_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_command_list_diagnostic_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::draw_instanced)*>() : nullptr;

    if (is_stereo_trace_enabled()) {
        increment_bucket(
            g_current_stereo_trace_bucket,
            g_stereo_trace_counters.draw_unknown,
            g_stereo_trace_counters.draw_left,
            g_stereo_trace_counters.draw_right,
            g_stereo_trace_counters.draw_full,
            g_stereo_trace_counters.draw_multi);
    }

    // ShaderHunter draw-skip: if the bound PSO's PS hash is hunter-suppressed,
    // do NOT forward to the original Draw call. ReShade ShaderToggler's
    // mechanism. Far more reliable than substituting the PSO bytecode.
    auto& reg = render::ShaderOverrideRegistry::get();
    reg.hunter_inc_draw_hit();
    // Read per-CL state once for both eye-diff and per-eye-skip decisions.
    const auto s = (command_list != nullptr) ? read_cmdlist_state(command_list) : g_cmdlist_state_empty;
    const int eye_bucket = static_cast<int>(s.last_viewport_bucket);
    // Eye-Diff record: per-PSO per-eye fingerprint of the current draw.
    if (reg.eyediff_enabled() && command_list != nullptr) {
        auto [ps_hash, vs_hash] = reg.snapshot_pso_hashes_for(reinterpret_cast<uintptr_t>(s.current_pso));
        reg.eyediff_record_draw(ps_hash, vs_hash, eye_bucket, s.last_rtv0_handle, s.last_graphics_root_desc_table0);
    }
    sn2_log_pso3069_draw_snapshot(
        command_list,
        s,
        "DrawInstanced",
        vertex_count_per_instance,
        instance_count,
        start_vertex_location,
        0,
        start_instance_location);
    sn2_capture_pass_rtv(command_list, s);
    // Phase 5 v4: env-driven skip of right-eye SkyAtmos draws (diagnostic).
    if (sn2_should_skip_right_sky_atmos(s)) {
        return;
    }
    // Per-eye selective skip — evaluated at draw time so viewport (and thus
    // eye bucket) is guaranteed to be set on this CL by now.
    if (s.current_pso != nullptr &&
            reg.hunter_should_skip_draw_per_eye(reinterpret_cast<uintptr_t>(s.current_pso), eye_bucket)) {
        reg.hunter_inc_draw_skipped();
        return;
    }
    if (reg.hunter_should_skip_graphics(command_list)) {
        reg.hunter_inc_draw_skipped();
        return;
    }

    if (original != nullptr) {
        original(command_list, vertex_count_per_instance, instance_count, start_vertex_location, start_instance_location);
    }
}

void WINAPI D3D12Hook::draw_indexed_instanced(
    ID3D12GraphicsCommandList* command_list,
    UINT index_count_per_instance,
    UINT instance_count,
    UINT start_index_location,
    INT base_vertex_location,
    UINT start_instance_location
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = command_list != nullptr ? &(*(void***)command_list)[DRAW_INDEXED_INSTANCED_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_command_list_diagnostic_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::draw_indexed_instanced)*>() : nullptr;

    if (is_stereo_trace_enabled()) {
        increment_bucket(
            g_current_stereo_trace_bucket,
            g_stereo_trace_counters.draw_indexed_unknown,
            g_stereo_trace_counters.draw_indexed_left,
            g_stereo_trace_counters.draw_indexed_right,
            g_stereo_trace_counters.draw_indexed_full,
            g_stereo_trace_counters.draw_indexed_multi);
    }

    auto& reg2 = render::ShaderOverrideRegistry::get();
    reg2.hunter_inc_draw_indexed_hit();
    const auto s2 = (command_list != nullptr) ? read_cmdlist_state(command_list) : g_cmdlist_state_empty;
    const int eye_bucket2 = static_cast<int>(s2.last_viewport_bucket);
    if (reg2.eyediff_enabled() && command_list != nullptr) {
        auto [ps_hash, vs_hash] = reg2.snapshot_pso_hashes_for(reinterpret_cast<uintptr_t>(s2.current_pso));
        reg2.eyediff_record_draw(ps_hash, vs_hash, eye_bucket2, s2.last_rtv0_handle, s2.last_graphics_root_desc_table0);
    }
    sn2_log_pso3069_draw_snapshot(
        command_list,
        s2,
        "DrawIndexedInstanced",
        index_count_per_instance,
        instance_count,
        start_index_location,
        base_vertex_location,
        start_instance_location);
    sn2_capture_pass_rtv(command_list, s2);
    // Phase 5 v4: env-driven skip of right-eye SkyAtmos draws (diagnostic).
    if (sn2_should_skip_right_sky_atmos(s2)) {
        return;
    }
    if (s2.current_pso != nullptr &&
            reg2.hunter_should_skip_draw_per_eye(reinterpret_cast<uintptr_t>(s2.current_pso), eye_bucket2)) {
        reg2.hunter_inc_draw_indexed_skipped();
        return;
    }
    if (reg2.hunter_should_skip_graphics(command_list)) {
        reg2.hunter_inc_draw_indexed_skipped();
        return;
    }

    if (original != nullptr) {
        original(command_list, index_count_per_instance, instance_count, start_index_location, base_vertex_location, start_instance_location);
    }
}

void WINAPI D3D12Hook::dispatch(
    ID3D12GraphicsCommandList* command_list,
    UINT thread_group_count_x,
    UINT thread_group_count_y,
    UINT thread_group_count_z
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = command_list != nullptr ? &(*(void***)command_list)[DISPATCH_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_command_list_diagnostic_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::dispatch)*>() : nullptr;

    auto& reg = render::ShaderOverrideRegistry::get();
    reg.hunter_inc_dispatch_hit();
    // Per-eye selective skip for compute. eye_bucket from CL viewport state
    // (compute usually inherits the graphics-pass viewport set just before).
    if (command_list != nullptr) {
        const auto s = read_cmdlist_state(command_list);
        if (s.current_pso != nullptr &&
                reg.hunter_should_skip_draw_per_eye(reinterpret_cast<uintptr_t>(s.current_pso),
                                                    static_cast<int>(s.last_viewport_bucket))) {
            reg.hunter_inc_dispatch_skipped();
            return;
        }
    }
    if (reg.hunter_should_skip_compute(command_list)) {
        reg.hunter_inc_dispatch_skipped();
        return;
    }

    // 2026-05-19 SN2 COMPUTE CAPTURE — capture LEFT-eye target dispatches
    if (sn2_compute_capture::enabled() && command_list != nullptr) {
        const auto state = read_cmdlist_state(command_list);
        const int view_id = cmdlist_view_id(state);
        void* current_pso = state.current_pso;
        if (current_pso != nullptr) {
            const uint32_t cs_crc = reg.d3d12_pso_compute_crc32(reinterpret_cast<uintptr_t>(current_pso));
            sn2_compute_capture::capture_direct(command_list, current_pso, cs_crc,
                thread_group_count_x, thread_group_count_y, thread_group_count_z, view_id);
        }
    }

    if (original != nullptr) {
        original(command_list, thread_group_count_x, thread_group_count_y, thread_group_count_z);
    }
}

void WINAPI D3D12Hook::execute_bundle(
    ID3D12GraphicsCommandList* command_list,
    ID3D12GraphicsCommandList* bundle
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = command_list != nullptr ? &(*(void***)command_list)[EXECUTE_BUNDLE_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_command_list_diagnostic_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::execute_bundle)*>() : nullptr;

    auto& reg = render::ShaderOverrideRegistry::get();
    reg.hunter_inc_execute_bundle_hit();
    if (reg.hunter_should_skip_graphics(command_list) || reg.hunter_should_skip_graphics(bundle)) {
        reg.hunter_inc_execute_bundle_skipped();
        return;
    }

    if (original != nullptr) {
        original(command_list, bundle);
    }
}

void WINAPI D3D12Hook::execute_indirect(
    ID3D12GraphicsCommandList* command_list,
    ID3D12CommandSignature* command_signature,
    UINT max_command_count,
    ID3D12Resource* argument_buffer,
    UINT64 argument_buffer_offset,
    ID3D12Resource* count_buffer,
    UINT64 count_buffer_offset
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = command_list != nullptr ? &(*(void***)command_list)[EXECUTE_INDIRECT_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_command_list_diagnostic_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::execute_indirect)*>() : nullptr;

    auto& reg = render::ShaderOverrideRegistry::get();
    reg.hunter_inc_execute_indirect_hit();
    if (reg.hunter_should_skip_graphics(command_list)) {
        reg.hunter_inc_execute_indirect_skipped();
        return;
    }

    // 2026-05-19: SN2 right-eye fog/tile compute dispatches <0,1,1> per user's
    // RenderDoc analysis (captures/pso3069_PS_pseudocode.md:717). Log per-eye
    // ExecuteIndirect calls with their argument buffer GPU VA + count_buffer.
    // The CPU mapping of arg buffers (if tracked) lets us read the X,Y,Z that
    // will be dispatched.
    static const bool sn2_ei_diag = []() {
        const char* env = std::getenv("UEVR_SN2_EI_DIAG");
        return env != nullptr && env[0] != '\0' && env[0] != '0';
    }();
    if (sn2_ei_diag && command_list != nullptr && argument_buffer != nullptr) {
        const auto state = read_cmdlist_state(command_list);
        const int view_id = cmdlist_view_id(state);
        const int fog_view_id = sn2_get_current_fog_view();
        ID3D12Device* device = d3d12 != nullptr ? d3d12->get_device() : nullptr;
        if (device != nullptr) {
            sn2_gpu_readback::init(device);
        }

        // Capture PSO identity at EI time. For compute indirects, the
        // currently-bound PSO IS the dispatched shader.
        void* current_pso = state.current_pso;
        uint32_t cs_crc = 0;
        uint32_t ps_crc = 0;
        if (current_pso != nullptr) {
            auto& reg = render::ShaderOverrideRegistry::get();
            cs_crc = reg.d3d12_pso_compute_crc32(reinterpret_cast<uintptr_t>(current_pso));
            ps_crc = reg.d3d12_pso_pixel_crc32(reinterpret_cast<uintptr_t>(current_pso));
        }

        sn2_command_signature_registry::Entry sig_info{};
        const bool sig_known = sn2_command_signature_registry::lookup(command_signature, sig_info);
        const char* sig_kind = sig_known ? sn2_command_signature_registry::kind(sig_info) : "UNTRACKED";

        // 2026-05-19 SN2 COMPUTE CAPTURE: capture LEFT-eye Nanite/VSM indirect dispatches
        if (current_pso != nullptr && cs_crc != 0) {
            sn2_compute_capture::capture_indirect(command_list, current_pso, cs_crc,
                argument_buffer, argument_buffer_offset, view_id);
        }

        static std::atomic<uint64_t> n_all{0};
        const auto n = n_all.fetch_add(1, std::memory_order_relaxed);
        const bool log_this = (n < 128) || ((n % 500) == 0);

        if (log_this) {
            D3D12_GPU_VIRTUAL_ADDRESS arg_va = argument_buffer->GetGPUVirtualAddress() + argument_buffer_offset;
            const char* tag =
                (view_id == 0) ? "EI-VP-LEFT" :
                (view_id == 1) ? "EI-VP-RIGHT" :
                (fog_view_id == 0) ? "EI-FOG-LEFT" :
                (fog_view_id == 1) ? "EI-FOG-RIGHT" :
                "EI-UNKNOWN";
            const bool readback_args = !sig_known || sig_info.has_dispatch || sig_info.has_dispatch_mesh;
            const char* readback_tag =
                (sig_known && sig_info.has_dispatch && view_id == 0) ? "EI-DISPATCH-LEFT" :
                (sig_known && sig_info.has_dispatch && view_id == 1) ? "EI-DISPATCH-RIGHT" :
                (sig_known && sig_info.has_dispatch_mesh && view_id == 0) ? "EI-MESH-LEFT" :
                (sig_known && sig_info.has_dispatch_mesh && view_id == 1) ? "EI-MESH-RIGHT" :
                tag;
            if (readback_args && sn2_gpu_readback::initialized()) {
                sn2_gpu_readback::schedule_copy(command_list, argument_buffer,
                    argument_buffer_offset, 64, readback_tag, n + 1);
            }
            SPDLOG_WARN("[SN2-EI] tag={} ei_n={} view_id={} fog_view={} bucket={} sig_id={} sig_kind={} sig_stride={} pso=0x{:x} cs_crc=0x{:08x} ps_crc=0x{:08x} arg_va=0x{:x} max={} count_buf={} arg_off={} count_off={} readback={}",
                tag, n + 1, view_id, fog_view_id, static_cast<int>(state.last_viewport_bucket),
                sig_known ? sig_info.id : 0,
                sig_kind,
                sig_known ? sig_info.byte_stride : 0,
                (uint64_t)current_pso, cs_crc, ps_crc,
                (uint64_t)arg_va, max_command_count, count_buffer != nullptr ? "set" : "null",
                argument_buffer_offset, count_buffer_offset, readback_args ? 1 : 0);
        }
    }

    // 2026-05-19 RIGHT-EYE INDIRECT ARG SUBSTITUTE.
    // If this ExecuteIndirect is a RIGHT-eye MainIndirectDispatchCS dispatch
    // AND we have LEFT-eye captured arg bytes for this PSO in our UPLOAD
    // buffer, SWAP the arg buffer parameter to our buffer. Result: the
    // downstream Nanite cull computes that read this dispatch's output will
    // actually run (with non-zero workgroup counts) for right eye.
    static const bool sn2_arg_sub = []() {
        const char* env = std::getenv("UEVR_SN2_RIGHT_ARG_SUBSTITUTE");
        return env != nullptr && env[0] != '\0' && env[0] != '0';
    }();
    ID3D12Resource* effective_arg_buffer = argument_buffer;
    UINT64 effective_arg_offset = argument_buffer_offset;
    if (sn2_arg_sub && command_list != nullptr && argument_buffer != nullptr) {
        const auto state = read_cmdlist_state(command_list);
        const int view_id = cmdlist_view_id(state);
        void* current_pso = state.current_pso;
        if (view_id == 1 && current_pso != nullptr) {
            auto& reg = render::ShaderOverrideRegistry::get();
            const uint32_t cs_crc = reg.d3d12_pso_compute_crc32(reinterpret_cast<uintptr_t>(current_pso));
            if (cs_crc != 0 && sn2_compute_capture::is_main_indirect_compute(cs_crc)) {
                const uint64_t off = sn2_compute_capture::main_indirect_inject_offset(cs_crc);
                if (off != UINT64_MAX && sn2_indirect_arg_inject::initialized()) {
                    ID3D12Resource* inject_buf = sn2_indirect_arg_inject::resource();
                    if (inject_buf != nullptr) {
                        effective_arg_buffer = inject_buf;
                        effective_arg_offset = off;
                        static std::atomic<uint64_t> sub_count{0};
                        const auto sc = sub_count.fetch_add(1, std::memory_order_relaxed);
                        if (sc < 16 || (sc % 200) == 0) {
                            SPDLOG_WARN("[SN2-RIGHT-ARG-SUB] RIGHT MainIndirectCS cs_crc=0x{:08x} arg_va=0x{:x} -> 0x{:x} (sub={})",
                                cs_crc, (uint64_t)argument_buffer->GetGPUVirtualAddress() + argument_buffer_offset,
                                (uint64_t)inject_buf->GetGPUVirtualAddress() + off, sc + 1);
                        }
                    }
                }
            }
        }
        // ALSO: when LEFT-eye MainIndirectCS fires, schedule GPU readback of its
        // arg buffer (so next frame we have its bytes for right-eye substitute).
        if (view_id == 0 && current_pso != nullptr) {
            auto& reg = render::ShaderOverrideRegistry::get();
            const uint32_t cs_crc = reg.d3d12_pso_compute_crc32(reinterpret_cast<uintptr_t>(current_pso));
            if (cs_crc != 0 && sn2_compute_capture::is_main_indirect_compute(cs_crc)) {
                ID3D12Device* device = d3d12 != nullptr ? d3d12->get_device() : nullptr;
                if (device != nullptr) {
                    sn2_gpu_readback::init(device);
                    sn2_indirect_arg_inject::init(device);
                }
                if (sn2_gpu_readback::initialized()) {
                    // Tag the slot with the cs_crc32 so the drain knows which CS this was
                    static thread_local char tag_buf[32];
                    std::snprintf(tag_buf, sizeof(tag_buf), "MAIN-LEFT-%08x", cs_crc);
                    sn2_gpu_readback::schedule_copy(command_list, argument_buffer,
                        argument_buffer_offset, 16, tag_buf, cs_crc);
                }
            }
        }
    }

    if (original != nullptr) {
        original(command_list, command_signature, max_command_count, effective_arg_buffer, effective_arg_offset, count_buffer, count_buffer_offset);
    }
}

void WINAPI D3D12Hook::dispatch_mesh(
    ID3D12GraphicsCommandList6* command_list,
    UINT thread_group_count_x,
    UINT thread_group_count_y,
    UINT thread_group_count_z
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = command_list != nullptr ? &(*(void***)command_list)[DISPATCH_MESH_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_command_list_diagnostic_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::dispatch_mesh)*>() : nullptr;

    auto& reg = render::ShaderOverrideRegistry::get();
    auto* base_command_list = reinterpret_cast<ID3D12GraphicsCommandList*>(command_list);
    reg.hunter_inc_dispatch_mesh_hit();
    if (reg.hunter_should_skip_graphics(base_command_list)) {
        reg.hunter_inc_dispatch_mesh_skipped();
        return;
    }

    if (original != nullptr) {
        original(command_list, thread_group_count_x, thread_group_count_y, thread_group_count_z);
    }
}

void WINAPI D3D12Hook::rs_set_viewports(
    ID3D12GraphicsCommandList* command_list,
    UINT num_viewports,
    const D3D12_VIEWPORT* viewports
) {
    update_cmdlist_viewport(command_list, num_viewports, viewports);
    auto d3d12 = g_d3d12_hook;
    const auto slot = command_list != nullptr ? &(*(void***)command_list)[RS_SET_VIEWPORTS_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_command_list_diagnostic_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::rs_set_viewports)*>() : nullptr;

    if (is_stereo_trace_enabled()) {
        g_current_stereo_trace_bucket = classify_viewports(num_viewports, viewports);
        increment_bucket(
            g_current_stereo_trace_bucket,
            g_stereo_trace_counters.viewport_unknown,
            g_stereo_trace_counters.viewport_left,
            g_stereo_trace_counters.viewport_right,
            g_stereo_trace_counters.viewport_full,
            g_stereo_trace_counters.viewport_multi);
    }

    if (original != nullptr) {
        original(command_list, num_viewports, viewports);
    }
}

void WINAPI D3D12Hook::om_set_render_targets(
    ID3D12GraphicsCommandList* command_list,
    UINT num_render_target_descriptors,
    const D3D12_CPU_DESCRIPTOR_HANDLE* render_target_descriptors,
    BOOL rts_single_handle_to_descriptor_range,
    const D3D12_CPU_DESCRIPTOR_HANDLE* depth_stencil_descriptor
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = command_list != nullptr ? &(*(void***)command_list)[OM_SET_RENDER_TARGETS_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_command_list_diagnostic_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::om_set_render_targets)*>() : nullptr;

    if (is_stereo_trace_enabled()) {
        ++g_stereo_trace_counters.om_set_render_targets;
    }

    const auto diagnostic_rtv_count = rts_single_handle_to_descriptor_range && num_render_target_descriptors > 1
        ? 1
        : num_render_target_descriptors;

    render::D3D12Diagnostics::get().record_rtv_bind(
        "D3D12Hook::OMSetRenderTargets",
        diagnostic_rtv_count,
        render_target_descriptors,
        depth_stencil_descriptor);

    // Eye-diff: capture RTV[0] handle so the draw-hook can fingerprint it.
    if (num_render_target_descriptors > 0 && render_target_descriptors != nullptr) {
        update_cmdlist_rtv0(command_list, &render_target_descriptors[0]);
    }

    if (original != nullptr) {
        original(command_list, num_render_target_descriptors, render_target_descriptors, rts_single_handle_to_descriptor_range, depth_stencil_descriptor);
    }
}

void WINAPI D3D12Hook::clear_render_target_view(
    ID3D12GraphicsCommandList* command_list,
    D3D12_CPU_DESCRIPTOR_HANDLE render_target_view,
    const FLOAT color_rgba[4],
    UINT num_rects,
    const D3D12_RECT* rects
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = command_list != nullptr ? &(*(void***)command_list)[CLEAR_RENDER_TARGET_VIEW_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_command_list_diagnostic_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::clear_render_target_view)*>() : nullptr;

    if (is_stereo_trace_enabled()) {
        increment_bucket(
            classify_rects(num_rects, rects),
            g_stereo_trace_counters.clear_unknown,
            g_stereo_trace_counters.clear_left,
            g_stereo_trace_counters.clear_right,
            g_stereo_trace_counters.clear_full,
            g_stereo_trace_counters.clear_multi);
    }

    if (original != nullptr) {
        original(command_list, render_target_view, color_rgba, num_rects, rects);
    }
}

void WINAPI D3D12Hook::resource_barrier(
    ID3D12GraphicsCommandList* command_list,
    UINT num_barriers,
    const D3D12_RESOURCE_BARRIER* barriers
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = command_list != nullptr ? &(*(void***)command_list)[RESOURCE_BARRIER_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_command_list_diagnostic_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::resource_barrier)*>() : nullptr;

    if (is_stereo_trace_enabled()) {
        g_stereo_trace_counters.resource_barriers.fetch_add(num_barriers);
    }

    render::D3D12Diagnostics::get().record_resource_barriers(
        "D3D12Hook::ResourceBarrier",
        num_barriers,
        barriers);

    if (original != nullptr) {
        original(command_list, num_barriers, barriers);
    }
}

// 2026-05-17 SN2 FOG-FIX Task #45: thread-local bindless heap base.
// SetDescriptorHeaps stores the currently-bound CBV_SRV_UAV heap's CPU and
// GPU base addresses + the increment size. SetComputeRootDescriptorTable
// uses these to translate the bound table's GPU descriptor handle back to a
// CPU descriptor handle, then walks neighbouring descriptors and matches
// them against sn2_fog_uav_map / sn2_fog_srv_map to retro-tag pooled
// resources with the current view_id atomic.
struct BindlessHeapState {
    SIZE_T  cpu_base{0};
    UINT64  gpu_base{0};
    UINT    stride{0};
    UINT    num_descriptors{0};  // upper bound for in-heap check
};
thread_local BindlessHeapState tls_bindless_heap{};

// Returns true if `gpu` is within [gpu_base, gpu_base + num_descriptors * stride).
// Used to gate GPU→CPU conversion: cross-heap GPU handles must NOT be projected
// onto our captured CPU base (would yield garbage cpu_start).
inline bool gpu_handle_in_bindless_heap(UINT64 gpu) {
    if (tls_bindless_heap.stride == 0 || tls_bindless_heap.gpu_base == 0) return false;
    if (gpu < tls_bindless_heap.gpu_base) return false;
    const UINT64 max_off = (UINT64)tls_bindless_heap.num_descriptors * (UINT64)tls_bindless_heap.stride;
    if (max_off == 0) return false;
    return (gpu - tls_bindless_heap.gpu_base) < max_off;
}

// Re-use the SRV-map TU-cross-link for the view atomic.
extern "C" int sn2_get_current_fog_view();

struct Sn2Pso3069SlotInfo {
    UINT slot{};
    uint64_t gpu_handle{};
    SIZE_T cpu_handle{};
    bool in_heap{};
    bool readable{};
    bool known_descriptor{};
    uint64_t desc_hash{};
    const char* kind{"Unknown"};
    ID3D12Resource* resource{};
    DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
    D3D12_RESOURCE_DIMENSION dimension{D3D12_RESOURCE_DIMENSION_UNKNOWN};
    uint64_t width{};
    uint32_t height{};
    uint16_t depth_or_array{};
    int tracked_view_id{-1};
    uint64_t descriptor_generation{};
};

Sn2Pso3069SlotInfo sn2_resolve_pso3069_slot(uint64_t base_gpu_handle, UINT slot) {
    Sn2Pso3069SlotInfo out{};
    out.slot = slot;
    if (base_gpu_handle == 0 || tls_bindless_heap.stride == 0) {
        return out;
    }

    out.gpu_handle = base_gpu_handle + static_cast<uint64_t>(slot) * tls_bindless_heap.stride;
    out.in_heap = gpu_handle_in_bindless_heap(out.gpu_handle);
    if (!out.in_heap) {
        return out;
    }

    out.cpu_handle = tls_bindless_heap.cpu_base + static_cast<SIZE_T>(out.gpu_handle - tls_bindless_heap.gpu_base);
    out.readable = is_readable_process_range_d3d12(out.cpu_handle, 32);
    out.desc_hash = sn2_descriptor_registry::descriptor_memory_hash(D3D12_CPU_DESCRIPTOR_HANDLE{out.cpu_handle});

    sn2_descriptor_registry::Entry desc_entry{};
    if (sn2_descriptor_registry::lookup(D3D12_CPU_DESCRIPTOR_HANDLE{out.cpu_handle}, desc_entry)) {
        out.known_descriptor = true;
        out.kind = sn2_descriptor_registry::kind_name(desc_entry.kind);
        out.resource = desc_entry.resource;
        out.descriptor_generation = desc_entry.generation;
        if (desc_entry.has_resource_desc) {
            out.format = desc_entry.resource_desc.Format;
            out.dimension = desc_entry.resource_desc.Dimension;
            out.width = static_cast<uint64_t>(desc_entry.resource_desc.Width);
            out.height = desc_entry.resource_desc.Height;
            out.depth_or_array = desc_entry.resource_desc.DepthOrArraySize;
        }
    }

    sn2_bindless_slot_map::Entry fog_entry{};
    if (sn2_bindless_slot_map::lookup(D3D12_CPU_DESCRIPTOR_HANDLE{out.cpu_handle}, fog_entry)) {
        out.tracked_view_id = fog_entry.src_view_id;
        if (out.resource == nullptr) {
            out.resource = fog_entry.src_resource;
        }
    }

    if (out.tracked_view_id == -1 && out.resource != nullptr) {
        sn2_fog_srv_map::Entry srv{};
        if (sn2_fog_srv_map::lookup_by_resource(out.resource, srv)) {
            out.tracked_view_id = srv.view_id;
        } else {
            sn2_fog_uav_map::Entry uav{};
            if (sn2_fog_uav_map::lookup_by_resource(out.resource, uav)) {
                out.tracked_view_id = uav.view_id;
            }
        }
    }

    return out;
}

const char* sn2_eye_name(int view_id, StereoTraceBucket bucket) {
    if (view_id == 0) return "left";
    if (view_id == 1) return "right";
    switch (bucket) {
    case StereoTraceBucket::Left: return "left_bucket";
    case StereoTraceBucket::Right: return "right_bucket";
    case StereoTraceBucket::Full: return "full";
    case StereoTraceBucket::Multi: return "multi";
    default: return "unknown";
    }
}

// Phase 3 (multi-pass): queue capture intent for sky/vol/post PSOs. Same
// pattern as the basepass PSO3069 snap: lookup the bound RTV resource and
// queue_intent_named so the present hook drains it. One capture per eye per
// pass per N hits.
static void sn2_capture_pass_rtv(
    ID3D12GraphicsCommandList* command_list,
    const CommandListCorrelationState& state)
{
    static const bool snap_passes = []() {
        const char* env = std::getenv("UEVR_SN2_PASS_SNAPSHOTS");
        return env != nullptr && env[0] != '\0' && env[0] != '0';
    }();
    if (!snap_passes || command_list == nullptr || state.current_pso == nullptr) return;
    if (!sn2_rt_snapshot::initialized() || state.last_rtv0_handle == 0) return;

    auto& reg = render::ShaderOverrideRegistry::get();
    const uint32_t ps_crc = reg.d3d12_pso_pixel_crc32(reinterpret_cast<uintptr_t>(state.current_pso));
    if (ps_crc == 0) return;

    const char* tag = nullptr;
    switch (ps_crc) {
    // SkyAtmosphereRayMarchingPS (5 variants).
    case 0x009f8918u: case 0x1c5283f9u: case 0x2bbaec7fu:
    case 0x89fcbc93u: case 0xbe2d188bu:
        tag = "SKY";
        break;
    // VolumetricRenderTargetPS + VolumetricRTOverScenePS.
    case 0x194c423au: case 0xfd0327d5u:
    case 0xdb56c691u: case 0xf1d1132cu:
        tag = "VOL";
        break;
    // ApplyLowerHemisphereColorPS (the sky-tint pass).
    case 0x1f958d46u:
        tag = "SKYLO";
        break;
    default:
        return;
    }

    ID3D12Resource* rtv_res = sn2_rt_snapshot::lookup_rtv(state.last_rtv0_handle);
    if (rtv_res == nullptr) return;

    const int view_id = cmdlist_view_id(state);
    const char eye = (view_id == 0) ? 'L' : (view_id == 1 ? 'R' : '?');

    // Per-(tag,eye) counter so we capture once per 600 hits.
    static std::atomic<uint64_t> sky_l{0}, sky_r{0};
    static std::atomic<uint64_t> vol_l{0}, vol_r{0};
    static std::atomic<uint64_t> sl_l{0}, sl_r{0};
    std::atomic<uint64_t>* ctr = nullptr;
    if (tag[0] == 'S' && tag[1] == 'K' && tag[2] == 'Y' && tag[3] == 'L') {
        ctr = (eye == 'L') ? &sl_l : &sl_r;
    } else if (tag[0] == 'S') {
        ctr = (eye == 'L') ? &sky_l : &sky_r;
    } else {
        ctr = (eye == 'L') ? &vol_l : &vol_r;
    }
    const uint64_t cnt = ctr->fetch_add(1, std::memory_order_relaxed);
    if ((cnt % 600) != 0) return;

    sn2_rt_snapshot::queue_intent_named(rtv_res, tag, eye, cnt);
}

static int sn2_parse_draw_count_from_marker(const char* text, int fallback) {
    if (text == nullptr || *text == '\0') return fallback;
    const char* p = std::strstr(text, "draws");
    if (p == nullptr) p = std::strstr(text, "Draws");
    if (p == nullptr) return fallback;
    while (*p != '\0' && (*p < '0' || *p > '9')) ++p;
    if (*p == '\0') return fallback;
    char* end = nullptr;
    const long parsed = std::strtol(p, &end, 10);
    if (end == p || parsed <= 0 || parsed > 100000) return fallback;
    return static_cast<int>(parsed);
}

static bool sn2_consume_pso3069_capture_log_token(uint64_t seq) {
    static std::atomic<int> remaining{env_int_a("UEVR_SN2_PSO3069_CAPTURE_NEXT", 0)};
    static std::atomic<bool> initial_logged{false};
    const int initial = remaining.load(std::memory_order_relaxed);
    if (initial > 0 && !initial_logged.exchange(true, std::memory_order_relaxed)) {
        SPDLOG_WARN("[SN2-CAPTURE-MARK] armed next={} pso3069 draws from UEVR_SN2_PSO3069_CAPTURE_NEXT", initial);
    }

    int value = remaining.load(std::memory_order_acquire);
    while (value > 0) {
        if (remaining.compare_exchange_weak(value, value - 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
            return true;
        }
    }

    std::string marker = env_value_a("UEVR_SN2_PSO3069_CAPTURE_MARK_FILE");
    if (marker == "(unset)" || marker == "(too-long)" || marker.empty()) {
        char temp_path[MAX_PATH]{};
        const DWORD temp_len = GetTempPathA(static_cast<DWORD>(sizeof(temp_path)), temp_path);
        if (temp_len == 0 || temp_len >= sizeof(temp_path)) return false;
        marker = std::string(temp_path) + "uevr_sn2_pso3069_capture.req";
    }

    if (GetFileAttributesA(marker.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    int draws = env_int_a("UEVR_SN2_PSO3069_CAPTURE_MARK_DRAWS", 256);
    HANDLE file = CreateFileA(marker.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        char buf[512]{};
        DWORD read = 0;
        if (ReadFile(file, buf, static_cast<DWORD>(sizeof(buf) - 1), &read, nullptr) && read > 0) {
            buf[read] = '\0';
            draws = sn2_parse_draw_count_from_marker(buf, draws);
        }
        CloseHandle(file);
    }
    DeleteFileA(marker.c_str());
    if (draws <= 0) draws = 256;
    remaining.store(draws - 1, std::memory_order_release);
    SPDLOG_WARN("[SN2-CAPTURE-MARK] armed next={} pso3069 draws at seq={} marker={}", draws, seq + 1, marker);
    return true;
}

static void sn2_log_pso3069_draw_snapshot(
    ID3D12GraphicsCommandList* command_list,
    const CommandListCorrelationState& state,
    const char* draw_kind,
    UINT a,
    UINT b,
    UINT c,
    INT d,
    UINT e)
{
    if (!sn2_pso3069_diag_enabled() || command_list == nullptr || state.current_pso == nullptr) {
        return;
    }

    auto& reg = render::ShaderOverrideRegistry::get();
    const uint32_t ps_crc = reg.d3d12_pso_pixel_crc32(reinterpret_cast<uintptr_t>(state.current_pso));
    if (ps_crc != 0x166DBA88u) {
        return;
    }

    const int view_id = cmdlist_view_id(state);
    const auto slot5 = sn2_resolve_pso3069_slot(state.last_graphics_root_desc_table0, 5);
    const auto slot8 = sn2_resolve_pso3069_slot(state.last_graphics_root_desc_table0, 8);
    const auto slot9 = sn2_resolve_pso3069_slot(state.last_graphics_root_desc_table0, 9);

    const uint64_t cbv4 = state.last_graphics_root_cbv[4];
    const uint64_t cbv5 = state.last_graphics_root_cbv[5];
    const uint64_t cbv6 = state.last_graphics_root_cbv[6];
    const uint64_t cbv7 = state.last_graphics_root_cbv[7];

    bool cb0_cpu_mapped = false;
    uint64_t cb0_rows_hash = 0;
    float r4[4]{}, r5[4]{}, r6[4]{}, r7[4]{};
    if (cbv4 != 0) {
        uint8_t* cpu = sn2_upload_buf_map::gpu_va_to_cpu(cbv4, 128);
        if (cpu != nullptr) {
            cb0_cpu_mapped = true;
            cb0_rows_hash = sn2_descriptor_registry::fnv1a64(cpu + 64, 64);
            const float* f = reinterpret_cast<const float*>(cpu);
            for (int i = 0; i < 4; ++i) {
                r4[i] = f[16 + i];
                r5[i] = f[20 + i];
                r6[i] = f[24 + i];
                r7[i] = f[28 + i];
            }
        }
    }

    static std::atomic<uint64_t> seq{0};
    const auto n = seq.fetch_add(1, std::memory_order_relaxed);
    const bool marker_log = sn2_consume_pso3069_capture_log_token(n);
    const bool log_this = n < 512 || (n % 1000) == 0 || marker_log;
    if (!log_this) {
        return;
    }

    const auto log_slot = [](const Sn2Pso3069SlotInfo& s, const char* name) {
        SPDLOG_WARN(
            "[SN2-PSO3069-Slot] {} slot={} gpu=0x{:x} cpu=0x{:x} in_heap={} readable={} known={} kind={} res={:p} fmt={} dim={} size={}x{}x{} view={} desc_hash=0x{:016x} gen={}",
            name, s.slot, s.gpu_handle, static_cast<uint64_t>(s.cpu_handle),
            s.in_heap ? 1 : 0, s.readable ? 1 : 0, s.known_descriptor ? 1 : 0,
            s.kind, static_cast<void*>(s.resource), static_cast<int>(s.format),
            static_cast<int>(s.dimension), s.width, s.height, s.depth_or_array,
            s.tracked_view_id, s.desc_hash, s.descriptor_generation);
    };

    SPDLOG_WARN(
        "[SN2-PSO3069-Draw] seq={} kind={} eye={} view_id={} bucket={} pso={:p} ps_crc=0x{:08x} vp=({:.1f},{:.1f},{:.1f},{:.1f}) rtv0=0x{:x} root_table0=0x{:x} cbv4=0x{:x} cbv5=0x{:x} cbv6=0x{:x} cbv7=0x{:x} cb0_cpu={} cb0_rows_hash=0x{:016x} args=({}, {}, {}, {}, {}) desc_registry={}",
        n + 1, draw_kind, sn2_eye_name(view_id, state.last_viewport_bucket),
        view_id, static_cast<int>(state.last_viewport_bucket),
        state.current_pso, ps_crc,
        state.viewport_top_left_x, state.viewport_top_left_y,
        state.viewport_width, state.viewport_height,
        state.last_rtv0_handle, state.last_graphics_root_desc_table0,
        cbv4, cbv5, cbv6, cbv7,
        cb0_cpu_mapped ? 1 : 0, cb0_rows_hash,
        a, b, c, d, e,
        sn2_descriptor_registry::size());

    if (cb0_cpu_mapped) {
        SPDLOG_WARN(
            "[SN2-PSO3069-CB0] seq={} cbv4=0x{:x} row4=({:.6f},{:.6f},{:.6f},{:.6f}) row5=({:.6f},{:.6f},{:.6f},{:.6f}) row6=({:.6f},{:.6f},{:.6f},{:.6f}) row7=({:.6f},{:.6f},{:.6f},{:.6f})",
            n + 1, cbv4,
            r4[0], r4[1], r4[2], r4[3],
            r5[0], r5[1], r5[2], r5[3],
            r6[0], r6[1], r6[2], r6[3],
            r7[0], r7[1], r7[2], r7[3]);
    } else {
        SPDLOG_WARN("[SN2-PSO3069-CB0] seq={} cbv4=0x{:x} NO_CPU_MAPPING", n + 1, cbv4);
    }

    log_slot(slot8, "t8");
    log_slot(slot9, "t9");
    log_slot(slot5, "t5");

    // 2026-05-20 PHASE 3: snapshot basepass RTV per eye for divergence diff.
    // The basepass draw hook fires once per object per eye. We capture the
    // bound RTV (state.last_rtv0_handle → resource via descriptor registry)
    // once every 600 calls per eye so we get a single snapshot per ~10s.
    static const bool snap_basepass = []() {
        const char* env = std::getenv("UEVR_SN2_BASEPASS_SNAPSHOT");
        return env != nullptr && env[0] != '\0' && env[0] != '0';
    }();
    if (snap_basepass && sn2_rt_snapshot::initialized() && state.last_rtv0_handle != 0) {
        ID3D12Resource* rtv_res = sn2_rt_snapshot::lookup_rtv(state.last_rtv0_handle);
        if (rtv_res != nullptr) {
            static std::atomic<uint64_t> bp_left{0}, bp_right{0};
            const uint64_t cnt = (view_id == 0 ? bp_left : bp_right).fetch_add(1, std::memory_order_relaxed);
            if ((cnt % 600) == 0) {
                // Phase 3 v2: queue capture intent for present-time processing.
                sn2_rt_snapshot::queue_intent(rtv_res,
                    view_id == 0 ? 'L' : (view_id == 1 ? 'R' : '?'),
                    cnt);
            }
        } else {
            static std::atomic<uint64_t> miss_cnt{0};
            const uint64_t mc = miss_cnt.fetch_add(1, std::memory_order_relaxed);
            if (mc < 8 || (mc % 4096) == 0) {
                SPDLOG_WARN("[SN2-BP-Snap] rtv_lookup_miss handle=0x{:x} miss_n={}",
                    state.last_rtv0_handle, mc + 1);
            }
        }
    }

    // 2026-05-19 PSO3069 t8/t9 LEFT->RIGHT descriptor redirect.
    // ROOT CAUSE (from compute capture diagnostic): Nanite + VSM compute
    // passes only fire for LEFT eye under -emulatestereo, so RIGHT eye's
    // t8/t9 volumetric-lighting textures are uninitialized. Each eye binds
    // DIFFERENT t8/t9 SRVs (the user's enhanced PSO3069-Slot diag proved
    // this: left desc_hash=0xa150646f3af1b421 vs right=0xccee770f72a739ff).
    //
    // FIX: when LEFT eye binds pso3069, snapshot its t8/t9 source SRV CPU
    // handles (from the same shader-visible bindless heap). When RIGHT eye
    // is about to draw pso3069, CopyDescriptorsSimple LEFT's source SRVs
    // over RIGHT's slot positions in the heap. Right eye then samples the
    // LEFT-populated volume textures with right-eye matrices → correct UV
    // math, shared volume data, no Nanite-not-run staleness.
    //
    // Gated by UEVR_SN2_FOG_SRV_REDIRECT=1 (default OFF) so we can test
    // safely. Source CPU handle is the t8/t9 slot's CPU address in the
    // shader-visible heap (which IS allowed as a CopyDescriptorsSimple
    // source — both shader-visible and non-shader-visible are valid
    // source types per the D3D12 spec).
    static const bool fog_srv_redirect = []() {
        const char* env = std::getenv("UEVR_SN2_FOG_SRV_REDIRECT");
        return env != nullptr && env[0] != '\0' && env[0] != '0';
    }();
    if (fog_srv_redirect) {
        struct LeftFogSrvs {
            uint64_t t8_cpu{0};
            uint64_t t9_cpu{0};
            uint64_t t8_desc_hash{0};
            uint64_t t9_desc_hash{0};
            uint64_t captured_at_seq{0};
        };
        static std::mutex s_mutex;
        static LeftFogSrvs s_left;
        static std::atomic<uint64_t> s_redirect_count{0};

        if (view_id == 0 && slot8.in_heap && slot9.in_heap) {
            // Capture LEFT eye's t8/t9 source positions
            std::scoped_lock _{s_mutex};
            s_left.t8_cpu = static_cast<uint64_t>(slot8.cpu_handle);
            s_left.t9_cpu = static_cast<uint64_t>(slot9.cpu_handle);
            s_left.t8_desc_hash = slot8.desc_hash;
            s_left.t9_desc_hash = slot9.desc_hash;
            s_left.captured_at_seq = n + 1;
        } else if (view_id == 1 && slot8.in_heap && slot9.in_heap) {
            // RIGHT eye: redirect t8/t9 to LEFT's
            uint64_t lt8_cpu = 0, lt9_cpu = 0, lt8_hash = 0, lt9_hash = 0;
            {
                std::scoped_lock _{s_mutex};
                lt8_cpu = s_left.t8_cpu;
                lt9_cpu = s_left.t9_cpu;
                lt8_hash = s_left.t8_desc_hash;
                lt9_hash = s_left.t9_desc_hash;
            }
            if (lt8_cpu != 0 && lt9_cpu != 0 &&
                (lt8_hash != slot8.desc_hash || lt9_hash != slot9.desc_hash))
            {
                // Need the device to call CopyDescriptorsSimple
                auto* d3d12 = g_d3d12_hook;
                ID3D12Device* device = d3d12 != nullptr ? d3d12->get_device() : nullptr;
                if (device != nullptr) {
                    D3D12_CPU_DESCRIPTOR_HANDLE dst{};
                    dst.ptr = static_cast<SIZE_T>(slot8.cpu_handle);
                    D3D12_CPU_DESCRIPTOR_HANDLE src{};
                    src.ptr = static_cast<SIZE_T>(lt8_cpu);
                    // Copy 2 contiguous descriptors (t8, t9) from LEFT to RIGHT.
                    // We assume t9 is at t8 + stride in both eyes (true for
                    // pso3069 per the diag where slot 8 and 9 share heap).
                    device->CopyDescriptorsSimple(2, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                    const auto rc = s_redirect_count.fetch_add(1, std::memory_order_relaxed);
                    if (rc < 16 || (rc % 200) == 0) {
                        SPDLOG_WARN("[SN2-FOG-REDIRECT] right t8/t9 cpu=0x{:x} <- left cpu=0x{:x} (rc={}, was r_hash=0x{:016x} {:016x}, now=left_hash=0x{:016x} {:016x})",
                            static_cast<uint64_t>(slot8.cpu_handle), lt8_cpu, rc + 1,
                            slot8.desc_hash, slot9.desc_hash, lt8_hash, lt9_hash);
                    }
                }
            }
        }
    }
}

// 2026-05-16 SN2 fog t5 descriptor swap — diagnostic phase.
// Hooks ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable so we can
// observe which root parameter / GPU descriptor handle gets bound for the
// right-eye basepass draw. Project memory `sn2-fog-ub-correct-gpu-wrong` proves
// the CPU-side FFogUniformParameters write at +0x120 reaches view 1 correctly
// but PS slot 5 still binds view 0's filled fog volume — meaning the binding
// path is via this D3D12 entrypoint rather than the cbuffer.
void WINAPI D3D12Hook::set_graphics_root_descriptor_table(
    ID3D12GraphicsCommandList* command_list,
    UINT root_parameter_index,
    D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = command_list != nullptr ? &(*(void***)command_list)[SET_GRAPHICS_ROOT_DESCRIPTOR_TABLE_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_command_list_diagnostic_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::set_graphics_root_descriptor_table)*>() : nullptr;

    // Per-cmdlist viewport state. Used to derive view_id (0=left, 1=right,
    // -1=unknown — viewport not set yet on this cmdlist).
    const auto state = read_cmdlist_state(command_list);
    const int view_id = cmdlist_view_id(state);

    // Eye-diff: capture root descriptor table 0 (UE5 bindless heap).
    update_cmdlist_root_desc_table0(command_list, root_parameter_index, base_descriptor);

    // 2026-05-19 PSO3069 descriptor-table diag: log base_descriptor per eye when
    // the current PSO is the basepass-water shader (CRC 166DBA88). If both eyes
    // bind the SAME base_descriptor, they're reading from the same t5/t8/t9
    // bindless slots → if those textures contain only left-eye-correct volume
    // data, the right eye samples wrong data even with correct per-eye matrix.
    // 2026-05-20 Phase 5 — SKY ATMOS right-eye bindless-table swap.
    // Per [[sn2-phase4-SKY-pass-is-origin]] the divergence first appears at
    // SkyAtmosphereRayMarchingPS (right-eye sky RT ~3.5× brighter than left).
    // Root cause: right-eye sky pass samples uninitialized atmosphere/sky-view
    // LUT slots in the bindless heap (Nanite/VSM didn't run for view 1, so
    // its LUT producers never populated those positions). Fix: when LEFT
    // binds root-table-0 for a sky-atmos PSO, snapshot the GPU handle.
    // When RIGHT binds the same root-table-0 for a sky-atmos PSO, override
    // base_descriptor.ptr to point to LEFT's snapshot — right's shader then
    // samples LEFT's populated LUTs. View matrices come from cb0 (separate
    // root parameter, untouched), so the per-eye matrix math still works.
    if (root_parameter_index == 0 && state.current_pso != nullptr) {
        const uint32_t crc_for_swap = render::ShaderOverrideRegistry::get().d3d12_pso_pixel_crc32(reinterpret_cast<uintptr_t>(state.current_pso));
        const bool is_sky_atmos =
            crc_for_swap == 0x009f8918u || crc_for_swap == 0x1c5283f9u ||
            crc_for_swap == 0x2bbaec7fu || crc_for_swap == 0x89fcbc93u ||
            crc_for_swap == 0xbe2d188bu;
        if (is_sky_atmos) {
            static std::atomic<uint64_t> g_left_sky_table0_gpu{0};
            static std::atomic<uint64_t> g_swap_count{0};
            static const bool sky_swap_enabled = []() {
                const char* env = std::getenv("UEVR_SN2_SKYATMOS_RIGHT_TABLE_SWAP");
                return env != nullptr && env[0] != '\0' && env[0] != '0';
            }();
            if (sky_swap_enabled) {
                if (view_id == 0 && base_descriptor.ptr != 0) {
                    g_left_sky_table0_gpu.store(base_descriptor.ptr, std::memory_order_release);
                } else if (view_id == 1) {
                    const uint64_t lgpu = g_left_sky_table0_gpu.load(std::memory_order_acquire);
                    const uint64_t rgpu = base_descriptor.ptr;
                    // v2 (2026-05-20): per-slot CopyDescriptorsSimple instead of
                    // GPU-handle swap. v1 swap fired 58+ times with no visual
                    // change because L and R bindless slot CONTENTS may be
                    // identical (only positions differ). Copy a range of
                    // descriptors from L's CPU base to R's CPU base so right
                    // shader samples L-populated SRVs at R's slot positions.
                    if (lgpu != 0 && rgpu != 0 && lgpu != rgpu &&
                        tls_bindless_heap.stride > 0 &&
                        tls_bindless_heap.gpu_base != 0 &&
                        tls_bindless_heap.cpu_base != 0 &&
                        lgpu >= tls_bindless_heap.gpu_base &&
                        rgpu >= tls_bindless_heap.gpu_base)
                    {
                        const uint64_t l_off = lgpu - tls_bindless_heap.gpu_base;
                        const uint64_t r_off = rgpu - tls_bindless_heap.gpu_base;
                        const uint64_t heap_bytes =
                            (uint64_t)tls_bindless_heap.num_descriptors * tls_bindless_heap.stride;
                        constexpr UINT N_DESC = 16;
                        const uint64_t range_bytes = (uint64_t)N_DESC * tls_bindless_heap.stride;
                        if (l_off + range_bytes <= heap_bytes && r_off + range_bytes <= heap_bytes) {
                            auto* d3d12 = g_d3d12_hook;
                            ID3D12Device* device = d3d12 != nullptr ? d3d12->get_device() : nullptr;
                            if (device != nullptr) {
                                D3D12_CPU_DESCRIPTOR_HANDLE dst{static_cast<SIZE_T>(tls_bindless_heap.cpu_base + r_off)};
                                D3D12_CPU_DESCRIPTOR_HANDLE src{static_cast<SIZE_T>(tls_bindless_heap.cpu_base + l_off)};
                                device->CopyDescriptorsSimple(N_DESC, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                                const auto sc = g_swap_count.fetch_add(1, std::memory_order_relaxed);
                                if (sc < 32 || (sc % 200) == 0) {
                                    SPDLOG_WARN("[SN2-SKYATMOS-COPY] right rp=0 copied {} desc from L_cpu=0x{:x} to R_cpu=0x{:x} (lgpu=0x{:x} rgpu=0x{:x} pso=0x{:08x} sc={})",
                                        N_DESC, (uint64_t)src.ptr, (uint64_t)dst.ptr,
                                        lgpu, rgpu, crc_for_swap, sc + 1);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (root_parameter_index == 0) {
        void* current_pso = state.current_pso;
        if (current_pso != nullptr) {
            const uint32_t crc = render::ShaderOverrideRegistry::get().d3d12_pso_pixel_crc32(reinterpret_cast<uintptr_t>(current_pso));
            if (crc == 0x166DBA88u) {
                static std::atomic<uint64_t> s_left_seen{0}, s_right_seen{0}, s_log_count{0};
                if (view_id == 0) {
                    const auto n = s_left_seen.fetch_add(1, std::memory_order_relaxed);
                    if (n < 8 || (n % 1000) == 0) {
                        SPDLOG_WARN("[SN2-PSO3069-DT] LEFT  rp=0 base.ptr=0x{:x} (n={})", (uint64_t)base_descriptor.ptr, n + 1);
                    }
                } else if (view_id == 1) {
                    const auto n = s_right_seen.fetch_add(1, std::memory_order_relaxed);
                    if (n < 8 || (n % 1000) == 0) {
                        SPDLOG_WARN("[SN2-PSO3069-DT] RIGHT rp=0 base.ptr=0x{:x} (n={})", (uint64_t)base_descriptor.ptr, n + 1);
                    }
                }
                // Periodic summary
                const auto lc = s_log_count.fetch_add(1, std::memory_order_relaxed);
                if ((lc % 5000) == 0 && lc > 0) {
                    SPDLOG_WARN("[SN2-PSO3069-DT] summary: left={} right={} unknown_view={}",
                        s_left_seen.load(), s_right_seen.load(), lc - s_left_seen.load() - s_right_seen.load());
                }
            }
        }
    }

    // 2026-05-17 Step B diag: periodic summary of per-view-id descriptor-table
    // SET counts. Lets us quickly confirm both left-eye and right-eye are
    // detected (vs. all 0 or all 1 like the old thread_local approach).
    {
        static std::atomic<uint64_t> n_unknown{0}, n_left{0}, n_right{0};
        if      (view_id == 0) n_left.fetch_add(1, std::memory_order_relaxed);
        else if (view_id == 1) n_right.fetch_add(1, std::memory_order_relaxed);
        else                   n_unknown.fetch_add(1, std::memory_order_relaxed);

        static std::atomic<uint64_t> last_summary_ms{0};
        const uint64_t now_ms = (uint64_t)GetTickCount64();
        uint64_t prev = last_summary_ms.load(std::memory_order_relaxed);
        if (now_ms - prev >= 3000) {
            if (last_summary_ms.compare_exchange_strong(prev, now_ms, std::memory_order_relaxed)) {
                SPDLOG_WARN(
                    "[D3D12RDT-Summary] last3s+: unknown={} left={} right={} (per-cmdlist-viewport, threshold=1.0f)",
                    n_unknown.load(std::memory_order_relaxed),
                    n_left.load(std::memory_order_relaxed),
                    n_right.load(std::memory_order_relaxed));
            }
        }
    }

    // HARD LOG CAP for per-call right-eye sample logs.
    const bool is_right_eye = view_id == 1;
    static std::atomic<uint64_t> s_logged{0};
    constexpr uint64_t MAX_LOGS = 200;

    if (is_right_eye && s_logged.load(std::memory_order_relaxed) < MAX_LOGS) {
        static std::atomic<uint64_t> s_last_log_ms{0};
        const uint64_t now_ms = static_cast<uint64_t>(GetTickCount64());
        uint64_t last = s_last_log_ms.load(std::memory_order_relaxed);
        if (now_ms - last >= 200) {  // 5 logs/sec
            if (s_last_log_ms.compare_exchange_strong(last, now_ms, std::memory_order_relaxed)) {
                const auto n = s_logged.fetch_add(1, std::memory_order_relaxed);
                SPDLOG_WARN(
                    "[D3D12RDT-R] #{} cmdlist={:p} pso={:p} vx={} vw={} rootIdx={} gpu=0x{:x}",
                    n + 1, (void*)command_list, state.current_pso,
                    state.viewport_top_left_x, state.viewport_width,
                    root_parameter_index, base_descriptor.ptr);
            }
        }
    }

    // 2026-05-17 Step C: backfill view_id on captured fog SRVs by walking
    // bindless slots in the bound descriptor range. Uses per-cmdlist viewport
    // (NOT the atomic) so view 0 actually gets tagged (the atomic-based
    // compute version always saw atomic==1 due to render-thread timing).
    if (view_id == 0 || view_id == 1) {
        if (tls_bindless_heap.stride > 0 && tls_bindless_heap.gpu_base != 0 && tls_bindless_heap.cpu_base != 0) {
            const UINT64 gpu_h = base_descriptor.ptr;
            // BOUND-CHECK: skip cross-heap GPU handles (would produce garbage cpu_start).
            // E.g., heap_2353 binding while tls_bindless_heap holds heap_16.
            if (gpu_handle_in_bindless_heap(gpu_h)) {
                const SIZE_T cpu_start = tls_bindless_heap.cpu_base + (SIZE_T)(gpu_h - tls_bindless_heap.gpu_base);
                // Range size heuristic — same as compute hook (covers typical
                // UE5 basepass tables, small slice of the bindless heap).
                constexpr SIZE_T RANGE_SLOTS = 65536;
                const SIZE_T cpu_hi = cpu_start + RANGE_SLOTS * tls_bindless_heap.stride;
                const size_t tagged = sn2_bindless_slot_map::bulk_tag_sources_in_range(cpu_start, cpu_hi, view_id);
                if (tagged > 0) {
                    sn2_bindless_slot_map::resolve_pending_tags();
                    static std::atomic<uint64_t> tag_n{0};
                    const auto idx = tag_n.fetch_add(1, std::memory_order_relaxed);
                    if (idx < 16 || (idx % 1000) == 0) {
                        SPDLOG_WARN(
                            "[D3D12RDT-Tag] log#{} view_id={} rootIdx={} tagged={} v0_slots={} v1_slots={} srv_v0={} srv_v1={}",
                            idx + 1, view_id, root_parameter_index, tagged,
                            sn2_bindless_slot_map::count_by_view(0),
                            sn2_bindless_slot_map::count_by_view(1),
                            sn2_fog_srv_map::count_by_view(0),
                            sn2_fog_srv_map::count_by_view(1));
                    }
                }

                // 2026-05-17 Step D: actual descriptor swap. When recording a
                // right-eye descriptor table, find slots whose source is a
                // view-0-tagged fog SRV (= the bug binding) and CopyDescriptors
                // Simple them to a view-1-tagged fog SRV's cpu_handle.
                static const bool swap_enabled = []() {
                    char v[8]{};
                    const auto n = GetEnvironmentVariableA("UEVR_SUBNAUTICA2_FOG_DESCRIPTOR_SWAP", v, sizeof(v));
                    return n != 0 && v[0] == '1';
                }();
                // 2026-05-17 evening DISABLED: a fresh investigation
                // (E:\Github\Subnautica 2\moddingkit\runs\SN2_FOG_INVESTIGATION_REPORT_2026-05-17.md)
                // proved the GPU bindings are ALREADY per-view correct — SN2
                // creates two distinct LUT textures (2275, 2281) and binds them
                // correctly to per-eye descriptor table bases. The byte-scan
                // here was overwriting CORRECT view-1 SRVs with view-0 SRVs,
                // causing the "sparkling shaders" symptom AND making view 1
                // render with view-0 data. The real bug is in per-view CBUFFER
                // DATA (cb0/cb2 of PSO 2993 for view 1) — fixing the swap
                // here cannot help.
                if (false && swap_enabled && view_id == 1) {
                    // 2026-05-17 Step D-safe: use UEVR-owned pool of AddRef'd
                    // fog Texture3D SRVs. The pool is populated at SRV-create
                    // time (when the resource is provably live), so source
                    // descriptors are never UAF.
                    //
                    // For each bindless slot in the bound range whose CURRENT
                    // source (per sn2_bindless_slot_map) is a fog Texture3D
                    // that's already in our pool, swap the slot to the
                    // "other" pooled SRV with matching dims+format. In a
                    // stereo-mode pool of 2 fog vols (view 0 + view 1),
                    // "other" reliably picks the view-not-currently-bound one.
                    struct SwapTarget {
                        D3D12_CPU_DESCRIPTOR_HANDLE dst;
                        D3D12_CPU_DESCRIPTOR_HANDLE src;
                    };
                    std::vector<SwapTarget> targets;

                    // Snapshot bindless slots in range first to avoid holding
                    // the bindless map mutex while we touch the pool / device.
                    struct CandidateSlot {
                        D3D12_CPU_DESCRIPTOR_HANDLE bindless_cpu;
                        ID3D12Resource*             src_resource;
                    };
                    // 2026-05-17 evening STRATEGY CHANGE: don't rely on
                    // src_view_id tagging — the per-view-fog-hook tags SRVs
                    // but in practice the tagged slots end up outside the
                    // basepass's bound range (cand_in_range stays 0 even
                    // after many frames with v1_slots populated).
                    // Instead: filter candidates by "src_resource is a known
                    // fog Texture3D" (= present in our pool). Any fog 3D
                    // texture bound at view 1's basepass is the bug binding
                    // — overwrite it with the view-0 texture that the
                    // existing midhook saved into a global atomic.
                    std::vector<CandidateSlot> cand;
                    sn2_bindless_slot_map::for_each_in_range(cpu_start, cpu_hi,
                        [](const sn2_bindless_slot_map::Entry& e, void* ctx) {
                            if (e.src_resource == nullptr) return;
                            // Filter: must be a fog Texture3D (in our pool).
                            bool is_fog = false;
                            {
                                std::scoped_lock pl{sn2_view1_fog_srv_pool::g_mutex};
                                for (const auto& pe : sn2_view1_fog_srv_pool::g_entries) {
                                    if (pe.resource == e.src_resource) {
                                        is_fog = true;
                                        break;
                                    }
                                }
                            }
                            if (!is_fog) return;
                            auto* cv = (std::vector<CandidateSlot>*)ctx;
                            cv->push_back({e.bindless_cpu_handle, e.src_resource});
                        }, &cand);

                    // 2026-05-17 evening BYTE-SCAN FALLBACK: scan basepass-bound
                    // slots and compare to pool SRVs. User reported "sparkling
                    // shaders" with looser variant — that means we were swapping
                    // non-fog slots that coincidentally matched bytes (e.g.,
                    // uninitialized/zero descriptors). Tightened:
                    //   1. Skip pool entries whose first 32 bytes are nearly-zero
                    //      (those would match many zero-init slots, false hits).
                    //   2. Reduce scan range to 64 slots (matches root sig 2497
                    //      root_param 0 = 64 SRVs).
                    //   3. Require the matched src_bytes have at least 4 distinct
                    //      non-zero bytes — discriminates real SRVs from padding.
                    if (cand.empty()) {
                        constexpr size_t SCAN_SLOTS = 64;
                        const size_t scan_end = cpu_start + SCAN_SLOTS * tls_bindless_heap.stride;
                        if (scan_end <= cpu_hi) {
                            struct PoolBytes {
                                ID3D12Resource* resource;
                                D3D12_CPU_DESCRIPTOR_HANDLE cpu;
                                uint8_t bytes[32];
                            };
                            std::vector<PoolBytes> pool_bytes;
                            {
                                std::scoped_lock pl{sn2_view1_fog_srv_pool::g_mutex};
                                pool_bytes.reserve(sn2_view1_fog_srv_pool::g_entries.size());
                                for (const auto& pe : sn2_view1_fog_srv_pool::g_entries) {
                                    PoolBytes pb{};
                                    pb.resource = pe.resource;
                                    pb.cpu = pe.cpu_handle;
                                    if (!is_readable_process_range_d3d12(pe.cpu_handle.ptr, 32)) continue;
                                    memcpy(pb.bytes, (const void*)pe.cpu_handle.ptr, 32);
                                    // Discrimination: require non-trivial bytes.
                                    int non_zero = 0;
                                    for (int j = 0; j < 32; ++j) if (pb.bytes[j] != 0) ++non_zero;
                                    if (non_zero < 8) continue;  // too many zeros = false-positive bait
                                    pool_bytes.push_back(pb);
                                }
                            }

                            for (size_t i = 0; i < SCAN_SLOTS; ++i) {
                                const SIZE_T slot_cpu = cpu_start + i * tls_bindless_heap.stride;
                                if (!is_readable_process_range_d3d12(slot_cpu, 32)) continue;
                                uint8_t slot_bytes[32];
                                memcpy(slot_bytes, (const void*)slot_cpu, 32);
                                for (const auto& pb : pool_bytes) {
                                    if (memcmp(slot_bytes, pb.bytes, 32) == 0) {
                                        cand.push_back({
                                            D3D12_CPU_DESCRIPTOR_HANDLE{slot_cpu},
                                            pb.resource
                                        });
                                        break;
                                    }
                                }
                            }

                            if (!cand.empty()) {
                                static std::atomic<uint64_t> bs_n{0};
                                const auto bn = bs_n.fetch_add(1, std::memory_order_relaxed);
                                if (bn < 8 || (bn % 600) == 0) {
                                    SPDLOG_WARN(
                                        "[D3D12-FogSwap-ByteScan] #{} found {} fog-SRV slots via byte-scan (pool_bytes_filtered={}/{})",
                                        bn + 1, cand.size(), pool_bytes.size(),
                                        sn2_view1_fog_srv_pool::size());
                                }
                            }
                        }
                    }

                    // 2026-05-17 ONE-SHOT diagnostic — fires exactly once at
                    // the first right-eye descriptor table SET that hits this
                    // code path. Dumps everything we need in a single frame
                    // (no per-call overhead, no log spam) to answer:
                    //   1. Are slots in the bound range actually Texture3Ds
                    //      with fog dims? (filter sanity)
                    //   2. Does fog_srv_map have entries matching slot src_res
                    //      pointers? (= live binding's resource IS captured)
                    //   3. Does the pool have entries matching slot dims but
                    //      different pointers? (= pointer-drift confirmed)
                    {
                        static std::atomic<bool> diag_done{false};
                        bool expected = false;
                        if (diag_done.compare_exchange_strong(expected, true)) {
                            // Compute heap CPU range from stored bindless_heap state.
                            SPDLOG_WARN("[SN2-FogSwap-Diag] BASEPASS_RIGHT_EYE_BINDING (one-shot)");
                            SPDLOG_WARN("[SN2-FogSwap-Diag] table: gpu_base=0x{:x} cpu_start=0x{:x} cpu_hi=0x{:x} stride={} range_slots={} root_param={}",
                                        (uint64_t)tls_bindless_heap.gpu_base, (uint64_t)cpu_start,
                                        (uint64_t)cpu_hi, tls_bindless_heap.stride,
                                        (cpu_hi - cpu_start) / (tls_bindless_heap.stride ? tls_bindless_heap.stride : 1),
                                        root_parameter_index);
                            SPDLOG_WARN("[SN2-FogSwap-Diag] cand_in_range={} (slots with src_view_id==0 AND src_resource!=null)", cand.size());

                            // Coverage probe: count ALL bindless slot map entries in the
                            // requested range (no view/resource filter), and find the
                            // nearest entry below + above the requested range so we can
                            // see whether the slot_map is empty here OR just lacks tags.
                            {
                                size_t in_range_total = 0;
                                SIZE_T nearest_below = 0;
                                SIZE_T nearest_above = (SIZE_T)~0ULL;
                                struct CoverageCtx {
                                    size_t* in_range_total;
                                    SIZE_T cpu_start;
                                    SIZE_T cpu_hi;
                                    SIZE_T* nearest_below;
                                    SIZE_T* nearest_above;
                                };
                                CoverageCtx cctx{&in_range_total, cpu_start, cpu_hi, &nearest_below, &nearest_above};
                                // Walk a HUGE range (full heap) — but for_each_in_range
                                // limits to entries with bindless_cpu in [lo, hi). Use
                                // a near-infinite hi to scan all.
                                sn2_bindless_slot_map::for_each_in_range(0, (SIZE_T)~0ULL,
                                    [](const sn2_bindless_slot_map::Entry& e, void* ctx) {
                                        auto* cc = (CoverageCtx*)ctx;
                                        const SIZE_T p = e.bindless_cpu_handle.ptr;
                                        if (p >= cc->cpu_start && p < cc->cpu_hi) {
                                            ++(*cc->in_range_total);
                                        } else if (p < cc->cpu_start) {
                                            if (p > *cc->nearest_below) *cc->nearest_below = p;
                                        } else if (p >= cc->cpu_hi) {
                                            if (p < *cc->nearest_above) *cc->nearest_above = p;
                                        }
                                    }, &cctx);
                                SPDLOG_WARN("[SN2-FogSwap-Diag] coverage: total bindless_slot_map size={} in_range_total={} nearest_below=0x{:x} nearest_above=0x{:x}",
                                            sn2_bindless_slot_map::size(),
                                            in_range_total,
                                            (uint64_t)nearest_below,
                                            (uint64_t)nearest_above);
                            }

                            // Dump first 8 candidate slots. To safely call
                            // GetDesc() on a possibly-stale ID3D12Resource*,
                            // we first verify the resource is in our pool
                            // (which means we AddRef'd it and it's alive).
                            // If not in pool, skip GetDesc.
                            for (size_t i = 0; i < std::min<size_t>(8, cand.size()); ++i) {
                                ID3D12Resource* r = cand[i].src_resource;
                                std::string desc_str = "(not_in_pool_skip_GetDesc)";
                                if (r != nullptr) {
                                    bool in_pool = false;
                                    {
                                        std::scoped_lock pl{sn2_view1_fog_srv_pool::g_mutex};
                                        for (const auto& pe : sn2_view1_fog_srv_pool::g_entries) {
                                            if (pe.resource == r) { in_pool = true; break; }
                                        }
                                    }
                                    if (in_pool) {
                                        D3D12_RESOURCE_DESC rd = r->GetDesc();
                                        desc_str = fmt::format("dim={}x{}x{} fmt={} (POOLED)",
                                            (int)rd.Width, (int)rd.Height,
                                            (int)rd.DepthOrArraySize, (int)rd.Format);
                                    }
                                }
                                SPDLOG_WARN("[SN2-FogSwap-Diag] cand[{}]: bindless_cpu=0x{:x} src_res=0x{:x} {}",
                                            i, (uint64_t)cand[i].bindless_cpu.ptr, (uintptr_t)r, desc_str);
                            }

                            // For each candidate's src_resource, check if it's in fog_srv_map
                            size_t in_srv_map = 0;
                            size_t in_pool = 0;
                            for (const auto& c : cand) {
                                sn2_fog_srv_map::Entry e{};
                                if (sn2_fog_srv_map::lookup_by_resource(c.src_resource, e)) ++in_srv_map;
                                std::scoped_lock pl{sn2_view1_fog_srv_pool::g_mutex};
                                for (const auto& pe : sn2_view1_fog_srv_pool::g_entries) {
                                    if (pe.resource == c.src_resource) { ++in_pool; break; }
                                }
                            }
                            SPDLOG_WARN("[SN2-FogSwap-Diag] of {} candidates: {} have src_resource in sn2_fog_srv_map, {} have src_resource in pool",
                                        cand.size(), in_srv_map, in_pool);

                            // Dump first 8 pool entries
                            {
                                std::scoped_lock pl{sn2_view1_fog_srv_pool::g_mutex};
                                SPDLOG_WARN("[SN2-FogSwap-Diag] pool: {} entries", sn2_view1_fog_srv_pool::g_entries.size());
                                for (size_t i = 0; i < std::min<size_t>(8, sn2_view1_fog_srv_pool::g_entries.size()); ++i) {
                                    const auto& pe = sn2_view1_fog_srv_pool::g_entries[i];
                                    SPDLOG_WARN("[SN2-FogSwap-Diag] pool[{}]: res=0x{:x} dim={}x{}x{} fmt={} cpu=0x{:x}",
                                                i, (uintptr_t)pe.resource, (int)pe.width, (int)pe.height,
                                                (int)pe.depth, (int)pe.format, (uint64_t)pe.cpu_handle.ptr);
                                }
                            }

                            // Dim-match count via fog_srv_map (which stores SRV
                            // info for resources we definitely captured) —
                            // safely indirect (no raw GetDesc on possibly-
                            // stale pointers).
                            size_t srv_map_size = sn2_fog_srv_map::size();
                            SPDLOG_WARN("[SN2-FogSwap-Diag] sn2_fog_srv_map: total={} v0={} v1={}",
                                        srv_map_size,
                                        sn2_fog_srv_map::count_by_view(0),
                                        sn2_fog_srv_map::count_by_view(1));
                            SPDLOG_WARN("[SN2-FogSwap-Diag] === END ONE-SHOT (analysis path below this won't dump)");
                        }
                    }

                    if (!cand.empty()) {
                        // For each candidate, ask the pool: do you have a
                        // resource OTHER than `src_resource` with matching
                        // dims+format? If yes, that's our swap source.
                        //
                        // We need dims+format. The pool stores them per-Entry,
                        // but we can't GetDesc on src_resource (may be stale).
                        // Instead: iterate the pool, find entries whose
                        // resource pointer EQUALS src_resource (= the pool
                        // entry for the bound view-0 resource), use that
                        // entry's dims/format, then ask for "find_match
                        // excluding self".
                        // 2026-05-17 evening STRATEGY UPDATE: prefer pool entry
                        // matching the most recent view-0 fog texture pointer
                        // published by the lightscat midhook
                        // (g_subnautica2_view0_lightscat). This guarantees we
                        // swap to view 0's CURRENT-frame fog texture, not a
                        // random pool entry that might be view 1's stale frame.
                        const uintptr_t view0_tex_ptr = g_subnautica2_view0_lightscat.load(std::memory_order_relaxed);
                        std::scoped_lock pool_lock{sn2_view1_fog_srv_pool::g_mutex};

                        // Find the pool entry matching view 0's tex (the "good" SRV).
                        D3D12_CPU_DESCRIPTOR_HANDLE view0_pool_cpu{0};
                        UINT64 view0_w = 0; UINT view0_h = 0; UINT16 view0_d = 0; DXGI_FORMAT view0_f = DXGI_FORMAT_UNKNOWN;
                        if (view0_tex_ptr != 0) {
                            for (const auto& pe : sn2_view1_fog_srv_pool::g_entries) {
                                if ((uintptr_t)pe.resource == view0_tex_ptr) {
                                    view0_pool_cpu = pe.cpu_handle;
                                    view0_w = pe.width; view0_h = pe.height;
                                    view0_d = pe.depth; view0_f = pe.format;
                                    break;
                                }
                            }
                        }

                        for (auto& c : cand) {
                            if (c.src_resource == (ID3D12Resource*)view0_tex_ptr) {
                                continue;  // already view 0's tex, nothing to do
                            }
                            // Find current binding's dims to confirm same shape as view 0's.
                            UINT64 ww = 0; UINT hh = 0; UINT16 dd = 0; DXGI_FORMAT ff = DXGI_FORMAT_UNKNOWN;
                            bool have_dims = false;
                            for (const auto& pe : sn2_view1_fog_srv_pool::g_entries) {
                                if (pe.resource == c.src_resource) {
                                    ww = pe.width; hh = pe.height; dd = pe.depth; ff = pe.format;
                                    have_dims = true;
                                    break;
                                }
                            }
                            if (!have_dims) continue;  // not a known fog resource

                            // Preferred: use the pool entry whose resource == view 0's
                            // currently-published texture (and dims match this binding).
                            D3D12_CPU_DESCRIPTOR_HANDLE src_cpu{0};
                            if (view0_pool_cpu.ptr != 0 &&
                                view0_w == ww && view0_h == hh &&
                                view0_d == dd && view0_f == ff)
                            {
                                src_cpu = view0_pool_cpu;
                            } else {
                                // Fallback: any pool entry with matching dims that's not the current one.
                                for (const auto& pe : sn2_view1_fog_srv_pool::g_entries) {
                                    if (pe.resource == c.src_resource) continue;
                                    if (pe.width == ww && pe.height == hh && pe.depth == dd && pe.format == ff) {
                                        src_cpu = pe.cpu_handle;
                                        break;
                                    }
                                }
                            }
                            if (src_cpu.ptr == 0) continue;  // no alternative
                            targets.push_back({c.bindless_cpu, src_cpu});
                        }
                    }

                    if (!targets.empty() && d3d12 != nullptr) {
                        ID3D12Device* dev = d3d12->get_device();
                        if (dev != nullptr) {
                            // 2026-05-17 evening: verify swap actually changes memory.
                            // Sample first target: read 32 bytes before, run swap, read 32 bytes after.
                            uint8_t before_bytes[32]{}, after_bytes[32]{}, src_bytes[32]{};
                            const bool have_diag_sample = !targets.empty()
                                && is_readable_process_range_d3d12(targets[0].dst.ptr, 32)
                                && is_readable_process_range_d3d12(targets[0].src.ptr, 32);
                            if (have_diag_sample) {
                                memcpy(before_bytes, (const void*)targets[0].dst.ptr, 32);
                                memcpy(src_bytes,    (const void*)targets[0].src.ptr, 32);
                            }
                            for (auto& t : targets) {
                                dev->CopyDescriptorsSimple(1, t.dst, t.src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                            }
                            if (have_diag_sample) {
                                memcpy(after_bytes, (const void*)targets[0].dst.ptr, 32);
                            }
                            static std::atomic<uint64_t> swap_n{0};
                            const auto sn = swap_n.fetch_add(1, std::memory_order_relaxed);
                            if (sn < 16 || (sn % 600) == 0) {
                                SPDLOG_WARN(
                                    "[D3D12-FogSwap] #{} swapped {} fog slot(s) right-eye rootIdx={} (pool={} srv_v0={} srv_v1={})",
                                    sn + 1, targets.size(), root_parameter_index,
                                    sn2_view1_fog_srv_pool::size(),
                                    sn2_fog_srv_map::count_by_view(0),
                                    sn2_fog_srv_map::count_by_view(1));
                                if (have_diag_sample) {
                                    bool changed = memcmp(before_bytes, after_bytes, 32) != 0;
                                    bool matches_src = memcmp(after_bytes, src_bytes, 32) == 0;
                                    SPDLOG_WARN(
                                        "[D3D12-FogSwap-Verify] #{} dst=0x{:x} src=0x{:x} changed={} after_matches_src={} before[0..8]={:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x} after[0..8]={:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
                                        sn + 1,
                                        (uint64_t)targets[0].dst.ptr, (uint64_t)targets[0].src.ptr,
                                        changed, matches_src,
                                        before_bytes[0], before_bytes[1], before_bytes[2], before_bytes[3],
                                        before_bytes[4], before_bytes[5], before_bytes[6], before_bytes[7],
                                        after_bytes[0], after_bytes[1], after_bytes[2], after_bytes[3],
                                        after_bytes[4], after_bytes[5], after_bytes[6], after_bytes[7]);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (original != nullptr) {
        original(command_list, root_parameter_index, base_descriptor);
    }
}

// 2026-05-17 SN2 FOG-FIX Task #45 — capture currently-bound CBV_SRV_UAV heap.
// Per-thread because cmdlist recording is single-threaded per cmdlist, and
// UE5 uses one cmdlist per worker thread.
void WINAPI D3D12Hook::set_descriptor_heaps(
    ID3D12GraphicsCommandList* command_list,
    UINT num_descriptor_heaps,
    ID3D12DescriptorHeap* const* descriptor_heaps
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = command_list != nullptr ? &(*(void***)command_list)[SET_DESCRIPTOR_HEAPS_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_command_list_diagnostic_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::set_descriptor_heaps)*>() : nullptr;

    if (descriptor_heaps != nullptr && num_descriptor_heaps > 0 && d3d12 != nullptr) {
        ID3D12Device* device = d3d12->get_device();
        if (device != nullptr) {
            for (UINT i = 0; i < num_descriptor_heaps; ++i) {
                ID3D12DescriptorHeap* heap = descriptor_heaps[i];
                if (heap == nullptr) continue;
                D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
                if (desc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) continue;
                tls_bindless_heap.cpu_base = heap->GetCPUDescriptorHandleForHeapStart().ptr;
                tls_bindless_heap.gpu_base = heap->GetGPUDescriptorHandleForHeapStart().ptr;
                tls_bindless_heap.stride   = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                tls_bindless_heap.num_descriptors = desc.NumDescriptors;

                static std::atomic<uint64_t> n{0};
                const auto idx = n.fetch_add(1, std::memory_order_relaxed);
                if (idx < 8) {
                    SPDLOG_WARN(
                        "[D3D12-BindlessHeap] tid={} heap={:p} cpu_base=0x{:x} gpu_base=0x{:x} stride={} num_desc={}",
                        (uint32_t)GetCurrentThreadId(), (void*)heap,
                        (uint64_t)tls_bindless_heap.cpu_base,
                        (uint64_t)tls_bindless_heap.gpu_base,
                        tls_bindless_heap.stride,
                        desc.NumDescriptors);
                }
                break; // only one CBV_SRV_UAV heap per call expected
            }
        }
    }

    if (original != nullptr) {
        original(command_list, num_descriptor_heaps, descriptor_heaps);
    }
}

// 2026-05-17 SN2 FOG-FIX Task #45 — retroactive view tagging.
// Called during UE5's compute pass binding. When the per-view fog atomic
// (sn2_get_current_fog_view()) is 0 or 1, walks up to MAX_WALK descriptor
// slots from the bound base, looks each cpu_handle up in our SRV / UAV maps,
// and on hit assigns view_id. UE5 pools fog Texture3D resources at world
// load, so this lets us tag those pooled resources after-the-fact based on
// which view's compute scope binds them.
void WINAPI D3D12Hook::set_compute_root_descriptor_table(
    ID3D12GraphicsCommandList* command_list,
    UINT root_parameter_index,
    D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = command_list != nullptr ? &(*(void***)command_list)[SET_COMPUTE_ROOT_DESCRIPTOR_TABLE_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_command_list_diagnostic_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::set_compute_root_descriptor_table)*>() : nullptr;

    const int view_id = sn2_get_current_fog_view();
    // Periodic logging — first 8 calls + every 4000th — so we can see
    // post-fog-hook activity (view_id should become 0 or 1).
    {
        static std::atomic<uint64_t> always_n{0};
        const auto a = always_n.fetch_add(1, std::memory_order_relaxed);
        if (a < 8 || (a % 4000) == 0) {
            SPDLOG_WARN(
                "[D3D12-CRDT] log#{} tid={} view_id={} rootIdx={} gpu=0x{:x} tls_cpu_base=0x{:x} tls_gpu_base=0x{:x} stride={} bindless_map={}",
                a + 1, (uint32_t)GetCurrentThreadId(),
                view_id, root_parameter_index, base_descriptor.ptr,
                (uint64_t)tls_bindless_heap.cpu_base,
                (uint64_t)tls_bindless_heap.gpu_base,
                tls_bindless_heap.stride,
                sn2_bindless_slot_map::size());
        }
    }
    if (view_id == 0 || view_id == 1) {
        if (tls_bindless_heap.stride > 0 && tls_bindless_heap.gpu_base != 0 && tls_bindless_heap.cpu_base != 0) {
            const UINT64 gpu_h = base_descriptor.ptr;
            // BOUND-CHECK: skip cross-heap GPU handles (would produce garbage cpu_start).
            // E.g., heap_2353 binding while tls_bindless_heap holds heap_16.
            if (gpu_handle_in_bindless_heap(gpu_h)) {
                const SIZE_T cpu_start = tls_bindless_heap.cpu_base + (SIZE_T)(gpu_h - tls_bindless_heap.gpu_base);
                // The bound table can cover up to the whole bindless heap
                // (~1M slots). Walking every slot is infeasible. Instead
                // ask the bindless slot map (small, ~440 entries) for the
                // entries that fall inside the bound range and bulk-tag
                // their source UAVs/SRVs.
                // Range size heuristic: assume the table is at most 64K
                // slots — covers fog tables comfortably while still being
                // a small slice of the heap.
                constexpr SIZE_T RANGE_SLOTS = 65536;
                const SIZE_T cpu_hi = cpu_start + RANGE_SLOTS * tls_bindless_heap.stride;
                const size_t tagged_resources =
                    sn2_bindless_slot_map::bulk_tag_sources_in_range(cpu_start, cpu_hi, view_id);

                if (tagged_resources > 0) {
                    const size_t resolved = sn2_bindless_slot_map::resolve_pending_tags();

                    static std::atomic<uint64_t> n{0};
                    const auto idx = n.fetch_add(1, std::memory_order_relaxed);
                    if (idx < 16 || (idx % 600) == 0) {
                        SPDLOG_WARN(
                            "[D3D12-CRDT-Tag] log#{} tid={} view_id={} rootIdx={} gpu=0x{:x} cpu_lo=0x{:x} cpu_hi=0x{:x} tagged_resources={} resolved={} v0_slots={} v1_slots={}",
                            idx + 1, (uint32_t)GetCurrentThreadId(),
                            view_id, root_parameter_index, gpu_h,
                            (uint64_t)cpu_start, (uint64_t)cpu_hi,
                            tagged_resources, resolved,
                            sn2_bindless_slot_map::count_by_view(0),
                            sn2_bindless_slot_map::count_by_view(1));
                    }
                }
            }
        }
    }

    if (original != nullptr) {
        original(command_list, root_parameter_index, base_descriptor);
    }
}

// 2026-05-17 SN2 fog Path A: hook SetGraphicsRootConstantBufferView to
// redirect view 1's basepass fog cbuffer binding to UEVR's per-view buffer.
// Filtered by viewport (right-eye), root_param index (3 = fog cbuffer in
// SN2's basepass root sig), and GPU_VA match (only redirect when SN2 binds
// the captured-shared fog cbuffer; pass through for unrelated cbuffer
// bindings to avoid corrupting view 0 / other render passes).
void WINAPI D3D12Hook::set_graphics_root_constant_buffer_view(
    ID3D12GraphicsCommandList* command_list,
    UINT root_parameter_index,
    D3D12_GPU_VIRTUAL_ADDRESS gpu_va
) {
    auto d3d12 = g_d3d12_hook;
    const auto slot = command_list != nullptr ? &(*(void***)command_list)[SET_GRAPHICS_ROOT_CONSTANT_BUFFER_VIEW_VTABLE_INDEX] : nullptr;
    auto* hook = d3d12 != nullptr ? d3d12->find_command_list_diagnostic_hook(slot) : nullptr;
    auto original = hook != nullptr ? hook->get_original<decltype(D3D12Hook::set_graphics_root_constant_buffer_view)*>() : nullptr;

    // 2026-05-20 Phase 5 v3 — SKY-ATMOS right-eye CB GPU-VA redirect.
    // v1 (GPU-handle swap) and v2 (per-slot CopyDescriptorsSimple) both failed
    // because L's and R's bindless tables OVERLAP in the same heap (only 2
    // descriptors apart) — they share SRV content. The remaining lever is
    // constant buffers (cb_View, cb_SkyAtmosphere, etc.) which carry per-eye
    // exposure/atmosphere/aerial-perspective uniforms. At right-eye sky-atmos
    // CB bind, replace gpu_va with LEFT's most-recent CB GPU-VA for the same
    // root_parameter_index. Gated by UEVR_SN2_SKYATMOS_CB_REDIRECT.
    if (command_list != nullptr && root_parameter_index < 16) {
        static const bool sky_cb_redirect = []() {
            const char* env = std::getenv("UEVR_SN2_SKYATMOS_CB_REDIRECT");
            return env != nullptr && env[0] != '\0' && env[0] != '0';
        }();
        if (sky_cb_redirect) {
            const auto state_sa = read_cmdlist_state(command_list);
            void* pso = state_sa.current_pso;
            if (pso != nullptr) {
                const uint32_t crc_sa = render::ShaderOverrideRegistry::get().d3d12_pso_pixel_crc32(reinterpret_cast<uintptr_t>(pso));
                const bool is_sky_atmos =
                    crc_sa == 0x009f8918u || crc_sa == 0x1c5283f9u ||
                    crc_sa == 0x2bbaec7fu || crc_sa == 0x89fcbc93u ||
                    crc_sa == 0xbe2d188bu;
                if (is_sky_atmos) {
                    static std::array<std::atomic<uint64_t>, 16> g_left_sky_cb_va{};
                    static std::atomic<uint64_t> g_cb_redirect_count{0};
                    const int view_sa = cmdlist_view_id(state_sa);
                    if (view_sa == 0 && gpu_va != 0) {
                        g_left_sky_cb_va[root_parameter_index].store(
                            static_cast<uint64_t>(gpu_va), std::memory_order_release);
                    } else if (view_sa == 1) {
                        const uint64_t lva = g_left_sky_cb_va[root_parameter_index].load(std::memory_order_acquire);
                        if (lva != 0 && lva != static_cast<uint64_t>(gpu_va)) {
                            const uint64_t before = static_cast<uint64_t>(gpu_va);
                            gpu_va = static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(lva);
                            const auto rc = g_cb_redirect_count.fetch_add(1, std::memory_order_relaxed);
                            if (rc < 32 || (rc % 200) == 0) {
                                SPDLOG_WARN("[SN2-SKYATMOS-CB-REDIRECT] right rp={} va 0x{:x} -> 0x{:x} (left snap) pso_crc=0x{:08x} (rc={})",
                                    root_parameter_index, before, lva, crc_sa, rc + 1);
                            }
                        }
                    }
                }
            }
        }
    }

    // One-shot diagnostic: confirm the hook IS firing and report env var state.
    // Without this, "no [SN2-PathA] lines" is ambiguous between
    // (a) hook not installed, (b) env var not set, (c) hook installed but
    // never receives a basepass binding (game stuck at menu).
    {
        static std::atomic<uint64_t> any_call{0};
        const auto n = any_call.fetch_add(1, std::memory_order_relaxed);
        if (n < 4 || n == 100 || n == 1000) {
            SPDLOG_WARN("[SN2-PathA][DIAG] hook fired call#{} root_param={} gpu_va=0x{:x} env={} buffer_ready={}",
                        n + 1, root_parameter_index, (uint64_t)gpu_va,
                        sn2_fog_path_a::enabled_env() ? 1 : 0,
                        sn2_fog_path_a::buffer_ready() ? 1 : 0);
        }
    }

    // 2026-05-19 EXPERIMENT: SN2 basepass cb0 force-left-eye-binding.
    // Hypothesis (from pso3069_PS.dxil analysis, see memory note
    // sn2-basepass-pso3069-root-cause): the basepass MainPS uses cb0[4..7]
    // (RelativeWorldToClip matrix) which is per-eye-specific. Under UEVR
    // -emulatestereo, both eyes' draws may or may not share the same cb0
    // binding. This experiment tests: if we force the RIGHT eye's cb0
    // binding to use the LAST cb0 GPU VA the LEFT eye bound, what happens?
    //   - If both eyes already share cb0 (same GPU VA): this is a no-op, no
    //     visual change, confirms a deeper bug
    //   - If they're different (each eye gets its own cb0): right eye will
    //     now see left-eye-correct matrices → fog UV should match left →
    //     right-eye water/sky should match left's color (depth perception
    //     will be slightly off but COLOR should match)
    // Gated by env var UEVR_SN2_FORCE_LEFT_CB0=1 so we don't break anything
    // by default. Per-cmdlist thread-local so multiple cmdlists don't fight.
    static const bool force_left_cb0 = []() {
        const char* env = std::getenv("UEVR_SN2_FORCE_LEFT_CB0");
        return env != nullptr && env[0] != '\0' && env[0] != '0';
    }();
    // 2026-05-19 CORRECTION (user-supplied): pso3069's HLSL cb0 is at
    // D3D12 root_parameter_index == 4. cb1/cb2/cb3 are at root_param 5/6/7.
    // UEVR dump CRC32 for pso3069 is 166DBA88, FNV64 bef3c05e3ad32a36.
    //
    // BUGS PREVIOUSLY HERE (now fixed):
    //   1. Was matching root_params 4,5,6,7 but storing in ONE shared last_cb0_va
    //      → could swap cb1/2/3 to a previous cb0 VA. Now gated to root_param == 4 only.
    //   2. Pairing "different VA on same PSO = left/right" is unsafe because basepass
    //      binds many materials per PSO. This is bind-time and unsafe — should move
    //      to draw-time correlation. For now we disable the SWAP path entirely; only
    //      keep the diagnostic logging (true bytes, not a fake swap).
    //
    // UEVR_SN2_FORCE_LEFT_CB0=1  → DISABLED (was unsafe). Leave at 0.
    // UEVR_SN2_CB0_DIAG=1        → log byte contents of left+right cb0 only (root_param=4)
    static const bool cb0_diag = []() {
        const char* env = std::getenv("UEVR_SN2_CB0_DIAG");
        return env != nullptr && env[0] != '\0' && env[0] != '0';
    }();
    // Ignore force_left_cb0 entirely now — too risky for the bind-time pairing.
    const bool do_swap = false;
    const bool do_diag = cb0_diag;
    const bool target_root_param = (root_parameter_index == 4);  // ONLY cb0
    if ((do_swap || do_diag) && target_root_param && gpu_va != 0 && command_list != nullptr) {
        struct PerEyeCbState {
            D3D12_GPU_VIRTUAL_ADDRESS last_cb0_va = 0;
            void* last_cb0_pso = nullptr;
        };
        thread_local PerEyeCbState s;

        void* current_pso = read_cmdlist_state(command_list).current_pso;

        // Check if current PSO matches pso3069 by examining its shader hashes.
        bool is_target_pso = false;
        if (current_pso != nullptr) {
            const uint32_t crc = render::ShaderOverrideRegistry::get().d3d12_pso_pixel_crc32(reinterpret_cast<uintptr_t>(current_pso));
            if (crc == 0x166DBA88u) {
                is_target_pso = true;
            }
        }

        static std::atomic<uint64_t> ev_count{0};
        static std::atomic<uint64_t> swap_count{0};
        const auto evc = ev_count.fetch_add(1, std::memory_order_relaxed);

        if (is_target_pso) {
            if (s.last_cb0_va != 0 && s.last_cb0_pso == current_pso && s.last_cb0_va != gpu_va) {
                // Same target PSO, different cb0 VA → right-eye partner of the left-eye binding.
                const auto orig_va = gpu_va;
                const auto left_va = s.last_cb0_va;
                const auto sc = swap_count.fetch_add(1, std::memory_order_relaxed);

                // DIAG 2026-05-19: dump byte contents from rows 4..7 (offsets 64..127) and rows
                // 30..31 (offsets 480..511) to compare left vs right cb0. If they're identical,
                // the swap is a no-op and the bug isn't in cb0 contents.
                if (sc < 8 || (sc % 300) == 0) {
                    auto dump = [](D3D12_GPU_VIRTUAL_ADDRESS va, const char* eye, uint64_t sc, uint64_t evc) {
                        uint8_t* cpu = sn2_upload_buf_map::gpu_va_to_cpu(va, 512);
                        if (cpu == nullptr) {
                            SPDLOG_WARN("[SN2-CB0-Diag] sc={} {} va=0x{:x} (NO_CPU_MAPPING)", sc + 1, eye, (uint64_t)va);
                            return;
                        }
                        const float* f = reinterpret_cast<const float*>(cpu);
                        // Row 4 (RelativeWorldToClip row 0, 16 bytes = 4 floats at offset 64)
                        SPDLOG_WARN("[SN2-CB0-Diag] sc={} {} va=0x{:x} row4=({:.3f} {:.3f} {:.3f} {:.3f}) row5=({:.3f} {:.3f} {:.3f} {:.3f}) row6=({:.3f} {:.3f} {:.3f} {:.3f}) row7=({:.3f} {:.3f} {:.3f} {:.3f})",
                            sc + 1, eye, (uint64_t)va,
                            f[16], f[17], f[18], f[19],
                            f[20], f[21], f[22], f[23],
                            f[24], f[25], f[26], f[27],
                            f[28], f[29], f[30], f[31]);
                    };
                    dump(left_va, "LEFT", sc, evc);
                    dump(orig_va, "RIGHT", sc, evc);
                }

                if (do_swap) {
                    gpu_va = left_va;
                }
                if (sc < 32 || (sc % 100) == 0) {
                    SPDLOG_WARN("[SN2-CB0-Swap-RP4] PSO3069 SWAP cb0 0x{:x} -> 0x{:x} (pso=0x{:x} evc={} sc={})",
                                (uint64_t)orig_va, (uint64_t)gpu_va, (uint64_t)current_pso, evc + 1, sc + 1);
                }
                s.last_cb0_va = 0;
                s.last_cb0_pso = nullptr;
            } else {
                s.last_cb0_va = gpu_va;
                s.last_cb0_pso = current_pso;
                if (evc < 16) {
                    SPDLOG_WARN("[SN2-CB0-Swap-RP4] tracked PSO3069 cb0 va=0x{:x} pso=0x{:x} (evc={})",
                                (uint64_t)gpu_va, (uint64_t)current_pso, evc + 1);
                }
            }
        }
    }

    // 2026-05-18: SN2 SKY ATMOS FIX v3 — SAME-PSO stride heuristic.
    // Bug: PSO 2993 (RenderSkyAtmosphereRayMarchingPS) reads cb0 (root_param 5)
    // at rows 30/31 ONLY. View 0 sees (0,0,0,1.0)/(0,0,0,1.0). View 1 sees
    // (1,1,1,1)/(1,1,1,1) — `cb0[31].y` truthy forces shader discard path →
    // black sky for right eye.
    //
    // Detection: track the GpuVa of the LAST root_param=5 binding AND the PSO*
    // bound when it happened. When a NEW root_param=5 binding comes with the
    // SAME PSO* still bound on this cmdlist AND a different GpuVa, that pair
    // IS the per-eye sky draw — redirect view 1 (second) to view 0 (first).
    //
    // The same-PSO gate eliminates the cbv-stride false positives: unrelated
    // PSOs would change the bound PSO between their cb0 bindings.
    //
    // Mode bitmask: env var UEVR_SN2_SKY_ATMOS_FIX = 1|2|4|...
    //   bit 0 (1) = redirect cb2 (root_param 2)
    //   bit 1 (2) = redirect cb1 (root_param 3)
    //   bit 2 (4) = redirect cb0 (root_param 5)  ← the actual root-cause fix
    static const int sky_mode = []() {
        const char* env = std::getenv("UEVR_SN2_SKY_ATMOS_FIX");
        if (env == nullptr || env[0] == '\0' || env[0] == '0') return 0;
        return std::atoi(env);
    }();
    if (sky_mode != 0) {
        struct PerSkyCbState {
            D3D12_GPU_VIRTUAL_ADDRESS cb0_first = 0;
            D3D12_GPU_VIRTUAL_ADDRESS cb1_first = 0;
            D3D12_GPU_VIRTUAL_ADDRESS cb2_first = 0;
            void* cb0_pso = nullptr;
            void* cb1_pso = nullptr;
            void* cb2_pso = nullptr;
            uint64_t cb0_tick = 0;
            uint64_t cb1_tick = 0;
            uint64_t cb2_tick = 0;
        };
        thread_local PerSkyCbState s;

        void* current_pso = read_cmdlist_state(command_list).current_pso;
        const uint64_t now_tick = GetTickCount64();

        const bool want_cb0 = (sky_mode & 4) && root_parameter_index == 5;
        const bool want_cb1 = (sky_mode & 2) && root_parameter_index == 3;
        const bool want_cb2 = (sky_mode & 1) && root_parameter_index == 2;
        if (want_cb0 || want_cb1 || want_cb2) {
            D3D12_GPU_VIRTUAL_ADDRESS* first = nullptr;
            void**                     pso   = nullptr;
            uint64_t*                  tick  = nullptr;
            if (want_cb0) { first = &s.cb0_first; pso = &s.cb0_pso; tick = &s.cb0_tick; }
            else if (want_cb1) { first = &s.cb1_first; pso = &s.cb1_pso; tick = &s.cb1_tick; }
            else { first = &s.cb2_first; pso = &s.cb2_pso; tick = &s.cb2_tick; }

            // 2026-05-18: heuristic redirect DISABLED entirely. The over-broad
            // same-PSO redirect was corrupting the LEFT eye (turning teal->blue).
            // The proper fix runs from sn2_sky_atmos_scan::scan_and_patch_uploads()
            // which is invoked once per Present and patches the bad pattern in
            // CPU-mapped upload memory directly.
            (void)*first; (void)*pso; (void)*tick;
            (void)now_tick;
        }
    }

    // 2026-05-18: INLINE cb0 patcher — fires on every cb0 binding, no race.
    // CORRECTION: cb0 is at root_param 2 (verified from sky_event3714_rp2_cb0.bin
    // file naming — "rp2" = root_param 2).
    //
    // PRIMARY PATH: redirect to OUR OWN buffer with correct view-0 cb0 bytes when
    // the bound GPU VA is in the engine's pre-injection pool range (0x14000000..0x15000000).
    // This sidesteps the fact that the engine's cb0 pool was created before UEVR
    // injected, so we don't have its CPU mapping.
    //
    // SECONDARY PATH (fallback): for buffers we DO have CPU mapping for, byte-patch
    // in place via try_inline_patch_cb0.
    //
    // Enabled via env: UEVR_SN2_SKY_ATMOS_INLINE_FIX=1
    static const bool sky_inline_fix = []() {
        const char* env = std::getenv("UEVR_SN2_SKY_ATMOS_INLINE_FIX");
        return env != nullptr && env[0] != '\0' && env[0] != '0';
    }();
    if (sky_inline_fix && root_parameter_index == 2 && gpu_va != 0) {
        // PRIMARY: GPU VA range redirect for the pre-injection cb0 pool,
        // GATED on current PSO matching the SkyAtmosphere fingerprint.
        // This narrows from "any cb0 binding in pool range" (too broad — broke
        // ALL rendering) to "only cb0 bindings for our target PSO".
        //
        // 2026-05-18 v3 FIX: PSOs are created pre-injection so create-time
        // fingerprint never fires. NEW: at each cb0 binding, check the current
        // PSO via GetCachedBlob() to extract its bytecode and search for the
        // entry name "RenderSkyAtmosphereRayMarchingPS". Cache result per PSO.
        void* cur_pso = read_cmdlist_state(command_list).current_pso;
        {
            static std::atomic<uint64_t> diag{0};
            const auto n = diag.fetch_add(1, std::memory_order_relaxed);
            if (n < 8 || n == 100 || n == 1000) {
                SPDLOG_WARN("[SN2-SkyAtmosInline][DIAG] cb0 bind call#{} cur_pso=0x{:x} fp_set={}",
                            n + 1, (uintptr_t)cur_pso, g_sky_atmos_pso.load(std::memory_order_relaxed) != nullptr);
            }
        }
        if (cur_pso != nullptr && g_sky_atmos_pso.load(std::memory_order_relaxed) == nullptr) {
            // Try runtime fingerprint via GetCachedBlob
            static std::unordered_set<void*> checked_psos;
            static std::mutex check_mtx;
            bool already_checked = false;
            {
                std::scoped_lock _{check_mtx};
                already_checked = checked_psos.count(cur_pso) > 0;
                if (!already_checked) checked_psos.insert(cur_pso);
            }
            if (!already_checked) {
                ID3D12PipelineState* pso_iface = (ID3D12PipelineState*)cur_pso;
                ID3DBlob* blob = nullptr;
                HRESULT hr = pso_iface->GetCachedBlob(&blob);
                size_t blob_size = (SUCCEEDED(hr) && blob != nullptr) ? blob->GetBufferSize() : 0;
                bool found = false;
                if (SUCCEEDED(hr) && blob != nullptr) {
                    const uint8_t* data = (const uint8_t*)blob->GetBufferPointer();
                    const size_t size = blob->GetBufferSize();
                    // Search for the DXBC embedded SHA1 hash of pso2993_PS.dxbc
                    // (RenderSkyAtmosphereRayMarchingPS). The hash is at offset 4
                    // of the original .dxbc file, 16 bytes. UE5 strips entry names
                    // (-Qstrip_reflect -Qstrip_debug) but the DXBC hash IS preserved
                    // since it's part of the bytecode container.
                    static constexpr uint8_t kDxbcHash[16] = {
                        0xAB, 0x64, 0x61, 0xFB, 0x6F, 0x0F, 0xFD, 0x40,
                        0xF1, 0xF0, 0x4E, 0x17, 0xEB, 0xF0, 0xC5, 0x1A
                    };
                    if (data != nullptr && size >= sizeof(kDxbcHash)) {
                        for (size_t i = 0; i + sizeof(kDxbcHash) <= size; ++i) {
                            if (data[i] == kDxbcHash[0] && memcmp(data + i, kDxbcHash, sizeof(kDxbcHash)) == 0) {
                                g_sky_atmos_pso.store(cur_pso, std::memory_order_relaxed);
                                SPDLOG_WARN("[SN2-SkyAtmosFix-RT] RUNTIME FINGERPRINTED PSO=0x{:x} blob_size={} hash_offset={}",
                                            (uintptr_t)cur_pso, size, i);
                                found = true;
                                break;
                            }
                        }
                    }
                    blob->Release();
                }
                {
                    static std::atomic<uint64_t> nblobs{0};
                    const auto n = nblobs.fetch_add(1, std::memory_order_relaxed);
                    if (n < 32 || (n % 50) == 0) {
                        SPDLOG_WARN("[SN2-SkyAtmosFix-RT][DIAG] GetCachedBlob pso#{} pso=0x{:x} hr=0x{:08x} blob_size={} found={}",
                                    n + 1, (uintptr_t)cur_pso, (uint32_t)hr, blob_size, found ? 1 : 0);
                    }
                }
            }
        }
        void* sky_pso = g_sky_atmos_pso.load(std::memory_order_relaxed);
        if (sky_pso != nullptr && cur_pso == sky_pso &&
            gpu_va >= 0x14000000ULL && gpu_va < 0x15000000ULL) {
            D3D12_GPU_VIRTUAL_ADDRESS our_va = sn2_fixed_cb0::get_or_create(d3d12 != nullptr ? d3d12->get_device() : nullptr);
            if (our_va != 0) {
                static std::atomic<uint64_t> rd{0};
                const auto n = rd.fetch_add(1, std::memory_order_relaxed);
                if (n < 8 || (n % 600) == 0) {
                    SPDLOG_WARN("[SN2-SkyAtmosRedirect] cb0 redirect#{} pso=0x{:x} orig=0x{:x} -> our=0x{:x}",
                                n + 1, (uintptr_t)cur_pso, (uint64_t)gpu_va, (uint64_t)our_va);
                }
                gpu_va = our_va;
            }
        } else {
            // SECONDARY: in-place byte patch for tracked buffers
            uint8_t* cpu = sn2_upload_buf_map::gpu_va_to_cpu(gpu_va, 512);
            if (cpu != nullptr) {
                const bool patched = sn2_upload_buf_map::try_inline_patch_cb0(cpu);
                if (patched) {
                    static std::atomic<uint64_t> hits{0};
                    const auto n = hits.fetch_add(1, std::memory_order_relaxed);
                    if (n < 8 || (n % 200) == 0) {
                        SPDLOG_WARN("[SN2-SkyAtmosInline] PATCHED cb0 at gpu_va=0x{:x} hit#{}",
                                    (uint64_t)gpu_va, n + 1);
                    }
                }
            }
        }
    }

    if (sn2_fog_path_a::enabled_env()) {
        if (root_parameter_index == 3) {
            // Capture the shared fog cbuffer GPU_VA on the first sighting
            // (any caller; viewport-based eye detection doesn't work because
            // SN2 binds cbuffers BEFORE setting the viewport on each cmdlist).
            sn2_fog_path_a::note_left_eye_binding(gpu_va);

            // Eagerly create UEVR's buffer the first time we see a fog
            // cbuffer binding. This makes sn2_fog_path_a::buffer_ready()
            // flip true so FixV5's subsequent write_buffer calls succeed
            // BEFORE the first armed redirect fires.
            if (!sn2_fog_path_a::buffer_ready() && d3d12 != nullptr) {
                sn2_fog_path_a::ensure_buffer(d3d12->get_device());
            }

            const auto shared_va = sn2_fog_path_a::shared_gpu_va();
            if (shared_va != 0 && gpu_va == shared_va && d3d12 != nullptr) {
                // Redirect ONLY if FixV5 hook has armed us (= we just
                // finished view 1's CPU-side fog setup, so the next binding
                // of the shared fog cbuffer is view 1's basepass).
                if (sn2_fog_path_a::try_consume_redirect()) {
                    ID3D12Device* device = d3d12->get_device();
                    if (sn2_fog_path_a::ensure_buffer(device)) {
                        const auto our_va = sn2_fog_path_a::buffer_gpu_va();
                        if (our_va != 0) {
                            sn2_fog_path_a::mark_redirected();
                            static std::atomic<uint64_t> log_n{0};
                            const auto n = log_n.fetch_add(1, std::memory_order_relaxed);
                            if (n < 16 || (n % 600) == 0) {
                                SPDLOG_WARN(
                                    "[SN2-PathA] redirect armed-binding root_param=3: shared=0x{:x} -> ours=0x{:x} (count={})",
                                    (uint64_t)gpu_va, (uint64_t)our_va, n + 1);
                            }
                            gpu_va = our_va;
                        }
                    }
                }
            }
        }
    }

    update_cmdlist_root_cbv(command_list, root_parameter_index, gpu_va);

    if (original != nullptr) {
        original(command_list, root_parameter_index, gpu_va);
    }
}

thread_local int32_t g_resize_buffers_depth = 0;

HRESULT WINAPI D3D12Hook::resize_buffers(IDXGISwapChain3* swap_chain, UINT buffer_count, UINT width, UINT height, DXGI_FORMAT new_format, UINT swap_chain_flags) {
    std::scoped_lock _{g_framework->get_hook_monitor_mutex()};

    spdlog::info("D3D12 resize buffers called");
    spdlog::info(" Parameters: buffer_count {} width {} height {} new_format {} swap_chain_flags {}", buffer_count, width, height, (uint32_t)new_format, swap_chain_flags);

    auto d3d12 = g_d3d12_hook;
    //auto& hook = d3d12->m_resize_buffers_hook;
    //auto resize_buffers_fn = hook->get_original<decltype(D3D12Hook::resize_buffers)*>();

    HWND swapchain_wnd{nullptr};
    swap_chain->GetHwnd(&swapchain_wnd);

    auto resize_buffers_fn = d3d12->m_swapchain_hook->get_method<decltype(D3D12Hook::resize_buffers)*>(13);

    if (WindowFilter::get().is_filtered(swapchain_wnd)) {
        return resize_buffers_fn(swap_chain, buffer_count, width, height, new_format, swap_chain_flags);
    }

    d3d12->m_display_width = width;
    d3d12->m_display_height = height;

    if (g_resize_buffers_depth > 0) {
        auto original_bytes = utility::get_original_bytes(Address{resize_buffers_fn});

        if (original_bytes) {
            ProtectionOverride protection_override{resize_buffers_fn, original_bytes->size(), PAGE_EXECUTE_READWRITE};

            memcpy(resize_buffers_fn, original_bytes->data(), original_bytes->size());

            spdlog::info("Resize buffers fixed");
        }

        if ((uintptr_t)resize_buffers_fn != (uintptr_t)&D3D12Hook::resize_buffers && g_resize_buffers_depth == 1) {
            spdlog::info("Attempting to call the real resize buffers function");

            ++g_resize_buffers_depth;
            const auto result = resize_buffers_fn(swap_chain, buffer_count, width, height, new_format, swap_chain_flags);
            --g_resize_buffers_depth;

            if (result != S_OK) {
                spdlog::error("Resize buffers failed: {:x}", result);
            }

            return result;
        } else {
            spdlog::info("Just returning S_OK");
            return S_OK;
        }
    }

    if (d3d12->m_on_resize_buffers) {
        d3d12->m_on_resize_buffers(*d3d12, width, height);
    }

    ++g_resize_buffers_depth;

    const auto result = resize_buffers_fn(swap_chain, buffer_count, width, height, new_format, swap_chain_flags);

    if (result != S_OK) {
        spdlog::error("Resize buffers failed: {:x}", result);
    }

    --g_resize_buffers_depth;

    return result;
}

thread_local int32_t g_resize_target_depth = 0;

HRESULT WINAPI D3D12Hook::resize_target(IDXGISwapChain3* swap_chain, const DXGI_MODE_DESC* new_target_parameters) {
    std::scoped_lock _{g_framework->get_hook_monitor_mutex()};

    spdlog::info("D3D12 resize target called");
    spdlog::info(" Parameters: new_target_parameters {:x}", (uintptr_t)new_target_parameters);

    auto d3d12 = g_d3d12_hook;
    //auto resize_target_fn = d3d12->m_resize_target_hook->get_original<decltype(D3D12Hook::resize_target)*>();

    HWND swapchain_wnd{nullptr};
    swap_chain->GetHwnd(&swapchain_wnd);

    auto resize_target_fn = d3d12->m_swapchain_hook->get_method<decltype(D3D12Hook::resize_target)*>(14);

    if (WindowFilter::get().is_filtered(swapchain_wnd)) {
        return resize_target_fn(swap_chain, new_target_parameters);
    }

    d3d12->m_render_width = new_target_parameters->Width;
    d3d12->m_render_height = new_target_parameters->Height;

    // Restore the original code to the resize_buffers function.
    if (g_resize_target_depth > 0) {
        auto original_bytes = utility::get_original_bytes(Address{resize_target_fn});

        if (original_bytes) {
            ProtectionOverride protection_override{resize_target_fn, original_bytes->size(), PAGE_EXECUTE_READWRITE};

            memcpy(resize_target_fn, original_bytes->data(), original_bytes->size());

            spdlog::info("Resize target fixed");
        }

        if ((uintptr_t)resize_target_fn != (uintptr_t)&D3D12Hook::resize_target && g_resize_target_depth == 1) {
            spdlog::info("Attempting to call the real resize target function");

            ++g_resize_target_depth;
            const auto result = resize_target_fn(swap_chain, new_target_parameters);
            --g_resize_target_depth;

            if (result != S_OK) {
                spdlog::error("Resize target failed: {:x}", result);
            }

            return result;
        } else {
            spdlog::info("Just returning S_OK");
            return S_OK;
        }
    }

    if (d3d12->m_on_resize_target) {
        d3d12->m_on_resize_target(*d3d12, new_target_parameters->Width, new_target_parameters->Height);
    }

    ++g_resize_target_depth;

    const auto result = resize_target_fn(swap_chain, new_target_parameters);
    
    if (result != S_OK) {
        spdlog::error("Resize target failed: {:x}", result);
    }

    --g_resize_target_depth;

    return result;
}

/*HRESULT WINAPI D3D12Hook::create_swap_chain(IDXGIFactory4* factory, IUnknown* device, HWND hwnd, const DXGI_SWAP_CHAIN_DESC* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* p_fullscreen_desc, IDXGIOutput* p_restrict_to_output, IDXGISwapChain** swap_chain)
{
    spdlog::info("D3D12 create swapchain called");

    auto d3d12 = g_d3d12_hook;

    d3d12->m_command_queue = (ID3D12CommandQueue*)device;
    
    if (d3d12->m_on_create_swap_chain) {
        d3d12->m_on_create_swap_chain(*d3d12);
    }

    auto create_swap_chain_fn = d3d12->m_create_swap_chain_hook->get_original<decltype(D3D12Hook::create_swap_chain)>();

    return create_swap_chain_fn(factory, device, hwnd, desc, p_fullscreen_desc, p_restrict_to_output, swap_chain);
}*/
