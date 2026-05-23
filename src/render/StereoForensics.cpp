#include "render/StereoForensics.hpp"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "render/ShaderOverrideRegistry.hpp"
#include "render/StereoEye.hpp"

using json = nlohmann::json;

namespace {

bool should_log_forensics_exception(std::string_view api, uint64_t* out_count = nullptr) noexcept {
    try {
        static std::mutex mutex;
        static std::unordered_map<std::string, uint64_t> counts;
        uint64_t count = 0;
        {
            std::scoped_lock _{mutex};
            count = ++counts[std::string{api}];
        }
        if (out_count != nullptr) {
            *out_count = count;
        }
        return count <= 8 || (count % 256) == 0;
    } catch (...) {
        return false;
    }
}

void log_forensics_exception(std::string_view api, const std::exception& e) noexcept {
    try {
        uint64_t count = 0;
        if (should_log_forensics_exception(api, &count)) {
            SPDLOG_WARN("[StereoForensics] {} failed; diagnostics event dropped: {} (count={})", api, e.what(), count);
        }
    } catch (...) {
    }
}

void log_forensics_exception(std::string_view api) noexcept {
    try {
        uint64_t count = 0;
        if (should_log_forensics_exception(api, &count)) {
            SPDLOG_WARN("[StereoForensics] {} failed with unknown exception; diagnostics event dropped (count={})", api, count);
        }
    } catch (...) {
    }
}

#define STEREO_FORENSICS_TRY(api_name) try
#define STEREO_FORENSICS_CATCH_VOID(api_name) \
    catch (const std::exception& e) { \
        log_forensics_exception((api_name), e); \
        return; \
    } catch (...) { \
        log_forensics_exception((api_name)); \
        return; \
    }
#define STEREO_FORENSICS_CATCH_RETURN(api_name, fallback_value) \
    catch (const std::exception& e) { \
        log_forensics_exception((api_name), e); \
        return (fallback_value); \
    } catch (...) { \
        log_forensics_exception((api_name)); \
        return (fallback_value); \
    }

bool env_truthy(const char* name) {
    char value[32]{};
    const auto len = GetEnvironmentVariableA(name, value, static_cast<DWORD>(sizeof(value)));
    return len > 0 && len < sizeof(value) && value[0] != '\0' && value[0] != '0';
}

std::string env_string(const char* name, std::string fallback = {}) {
    const DWORD len = GetEnvironmentVariableA(name, nullptr, 0);
    if (len == 0) {
        return fallback;
    }
    std::string out(len, '\0');
    const DWORD copied = GetEnvironmentVariableA(name, out.data(), len);
    if (copied == 0 || copied >= len) {
        return fallback;
    }
    out.resize(copied);
    return out.empty() ? fallback : out;
}

uint64_t env_u64(const char* name, uint64_t fallback) {
    const auto value = env_string(name);
    if (value.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const auto parsed = std::strtoull(value.c_str(), &end, 0);
    return end != value.c_str() ? parsed : fallback;
}

template <typename T>
T json_value_or(const json& object, const char* key, T fallback) {
    if (!object.is_object()) {
        return fallback;
    }
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return fallback;
    }
    try {
        return it->get<T>();
    } catch (...) {
        return fallback;
    }
}

std::string hex_u64(uint64_t value) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::uppercase << value;
    return ss.str();
}

json ptr_json(uint64_t value) {
    return json{{"value", value}, {"hex", hex_u64(value)}};
}

std::string event_uid(uint64_t frame, uint64_t index) {
    return "frame:" + std::to_string(frame) + "/event:" + std::to_string(index);
}

std::string resource_uid(uint64_t resource) {
    return resource == 0 ? std::string{} : ("resource:" + hex_u64(resource));
}

std::string resource_instance_uid(uint64_t resource, uint64_t generation) {
    if (resource == 0) {
        return {};
    }
    return resource_uid(resource) + "#gen:" + std::to_string(std::max<uint64_t>(1, generation));
}

bool is_resource_create_source(std::string_view source) {
    return source == "D3D12Hook::CreateCommittedResource" ||
        source == "D3D12Hook::CreatePlacedResource";
}

std::string descriptor_view_uid(const std::string& view_key) {
    return view_key.empty() ? std::string{} : ("view:" + view_key);
}

const char* register_prefix_for_range_type(std::string_view type) {
    if (type == "srv") {
        return "t";
    }
    if (type == "uav") {
        return "u";
    }
    if (type == "cbv") {
        return "b";
    }
    if (type == "sampler") {
        return "s";
    }
    return "";
}

std::string pso_uid(uint64_t pipeline_state) {
    return pipeline_state == 0 ? std::string{} : ("pso:" + hex_u64(pipeline_state));
}

json shader_uid_json(uint32_t vs_crc, uint32_t ps_crc, uint32_t gs_crc, uint32_t cs_crc) {
    json out = json::object();
    if (vs_crc != 0) {
        out["vs"] = "vs:" + hex_u64(vs_crc);
    }
    if (ps_crc != 0) {
        out["ps"] = "ps:" + hex_u64(ps_crc);
    }
    if (gs_crc != 0) {
        out["gs"] = "gs:" + hex_u64(gs_crc);
    }
    if (cs_crc != 0) {
        out["cs"] = "cs:" + hex_u64(cs_crc);
    }
    return out;
}

std::string shader_key(uint32_t vs_crc, uint32_t ps_crc, uint32_t gs_crc, uint32_t cs_crc, uint64_t root_signature_hash) {
    std::ostringstream ss;
    ss << "rs:" << hex_u64(root_signature_hash)
       << "|vs:" << hex_u64(vs_crc)
       << "|ps:" << hex_u64(ps_crc)
       << "|gs:" << hex_u64(gs_crc)
       << "|cs:" << hex_u64(cs_crc);
    return ss.str();
}

uint64_t root_signature_hash_for(uintptr_t root_signature, uintptr_t pipeline_state) {
    auto& diagnostics = render::D3D12Diagnostics::get();
    if (root_signature != 0) {
        if (const auto info = diagnostics.root_signature(root_signature); info.has_value()) {
            return info->blob_hash;
        }
    }
    if (pipeline_state != 0) {
        if (const auto info = diagnostics.root_signature_for_pipeline(pipeline_state); info.has_value()) {
            return info->blob_hash;
        }
    }
    return 0;
}

std::string heap_type_name(D3D12_HEAP_TYPE type) {
    switch (type) {
    case D3D12_HEAP_TYPE_DEFAULT: return "default";
    case D3D12_HEAP_TYPE_UPLOAD: return "upload";
    case D3D12_HEAP_TYPE_READBACK: return "readback";
    case D3D12_HEAP_TYPE_CUSTOM: return "custom";
    default: return "unknown";
    }
}

std::string descriptor_heap_type_name(D3D12_DESCRIPTOR_HEAP_TYPE type) {
    switch (type) {
    case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV: return "cbv_srv_uav";
    case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER: return "sampler";
    case D3D12_DESCRIPTOR_HEAP_TYPE_RTV: return "rtv";
    case D3D12_DESCRIPTOR_HEAP_TYPE_DSV: return "dsv";
    default: return "unknown";
    }
}

std::string resource_dimension_name(D3D12_RESOURCE_DIMENSION dim) {
    switch (dim) {
    case D3D12_RESOURCE_DIMENSION_BUFFER: return "buffer";
    case D3D12_RESOURCE_DIMENSION_TEXTURE1D: return "texture1d";
    case D3D12_RESOURCE_DIMENSION_TEXTURE2D: return "texture2d";
    case D3D12_RESOURCE_DIMENSION_TEXTURE3D: return "texture3d";
    default: return "unknown";
    }
}

std::string descriptor_kind_name(render::StereoForensics::DescriptorKind kind) {
    using Kind = render::StereoForensics::DescriptorKind;
    switch (kind) {
    case Kind::CBV: return "CBV";
    case Kind::SRV: return "SRV";
    case Kind::UAV: return "UAV";
    case Kind::RTV: return "RTV";
    case Kind::DSV: return "DSV";
    default: return "Unknown";
    }
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
        return 8;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_D32_FLOAT:
        return 4;
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_UINT:
        return 2;
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_A8_UNORM:
        return 1;
    default:
        return 4;
    }
}

uint64_t approx_resource_bytes(const D3D12_RESOURCE_DESC& desc) {
    if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
        return desc.Width;
    }
    const auto height = std::max<UINT>(1, desc.Height);
    const auto depth_or_array = std::max<UINT16>(1, desc.DepthOrArraySize);
    const auto samples = std::max<UINT>(1, desc.SampleDesc.Count);
    return desc.Width * static_cast<uint64_t>(height) * depth_or_array * samples * bytes_per_pixel(desc.Format);
}

json resource_desc_json(const D3D12_RESOURCE_DESC& desc) {
    return {
        {"dimension", resource_dimension_name(desc.Dimension)},
        {"dimension_id", static_cast<uint32_t>(desc.Dimension)},
        {"width", desc.Width},
        {"height", desc.Height},
        {"depth_or_array_size", desc.DepthOrArraySize},
        {"mip_levels", desc.MipLevels},
        {"format", static_cast<uint32_t>(desc.Format)},
        {"sample_count", desc.SampleDesc.Count},
        {"sample_quality", desc.SampleDesc.Quality},
        {"layout", static_cast<uint32_t>(desc.Layout)},
        {"flags", static_cast<uint32_t>(desc.Flags)},
        {"approx_bytes", approx_resource_bytes(desc)}
    };
}

std::string resource_desc_key(const json& resource) {
    const auto desc = resource.value("desc", json::object());
    std::ostringstream ss;
    ss << desc.value("dimension_id", 0u) << ':'
       << desc.value("width", 0ull) << 'x'
       << desc.value("height", 0u) << 'x'
       << desc.value("depth_or_array_size", 0u) << ':'
       << desc.value("format", 0u) << ':'
       << desc.value("sample_count", 0u) << ':'
       << desc.value("flags", 0u);
    return ss.str();
}

std::string try_debug_name(ID3D12Object* object) {
    if (object == nullptr) {
        return {};
    }

    UINT bytes = 0;
    if (SUCCEEDED(object->GetPrivateData(WKPDID_D3DDebugObjectNameW, &bytes, nullptr)) &&
        bytes > sizeof(wchar_t) && bytes < 4096) {
        std::wstring wide(bytes / sizeof(wchar_t), L'\0');
        if (SUCCEEDED(object->GetPrivateData(WKPDID_D3DDebugObjectNameW, &bytes, wide.data()))) {
            while (!wide.empty() && wide.back() == L'\0') {
                wide.pop_back();
            }
            if (!wide.empty()) {
                const int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
                std::string out(len, '\0');
                WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), out.data(), len, nullptr, nullptr);
                return out;
            }
        }
    }

    bytes = 0;
    if (SUCCEEDED(object->GetPrivateData(WKPDID_D3DDebugObjectName, &bytes, nullptr)) &&
        bytes > 1 && bytes < 2048) {
        std::string out(bytes, '\0');
        if (SUCCEEDED(object->GetPrivateData(WKPDID_D3DDebugObjectName, &bytes, out.data()))) {
            while (!out.empty() && out.back() == '\0') {
                out.pop_back();
            }
            return out;
        }
    }

    return {};
}

uint64_t parse_crc_value(const json& value) {
    if (value.is_number_unsigned()) {
        return value.get<uint64_t>();
    }
    if (value.is_string()) {
        const auto s = value.get<std::string>();
        char* end = nullptr;
        const auto parsed = std::strtoull(s.c_str(), &end, 0);
        return end != s.c_str() ? parsed : 0;
    }
    return 0;
}

int32_t parse_eye_value(const json& value) {
    if (value.is_number_integer()) {
        return render::canonicalize_stereo_eye_bucket(
            value.get<int32_t>(),
            render::kStereoEyeAny);
    }
    if (!value.is_string()) {
        return render::kStereoEyeAny;
    }
    return render::parse_stereo_eye_bucket(
        value.get<std::string>(),
        render::kStereoEyeAny);
}

uint64_t json_u64(const json& value, const char* key, uint64_t fallback = 0) {
    if (!value.contains(key)) {
        return fallback;
    }
    const auto& v = value.at(key);
    if (v.is_number_unsigned()) {
        return v.get<uint64_t>();
    }
    if (v.is_number_integer()) {
        return static_cast<uint64_t>(v.get<int64_t>());
    }
    return fallback;
}

std::string rounded_viewport_key(const json& e) {
    const auto vp = e.value("viewport", json::object());
    const auto w = static_cast<int>(std::lround(vp.value("width", 0.0)));
    const auto h = static_cast<int>(std::lround(vp.value("height", 0.0)));
    return std::to_string(w) + "x" + std::to_string(h);
}

json nonzero_roots_json(const render::D3D12Diagnostics::RootSlotArray& values) {
    json out = json::array();
    for (size_t i = 0; i < values.size(); ++i) {
        if (values[i] != 0) {
            out.push_back({{"root", i}, {"value", values[i]}, {"hex", hex_u64(values[i])}});
        }
    }
    return out;
}

json nonzero_hashes_json(const render::D3D12Diagnostics::RootHashArray& values) {
    json out = json::array();
    for (size_t i = 0; i < values.size(); ++i) {
        if (values[i] != 0) {
            out.push_back({{"root", i}, {"hash", values[i]}, {"hex", hex_u64(values[i])}});
        }
    }
    return out;
}

std::unordered_map<uint32_t, uint64_t> root_hash_map(const json& roots) {
    std::unordered_map<uint32_t, uint64_t> out;
    if (!roots.is_array()) {
        return out;
    }
    for (const auto& item : roots) {
        out[item.value("root", 0u)] = item.value("hash", 0ull);
    }
    return out;
}

json root_signature_layout_json(uintptr_t root_signature, uintptr_t pipeline_state) {
    auto& diagnostics = render::D3D12Diagnostics::get();
    auto info = diagnostics.root_signature(root_signature);
    if (!info.has_value() && pipeline_state != 0) {
        info = diagnostics.root_signature_for_pipeline(pipeline_state);
    }
    if (!info.has_value()) {
        return json::object();
    }

    json parameters = json::array();
    for (const auto& parameter : info->parameters) {
        json ranges = json::array();
        uint32_t append_offset = 0;
        for (const auto& range : parameter.ranges) {
            const bool appends = range.offset_from_table_start == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            const auto effective_offset = appends ? append_offset : range.offset_from_table_start;
            ranges.push_back({
                {"type", range.type},
                {"base_shader_register", range.base_shader_register},
                {"num_descriptors", range.num_descriptors},
                {"register_space", range.register_space},
                {"offset_from_table_start", range.offset_from_table_start},
                {"effective_offset_from_table_start", effective_offset},
                {"appended", appends}
            });
            if (range.num_descriptors == UINT_MAX) {
                append_offset = UINT_MAX;
            } else if (append_offset != UINT_MAX) {
                append_offset = effective_offset + range.num_descriptors;
            }
        }

        parameters.push_back({
            {"index", parameter.index},
            {"parameter_type", parameter.parameter_type},
            {"visibility", parameter.visibility},
            {"shader_register", parameter.shader_register},
            {"register_space", parameter.register_space},
            {"num_32bit_values", parameter.num_32bit_values},
            {"ranges", std::move(ranges)}
        });
    }

    return {
        {"root_signature", info->pointer},
        {"root_signature_hex", hex_u64(info->pointer)},
        {"root_signature_hash", info->blob_hash},
        {"root_signature_hash_hex", hex_u64(info->blob_hash)},
        {"version", info->version},
        {"flags", info->flags},
        {"static_sampler_count", info->static_sampler_count},
        {"parameter_count", parameters.size()},
        {"parameters", std::move(parameters)}
    };
}

json root_binding_json(uintptr_t root_signature, uintptr_t pipeline_state, uint32_t root_parameter, uint32_t descriptor_index) {
    auto& diagnostics = render::D3D12Diagnostics::get();
    auto info = diagnostics.root_signature(root_signature);
    if (!info.has_value() && pipeline_state != 0) {
        info = diagnostics.root_signature_for_pipeline(pipeline_state);
    }
    if (!info.has_value()) {
        return json::object();
    }

    for (const auto& parameter : info->parameters) {
        if (parameter.index != root_parameter) {
            continue;
        }

        json out{
            {"root", root_parameter},
            {"root_signature_hash", info->blob_hash},
            {"root_signature_hash_hex", hex_u64(info->blob_hash)},
            {"parameter_type", parameter.parameter_type},
            {"visibility", parameter.visibility},
            {"register_space", parameter.register_space}
        };

        if (parameter.parameter_type == "descriptor_table") {
            uint32_t append_offset = 0;
            json ranges = json::array();
            for (const auto& range : parameter.ranges) {
                const bool appends = range.offset_from_table_start == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                const auto effective_offset = appends ? append_offset : range.offset_from_table_start;
                const uint64_t range_end = range.num_descriptors == UINT_MAX
                    ? UINT64_MAX
                    : static_cast<uint64_t>(effective_offset) + range.num_descriptors;
                const bool in_range = descriptor_index >= effective_offset &&
                    static_cast<uint64_t>(descriptor_index) < range_end;

                ranges.push_back({
                    {"type", range.type},
                    {"base_shader_register", range.base_shader_register},
                    {"num_descriptors", range.num_descriptors},
                    {"register_space", range.register_space},
                    {"offset_from_table_start", range.offset_from_table_start},
                    {"effective_offset_from_table_start", effective_offset},
                    {"appended", appends},
                    {"contains_descriptor_index", in_range}
                });

                if (in_range) {
                    const auto relative = descriptor_index - effective_offset;
                    const auto shader_register = range.base_shader_register + relative;
                    out["binding_type"] = range.type;
                    out["descriptor_table_offset"] = descriptor_index;
                    out["range_offset"] = effective_offset;
                    out["range_relative_index"] = relative;
                    out["shader_register"] = shader_register;
                    out["register_space"] = range.register_space;
                    const auto* prefix = register_prefix_for_range_type(range.type);
                    if (*prefix != '\0') {
                        out["shader_register_name"] = std::string{prefix} + std::to_string(shader_register);
                    }
                }

                if (range.num_descriptors == UINT_MAX) {
                    append_offset = UINT_MAX;
                } else if (append_offset != UINT_MAX) {
                    append_offset = effective_offset + range.num_descriptors;
                }
            }
            out["ranges"] = std::move(ranges);
            return out;
        }

        out["binding_type"] = parameter.parameter_type;
        out["shader_register"] = parameter.shader_register;
        if (parameter.parameter_type == "cbv") {
            out["shader_register_name"] = "b" + std::to_string(parameter.shader_register);
        } else if (parameter.parameter_type == "srv") {
            out["shader_register_name"] = "t" + std::to_string(parameter.shader_register);
        } else if (parameter.parameter_type == "uav") {
            out["shader_register_name"] = "u" + std::to_string(parameter.shader_register);
        }
        return out;
    }

    return json{{"root", root_parameter}, {"missing_root_parameter", true}};
}

std::string descriptor_view_key(const json& descriptor, uint64_t fallback_resource = 0, std::string_view fallback_kind = {}) {
    const auto resource = json_value_or<uint64_t>(descriptor, "resource", fallback_resource);
    const auto kind = json_value_or<std::string>(descriptor, "kind", std::string{fallback_kind});
    const auto format = json_value_or<uint32_t>(descriptor, "format", 0u);
    const auto view_dimension = json_value_or<uint32_t>(descriptor, "view_dimension", 0u);
    const auto mip = descriptor.is_object() && descriptor.contains("most_detailed_mip")
        ? json_value_or<uint32_t>(descriptor, "most_detailed_mip", 0u)
        : json_value_or<uint32_t>(descriptor, "mip_slice", 0u);
    const auto mip_levels = json_value_or<uint32_t>(descriptor, "mip_levels", 0u);
    const auto first_array_slice = json_value_or<uint32_t>(descriptor, "first_array_slice", 0u);
    const auto array_size = json_value_or<uint32_t>(descriptor, "array_size", 0u);
    const auto plane_slice = json_value_or<uint32_t>(descriptor, "plane_slice", 0u);
    const auto first_w_slice = json_value_or<uint32_t>(descriptor, "first_w_slice", 0u);
    const auto w_size = json_value_or<uint32_t>(descriptor, "w_size", 0u);
    const auto first_element = json_value_or<uint64_t>(descriptor, "first_element", 0ull);
    const auto num_elements = json_value_or<uint32_t>(descriptor, "num_elements", 0u);
    const auto buffer_location = json_value_or<uint64_t>(descriptor, "buffer_location", 0ull);
    const auto size_in_bytes = json_value_or<uint32_t>(descriptor, "size_in_bytes", 0u);

    std::ostringstream ss;
    ss << hex_u64(resource) << '|' << kind
       << "|fmt:" << format
       << "|dim:" << view_dimension
       << "|mip:" << mip << '+' << mip_levels
       << "|slice:" << first_array_slice << '+' << array_size
       << "|plane:" << plane_slice
       << "|w:" << first_w_slice << '+' << w_size
       << "|elem:" << first_element << '+' << num_elements
       << "|cb:" << hex_u64(buffer_location) << '+' << size_in_bytes;
    return ss.str();
}

std::string descriptor_read_signature(const json& reads) {
    std::vector<std::string> parts;
    if (!reads.is_array()) {
        return {};
    }
    for (const auto& read : reads) {
        std::ostringstream ss;
        const auto desc = read.value("descriptor", json::object());
        ss << read.value("root", 0u) << ':'
           << read.value("slot", 0u) << ':'
           << read.value("descriptor_type", std::string{}) << ':'
           << desc.value("resource_desc_key", std::string{}) << ':'
           << read.value("view_key", desc.value("view_key", std::string{}));
        parts.push_back(ss.str());
    }
    std::sort(parts.begin(), parts.end());
    std::ostringstream out;
    for (const auto& part : parts) {
        out << part << ';';
    }
    return out.str();
}

uint32_t pair_score(const json& left, const json& right, std::vector<std::string>* reasons = nullptr) {
    uint32_t score = 0;
    auto add = [&](uint32_t points, std::string reason) {
        score += points;
        if (reasons != nullptr) {
            reasons->push_back(std::move(reason));
        }
    };

    if (left.value("kind", std::string{}) == right.value("kind", std::string{})) {
        add(10, "kind");
    }
    const auto left_rs_hash = left.value("root_signature_hash", 0ull);
    const auto right_rs_hash = right.value("root_signature_hash", 0ull);
    if ((left_rs_hash != 0 && left_rs_hash == right_rs_hash) ||
        (left_rs_hash == 0 && right_rs_hash == 0 && left.value("root_signature", 0ull) == right.value("root_signature", 0ull))) {
        add(15, left_rs_hash != 0 ? "root_signature_hash" : "root_signature_pointer");
    }
    if (rounded_viewport_key(left) == rounded_viewport_key(right)) {
        add(10, "viewport");
    }
    if (left.value("arg0", 0u) == right.value("arg0", 0u) &&
        left.value("arg1", 0u) == right.value("arg1", 0u) &&
        left.value("arg2", 0u) == right.value("arg2", 0u)) {
        add(10, "args");
    }
    if (left.value("rtv0_desc_key", std::string{}) == right.value("rtv0_desc_key", std::string{})) {
        add(10, "rtv_desc");
    }
    if (left.value("ps_crc", 0u) != 0 && left.value("ps_crc", 0u) == right.value("ps_crc", 0u)) {
        add(20, "ps_crc");
    }
    if (left.value("cs_crc", 0u) != 0 && left.value("cs_crc", 0u) == right.value("cs_crc", 0u)) {
        add(20, "cs_crc");
    }
    if (descriptor_read_signature(left.value("descriptor_reads", json::array())) ==
        descriptor_read_signature(right.value("descriptor_reads", json::array()))) {
        add(15, "descriptor_signature");
    }
    return std::min<uint32_t>(100, score);
}

std::string work_loose_key(const json& e) {
    std::ostringstream ss;
    ss << e.value("kind", std::string{}) << '|'
       << e.value("root_signature_hash", json_u64(e, "root_signature")) << '|'
       << rounded_viewport_key(e) << '|'
       << e.value("arg0", 0u) << ':'
       << e.value("arg1", 0u) << ':'
       << e.value("arg2", 0u) << '|'
       << e.value("rtv0_desc_key", std::string{});
    return ss.str();
}

std::string work_strict_key(const json& e) {
    const auto ps = e.value("ps_crc", 0u);
    const auto cs = e.value("cs_crc", 0u);
    std::ostringstream ss;
    ss << work_loose_key(e) << "|shader_key:" << e.value("shader_key", std::string{})
       << "|shader:" << (ps != 0 ? ps : cs);
    return ss.str();
}

} // namespace

namespace render {

struct StereoForensics::Impl {
    struct BoundTargets {
        std::vector<uintptr_t> rtvs;
        uintptr_t dsv{};
    };

    struct Producer {
        uint64_t frame{};
        uint64_t event_index{};
        std::string kind;
        uintptr_t pipeline_state{};
        int32_t eye_bucket{-1};
        uintptr_t descriptor{};
        std::string view_key;
    };

    struct ExperimentRule {
        std::string name;
        std::string action;
        std::string kind;
        uint32_t ps_crc{};
        uint32_t cs_crc{};
        int32_t eye_bucket{-1};
        bool enabled{true};
        uint64_t hits{};
    };

    mutable std::recursive_mutex mutex;
    bool enabled{env_truthy("UEVR_STEREO_FORENSICS")};
    bool experiments_enabled{env_truthy("UEVR_STEREO_EXPERIMENTS")};
    uint64_t max_events_per_frame{env_u64("UEVR_STEREO_FORENSICS_MAX_EVENTS_PER_FRAME", 5000)};
    uint64_t frame_stride{std::max<uint64_t>(1, env_u64("UEVR_STEREO_FORENSICS_FRAME_STRIDE", 30))};
    uint64_t start_frame{std::max<uint64_t>(1, env_u64("UEVR_STEREO_FORENSICS_START_FRAME", 1))};
    uint64_t max_captured_frames{env_u64("UEVR_STEREO_FORENSICS_MAX_CAPTURED_FRAMES", 16)};
    uint64_t max_total_events{env_u64("UEVR_STEREO_FORENSICS_MAX_TOTAL_EVENTS", 80000)};
    uint64_t max_total_bytes{env_u64("UEVR_STEREO_FORENSICS_MAX_TOTAL_BYTES", 128ull * 1024ull * 1024ull)};
    uint64_t max_events_per_kind_per_frame{env_u64("UEVR_STEREO_FORENSICS_MAX_EVENTS_PER_KIND_PER_FRAME", 3000)};
    uint64_t max_pso_binds_per_frame{env_u64("UEVR_STEREO_FORENSICS_MAX_SET_PSO_PER_FRAME", 256)};
    uint64_t max_root_binds_per_frame{env_u64("UEVR_STEREO_FORENSICS_MAX_ROOT_BINDS_PER_FRAME", 2048)};
    uint64_t flush_every_captured_frames{std::max<uint64_t>(1, env_u64("UEVR_STEREO_FORENSICS_FLUSH_EVERY_CAPTURED_FRAMES", 1))};
    uint64_t max_descriptors{env_u64("UEVR_STEREO_FORENSICS_MAX_DESCRIPTORS", 262144)};
    uint64_t max_writer_history{env_u64("UEVR_STEREO_FORENSICS_MAX_WRITER_HISTORY", 100000)};
    std::filesystem::path base_dir{env_string("UEVR_STEREO_FORENSICS_DIR", "C:\\tmp\\uevr_forensics")};
    std::filesystem::path arm_file{env_string("UEVR_STEREO_FORENSICS_ARM_FILE", "C:\\tmp\\uevr_forensics_arm.txt")};
    std::filesystem::path session;
    std::ofstream events_stream;
    bool frame_started{};
    bool capture_frame_active{};
    bool capture_stopped{};
    std::atomic<bool> capture_frame_active_atomic{false};
    std::atomic<bool> capture_stopped_atomic{false};
    bool capture_complete_logged{};
    uint64_t frame{};
    uint64_t event_index{};
    uint64_t dropped_events{};
    uint64_t dropped_events_total{};
    uint64_t captured_frames{};
    uint64_t skipped_frames{};
    uint64_t total_events_recorded{};
    uint64_t total_events_written{};
    uint64_t total_event_bytes{};
    json frame_context = json::object();
    std::vector<json> frame_events;
    std::unordered_map<std::string, uint64_t> frame_event_kind_counts;
    std::unordered_map<std::string, uint64_t> dropped_event_kind_counts;
    std::unordered_map<uintptr_t, json> resources;
    std::unordered_map<uintptr_t, json> descriptors;
    std::unordered_map<uintptr_t, json> descriptor_heaps;
    std::unordered_map<uintptr_t, BoundTargets> targets_by_command_list;
    std::unordered_map<uintptr_t, Producer> last_writer_by_resource;
    std::unordered_map<std::string, Producer> last_writer_by_view;
    std::unordered_map<uintptr_t, uint64_t> alias_barrier_generation_by_resource;
    std::vector<json> writer_history;
    std::vector<ExperimentRule> experiments;
    std::unordered_map<std::string, json> experiment_observations;
    uint64_t experiments_last_load_frame{UINT64_MAX};

    bool ensure_session_locked() {
        if (!enabled) {
            return false;
        }
        if (!session.empty()) {
            if (!events_stream.is_open()) {
                events_stream.open(session / "events.jsonl", std::ios::binary | std::ios::app);
            }
            return events_stream.is_open();
        }

        const auto now = std::chrono::system_clock::now();
        const auto now_time = std::chrono::system_clock::to_time_t(now);
        std::tm local{};
        localtime_s(&local, &now_time);

        std::ostringstream name;
        name << "session_" << std::put_time(&local, "%Y%m%d_%H%M%S")
             << "_pid" << GetCurrentProcessId();
        session = base_dir / name.str();

        std::error_code ec;
        std::filesystem::create_directories(session, ec);
        std::filesystem::create_directories(session / "frames", ec);
        if (ec) {
            SPDLOG_WARN("[StereoForensics] failed to create session dir {}: {}", session.string(), ec.message());
            session.clear();
            return false;
        }

        events_stream.open(session / "events.jsonl", std::ios::binary | std::ios::app);
        if (!events_stream) {
            SPDLOG_WARN("[StereoForensics] failed to open events stream at {}", (session / "events.jsonl").string());
            session.clear();
            return false;
        }

        SPDLOG_INFO("[StereoForensics] session started at {}", session.string());
        return true;
    }

    bool hard_capture_limit_reached_locked() const {
        if (max_captured_frames != 0 && captured_frames >= max_captured_frames) {
            return true;
        }
        if (max_total_events != 0 && total_events_recorded >= max_total_events) {
            return true;
        }
        if (max_total_bytes != 0 && total_event_bytes >= max_total_bytes) {
            return true;
        }
        return false;
    }

    bool should_capture_frame_locked(uint64_t frame_number) const {
        if (!enabled || capture_stopped || hard_capture_limit_reached_locked()) {
            return false;
        }
        if (frame_number < start_frame) {
            return false;
        }
        return ((frame_number - start_frame) % frame_stride) == 0;
    }

    void set_capture_state_locked(bool active, bool stopped) {
        capture_frame_active = active;
        capture_stopped = stopped;
        capture_frame_active_atomic.store(active, std::memory_order_relaxed);
        capture_stopped_atomic.store(stopped, std::memory_order_relaxed);
    }

    void log_capture_complete_locked() {
        if (capture_complete_logged) {
            return;
        }
        capture_complete_logged = true;
        SPDLOG_INFO(
            "[StereoForensics] capture complete: captured_frames={} total_events={} bytes={} dropped={} session={}",
            captured_frames,
            total_events_written,
            total_event_bytes,
            dropped_events_total,
            session.string());
    }

    void check_arm_file_locked() {
        if (arm_file.empty()) {
            return;
        }
        std::error_code ec;
        if (!std::filesystem::exists(arm_file, ec)) {
            return;
        }
        std::filesystem::remove(arm_file, ec);
        captured_frames = 0;
        start_frame = frame + 1;
        capture_complete_logged = false;
        set_capture_state_locked(false, false);
        SPDLOG_INFO(
            "[StereoForensics] capture armed by {}; next eligible frame={} stride={} max_captured_frames={}",
            arm_file.string(),
            start_frame,
            frame_stride,
            max_captured_frames);
    }

    uint64_t per_kind_cap_locked(const json& event) const {
        const auto kind = event.value("kind", std::string{});
        if (kind == "set_pso") {
            return max_pso_binds_per_frame;
        }
        if (event.value("event_class", std::string{}) == "bind" && event.contains("root")) {
            return max_root_binds_per_frame;
        }
        return max_events_per_kind_per_frame;
    }

    void drop_event_locked(const json& event) {
        ++dropped_events;
        ++dropped_events_total;
        const auto kind = event.value("kind", std::string{"(unknown)"});
        ++dropped_event_kind_counts[kind.empty() ? "(empty)" : kind];
    }

    uint64_t push_event_locked(json event) {
        if (!enabled || !ensure_session_locked()) {
            return 0;
        }
        if (!capture_frame_active || capture_stopped) {
            return 0;
        }
        if (max_total_events != 0 && total_events_recorded >= max_total_events) {
            set_capture_state_locked(capture_frame_active, true);
            drop_event_locked(event);
            return 0;
        }
        if (max_events_per_frame != 0 && frame_events.size() >= max_events_per_frame) {
            drop_event_locked(event);
            return 0;
        }

        const auto kind = event.value("kind", std::string{"(unknown)"});
        const auto cap = per_kind_cap_locked(event);
        auto& kind_count = frame_event_kind_counts[kind.empty() ? "(empty)" : kind];
        if (cap != 0 && kind_count >= cap) {
            drop_event_locked(event);
            return 0;
        }
        ++kind_count;

        const auto index = ++event_index;
        event["frame"] = frame;
        event["event_index"] = index;
        event["event_uid"] = event_uid(frame, index);
        frame_events.emplace_back(std::move(event));
        ++total_events_recorded;
        return index;
    }

    uint64_t resource_id_locked(uintptr_t resource) {
        if (resource == 0) {
            return 0;
        }
        auto it = resources.find(resource);
        if (it == resources.end()) {
            json rec{
                {"id", resources.size() + 1},
                {"resource", resource},
                {"resource_hex", hex_u64(resource)},
                {"resource_uid", resource_uid(resource)},
                {"resource_generation", 1},
                {"resource_instance_uid", resource_instance_uid(resource, 1)},
                {"resource_reuse_detected", false},
                {"first_seen_frame", frame},
                {"last_seen_frame", frame},
                {"desc", json::object()},
                {"desc_key", ""}
            };
            it = resources.emplace(resource, std::move(rec)).first;
        } else {
            it->second["last_seen_frame"] = frame;
            if (!it->second.contains("resource_generation")) {
                it->second["resource_generation"] = 1;
            }
            it->second["resource_instance_uid"] = resource_instance_uid(
                resource,
                it->second.value("resource_generation", 1ull));
        }
        return it->second.value("id", 0ull);
    }

    void update_resource_locked(
        std::string_view source,
        ID3D12Resource* resource,
        const D3D12_RESOURCE_DESC* desc,
        bool placed,
        uintptr_t heap,
        uint64_t heap_offset,
        const D3D12_HEAP_PROPERTIES* heap_props,
        D3D12_HEAP_FLAGS heap_flags,
        D3D12_RESOURCE_STATES initial_state) {
        if (resource == nullptr) {
            return;
        }

        D3D12_RESOURCE_DESC actual_desc{};
        if (desc != nullptr) {
            actual_desc = *desc;
        } else {
            actual_desc = resource->GetDesc();
        }

        const auto key = reinterpret_cast<uintptr_t>(resource);
        const auto id = resource_id_locked(key);
        auto& rec = resources[key];
        const auto new_desc = resource_desc_json(actual_desc);
        json desc_holder{{"desc", new_desc}};
        const auto new_desc_key = resource_desc_key(desc_holder);
        const auto old_desc_key = rec.value("desc_key", std::string{});
        const auto old_generation = std::max<uint64_t>(1, rec.value("resource_generation", 1ull));
        auto generation = old_generation;
        bool generation_bumped = false;
        auto bump_generation = [&](const char* reason) {
            if (!generation_bumped) {
                generation = old_generation + 1;
                generation_bumped = true;
            }
            rec["resource_reuse_detected"] = true;
            rec["last_reuse_reason"] = reason;
            rec["last_reuse_frame"] = frame;
        };

        const bool creation_source = is_resource_create_source(source);
        const auto create_count = rec.value("create_count", 0ull);
        if (creation_source) {
            if (create_count > 0) {
                bump_generation(rec.value("released", false)
                    ? "create_after_final_release"
                    : "create_reused_live_pointer");
            }
            rec["create_count"] = create_count + 1;
            rec["last_create_frame"] = frame;
            if (!rec.contains("first_create_frame")) {
                rec["first_create_frame"] = frame;
            }
            rec["released"] = false;
        }
        if (!old_desc_key.empty() && old_desc_key != new_desc_key) {
            rec["prior_desc_key"] = old_desc_key;
            rec["desc_key_changed_frame"] = frame;
            bump_generation("resource_desc_changed_for_pointer");
        }

        const bool preserve_placement =
            !placed &&
            heap == 0 &&
            heap_props == nullptr &&
            rec.contains("placed");
        rec["id"] = id;
        rec["resource"] = key;
        rec["resource_hex"] = hex_u64(key);
        rec["resource_uid"] = resource_uid(key);
        rec["resource_generation"] = generation;
        rec["resource_instance_uid"] = resource_instance_uid(key, generation);
        rec["last_seen_frame"] = frame;
        rec["source"] = std::string{source};
        if (!preserve_placement) {
            rec["placed"] = placed;
            rec["heap"] = heap;
            rec["heap_hex"] = hex_u64(heap);
            rec["heap_offset"] = heap_offset;
            rec["heap_offset_hex"] = hex_u64(heap_offset);
            rec["initial_state"] = static_cast<uint32_t>(initial_state);
            rec["heap_flags"] = static_cast<uint32_t>(heap_flags);
            if (heap_props != nullptr) {
                rec["heap_type"] = heap_type_name(heap_props->Type);
                rec["heap_type_id"] = static_cast<uint32_t>(heap_props->Type);
            }
        }
        rec["desc"] = new_desc;
        rec["desc_key"] = new_desc_key;
        if (!preserve_placement) {
            rec["alias_group"] = placed
                ? (hex_u64(heap) + "+" + hex_u64(heap_offset) + ":" + std::to_string(approx_resource_bytes(actual_desc)))
                : "";
        }
        const std::string allocation_classification = placed
            ? "placed_resource"
            : (creation_source ? "committed_resource" : "descriptor_observed_resource");
        rec["allocation_classification"] = allocation_classification;
        if (placed && actual_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
            rec["transient_pool_candidate"] = true;
        }
        if (rec.value("classification", std::string{}) != "frame_written") {
            rec["classification"] = placed
                ? "placed_transient_candidate"
                : allocation_classification;
        }
        const auto name = try_debug_name(resource);
        if (!name.empty()) {
            rec["name"] = name;
        }
    }

    json descriptor_base_locked(
        DescriptorKind kind,
        D3D12_CPU_DESCRIPTOR_HANDLE handle,
        uintptr_t resource) {
        json rec;
        const auto cpu = static_cast<uintptr_t>(handle.ptr);
        auto it = descriptors.find(cpu);
        if (it != descriptors.end()) {
            rec = it->second;
        } else {
            rec["id"] = descriptors.size() + 1;
            rec["first_seen_frame"] = frame;
        }
        rec["cpu"] = cpu;
        rec["cpu_hex"] = hex_u64(cpu);
        rec["kind"] = descriptor_kind_name(kind);
        rec["last_seen_frame"] = frame;
        rec["resource"] = resource;
        rec["resource_hex"] = hex_u64(resource);
        rec["resource_id"] = resource_id_locked(resource);
        if (resource != 0) {
            const auto rit = resources.find(resource);
            if (rit != resources.end()) {
                rec["resource_desc_key"] = rit->second.value("desc_key", std::string{});
                rec["resource_generation"] = rit->second.value("resource_generation", 1ull);
                rec["resource_instance_uid"] = rit->second.value("resource_instance_uid", resource_instance_uid(resource, 1));
            }
        }
        return rec;
    }

    void publish_descriptor_locked(json rec) {
        const auto cpu = json_value_or<uint64_t>(rec, "cpu", 0ull);
        if (cpu == 0) {
            return;
        }
        if (descriptors.size() >= max_descriptors && !descriptors.contains(cpu)) {
            return;
        }
        rec["view_key"] = descriptor_view_key(rec);
        rec["descriptor_view_uid"] = descriptor_view_uid(json_value_or<std::string>(rec, "view_key", std::string{}));
        descriptors[cpu] = std::move(rec);
    }

    std::optional<json> descriptor_for_cpu_locked(uintptr_t cpu) const {
        const auto it = descriptors.find(cpu);
        if (it == descriptors.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    json descriptor_use_locked(
        const D3D12Diagnostics::DescriptorReadInfo& read,
        uintptr_t root_signature,
        uintptr_t pipeline_state) {
        json use{
            {"root", read.root_parameter},
            {"slot", read.descriptor_index},
            {"cpu", read.descriptor_cpu},
            {"cpu_hex", hex_u64(read.descriptor_cpu)},
            {"source_cpu", read.descriptor_source_cpu},
            {"source_cpu_hex", hex_u64(read.descriptor_source_cpu)},
            {"resource", read.resource},
            {"resource_hex", hex_u64(read.resource)},
            {"resource_id", resource_id_locked(read.resource)},
            {"descriptor_type", read.descriptor_type}
        };
        auto binding = root_binding_json(root_signature, pipeline_state, read.root_parameter, read.descriptor_index);
        if (!binding.empty()) {
            use["root_binding"] = binding;
            if (binding.contains("binding_type")) {
                use["binding_type"] = binding["binding_type"];
            }
            if (binding.contains("shader_register")) {
                use["shader_register"] = binding["shader_register"];
            }
            if (binding.contains("shader_register_name")) {
                use["shader_register_name"] = binding["shader_register_name"];
            }
            if (binding.contains("register_space")) {
                use["register_space"] = binding["register_space"];
            }
        }

        auto desc = descriptor_for_cpu_locked(read.descriptor_cpu);
        if (!desc.has_value() && read.descriptor_source_cpu != 0) {
            desc = descriptor_for_cpu_locked(read.descriptor_source_cpu);
        }
        if (desc.has_value()) {
            use["descriptor"] = *desc;
            if (use.value("resource", 0ull) == 0) {
                use["resource"] = desc->value("resource", 0ull);
                use["resource_hex"] = hex_u64(desc->value("resource", 0ull));
                use["resource_id"] = desc->value("resource_id", 0ull);
            }
        }
        if (use.value("resource", 0ull) != 0) {
            const auto resource = use.value("resource", 0ull);
            use["resource_uid"] = resource_uid(resource);
            if (const auto rit = resources.find(resource); rit != resources.end()) {
                use["resource_generation"] = rit->second.value("resource_generation", 1ull);
                use["resource_instance_uid"] = rit->second.value("resource_instance_uid", resource_instance_uid(resource, 1));
            }
        }

        std::string view_key;
        if (desc.has_value()) {
            view_key = desc->value("view_key", descriptor_view_key(*desc, use.value("resource", 0ull), read.descriptor_type));
            use["view_key"] = view_key;
        } else if (use.value("resource", 0ull) != 0) {
            json fallback{{"resource", use.value("resource", 0ull)}, {"kind", read.descriptor_type}};
            view_key = descriptor_view_key(fallback, use.value("resource", 0ull), read.descriptor_type);
            use["view_key"] = view_key;
        }
        if (!view_key.empty()) {
            use["descriptor_view_uid"] = descriptor_view_uid(view_key);
        }

        const auto resource = use.value("resource", 0ull);
        if (resource != 0) {
            auto view_producer = view_key.empty() ? last_writer_by_view.end() : last_writer_by_view.find(view_key);
            const Producer* producer_ptr = view_producer != last_writer_by_view.end()
                ? &view_producer->second
                : nullptr;
            if (producer_ptr == nullptr) {
                const auto producer = last_writer_by_resource.find(resource);
                if (producer != last_writer_by_resource.end()) {
                    producer_ptr = &producer->second;
                }
            }
            if (producer_ptr != nullptr) {
                use["producer"] = {
                    {"frame", producer_ptr->frame},
                    {"event_index", producer_ptr->event_index},
                    {"kind", producer_ptr->kind},
                    {"pipeline_state", producer_ptr->pipeline_state},
                    {"pipeline_state_hex", hex_u64(producer_ptr->pipeline_state)},
                    {"eye_bucket", producer_ptr->eye_bucket},
                    {"descriptor", producer_ptr->descriptor},
                    {"descriptor_hex", hex_u64(producer_ptr->descriptor)},
                    {"view_key", producer_ptr->view_key}
                };
            } else if (read.producer_frame != 0 || read.producer_draw != 0) {
                use["producer"] = {
                    {"frame", read.producer_frame},
                    {"event_index", read.producer_draw},
                    {"kind", read.producer_kind},
                    {"pipeline_state", read.producer_pso},
                    {"pipeline_state_hex", hex_u64(read.producer_pso)},
                    {"eye_bucket", read.producer_eye_bucket},
                    {"descriptor", read.producer_descriptor},
                    {"descriptor_hex", hex_u64(read.producer_descriptor)}
                };
            }
            use["classification"] = classify_read_locked(resource, view_key);
        }

        return use;
    }

    json classify_read_locked(uintptr_t resource, const std::string& view_key) const {
        json out{
            {"kind", "unknown"},
            {"resource_known", false},
            {"has_resource_producer", false},
            {"has_view_producer", false},
            {"has_current_frame_resource_producer", false},
            {"has_current_frame_view_producer", false},
            {"alias_reused", false},
            {"alias_barrier_generation", 0}
        };
        if (resource == 0) {
            out["kind"] = "unknown";
            return out;
        }

        const auto resource_producer = last_writer_by_resource.find(resource);
        const auto view_producer = view_key.empty() ? last_writer_by_view.end() : last_writer_by_view.find(view_key);
        out["has_resource_producer"] = resource_producer != last_writer_by_resource.end();
        out["has_view_producer"] = view_producer != last_writer_by_view.end();
        if (resource_producer != last_writer_by_resource.end()) {
            out["resource_producer_frame"] = resource_producer->second.frame;
            out["resource_producer_event"] = resource_producer->second.event_index;
            out["has_current_frame_resource_producer"] = resource_producer->second.frame == frame;
        }
        if (view_producer != last_writer_by_view.end()) {
            out["view_producer_frame"] = view_producer->second.frame;
            out["view_producer_event"] = view_producer->second.event_index;
            out["has_current_frame_view_producer"] = view_producer->second.frame == frame;
        }
        if (view_producer != last_writer_by_view.end()) {
            out["kind"] = view_producer->second.frame == frame
                ? "frame_produced_view"
                : "history_produced_view";
        } else if (resource_producer != last_writer_by_resource.end()) {
            out["kind"] = resource_producer->second.frame == frame
                ? "frame_produced_resource"
                : "history_produced_resource";
        }

        const auto resource_it = resources.find(resource);
        if (resource_it != resources.end()) {
            out["resource_known"] = true;
            out["resource_generation"] = resource_it->second.value("resource_generation", 1ull);
            out["resource_instance_uid"] = resource_it->second.value("resource_instance_uid", resource_instance_uid(resource, 1));
            out["resource_classification"] = resource_it->second.value("classification", std::string{});
            out["resource_first_seen_frame"] = resource_it->second.value("first_seen_frame", 0ull);
            out["resource_last_seen_frame"] = resource_it->second.value("last_seen_frame", 0ull);
            out["created_this_frame"] = resource_it->second.value("first_seen_frame", 0ull) == frame;
            out["seen_before_frame"] = resource_it->second.value("first_seen_frame", frame) < frame;
            out["static_imported_candidate"] =
                !out.value("has_resource_producer", false) &&
                resource_it->second.value("first_seen_frame", frame) < frame;
            if (out.value("kind", std::string{}) == "unknown") {
                if (out.value("static_imported_candidate", false)) {
                    out["kind"] = "static_or_imported";
                } else if (out.value("created_this_frame", false)) {
                    out["kind"] = "created_unwritten_this_frame";
                } else {
                    out["kind"] = "known_unproduced_resource";
                }
            }
            const auto alias = resource_it->second.value("alias_group", std::string{});
            if (!alias.empty()) {
                out["alias_group"] = alias;
                out["alias_reused"] = true;
            }
            out["resource_desc_key"] = resource_it->second.value("desc_key", std::string{});
        } else if (out.value("kind", std::string{}) == "unknown") {
            out["kind"] = "unknown_resource";
        }
        const auto gen_it = alias_barrier_generation_by_resource.find(resource);
        if (gen_it != alias_barrier_generation_by_resource.end()) {
            out["alias_barrier_generation"] = gen_it->second;
        }
        return out;
    }

    json producer_json_locked(const Producer& producer) const {
        return {
            {"frame", producer.frame},
            {"event_index", producer.event_index},
            {"kind", producer.kind},
            {"pipeline_state", producer.pipeline_state},
            {"pipeline_state_hex", hex_u64(producer.pipeline_state)},
            {"eye_bucket", producer.eye_bucket},
            {"descriptor", producer.descriptor},
            {"descriptor_hex", hex_u64(producer.descriptor)},
            {"view_key", producer.view_key}
        };
    }

    json write_json_locked(
        std::string_view kind,
        uintptr_t descriptor,
        uintptr_t resource,
        uint32_t target_index) {
        std::string view_key;
        json descriptor_json = json::object();
        if (auto desc = descriptor_for_cpu_locked(descriptor); desc.has_value()) {
            descriptor_json = *desc;
            view_key = desc->value("view_key", descriptor_view_key(*desc, resource, kind));
        } else if (resource != 0) {
            descriptor_json = {{"resource", resource}, {"kind", std::string{kind}}};
            view_key = descriptor_view_key(descriptor_json, resource, kind);
        }

        json write{
            {"kind", std::string{kind}},
            {"descriptor", descriptor},
            {"descriptor_hex", hex_u64(descriptor)},
            {"resource", resource},
            {"resource_hex", hex_u64(resource)},
            {"resource_uid", resource_uid(resource)},
            {"resource_id", resource_id_locked(resource)},
            {"target_index", target_index},
            {"view_key", view_key},
            {"descriptor_view_uid", descriptor_view_uid(view_key)}
        };
        if (!descriptor_json.empty()) {
            write["descriptor_record"] = descriptor_json;
        }
        const auto rit = resources.find(resource);
        if (rit != resources.end()) {
            write["resource_desc_key"] = rit->second.value("desc_key", std::string{});
            write["resource_generation"] = rit->second.value("resource_generation", 1ull);
            write["resource_instance_uid"] = rit->second.value("resource_instance_uid", resource_instance_uid(resource, 1));
            if (rit->second.contains("alias_group")) {
                write["alias_group"] = rit->second["alias_group"];
            }
        }
        if (const auto gen = alias_barrier_generation_by_resource.find(resource); gen != alias_barrier_generation_by_resource.end()) {
            write["alias_barrier_generation"] = gen->second;
        }
        const auto previous_view = view_key.empty() ? last_writer_by_view.end() : last_writer_by_view.find(view_key);
        if (previous_view != last_writer_by_view.end()) {
            write["prior_view_producer"] = producer_json_locked(previous_view->second);
        }
        const auto previous = last_writer_by_resource.find(resource);
        if (previous != last_writer_by_resource.end()) {
            write["prior_producer"] = producer_json_locked(previous->second);
        }
        return write;
    }

    void note_writes_locked(const json& event, uint64_t pushed_index) {
        if (!event.contains("writes") || !event["writes"].is_array()) {
            return;
        }
        for (const auto& write : event["writes"]) {
            const auto resource = write.value("resource", 0ull);
            if (resource == 0) {
                continue;
            }
            Producer producer{
                frame,
                pushed_index,
                event.value("kind", std::string{}),
                event.value("pipeline_state", 0ull),
                event.value("eye_bucket", -1),
                write.value("descriptor", 0ull),
                write.value("view_key", std::string{})
            };
            last_writer_by_resource[resource] = producer;
            if (!producer.view_key.empty()) {
                last_writer_by_view[producer.view_key] = producer;
            }
            auto& resource_rec = resources[resource];
            resource_rec["classification"] = "frame_written";
            resource_rec["last_writer_frame"] = frame;
            resource_rec["last_writer_event"] = pushed_index;
            resource_rec["last_writer_kind"] = producer.kind;
            resource_rec["last_writer_eye_bucket"] = producer.eye_bucket;
            if (!resource_rec.contains("first_writer_frame")) {
                resource_rec["first_writer_frame"] = frame;
                resource_rec["first_writer_event"] = pushed_index;
            }

            json history = write;
            history["producer"] = producer_json_locked(producer);
            history["producer_frame"] = producer.frame;
            history["producer_event"] = producer.event_index;
            history["producer_kind"] = producer.kind;
            history["producer_eye_bucket"] = producer.eye_bucket;
            writer_history.push_back(std::move(history));
            if (writer_history.size() > max_writer_history) {
                const auto erase_count = std::max<size_t>(1, writer_history.size() / 4);
                writer_history.erase(writer_history.begin(), writer_history.begin() + static_cast<std::ptrdiff_t>(erase_count));
            }
        }
    }

    void load_experiments_locked() {
        if (!experiments_enabled) {
            return;
        }
        if (experiments_last_load_frame != UINT64_MAX && frame < experiments_last_load_frame + 30) {
            return;
        }
        experiments_last_load_frame = frame;

        const auto path = env_string("UEVR_STEREO_EXPERIMENTS_FILE");
        if (path.empty()) {
            return;
        }

        std::ifstream file(path);
        if (!file) {
            return;
        }

        try {
            const auto doc = json::parse(file);
            const auto& arr = doc.contains("rules")
                ? doc.at("rules")
                : (doc.contains("experiments") ? doc.at("experiments") : doc);
            if (!arr.is_array()) {
                return;
            }

            std::vector<ExperimentRule> loaded;
            for (const auto& item : arr) {
                ExperimentRule rule;
                rule.name = item.value("name", std::string{});
                rule.enabled = item.value("enabled", true);

                const auto match = item.value("match", json::object());
                const auto action = item.value("action", json{});
                if (action.is_string()) {
                    rule.action = action.get<std::string>();
                } else if (action.is_object()) {
                    rule.action = action.value("type", std::string{});
                } else {
                    rule.action = item.value("action", std::string{});
                }
                rule.kind = match.value("kind", item.value("kind", std::string{}));
                rule.ps_crc = static_cast<uint32_t>(parse_crc_value(match.value("ps_crc", item.value("ps_crc", json{}))));
                rule.cs_crc = static_cast<uint32_t>(parse_crc_value(match.value("cs_crc", item.value("cs_crc", json{}))));
                rule.eye_bucket = parse_eye_value(match.value("eye", item.value("eye", json{"both"})));
                if (rule.name.empty()) {
                    rule.name = rule.action + "_" + std::to_string(loaded.size());
                }
                if (!rule.action.empty()) {
                    loaded.emplace_back(std::move(rule));
                }
            }

            for (auto& loaded_rule : loaded) {
                for (const auto& existing : experiments) {
                    if (existing.name == loaded_rule.name) {
                        loaded_rule.hits = existing.hits;
                        break;
                    }
                }
            }
            experiments = std::move(loaded);
        } catch (const std::exception& e) {
            SPDLOG_WARN("[StereoForensics] experiment parse failed: {}", e.what());
        }
    }

    void write_json_file_locked(const std::filesystem::path& path, const json& doc) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return;
        }
        out << doc.dump(2);
    }

    void append_events_locked() {
        if (!events_stream.is_open()) {
            events_stream.open(session / "events.jsonl", std::ios::binary | std::ios::app);
        }
        if (!events_stream) {
            return;
        }
        for (const auto& event : frame_events) {
            std::string line = event.dump();
            const auto line_bytes = static_cast<uint64_t>(line.size() + 1);
            if (max_total_bytes != 0 && total_event_bytes + line_bytes > max_total_bytes) {
                set_capture_state_locked(capture_frame_active, true);
                drop_event_locked(event);
                break;
            }
            events_stream << line << '\n';
            ++total_events_written;
            total_event_bytes += line_bytes;
        }
        if ((captured_frames % flush_every_captured_frames) == 0) {
            events_stream.flush();
        }
    }

    json resource_array_locked() const {
        json out = json::array();
        for (const auto& [_, resource] : resources) {
            out.push_back(resource);
        }
        return out;
    }

    json descriptor_array_locked() const {
        json out = json::array();
        for (const auto& [_, descriptor] : descriptors) {
            out.push_back(descriptor);
        }
        return out;
    }

    json descriptor_heap_array_locked() const {
        json out = json::array();
        for (const auto& [_, heap] : descriptor_heaps) {
            out.push_back(heap);
        }
        return out;
    }

    json build_eye_diff_locked() const {
        struct Group {
            std::vector<const json*> left;
            std::vector<const json*> right;
            std::vector<const json*> unknown;
        };

        std::unordered_map<std::string, Group> groups;
        std::unordered_map<std::string, Group> strict_groups;
        for (const auto& event : frame_events) {
            if (event.value("event_class", std::string{}) != "work") {
                continue;
            }
            auto& group = groups[work_loose_key(event)];
            auto& strict = strict_groups[work_strict_key(event)];
            const auto eye = event.value("eye_bucket", -1);
            auto* ptr = &event;
            if (eye == 1) {
                group.left.push_back(ptr);
                strict.left.push_back(ptr);
            } else if (eye == 2) {
                group.right.push_back(ptr);
                strict.right.push_back(ptr);
            } else {
                group.unknown.push_back(ptr);
                strict.unknown.push_back(ptr);
            }
        }

        json issues = json::array();
        auto add_issue = [&](std::string severity, std::string kind, const json& data) {
            json issue = data;
            issue["severity"] = std::move(severity);
            issue["kind"] = std::move(kind);
            issues.push_back(std::move(issue));
        };

        json paired_groups = json::array();
        for (const auto& [key, group] : groups) {
            if (group.left.size() != group.right.size()) {
                add_issue("high", "eye_event_count_mismatch", {
                    {"key", key},
                    {"left_count", group.left.size()},
                    {"right_count", group.right.size()},
                    {"unknown_count", group.unknown.size()},
                    {"sample_left_event", group.left.empty() ? json{} : *group.left.front()},
                    {"sample_right_event", group.right.empty() ? json{} : *group.right.front()}
                });
            }

            if (group.left.empty() || group.right.empty()) {
                continue;
            }

            const json* best_left = group.left.front();
            const json* best_right = group.right.front();
            uint32_t best_score = 0;
            std::vector<std::string> best_reasons;
            for (const auto* candidate_left : group.left) {
                for (const auto* candidate_right : group.right) {
                    std::vector<std::string> reasons;
                    const auto score = pair_score(*candidate_left, *candidate_right, &reasons);
                    if (score >= best_score) {
                        best_score = score;
                        best_left = candidate_left;
                        best_right = candidate_right;
                        best_reasons = std::move(reasons);
                    }
                }
            }

            const auto& left = *best_left;
            const auto& right = *best_right;
            const auto pair_confidence = static_cast<double>(best_score) / 100.0;
            if (paired_groups.size() < 512) {
                paired_groups.push_back({
                    {"key", key},
                    {"left_event", left.value("event_index", 0ull)},
                    {"right_event", right.value("event_index", 0ull)},
                    {"pair_score", best_score},
                    {"pair_confidence", pair_confidence},
                    {"pair_reason", best_reasons}
                });
            }
            if (left.value("shader_key", std::string{}) != right.value("shader_key", std::string{}) ||
                left.value("root_signature_hash", 0ull) != right.value("root_signature_hash", 0ull) ||
                left.value("pipeline_state", 0ull) != right.value("pipeline_state", 0ull) ||
                left.value("ps_crc", 0u) != right.value("ps_crc", 0u) ||
                left.value("cs_crc", 0u) != right.value("cs_crc", 0u)) {
                add_issue("medium", "pso_or_shader_differs", {
                    {"key", key},
                    {"left_event", left.value("event_index", 0ull)},
                    {"right_event", right.value("event_index", 0ull)},
                    {"left_pso", left.value("pipeline_state_hex", std::string{})},
                    {"right_pso", right.value("pipeline_state_hex", std::string{})},
                    {"left_shader_key", left.value("shader_key", std::string{})},
                    {"right_shader_key", right.value("shader_key", std::string{})},
                    {"left_root_signature_hash", left.value("root_signature_hash_hex", std::string{})},
                    {"right_root_signature_hash", right.value("root_signature_hash_hex", std::string{})},
                    {"left_vs_crc", left.value("vs_crc_hex", std::string{})},
                    {"right_vs_crc", right.value("vs_crc_hex", std::string{})},
                    {"left_ps_crc", left.value("ps_crc_hex", std::string{})},
                    {"right_ps_crc", right.value("ps_crc_hex", std::string{})},
                    {"left_cs_crc", left.value("cs_crc_hex", std::string{})},
                    {"right_cs_crc", right.value("cs_crc_hex", std::string{})}
                });
            }

            const auto left_g_cbv = root_hash_map(left.value("graphics_cbv_hashes", json::array()));
            const auto right_g_cbv = root_hash_map(right.value("graphics_cbv_hashes", json::array()));
            for (const auto& [root, lhash] : left_g_cbv) {
                const auto it = right_g_cbv.find(root);
                if (it != right_g_cbv.end() && it->second != 0 && it->second != lhash) {
                    add_issue("medium", "graphics_cbv_hash_differs", {
                        {"key", key},
                        {"root", root},
                        {"left_event", left.value("event_index", 0ull)},
                        {"right_event", right.value("event_index", 0ull)},
                        {"left_hash", hex_u64(lhash)},
                        {"right_hash", hex_u64(it->second)}
                    });
                }
            }

            auto compare_reads = [&](const json& lreads, const json& rreads) {
                if (!lreads.is_array() || !rreads.is_array()) {
                    return;
                }
                for (const auto& lr : lreads) {
                    const auto root = lr.value("root", 0u);
                    const auto slot = lr.value("slot", 0u);
                    const auto type = lr.value("descriptor_type", std::string{});
                    const json* rr_match = nullptr;
                    for (const auto& rr : rreads) {
                        if (rr.value("root", 0u) == root &&
                            rr.value("slot", 0u) == slot &&
                            rr.value("descriptor_type", std::string{}) == type) {
                            rr_match = &rr;
                            break;
                        }
                    }
                    if (rr_match == nullptr) {
                        add_issue("medium", "descriptor_missing_on_right", {
                            {"key", key},
                            {"root", root},
                            {"slot", slot},
                            {"descriptor_type", type},
                            {"left_resource", lr.value("resource_hex", std::string{})}
                        });
                        continue;
                    }

                    const auto lres = lr.value("resource", 0ull);
                    const auto rres = rr_match->value("resource", 0ull);
                    const auto ldesc = lr.value("descriptor", json::object());
                    const auto rdesc = rr_match->value("descriptor", json::object());
                    if (lres == rres) {
                        const auto lslice = ldesc.value("first_array_slice", 0u);
                        const auto rslice = rdesc.value("first_array_slice", 0u);
                        if (lslice != rslice) {
                            add_issue("info", "same_resource_different_slice", {
                                {"key", key},
                                {"root", root},
                                {"slot", slot},
                                {"resource", lr.value("resource_hex", std::string{})},
                                {"left_first_array_slice", lslice},
                                {"right_first_array_slice", rslice}
                            });
                        } else if (lr.value("view_key", std::string{}) != rr_match->value("view_key", std::string{})) {
                            add_issue("info", "same_resource_different_view", {
                                {"key", key},
                                {"root", root},
                                {"slot", slot},
                                {"resource", lr.value("resource_hex", std::string{})},
                                {"left_view_key", lr.value("view_key", std::string{})},
                                {"right_view_key", rr_match->value("view_key", std::string{})}
                            });
                        }
                    } else {
                        const auto lkey = ldesc.value("resource_desc_key", std::string{});
                        const auto rkey = rdesc.value("resource_desc_key", std::string{});
                        add_issue(lkey == rkey ? "info" : "medium", lkey == rkey ? "same_desc_different_resource" : "descriptor_resource_differs", {
                            {"key", key},
                            {"root", root},
                            {"slot", slot},
                            {"descriptor_type", type},
                            {"left_resource", lr.value("resource_hex", std::string{})},
                            {"right_resource", rr_match->value("resource_hex", std::string{})},
                            {"left_resource_desc_key", lkey},
                            {"right_resource_desc_key", rkey},
                            {"left_classification", lr.value("classification", json::object())},
                            {"right_classification", rr_match->value("classification", json::object())}
                        });
                    }

                    const auto lprod = lr.value("producer", json::object());
                    const auto rprod = rr_match->value("producer", json::object());
                    if (!lprod.empty() || !rprod.empty()) {
                        if (lprod.value("event_index", 0ull) != rprod.value("event_index", 0ull) ||
                            lprod.value("eye_bucket", -1) != rprod.value("eye_bucket", -1)) {
                            add_issue("medium", "producer_lineage_differs", {
                                {"key", key},
                                {"root", root},
                                {"slot", slot},
                                {"left_producer", lprod},
                                {"right_producer", rprod}
                            });
                        }
                    }
                }
            };

            compare_reads(left.value("descriptor_reads", json::array()), right.value("descriptor_reads", json::array()));
        }

        if (issues.size() > 512) {
            issues.erase(issues.begin() + 512, issues.end());
        }

        json strict_summary = json::array();
        for (const auto& [key, group] : strict_groups) {
            if (group.left.size() != group.right.size()) {
                strict_summary.push_back({
                    {"key", key},
                    {"left_count", group.left.size()},
                    {"right_count", group.right.size()},
                    {"unknown_count", group.unknown.size()}
                });
            }
            if (strict_summary.size() >= 256) {
                break;
            }
        }

        return {
            {"frame", frame},
            {"work_event_groups", groups.size()},
            {"paired_groups", std::move(paired_groups)},
            {"strict_mismatched_groups", std::move(strict_summary)},
            {"issues", std::move(issues)}
        };
    }

    json build_lineage_locked() const {
        json edges = json::array();
        std::unordered_map<std::string, uint64_t> classification_counts;
        uint64_t read_count = 0;
        uint64_t reads_without_producer = 0;
        uint64_t reads_with_history_producer = 0;
        uint64_t static_imported_candidates = 0;
        uint64_t alias_reads = 0;
        uint64_t released_resource_reads = 0;
        for (const auto& event : frame_events) {
            if (event.value("event_class", std::string{}) != "work") {
                continue;
            }
            const auto consumer = event.value("event_index", 0ull);
            for (const auto& read : event.value("descriptor_reads", json::array())) {
                ++read_count;
                const auto classification = read.value("classification", json::object());
                const auto class_kind = classification.value("kind", std::string{"unknown"});
                ++classification_counts[class_kind];
                if (!classification.value("has_resource_producer", false) &&
                    !classification.value("has_view_producer", false)) {
                    ++reads_without_producer;
                }
                if (class_kind == "history_produced_view" || class_kind == "history_produced_resource") {
                    ++reads_with_history_producer;
                }
                if (classification.value("static_imported_candidate", false)) {
                    ++static_imported_candidates;
                }
                if (classification.value("alias_reused", false)) {
                    ++alias_reads;
                }
                if (classification.value("resource_classification", std::string{}) == "released") {
                    ++released_resource_reads;
                }
                json edge{
                    {"consumer_event", consumer},
                    {"consumer_kind", event.value("kind", std::string{})},
                    {"consumer_eye_bucket", event.value("eye_bucket", -1)},
                    {"root", read.value("root", 0u)},
                    {"slot", read.value("slot", 0u)},
                    {"descriptor_type", read.value("descriptor_type", std::string{})},
                    {"resource", read.value("resource", 0ull)},
                    {"resource_hex", read.value("resource_hex", std::string{})},
                    {"resource_generation", read.value("resource_generation", 0ull)},
                    {"resource_instance_uid", read.value("resource_instance_uid", std::string{})},
                    {"view_key", read.value("view_key", std::string{})},
                    {"binding_type", read.value("binding_type", std::string{})},
                    {"shader_register", read.value("shader_register", 0u)},
                    {"shader_register_name", read.value("shader_register_name", std::string{})},
                    {"register_space", read.value("register_space", 0u)},
                    {"root_binding", read.value("root_binding", json::object())},
                    {"classification", read.value("classification", json::object())}
                };
                if (read.contains("descriptor")) {
                    edge["descriptor"] = read["descriptor"];
                }
                if (read.contains("producer")) {
                    edge["producer"] = read["producer"];
                }
                edges.push_back(std::move(edge));
                if (edges.size() >= 20000) {
                    break;
                }
            }
            if (edges.size() >= 20000) {
                break;
            }
        }

        json producers = json::array();
        for (const auto& [resource, producer] : last_writer_by_resource) {
            producers.push_back({
                {"resource", resource},
                {"resource_hex", hex_u64(resource)},
                {"resource_generation", resources.contains(resource) ? resources.at(resource).value("resource_generation", 1ull) : 1ull},
                {"resource_instance_uid", resources.contains(resource) ? resources.at(resource).value("resource_instance_uid", resource_instance_uid(resource, 1)) : resource_instance_uid(resource, 1)},
                {"producer_frame", producer.frame},
                {"producer_event", producer.event_index},
                {"producer_kind", producer.kind},
                {"producer_pso", producer.pipeline_state},
                {"producer_pso_hex", hex_u64(producer.pipeline_state)},
                {"producer_eye_bucket", producer.eye_bucket},
                {"view_key", producer.view_key}
            });
            if (producers.size() >= 20000) {
                break;
            }
        }

        json view_producers = json::array();
        for (const auto& [view_key, producer] : last_writer_by_view) {
            view_producers.push_back({
                {"view_key", view_key},
                {"producer", producer_json_locked(producer)}
            });
            if (view_producers.size() >= 20000) {
                break;
            }
        }

        json history = json::array();
        const auto start = writer_history.size() > 20000 ? writer_history.size() - 20000 : 0;
        for (size_t i = start; i < writer_history.size(); ++i) {
            history.push_back(writer_history[i]);
        }

        json class_counts_json = json::object();
        for (const auto& [kind, count] : classification_counts) {
            class_counts_json[kind] = count;
        }

        return {
            {"frame", frame},
            {"read_edges", std::move(edges)},
            {"latest_resource_producers", std::move(producers)},
            {"latest_view_producers", std::move(view_producers)},
            {"writer_history", std::move(history)},
            {"validation", {
                {"read_count", read_count},
                {"classification_counts", std::move(class_counts_json)},
                {"reads_without_producer", reads_without_producer},
                {"reads_with_history_producer", reads_with_history_producer},
                {"static_imported_candidates", static_imported_candidates},
                {"alias_reads", alias_reads},
                {"released_resource_reads", released_resource_reads},
                {"writer_history_count", writer_history.size()}
            }}
        };
    }

    json experiments_json_locked() const {
        json arr = json::array();
        for (const auto& rule : experiments) {
            arr.push_back({
                {"name", rule.name},
                {"action", rule.action},
                {"kind", rule.kind},
                {"ps_crc", rule.ps_crc},
                {"ps_crc_hex", hex_u64(rule.ps_crc)},
                {"cs_crc", rule.cs_crc},
                {"cs_crc_hex", hex_u64(rule.cs_crc)},
                {"eye_bucket", rule.eye_bucket},
                {"enabled", rule.enabled},
                {"hits", rule.hits}
            });
        }
        json observations = json::array();
        for (const auto& [_, rec] : experiment_observations) {
            observations.push_back(rec);
        }
        return {
            {"enabled", experiments_enabled},
            {"note", "v2 rule files are parsed. StereoForensics executes skip-style actions here; Sn2DebugColorOverride consumes color_override; D3D12Hook executes supported mutation actions (swap_cbv_left_to_right, swap_descriptor_from_left, force_srv_array_slice, neutralize_texture)."},
            {"runtime_capabilities", {
                {"executable_actions", json::array({
                    "skip",
                    "skip_draw",
                    "skip_dispatch",
                    "color_override",
                    "swap_cbv_left_to_right",
                    "swap_descriptor_from_left",
                    "force_srv_array_slice",
                    "neutralize_texture"
                })},
                {"mutation_actions", json::array({
                    "swap_cbv_left_to_right",
                    "swap_descriptor_from_left",
                    "force_srv_array_slice",
                    "neutralize_texture"
                })},
                {"probe_actions", json::array({"color_override"})},
                {"unsupported_actions", json::array({
                    "duplicate_left_work_into_right_bucket",
                    "replace_shader_from_left_permutation",
                    "replace_ps_bytecode",
                    "patch_cb_bytes"
                })}
            }},
            {"experiments", std::move(arr)},
            {"observations", std::move(observations)}
        };
    }

    void finalize_frame_locked() {
        if (!enabled || !frame_started || !ensure_session_locked()) {
            return;
        }
        if (!capture_frame_active) {
            frame_events.clear();
            frame_event_kind_counts.clear();
            dropped_event_kind_counts.clear();
            event_index = 0;
            dropped_events = 0;
            frame_started = false;
            return;
        }
        bool final_capture_snapshot =
            (max_captured_frames != 0 && captured_frames >= max_captured_frames) ||
            (max_total_events != 0 && total_events_recorded >= max_total_events) ||
            (max_total_bytes != 0 && total_event_bytes >= max_total_bytes);

        try {
            append_events_locked();
            if (hard_capture_limit_reached_locked()) {
                set_capture_state_locked(capture_frame_active, true);
                final_capture_snapshot = true;
            }
            json kind_counts = json::object();
            for (const auto& [kind, count] : frame_event_kind_counts) {
                kind_counts[kind] = count;
            }
            json dropped_kind_counts = json::object();
            for (const auto& [kind, count] : dropped_event_kind_counts) {
                dropped_kind_counts[kind] = count;
            }
            json limiter_status = {
                {"capture_frame_active", capture_frame_active},
                {"capture_stopped", capture_stopped},
                {"frame_stride", frame_stride},
                {"start_frame", start_frame},
                {"captured_frames", captured_frames},
                {"skipped_frames", skipped_frames},
                {"max_captured_frames", max_captured_frames},
                {"max_events_per_frame", max_events_per_frame},
                {"max_events_per_kind_per_frame", max_events_per_kind_per_frame},
                {"max_set_pso_per_frame", max_pso_binds_per_frame},
                {"max_root_binds_per_frame", max_root_binds_per_frame},
                {"max_total_events", max_total_events},
                {"max_total_bytes", max_total_bytes},
                {"total_events_recorded", total_events_recorded},
                {"total_events_written", total_events_written},
                {"total_event_bytes", total_event_bytes},
                {"dropped_events_total", dropped_events_total},
                {"frame_event_kind_counts", kind_counts},
                {"dropped_event_kind_counts", dropped_kind_counts}
            };
            if (final_capture_snapshot) {
                write_json_file_locked(session / "resources.json", {{"resources", resource_array_locked()}});
                write_json_file_locked(session / "descriptors.json", {{"descriptors", descriptor_array_locked()}});
                write_json_file_locked(session / "descriptor_heaps.json", {{"descriptor_heaps", descriptor_heap_array_locked()}});
            }
            write_json_file_locked(session / "lineage.json", build_lineage_locked());
            write_json_file_locked(session / "eye_diff.json", build_eye_diff_locked());
            write_json_file_locked(session / "experiments.json", experiments_json_locked());

            const auto frame_path = session / "frames" / ("frame_" + std::to_string(frame) + "_summary.json");
            write_json_file_locked(frame_path, {
                {"frame", frame},
                {"event_count", frame_events.size()},
                {"dropped_events", dropped_events},
                {"limiter_status", limiter_status},
                {"context", frame_context},
                {"eye_diff_path", (session / "eye_diff.json").string()},
                {"lineage_path", (session / "lineage.json").string()}
            });

            write_json_file_locked(session / "manifest.json", {
                {"schema", "uevr.stereo_forensics.v1"},
                {"session_dir", session.string()},
                {"latest_frame", frame},
                {"latest_frame_event_count", frame_events.size()},
                {"dropped_events", dropped_events},
                {"limiter_status", limiter_status},
                {"events_jsonl", (session / "events.jsonl").string()},
                {"resources", (session / "resources.json").string()},
                {"descriptors", (session / "descriptors.json").string()},
                {"descriptor_heaps", (session / "descriptor_heaps.json").string()},
                {"lineage", (session / "lineage.json").string()},
                {"eye_diff", (session / "eye_diff.json").string()},
                {"experiments", (session / "experiments.json").string()}
            });
        } catch (const std::exception& e) {
            SPDLOG_WARN("[StereoForensics] finalize frame failed: {}", e.what());
        }

        frame_events.clear();
        frame_event_kind_counts.clear();
        dropped_event_kind_counts.clear();
        event_index = 0;
        dropped_events = 0;
        set_capture_state_locked(false, capture_stopped);
        if (capture_stopped) {
            if (events_stream.is_open()) {
                events_stream.flush();
            }
            log_capture_complete_locked();
        }
        frame_started = false;
    }
};

StereoForensics& StereoForensics::get() {
    static StereoForensics instance;
    return instance;
}

StereoForensics::StereoForensics()
    : m_impl(std::make_unique<Impl>()) {
}

StereoForensics::~StereoForensics() {
    STEREO_FORENSICS_TRY("~StereoForensics") {
    if (m_impl != nullptr) {
        std::scoped_lock _{m_impl->mutex};
        m_impl->finalize_frame_locked();
    }
    } STEREO_FORENSICS_CATCH_VOID("~StereoForensics")
}

bool StereoForensics::is_enabled() const {
    STEREO_FORENSICS_TRY("is_enabled") {
    return m_impl != nullptr && m_impl->enabled;
    } STEREO_FORENSICS_CATCH_RETURN("is_enabled", false)
}

bool StereoForensics::experiments_enabled() const {
    STEREO_FORENSICS_TRY("experiments_enabled") {
    return m_impl != nullptr && m_impl->experiments_enabled;
    } STEREO_FORENSICS_CATCH_RETURN("experiments_enabled", false)
}

bool StereoForensics::is_capturing_this_frame() const {
    STEREO_FORENSICS_TRY("is_capturing_this_frame") {
    return m_impl != nullptr &&
        m_impl->enabled &&
        m_impl->capture_frame_active_atomic.load(std::memory_order_relaxed) &&
        !m_impl->capture_stopped_atomic.load(std::memory_order_relaxed);
    } STEREO_FORENSICS_CATCH_RETURN("is_capturing_this_frame", false)
}

std::filesystem::path StereoForensics::session_dir() const {
    STEREO_FORENSICS_TRY("session_dir") {
    if (m_impl == nullptr) {
        return {};
    }
    std::scoped_lock _{m_impl->mutex};
    return m_impl->session;
    } STEREO_FORENSICS_CATCH_RETURN("session_dir", std::filesystem::path())
}

void StereoForensics::begin_frame(
    ID3D12Device* device,
    IDXGISwapChain3* swapchain,
    ID3D12CommandQueue* queue,
    uint32_t render_width,
    uint32_t render_height,
    uint32_t display_width,
    uint32_t display_height,
    bool proton_swapchain,
    bool framegen_swapchain) {
    STEREO_FORENSICS_TRY("begin_frame") {
    if (!is_enabled()) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
    if (!m_impl->ensure_session_locked()) {
        return;
    }
    m_impl->finalize_frame_locked();
    m_impl->check_arm_file_locked();
    ++m_impl->frame;
    m_impl->frame_started = true;
    m_impl->set_capture_state_locked(m_impl->should_capture_frame_locked(m_impl->frame), m_impl->capture_stopped);
    if (m_impl->capture_frame_active) {
        ++m_impl->captured_frames;
        m_impl->frame_events.reserve(static_cast<size_t>(std::min<uint64_t>(m_impl->max_events_per_frame, 8192)));
    } else {
        if (m_impl->hard_capture_limit_reached_locked()) {
            m_impl->set_capture_state_locked(false, true);
            m_impl->log_capture_complete_locked();
        }
        ++m_impl->skipped_frames;
    }
    m_impl->frame_context = {
        {"device", reinterpret_cast<uintptr_t>(device)},
        {"device_hex", hex_u64(reinterpret_cast<uintptr_t>(device))},
        {"swapchain", reinterpret_cast<uintptr_t>(swapchain)},
        {"swapchain_hex", hex_u64(reinterpret_cast<uintptr_t>(swapchain))},
        {"queue", reinterpret_cast<uintptr_t>(queue)},
        {"queue_hex", hex_u64(reinterpret_cast<uintptr_t>(queue))},
        {"render_width", render_width},
        {"render_height", render_height},
        {"display_width", display_width},
        {"display_height", display_height},
        {"proton_swapchain", proton_swapchain},
        {"framegen_swapchain", framegen_swapchain},
        {"capture_frame_active", m_impl->capture_frame_active},
        {"capture_stopped", m_impl->capture_stopped}
    };
    m_impl->load_experiments_locked();
    m_impl->push_event_locked({{"event_class", "frame"}, {"kind", "begin_frame"}, {"context", m_impl->frame_context}});
    } STEREO_FORENSICS_CATCH_VOID("begin_frame")
}

void StereoForensics::record_descriptor_heap_created(
    std::string_view source,
    ID3D12DescriptorHeap* heap,
    const D3D12_DESCRIPTOR_HEAP_DESC* desc,
    UINT descriptor_stride) {
    STEREO_FORENSICS_TRY("record_descriptor_heap_created") {
    if (!is_enabled() || heap == nullptr || desc == nullptr) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
    if (m_impl->capture_stopped) {
        return;
    }
    const auto key = reinterpret_cast<uintptr_t>(heap);
    const auto cpu_base = heap->GetCPUDescriptorHandleForHeapStart().ptr;
    const auto gpu_base = (desc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0
        ? heap->GetGPUDescriptorHandleForHeapStart().ptr
        : 0;

    json rec{
        {"heap", key},
        {"heap_hex", hex_u64(key)},
        {"type", descriptor_heap_type_name(desc->Type)},
        {"type_id", static_cast<uint32_t>(desc->Type)},
        {"num_descriptors", desc->NumDescriptors},
        {"flags", static_cast<uint32_t>(desc->Flags)},
        {"node_mask", desc->NodeMask},
        {"cpu_base", cpu_base},
        {"cpu_base_hex", hex_u64(cpu_base)},
        {"gpu_base", gpu_base},
        {"gpu_base_hex", hex_u64(gpu_base)},
        {"stride", descriptor_stride},
        {"first_seen_frame", m_impl->frame},
        {"last_seen_frame", m_impl->frame}
    };
    if (const auto it = m_impl->descriptor_heaps.find(key); it != m_impl->descriptor_heaps.end()) {
        rec["first_seen_frame"] = it->second.value("first_seen_frame", m_impl->frame);
    }
    m_impl->descriptor_heaps[key] = rec;
    m_impl->push_event_locked({{"event_class", "create"}, {"kind", "create_descriptor_heap"}, {"source", std::string{source}}, {"heap", rec}});
    } STEREO_FORENSICS_CATCH_VOID("record_descriptor_heap_created")
}

void StereoForensics::record_resource_created(
    std::string_view source,
    ID3D12Resource* resource,
    const D3D12_RESOURCE_DESC* desc,
    const D3D12_HEAP_PROPERTIES* heap_props,
    D3D12_HEAP_FLAGS heap_flags,
    D3D12_RESOURCE_STATES initial_state) {
    STEREO_FORENSICS_TRY("record_resource_created") {
    if (!is_enabled() || resource == nullptr) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
    if (m_impl->capture_stopped) {
        return;
    }
    m_impl->update_resource_locked(source, resource, desc, false, 0, 0, heap_props, heap_flags, initial_state);
    const auto key = reinterpret_cast<uintptr_t>(resource);
    m_impl->push_event_locked({{"event_class", "create"}, {"kind", "create_committed_resource"}, {"source", std::string{source}}, {"resource", m_impl->resources[key]}});
    } STEREO_FORENSICS_CATCH_VOID("record_resource_created")
}

void StereoForensics::record_placed_resource_created(
    std::string_view source,
    ID3D12Resource* resource,
    ID3D12Heap* heap,
    uint64_t heap_offset,
    const D3D12_RESOURCE_DESC* desc,
    D3D12_RESOURCE_STATES initial_state) {
    STEREO_FORENSICS_TRY("record_placed_resource_created") {
    if (!is_enabled() || resource == nullptr) {
        return;
    }
    D3D12_HEAP_PROPERTIES heap_props{};
    D3D12_HEAP_FLAGS heap_flags{};
    if (heap != nullptr) {
        const auto heap_desc = heap->GetDesc();
        heap_props = heap_desc.Properties;
        heap_flags = heap_desc.Flags;
    }
    std::scoped_lock _{m_impl->mutex};
    if (m_impl->capture_stopped) {
        return;
    }
    m_impl->update_resource_locked(source, resource, desc, true, reinterpret_cast<uintptr_t>(heap), heap_offset, &heap_props, heap_flags, initial_state);
    const auto key = reinterpret_cast<uintptr_t>(resource);
    m_impl->push_event_locked({{"event_class", "create"}, {"kind", "create_placed_resource"}, {"source", std::string{source}}, {"resource", m_impl->resources[key]}});
    } STEREO_FORENSICS_CATCH_VOID("record_placed_resource_created")
}

void StereoForensics::record_resource_released(
    std::string_view source,
    ID3D12Resource* resource,
    uint32_t ref_count_after_release) {
    STEREO_FORENSICS_TRY("record_resource_released") {
    if (!is_enabled() || resource == nullptr) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
    if (m_impl->capture_stopped) {
        return;
    }
    const auto key = reinterpret_cast<uintptr_t>(resource);
    const auto id = m_impl->resource_id_locked(key);
    auto& rec = m_impl->resources[key];
    rec["id"] = id;
    rec["resource"] = key;
    rec["resource_hex"] = hex_u64(key);
    rec["resource_uid"] = resource_uid(key);
    if (!rec.contains("resource_generation")) {
        rec["resource_generation"] = 1;
    }
    rec["resource_instance_uid"] = resource_instance_uid(key, rec.value("resource_generation", 1ull));
    rec["last_release_frame"] = m_impl->frame;
    rec["last_release_refcount"] = ref_count_after_release;
    rec["release_count"] = rec.value("release_count", 0ull) + 1;
    if (ref_count_after_release == 0) {
        rec["released"] = true;
        rec["final_release_frame"] = m_impl->frame;
        rec["classification"] = "released";
    }
    m_impl->push_event_locked({
        {"event_class", "lifetime"},
        {"kind", ref_count_after_release == 0 ? "resource_final_release" : "resource_release"},
        {"source", std::string{source}},
        {"resource", key},
        {"resource_hex", hex_u64(key)},
        {"resource_generation", rec.value("resource_generation", 1ull)},
        {"resource_instance_uid", rec.value("resource_instance_uid", resource_instance_uid(key, 1))},
        {"ref_count_after_release", ref_count_after_release}
    });
    } STEREO_FORENSICS_CATCH_VOID("record_resource_released")
}

void StereoForensics::record_cbv_descriptor(
    std::string_view source,
    const D3D12_CONSTANT_BUFFER_VIEW_DESC* desc,
    D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    STEREO_FORENSICS_TRY("record_cbv_descriptor") {
    if (!is_enabled() || desc == nullptr || handle.ptr == 0) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
    if (m_impl->capture_stopped) {
        return;
    }
    auto rec = m_impl->descriptor_base_locked(DescriptorKind::CBV, handle, 0);
    rec["source"] = std::string{source};
    rec["buffer_location"] = desc->BufferLocation;
    rec["buffer_location_hex"] = hex_u64(desc->BufferLocation);
    rec["size_in_bytes"] = desc->SizeInBytes;
    m_impl->publish_descriptor_locked(std::move(rec));
    } STEREO_FORENSICS_CATCH_VOID("record_cbv_descriptor")
}

void StereoForensics::record_srv_descriptor(
    std::string_view source,
    ID3D12Resource* resource,
    const D3D12_SHADER_RESOURCE_VIEW_DESC* desc,
    D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    STEREO_FORENSICS_TRY("record_srv_descriptor") {
    if (!is_enabled() || resource == nullptr || handle.ptr == 0) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
    if (m_impl->capture_stopped) {
        return;
    }
    m_impl->update_resource_locked(source, resource, nullptr, false, 0, 0, nullptr, D3D12_HEAP_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
    auto rec = m_impl->descriptor_base_locked(DescriptorKind::SRV, handle, reinterpret_cast<uintptr_t>(resource));
    rec["source"] = std::string{source};
    if (desc != nullptr) {
        rec["format"] = static_cast<uint32_t>(desc->Format);
        rec["view_dimension"] = static_cast<uint32_t>(desc->ViewDimension);
        switch (desc->ViewDimension) {
        case D3D12_SRV_DIMENSION_TEXTURE2D:
            rec["most_detailed_mip"] = desc->Texture2D.MostDetailedMip;
            rec["mip_levels"] = desc->Texture2D.MipLevels;
            rec["plane_slice"] = desc->Texture2D.PlaneSlice;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
            rec["most_detailed_mip"] = desc->Texture2DArray.MostDetailedMip;
            rec["mip_levels"] = desc->Texture2DArray.MipLevels;
            rec["first_array_slice"] = desc->Texture2DArray.FirstArraySlice;
            rec["array_size"] = desc->Texture2DArray.ArraySize;
            rec["plane_slice"] = desc->Texture2DArray.PlaneSlice;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE3D:
            rec["most_detailed_mip"] = desc->Texture3D.MostDetailedMip;
            rec["mip_levels"] = desc->Texture3D.MipLevels;
            break;
        case D3D12_SRV_DIMENSION_BUFFER:
            rec["first_element"] = desc->Buffer.FirstElement;
            rec["num_elements"] = desc->Buffer.NumElements;
            rec["structure_byte_stride"] = desc->Buffer.StructureByteStride;
            break;
        default:
            break;
        }
    }
    m_impl->publish_descriptor_locked(std::move(rec));
    } STEREO_FORENSICS_CATCH_VOID("record_srv_descriptor")
}

void StereoForensics::record_uav_descriptor(
    std::string_view source,
    ID3D12Resource* resource,
    const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc,
    D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    STEREO_FORENSICS_TRY("record_uav_descriptor") {
    if (!is_enabled() || resource == nullptr || handle.ptr == 0) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
    if (m_impl->capture_stopped) {
        return;
    }
    m_impl->update_resource_locked(source, resource, nullptr, false, 0, 0, nullptr, D3D12_HEAP_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
    auto rec = m_impl->descriptor_base_locked(DescriptorKind::UAV, handle, reinterpret_cast<uintptr_t>(resource));
    rec["source"] = std::string{source};
    if (desc != nullptr) {
        rec["format"] = static_cast<uint32_t>(desc->Format);
        rec["view_dimension"] = static_cast<uint32_t>(desc->ViewDimension);
        switch (desc->ViewDimension) {
        case D3D12_UAV_DIMENSION_TEXTURE2D:
            rec["mip_slice"] = desc->Texture2D.MipSlice;
            rec["plane_slice"] = desc->Texture2D.PlaneSlice;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
            rec["mip_slice"] = desc->Texture2DArray.MipSlice;
            rec["first_array_slice"] = desc->Texture2DArray.FirstArraySlice;
            rec["array_size"] = desc->Texture2DArray.ArraySize;
            rec["plane_slice"] = desc->Texture2DArray.PlaneSlice;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE3D:
            rec["mip_slice"] = desc->Texture3D.MipSlice;
            rec["first_w_slice"] = desc->Texture3D.FirstWSlice;
            rec["w_size"] = desc->Texture3D.WSize;
            break;
        case D3D12_UAV_DIMENSION_BUFFER:
            rec["first_element"] = desc->Buffer.FirstElement;
            rec["num_elements"] = desc->Buffer.NumElements;
            rec["structure_byte_stride"] = desc->Buffer.StructureByteStride;
            break;
        default:
            break;
        }
    }
    m_impl->publish_descriptor_locked(std::move(rec));
    } STEREO_FORENSICS_CATCH_VOID("record_uav_descriptor")
}

void StereoForensics::record_rtv_descriptor(
    std::string_view source,
    ID3D12Resource* resource,
    const D3D12_RENDER_TARGET_VIEW_DESC* desc,
    D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    STEREO_FORENSICS_TRY("record_rtv_descriptor") {
    if (!is_enabled() || resource == nullptr || handle.ptr == 0) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
    if (m_impl->capture_stopped) {
        return;
    }
    m_impl->update_resource_locked(source, resource, nullptr, false, 0, 0, nullptr, D3D12_HEAP_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
    auto rec = m_impl->descriptor_base_locked(DescriptorKind::RTV, handle, reinterpret_cast<uintptr_t>(resource));
    rec["source"] = std::string{source};
    if (desc != nullptr) {
        rec["format"] = static_cast<uint32_t>(desc->Format);
        rec["view_dimension"] = static_cast<uint32_t>(desc->ViewDimension);
        if (desc->ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2DARRAY) {
            rec["mip_slice"] = desc->Texture2DArray.MipSlice;
            rec["first_array_slice"] = desc->Texture2DArray.FirstArraySlice;
            rec["array_size"] = desc->Texture2DArray.ArraySize;
            rec["plane_slice"] = desc->Texture2DArray.PlaneSlice;
        } else if (desc->ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2D) {
            rec["mip_slice"] = desc->Texture2D.MipSlice;
            rec["plane_slice"] = desc->Texture2D.PlaneSlice;
        }
    }
    m_impl->publish_descriptor_locked(std::move(rec));
    } STEREO_FORENSICS_CATCH_VOID("record_rtv_descriptor")
}

void StereoForensics::record_dsv_descriptor(
    std::string_view source,
    ID3D12Resource* resource,
    const D3D12_DEPTH_STENCIL_VIEW_DESC* desc,
    D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    STEREO_FORENSICS_TRY("record_dsv_descriptor") {
    if (!is_enabled() || resource == nullptr || handle.ptr == 0) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
    if (m_impl->capture_stopped) {
        return;
    }
    m_impl->update_resource_locked(source, resource, nullptr, false, 0, 0, nullptr, D3D12_HEAP_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
    auto rec = m_impl->descriptor_base_locked(DescriptorKind::DSV, handle, reinterpret_cast<uintptr_t>(resource));
    rec["source"] = std::string{source};
    if (desc != nullptr) {
        rec["format"] = static_cast<uint32_t>(desc->Format);
        rec["view_dimension"] = static_cast<uint32_t>(desc->ViewDimension);
        rec["flags"] = static_cast<uint32_t>(desc->Flags);
    }
    m_impl->publish_descriptor_locked(std::move(rec));
    } STEREO_FORENSICS_CATCH_VOID("record_dsv_descriptor")
}

void StereoForensics::record_descriptor_copy(
    std::string_view source,
    D3D12_CPU_DESCRIPTOR_HANDLE dst,
    D3D12_CPU_DESCRIPTOR_HANDLE src) {
    STEREO_FORENSICS_TRY("record_descriptor_copy") {
    if (!is_enabled() || dst.ptr == 0 || src.ptr == 0) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
    if (m_impl->capture_stopped) {
        return;
    }
    json rec;
    const auto src_it = m_impl->descriptors.find(static_cast<uintptr_t>(src.ptr));
    if (src_it != m_impl->descriptors.end()) {
        rec = src_it->second;
    } else {
        rec = {
            {"id", m_impl->descriptors.size() + 1},
            {"kind", "Unknown"},
            {"resource", 0},
            {"resource_hex", "0x0"},
            {"first_seen_frame", m_impl->frame}
        };
    }
    rec["cpu"] = static_cast<uintptr_t>(dst.ptr);
    rec["cpu_hex"] = hex_u64(dst.ptr);
    rec["source_cpu"] = static_cast<uintptr_t>(src.ptr);
    rec["source_cpu_hex"] = hex_u64(src.ptr);
    rec["source"] = std::string{source};
    rec["last_seen_frame"] = m_impl->frame;
    m_impl->publish_descriptor_locked(std::move(rec));
    m_impl->push_event_locked({
        {"event_class", "descriptor"},
        {"kind", "copy_descriptor"},
        {"source", std::string{source}},
        {"dst_cpu", static_cast<uintptr_t>(dst.ptr)},
        {"dst_cpu_hex", hex_u64(dst.ptr)},
        {"src_cpu", static_cast<uintptr_t>(src.ptr)},
        {"src_cpu_hex", hex_u64(src.ptr)}
    });
    } STEREO_FORENSICS_CATCH_VOID("record_descriptor_copy")
}

void StereoForensics::record_descriptor_heaps_set(
    std::string_view source,
    uintptr_t command_list,
    uint32_t count,
    ID3D12DescriptorHeap* const* heaps) {
    STEREO_FORENSICS_TRY("record_descriptor_heaps_set") {
    if (!is_enabled()) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
    if (!m_impl->capture_frame_active || m_impl->capture_stopped) {
        return;
    }
    json arr = json::array();
    for (uint32_t i = 0; i < count && heaps != nullptr; ++i) {
        auto* heap = heaps[i];
        if (heap == nullptr) {
            continue;
        }
        const auto key = reinterpret_cast<uintptr_t>(heap);
        arr.push_back({{"heap", key}, {"heap_hex", hex_u64(key)}});
        if (const auto it = m_impl->descriptor_heaps.find(key); it != m_impl->descriptor_heaps.end()) {
            it->second["last_seen_frame"] = m_impl->frame;
        }
    }
    m_impl->push_event_locked({{"event_class", "bind"}, {"kind", "set_descriptor_heaps"}, {"source", std::string{source}}, {"command_list", command_list}, {"command_list_hex", hex_u64(command_list)}, {"heaps", std::move(arr)}});
    } STEREO_FORENSICS_CATCH_VOID("record_descriptor_heaps_set")
}

void StereoForensics::record_render_targets_set(
    std::string_view source,
    uintptr_t command_list,
    uint32_t rtv_count,
    const D3D12_CPU_DESCRIPTOR_HANDLE* rtvs,
    bool single_handle_range,
    uint32_t rtv_stride,
    const D3D12_CPU_DESCRIPTOR_HANDLE* dsv) {
    STEREO_FORENSICS_TRY("record_render_targets_set") {
    if (!is_enabled() || command_list == 0) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
    Impl::BoundTargets targets;
    json rtv_json = json::array();
    for (uint32_t i = 0; i < rtv_count && rtvs != nullptr; ++i) {
        uintptr_t handle = 0;
        if (single_handle_range) {
            handle = static_cast<uintptr_t>(rtvs[0].ptr + static_cast<SIZE_T>(i) * rtv_stride);
        } else {
            handle = static_cast<uintptr_t>(rtvs[i].ptr);
        }
        if (handle == 0) {
            continue;
        }
        targets.rtvs.push_back(handle);
        json item{{"slot", i}, {"cpu", handle}, {"cpu_hex", hex_u64(handle)}};
        if (auto desc = m_impl->descriptor_for_cpu_locked(handle); desc.has_value()) {
            item["descriptor"] = *desc;
        }
        rtv_json.push_back(std::move(item));
    }
    if (dsv != nullptr && dsv->ptr != 0) {
        targets.dsv = static_cast<uintptr_t>(dsv->ptr);
    }
    m_impl->targets_by_command_list[command_list] = std::move(targets);
    if (!m_impl->capture_frame_active || m_impl->capture_stopped) {
        return;
    }
    m_impl->push_event_locked({{"event_class", "bind"}, {"kind", "om_set_render_targets"}, {"source", std::string{source}}, {"command_list", command_list}, {"command_list_hex", hex_u64(command_list)}, {"rtvs", std::move(rtv_json)}, {"dsv", dsv != nullptr ? static_cast<uintptr_t>(dsv->ptr) : 0}});
    } STEREO_FORENSICS_CATCH_VOID("record_render_targets_set")
}

void StereoForensics::record_root_bind(
    std::string_view source,
    uintptr_t command_list,
    uintptr_t pipeline_state,
    int32_t eye_bucket,
    bool graphics,
    std::string_view kind,
    uint32_t root_parameter,
    uintptr_t value,
    uint32_t value_count,
    uint64_t value_hash) {
    STEREO_FORENSICS_TRY("record_root_bind") {
    if (!is_enabled()) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
    if (!m_impl->capture_frame_active || m_impl->capture_stopped) {
        return;
    }
    m_impl->push_event_locked({
        {"event_class", "bind"},
        {"kind", "root_bind"},
        {"source", std::string{source}},
        {"pipeline", graphics ? "graphics" : "compute"},
        {"root_kind", std::string{kind}},
        {"command_list", command_list},
        {"command_list_hex", hex_u64(command_list)},
        {"pipeline_state", pipeline_state},
        {"pipeline_state_hex", hex_u64(pipeline_state)},
        {"eye_bucket", eye_bucket},
        {"root", root_parameter},
        {"value", value},
        {"value_hex", hex_u64(value)},
        {"value_count", value_count},
        {"value_hash", value_hash},
        {"value_hash_hex", hex_u64(value_hash)}
    });
    } STEREO_FORENSICS_CATCH_VOID("record_root_bind")
}

void StereoForensics::record_pso_bind(
    std::string_view source,
    uintptr_t command_list,
    uintptr_t requested_pipeline_state,
    uintptr_t bound_pipeline_state,
    uintptr_t graphics_root_signature,
    uintptr_t compute_root_signature,
    int32_t eye_bucket) {
    STEREO_FORENSICS_TRY("record_pso_bind") {
    if (!is_enabled()) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
    if (!m_impl->capture_frame_active || m_impl->capture_stopped) {
        return;
    }
    auto& registry = ShaderOverrideRegistry::get();
    const auto vs_crc = registry.d3d12_pso_vertex_crc32(bound_pipeline_state);
    const auto ps_crc = registry.d3d12_pso_pixel_crc32(bound_pipeline_state);
    const auto gs_crc = registry.d3d12_pso_geometry_crc32(bound_pipeline_state);
    const auto cs_crc = registry.d3d12_pso_compute_crc32(bound_pipeline_state);
    const auto selected_root_signature = cs_crc != 0 && ps_crc == 0
        ? compute_root_signature
        : graphics_root_signature;
    const auto root_signature_hash = root_signature_hash_for(selected_root_signature, bound_pipeline_state);
    const auto stable_shader_key = shader_key(vs_crc, ps_crc, gs_crc, cs_crc, root_signature_hash);
    m_impl->push_event_locked({
        {"event_class", "bind"},
        {"kind", "set_pso"},
        {"source", std::string{source}},
        {"command_list", command_list},
        {"command_list_hex", hex_u64(command_list)},
        {"requested_pipeline_state", requested_pipeline_state},
        {"requested_pipeline_state_hex", hex_u64(requested_pipeline_state)},
        {"bound_pipeline_state", bound_pipeline_state},
        {"bound_pipeline_state_hex", hex_u64(bound_pipeline_state)},
        {"pipeline_state", bound_pipeline_state},
        {"pipeline_state_hex", hex_u64(bound_pipeline_state)},
        {"pso_uid", pso_uid(bound_pipeline_state)},
        {"graphics_root_signature", graphics_root_signature},
        {"graphics_root_signature_hex", hex_u64(graphics_root_signature)},
        {"compute_root_signature", compute_root_signature},
        {"compute_root_signature_hex", hex_u64(compute_root_signature)},
        {"root_signature", selected_root_signature},
        {"root_signature_hex", hex_u64(selected_root_signature)},
        {"root_signature_hash", root_signature_hash},
        {"root_signature_hash_hex", hex_u64(root_signature_hash)},
        {"eye_bucket", eye_bucket},
        {"vs_crc", vs_crc},
        {"vs_crc_hex", hex_u64(vs_crc)},
        {"ps_crc", ps_crc},
        {"ps_crc_hex", hex_u64(ps_crc)},
        {"gs_crc", gs_crc},
        {"gs_crc_hex", hex_u64(gs_crc)},
        {"cs_crc", cs_crc},
        {"cs_crc_hex", hex_u64(cs_crc)},
        {"shader_uid", shader_uid_json(vs_crc, ps_crc, gs_crc, cs_crc)},
        {"shader_key", stable_shader_key}
    });
    } STEREO_FORENSICS_CATCH_VOID("record_pso_bind")
}

void StereoForensics::record_resource_barriers(
    std::string_view source,
    uintptr_t command_list,
    uint32_t count,
    const D3D12_RESOURCE_BARRIER* barriers) {
    STEREO_FORENSICS_TRY("record_resource_barriers") {
    if (!is_enabled() || barriers == nullptr || count == 0) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
    if (!m_impl->capture_frame_active || m_impl->capture_stopped) {
        return;
    }
    json arr = json::array();
    for (uint32_t i = 0; i < count; ++i) {
        const auto& b = barriers[i];
        json item{{"type", static_cast<uint32_t>(b.Type)}, {"flags", static_cast<uint32_t>(b.Flags)}};
        if (b.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) {
            const auto resource = reinterpret_cast<uintptr_t>(b.Transition.pResource);
            item["resource"] = resource;
            item["resource_hex"] = hex_u64(resource);
            item["subresource"] = b.Transition.Subresource;
            item["state_before"] = static_cast<uint32_t>(b.Transition.StateBefore);
            item["state_after"] = static_cast<uint32_t>(b.Transition.StateAfter);
            if (resource != 0) {
                m_impl->resource_id_locked(resource);
                auto& rec = m_impl->resources[resource];
                rec["last_state"] = static_cast<uint32_t>(b.Transition.StateAfter);
                rec["last_state_frame"] = m_impl->frame;
                rec["last_state_subresource"] = b.Transition.Subresource;
            }
        } else if (b.Type == D3D12_RESOURCE_BARRIER_TYPE_ALIASING) {
            const auto before = reinterpret_cast<uintptr_t>(b.Aliasing.pResourceBefore);
            const auto after = reinterpret_cast<uintptr_t>(b.Aliasing.pResourceAfter);
            item["resource_before"] = before;
            item["resource_before_hex"] = hex_u64(before);
            item["resource_after"] = after;
            item["resource_after_hex"] = hex_u64(after);
            if (before != 0) {
                item["resource_before_alias_generation"] = ++m_impl->alias_barrier_generation_by_resource[before];
            }
            if (after != 0) {
                item["resource_after_alias_generation"] = ++m_impl->alias_barrier_generation_by_resource[after];
            }
        } else if (b.Type == D3D12_RESOURCE_BARRIER_TYPE_UAV) {
            item["resource"] = reinterpret_cast<uintptr_t>(b.UAV.pResource);
            item["resource_hex"] = hex_u64(reinterpret_cast<uintptr_t>(b.UAV.pResource));
        }
        arr.push_back(std::move(item));
    }
    m_impl->push_event_locked({{"event_class", "barrier"}, {"kind", "resource_barrier"}, {"source", std::string{source}}, {"command_list", command_list}, {"command_list_hex", hex_u64(command_list)}, {"barriers", std::move(arr)}});
    } STEREO_FORENSICS_CATCH_VOID("record_resource_barriers")
}

void StereoForensics::record_rtv_clear(
    std::string_view source,
    uintptr_t command_list,
    uintptr_t pipeline_state,
    int32_t eye_bucket,
    D3D12_CPU_DESCRIPTOR_HANDLE rtv,
    const FLOAT color_rgba[4],
    uint32_t rect_count) {
    STEREO_FORENSICS_TRY("record_rtv_clear") {
    if (!is_enabled() || rtv.ptr == 0) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
    if (!m_impl->capture_frame_active || m_impl->capture_stopped) {
        return;
    }
    const auto cpu = static_cast<uintptr_t>(rtv.ptr);
    uintptr_t resource = 0;
    if (auto desc = m_impl->descriptor_for_cpu_locked(cpu); desc.has_value()) {
        resource = desc->value("resource", 0ull);
    }
    json event{
        {"event_class", "work"},
        {"kind", "clear_rtv"},
        {"source", std::string{source}},
        {"command_list", command_list},
        {"command_list_hex", hex_u64(command_list)},
        {"pipeline_state", pipeline_state},
        {"pipeline_state_hex", hex_u64(pipeline_state)},
        {"eye_bucket", eye_bucket},
        {"rtv", cpu},
        {"rtv_hex", hex_u64(cpu)},
        {"rect_count", rect_count},
        {"color", color_rgba != nullptr ? json::array({color_rgba[0], color_rgba[1], color_rgba[2], color_rgba[3]}) : json::array()}
    };
    event["writes"] = json::array({m_impl->write_json_locked("clear_rtv", cpu, resource, 0)});
    const auto pushed = m_impl->push_event_locked(event);
    if (pushed != 0) {
        m_impl->note_writes_locked(event, pushed);
    }
    } STEREO_FORENSICS_CATCH_VOID("record_rtv_clear")
}

void StereoForensics::record_resource_copy(
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
    uint64_t src_byte_offset) {
    STEREO_FORENSICS_TRY("record_resource_copy") {
    if (!is_enabled() || (dst_resource == 0 && src_resource == 0)) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
    if (!m_impl->capture_frame_active || m_impl->capture_stopped) {
        return;
    }
    json reads = json::array();
    if (src_resource != 0) {
        json read{{"resource", src_resource}, {"resource_hex", hex_u64(src_resource)}, {"resource_id", m_impl->resource_id_locked(src_resource)}, {"subresource", src_subresource}, {"descriptor_type", "RESOURCE"}};
        if (const auto producer = m_impl->last_writer_by_resource.find(src_resource); producer != m_impl->last_writer_by_resource.end()) {
            read["producer"] = {{"frame", producer->second.frame}, {"event_index", producer->second.event_index}, {"kind", producer->second.kind}, {"eye_bucket", producer->second.eye_bucket}};
        }
        reads.push_back(std::move(read));
    }
    json writes = json::array();
    if (dst_resource != 0) {
        writes.push_back(m_impl->write_json_locked(kind, dst_resource, dst_resource, dst_subresource));
    }
    json event{
        {"event_class", "work"},
        {"kind", std::string{kind}},
        {"source", std::string{source}},
        {"command_list", command_list},
        {"command_list_hex", hex_u64(command_list)},
        {"dst_resource", dst_resource},
        {"dst_resource_hex", hex_u64(dst_resource)},
        {"src_resource", src_resource},
        {"src_resource_hex", hex_u64(src_resource)},
        {"dst_subresource", dst_subresource},
        {"src_subresource", src_subresource},
        {"byte_count", byte_count},
        {"width", width},
        {"height", height},
        {"depth", depth},
        {"dst_byte_offset", dst_byte_offset},
        {"src_byte_offset", src_byte_offset},
        {"descriptor_reads", std::move(reads)},
        {"writes", std::move(writes)}
    };
    const auto pushed = m_impl->push_event_locked(event);
    if (pushed != 0) {
        m_impl->note_writes_locked(event, pushed);
    }
    } STEREO_FORENSICS_CATCH_VOID("record_resource_copy")
}

void StereoForensics::record_draw_or_dispatch(
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
    const std::vector<D3D12Diagnostics::DescriptorReadInfo>& descriptor_reads) {
    STEREO_FORENSICS_TRY("record_draw_or_dispatch") {
    if (!is_enabled()) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
    if (!m_impl->capture_frame_active || m_impl->capture_stopped) {
        return;
    }
    auto& registry = ShaderOverrideRegistry::get();
    const auto vs_crc = registry.d3d12_pso_vertex_crc32(pipeline_state);
    const auto ps_crc = registry.d3d12_pso_pixel_crc32(pipeline_state);
    const auto gs_crc = registry.d3d12_pso_geometry_crc32(pipeline_state);
    const auto cs_crc = registry.d3d12_pso_compute_crc32(pipeline_state);
    const auto root_signature_hash = root_signature_hash_for(root_signature, pipeline_state);
    const auto stable_shader_key = shader_key(vs_crc, ps_crc, gs_crc, cs_crc, root_signature_hash);

    json reads = json::array();
    for (const auto& read : descriptor_reads) {
        reads.push_back(m_impl->descriptor_use_locked(read, root_signature, pipeline_state));
    }

    json writes = json::array();
    const bool is_dispatch = kind == "dispatch" || kind == "dispatch_mesh";
    if (is_dispatch) {
        for (const auto& read : reads) {
            if (read.value("descriptor_type", std::string{}) == "UAV") {
                writes.push_back(m_impl->write_json_locked("uav", read.value("cpu", 0ull), read.value("resource", 0ull), read.value("slot", 0u)));
            }
        }
    } else {
        if (const auto it = m_impl->targets_by_command_list.find(command_list); it != m_impl->targets_by_command_list.end()) {
            for (size_t i = 0; i < it->second.rtvs.size(); ++i) {
                const auto rtv = it->second.rtvs[i];
                uintptr_t resource = 0;
                if (auto desc = m_impl->descriptor_for_cpu_locked(rtv); desc.has_value()) {
                    resource = desc->value("resource", 0ull);
                }
                if (resource != 0) {
                    writes.push_back(m_impl->write_json_locked("rtv", rtv, resource, static_cast<uint32_t>(i)));
                }
            }
        }
    }

    uintptr_t rtv0_resource = 0;
    std::string rtv0_desc_key;
    if (!writes.empty()) {
        rtv0_resource = writes[0].value("resource", 0ull);
        rtv0_desc_key = writes[0].value("resource_desc_key", std::string{});
    }

    json event{
        {"event_class", "work"},
        {"kind", std::string{kind}},
        {"source", std::string{source}},
        {"command_list", command_list},
        {"command_list_hex", hex_u64(command_list)},
        {"pipeline_state", pipeline_state},
        {"pipeline_state_hex", hex_u64(pipeline_state)},
        {"pso_uid", pso_uid(pipeline_state)},
        {"root_signature", root_signature},
        {"root_signature_hex", hex_u64(root_signature)},
        {"root_signature_hash", root_signature_hash},
        {"root_signature_hash_hex", hex_u64(root_signature_hash)},
        {"root_signature_layout", root_signature_layout_json(root_signature, pipeline_state)},
        {"eye_bucket", eye_bucket},
        {"executed", executed},
        {"vs_crc", vs_crc},
        {"vs_crc_hex", hex_u64(vs_crc)},
        {"ps_crc", ps_crc},
        {"ps_crc_hex", hex_u64(ps_crc)},
        {"gs_crc", gs_crc},
        {"gs_crc_hex", hex_u64(gs_crc)},
        {"cs_crc", cs_crc},
        {"cs_crc_hex", hex_u64(cs_crc)},
        {"shader_uid", shader_uid_json(vs_crc, ps_crc, gs_crc, cs_crc)},
        {"shader_key", stable_shader_key},
        {"viewport", {{"valid", has_viewport}, {"x", viewport_top_left_x}, {"y", viewport_top_left_y}, {"width", viewport_width}, {"height", viewport_height}, {"count", viewport_count}}},
        {"scissor", {{"valid", has_scissor}, {"left", scissor_left}, {"top", scissor_top}, {"right", scissor_right}, {"bottom", scissor_bottom}, {"count", scissor_count}}},
        {"arg0", arg0},
        {"arg1", arg1},
        {"arg2", arg2},
        {"arg3", arg3},
        {"arg4", arg4},
        {"rtv0_resource", rtv0_resource},
        {"rtv0_resource_hex", hex_u64(rtv0_resource)},
        {"rtv0_desc_key", rtv0_desc_key},
        {"graphics_root_descriptor_tables", nonzero_roots_json(graphics_root_descriptor_tables)},
        {"compute_root_descriptor_tables", nonzero_roots_json(compute_root_descriptor_tables)},
        {"graphics_root_cbvs", nonzero_roots_json(graphics_root_cbvs)},
        {"compute_root_cbvs", nonzero_roots_json(compute_root_cbvs)},
        {"graphics_root_srvs", nonzero_roots_json(graphics_root_srvs)},
        {"compute_root_srvs", nonzero_roots_json(compute_root_srvs)},
        {"graphics_root_uavs", nonzero_roots_json(graphics_root_uavs)},
        {"compute_root_uavs", nonzero_roots_json(compute_root_uavs)},
        {"graphics_cbv_hashes", nonzero_hashes_json(graphics_root_cbv_hash)},
        {"compute_cbv_hashes", nonzero_hashes_json(compute_root_cbv_hash)},
        {"graphics_constants_hashes", nonzero_hashes_json(graphics_root_constants_hash)},
        {"compute_constants_hashes", nonzero_hashes_json(compute_root_constants_hash)},
        {"graphics_descriptor_table_resource_hashes", nonzero_hashes_json(graphics_root_descriptor_table_resource_hash)},
        {"compute_descriptor_table_resource_hashes", nonzero_hashes_json(compute_root_descriptor_table_resource_hash)},
        {"descriptor_reads", std::move(reads)},
        {"writes", std::move(writes)}
    };
    const auto pushed = m_impl->push_event_locked(event);
    if (pushed != 0) {
        m_impl->note_writes_locked(event, pushed);
    }
    } STEREO_FORENSICS_CATCH_VOID("record_draw_or_dispatch")
}

bool StereoForensics::should_skip_event(
    std::string_view kind,
    uint32_t ps_crc,
    uint32_t cs_crc,
    int32_t eye_bucket) {
    STEREO_FORENSICS_TRY("should_skip_event") {
    if (!is_enabled() || !experiments_enabled()) {
        return false;
    }
    std::scoped_lock _{m_impl->mutex};
    for (auto& rule : m_impl->experiments) {
        if (!rule.enabled) {
            continue;
        }
        if (rule.action != "skip" && rule.action != "skip_draw" && rule.action != "skip_dispatch") {
            continue;
        }
        if (!rule.kind.empty() && rule.kind != kind) {
            continue;
        }
        if (rule.eye_bucket != -1 && rule.eye_bucket != eye_bucket) {
            continue;
        }
        if (rule.ps_crc != 0 && rule.ps_crc != ps_crc) {
            continue;
        }
        if (rule.cs_crc != 0 && rule.cs_crc != cs_crc) {
            continue;
        }
        ++rule.hits;
        m_impl->push_event_locked({
            {"event_class", "experiment"},
            {"kind", "experiment_skip_match"},
            {"name", rule.name},
            {"action", rule.action},
            {"target_kind", std::string{kind}},
            {"ps_crc", ps_crc},
            {"ps_crc_hex", hex_u64(ps_crc)},
            {"cs_crc", cs_crc},
            {"cs_crc_hex", hex_u64(cs_crc)},
            {"eye_bucket", eye_bucket},
            {"hits", rule.hits}
        });
        return true;
    }
    return false;
    } STEREO_FORENSICS_CATCH_RETURN("should_skip_event", false)
}

void StereoForensics::record_experiment_observation(
    std::string_view name,
    std::string_view action,
    std::string_view outcome,
    std::string_view kind,
    uint32_t ps_crc,
    uint32_t cs_crc,
    int32_t eye_bucket) {
    STEREO_FORENSICS_TRY("record_experiment_observation") {
    if (!is_enabled() || !experiments_enabled()) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
    const auto key = std::string{name} + "|" + std::string{action} + "|" + std::string{outcome};
    auto& rec = m_impl->experiment_observations[key];
    if (rec.empty()) {
        rec = {
            {"name", std::string{name}},
            {"action", std::string{action}},
            {"outcome", std::string{outcome}},
            {"count", 0u},
            {"last_frame", m_impl->frame},
            {"last_kind", std::string{kind}},
            {"last_eye_bucket", eye_bucket},
            {"last_ps_crc", ps_crc},
            {"last_ps_crc_hex", hex_u64(ps_crc)},
            {"last_cs_crc", cs_crc},
            {"last_cs_crc_hex", hex_u64(cs_crc)}
        };
    }
    rec["count"] = rec.value("count", 0ull) + 1ull;
    rec["last_frame"] = m_impl->frame;
    rec["last_kind"] = std::string{kind};
    rec["last_eye_bucket"] = eye_bucket;
    rec["last_ps_crc"] = ps_crc;
    rec["last_ps_crc_hex"] = hex_u64(ps_crc);
    rec["last_cs_crc"] = cs_crc;
    rec["last_cs_crc_hex"] = hex_u64(cs_crc);
    m_impl->push_event_locked({
        {"event_class", "experiment"},
        {"kind", "experiment_observation"},
        {"name", std::string{name}},
        {"action", std::string{action}},
        {"outcome", std::string{outcome}},
        {"target_kind", std::string{kind}},
        {"ps_crc", ps_crc},
        {"ps_crc_hex", hex_u64(ps_crc)},
        {"cs_crc", cs_crc},
        {"cs_crc_hex", hex_u64(cs_crc)},
        {"eye_bucket", eye_bucket},
        {"count", rec.value("count", 0ull)}
    });
    } STEREO_FORENSICS_CATCH_VOID("record_experiment_observation")
}

} // namespace render
