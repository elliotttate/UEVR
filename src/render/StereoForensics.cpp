#include "render/StereoForensics.hpp"

#include <Windows.h>

#include <algorithm>
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

std::string descriptor_view_uid(const std::string& view_key) {
    return view_key.empty() ? std::string{} : ("view:" + view_key);
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

std::string descriptor_view_key(const json& descriptor, uint64_t fallback_resource = 0, std::string_view fallback_kind = {}) {
    const auto resource = descriptor.value("resource", fallback_resource);
    const auto kind = descriptor.value("kind", std::string{fallback_kind});
    const auto format = descriptor.value("format", 0u);
    const auto view_dimension = descriptor.value("view_dimension", 0u);
    const auto mip = descriptor.contains("most_detailed_mip")
        ? descriptor.value("most_detailed_mip", 0u)
        : descriptor.value("mip_slice", 0u);
    const auto mip_levels = descriptor.value("mip_levels", 0u);
    const auto first_array_slice = descriptor.value("first_array_slice", 0u);
    const auto array_size = descriptor.value("array_size", 0u);
    const auto plane_slice = descriptor.value("plane_slice", 0u);
    const auto first_w_slice = descriptor.value("first_w_slice", 0u);
    const auto w_size = descriptor.value("w_size", 0u);
    const auto first_element = descriptor.value("first_element", 0ull);
    const auto num_elements = descriptor.value("num_elements", 0u);
    const auto buffer_location = descriptor.value("buffer_location", 0ull);
    const auto size_in_bytes = descriptor.value("size_in_bytes", 0u);

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
    uint64_t max_events_per_frame{env_u64("UEVR_STEREO_FORENSICS_MAX_EVENTS_PER_FRAME", 100000)};
    uint64_t max_descriptors{env_u64("UEVR_STEREO_FORENSICS_MAX_DESCRIPTORS", 262144)};
    uint64_t max_writer_history{env_u64("UEVR_STEREO_FORENSICS_MAX_WRITER_HISTORY", 100000)};
    std::filesystem::path base_dir{env_string("UEVR_STEREO_FORENSICS_DIR", "C:\\tmp\\uevr_forensics")};
    std::filesystem::path session;
    bool frame_started{};
    uint64_t frame{};
    uint64_t event_index{};
    uint64_t dropped_events{};
    json frame_context = json::object();
    std::vector<json> frame_events;
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
            return true;
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

        SPDLOG_INFO("[StereoForensics] session started at {}", session.string());
        return true;
    }

    uint64_t push_event_locked(json event) {
        if (!enabled || !ensure_session_locked()) {
            return 0;
        }
        if (frame_events.size() >= max_events_per_frame) {
            ++dropped_events;
            return 0;
        }

        const auto index = ++event_index;
        event["frame"] = frame;
        event["event_index"] = index;
        event["event_uid"] = event_uid(frame, index);
        frame_events.emplace_back(std::move(event));
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
                {"first_seen_frame", frame},
                {"last_seen_frame", frame},
                {"desc", json::object()},
                {"desc_key", ""}
            };
            it = resources.emplace(resource, std::move(rec)).first;
        } else {
            it->second["last_seen_frame"] = frame;
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
        const bool preserve_placement =
            !placed &&
            heap == 0 &&
            heap_props == nullptr &&
            rec.contains("placed");
        rec["id"] = id;
        rec["resource"] = key;
        rec["resource_hex"] = hex_u64(key);
        rec["resource_uid"] = resource_uid(key);
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
        rec["desc"] = resource_desc_json(actual_desc);
        rec["desc_key"] = resource_desc_key(rec);
        if (!preserve_placement) {
            rec["alias_group"] = placed
                ? (hex_u64(heap) + "+" + hex_u64(heap_offset) + ":" + std::to_string(approx_resource_bytes(actual_desc)))
                : "";
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
            }
        }
        return rec;
    }

    void publish_descriptor_locked(json rec) {
        if (descriptors.size() >= max_descriptors && !descriptors.contains(rec.value("cpu", 0ull))) {
            return;
        }
        rec["view_key"] = descriptor_view_key(rec);
        rec["descriptor_view_uid"] = descriptor_view_uid(rec.value("view_key", std::string{}));
        descriptors[rec.value("cpu", 0ull)] = std::move(rec);
    }

    std::optional<json> descriptor_for_cpu_locked(uintptr_t cpu) const {
        const auto it = descriptors.find(cpu);
        if (it == descriptors.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    json descriptor_use_locked(const D3D12Diagnostics::DescriptorReadInfo& read) {
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
            use["resource_uid"] = resource_uid(use.value("resource", 0ull));
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
            {"kind", "static_or_imported"},
            {"has_resource_producer", false},
            {"has_view_producer", false},
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
        if (view_producer != last_writer_by_view.end()) {
            out["kind"] = "frame_produced_view";
        } else if (resource_producer != last_writer_by_resource.end()) {
            out["kind"] = "frame_produced_resource";
        }

        const auto resource_it = resources.find(resource);
        if (resource_it != resources.end()) {
            const auto alias = resource_it->second.value("alias_group", std::string{});
            if (!alias.empty()) {
                out["alias_group"] = alias;
                out["alias_reused"] = true;
            }
            out["resource_desc_key"] = resource_it->second.value("desc_key", std::string{});
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
        std::ofstream out(session / "events.jsonl", std::ios::binary | std::ios::app);
        if (!out) {
            return;
        }
        for (const auto& event : frame_events) {
            out << event.dump() << '\n';
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
        for (const auto& event : frame_events) {
            if (event.value("event_class", std::string{}) != "work") {
                continue;
            }
            const auto consumer = event.value("event_index", 0ull);
            for (const auto& read : event.value("descriptor_reads", json::array())) {
                json edge{
                    {"consumer_event", consumer},
                    {"consumer_kind", event.value("kind", std::string{})},
                    {"consumer_eye_bucket", event.value("eye_bucket", -1)},
                    {"root", read.value("root", 0u)},
                    {"slot", read.value("slot", 0u)},
                    {"descriptor_type", read.value("descriptor_type", std::string{})},
                    {"resource", read.value("resource", 0ull)},
                    {"resource_hex", read.value("resource_hex", std::string{})},
                    {"view_key", read.value("view_key", std::string{})},
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

        return {
            {"frame", frame},
            {"read_edges", std::move(edges)},
            {"latest_resource_producers", std::move(producers)},
            {"latest_view_producers", std::move(view_producers)},
            {"writer_history", std::move(history)}
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
            {"note", "v2 rule files are parsed. StereoForensics executes skip-style actions here; Sn2DebugColorOverride consumes color_override; D3D12Hook executes supported mutation actions (swap_cbv_left_to_right, swap_descriptor_from_left, force_srv_array_slice)."},
            {"runtime_capabilities", {
                {"executable_actions", json::array({
                    "skip",
                    "skip_draw",
                    "skip_dispatch",
                    "color_override",
                    "swap_cbv_left_to_right",
                    "swap_descriptor_from_left",
                    "force_srv_array_slice"
                })},
                {"mutation_actions", json::array({
                    "swap_cbv_left_to_right",
                    "swap_descriptor_from_left",
                    "force_srv_array_slice"
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

        try {
            append_events_locked();
            write_json_file_locked(session / "resources.json", {{"resources", resource_array_locked()}});
            write_json_file_locked(session / "descriptors.json", {{"descriptors", descriptor_array_locked()}});
            write_json_file_locked(session / "descriptor_heaps.json", {{"descriptor_heaps", descriptor_heap_array_locked()}});
            write_json_file_locked(session / "lineage.json", build_lineage_locked());
            write_json_file_locked(session / "eye_diff.json", build_eye_diff_locked());
            write_json_file_locked(session / "experiments.json", experiments_json_locked());

            const auto frame_path = session / "frames" / ("frame_" + std::to_string(frame) + "_summary.json");
            write_json_file_locked(frame_path, {
                {"frame", frame},
                {"event_count", frame_events.size()},
                {"dropped_events", dropped_events},
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
        event_index = 0;
        dropped_events = 0;
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
    if (m_impl != nullptr) {
        std::scoped_lock _{m_impl->mutex};
        m_impl->finalize_frame_locked();
    }
}

bool StereoForensics::is_enabled() const {
    return m_impl != nullptr && m_impl->enabled;
}

bool StereoForensics::experiments_enabled() const {
    return m_impl != nullptr && m_impl->experiments_enabled;
}

std::filesystem::path StereoForensics::session_dir() const {
    if (m_impl == nullptr) {
        return {};
    }
    std::scoped_lock _{m_impl->mutex};
    return m_impl->session;
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
    if (!is_enabled()) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
    m_impl->ensure_session_locked();
    m_impl->finalize_frame_locked();
    ++m_impl->frame;
    m_impl->frame_started = true;
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
        {"framegen_swapchain", framegen_swapchain}
    };
    m_impl->load_experiments_locked();
    m_impl->push_event_locked({{"event_class", "frame"}, {"kind", "begin_frame"}, {"context", m_impl->frame_context}});
}

void StereoForensics::record_descriptor_heap_created(
    std::string_view source,
    ID3D12DescriptorHeap* heap,
    const D3D12_DESCRIPTOR_HEAP_DESC* desc,
    UINT descriptor_stride) {
    if (!is_enabled() || heap == nullptr || desc == nullptr) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
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
}

void StereoForensics::record_resource_created(
    std::string_view source,
    ID3D12Resource* resource,
    const D3D12_RESOURCE_DESC* desc,
    const D3D12_HEAP_PROPERTIES* heap_props,
    D3D12_HEAP_FLAGS heap_flags,
    D3D12_RESOURCE_STATES initial_state) {
    if (!is_enabled() || resource == nullptr) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
    m_impl->update_resource_locked(source, resource, desc, false, 0, 0, heap_props, heap_flags, initial_state);
    const auto key = reinterpret_cast<uintptr_t>(resource);
    m_impl->push_event_locked({{"event_class", "create"}, {"kind", "create_committed_resource"}, {"source", std::string{source}}, {"resource", m_impl->resources[key]}});
}

void StereoForensics::record_placed_resource_created(
    std::string_view source,
    ID3D12Resource* resource,
    ID3D12Heap* heap,
    uint64_t heap_offset,
    const D3D12_RESOURCE_DESC* desc,
    D3D12_RESOURCE_STATES initial_state) {
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
    m_impl->update_resource_locked(source, resource, desc, true, reinterpret_cast<uintptr_t>(heap), heap_offset, &heap_props, heap_flags, initial_state);
    const auto key = reinterpret_cast<uintptr_t>(resource);
    m_impl->push_event_locked({{"event_class", "create"}, {"kind", "create_placed_resource"}, {"source", std::string{source}}, {"resource", m_impl->resources[key]}});
}

void StereoForensics::record_cbv_descriptor(
    std::string_view source,
    const D3D12_CONSTANT_BUFFER_VIEW_DESC* desc,
    D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    if (!is_enabled() || desc == nullptr || handle.ptr == 0) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
    auto rec = m_impl->descriptor_base_locked(DescriptorKind::CBV, handle, 0);
    rec["source"] = std::string{source};
    rec["buffer_location"] = desc->BufferLocation;
    rec["buffer_location_hex"] = hex_u64(desc->BufferLocation);
    rec["size_in_bytes"] = desc->SizeInBytes;
    m_impl->publish_descriptor_locked(std::move(rec));
}

void StereoForensics::record_srv_descriptor(
    std::string_view source,
    ID3D12Resource* resource,
    const D3D12_SHADER_RESOURCE_VIEW_DESC* desc,
    D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    if (!is_enabled() || resource == nullptr || handle.ptr == 0) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
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
}

void StereoForensics::record_uav_descriptor(
    std::string_view source,
    ID3D12Resource* resource,
    const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc,
    D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    if (!is_enabled() || resource == nullptr || handle.ptr == 0) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
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
}

void StereoForensics::record_rtv_descriptor(
    std::string_view source,
    ID3D12Resource* resource,
    const D3D12_RENDER_TARGET_VIEW_DESC* desc,
    D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    if (!is_enabled() || resource == nullptr || handle.ptr == 0) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
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
}

void StereoForensics::record_dsv_descriptor(
    std::string_view source,
    ID3D12Resource* resource,
    const D3D12_DEPTH_STENCIL_VIEW_DESC* desc,
    D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    if (!is_enabled() || resource == nullptr || handle.ptr == 0) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
    m_impl->update_resource_locked(source, resource, nullptr, false, 0, 0, nullptr, D3D12_HEAP_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
    auto rec = m_impl->descriptor_base_locked(DescriptorKind::DSV, handle, reinterpret_cast<uintptr_t>(resource));
    rec["source"] = std::string{source};
    if (desc != nullptr) {
        rec["format"] = static_cast<uint32_t>(desc->Format);
        rec["view_dimension"] = static_cast<uint32_t>(desc->ViewDimension);
        rec["flags"] = static_cast<uint32_t>(desc->Flags);
    }
    m_impl->publish_descriptor_locked(std::move(rec));
}

void StereoForensics::record_descriptor_copy(
    std::string_view source,
    D3D12_CPU_DESCRIPTOR_HANDLE dst,
    D3D12_CPU_DESCRIPTOR_HANDLE src) {
    if (!is_enabled() || dst.ptr == 0 || src.ptr == 0) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
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
}

void StereoForensics::record_descriptor_heaps_set(
    std::string_view source,
    uintptr_t command_list,
    uint32_t count,
    ID3D12DescriptorHeap* const* heaps) {
    if (!is_enabled()) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
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
}

void StereoForensics::record_render_targets_set(
    std::string_view source,
    uintptr_t command_list,
    uint32_t rtv_count,
    const D3D12_CPU_DESCRIPTOR_HANDLE* rtvs,
    bool single_handle_range,
    uint32_t rtv_stride,
    const D3D12_CPU_DESCRIPTOR_HANDLE* dsv) {
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
    m_impl->push_event_locked({{"event_class", "bind"}, {"kind", "om_set_render_targets"}, {"source", std::string{source}}, {"command_list", command_list}, {"command_list_hex", hex_u64(command_list)}, {"rtvs", std::move(rtv_json)}, {"dsv", dsv != nullptr ? static_cast<uintptr_t>(dsv->ptr) : 0}});
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
    if (!is_enabled()) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
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
}

void StereoForensics::record_pso_bind(
    std::string_view source,
    uintptr_t command_list,
    uintptr_t requested_pipeline_state,
    uintptr_t bound_pipeline_state,
    uintptr_t graphics_root_signature,
    uintptr_t compute_root_signature,
    int32_t eye_bucket) {
    if (!is_enabled()) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
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
}

void StereoForensics::record_resource_barriers(
    std::string_view source,
    uintptr_t command_list,
    uint32_t count,
    const D3D12_RESOURCE_BARRIER* barriers) {
    if (!is_enabled() || barriers == nullptr || count == 0) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
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
}

void StereoForensics::record_rtv_clear(
    std::string_view source,
    uintptr_t command_list,
    uintptr_t pipeline_state,
    int32_t eye_bucket,
    D3D12_CPU_DESCRIPTOR_HANDLE rtv,
    const FLOAT color_rgba[4],
    uint32_t rect_count) {
    if (!is_enabled() || rtv.ptr == 0) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
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
    m_impl->note_writes_locked(event, pushed);
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
    if (!is_enabled() || (dst_resource == 0 && src_resource == 0)) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
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
    m_impl->note_writes_locked(event, pushed);
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
    if (!is_enabled()) {
        return;
    }
    std::scoped_lock _{m_impl->mutex};
    auto& registry = ShaderOverrideRegistry::get();
    const auto vs_crc = registry.d3d12_pso_vertex_crc32(pipeline_state);
    const auto ps_crc = registry.d3d12_pso_pixel_crc32(pipeline_state);
    const auto gs_crc = registry.d3d12_pso_geometry_crc32(pipeline_state);
    const auto cs_crc = registry.d3d12_pso_compute_crc32(pipeline_state);
    const auto root_signature_hash = root_signature_hash_for(root_signature, pipeline_state);
    const auto stable_shader_key = shader_key(vs_crc, ps_crc, gs_crc, cs_crc, root_signature_hash);

    json reads = json::array();
    for (const auto& read : descriptor_reads) {
        reads.push_back(m_impl->descriptor_use_locked(read));
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
    m_impl->note_writes_locked(event, pushed);
}

bool StereoForensics::should_skip_event(
    std::string_view kind,
    uint32_t ps_crc,
    uint32_t cs_crc,
    int32_t eye_bucket) {
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
}

void StereoForensics::record_experiment_observation(
    std::string_view name,
    std::string_view action,
    std::string_view outcome,
    std::string_view kind,
    uint32_t ps_crc,
    uint32_t cs_crc,
    int32_t eye_bucket) {
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
}

} // namespace render
