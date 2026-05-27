#include "render/D3D12Diagnostics.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include <wrl/client.h>

namespace {
constexpr size_t MAX_RECENT_BINDINGS = 512;
constexpr size_t MAX_RECENT_ROOT_BINDS = 4096;
constexpr size_t MAX_RECENT_DRAW_EVENTS = 4096;
constexpr size_t MAX_RECENT_BARRIERS = 512;
constexpr size_t MAX_RECENT_WARNINGS = 64;
constexpr size_t MAX_RECENT_PIPELINE_CACHE_EVENTS = 256;

// Try to read an engine-supplied debug name off a resource. UE's RHI sets
// WKPDID_D3DDebugObjectName / WKPDID_D3DDebugObjectNameW on render targets
// ("SceneColor", "GBufferA", "MotionBlur_*", etc.), which are the most
// human-readable hooks we have to tie a raw RTV back to engine intent.
std::string try_resolve_d3d_debug_name(ID3D12Resource* resource) {
    if (resource == nullptr) return {};
    UINT size = 0;
    if (SUCCEEDED(resource->GetPrivateData(WKPDID_D3DDebugObjectName, &size, nullptr)) && size > 0 && size < 1024) {
        std::string buf(size, '\0');
        if (SUCCEEDED(resource->GetPrivateData(WKPDID_D3DDebugObjectName, &size, buf.data()))) {
            while (!buf.empty() && buf.back() == '\0') buf.pop_back();
            if (!buf.empty()) return buf;
        }
    }
    UINT wsize = 0;
    if (SUCCEEDED(resource->GetPrivateData(WKPDID_D3DDebugObjectNameW, &wsize, nullptr)) && wsize > 0 && wsize < 2048) {
        std::wstring wbuf(wsize / sizeof(wchar_t), L'\0');
        if (SUCCEEDED(resource->GetPrivateData(WKPDID_D3DDebugObjectNameW, &wsize, wbuf.data()))) {
            while (!wbuf.empty() && wbuf.back() == L'\0') wbuf.pop_back();
            if (!wbuf.empty()) {
                int n = WideCharToMultiByte(CP_UTF8, 0, wbuf.c_str(), (int)wbuf.size(), nullptr, 0, nullptr, nullptr);
                std::string narrow(n, '\0');
                WideCharToMultiByte(CP_UTF8, 0, wbuf.c_str(), (int)wbuf.size(), narrow.data(), n, nullptr, nullptr);
                return narrow;
            }
        }
    }
    return {};
}

template <typename T>
void push_ring(std::vector<T>& values, T value, size_t max_entries) {
    if (values.size() >= max_entries) {
        values.erase(values.begin());
    }

    values.emplace_back(std::move(value));
}

std::string format_pointer(uintptr_t pointer) {
    std::ostringstream ss{};
    ss << "0x" << std::hex << std::uppercase << pointer;
    return ss.str();
}

uint64_t fnv1a64_bytes(const void* data, size_t size) {
    constexpr uint64_t offset = 1469598103934665603ull;
    constexpr uint64_t prime = 1099511628211ull;
    if (data == nullptr || size == 0) {
        return 0;
    }
    uint64_t hash = offset;
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= prime;
    }
    return hash;
}

std::string descriptor_heap_type_to_string(D3D12_DESCRIPTOR_HEAP_TYPE type) {
    switch (type) {
    case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
        return "CBV/SRV/UAV";
    case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
        return "Sampler";
    case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
        return "RTV";
    case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
        return "DSV";
    default:
        return "Unknown";
    }
}

std::string root_parameter_type_to_string(D3D12_ROOT_PARAMETER_TYPE type) {
    switch (type) {
    case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
        return "descriptor_table";
    case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
        return "32bit_constants";
    case D3D12_ROOT_PARAMETER_TYPE_CBV:
        return "cbv";
    case D3D12_ROOT_PARAMETER_TYPE_SRV:
        return "srv";
    case D3D12_ROOT_PARAMETER_TYPE_UAV:
        return "uav";
    default:
        return "unknown";
    }
}

std::string descriptor_range_type_to_string(D3D12_DESCRIPTOR_RANGE_TYPE type) {
    switch (type) {
    case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
        return "srv";
    case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
        return "uav";
    case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
        return "cbv";
    case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
        return "sampler";
    default:
        return "unknown";
    }
}

std::string shader_visibility_to_string(D3D12_SHADER_VISIBILITY visibility) {
    switch (visibility) {
    case D3D12_SHADER_VISIBILITY_ALL:
        return "all";
    case D3D12_SHADER_VISIBILITY_VERTEX:
        return "vertex";
    case D3D12_SHADER_VISIBILITY_HULL:
        return "hull";
    case D3D12_SHADER_VISIBILITY_DOMAIN:
        return "domain";
    case D3D12_SHADER_VISIBILITY_GEOMETRY:
        return "geometry";
    case D3D12_SHADER_VISIBILITY_PIXEL:
        return "pixel";
    case D3D12_SHADER_VISIBILITY_AMPLIFICATION:
        return "amplification";
    case D3D12_SHADER_VISIBILITY_MESH:
        return "mesh";
    default:
        return "unknown";
    }
}

std::string root_signature_flags_to_string(D3D12_ROOT_SIGNATURE_FLAGS flags) {
    if (flags == D3D12_ROOT_SIGNATURE_FLAG_NONE) {
        return "none";
    }

    std::string result{};
    auto add = [&](D3D12_ROOT_SIGNATURE_FLAGS flag, const char* token) {
        if ((flags & flag) != 0) {
            if (!result.empty()) {
                result += "|";
            }
            result += token;
        }
    };

    add(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT, "allow_ia");
    add(D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS, "deny_vs");
    add(D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS, "deny_hs");
    add(D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS, "deny_ds");
    add(D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS, "deny_gs");
    add(D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS, "deny_ps");
    add(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT, "allow_stream_output");
    add(D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE, "local");
    add(D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS, "deny_as");
    add(D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS, "deny_ms");
    add(D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED, "direct_cbv_srv_uav_heap");
    add(D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED, "direct_sampler_heap");

    if (result.empty()) {
        result = format_pointer(static_cast<uintptr_t>(flags));
    }

    return result;
}

std::string barrier_type_to_string(D3D12_RESOURCE_BARRIER_TYPE type) {
    switch (type) {
    case D3D12_RESOURCE_BARRIER_TYPE_TRANSITION:
        return "Transition";
    case D3D12_RESOURCE_BARRIER_TYPE_ALIASING:
        return "Aliasing";
    case D3D12_RESOURCE_BARRIER_TYPE_UAV:
        return "UAV";
    default:
        return "Unknown";
    }
}

void append_state_token(std::string& out, const char* token) {
    if (!out.empty()) {
        out += "|";
    }

    out += token;
}

std::string resource_state_to_string(D3D12_RESOURCE_STATES state) {
    if (state == D3D12_RESOURCE_STATE_COMMON) {
        return "COMMON";
    }

    std::string result{};

    if ((state & D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER) != 0) {
        append_state_token(result, "VB/CB");
    }
    if ((state & D3D12_RESOURCE_STATE_INDEX_BUFFER) != 0) {
        append_state_token(result, "IB");
    }
    if ((state & D3D12_RESOURCE_STATE_RENDER_TARGET) != 0) {
        append_state_token(result, "RT");
    }
    if ((state & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) != 0) {
        append_state_token(result, "UAV");
    }
    if ((state & D3D12_RESOURCE_STATE_DEPTH_WRITE) != 0) {
        append_state_token(result, "DepthWrite");
    }
    if ((state & D3D12_RESOURCE_STATE_DEPTH_READ) != 0) {
        append_state_token(result, "DepthRead");
    }
    if ((state & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) != 0) {
        append_state_token(result, "NPSR");
    }
    if ((state & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) != 0) {
        append_state_token(result, "PSR");
    }
    if ((state & D3D12_RESOURCE_STATE_STREAM_OUT) != 0) {
        append_state_token(result, "StreamOut");
    }
    if ((state & D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT) != 0) {
        append_state_token(result, "Indirect");
    }
    if ((state & D3D12_RESOURCE_STATE_COPY_DEST) != 0) {
        append_state_token(result, "CopyDest");
    }
    if ((state & D3D12_RESOURCE_STATE_COPY_SOURCE) != 0) {
        append_state_token(result, "CopySource");
    }
    if ((state & D3D12_RESOURCE_STATE_RESOLVE_DEST) != 0) {
        append_state_token(result, "ResolveDest");
    }
    if ((state & D3D12_RESOURCE_STATE_RESOLVE_SOURCE) != 0) {
        append_state_token(result, "ResolveSource");
    }
    if ((state & D3D12_RESOURCE_STATE_PRESENT) != 0) {
        append_state_token(result, "Present");
    }
    if ((state & D3D12_RESOURCE_STATE_PREDICATION) != 0) {
        append_state_token(result, "Predication");
    }

    if (result.empty()) {
        result = format_pointer(static_cast<uintptr_t>(state));
    }

    return result;
}

uint64_t bytes_per_pixel(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
        return 16;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32_SINT:
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
        return 8;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
        return 4;
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R16_SINT:
        return 2;
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_SINT:
    case DXGI_FORMAT_A8_UNORM:
        return 1;
    default:
        return 4;
    }
}

uint64_t approximate_resource_size(const D3D12_RESOURCE_DESC& desc) {
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        return 0;
    }

    const auto samples = std::max<UINT>(1, desc.SampleDesc.Count);
    const auto array_size = std::max<UINT16>(1, desc.DepthOrArraySize);
    const auto bpp = bytes_per_pixel(desc.Format);
    return static_cast<uint64_t>(desc.Width) * static_cast<uint64_t>(std::max<UINT>(1, desc.Height)) * bpp * samples * array_size;
}

std::string resource_name_or_pointer(ID3D12Object* object, uintptr_t pointer) {
    if (object != nullptr) {
        UINT chars = 0;
        if (SUCCEEDED(object->GetPrivateData(WKPDID_D3DDebugObjectNameW, &chars, nullptr)) && chars > sizeof(wchar_t)) {
            std::wstring name(chars / sizeof(wchar_t), L'\0');
            if (SUCCEEDED(object->GetPrivateData(WKPDID_D3DDebugObjectNameW, &chars, name.data()))) {
                name.resize((chars / sizeof(wchar_t)) - 1);
                if (!name.empty()) {
                    std::string narrow{};
                    narrow.reserve(name.size());
                    for (const auto ch : name) {
                        narrow.push_back(static_cast<char>(ch & 0xFF));
                    }
                    return narrow;
                }
            }
        }
    }

    return format_pointer(pointer);
}
} // namespace

namespace render {
namespace {
void append_root_descriptor_range(
    D3D12Diagnostics::RootParameterInfo& out,
    const D3D12_DESCRIPTOR_RANGE& range
) {
    D3D12Diagnostics::RootDescriptorRangeInfo info{};
    info.type = descriptor_range_type_to_string(range.RangeType);
    info.base_shader_register = range.BaseShaderRegister;
    info.num_descriptors = range.NumDescriptors;
    info.register_space = range.RegisterSpace;
    info.offset_from_table_start = range.OffsetInDescriptorsFromTableStart;
    out.ranges.emplace_back(std::move(info));
}

void append_root_descriptor_range(
    D3D12Diagnostics::RootParameterInfo& out,
    const D3D12_DESCRIPTOR_RANGE1& range
) {
    D3D12Diagnostics::RootDescriptorRangeInfo info{};
    info.type = descriptor_range_type_to_string(range.RangeType);
    info.base_shader_register = range.BaseShaderRegister;
    info.num_descriptors = range.NumDescriptors;
    info.register_space = range.RegisterSpace;
    info.offset_from_table_start = range.OffsetInDescriptorsFromTableStart;
    out.ranges.emplace_back(std::move(info));
}

void append_root_parameter(
    D3D12Diagnostics::RootSignatureInfo& out,
    uint32_t index,
    const D3D12_ROOT_PARAMETER& param
) {
    D3D12Diagnostics::RootParameterInfo info{};
    info.index = index;
    info.parameter_type = root_parameter_type_to_string(param.ParameterType);
    info.visibility = shader_visibility_to_string(param.ShaderVisibility);

    switch (param.ParameterType) {
    case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
        if (param.DescriptorTable.pDescriptorRanges != nullptr) {
            info.ranges.reserve(param.DescriptorTable.NumDescriptorRanges);
            for (UINT i = 0; i < param.DescriptorTable.NumDescriptorRanges; ++i) {
                append_root_descriptor_range(info, param.DescriptorTable.pDescriptorRanges[i]);
            }
        }
        break;
    case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
        info.shader_register = param.Constants.ShaderRegister;
        info.register_space = param.Constants.RegisterSpace;
        info.num_32bit_values = param.Constants.Num32BitValues;
        break;
    case D3D12_ROOT_PARAMETER_TYPE_CBV:
    case D3D12_ROOT_PARAMETER_TYPE_SRV:
    case D3D12_ROOT_PARAMETER_TYPE_UAV:
        info.shader_register = param.Descriptor.ShaderRegister;
        info.register_space = param.Descriptor.RegisterSpace;
        break;
    default:
        break;
    }

    out.parameters.emplace_back(std::move(info));
}

void append_root_parameter(
    D3D12Diagnostics::RootSignatureInfo& out,
    uint32_t index,
    const D3D12_ROOT_PARAMETER1& param
) {
    D3D12Diagnostics::RootParameterInfo info{};
    info.index = index;
    info.parameter_type = root_parameter_type_to_string(param.ParameterType);
    info.visibility = shader_visibility_to_string(param.ShaderVisibility);

    switch (param.ParameterType) {
    case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
        if (param.DescriptorTable.pDescriptorRanges != nullptr) {
            info.ranges.reserve(param.DescriptorTable.NumDescriptorRanges);
            for (UINT i = 0; i < param.DescriptorTable.NumDescriptorRanges; ++i) {
                append_root_descriptor_range(info, param.DescriptorTable.pDescriptorRanges[i]);
            }
        }
        break;
    case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
        info.shader_register = param.Constants.ShaderRegister;
        info.register_space = param.Constants.RegisterSpace;
        info.num_32bit_values = param.Constants.Num32BitValues;
        break;
    case D3D12_ROOT_PARAMETER_TYPE_CBV:
    case D3D12_ROOT_PARAMETER_TYPE_SRV:
    case D3D12_ROOT_PARAMETER_TYPE_UAV:
        info.shader_register = param.Descriptor.ShaderRegister;
        info.register_space = param.Descriptor.RegisterSpace;
        break;
    default:
        break;
    }

    out.parameters.emplace_back(std::move(info));
}

D3D12Diagnostics::RootSignatureInfo decode_root_signature_blob(const void* blob, size_t blob_size) {
    D3D12Diagnostics::RootSignatureInfo info{};
    info.blob_size = static_cast<uint32_t>(std::min<size_t>(blob_size, UINT32_MAX));
    info.blob_hash = fnv1a64_bytes(blob, blob_size);

    if (blob == nullptr || blob_size == 0) {
        info.decode_error = "empty root signature blob";
        return info;
    }

    Microsoft::WRL::ComPtr<ID3D12VersionedRootSignatureDeserializer> versioned{};
    HRESULT hr = D3D12CreateVersionedRootSignatureDeserializer(
        blob,
        blob_size,
        IID_PPV_ARGS(&versioned));

    if (SUCCEEDED(hr) && versioned != nullptr) {
        const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* desc = nullptr;
        hr = versioned->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_1, &desc);
        if (FAILED(hr) || desc == nullptr) {
            hr = versioned->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_0, &desc);
        }

        if (SUCCEEDED(hr) && desc != nullptr) {
            if (desc->Version == D3D_ROOT_SIGNATURE_VERSION_1_1) {
                info.version = "1.1";
                info.flags = root_signature_flags_to_string(desc->Desc_1_1.Flags);
                info.static_sampler_count = desc->Desc_1_1.NumStaticSamplers;
                info.parameters.reserve(desc->Desc_1_1.NumParameters);
                for (UINT i = 0; i < desc->Desc_1_1.NumParameters; ++i) {
                    append_root_parameter(info, i, desc->Desc_1_1.pParameters[i]);
                }
            } else {
                info.version = "1.0";
                info.flags = root_signature_flags_to_string(desc->Desc_1_0.Flags);
                info.static_sampler_count = desc->Desc_1_0.NumStaticSamplers;
                info.parameters.reserve(desc->Desc_1_0.NumParameters);
                for (UINT i = 0; i < desc->Desc_1_0.NumParameters; ++i) {
                    append_root_parameter(info, i, desc->Desc_1_0.pParameters[i]);
                }
            }
            return info;
        }
    }

    Microsoft::WRL::ComPtr<ID3D12RootSignatureDeserializer> legacy{};
    hr = D3D12CreateRootSignatureDeserializer(blob, blob_size, IID_PPV_ARGS(&legacy));
    if (SUCCEEDED(hr) && legacy != nullptr) {
        const auto* desc = legacy->GetRootSignatureDesc();
        if (desc != nullptr) {
            info.version = "1.0";
            info.flags = root_signature_flags_to_string(desc->Flags);
            info.static_sampler_count = desc->NumStaticSamplers;
            info.parameters.reserve(desc->NumParameters);
            for (UINT i = 0; i < desc->NumParameters; ++i) {
                append_root_parameter(info, i, desc->pParameters[i]);
            }
            return info;
        }
    }

    std::ostringstream ss{};
    ss << "D3D12 root signature deserialization failed hr=0x" << std::hex << std::uppercase << static_cast<uint32_t>(hr);
    info.decode_error = ss.str();
    return info;
}
} // namespace

D3D12Diagnostics& D3D12Diagnostics::get() {
    static D3D12Diagnostics instance{};
    return instance;
}

void D3D12Diagnostics::set_enabled(bool enabled) {
    const auto was_enabled = m_enabled.exchange(enabled, std::memory_order_relaxed);

    if (was_enabled == enabled) {
        return;
    }

    if (!enabled) {
        std::scoped_lock _{m_mutex};
        clear_state_locked();
    }
}

bool D3D12Diagnostics::is_enabled() const {
    return m_enabled.load(std::memory_order_relaxed);
}

void D3D12Diagnostics::set_lightweight(bool lightweight) {
    m_lightweight.store(lightweight, std::memory_order_relaxed);
}

bool D3D12Diagnostics::is_lightweight() const {
    return m_lightweight.load(std::memory_order_relaxed);
}

void D3D12Diagnostics::begin_frame(
    ID3D12Device* device,
    IDXGISwapChain3* swapchain,
    ID3D12CommandQueue* queue,
    uint32_t render_width,
    uint32_t render_height,
    uint32_t display_width,
    uint32_t display_height,
    bool proton_swapchain,
    bool framegen_swapchain
) {
    if (!is_enabled()) {
        return;
    }

    std::scoped_lock _{m_mutex};
    ++m_frame;
    m_device = reinterpret_cast<uintptr_t>(device);
    m_swapchain = reinterpret_cast<uintptr_t>(swapchain);
    m_command_queue = reinterpret_cast<uintptr_t>(queue);
    m_render_width = render_width;
    m_render_height = render_height;
    m_display_width = display_width;
    m_display_height = display_height;
    m_proton_swapchain = proton_swapchain;
    m_framegen_swapchain = framegen_swapchain;
    m_descriptor_heap_sets_this_frame = 0;
    m_descriptor_heap_switches_this_frame = 0;
    m_resource_barriers_this_frame = 0;
    m_rtv_binds_this_frame = 0;
    m_root_binds_this_frame = 0;
    m_draw_events_this_frame = 0;
    m_transient_heap_creations_this_frame = 0;
    m_transient_resource_creations_this_frame = 0;
    m_transient_resource_bytes_this_frame = 0;

    for (auto& [_, heap] : m_heaps) {
        heap.is_active = false;
    }
}

void D3D12Diagnostics::register_descriptor_heap(
    std::string_view source,
    ID3D12DescriptorHeap* heap,
    uint32_t estimated_in_use,
    bool transient,
    std::string_view name
) {
    if (!is_enabled()) {
        return;
    }

    if (heap == nullptr) {
        push_warning(source, "Attempted to register a null descriptor heap");
        return;
    }

    std::scoped_lock _{m_mutex};

    const auto key = reinterpret_cast<uintptr_t>(heap);
    const auto desc = heap->GetDesc();
    auto it = m_heaps.find(key);
    const bool is_new = it == m_heaps.end();

    if (is_new) {
        HeapInfo info{};
        info.pointer = key;
        info.name = name.empty() ? resource_name_or_pointer(heap, key) : std::string{name};
        info.source = std::string{source};
        info.type = descriptor_heap_type_to_string(desc.Type);
        info.total_descriptors = desc.NumDescriptors;
        const auto requested_in_use = estimated_in_use == 0 ? desc.NumDescriptors : estimated_in_use;
        info.estimated_in_use = requested_in_use < desc.NumDescriptors ? requested_in_use : desc.NumDescriptors;
        info.first_seen_frame = m_frame;
        info.last_seen_frame = m_frame;
        info.shader_visible = desc.Flags == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        info.transient = transient;
        m_heaps.emplace(key, std::move(info));

        if (transient) {
            ++m_transient_heap_creations_this_frame;
        }
    } else {
        auto& info = it->second;
        info.last_seen_frame = m_frame;
        if (!name.empty()) {
            info.name = std::string{name};
        }
        info.source = std::string{source};
        info.total_descriptors = desc.NumDescriptors;
        if (estimated_in_use != 0) {
            const auto max_in_use = info.estimated_in_use > estimated_in_use ? info.estimated_in_use : estimated_in_use;
            info.estimated_in_use = max_in_use < desc.NumDescriptors ? max_in_use : desc.NumDescriptors;
        } else if (info.estimated_in_use == 0) {
            info.estimated_in_use = desc.NumDescriptors;
        }
        info.shader_visible = desc.Flags == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        info.transient = info.transient || transient;
    }
}

void D3D12Diagnostics::register_resource(
    std::string_view source,
    ID3D12Resource* resource,
    bool transient,
    std::string_view name
) {
    if (resource == nullptr) {
        if (is_enabled()) {
            std::scoped_lock _{m_mutex};
            push_warning(source, "Attempted to register a null resource");
        }
        return;
    }

    std::scoped_lock _{m_mutex};

    const auto key = reinterpret_cast<uintptr_t>(resource);
    const auto desc = resource->GetDesc();
    const auto bytes = approximate_resource_size(desc);
    auto it = m_resources.find(key);

    if (it == m_resources.end()) {
        ResourceInfo info{};
        info.pointer = key;
        info.name = name.empty() ? resource_name_or_pointer(resource, key) : std::string{name};
        info.source = std::string{source};
        info.format = std::to_string(static_cast<uint32_t>(desc.Format));
        info.width = static_cast<uint32_t>(desc.Width);
        info.height = desc.Height;
        info.approx_bytes = bytes;
        info.first_seen_frame = m_frame;
        info.last_seen_frame = m_frame;
        info.transient = transient;
        m_resources.emplace(key, std::move(info));

        if (transient) {
            ++m_transient_resource_creations_this_frame;
            m_transient_resource_bytes_this_frame += bytes;
        }
    } else {
        auto& info = it->second;
        info.last_seen_frame = m_frame;
        if (!name.empty()) {
            info.name = std::string{name};
        }
        info.source = std::string{source};
        info.format = std::to_string(static_cast<uint32_t>(desc.Format));
        info.width = static_cast<uint32_t>(desc.Width);
        info.height = desc.Height;
        info.approx_bytes = bytes;
        info.transient = info.transient || transient;
    }
}

void D3D12Diagnostics::register_rtv_descriptor(
    std::string_view source,
    ID3D12Resource* resource,
    D3D12_CPU_DESCRIPTOR_HANDLE handle,
    std::string_view name
) {
    if (handle.ptr == 0) {
        if (is_enabled()) {
            std::scoped_lock _{m_mutex};
            push_warning(source, "Attempted to register a null RTV descriptor");
        }
        return;
    }

    std::scoped_lock _{m_mutex};

    if (resource != nullptr) {
        register_resource(source, resource, false, name);
    }

    const auto key = static_cast<uintptr_t>(handle.ptr);
    auto& descriptor = m_rtv_descriptors[key];
    descriptor.handle = key;
    descriptor.resource = reinterpret_cast<uintptr_t>(resource);
    descriptor.source = std::string{source};
    descriptor.descriptor_type = "RTV";
    descriptor.last_seen_frame = m_frame;

    if (descriptor.first_seen_frame == 0) {
        descriptor.first_seen_frame = m_frame;
    }

    if (resource != nullptr) {
        const auto resource_key = reinterpret_cast<uintptr_t>(resource);
        if (const auto it = m_resources.find(resource_key); it != m_resources.end()) {
            descriptor.name = it->second.name;
            if (descriptor.name.empty()) {
                auto dbg = try_resolve_d3d_debug_name(resource);
                if (!dbg.empty()) {
                    descriptor.name = dbg;
                    it->second.name = dbg;
                }
            }
        }
    }

    if (descriptor.name.empty()) {
        descriptor.name = name.empty() ? format_pointer(key) : std::string{name};
    }
}

void D3D12Diagnostics::register_dsv_descriptor(
    std::string_view source,
    ID3D12Resource* resource,
    D3D12_CPU_DESCRIPTOR_HANDLE handle,
    std::string_view name
) {
    if (handle.ptr == 0) {
        if (is_enabled()) {
            std::scoped_lock _{m_mutex};
            push_warning(source, "Attempted to register a null DSV descriptor");
        }
        return;
    }

    std::scoped_lock _{m_mutex};

    if (resource != nullptr) {
        register_resource(source, resource, false, name);
    }

    const auto key = static_cast<uintptr_t>(handle.ptr);
    auto& descriptor = m_dsv_descriptors[key];
    descriptor.handle = key;
    descriptor.resource = reinterpret_cast<uintptr_t>(resource);
    descriptor.source = std::string{source};
    descriptor.descriptor_type = "DSV";
    descriptor.last_seen_frame = m_frame;

    if (descriptor.first_seen_frame == 0) {
        descriptor.first_seen_frame = m_frame;
    }

    if (resource != nullptr) {
        const auto resource_key = reinterpret_cast<uintptr_t>(resource);
        if (const auto it = m_resources.find(resource_key); it != m_resources.end()) {
            descriptor.name = it->second.name;
            if (descriptor.name.empty()) {
                auto dbg = try_resolve_d3d_debug_name(resource);
                if (!dbg.empty()) {
                    descriptor.name = dbg;
                    it->second.name = dbg;
                }
            }
        }
    }

    if (descriptor.name.empty()) {
        descriptor.name = name.empty() ? format_pointer(key) : std::string{name};
    }
}

void D3D12Diagnostics::register_srv_descriptor(
    std::string_view source,
    ID3D12Resource* resource,
    D3D12_CPU_DESCRIPTOR_HANDLE handle,
    std::string_view name
) {
    if (handle.ptr == 0) {
        if (is_enabled()) {
            std::scoped_lock _{m_mutex};
            push_warning(source, "Attempted to register a null SRV descriptor");
        }
        return;
    }

    std::scoped_lock _{m_mutex};

    if (resource != nullptr) {
        register_resource(source, resource, false, name);
    }

    const auto key = static_cast<uintptr_t>(handle.ptr);
    auto& descriptor = m_srv_descriptors[key];
    descriptor.handle = key;
    descriptor.resource = reinterpret_cast<uintptr_t>(resource);
    descriptor.source = std::string{source};
    descriptor.descriptor_type = "SRV";
    descriptor.last_seen_frame = m_frame;

    if (descriptor.first_seen_frame == 0) {
        descriptor.first_seen_frame = m_frame;
    }

    if (resource != nullptr) {
        const auto resource_key = reinterpret_cast<uintptr_t>(resource);
        if (const auto it = m_resources.find(resource_key); it != m_resources.end()) {
            descriptor.name = it->second.name;
        }
    }

    if (descriptor.name.empty()) {
        descriptor.name = name.empty() ? format_pointer(key) : std::string{name};
    }
}

void D3D12Diagnostics::register_uav_descriptor(
    std::string_view source,
    ID3D12Resource* resource,
    D3D12_CPU_DESCRIPTOR_HANDLE handle,
    std::string_view name
) {
    if (handle.ptr == 0) {
        if (is_enabled()) {
            std::scoped_lock _{m_mutex};
            push_warning(source, "Attempted to register a null UAV descriptor");
        }
        return;
    }

    std::scoped_lock _{m_mutex};

    if (resource != nullptr) {
        register_resource(source, resource, false, name);
    }

    const auto key = static_cast<uintptr_t>(handle.ptr);
    auto& descriptor = m_uav_descriptors[key];
    descriptor.handle = key;
    descriptor.resource = reinterpret_cast<uintptr_t>(resource);
    descriptor.source = std::string{source};
    descriptor.descriptor_type = "UAV";
    descriptor.last_seen_frame = m_frame;

    if (descriptor.first_seen_frame == 0) {
        descriptor.first_seen_frame = m_frame;
    }

    if (resource != nullptr) {
        const auto resource_key = reinterpret_cast<uintptr_t>(resource);
        if (const auto it = m_resources.find(resource_key); it != m_resources.end()) {
            descriptor.name = it->second.name;
        }
    }

    if (descriptor.name.empty()) {
        descriptor.name = name.empty() ? format_pointer(key) : std::string{name};
    }
}

void D3D12Diagnostics::record_descriptor_copy(
    std::string_view source,
    D3D12_CPU_DESCRIPTOR_HANDLE dst,
    D3D12_CPU_DESCRIPTOR_HANDLE src
) {
    if (dst.ptr == 0 || src.ptr == 0) {
        return;
    }

    std::scoped_lock _{m_mutex};

    auto copy_descriptor = [&](auto& descriptors) {
        const auto src_key = static_cast<uintptr_t>(src.ptr);
        const auto it = descriptors.find(src_key);
        if (it == descriptors.end()) {
            return false;
        }

        auto copy = it->second;
        copy.handle = static_cast<uintptr_t>(dst.ptr);
        copy.source_handle = static_cast<uintptr_t>(src.ptr);
        copy.source_frame = m_frame;
        copy.source = std::string{source};
        copy.last_seen_frame = m_frame;
        descriptors[copy.handle] = std::move(copy);
        return true;
    };

    if (copy_descriptor(m_srv_descriptors) || copy_descriptor(m_uav_descriptors)) {
        return;
    }

    copy_descriptor(m_rtv_descriptors);
}

void D3D12Diagnostics::register_root_signature(
    std::string_view source,
    ID3D12RootSignature* root_signature,
    const void* blob,
    size_t blob_size
) {
    (void)source;
    if (root_signature == nullptr || blob == nullptr || blob_size == 0) {
        return;
    }

    std::scoped_lock _{m_mutex};
    const auto key = reinterpret_cast<uintptr_t>(root_signature);
    auto decoded = decode_root_signature_blob(blob, blob_size);
    decoded.pointer = key;
    decoded.last_seen_frame = m_frame;

    if (const auto it = m_root_signatures.find(key); it != m_root_signatures.end()) {
        decoded.first_seen_frame = it->second.first_seen_frame;
        if (decoded.first_seen_frame == 0) {
            decoded.first_seen_frame = m_frame;
        }
    } else {
        decoded.first_seen_frame = m_frame;
    }

    m_root_signatures[key] = std::move(decoded);
}

void D3D12Diagnostics::register_pipeline_root_signature(
    std::string_view source,
    ID3D12PipelineState* pipeline_state,
    ID3D12RootSignature* root_signature
) {
    (void)source;
    if (pipeline_state == nullptr || root_signature == nullptr) {
        return;
    }

    std::scoped_lock _{m_mutex};
    m_pso_root_signatures[reinterpret_cast<uintptr_t>(pipeline_state)] =
        reinterpret_cast<uintptr_t>(root_signature);
}

std::vector<D3D12Diagnostics::RootSignatureInfo>
D3D12Diagnostics::snapshot_root_signatures() const {
    std::scoped_lock _{m_mutex};
    std::vector<RootSignatureInfo> out{};
    out.reserve(m_root_signatures.size());
    for (const auto& kv : m_root_signatures) {
        out.push_back(kv.second);
    }
    return out;
}

std::vector<std::pair<uintptr_t, uintptr_t>>
D3D12Diagnostics::snapshot_pso_root_signature_pairs() const {
    std::scoped_lock _{m_mutex};
    std::vector<std::pair<uintptr_t, uintptr_t>> out{};
    out.reserve(m_pso_root_signatures.size());
    for (const auto& [pso, rs] : m_pso_root_signatures) {
        out.emplace_back(pso, rs);
    }
    return out;
}

void D3D12Diagnostics::record_descriptor_heaps_set(
    std::string_view source,
    uint32_t count,
    ID3D12DescriptorHeap* const* heaps
) {
    // High-frequency (per SetDescriptorHeaps) + builds a detail string each call.
    // Skip in lightweight mode; the Shader Hunter resolves RT names from RTV/DSV
    // creation, not from the active descriptor-heap set.
    if (!is_enabled() || is_lightweight()) {
        return;
    }

    std::scoped_lock _{m_mutex};
    ++m_descriptor_heap_sets_this_frame;

    std::ostringstream detail{};
    detail << "count=" << count;

    for (uint32_t i = 0; i < count; ++i) {
        const auto heap = heaps != nullptr ? heaps[i] : nullptr;
        if (heap == nullptr) {
            push_warning(source, "SetDescriptorHeaps received a null heap");
            continue;
        }

        const auto desc = heap->GetDesc();
        register_descriptor_heap(source, heap, desc.NumDescriptors, false);

        const auto key = reinterpret_cast<uintptr_t>(heap);
        auto& tracked = m_heaps[key];
        tracked.bind_count++;
        tracked.is_active = true;

        uintptr_t* active_ptr = nullptr;
        if (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
            active_ptr = &m_active_cbv_srv_uav_heap;
        } else if (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) {
            active_ptr = &m_active_sampler_heap;
        }

        if (active_ptr != nullptr && *active_ptr != key) {
            if (*active_ptr != 0) {
                ++m_descriptor_heap_switches_this_frame;
            }
            *active_ptr = key;
        }

        detail << " [" << i << "] " << descriptor_heap_type_to_string(desc.Type) << "=" << format_pointer(key);
    }

    push_ring(m_recent_bindings, BindingEvent{m_frame, std::string{source}, "SetDescriptorHeaps", detail.str()}, MAX_RECENT_BINDINGS);
    note_frame_warning_if_needed();
}

void D3D12Diagnostics::record_resource_barriers(
    std::string_view source,
    uint32_t count,
    const D3D12_RESOURCE_BARRIER* barriers
) {
    // High-frequency; skip in lightweight mode (Shader Hunter doesn't need barriers).
    if (!is_enabled() || is_lightweight()) {
        return;
    }

    if (barriers == nullptr || count == 0) {
        return;
    }

    std::scoped_lock _{m_mutex};
    m_resource_barriers_this_frame += count;

    for (uint32_t i = 0; i < count; ++i) {
        const auto& barrier = barriers[i];
        BarrierEvent event{};
        event.frame = m_frame;
        event.source = std::string{source};
        event.type = barrier_type_to_string(barrier.Type);

        if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) {
            event.resource = reinterpret_cast<uintptr_t>(barrier.Transition.pResource);
            event.before_state = resource_state_to_string(barrier.Transition.StateBefore);
            event.after_state = resource_state_to_string(barrier.Transition.StateAfter);
            event.subresource = barrier.Transition.Subresource;

            if (barrier.Transition.pResource == nullptr) {
                event.note = "null resource";
                push_warning(source, "Transition barrier with null resource");
            } else if (barrier.Transition.StateBefore == barrier.Transition.StateAfter) {
                event.note = "before == after";
                push_warning(source, "Transition barrier keeps the same before/after state");
            }
        } else if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_ALIASING) {
            event.resource = reinterpret_cast<uintptr_t>(barrier.Aliasing.pResourceAfter);
            event.note = "aliasing";
        } else if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_UAV) {
            event.resource = reinterpret_cast<uintptr_t>(barrier.UAV.pResource);
            event.note = "uav";
        }

        push_ring(m_recent_barriers, std::move(event), MAX_RECENT_BARRIERS);
    }

    note_frame_warning_if_needed();
}

void D3D12Diagnostics::record_rtv_bind(
    std::string_view source,
    uint32_t rtv_count,
    const D3D12_CPU_DESCRIPTOR_HANDLE* rtvs,
    const D3D12_CPU_DESCRIPTOR_HANDLE* dsv
) {
    if (!is_enabled()) {
        return;
    }

    std::scoped_lock _{m_mutex};
    ++m_rtv_binds_this_frame;

    CurrentBindContext context{};
    context.frame = m_frame;
    context.source = std::string{source};
    context.exact_this_frame = true;
    context.render_targets.reserve(rtv_count);

    std::ostringstream detail{};
    detail << "rtv_count=" << rtv_count;

    if (rtvs != nullptr) {
        for (uint32_t i = 0; i < rtv_count; ++i) {
            detail << " rtv[" << i << "]=0x" << std::hex << std::uppercase << rtvs[i].ptr << std::dec;

            BoundTargetInfo target{};
            target.handle = static_cast<uintptr_t>(rtvs[i].ptr);
            target.descriptor_type = "RTV";

            if (const auto it = m_rtv_descriptors.find(target.handle); it != m_rtv_descriptors.end()) {
                target.resource = it->second.resource;
                target.name = it->second.name;
                // The engine may set the debug name after CreateRenderTargetView.
                // Retry the lookup here so subsequent binds see a real name.
                if (target.name.empty() && it->second.resource != 0) {
                    auto dbg = try_resolve_d3d_debug_name(reinterpret_cast<ID3D12Resource*>(it->second.resource));
                    if (!dbg.empty()) {
                        it->second.name = dbg;
                        target.name = dbg;
                        if (auto rit = m_resources.find(it->second.resource); rit != m_resources.end()) {
                            rit->second.name = dbg;
                        }
                    }
                }
            }

            if (target.name.empty()) {
                target.name = format_pointer(target.handle);
            }

            context.render_targets.emplace_back(std::move(target));
        }
    }

    if (dsv != nullptr) {
        detail << " dsv=0x" << std::hex << std::uppercase << dsv->ptr << std::dec;

        BoundTargetInfo target{};
        target.handle = static_cast<uintptr_t>(dsv->ptr);
        target.descriptor_type = "DSV";

        if (const auto it = m_dsv_descriptors.find(target.handle); it != m_dsv_descriptors.end()) {
            target.resource = it->second.resource;
            target.name = it->second.name;
            if (target.name.empty() && it->second.resource != 0) {
                auto dbg = try_resolve_d3d_debug_name(reinterpret_cast<ID3D12Resource*>(it->second.resource));
                if (!dbg.empty()) {
                    it->second.name = dbg;
                    target.name = dbg;
                    if (auto rit = m_resources.find(it->second.resource); rit != m_resources.end()) {
                        rit->second.name = dbg;
                    }
                }
            }
        }

        if (target.name.empty()) {
            target.name = format_pointer(target.handle);
        }

        context.depth_target = std::move(target);
    }

    BindingEvent event{m_frame, std::string{source}, "OMSetRenderTargets", detail.str()};
    event.render_targets = context.render_targets;
    event.depth_target = context.depth_target;
    m_current_bind_context = std::move(context);
    push_ring(m_recent_bindings, std::move(event), MAX_RECENT_BINDINGS);
}

void D3D12Diagnostics::record_root_bind(
    std::string_view source,
    uintptr_t command_list,
    uintptr_t pipeline_state,
    int32_t eye_bucket,
    std::string_view pipeline,
    std::string_view kind,
    uint32_t root_parameter,
    uintptr_t value,
    uint32_t value_count,
    uint64_t value_hash
) {
    // Highest-frequency recorder (runs on every SetGraphics/ComputeRootDescriptorTable
    // and root CBV/SRV/UAV bind). Skip entirely in lightweight mode — the per-call
    // mutex + string work here is the dominant cost when the Shader Hunter is open.
    if (!is_enabled() || is_lightweight()) {
        return;
    }

    std::scoped_lock _{m_mutex};
    ++m_root_binds_this_frame;

    RootBindEvent event{};
    event.frame = m_frame;
    event.sequence = ++m_root_bind_sequence;
    event.source = std::string{source};
    event.pipeline = std::string{pipeline};
    event.kind = std::string{kind};
    event.command_list = command_list;
    event.pipeline_state = pipeline_state;
    event.eye_bucket = eye_bucket;
    event.root_parameter = root_parameter;
    event.value = value;
    event.value_count = value_count;
    event.value_hash = value_hash;

    push_ring(m_recent_root_binds, std::move(event), MAX_RECENT_ROOT_BINDS);
}

void D3D12Diagnostics::record_rtv_write(
    std::string_view source,
    uintptr_t command_list,
    uintptr_t pipeline_state,
    int32_t eye_bucket,
    D3D12_CPU_DESCRIPTOR_HANDLE rtv,
    std::string_view kind
) {
    if (!is_enabled() || rtv.ptr == 0) {
        return;
    }

    std::scoped_lock _{m_mutex};
    const auto descriptor_key = static_cast<uintptr_t>(rtv.ptr);
    uintptr_t resource_key = 0;
    std::string name{};
    if (const auto descriptor = m_rtv_descriptors.find(descriptor_key); descriptor != m_rtv_descriptors.end()) {
        resource_key = descriptor->second.resource;
        name = descriptor->second.name;
    }

    const auto lineage_key = resource_key != 0 ? resource_key : descriptor_key;
    m_last_resource_writes[lineage_key] = ResourceProducerInfo{
        m_frame,
        m_draw_events_this_frame,
        pipeline_state,
        command_list,
        std::string{kind.empty() ? source : kind},
        descriptor_key,
        0,
        eye_bucket,
        std::move(name)
    };
}

void D3D12Diagnostics::record_draw_event(
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
) {
    // Per-draw recorder (builds a full DrawEvent with root-table/CBV/SRV/UAV
    // snapshots). Skip in lightweight mode — the Shader Hunter maintains its own
    // collected map via ShaderOverrideRegistry::hunter_record_draw_event and does
    // not consume D3D12Diagnostics DrawEvents.
    if (!is_enabled() || is_lightweight()) {
        return;
    }

    std::scoped_lock _{m_mutex};

    DrawEvent event{};
    event.frame = m_frame;
    event.draw_index = ++m_draw_events_this_frame;
    event.source = std::string{source};
    event.kind = std::string{kind};
    event.command_list = command_list;
    event.pipeline_state = pipeline_state;
    event.root_signature = root_signature;
    if (event.root_signature == 0) {
        if (const auto pso_root = m_pso_root_signatures.find(pipeline_state); pso_root != m_pso_root_signatures.end()) {
            event.root_signature = pso_root->second;
        }
    }
    event.eye_bucket = eye_bucket;
    event.executed = executed;
    event.has_viewport = has_viewport;
    event.viewport_top_left_x = viewport_top_left_x;
    event.viewport_top_left_y = viewport_top_left_y;
    event.viewport_width = viewport_width;
    event.viewport_height = viewport_height;
    event.viewport_count = viewport_count;
    event.has_scissor = has_scissor;
    event.scissor_left = scissor_left;
    event.scissor_top = scissor_top;
    event.scissor_right = scissor_right;
    event.scissor_bottom = scissor_bottom;
    event.scissor_count = scissor_count;
    event.arg0 = arg0;
    event.arg1 = arg1;
    event.arg2 = arg2;
    event.arg3 = arg3;
    event.arg4 = arg4;
    event.rtv0 = rtv0;

    auto make_write_info = [&](const BoundTargetInfo& target, uint32_t target_index, std::string_view write_kind) {
        ResourceWriteInfo write{};
        write.target_index = target_index;
        write.descriptor = target.handle;
        write.resource = target.resource;
        write.name = target.name;
        write.kind = std::string{write_kind};

        const auto lineage_key = write.resource != 0 ? write.resource : write.descriptor;
        if (const auto producer = m_last_resource_writes.find(lineage_key); producer != m_last_resource_writes.end()) {
            write.prior_producer_frame = producer->second.frame;
            write.prior_producer_draw = producer->second.draw_index;
            write.prior_producer_pso = producer->second.pipeline_state;
            write.prior_producer_command_list = producer->second.command_list;
            write.prior_producer_kind = producer->second.kind;
            write.prior_producer_descriptor = producer->second.descriptor;
            write.prior_producer_target_index = producer->second.target_index;
            write.prior_producer_eye_bucket = producer->second.eye_bucket;
        }

        if (event.executed) {
            m_last_resource_writes[lineage_key] = ResourceProducerInfo{
                event.frame,
                event.draw_index,
                pipeline_state,
                command_list,
                std::string{write_kind},
                write.descriptor,
                target_index,
                eye_bucket,
                write.name
            };
        }

        return write;
    };

    if (m_current_bind_context.has_value() &&
        m_current_bind_context->frame == m_frame &&
        !m_current_bind_context->render_targets.empty()) {
        event.render_target_writes.reserve(m_current_bind_context->render_targets.size());
        for (uint32_t i = 0; i < m_current_bind_context->render_targets.size(); ++i) {
            auto write = make_write_info(m_current_bind_context->render_targets[i], i, "draw_rtv");
            if (i == 0) {
                event.rtv0 = write.descriptor;
                event.rtv0_resource = write.resource;
                event.prior_rtv0_producer_frame = write.prior_producer_frame;
                event.prior_rtv0_producer_draw = write.prior_producer_draw;
                event.prior_rtv0_producer_pso = write.prior_producer_pso;
            }
            event.render_target_writes.emplace_back(std::move(write));
        }
    } else if (rtv0 != 0) {
        BoundTargetInfo target{};
        target.handle = rtv0;
        target.descriptor_type = "RTV";
        if (const auto descriptor = m_rtv_descriptors.find(rtv0); descriptor != m_rtv_descriptors.end()) {
            target.resource = descriptor->second.resource;
            target.name = descriptor->second.name;
        }
        if (target.name.empty()) {
            target.name = format_pointer(rtv0);
        }

        auto write = make_write_info(target, 0, "draw_rtv");
        event.rtv0_resource = write.resource;
        event.prior_rtv0_producer_frame = write.prior_producer_frame;
        event.prior_rtv0_producer_draw = write.prior_producer_draw;
        event.prior_rtv0_producer_pso = write.prior_producer_pso;
        event.render_target_writes.emplace_back(std::move(write));
    }
    event.graphics_root_descriptor_tables = graphics_root_descriptor_tables;
    event.compute_root_descriptor_tables = compute_root_descriptor_tables;
    event.graphics_root_cbvs = graphics_root_cbvs;
    event.compute_root_cbvs = compute_root_cbvs;
    event.graphics_root_srvs = graphics_root_srvs;
    event.compute_root_srvs = compute_root_srvs;
    event.graphics_root_uavs = graphics_root_uavs;
    event.compute_root_uavs = compute_root_uavs;
    event.graphics_root_cbv_hash = graphics_root_cbv_hash;
    event.compute_root_cbv_hash = compute_root_cbv_hash;
    event.graphics_root_constants_hash = graphics_root_constants_hash;
    event.compute_root_constants_hash = compute_root_constants_hash;
    event.graphics_root_descriptor_table_resource_hash = graphics_root_descriptor_table_resource_hash;
    event.compute_root_descriptor_table_resource_hash = compute_root_descriptor_table_resource_hash;

    std::array<bool, MAX_ROOT_BIND_SLOTS> descriptor_table_roots{};
    bool has_root_signature_filter = false;
    if (event.root_signature != 0) {
        if (const auto root_it = m_root_signatures.find(event.root_signature); root_it != m_root_signatures.end()) {
            has_root_signature_filter = true;
            for (const auto& parameter : root_it->second.parameters) {
                if (parameter.index < descriptor_table_roots.size() &&
                    parameter.parameter_type == "descriptor_table") {
                    descriptor_table_roots[parameter.index] = true;
                }
            }
        }
    }

    event.descriptor_reads.reserve(descriptor_reads.size());
    for (const auto& read : descriptor_reads) {
        if (has_root_signature_filter &&
            (read.root_parameter >= descriptor_table_roots.size() ||
             !descriptor_table_roots[read.root_parameter])) {
            continue;
        }

        event.descriptor_reads.emplace_back(read);
    }

    for (auto& read : event.descriptor_reads) {
        if (read.resource == 0) {
            continue;
        }

        if (const auto producer = m_last_resource_writes.find(read.resource); producer != m_last_resource_writes.end()) {
            read.producer_frame = producer->second.frame;
            read.producer_draw = producer->second.draw_index;
            read.producer_pso = producer->second.pipeline_state;
            read.producer_command_list = producer->second.command_list;
            read.producer_kind = producer->second.kind;
            read.producer_descriptor = producer->second.descriptor;
            read.producer_target_index = producer->second.target_index;
            read.producer_eye_bucket = producer->second.eye_bucket;
        }

        if (read.descriptor_type == "UAV") {
            BoundTargetInfo target{};
            target.handle = read.descriptor_cpu;
            target.resource = read.resource;
            target.descriptor_type = "UAV";
            target.name = format_pointer(read.resource);
            auto write = make_write_info(target, read.descriptor_index, kind == "dispatch" ? "dispatch_uav" : "draw_uav");
            event.uav_writes.emplace_back(std::move(write));
        }
    }

    push_ring(m_recent_draw_events, std::move(event), MAX_RECENT_DRAW_EVENTS);
}

void D3D12Diagnostics::record_resource_copy(
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
    uint64_t src_byte_offset
) {
    if (!is_enabled() || (dst_resource == 0 && src_resource == 0)) {
        return;
    }

    std::scoped_lock _{m_mutex};

    DrawEvent event{};
    event.frame = m_frame;
    event.draw_index = ++m_draw_events_this_frame;
    event.source = std::string{source};
    event.kind = std::string{kind};
    event.command_list = command_list;
    event.eye_bucket = -1;
    event.arg0 = dst_subresource;
    event.arg1 = src_subresource;
    event.arg2 = static_cast<uint32_t>((std::min)(byte_count, static_cast<uint64_t>(UINT32_MAX)));
    event.arg3 = static_cast<int32_t>(width);
    event.arg4 = height != 0 ? height : depth;
    event.copy_dst_byte_offset = dst_byte_offset;
    event.copy_src_byte_offset = src_byte_offset;
    event.copy_byte_count = byte_count;

    auto resource_name = [&](uintptr_t resource) {
        if (resource == 0) {
            return std::string{};
        }
        if (const auto it = m_resources.find(resource); it != m_resources.end() && !it->second.name.empty()) {
            return it->second.name;
        }
        auto* resource_ptr = reinterpret_cast<ID3D12Resource*>(resource);
        auto debug_name = try_resolve_d3d_debug_name(resource_ptr);
        if (!debug_name.empty()) {
            return debug_name;
        }
        return format_pointer(resource);
    };

    if (src_resource != 0) {
        DescriptorReadInfo read{};
        read.root_parameter = 0;
        read.descriptor_index = src_subresource;
        read.descriptor_cpu = src_resource;
        read.resource = src_resource;
        read.descriptor_type = "RESOURCE";
        if (const auto producer = m_last_resource_writes.find(src_resource); producer != m_last_resource_writes.end()) {
            read.producer_frame = producer->second.frame;
            read.producer_draw = producer->second.draw_index;
            read.producer_pso = producer->second.pipeline_state;
            read.producer_command_list = producer->second.command_list;
            read.producer_kind = producer->second.kind;
            read.producer_descriptor = producer->second.descriptor;
            read.producer_target_index = producer->second.target_index;
            read.producer_eye_bucket = producer->second.eye_bucket;
        }
        event.descriptor_reads.emplace_back(std::move(read));
    }

    if (dst_resource != 0) {
        ResourceWriteInfo write{};
        write.target_index = dst_subresource;
        write.descriptor = dst_resource;
        write.resource = dst_resource;
        write.name = resource_name(dst_resource);
        write.kind = std::string{kind};

        if (const auto producer = m_last_resource_writes.find(dst_resource); producer != m_last_resource_writes.end()) {
            write.prior_producer_frame = producer->second.frame;
            write.prior_producer_draw = producer->second.draw_index;
            write.prior_producer_pso = producer->second.pipeline_state;
            write.prior_producer_command_list = producer->second.command_list;
            write.prior_producer_kind = producer->second.kind;
            write.prior_producer_descriptor = producer->second.descriptor;
            write.prior_producer_target_index = producer->second.target_index;
            write.prior_producer_eye_bucket = producer->second.eye_bucket;
        }

        m_last_resource_writes[dst_resource] = ResourceProducerInfo{
            event.frame,
            event.draw_index,
            0,
            command_list,
            std::string{kind},
            dst_resource,
            dst_subresource,
            -1,
            write.name
        };

        event.render_target_writes.emplace_back(std::move(write));
        event.rtv0_resource = dst_resource;
    }

    push_ring(m_recent_draw_events, std::move(event), MAX_RECENT_DRAW_EVENTS);
}

void D3D12Diagnostics::record_extra_descriptor_read(
    std::string_view source,
    uintptr_t command_list,
    uintptr_t pipeline_state,
    DescriptorReadInfo read
) {
    if (!is_enabled() || command_list == 0 || read.resource == 0) {
        return;
    }

    std::scoped_lock _{m_mutex};

    const auto fill_producer = [this](DescriptorReadInfo& candidate) {
        if (candidate.resource == 0) {
            return;
        }

        if (const auto producer = m_last_resource_writes.find(candidate.resource); producer != m_last_resource_writes.end()) {
            candidate.producer_frame = producer->second.frame;
            candidate.producer_draw = producer->second.draw_index;
            candidate.producer_pso = producer->second.pipeline_state;
            candidate.producer_command_list = producer->second.command_list;
            candidate.producer_kind = producer->second.kind;
            candidate.producer_descriptor = producer->second.descriptor;
            candidate.producer_target_index = producer->second.target_index;
            candidate.producer_eye_bucket = producer->second.eye_bucket;
        }
    };

    fill_producer(read);

    for (auto it = m_recent_draw_events.rbegin(); it != m_recent_draw_events.rend(); ++it) {
        if (it->frame != m_frame) {
            break;
        }

        if (it->command_list != command_list) {
            continue;
        }

        if (pipeline_state != 0 && it->pipeline_state != 0 && it->pipeline_state != pipeline_state) {
            continue;
        }

        const auto duplicate = std::any_of(
            it->descriptor_reads.begin(),
            it->descriptor_reads.end(),
            [&read](const auto& existing) {
                return existing.root_parameter == read.root_parameter &&
                       existing.descriptor_index == read.descriptor_index &&
                       existing.descriptor_cpu == read.descriptor_cpu &&
                       existing.resource == read.resource;
            });
        if (!duplicate) {
            it->descriptor_reads.emplace_back(std::move(read));
        }
        return;
    }

    push_warning(source, "extra descriptor read could not be matched to a recent draw event");
}

std::optional<D3D12Diagnostics::DescriptorReadInfo> D3D12Diagnostics::resolve_descriptor_read(
    uint32_t root_parameter,
    uint32_t descriptor_index,
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor
) const {
    if (descriptor.ptr == 0) {
        return std::nullopt;
    }

    std::scoped_lock _{m_mutex};
    const auto key = static_cast<uintptr_t>(descriptor.ptr);

    auto make = [&](const DescriptorInfo& tracked) {
        DescriptorReadInfo info{};
        info.root_parameter = root_parameter;
        info.descriptor_index = descriptor_index;
        info.descriptor_cpu = key;
        info.descriptor_source_cpu = tracked.source_handle;
        info.descriptor_source_frame = tracked.source_frame;
        info.resource = tracked.resource;
        info.descriptor_type = tracked.descriptor_type;
        return info;
    };

    if (const auto it = m_srv_descriptors.find(key); it != m_srv_descriptors.end()) {
        return make(it->second);
    }

    if (const auto it = m_uav_descriptors.find(key); it != m_uav_descriptors.end()) {
        return make(it->second);
    }

    return std::nullopt;
}

std::optional<D3D12Diagnostics::ResourceProducerSnapshot> D3D12Diagnostics::last_resource_producer(uintptr_t resource) const {
    if (resource == 0) {
        return std::nullopt;
    }

    std::scoped_lock _{m_mutex};
    const auto it = m_last_resource_writes.find(resource);
    if (it == m_last_resource_writes.end()) {
        return std::nullopt;
    }

    ResourceProducerSnapshot out{};
    out.frame = it->second.frame;
    out.draw_index = it->second.draw_index;
    out.pipeline_state = it->second.pipeline_state;
    out.command_list = it->second.command_list;
    out.kind = it->second.kind;
    out.descriptor = it->second.descriptor;
    out.target_index = it->second.target_index;
    out.eye_bucket = it->second.eye_bucket;
    out.name = it->second.name;
    return out;
}

void D3D12Diagnostics::record_gpu_timing_sample(
    std::string_view source,
    uintptr_t pipeline_state,
    int32_t eye_bucket,
    double milliseconds
) {
    if (!is_enabled() || pipeline_state == 0 || milliseconds < 0.0) {
        return;
    }

    std::scoped_lock _{m_mutex};
    std::ostringstream key{};
    key << pipeline_state << ':' << eye_bucket << ':' << source;

    auto& aggregate = m_gpu_timings[key.str()];
    if (aggregate.samples == 0) {
        aggregate.pipeline_state = pipeline_state;
        aggregate.kind = std::string{source};
        aggregate.eye_bucket = eye_bucket;
    }

    ++aggregate.samples;
    aggregate.total_ms += milliseconds;
    aggregate.max_ms = (std::max)(aggregate.max_ms, milliseconds);
    aggregate.last_frame = m_frame;
}

void D3D12Diagnostics::record_pipeline_cache_event(
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
    std::string_view note
) {
    if (!is_enabled()) {
        return;
    }

    std::scoped_lock _{m_mutex};

    PipelineCacheEvent event{};
    event.frame = m_frame;
    event.source = std::string{source};
    event.action = std::string{action};
    event.device = device;
    event.library = library;
    event.pipeline_state = pipeline_state;
    event.name = std::string{name};
    event.cached_blob_size = cached_blob_size;
    event.has_cached_pso = has_cached_pso;
    event.stripped_cached_pso = stripped_cached_pso;
    event.result = result;
    event.note = std::string{note};

    push_ring(m_recent_pipeline_cache_events, std::move(event), MAX_RECENT_PIPELINE_CACHE_EVENTS);
}

D3D12Diagnostics::Snapshot D3D12Diagnostics::snapshot() const {
    if (!is_enabled()) {
        return {};
    }

    std::scoped_lock _{m_mutex};

    Snapshot out{};
    out.available = m_device != 0 || m_swapchain != 0;
    out.frame = m_frame;
    out.device = m_device;
    out.swapchain = m_swapchain;
    out.command_queue = m_command_queue;
    out.render_width = m_render_width;
    out.render_height = m_render_height;
    out.display_width = m_display_width;
    out.display_height = m_display_height;
    out.proton_swapchain = m_proton_swapchain;
    out.framegen_swapchain = m_framegen_swapchain;
    out.active_cbv_srv_uav_heap = m_active_cbv_srv_uav_heap;
    out.active_sampler_heap = m_active_sampler_heap;
    out.descriptor_heap_sets_this_frame = m_descriptor_heap_sets_this_frame;
    out.descriptor_heap_switches_this_frame = m_descriptor_heap_switches_this_frame;
    out.resource_barriers_this_frame = m_resource_barriers_this_frame;
    out.rtv_binds_this_frame = m_rtv_binds_this_frame;
    out.root_binds_this_frame = m_root_binds_this_frame;
    out.draw_events_this_frame = m_draw_events_this_frame;
    out.transient_heap_creations_this_frame = m_transient_heap_creations_this_frame;
    out.transient_resource_creations_this_frame = m_transient_resource_creations_this_frame;
    out.transient_resource_bytes_this_frame = m_transient_resource_bytes_this_frame;
    out.current_bind_context = m_current_bind_context;

    out.heaps.reserve(m_heaps.size());
    for (const auto& [_, heap] : m_heaps) {
        out.heaps.emplace_back(heap);
    }

    std::sort(out.heaps.begin(), out.heaps.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.is_active != rhs.is_active) {
            return lhs.is_active > rhs.is_active;
        }
        if (lhs.last_seen_frame != rhs.last_seen_frame) {
            return lhs.last_seen_frame > rhs.last_seen_frame;
        }
        return lhs.name < rhs.name;
    });

    out.root_signatures.reserve(m_root_signatures.size());
    for (const auto& [_, root_signature] : m_root_signatures) {
        out.root_signatures.emplace_back(root_signature);
    }

    std::sort(out.root_signatures.begin(), out.root_signatures.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.last_seen_frame != rhs.last_seen_frame) {
            return lhs.last_seen_frame > rhs.last_seen_frame;
        }
        return lhs.pointer < rhs.pointer;
    });

    out.recent_bindings = m_recent_bindings;
    out.recent_root_binds = m_recent_root_binds;
    out.recent_draw_events = m_recent_draw_events;
    out.gpu_timings.reserve(m_gpu_timings.size());
    for (const auto& [_, aggregate] : m_gpu_timings) {
        GpuTimingInfo info{};
        info.pipeline_state = aggregate.pipeline_state;
        info.kind = aggregate.kind;
        info.eye_bucket = aggregate.eye_bucket;
        info.samples = aggregate.samples;
        info.avg_ms = aggregate.samples > 0 ? aggregate.total_ms / static_cast<double>(aggregate.samples) : 0.0;
        info.max_ms = aggregate.max_ms;
        info.last_frame = aggregate.last_frame;
        out.gpu_timings.emplace_back(std::move(info));
    }
    std::sort(out.gpu_timings.begin(), out.gpu_timings.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.last_frame != rhs.last_frame) {
            return lhs.last_frame > rhs.last_frame;
        }
        if (lhs.max_ms != rhs.max_ms) {
            return lhs.max_ms > rhs.max_ms;
        }
        return lhs.pipeline_state < rhs.pipeline_state;
    });
    if (out.gpu_timings.size() > 128) {
        out.gpu_timings.resize(128);
    }
    out.recent_pipeline_cache_events = m_recent_pipeline_cache_events;
    out.recent_barriers = m_recent_barriers;
    out.recent_warnings = m_recent_warnings;

    for (const auto& [_, resource] : m_resources) {
        out.tracked_resource_bytes_total += resource.approx_bytes;
        if (resource.transient) {
            out.tracked_transient_resource_bytes_total += resource.approx_bytes;
        }
    }

    return out;
}

std::optional<D3D12Diagnostics::CurrentBindContext> D3D12Diagnostics::current_bind_context() const {
    if (!is_enabled()) {
        return std::nullopt;
    }

    std::scoped_lock _{m_mutex};
    return m_current_bind_context;
}

std::optional<D3D12Diagnostics::RootSignatureInfo> D3D12Diagnostics::root_signature_for_pipeline(uintptr_t pipeline_state) const {
    if (pipeline_state == 0) {
        return std::nullopt;
    }

    std::scoped_lock _{m_mutex};
    const auto root_it = m_pso_root_signatures.find(pipeline_state);
    if (root_it == m_pso_root_signatures.end() || root_it->second == 0) {
        return std::nullopt;
    }

    const auto info_it = m_root_signatures.find(root_it->second);
    if (info_it == m_root_signatures.end()) {
        return std::nullopt;
    }

    return info_it->second;
}

std::optional<D3D12Diagnostics::RootSignatureInfo> D3D12Diagnostics::root_signature(uintptr_t root_signature) const {
    if (root_signature == 0) {
        return std::nullopt;
    }

    std::scoped_lock _{m_mutex};
    const auto info_it = m_root_signatures.find(root_signature);
    if (info_it == m_root_signatures.end()) {
        return std::nullopt;
    }

    return info_it->second;
}

void D3D12Diagnostics::reset() {
    std::scoped_lock _{m_mutex};
    clear_state_locked();
}

void D3D12Diagnostics::clear_state_locked() {
    m_heaps.clear();
    m_resources.clear();
    m_rtv_descriptors.clear();
    m_dsv_descriptors.clear();
    m_srv_descriptors.clear();
    m_uav_descriptors.clear();
    m_last_resource_writes.clear();
    m_gpu_timings.clear();
    m_recent_bindings.clear();
    m_recent_root_binds.clear();
    m_recent_draw_events.clear();
    m_recent_pipeline_cache_events.clear();
    m_recent_barriers.clear();
    m_recent_warnings.clear();
    m_current_bind_context.reset();
    m_frame = 0;
    m_device = 0;
    m_swapchain = 0;
    m_command_queue = 0;
    m_render_width = 0;
    m_render_height = 0;
    m_display_width = 0;
    m_display_height = 0;
    m_proton_swapchain = false;
    m_framegen_swapchain = false;
    m_active_cbv_srv_uav_heap = 0;
    m_active_sampler_heap = 0;
    m_descriptor_heap_sets_this_frame = 0;
    m_descriptor_heap_switches_this_frame = 0;
    m_resource_barriers_this_frame = 0;
    m_rtv_binds_this_frame = 0;
    m_root_binds_this_frame = 0;
    m_draw_events_this_frame = 0;
    m_root_bind_sequence = 0;
    m_transient_heap_creations_this_frame = 0;
    m_transient_resource_creations_this_frame = 0;
    m_transient_resource_bytes_this_frame = 0;
}

void D3D12Diagnostics::push_warning(std::string_view source, std::string message) {
    push_ring(m_recent_warnings, WarningEvent{m_frame, std::string{source}, std::move(message)}, MAX_RECENT_WARNINGS);
}

void D3D12Diagnostics::note_frame_warning_if_needed() {
    if (m_descriptor_heap_switches_this_frame == 32) {
        push_warning("Frame", "Descriptor heap switching exceeded 32 changes this frame");
    }

    if (m_transient_heap_creations_this_frame == 16) {
        push_warning("Frame", "Transient heap creation exceeded 16 heaps this frame");
    }

    if (m_transient_resource_bytes_this_frame > (256ull * 1024ull * 1024ull)) {
        push_warning("Frame", "Transient DX12 resource bytes exceeded 256 MB this frame");
    }
}
} // namespace render
