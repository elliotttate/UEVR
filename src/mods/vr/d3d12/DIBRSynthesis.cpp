#include <windows.h>

#include <algorithm>
#include <chrono>
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

// Guards against C++/HLSL layout drift across 243 fields: the compiled
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
        // 245 scalars (incl. reproj_enabled + scatter_compose) + two float4x4,
        // which reflection counts as ONE variable each.
        constexpr uint32_t expected_fields = 245u + 2u;
        constexpr uint32_t expected_last_offset = offsetof(DIBRStereoParams, scatter_compose);
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
    // Mirrors vrmod-stereo's layout: one table with SRV t0..t1 (offset 0) and
    // UAV u0 (offset 2), a root CBV at b0, and static samplers s0 (linear
    // clamp) / s1 (point clamp).
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 2;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 3; // u0 output, u1 scatter key, u2 scatter color
    ranges[1].BaseShaderRegister = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = 2;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 2;
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
        const char* file;
        std::string (*embedded)();
        ComPtr<ID3D12PipelineState>* pso;
    };
    const Kernel kernels[] = {
        {"dibr_inverse.hlsl", &dibr_shaders::dibr_inverse_source, &objs.pso_inverse},
        {"dibr_yoro.hlsl", &dibr_shaders::dibr_yoro_source, &objs.pso_yoro},
        {"dibr_raymarch.hlsl", &dibr_shaders::dibr_raymarch_source, &objs.pso_raymarch},
        {"dibr_scatter_clear.hlsl", &dibr_shaders::dibr_scatter_clear_source, &objs.pso_scatter_clear},
        {"dibr_scatter_depth.hlsl", &dibr_shaders::dibr_scatter_depth_source, &objs.pso_scatter_depth},
        {"dibr_scatter_color.hlsl", &dibr_shaders::dibr_scatter_color_source, &objs.pso_scatter_color},
        {"dibr_scatter_fill.hlsl", &dibr_shaders::dibr_scatter_fill_source, &objs.pso_scatter_fill},
    };

    for (const auto& k : kernels) {
        const auto source = load_shader_source(k.file, k.embedded);
        std::vector<uint8_t> bytecode{};
        if (!compile_kernel(source, k.file, bytecode)) {
            return false;
        }
        if (!validate_cbuffer_layout(bytecode, k.file)) {
            return false;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc{};
        pso_desc.pRootSignature = objs.root_sig.Get();
        pso_desc.CS = D3D12_SHADER_BYTECODE{bytecode.data(), bytecode.size()};
        if (FAILED(device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(k.pso->ReleaseAndGetAddressOf())))) {
            SPDLOG_ERROR("[DIBR] {}: CreateComputePipelineState failed", k.file);
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
    if (m_scatter_key != nullptr && m_scatter_width == width && m_scatter_height == height) {
        return true;
    }

    m_scatter_key.Reset();
    m_scatter_color.Reset();

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
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_scatter_key)))) {
        SPDLOG_ERROR_ONCE("[DIBR] failed to create {}x{} scatter key texture", width, height);
        return false;
    }
    m_scatter_key->SetName(L"DIBR Scatter Key");

    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (FAILED(device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_scatter_color)))) {
        SPDLOG_ERROR_ONCE("[DIBR] failed to create {}x{} scatter color texture", width, height);
        m_scatter_key.Reset();
        return false;
    }
    m_scatter_color->SetName(L"DIBR Scatter Color");

    m_scatter_width = width;
    m_scatter_height = height;
    SPDLOG_INFO("[DIBR] scatter buffers {}x{}", width, height);
    return true;
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
    // degrades to the plain gather kernel.
    if (mode == Mode::YoroScatter && params.reproj_enabled < 0.5f) {
        mode = Mode::Yoro;
    }

    ID3D12PipelineState* pso = nullptr;
    switch (mode) {
    case Mode::InverseWarp:
        pso = m_objs.pso_inverse.Get();
        break;
    case Mode::Yoro:
        pso = m_objs.pso_yoro.Get();
        break;
    case Mode::Raymarch:
        pso = m_objs.pso_raymarch.Get();
        break;
    case Mode::YoroScatter:
        // Final compose runs through the yoro kernel with scatter_compose set.
        pso = m_objs.pso_yoro.Get();
        if (m_objs.pso_scatter_clear == nullptr || m_objs.pso_scatter_depth == nullptr ||
            m_objs.pso_scatter_color == nullptr || m_objs.pso_scatter_fill == nullptr) {
            pso = nullptr;
        } else {
            params.scatter_compose = 1.0f;
        }
        break;
    }
    if (pso == nullptr) {
        return nullptr;
    }

    const auto color_desc = color->GetDesc();
    const auto depth_desc = depth->GetDesc();
    params.source_width = static_cast<uint32_t>(color_desc.Width);
    params.source_height = color_desc.Height;
    params.frame_index = m_frame_index;

    if (!ensure_scatter(device, params.source_width, params.source_height)) {
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

    const auto cpu_base = m_objs.heap->GetCPUDescriptorHandleForHeapStart();
    const size_t slot_offset = static_cast<size_t>(slot) * kDescriptorsPerSlot * m_objs.descriptor_stride;

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

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = m_output_format;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(m_output.Get(), nullptr, &uav, D3D12_CPU_DESCRIPTOR_HANDLE{cpu_base.ptr + slot_offset + 2 * m_objs.descriptor_stride});

    D3D12_UNORDERED_ACCESS_VIEW_DESC key_uav{};
    key_uav.Format = DXGI_FORMAT_R32_UINT;
    key_uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(m_scatter_key.Get(), nullptr, &key_uav, D3D12_CPU_DESCRIPTOR_HANDLE{cpu_base.ptr + slot_offset + 3 * m_objs.descriptor_stride});

    D3D12_UNORDERED_ACCESS_VIEW_DESC scol_uav{};
    scol_uav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    scol_uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(m_scatter_color.Get(), nullptr, &scol_uav, D3D12_CPU_DESCRIPTOR_HANDLE{cpu_base.ptr + slot_offset + 4 * m_objs.descriptor_stride});

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

    const uint32_t gx = (params.source_width + 15) / 16;
    const uint32_t gy = (params.source_height + 15) / 16;

    if (mode == Mode::YoroScatter) {
        const auto uav_barrier = [cmd_list](ID3D12Resource* r) {
            D3D12_RESOURCE_BARRIER b{};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            b.UAV.pResource = r;
            cmd_list->ResourceBarrier(1, &b);
        };

        // clear -> depth scatter (nearest wins) -> color resolve -> hole fill.
        cmd_list->SetPipelineState(m_objs.pso_scatter_clear.Get());
        cmd_list->Dispatch(gx, gy, 1);
        uav_barrier(m_scatter_key.Get());
        uav_barrier(m_scatter_color.Get());

        cmd_list->SetPipelineState(m_objs.pso_scatter_depth.Get());
        cmd_list->Dispatch(gx, gy, 1);
        uav_barrier(m_scatter_key.Get());

        cmd_list->SetPipelineState(m_objs.pso_scatter_color.Get());
        cmd_list->Dispatch(gx, gy, 1);
        uav_barrier(m_scatter_color.Get());

        cmd_list->SetPipelineState(m_objs.pso_scatter_fill.Get());
        cmd_list->Dispatch(gx, gy, 1);
        uav_barrier(m_scatter_color.Get());
    }

    cmd_list->SetPipelineState(pso);

    // One thread per SOURCE pixel; each thread writes both eyes' output pixels.
    cmd_list->Dispatch(gx, gy, 1);

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
    m_scatter_key.Reset();
    m_scatter_color.Reset();
    m_scatter_width = 0;
    m_scatter_height = 0;
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
    const uint32_t sw = p.source_width;
    const uint32_t sh = p.source_height;
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
