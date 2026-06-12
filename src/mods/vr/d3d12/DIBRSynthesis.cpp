#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include "render/ShaderCompiler.hpp"
#include "utility/Logging.hpp"

#include "DIBRShadersEmbedded.hpp"
#include "DIBRSynthesis.hpp"

namespace vrmod {
namespace {
using Microsoft::WRL::ComPtr;

std::string load_shader_source(const char* filename, std::string (*embedded)()) {
    // Iteration hook: UEVR_DIBR_SHADER_DIR=<dir> compiles <dir>\<filename>
    // instead of the embedded copy, so shader edits don't need a rebuild.
    if (const char* dir = std::getenv("UEVR_DIBR_SHADER_DIR"); dir != nullptr && dir[0] != '\0') {
        const auto path = std::filesystem::path{dir} / filename;
        std::ifstream f{path, std::ios::binary};
        if (f) {
            std::string src{std::istreambuf_iterator<char>{f}, std::istreambuf_iterator<char>{}};
            if (!src.empty()) {
                SPDLOG_INFO("[DIBR] using on-disk shader override: {}", path.string());
                return src;
            }
        }
        SPDLOG_WARN("[DIBR] UEVR_DIBR_SHADER_DIR set but {} is unreadable; using embedded source", path.string());
    }
    return embedded();
}

uint64_t fnv1a64(const void* data, size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

std::filesystem::path bytecode_cache_path(const char* name, uint64_t source_hash) {
    const char* base = std::getenv("LOCALAPPDATA");
    if (base == nullptr || base[0] == '\0') {
        return {};
    }
    return std::filesystem::path{base} / "UEVR" / "dibr_shader_cache" / fmt::format("{}.{:016x}.cs.bin", name, source_hash);
}

bool try_compile_backend(const std::filesystem::path& hlsl_path, const char* name, const char* profile,
    render::ShaderCompilerBackend backend, std::vector<uint8_t>& out_bytecode)
{
    render::ShaderCompileRequest req{};
    req.source_path = hlsl_path;
    req.entry_point = "CSMain";
    req.profile = profile;
    req.preferred_backend = backend;
    req.warnings_as_errors = false;
    req.strict_mode = false;
    req.debug_info = false;

    const auto t0 = std::chrono::steady_clock::now();
    const auto res = render::compile_shader_file(req);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

    if (!res.succeeded || res.bytecode.empty()) {
        SPDLOG_WARN("[DIBR] {}: {} {} compile failed after {} ms: {}", name, res.compiler, profile, ms, res.error);
        return false;
    }

    out_bytecode = res.bytecode;
    SPDLOG_INFO("[DIBR] {}: compiled {} bytes with {} {} in {} ms", name, out_bytecode.size(), res.compiler, profile, ms);
    return true;
}

// These kernels are expensive to optimize: DXC cs_6_0 takes ~1-2 minutes on
// the raymarch variant and FXC cs_5_0 is far worse (10+ minutes) - which is
// why compilation runs on a worker thread and the resulting bytecode is
// cached on disk keyed by a source hash (only the first run per source
// revision pays the cost).
bool compile_kernel(const std::string& source, const char* name, std::vector<uint8_t>& out_bytecode) {
    const auto hash = fnv1a64(source.data(), source.size());
    const auto cache = bytecode_cache_path(name, hash);

    if (!cache.empty()) {
        try {
            std::ifstream f{cache, std::ios::binary};
            if (f) {
                std::vector<uint8_t> bytes{std::istreambuf_iterator<char>{f}, std::istreambuf_iterator<char>{}};
                if (bytes.size() > 64) {
                    out_bytecode = std::move(bytes);
                    SPDLOG_INFO("[DIBR] {}: loaded {} bytes of cached bytecode", name, out_bytecode.size());
                    return true;
                }
            }
        } catch (...) {
        }
    }

    // render::compile_shader_file consumes a file path, so stage the
    // (embedded or override) source through the temp dir like the SN2 fog
    // reprojection pass does.
    std::filesystem::path hlsl_path{};
    try {
        wchar_t tmp[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tmp);
        hlsl_path = std::filesystem::path{tmp} / L"uevr_dibr" / name;
        std::filesystem::create_directories(hlsl_path.parent_path());
        std::ofstream f{hlsl_path, std::ios::binary | std::ios::trunc};
        f.write(source.data(), static_cast<std::streamsize>(source.size()));
    } catch (...) {
        SPDLOG_ERROR("[DIBR] {}: failed to stage shader source for compilation", name);
        return false;
    }

    if (!try_compile_backend(hlsl_path, name, "cs_6_0", render::ShaderCompilerBackend::Dxc, out_bytecode) &&
        !try_compile_backend(hlsl_path, name, "cs_5_0", render::ShaderCompilerBackend::Fxc, out_bytecode)) {
        SPDLOG_ERROR("[DIBR] {}: all compile backends failed", name);
        return false;
    }

    if (!cache.empty()) {
        try {
            std::filesystem::create_directories(cache.parent_path());
            std::ofstream f{cache, std::ios::binary | std::ios::trunc};
            f.write(reinterpret_cast<const char*>(out_bytecode.data()), static_cast<std::streamsize>(out_bytecode.size()));
        } catch (...) {
            SPDLOG_WARN("[DIBR] {}: failed to write bytecode cache", name);
        }
    }

    return true;
}

// Guards against C++/HLSL layout drift across 256 fields: the compiled
// shader's own reflection of the StereoParams cbuffer must agree with the
// C++ struct, otherwise every parameter after the drift point silently reads
// its neighbor's value.
bool validate_cbuffer_layout(const std::vector<uint8_t>& bytecode, const char* name) {
    const auto inspection = render::inspect_shader_bytecode(bytecode.data(), bytecode.size(), false);
    if (!inspection.ok || !inspection.reflection.ok) {
        SPDLOG_WARN("[DIBR] {}: shader reflection unavailable; skipping cbuffer validation", name);
        return true;
    }

    for (const auto& cb : inspection.reflection.constant_buffers) {
        if (cb.name != "StereoParams") {
            continue;
        }

        // Field count + the last field's offset pin the layout exactly and are
        // backend-independent (FXC reports the cbuffer size padded to 16 bytes,
        // DXC's DXIL reflection may not - so total size is only sanity-ranged).
        // 256 scalars (incl. reproj/scatter/overscan/temporal flags, out dims
        // and the CPU-resolved pre_* constants) + three float4x4, which
        // reflection counts as ONE variable each.
        constexpr uint32_t expected_fields = 256u + 3u;
        constexpr uint32_t expected_last_offset = offsetof(DIBRStereoParams, pre_edge_comp_inv);
        constexpr uint32_t expected_size_min = sizeof(DIBRStereoParams);
        constexpr uint32_t expected_size_max = (sizeof(DIBRStereoParams) + 15u) & ~15u;

        const uint32_t last_offset = cb.variables.empty() ? 0u : cb.variables.back().start_offset;
        if (cb.variables.size() != expected_fields || last_offset != expected_last_offset ||
            cb.size < expected_size_min || cb.size > expected_size_max) {
            SPDLOG_ERROR("[DIBR] {}: StereoParams layout drift (fields {} vs {}, last offset {} vs {}, size {}); refusing to enable",
                name, cb.variables.size(), expected_fields, last_offset, expected_last_offset, cb.size);
            return false;
        }
        return true;
    }

    SPDLOG_WARN("[DIBR] {}: StereoParams cbuffer not found in reflection; skipping validation", name);
    return true;
}

void transition(ID3D12GraphicsCommandList* cmd_list, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    if (before == after) {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    cmd_list->ResourceBarrier(1, &barrier);
}

// Per-pass GPU timing rides the same env the D3D12 hook instrumentation uses.
bool gpu_timing_enabled() {
    static const bool value = []() {
        const char* env = std::getenv("UEVR_ENABLE_D3D12_GPU_TIMESTAMPS");
        return env != nullptr && env[0] != '\0' && env[0] != '0';
    }();
    return value;
}

// Thread-group edge for every DIBR kernel ([numthreads(DIBR_TG, DIBR_TG, 1)]).
// UEVR_DIBR_TG=8|16|32 overrides for occupancy A/B runs; the define is baked
// into each kernel's source (so the bytecode cache keys on it) and the C++
// dispatch math uses the same value.
uint32_t dibr_thread_group() {
    static const uint32_t value = []() -> uint32_t {
        const char* env = std::getenv("UEVR_DIBR_TG");
        const int tg = (env != nullptr && env[0] != '\0') ? std::atoi(env) : 16;
        if (tg == 8 || tg == 16 || tg == 32) {
            return static_cast<uint32_t>(tg);
        }
        SPDLOG_WARN("[DIBR] UEVR_DIBR_TG={} unsupported (8/16/32); using 16", tg);
        return 16u;
    }();
    return value;
}
} // namespace

DIBRSynthesis::~DIBRSynthesis() {
    join_worker();
}

void DIBRSynthesis::join_worker() {
    if (m_worker.joinable()) {
        m_worker.join();
    }
    m_worker_finished.store(false, std::memory_order_release);
}

bool DIBRSynthesis::ensure(ID3D12Device* device) {
    switch (m_state.load(std::memory_order_acquire)) {
    case State::Ready:
        return true;
    case State::Building:
    case State::Failed:
        return false;
    case State::NotStarted:
        break;
    }

    if (device == nullptr) {
        return false;
    }

    // reset() never blocks on the worker, so a previous, abandoned build may
    // still be draining. Reap it once it finishes; stay idle until then.
    if (m_worker.joinable()) {
        if (!m_worker_finished.load(std::memory_order_acquire)) {
            SPDLOG_INFO_EVERY_N_SEC(5, "[DIBR] waiting for an abandoned kernel build to drain before restarting");
            return false;
        }
        join_worker();
    }

    // Kernel compilation takes seconds-to-minutes on a cache miss, so the
    // whole build happens on a worker thread; ensure() keeps returning false
    // (and synthesize() stays a no-op) until it lands. All the D3D12 calls in
    // the build are free-threaded device methods.
    m_state.store(State::Building, std::memory_order_release);
    const auto generation = m_generation.load(std::memory_order_acquire);
    m_worker = std::thread{[this, dev = Microsoft::WRL::ComPtr<ID3D12Device>{device}, generation]() mutable {
        build_async(std::move(dev), generation);
    }};
    return false;
}

void DIBRSynthesis::build_async(Microsoft::WRL::ComPtr<ID3D12Device> device, uint64_t generation) {
    const auto t0 = std::chrono::steady_clock::now();

    // Build into locals so an abandoning reset() can discard everything
    // without ever racing the live members.
    DeviceObjects objs{};
    const bool built =
        create_root_signature(device.Get(), objs) && create_psos(device.Get(), objs) && create_rings(device.Get(), objs);

    {
        std::scoped_lock _{m_commit_mtx};
        if (generation != m_generation.load(std::memory_order_acquire)) {
            SPDLOG_INFO("[DIBR] kernel build abandoned by reset(); discarding results");
        } else if (!built) {
            m_state.store(State::Failed, std::memory_order_release);
            SPDLOG_ERROR("[DIBR] pipeline build failed; synthetic stereo disabled until reset()");
        } else {
            m_objs = std::move(objs);
            const auto secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0).count();
            m_state.store(State::Ready, std::memory_order_release);
            SPDLOG_INFO("[DIBR] synthesis pipeline ready in {} s (inverse / yoro / raymarch kernels)", secs);
        }
    }

    m_worker_finished.store(true, std::memory_order_release);
}

bool DIBRSynthesis::create_root_signature(ID3D12Device* device, DeviceObjects& objs) {
    // One table with SRV t0..t3 (offset 0), UAV u0..u5 (offset 4) and SRV t4
    // (offset 10 - appended as its own range so the earlier offsets never
    // move), a root CBV at b0, and static samplers s0 (linear clamp) / s1
    // (point clamp).
    D3D12_DESCRIPTOR_RANGE ranges[3]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 4; // t0 color, t1 depth, t2 prepared depth, t3 history color
    ranges[0].BaseShaderRegister = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 6; // u0 output, u1/u2 scatter key+color, u3/u4 history color+key, u5 prep
    ranges[1].BaseShaderRegister = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = 4;
    ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[2].NumDescriptors = 1; // t4 SceneVelocity snapshot
    ranges[2].BaseShaderRegister = 4;
    ranges[2].OffsetInDescriptorsFromTableStart = 10;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 3;
    params[0].DescriptorTable.pDescriptorRanges = ranges;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 0;
    params[1].Descriptor.RegisterSpace = 0;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].MaxAnisotropy = 1;
    samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[0].ShaderRegister = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    samplers[1] = samplers[0];
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[1].ShaderRegister = 1;

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 2;
    desc.pParameters = params;
    desc.NumStaticSamplers = 2;
    desc.pStaticSamplers = samplers;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> blob{};
    ComPtr<ID3DBlob> error{};
    if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error))) {
        const char* msg = error != nullptr ? static_cast<const char*>(error->GetBufferPointer()) : "<unknown>";
        SPDLOG_ERROR("[DIBR] root signature serialize failed: {}", msg);
        return false;
    }
    if (FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&objs.root_sig)))) {
        SPDLOG_ERROR("[DIBR] CreateRootSignature failed");
        return false;
    }
    return true;
}

bool DIBRSynthesis::create_psos(ID3D12Device* device, DeviceObjects& objs) {
    struct Kernel {
        const char* name;  // cache + staging + log identity
        const char* file;  // on-disk override filename (UEVR_DIBR_SHADER_DIR)
        std::string (*embedded)();
        const char* prefix; // optional prepended defines (specializations)
        ComPtr<ID3D12PipelineState>* pso;
    };
    const Kernel kernels[] = {
        {"dibr_inverse.hlsl", "dibr_inverse.hlsl", &dibr_shaders::dibr_inverse_source, nullptr, &objs.pso_inverse},
        // _lean variants: optional output features compiled out; picked by
        // synthesize() whenever params_allow_lean() holds.
        {"dibr_inverse_lean.hlsl", "dibr_inverse.hlsl", &dibr_shaders::dibr_inverse_source, "#define DIBR_LEAN 1\n", &objs.pso_inverse_lean},
        {"dibr_yoro.hlsl", "dibr_yoro.hlsl", &dibr_shaders::dibr_yoro_source, nullptr, &objs.pso_yoro},
        {"dibr_yoro_lean.hlsl", "dibr_yoro.hlsl", &dibr_shaders::dibr_yoro_source, "#define DIBR_LEAN 1\n", &objs.pso_yoro_lean},
        // Same source, gather machinery compiled out - the scatter compose PSO.
        {"dibr_yoro_scatter.hlsl", "dibr_yoro.hlsl", &dibr_shaders::dibr_yoro_source, "#define SCATTER_COMPOSE 1\n", &objs.pso_yoro_scatter},
        {"dibr_yoro_scatter_lean.hlsl", "dibr_yoro.hlsl", &dibr_shaders::dibr_yoro_source, "#define SCATTER_COMPOSE 1\n#define DIBR_LEAN 1\n", &objs.pso_yoro_scatter_lean},
        {"dibr_raymarch.hlsl", "dibr_raymarch.hlsl", &dibr_shaders::dibr_raymarch_source, nullptr, &objs.pso_raymarch},
        {"dibr_raymarch_lean.hlsl", "dibr_raymarch.hlsl", &dibr_shaders::dibr_raymarch_source, "#define DIBR_LEAN 1\n", &objs.pso_raymarch_lean},
        {"dibr_scatter_depth.hlsl", "dibr_scatter_depth.hlsl", &dibr_shaders::dibr_scatter_depth_source, nullptr, &objs.pso_scatter_depth},
        {"dibr_scatter_color.hlsl", "dibr_scatter_color.hlsl", &dibr_shaders::dibr_scatter_color_source, nullptr, &objs.pso_scatter_color},
        {"dibr_scatter_fill.hlsl", "dibr_scatter_fill.hlsl", &dibr_shaders::dibr_scatter_fill_source, nullptr, &objs.pso_scatter_fill},
        {"dibr_afw_stash.hlsl", "dibr_afw_stash.hlsl", &dibr_shaders::dibr_afw_stash_source, nullptr, &objs.pso_afw_stash},
        // Depth conditioning prepass, one PSO per consuming kernel family.
        {"dibr_depth_prep_inverse.hlsl", "dibr_depth_prep.hlsl", &dibr_shaders::dibr_depth_prep_source, "#define PREP_MODE 0\n", &objs.pso_prep_inverse},
        {"dibr_depth_prep_raymarch.hlsl", "dibr_depth_prep.hlsl", &dibr_shaders::dibr_depth_prep_source, "#define PREP_MODE 1\n", &objs.pso_prep_raymarch},
        {"dibr_depth_prep_yoro.hlsl", "dibr_depth_prep.hlsl", &dibr_shaders::dibr_depth_prep_source, "#define PREP_MODE 2\n", &objs.pso_prep_yoro},
    };

    if (dibr_thread_group() != 16u) {
        SPDLOG_INFO("[DIBR] thread group override: {0}x{0} (UEVR_DIBR_TG)", dibr_thread_group());
    }

    for (const auto& k : kernels) {
        auto source = load_shader_source(k.file, k.embedded);
        source.insert(0, fmt::format("#define DIBR_TG {}\n", dibr_thread_group()));
        if (k.prefix != nullptr) {
            source.insert(0, k.prefix);
        }
        std::vector<uint8_t> bytecode{};
        if (!compile_kernel(source, k.name, bytecode)) {
            return false;
        }
        if (!validate_cbuffer_layout(bytecode, k.name)) {
            return false;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc{};
        pso_desc.pRootSignature = objs.root_sig.Get();
        pso_desc.CS = D3D12_SHADER_BYTECODE{bytecode.data(), bytecode.size()};
        if (FAILED(device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(k.pso->ReleaseAndGetAddressOf())))) {
            SPDLOG_ERROR("[DIBR] {}: CreateComputePipelineState failed", k.name);
            return false;
        }
    }
    return true;
}

bool DIBRSynthesis::create_rings(ID3D12Device* device, DeviceObjects& objs) {
    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC buf_desc{};
    buf_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buf_desc.Width = static_cast<uint64_t>(kRing) * kCbSlotSize;
    buf_desc.Height = 1;
    buf_desc.DepthOrArraySize = 1;
    buf_desc.MipLevels = 1;
    buf_desc.SampleDesc.Count = 1;
    buf_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &buf_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&objs.cbuffer)))) {
        SPDLOG_ERROR("[DIBR] constant buffer ring creation failed");
        return false;
    }

    const D3D12_RANGE no_read{0, 0};
    if (FAILED(objs.cbuffer->Map(0, &no_read, reinterpret_cast<void**>(&objs.cbuffer_ptr))) || objs.cbuffer_ptr == nullptr) {
        SPDLOG_ERROR("[DIBR] constant buffer ring map failed");
        return false;
    }

    // CPU-only descriptors for ClearUnorderedAccessView* (the API requires a
    // CPU handle from a NON-shader-visible heap alongside the GPU handle).
    D3D12_DESCRIPTOR_HEAP_DESC clear_heap_desc{};
    clear_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    clear_heap_desc.NumDescriptors = kRing * 2; // scatter key + color per ring slot
    if (FAILED(device->CreateDescriptorHeap(&clear_heap_desc, IID_PPV_ARGS(&objs.clear_heap)))) {
        SPDLOG_ERROR("[DIBR] clear descriptor heap creation failed");
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.NumDescriptors = kRing * kDescriptorsPerSlot;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&objs.heap)))) {
        SPDLOG_ERROR("[DIBR] descriptor heap creation failed");
        return false;
    }

    objs.descriptor_stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return true;
}

bool DIBRSynthesis::ensure_output(ID3D12Device* device, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return false;
    }
    if (m_output != nullptr && m_output_width == width && m_output_height == height) {
        return true;
    }

    m_output.Reset();

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = m_output_format;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    if (FAILED(device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_output)))) {
        SPDLOG_ERROR("[DIBR] output texture creation failed ({}x{} fmt {})", width, height, static_cast<int>(m_output_format));
        return false;
    }

    m_output->SetName(L"DIBR Packed Stereo Output");
    m_output_width = width;
    m_output_height = height;
    ++m_resource_generation;
    SPDLOG_INFO("[DIBR] packed output texture {}x{} (fmt {})", width, height, static_cast<int>(m_output_format));
    return true;
}

void DIBRSynthesis::set_output_format(DXGI_FORMAT format) {
    if (format == m_output_format) {
        return;
    }
    m_output_format = format;
    m_output.Reset();
    m_output_width = 0;
    m_output_height = 0;
}

bool DIBRSynthesis::ensure_scatter(ID3D12Device* device, uint32_t width, uint32_t height) {
    if (m_scatter_key[0] != nullptr && m_scatter_width == width && m_scatter_height == height) {
        return true;
    }

    for (auto& r : m_scatter_key) {
        r.Reset();
    }
    for (auto& r : m_scatter_color) {
        r.Reset();
    }
    m_scatter_history_valid = false;
    m_scatter_index = 0;

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    for (int i = 0; i < 2; ++i) {
        desc.Format = DXGI_FORMAT_R32_UINT;
        if (FAILED(device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_scatter_key[i])))) {
            SPDLOG_ERROR_ONCE("[DIBR] failed to create {}x{} scatter key texture", width, height);
            return false;
        }
        m_scatter_key[i]->SetName(i == 0 ? L"DIBR Scatter Key A" : L"DIBR Scatter Key B");

        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        if (FAILED(device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_scatter_color[i])))) {
            SPDLOG_ERROR_ONCE("[DIBR] failed to create {}x{} scatter color texture", width, height);
            m_scatter_key[i].Reset();
            return false;
        }
        m_scatter_color[i]->SetName(i == 0 ? L"DIBR Scatter Color A" : L"DIBR Scatter Color B");
    }

    m_scatter_width = width;
    m_scatter_height = height;
    ++m_resource_generation;
    SPDLOG_INFO("[DIBR] scatter buffers {}x{} (ping-pong pair)", width, height);
    return true;
}

bool DIBRSynthesis::ensure_afw_history(ID3D12Device* device, uint32_t width, uint32_t height) {
    // Sized like the scatter buffers (out == source space).
    if (m_afw_history_key != nullptr) {
        const auto d = m_afw_history_key->GetDesc();
        if (d.Width == width && d.Height == height) {
            return true;
        }
    }

    m_afw_history_key.Reset();
    m_afw_history_color.Reset();
    m_afw_history_valid = false;

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    desc.Format = DXGI_FORMAT_R32_UINT;
    if (FAILED(device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_afw_history_key)))) {
        SPDLOG_ERROR_ONCE("[DIBR] failed to create {}x{} AFW history key texture", width, height);
        return false;
    }
    m_afw_history_key->SetName(L"DIBR AFW History Key");

    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (FAILED(device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_afw_history_color)))) {
        SPDLOG_ERROR_ONCE("[DIBR] failed to create {}x{} AFW history color texture", width, height);
        m_afw_history_key.Reset();
        return false;
    }
    m_afw_history_color->SetName(L"DIBR AFW History Color");

    ++m_resource_generation;
    SPDLOG_INFO("[DIBR] AFW real-render history buffers {}x{}", width, height);
    return true;
}

bool DIBRSynthesis::ensure_prep(ID3D12Device* device, uint32_t width, uint32_t height) {
    if (m_prep != nullptr && m_prep_width == width && m_prep_height == height) {
        return true;
    }

    m_prep.Reset();
    m_prep_is_srv = false;

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    // RGBA16F: guaranteed typed-UAV-store format on all D3D12 hardware (RG16F
    // is an optional cap). Only .xy carry data (depth, gradient).
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    if (FAILED(device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_prep)))) {
        SPDLOG_ERROR_ONCE("[DIBR] failed to create {}x{} prepared-depth texture", width, height);
        return false;
    }
    m_prep->SetName(L"DIBR Prepared Depth");
    m_prep_width = width;
    m_prep_height = height;
    ++m_resource_generation;
    SPDLOG_INFO("[DIBR] prepared-depth texture {}x{}", width, height);
    return true;
}

namespace {
// CPU twins of Common.ush's velocity decode. Encoded channels are UNORM16
// already normalized to [0,1] here.
float vel_decode_linear(float e) {
    constexpr float inv_div = 1.0f / (0.499f * 0.5f);
    return e * inv_div - (32767.0f / 65535.0f) * inv_div;
}
float vel_decode_gamma_post(float lin) {
    return lin * std::fabs(lin) * 0.5f; // VELOCITY_ENCODE_GAMMA: sign-preserving square
}
float vel_decode_depth(float ez, float ew) {
    const uint32_t hi = static_cast<uint32_t>(std::lround(ez * 65535.0f)) << 16;
    const uint32_t lo = static_cast<uint32_t>(std::lround(ew * 65535.0f)) & 0xFFFEu;
    const uint32_t bits = hi | lo;
    float depth;
    std::memcpy(&depth, &bits, sizeof(depth));
    return depth;
}
// Column-major 4x4 (glm memcpy layout) times (x, y, z, 1).
void vel_mat_mul(const float m[16], float x, float y, float z, float out[4]) {
    for (int row = 0; row < 4; ++row) {
        out[row] = m[0 * 4 + row] * x + m[1 * 4 + row] * y + m[2 * 4 + row] * z + m[3 * 4 + row];
    }
}
} // namespace

void DIBRSynthesis::record_velocity_calibration(ID3D12GraphicsCommandList* cmd_list, uint32_t slot, const DIBRStereoParams& params) {
    auto& meta = m_vel_calib_slots[slot];
    meta.valid = false;

    const bool eligible = !m_vel_calibrated && m_velocity_tex != nullptr && params.temporal_enabled > 1.5f;
    if (!eligible) {
        return;
    }

    // The snapshot is copied at the depth-signature bind, AFTER the frame's
    // velocity pass, so it holds THIS call's frame pair. The texels live in
    // the RENDERED (source) eye's screen space, but under AFW the previous
    // frame's camera was the OTHER eye - so a static texel's predicted clip
    // displacement composes the same-frame source -> target-eye hop with the
    // target eye's current -> previous-frame reprojection:
    //   M = reproj_target_to_prev * reproj_source_to_{target eye}
    // mode_param0 < 0.5 means left is the reference (right is synthesized).
    const float* src_to_target = (params.mode_param0 < 0.5f)
        ? params.reproj_source_to_right
        : params.reproj_source_to_left;
    float pair_matrix[16]{};
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += params.reproj_target_to_prev[k * 4 + r] * src_to_target[c * 4 + k];
            }
            pair_matrix[c * 4 + r] = sum;
        }
    }

    if (m_vel_calib_readback == nullptr) {
        ComPtr<ID3D12Device> device{};
        if (FAILED(cmd_list->GetDevice(IID_PPV_ARGS(&device))) || device == nullptr) {
            return;
        }
        D3D12_HEAP_PROPERTIES heap_props{};
        heap_props.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buf_desc{};
        buf_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buf_desc.Width = static_cast<uint64_t>(kRing) * kVelSlotBytes;
        buf_desc.Height = 1;
        buf_desc.DepthOrArraySize = 1;
        buf_desc.MipLevels = 1;
        buf_desc.SampleDesc.Count = 1;
        buf_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &buf_desc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_vel_calib_readback)))) {
            return;
        }
        const D3D12_RANGE full_read{0, static_cast<SIZE_T>(buf_desc.Width)};
        if (FAILED(m_vel_calib_readback->Map(0, &full_read, reinterpret_cast<void**>(&m_vel_calib_mapped))) ||
            m_vel_calib_mapped == nullptr) {
            m_vel_calib_readback.Reset();
            m_vel_calib_mapped = nullptr;
            return;
        }
        m_vel_calib_last_log = std::chrono::steady_clock::now();
    }

    const auto vdesc = m_velocity_tex->GetDesc();
    if (vdesc.Format != DXGI_FORMAT_R16G16B16A16_UNORM) {
        // Two-channel platforms carry no packed depth: no displacement
        // prediction is possible, so the version-ordered default stands.
        SPDLOG_INFO_ONCE("[DIBR][velocity] calibration skipped: 2-channel velocity (no packed depth); using version-default decode");
        m_vel_calibrated = true;
        return;
    }

    const uint32_t vel_w = static_cast<uint32_t>(vdesc.Width);
    const uint32_t vel_h = vdesc.Height;
    // The lone view occupies the left half when the target is the double-wide
    // family texture; otherwise it covers the full extent.
    const uint32_t view_w = (vel_w >= params.source_width * 2) ? vel_w / 2 : vel_w;
    if (view_w < kVelStripTexels * 4 || vel_h < 8) {
        return;
    }

    std::memcpy(meta.matrix, pair_matrix, sizeof(meta.matrix));
    meta.vel_width = vel_w;
    meta.vel_height = vel_h;
    meta.view_w = view_w;
    meta.strip_x = view_w / 2 - kVelStripTexels / 2;
    meta.strip_y[0] = vel_h / 3;
    meta.strip_y[1] = (vel_h * 2) / 3;

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_velocity_tex;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    cmd_list->ResourceBarrier(1, &barrier);

    for (uint32_t s = 0; s < kVelStrips; ++s) {
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = m_velocity_tex;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = m_vel_calib_readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Offset = static_cast<uint64_t>(slot) * kVelSlotBytes + s * kVelRowBytes;
        dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
        dst.PlacedFootprint.Footprint.Width = kVelStripTexels;
        dst.PlacedFootprint.Footprint.Height = 1;
        dst.PlacedFootprint.Footprint.Depth = 1;
        dst.PlacedFootprint.Footprint.RowPitch = kVelRowBytes;
        D3D12_BOX box{};
        box.left = meta.strip_x;
        box.right = meta.strip_x + kVelStripTexels;
        box.top = meta.strip_y[s];
        box.bottom = meta.strip_y[s] + 1;
        box.back = 1;
        cmd_list->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
    }

    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    cmd_list->ResourceBarrier(1, &barrier);

    meta.valid = true;
}

void DIBRSynthesis::drain_velocity_calibration(uint32_t slot) {
    auto& meta = m_vel_calib_slots[slot];
    if (!meta.valid || m_vel_calib_mapped == nullptr || m_vel_calibrated) {
        meta.valid = false;
        return;
    }
    meta.valid = false;

    const uint32_t lone_view_w = (meta.view_w != 0) ? meta.view_w : meta.vel_width;

    for (uint32_t s = 0; s < kVelStrips; ++s) {
        const auto* texels = reinterpret_cast<const uint16_t*>(
            m_vel_calib_mapped + static_cast<size_t>(slot) * kVelSlotBytes + s * kVelRowBytes);
        for (uint32_t i = 0; i < kVelStripTexels; ++i) {
            const float ex = texels[i * 4 + 0] / 65535.0f;
            const float ey = texels[i * 4 + 1] / 65535.0f;
            const float ez = texels[i * 4 + 2] / 65535.0f;
            const float ew = texels[i * 4 + 3] / 65535.0f;
            ++m_vel_total_texels;
            if (texels[i * 4 + 0] == 0 && texels[i * 4 + 1] == 0) {
                ++m_vel_zero_texels;
                continue; // zero sentinel: surface wrote no velocity
            }

            // Predicted screen displacement of this surface across the frame
            // pair, from the velocity's own packed previous depth.
            const float depth = vel_decode_depth(ez, ew);
            if (!(depth > 0.0f) || depth > 1.0f) {
                continue;
            }
            const uint32_t px = meta.strip_x + i;
            const uint32_t py = meta.strip_y[s];
            const float ndc_x = ((px + 0.5f) / static_cast<float>(lone_view_w)) * 2.0f - 1.0f;
            const float ndc_y = 1.0f - ((py + 0.5f) / static_cast<float>(meta.vel_height)) * 2.0f;
            float prev[4]{};
            vel_mat_mul(meta.matrix, ndc_x, ndc_y, depth, prev);
            const float w = (std::fabs(prev[3]) > 1e-6f) ? prev[3] : 1e-6f;
            const float pred_x = ndc_x - prev[0] / w;
            const float pred_y = ndc_y - prev[1] / w;
            const float pred_len = std::sqrt(pred_x * pred_x + pred_y * pred_y);
            if (pred_len < 1.5e-3f || pred_len > 0.25f) {
                continue; // needs head motion, and excludes teleports
            }

            const float lin_x = vel_decode_linear(ex);
            const float lin_y = vel_decode_linear(ey);
            const float gam_x = vel_decode_gamma_post(lin_x);
            const float gam_y = vel_decode_gamma_post(lin_y);
            const float err_lin = std::sqrt((lin_x - pred_x) * (lin_x - pred_x) + (lin_y - pred_y) * (lin_y - pred_y)) / pred_len;
            const float err_gam = std::sqrt((gam_x - pred_x) * (gam_x - pred_x) + (gam_y - pred_y) * (gam_y - pred_y)) / pred_len;
            if (m_vel_err_linear.size() < 4096) {
                m_vel_err_linear.push_back(err_lin);
                m_vel_err_gamma.push_back(err_gam);
            }
        }
    }

    velocity_calibration_verdict();
}

void DIBRSynthesis::velocity_calibration_verdict() {
    constexpr size_t kNeeded = 128;
    if (m_vel_err_gamma.size() < kNeeded) {
        const auto now = std::chrono::steady_clock::now();
        if (now - m_vel_calib_last_log >= std::chrono::seconds(10)) {
            m_vel_calib_last_log = now;
            SPDLOG_INFO("[DIBR][velocity] calibrating: {} qualifying samples of {} needed (requires head motion + animated/velocity-writing pixels; zero-sentinel fraction {:.2f})",
                m_vel_err_gamma.size(), kNeeded,
                m_vel_total_texels > 0 ? static_cast<double>(m_vel_zero_texels) / static_cast<double>(m_vel_total_texels) : 0.0);
        }
        return;
    }

    auto median = [](std::vector<float> v) {
        const size_t mid = v.size() / 2;
        std::nth_element(v.begin(), v.begin() + mid, v.end());
        return v[mid];
    };
    const float med_gamma = median(m_vel_err_gamma);
    const float med_linear = median(m_vel_err_linear);
    m_vel_calibrated = true;

    if ((std::min)(med_gamma, med_linear) > 0.75f) {
        // Neither flavor tracks the camera prediction: the sampled pixels are
        // object-motion dominated, or the buffer is a frame stale. Fall back
        // to the version-ordered default (gamma for the UE5 family).
        SPDLOG_WARN("[DIBR][velocity] calibration INCONCLUSIVE (median rel-err gamma={:.3f} linear={:.3f}, n={}); defaulting to gamma decode",
            med_gamma, med_linear, m_vel_err_gamma.size());
    } else {
        SPDLOG_INFO("[DIBR][velocity] calibration verdict: flavor={} (median rel-err gamma={:.3f} linear={:.3f}, n={}, zero-sentinel fraction {:.2f})",
            med_gamma <= med_linear ? "gamma" : "linear", med_gamma, med_linear, m_vel_err_gamma.size(),
            m_vel_total_texels > 0 ? static_cast<double>(m_vel_zero_texels) / static_cast<double>(m_vel_total_texels) : 0.0);
    }
    m_vel_err_gamma.clear();
    m_vel_err_linear.clear();
}

bool DIBRSynthesis::ensure_gpu_timing(ID3D12Device* device) {
    if (m_ts_queue == nullptr) {
        return false;
    }
    if (m_ts_frequency == 0 &&
        (FAILED(m_ts_queue->GetTimestampFrequency(&m_ts_frequency)) || m_ts_frequency == 0)) {
        m_ts_frequency = 0;
        return false;
    }
    if (m_ts_heap != nullptr && m_ts_readback != nullptr && m_ts_mapped != nullptr) {
        return true;
    }

    D3D12_QUERY_HEAP_DESC query_desc{};
    query_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    query_desc.Count = kRing * kTsQueriesPerSlot;
    if (FAILED(device->CreateQueryHeap(&query_desc, IID_PPV_ARGS(&m_ts_heap)))) {
        SPDLOG_ERROR_ONCE("[DIBR] GPU timing query heap creation failed");
        return false;
    }

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buf_desc{};
    buf_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buf_desc.Width = static_cast<uint64_t>(kRing) * kTsQueriesPerSlot * sizeof(uint64_t);
    buf_desc.Height = 1;
    buf_desc.DepthOrArraySize = 1;
    buf_desc.MipLevels = 1;
    buf_desc.SampleDesc.Count = 1;
    buf_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &buf_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_ts_readback)))) {
        SPDLOG_ERROR_ONCE("[DIBR] GPU timing readback buffer creation failed");
        m_ts_heap.Reset();
        return false;
    }
    const D3D12_RANGE full_read{0, static_cast<SIZE_T>(buf_desc.Width)};
    if (FAILED(m_ts_readback->Map(0, &full_read, reinterpret_cast<void**>(&m_ts_mapped))) || m_ts_mapped == nullptr) {
        SPDLOG_ERROR_ONCE("[DIBR] GPU timing readback map failed");
        m_ts_heap.Reset();
        m_ts_readback.Reset();
        m_ts_mapped = nullptr;
        return false;
    }
    std::memset(m_ts_mapped, 0, static_cast<size_t>(buf_desc.Width));
    m_ts_slot_count.fill(0);
    m_ts_last_log = std::chrono::steady_clock::now();
    SPDLOG_INFO("[DIBR] per-pass GPU timing enabled (UEVR_ENABLE_D3D12_GPU_TIMESTAMPS)");
    return true;
}

void DIBRSynthesis::drain_gpu_timing_slot(uint32_t slot) {
    const uint32_t count = m_ts_slot_count[slot];
    if (count == 0 || m_ts_mapped == nullptr || m_ts_frequency == 0) {
        return;
    }
    for (uint32_t i = 0; i < count && i < kTsMaxPasses; ++i) {
        const size_t base = static_cast<size_t>(slot) * kTsQueriesPerSlot + i * 2;
        const uint64_t begin = m_ts_mapped[base];
        const uint64_t end = m_ts_mapped[base + 1];
        m_ts_mapped[base] = 0;
        m_ts_mapped[base + 1] = 0;
        if (begin == 0 || end <= begin) {
            continue;
        }
        const uint8_t pass = m_ts_slot_seq[slot][i];
        if (pass < kTsMaxPasses) {
            m_ts_sum_ms[pass] += static_cast<double>(end - begin) * 1000.0 / static_cast<double>(m_ts_frequency);
            ++m_ts_count[pass];
        }
    }
    m_ts_slot_count[slot] = 0;
}

void DIBRSynthesis::log_gpu_timing() {
    const auto now = std::chrono::steady_clock::now();
    if (m_ts_last_log.time_since_epoch().count() == 0) {
        m_ts_last_log = now;
        return;
    }
    if (now - m_ts_last_log < std::chrono::seconds(5)) {
        return;
    }
    m_ts_last_log = now;

    static constexpr const char* kPassNames[kTsMaxPasses] = {
        "prep", "clear", "scatter_depth", "scatter_color", "fill", "compose", "stash", "?"};
    std::string line{};
    double total_avg = 0.0;
    uint32_t frames = 0;
    for (uint32_t i = 0; i < kTsMaxPasses; ++i) {
        if (m_ts_count[i] == 0) {
            continue;
        }
        const double avg = m_ts_sum_ms[i] / static_cast<double>(m_ts_count[i]);
        line += fmt::format(" {}={:.3f}ms", kPassNames[i], avg);
        total_avg += avg;
        frames = (std::max)(frames, m_ts_count[i]);
        m_ts_sum_ms[i] = 0.0;
        m_ts_count[i] = 0;
    }
    if (!line.empty()) {
        SPDLOG_INFO("[DIBR][gpu] per-pass avg over ~{} frames:{} total={:.3f}ms", frames, line, total_avg);
    }
}

bool DIBRSynthesis::params_allow_lean(const DIBRStereoParams& p) {
    // Must stay in lockstep with the DIBR_LEAN stubs in the kernels: lean is
    // only picked when every compiled-out feature is at its pass-through
    // default, so the two PSOs produce identical output.
    return p.debug_view_mode < 0.5f &&
        p.edge_compression == 0.0f &&
        p.image_filter_sharpen_strength <= 0.0f &&
        p.image_filter_aa_strength <= 0.0f &&
        p.image_filter_deband_strength <= 0.0f &&
        p.image_filter_deband_grain <= 0.0f &&
        p.output_geometry_poly_strength <= 0.0f &&
        p.output_distortion_grid < 0.5f &&
        p.cursor_overlay_strength <= 0.0f &&
        p.comfort_nose_strength <= 0.0f &&
        p.output_matte_strength <= 0.0f &&
        p.output_composition_mode < 0.5f &&
        p.output_frame_marker_mode < 0.5f &&
        p.output_alignment_marker_mode < 0.5f &&
        p.output_saturation == 1.0f &&
        p.output_vignette_strength <= 0.0f &&
        p.output_hmd_vignette <= 0.0f &&
        p.output_geometry_keystone_tilt == 0.0f &&
        p.output_geometry_zoom == 1.0f &&
        p.output_geometry_fov == 0.0f &&
        p.output_geometry_scale_x == 1.0f &&
        p.output_geometry_scale_y == 1.0f &&
        p.output_geometry_offset_x == 0.0f &&
        p.output_geometry_offset_y == 0.0f &&
        p.output_geometry_barrel == 0.0f &&
        p.output_geometry_radial_k2 == 0.0f &&
        p.output_geometry_radial_k3 == 0.0f &&
        p.output_geometry_left_offset_x == 0.0f &&
        p.output_geometry_left_offset_y == 0.0f &&
        p.output_geometry_right_offset_x == 0.0f &&
        p.output_geometry_right_offset_y == 0.0f &&
        p.output_geometry_left_rotation_deg == 0.0f &&
        p.output_geometry_right_rotation_deg == 0.0f &&
        p.output_geometry_ipd_offset == 0.0f &&
        p.output_geometry_axis_swap < 0.5f &&
        p.output_headset_profile < 0.5f;
}

ID3D12Resource* DIBRSynthesis::synthesize(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmd_list,
    Mode mode,
    ID3D12Resource* color, D3D12_RESOURCE_STATES color_state,
    ID3D12Resource* depth, D3D12_RESOURCE_STATES depth_state,
    DIBRStereoParams params)
{
    if (!ensure(device) || cmd_list == nullptr || color == nullptr || depth == nullptr) {
        return nullptr;
    }

    // Scatter requires the true-matrix reprojection inputs; without them it
    // degrades to the plain gather kernel - EXCEPT in AFW mode: the gather's
    // screen-space disparity model is not geometrically exact, and with the
    // reference eye flipping per frame an inexact warp oscillates violently.
    // A mirrored mono frame (the caller's null path) is strictly better for
    // the transient frames where the reprojection inputs are unavailable
    // (e.g. eye offsets not yet populated, runtime pose stalls).
    if (mode == Mode::YoroScatter && params.reproj_enabled < 0.5f) {
        if (m_afw_mode) {
            SPDLOG_WARNING_EVERY_N_SEC(5, "[DIBR] AFW: reprojection inputs unavailable; mirroring instead of gather fallback");
            return nullptr;
        }
        mode = Mode::Yoro;
    }

    // When every optional output feature is at its pass-through default, the
    // lean PSO (features compiled out -> smaller code, lower register
    // pressure) is exactly equivalent to the full kernel.
    const bool lean = params_allow_lean(params);

    ID3D12PipelineState* pso = nullptr;
    ID3D12PipelineState* prep_pso = nullptr;
    switch (mode) {
    case Mode::InverseWarp:
        pso = (lean && m_objs.pso_inverse_lean != nullptr) ? m_objs.pso_inverse_lean.Get() : m_objs.pso_inverse.Get();
        prep_pso = m_objs.pso_prep_inverse.Get();
        break;
    case Mode::Yoro:
        pso = (lean && m_objs.pso_yoro_lean != nullptr) ? m_objs.pso_yoro_lean.Get() : m_objs.pso_yoro.Get();
        prep_pso = m_objs.pso_prep_yoro.Get();
        break;
    case Mode::Raymarch:
        pso = (lean && m_objs.pso_raymarch_lean != nullptr) ? m_objs.pso_raymarch_lean.Get() : m_objs.pso_raymarch.Get();
        prep_pso = m_objs.pso_prep_raymarch.Get();
        break;
    case Mode::YoroScatter:
        // Final compose runs through the SCATTER_COMPOSE=1 specialization of
        // the yoro kernel (gather machinery compiled out for occupancy). The
        // scatter chain consumes raw device depth, so no prepass is needed.
        pso = (lean && m_objs.pso_yoro_scatter_lean != nullptr) ? m_objs.pso_yoro_scatter_lean.Get() : m_objs.pso_yoro_scatter.Get();
        if (pso == nullptr || m_objs.pso_scatter_depth == nullptr ||
            m_objs.pso_scatter_color == nullptr || m_objs.pso_scatter_fill == nullptr ||
            (m_afw_mode && m_objs.pso_afw_stash == nullptr)) {
            pso = nullptr;
        } else {
            params.scatter_compose = 1.0f;
            // Temporal hole fill needs a valid previous frame (the real-render
            // stash in AFW mode, the filled scatter pair otherwise).
            if (m_afw_mode ? !m_afw_history_valid : !m_scatter_history_valid) {
                params.temporal_enabled = 0.0f;
            }
        }
        break;
    }
    if (pso == nullptr || (prep_pso == nullptr && mode != Mode::YoroScatter)) {
        return nullptr;
    }

    const auto color_desc = color->GetDesc();
    const auto depth_desc = depth->GetDesc();
    params.source_width = static_cast<uint32_t>(color_desc.Width);
    params.source_height = color_desc.Height;
    params.frame_index = m_frame_index;

    // CPU-resolved constants: dispatch-uniform values every kernel used to
    // recompute per pixel (effective convergence lerp, reciprocal source
    // dims, the edge-compression atan/tan normalization).
    {
        const float zb = std::clamp(params.zpd_balance, 0.0f, 1.0f);
        params.pre_effective_convergence = params.convergence + (0.5f - params.convergence) * zb;
        params.pre_inv_src_width = 1.0f / static_cast<float>((std::max)(params.source_width, 1u));
        params.pre_inv_src_height = 1.0f / static_cast<float>((std::max)(params.source_height, 1u));
        if (params.edge_compression > 0.0f) {
            params.pre_edge_comp_inv = 1.0f / std::atan(params.edge_compression * 3.0f);
        } else if (params.edge_compression < 0.0f) {
            params.pre_edge_comp_inv = 1.0f / std::tan(-params.edge_compression * 1.2f);
        } else {
            params.pre_edge_comp_inv = 1.0f;
        }
    }

    // Conditioned-depth prepass target for the gather kernels (source-sized).
    if (prep_pso != nullptr && !ensure_prep(device, params.source_width, params.source_height)) {
        return nullptr;
    }

    // Output (submit) eye size: callers set it when the overscan-grown render
    // target makes the source wider than the true-FOV output; everything else
    // gets the historical out == src behavior.
    if (params.out_width == 0 || params.out_height == 0 ||
        params.out_width > params.source_width || params.out_height > params.source_height) {
        params.out_width = params.source_width;
        params.out_height = params.source_height;
    }

    // Synthesis resolution: the scatter chain (clear/depth/color/fill) and its
    // ping-pong history run in this space; the compose pass bilinearly upscales
    // the scatter colour to the full out dims. UEVR_DIBR_SYNTH_SCALE < 1.0
    // trades a softer synthesized eye for ~1/scale^2 cheaper scatter passes.
    // 1.0 (default) keeps synth == out, which is bit-identical to the historical
    // full-res path. AFW (mode 6) is pinned to full res: its compose-side
    // history blend reads history at OUT-space coords, so a half-res history
    // would misregister.
    static const float synth_scale = []() {
        const char* v = std::getenv("UEVR_DIBR_SYNTH_SCALE");
        if (v == nullptr || v[0] == '\0') {
            return 1.0f;
        }
        float s = static_cast<float>(std::atof(v));
        if (s < 0.25f) s = 0.25f;
        if (s > 1.0f) s = 1.0f;
        return s;
    }();
    const bool synth_full_res = (synth_scale >= 0.999f) || m_afw_mode;
    params.synth_width = synth_full_res
        ? params.out_width
        : static_cast<uint32_t>(params.out_width * synth_scale + 0.5f);
    params.synth_height = synth_full_res
        ? params.out_height
        : static_cast<uint32_t>(params.out_height * synth_scale + 0.5f);
    if (params.synth_width < 16u) params.synth_width = 16u;
    if (params.synth_height < 16u) params.synth_height = 16u;

    // Scatter buffers (and history) live in SYNTHESIS space; the packed output
    // texture below stays at the full out dims.
    if (!ensure_scatter(device, params.synth_width, params.synth_height)) {
        return nullptr;
    }

    if (m_afw_mode && mode == Mode::YoroScatter &&
        !ensure_afw_history(device, params.synth_width, params.synth_height)) {
        return nullptr;
    }

    uint32_t out_w{}, out_h{};
    packed_output_dimensions(params, out_w, out_h);
    if (!ensure_output(device, out_w, out_h)) {
        return nullptr;
    }

    const uint32_t slot = m_ring_index;
    m_ring_index = (m_ring_index + 1) % kRing;
    std::memcpy(m_objs.cbuffer_ptr + static_cast<size_t>(slot) * kCbSlotSize, &params, sizeof(params));

    // Per-pass GPU timing: read back what this slot recorded kRing frames ago
    // (its GPU work is long retired) before the queries are overwritten below.
    const bool ts_on = gpu_timing_enabled() && ensure_gpu_timing(device);
    uint32_t ts_pass_count = 0;
    if (ts_on) {
        drain_gpu_timing_slot(slot);
        log_gpu_timing();
    }
    // Velocity discovery Layer 3: consume what this slot sampled kRing
    // frames ago before its readback region is overwritten below.
    drain_velocity_calibration(slot);
    const auto ts_begin = [&]() {
        if (ts_on && ts_pass_count < kTsMaxPasses) {
            cmd_list->EndQuery(m_ts_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * kTsQueriesPerSlot + ts_pass_count * 2);
        }
    };
    const auto ts_end = [&](TsPass pass) {
        if (ts_on && ts_pass_count < kTsMaxPasses) {
            cmd_list->EndQuery(m_ts_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * kTsQueriesPerSlot + ts_pass_count * 2 + 1);
            m_ts_slot_seq[slot][ts_pass_count] = static_cast<uint8_t>(pass);
            ++ts_pass_count;
        }
    };

    const auto cpu_base = m_objs.heap->GetCPUDescriptorHandleForHeapStart();
    const size_t slot_offset = static_cast<size_t>(slot) * kDescriptorsPerSlot * m_objs.descriptor_stride;

    const uint32_t cur = m_scatter_index;
    const uint32_t prev = cur ^ 1u;

    // History for the temporal hole fill: the previous frame's filled scatter
    // pair, or in AFW mode the real-render stash (which the stash pass then
    // overwrites with THIS frame's source after the fill has read it). The
    // color is additionally exposed as an SRV (t3): the fill and compose
    // passes only READ it, and the SRV path gives the compose's history blend
    // a filtered single-sample fetch. The resource is transitioned to a
    // shader-readable state around those passes below.
    const bool afw_history = m_afw_mode && mode == Mode::YoroScatter && m_afw_history_color != nullptr;
    ID3D12Resource* history_color = afw_history ? m_afw_history_color.Get() : m_scatter_color[prev].Get();
    ID3D12Resource* history_key = afw_history ? m_afw_history_key.Get() : m_scatter_key[prev].Get();

    // Descriptor-churn guard: hash every input that shapes this slot's
    // descriptor writes (the resource set only changes on mode flips,
    // resolution changes or recreation - m_resource_generation covers
    // pointer reuse), and skip the dozen Create*View calls when identical to
    // what the slot already holds.
    uint64_t desc_hash = 0xcbf29ce484222325ull;
    const auto mix = [&desc_hash](uint64_t v) {
        desc_hash ^= v;
        desc_hash *= 0x100000001b3ull;
    };
    mix(reinterpret_cast<uintptr_t>(color));
    mix(static_cast<uint64_t>(color_srv_format(color_desc.Format)));
    mix(static_cast<uint64_t>(color_desc.Width));
    mix(reinterpret_cast<uintptr_t>(depth));
    mix(static_cast<uint64_t>(depth_srv_format(depth_desc.Format)));
    mix(reinterpret_cast<uintptr_t>((prep_pso != nullptr) ? m_prep.Get() : nullptr));
    mix(reinterpret_cast<uintptr_t>(m_output.Get()));
    mix(static_cast<uint64_t>(m_output_format));
    mix(reinterpret_cast<uintptr_t>(m_scatter_key[cur].Get()));
    mix(reinterpret_cast<uintptr_t>(m_scatter_color[cur].Get()));
    mix(reinterpret_cast<uintptr_t>(history_color));
    mix(reinterpret_cast<uintptr_t>(history_key));
    mix(reinterpret_cast<uintptr_t>(m_velocity_tex));
    mix(static_cast<uint64_t>(mode == Mode::YoroScatter));
    mix(m_resource_generation);

    if (m_slot_desc_hash[slot] != desc_hash) {
        m_slot_desc_hash[slot] = desc_hash;

        D3D12_SHADER_RESOURCE_VIEW_DESC color_srv{};
        color_srv.Format = color_srv_format(color_desc.Format);
        color_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        color_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        color_srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(color, &color_srv, D3D12_CPU_DESCRIPTOR_HANDLE{cpu_base.ptr + slot_offset});

        D3D12_SHADER_RESOURCE_VIEW_DESC depth_srv{};
        depth_srv.Format = depth_srv_format(depth_desc.Format);
        depth_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        depth_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        depth_srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(depth, &depth_srv, D3D12_CPU_DESCRIPTOR_HANDLE{cpu_base.ptr + slot_offset + m_objs.descriptor_stride});

        // t2: conditioned-depth prepass output (null descriptor when this mode
        // has no prepass - the scatter compose never reads it).
        D3D12_SHADER_RESOURCE_VIEW_DESC prep_srv{};
        prep_srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        prep_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        prep_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        prep_srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView((prep_pso != nullptr) ? m_prep.Get() : nullptr, &prep_srv,
            D3D12_CPU_DESCRIPTOR_HANDLE{cpu_base.ptr + slot_offset + 2 * m_objs.descriptor_stride});

        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format = m_output_format;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(m_output.Get(), nullptr, &uav, D3D12_CPU_DESCRIPTOR_HANDLE{cpu_base.ptr + slot_offset + 4 * m_objs.descriptor_stride});

        D3D12_UNORDERED_ACCESS_VIEW_DESC key_uav{};
        key_uav.Format = DXGI_FORMAT_R32_UINT;
        key_uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(m_scatter_key[cur].Get(), nullptr, &key_uav, D3D12_CPU_DESCRIPTOR_HANDLE{cpu_base.ptr + slot_offset + 5 * m_objs.descriptor_stride});

        D3D12_UNORDERED_ACCESS_VIEW_DESC scol_uav{};
        scol_uav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        scol_uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(m_scatter_color[cur].Get(), nullptr, &scol_uav, D3D12_CPU_DESCRIPTOR_HANDLE{cpu_base.ptr + slot_offset + 6 * m_objs.descriptor_stride});

        device->CreateUnorderedAccessView(history_color, nullptr, &scol_uav, D3D12_CPU_DESCRIPTOR_HANDLE{cpu_base.ptr + slot_offset + 7 * m_objs.descriptor_stride});
        device->CreateUnorderedAccessView(history_key, nullptr, &key_uav, D3D12_CPU_DESCRIPTOR_HANDLE{cpu_base.ptr + slot_offset + 8 * m_objs.descriptor_stride});

        D3D12_SHADER_RESOURCE_VIEW_DESC hist_srv{};
        hist_srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        hist_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        hist_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        hist_srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView((mode == Mode::YoroScatter) ? history_color : nullptr, &hist_srv,
            D3D12_CPU_DESCRIPTOR_HANDLE{cpu_base.ptr + slot_offset + 3 * m_objs.descriptor_stride});

        // u5: prepass write target (null when this mode has no prepass).
        D3D12_UNORDERED_ACCESS_VIEW_DESC prep_uav{};
        prep_uav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        prep_uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView((prep_pso != nullptr) ? m_prep.Get() : nullptr, nullptr, &prep_uav,
            D3D12_CPU_DESCRIPTOR_HANDLE{cpu_base.ptr + slot_offset + 9 * m_objs.descriptor_stride});

        // t4: SceneVelocity snapshot (tracker leaves it in NON_PIXEL_SHADER_
        // RESOURCE; null descriptor when no snapshot exists yet).
        D3D12_SHADER_RESOURCE_VIEW_DESC vel_srv{};
        vel_srv.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
        vel_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        vel_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        vel_srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(m_velocity_tex, &vel_srv,
            D3D12_CPU_DESCRIPTOR_HANDLE{cpu_base.ptr + slot_offset + 10 * m_objs.descriptor_stride});

        // CPU-only twins of the scatter key/color UAVs for
        // ClearUnorderedAccessView* (same inputs, so the same guard applies).
        if (mode == Mode::YoroScatter) {
            const auto clear_cpu_base = m_objs.clear_heap->GetCPUDescriptorHandleForHeapStart();
            device->CreateUnorderedAccessView(m_scatter_key[cur].Get(), nullptr, &key_uav,
                D3D12_CPU_DESCRIPTOR_HANDLE{clear_cpu_base.ptr + static_cast<size_t>(slot) * 2 * m_objs.descriptor_stride});
            device->CreateUnorderedAccessView(m_scatter_color[cur].Get(), nullptr, &scol_uav,
                D3D12_CPU_DESCRIPTOR_HANDLE{clear_cpu_base.ptr + (static_cast<size_t>(slot) * 2 + 1) * m_objs.descriptor_stride});
        }
    }

    // Read-combo states that already include NON_PIXEL_SHADER_RESOURCE (e.g.
    // UEVR's ENGINE_SRC_COLOR / ENGINE_SRC_DEPTH) are readable by compute
    // as-is; narrowing them would just be barrier churn.
    constexpr auto shader_read = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    const bool color_needs_transition = (color_state & shader_read) != shader_read;
    const bool depth_needs_transition = (depth_state & shader_read) != shader_read;
    if (color_needs_transition) {
        transition(cmd_list, color, color_state, shader_read);
    }
    if (depth_needs_transition) {
        transition(cmd_list, depth, depth_state, shader_read);
    }
    transition(cmd_list, m_output.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // NOTE: this rebinds the descriptor heap for the remainder of the command
    // list - callers that had their own CBV/SRV/UAV heap bound must rebind it
    // after synthesize() returns.
    ID3D12DescriptorHeap* heaps[] = {m_objs.heap.Get()};
    cmd_list->SetDescriptorHeaps(1, heaps);
    cmd_list->SetComputeRootSignature(m_objs.root_sig.Get());

    const auto gpu_base = m_objs.heap->GetGPUDescriptorHandleForHeapStart();
    cmd_list->SetComputeRootDescriptorTable(0, D3D12_GPU_DESCRIPTOR_HANDLE{gpu_base.ptr + slot_offset});
    cmd_list->SetComputeRootConstantBufferView(1, m_objs.cbuffer->GetGPUVirtualAddress() + static_cast<uint64_t>(slot) * kCbSlotSize);

    // Source-space passes iterate rendered pixels; output-space passes cover
    // the (possibly narrower) true-FOV target.
    const uint32_t tg = dibr_thread_group();
    const auto groups = [tg](uint32_t v) { return (v + tg - 1) / tg; };
    const uint32_t gx_src = groups(params.source_width);
    const uint32_t gy_src = groups(params.source_height);
    const uint32_t gx_out = groups(params.out_width);
    const uint32_t gy_out = groups(params.out_height);
    // Scatter-space passes (clear, fill) cover the synthesis buffers, which are
    // synth_width x synth_height (== out dims at scale 1.0). The compose pass
    // still covers the full out dims and upscales the scatter result.
    const uint32_t gx_synth = groups(params.synth_width);
    const uint32_t gy_synth = groups(params.synth_height);

    // Batched UAV barriers: consecutive single-resource ResourceBarrier calls
    // collapse into one call with an array.
    const auto uav_barrier = [cmd_list](ID3D12Resource* r) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        b.UAV.pResource = r;
        cmd_list->ResourceBarrier(1, &b);
    };
    const auto uav_barrier_pair = [cmd_list](ID3D12Resource* a, ID3D12Resource* b) {
        D3D12_RESOURCE_BARRIER bs[2]{};
        bs[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        bs[0].UAV.pResource = a;
        bs[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        bs[1].UAV.pResource = b;
        cmd_list->ResourceBarrier(2, bs);
    };

    // Conditioned-depth prepass for the gather kernels: one dispatch over
    // source pixels runs the multi-tap conditioning chain ONCE per pixel; the
    // main kernel's search loops then read it back as single taps (t2). The
    // state transitions double as the write->read barrier.
    if (prep_pso != nullptr) {
        ts_begin();
        if (m_prep_is_srv) {
            transition(cmd_list, m_prep.Get(), shader_read, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        cmd_list->SetPipelineState(prep_pso);
        cmd_list->Dispatch(gx_src, gy_src, 1);
        transition(cmd_list, m_prep.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, shader_read);
        m_prep_is_srv = true;
        ts_end(TsPass::Prep);
    }

    if (mode == Mode::YoroScatter) {
        const uint32_t cur = m_scatter_index;

        // clear (fixed-function) -> depth scatter (nearest wins) -> color
        // resolve -> hole fill. ClearUnorderedAccessView* replaces the old
        // clear dispatch: it needs the shader-visible GPU handle (heap is
        // bound above) plus a CPU handle from the non-shader-visible
        // clear_heap (descriptors written under the churn guard above).
        ts_begin();
        const auto clear_cpu_base = m_objs.clear_heap->GetCPUDescriptorHandleForHeapStart();
        const D3D12_CPU_DESCRIPTOR_HANDLE key_clear_cpu{clear_cpu_base.ptr + static_cast<size_t>(slot) * 2 * m_objs.descriptor_stride};
        const D3D12_CPU_DESCRIPTOR_HANDLE color_clear_cpu{clear_cpu_base.ptr + (static_cast<size_t>(slot) * 2 + 1) * m_objs.descriptor_stride};
        const D3D12_GPU_DESCRIPTOR_HANDLE key_gpu{gpu_base.ptr + slot_offset + 5 * m_objs.descriptor_stride};
        const D3D12_GPU_DESCRIPTOR_HANDLE color_gpu{gpu_base.ptr + slot_offset + 6 * m_objs.descriptor_stride};
        const UINT key_zero[4] = {0u, 0u, 0u, 0u}; // 0 = empty (farther than any reversed-Z depth)
        const FLOAT color_zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        cmd_list->ClearUnorderedAccessViewUint(key_gpu, key_clear_cpu, m_scatter_key[cur].Get(), key_zero, 0, nullptr);
        cmd_list->ClearUnorderedAccessViewFloat(color_gpu, color_clear_cpu, m_scatter_color[cur].Get(), color_zero, 0, nullptr);
        uav_barrier_pair(m_scatter_key[cur].Get(), m_scatter_color[cur].Get());
        ts_end(TsPass::Clear);

        ts_begin();
        cmd_list->SetPipelineState(m_objs.pso_scatter_depth.Get());
        cmd_list->Dispatch(gx_src, gy_src, 1);
        uav_barrier(m_scatter_key[cur].Get());
        ts_end(TsPass::ScatterDepth);

        ts_begin();
        cmd_list->SetPipelineState(m_objs.pso_scatter_color.Get());
        cmd_list->Dispatch(gx_src, gy_src, 1);
        uav_barrier(m_scatter_color[cur].Get());
        ts_end(TsPass::ScatterColor);

        // The fill and the compose only READ the history color (t3 SRV);
        // shader-readable state for both, back to UAV after the compose.
        transition(cmd_list, history_color, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, shader_read);

        ts_begin();
        cmd_list->SetPipelineState(m_objs.pso_scatter_fill.Get());
        cmd_list->Dispatch(gx_synth, gy_synth, 1);
        // The fill pass also commits marker-bit keys; next frame reads this
        // buffer as g_historyKey, so its writes need ordering too.
        uav_barrier_pair(m_scatter_color[cur].Get(), m_scatter_key[cur].Get());
        ts_end(TsPass::Fill);

        // This frame's filled pair becomes the next frame's temporal history.
        m_scatter_index = cur ^ 1u;
        m_scatter_history_valid = true;
    }

    ts_begin();
    cmd_list->SetPipelineState(pso);

    // One thread per OUTPUT pixel; each thread writes both eyes' output pixels.
    cmd_list->Dispatch(gx_out, gy_out, 1);
    ts_end(TsPass::Compose);

    if (mode == Mode::YoroScatter) {
        // The compose was the last history reader; restore UNORDERED_ACCESS
        // (the AFW stash writes it next, and every pass assumes UAV at entry).
        transition(cmd_list, history_color, shader_read, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    if (mode == Mode::YoroScatter && afw_history) {
        // The fill AND the compose above have consumed the previous stash;
        // only now overwrite it with THIS frame's raw source render for the
        // next frame. The UAV barriers order the read-then-write on the
        // shared history pair.
        ts_begin();
        uav_barrier_pair(m_afw_history_color.Get(), m_afw_history_key.Get());
        cmd_list->SetPipelineState(m_objs.pso_afw_stash.Get());
        cmd_list->Dispatch(gx_out, gy_out, 1);
        uav_barrier_pair(m_afw_history_color.Get(), m_afw_history_key.Get());
        m_afw_history_valid = true;
        ts_end(TsPass::Stash);
    }

    // Velocity discovery Layer 3: sample a few texels of the snapshot for
    // the behavioral decode calibration (no-op once calibrated or when the
    // velocity example is not armed).
    record_velocity_calibration(cmd_list, slot, params);

    // Resolve this frame's timestamp pairs into the readback slot; they are
    // consumed when the ring wraps back around (kRing frames later).
    if (ts_on && ts_pass_count > 0) {
        cmd_list->ResolveQueryData(m_ts_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
            slot * kTsQueriesPerSlot, ts_pass_count * 2,
            m_ts_readback.Get(), static_cast<uint64_t>(slot) * kTsQueriesPerSlot * sizeof(uint64_t));
        m_ts_slot_count[slot] = static_cast<uint8_t>(ts_pass_count);
    }

    transition(cmd_list, m_output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    if (depth_needs_transition) {
        transition(cmd_list, depth, shader_read, depth_state);
    }
    if (color_needs_transition) {
        transition(cmd_list, color, shader_read, color_state);
    }

    ++m_frame_index;
    return m_output.Get();
}

void DIBRSynthesis::reset() {
    // Non-blocking by design: a build still in flight is abandoned via the
    // generation bump and discards its own objects when it finishes. Callers
    // must guarantee the GPU is done with the output texture (the integration
    // waits on its command context before each use).
    std::scoped_lock _{m_commit_mtx};
    m_generation.fetch_add(1, std::memory_order_acq_rel);
    m_objs = {};
    m_output.Reset();
    m_output_width = 0;
    m_output_height = 0;
    for (auto& r : m_scatter_key) {
        r.Reset();
    }
    for (auto& r : m_scatter_color) {
        r.Reset();
    }
    m_scatter_width = 0;
    m_scatter_height = 0;
    m_scatter_index = 0;
    m_scatter_history_valid = false;
    m_afw_history_key.Reset();
    m_afw_history_color.Reset();
    m_afw_history_valid = false;
    m_prep.Reset();
    m_prep_width = 0;
    m_prep_height = 0;
    m_prep_is_srv = false;
    if (m_ts_readback != nullptr && m_ts_mapped != nullptr) {
        m_ts_readback->Unmap(0, nullptr);
    }
    m_ts_heap.Reset();
    m_ts_readback.Reset();
    m_ts_mapped = nullptr;
    m_ts_queue = nullptr;
    m_ts_frequency = 0;
    m_ts_slot_count.fill(0);
    if (m_vel_calib_readback != nullptr && m_vel_calib_mapped != nullptr) {
        m_vel_calib_readback->Unmap(0, nullptr);
    }
    m_vel_calib_readback.Reset();
    m_vel_calib_mapped = nullptr;
    for (auto& s : m_vel_calib_slots) {
        s.valid = false;
    }
    m_velocity_tex = nullptr;
    m_slot_desc_hash.fill(0);
    m_ring_index = 0;
    m_state.store(State::NotStarted, std::memory_order_release);
}

DXGI_FORMAT DIBRSynthesis::color_srv_format(DXGI_FORMAT f) {
    switch (f) {
    // sRGB sources are viewed as plain UNORM on purpose: the kernel's output
    // is copied back into the same-family target bit-for-bit, so decoding to
    // linear here would double-decode the image.
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default:
        return f;
    }
}

DXGI_FORMAT DIBRSynthesis::depth_srv_format(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
        return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
        return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_D16_UNORM:
        return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    default:
        return f;
    }
}

uint32_t DIBRSynthesis::output_layout_mode(const DIBRStereoParams& p) {
    const float clamped = (std::max)(0.0f, (std::min)(4.0f, p.output_layout_mode));
    return static_cast<uint32_t>(clamped + 0.5f);
}

uint32_t DIBRSynthesis::frame_pack_gap_height(const DIBRStereoParams& p) {
    const float gap = static_cast<float>(p.source_height) * 0.08510638f;
    return (std::max)(1u, static_cast<uint32_t>(gap + 0.5f));
}

void DIBRSynthesis::packed_output_dimensions(const DIBRStereoParams& p, uint32_t& w, uint32_t& h) {
    // Packed output is laid out in OUTPUT (submit) eye units, which only
    // differ from the source when the overscan-grown RT widened the source.
    const uint32_t sw = (p.out_width != 0) ? p.out_width : p.source_width;
    const uint32_t sh = (p.out_height != 0) ? p.out_height : p.source_height;
    const uint32_t layout = output_layout_mode(p);
    if (layout >= 4) { // mono / debug passthrough
        w = sw;
        h = sh;
    } else if (layout == 3) { // frame-packed (top-bottom with gap)
        w = sw;
        h = sh * 2 + frame_pack_gap_height(p);
    } else if (layout == 2) { // 2x2 tiled
        w = sw * 2;
        h = sh * 2;
    } else if (layout == 1) { // top-bottom
        w = sw;
        h = sh * 2;
    } else { // 0: side-by-side double-wide
        w = sw * 2;
        h = sh;
    }
}
} // namespace vrmod
