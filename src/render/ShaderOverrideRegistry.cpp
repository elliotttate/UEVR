#include "render/ShaderOverrideRegistry.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>

#include <Windows.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Framework.hpp"
#include "render/D3D12Diagnostics.hpp"
#include "render/ShaderCompiler.hpp"
#include "utility/String.hpp"
// Fallback PS-CRC resolver for stream-form (UE5.6 CreatePipelineState) PSOs that
// are not present in m_d3d12_graphics_pso_records — used by hunter_should_skip_draw_per_eye.
#include "hooks/Sn2PsoBytecodeDumper.hpp"

using json = nlohmann::json;

namespace {
constexpr size_t MAX_RECENT_EVENTS = 64;
constexpr auto AUTO_RELOAD_INTERVAL = std::chrono::milliseconds(30000);
constexpr auto IDLE_AUTO_RELOAD_INTERVAL = std::chrono::milliseconds(60000);
constexpr uint64_t MAX_PSO_BIND_CONTEXT_AGE_FRAMES = 2;
constexpr size_t MAX_PSO_USAGE_ENTRIES = 8;
constexpr size_t HUNTER_MIN_SCENE_PS_SIZE = 1024;
constexpr int HUNTER_DEFAULT_RECENT_FRAME_AGE = 30;

bool env_truthy(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && v[0] != '\0' && v[0] != '0' &&
        std::string_view{v} != "false" && std::string_view{v} != "FALSE" &&
        std::string_view{v} != "off" && std::string_view{v} != "OFF";
}

bool verbose_override_scan_log_enabled() {
    static const bool enabled = env_truthy("UEVR_SHADER_OVERRIDE_VERBOSE_SCAN_LOG");
    return enabled;
}

bool verbose_pso_logging_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        const DWORD len = GetEnvironmentVariableA("UEVR_SHADER_HUNTER_VERBOSE_PSO_LOG", value, sizeof(value));
        return len > 0 && value[0] != '0';
    }();
    return enabled;
}

bool env_flag_enabled(const char* name, bool default_value = false) {
    char value[32]{};
    const DWORD len = GetEnvironmentVariableA(name, value, sizeof(value));
    if (len == 0) {
        return default_value;
    }

    const std::string_view raw{value, std::min<DWORD>(len, static_cast<DWORD>(sizeof(value) - 1))};
    return raw != "0" && raw != "false" && raw != "FALSE" && raw != "off" && raw != "OFF";
}

bool shader_hunter_pretrack_enabled() {
    static const bool enabled = [] {
        char value[32]{};
        if (GetEnvironmentVariableA("UEVR_SHADER_HUNTER_PRETRACK", value, sizeof(value)) > 0) {
            return env_flag_enabled("UEVR_SHADER_HUNTER_PRETRACK");
        }

        return env_flag_enabled("UEVR_ENABLE_D3D12_DIAGNOSTIC_COMMAND_LIST_HOOKS");
    }();
    return enabled;
}

// CRC32 (IEEE 802.3, reflected, polynomial 0xEDB88320). Matches what
// ShaderToggler uses to identify shaders, so we can correlate the user's
// ShaderToggler hash dumps with UEVR's runtime PSO captures.
uint32_t crc32_ieee(const void* data, size_t size) {
    static uint32_t table[256] = {};
    static bool initialized = false;
    if (!initialized) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int j = 0; j < 8; ++j) {
                c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        initialized = true;
    }
    uint32_t crc = 0xFFFFFFFF;
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

void dump_dxbc_once(const void* data, size_t size, uint32_t crc) {
    if (data == nullptr || size < 20) return;
    static std::array<bool, 65536> dumped{};
    const auto bucket = crc & 0xFFFF;
    if (dumped[bucket]) {
        // crc-mod-65536 collision; not perfect dedup but cheap
    }
    dumped[bucket] = true;
    char path[260]{};
    std::snprintf(path, sizeof(path), "C:\\Users\\ellio\\AppData\\Local\\Temp\\uevr_ps_crc%08X_sz%zu.dxbc", crc, size);
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) return;
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, data, static_cast<DWORD>(size), &written, nullptr);
    CloseHandle(h);
}

std::string backend_to_string(render::ShaderOverrideRegistry::Backend backend) {
    switch (backend) {
    case render::ShaderOverrideRegistry::Backend::D3D11:
        return "dx11";
    case render::ShaderOverrideRegistry::Backend::D3D12:
        return "dx12";
    default:
        return "unknown";
    }
}

std::string stage_to_string(render::ShaderOverrideRegistry::Stage stage) {
    switch (stage) {
    case render::ShaderOverrideRegistry::Stage::Vertex:
        return "vs";
    case render::ShaderOverrideRegistry::Stage::Pixel:
        return "ps";
    case render::ShaderOverrideRegistry::Stage::Geometry:
        return "gs";
    case render::ShaderOverrideRegistry::Stage::Compute:
        return "cs";
    case render::ShaderOverrideRegistry::Stage::Amplification:
        return "as";
    case render::ShaderOverrideRegistry::Stage::Mesh:
        return "ms";
    default:
        return "unknown";
    }
}

std::string format_pointer_to_hex(uintptr_t pointer) {
    std::ostringstream ss{};
    ss << "0x" << std::hex << std::uppercase << pointer;
    return ss.str();
}

std::string join_target_names(const std::vector<render::D3D12Diagnostics::BoundTargetInfo>& targets) {
    std::ostringstream ss{};
    for (size_t i = 0; i < targets.size(); ++i) {
        if (i > 0) {
            ss << " | ";
        }
        ss << (targets[i].name.empty() ? format_pointer_to_hex(targets[i].handle) : targets[i].name);
    }
    return ss.str();
}

std::string join_target_keys(const std::vector<render::D3D12Diagnostics::BoundTargetInfo>& targets) {
    std::ostringstream ss{};
    for (size_t i = 0; i < targets.size(); ++i) {
        if (i > 0) {
            ss << ";";
        }
        ss << format_pointer_to_hex(targets[i].handle);
    }
    return ss.str();
}

std::optional<render::ShaderOverrideRegistry::Backend> parse_backend(std::string_view value) {
    if (_stricmp(value.data(), "dx11") == 0) {
        return render::ShaderOverrideRegistry::Backend::D3D11;
    }

    if (_stricmp(value.data(), "dx12") == 0) {
        return render::ShaderOverrideRegistry::Backend::D3D12;
    }

    return std::nullopt;
}

std::optional<render::ShaderOverrideRegistry::Stage> parse_stage(std::string_view value) {
    if (_stricmp(value.data(), "vs") == 0 || _stricmp(value.data(), "vertex") == 0) {
        return render::ShaderOverrideRegistry::Stage::Vertex;
    }

    if (_stricmp(value.data(), "ps") == 0 || _stricmp(value.data(), "pixel") == 0) {
        return render::ShaderOverrideRegistry::Stage::Pixel;
    }

    if (_stricmp(value.data(), "gs") == 0 || _stricmp(value.data(), "geometry") == 0) {
        return render::ShaderOverrideRegistry::Stage::Geometry;
    }

    if (_stricmp(value.data(), "cs") == 0 || _stricmp(value.data(), "compute") == 0) {
        return render::ShaderOverrideRegistry::Stage::Compute;
    }

    if (_stricmp(value.data(), "as") == 0 || _stricmp(value.data(), "amplification") == 0) {
        return render::ShaderOverrideRegistry::Stage::Amplification;
    }

    if (_stricmp(value.data(), "ms") == 0 || _stricmp(value.data(), "mesh") == 0) {
        return render::ShaderOverrideRegistry::Stage::Mesh;
    }

    return std::nullopt;
}

std::string hunter_stage_to_string(render::ShaderOverrideRegistry::HunterStage stage) {
    switch (stage) {
    case render::ShaderOverrideRegistry::HunterStage::Pixel:
        return "pixel";
    case render::ShaderOverrideRegistry::HunterStage::Vertex:
        return "vertex";
    case render::ShaderOverrideRegistry::HunterStage::Compute:
        return "compute";
    default:
        return "unknown";
    }
}

std::optional<render::ShaderCompilerBackend> parse_compiler(std::string_view value) {
    if (_stricmp(value.data(), "auto") == 0) {
        return render::ShaderCompilerBackend::Auto;
    }

    if (_stricmp(value.data(), "dxc") == 0) {
        return render::ShaderCompilerBackend::Dxc;
    }

    if (_stricmp(value.data(), "fxc") == 0) {
        return render::ShaderCompilerBackend::Fxc;
    }

    return std::nullopt;
}

std::string normalize_hash(std::string value) {
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }), value.end());

    if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0) {
        value.erase(0, 2);
    }

    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    return value;
}

bool shader_hunter_compute_suppression_enabled() {
    return env_flag_enabled("UEVR_SHADER_HUNTER_ALLOW_COMPUTE_SUPPRESS", false);
}

bool shader_hunter_pixel_only_mode() {
    static const bool enabled =
        env_flag_enabled("UEVR_SHADER_HUNTER_PIXEL_ONLY", false) ||
        env_flag_enabled("UEVR_SHADER_HUNTER_PERF_MODE", false);
    return enabled;
}

bool shader_hunter_collect_compute_events_enabled() {
    static const bool enabled =
        !shader_hunter_pixel_only_mode() &&
        !env_flag_enabled("UEVR_SHADER_HUNTER_DISABLE_COMPUTE", false) &&
        !env_flag_enabled("UEVR_SHADER_HUNTER_SKIP_COMPUTE", false) &&
        env_flag_enabled("UEVR_SHADER_HUNTER_COLLECT_COMPUTE", true);
    return enabled;
}

bool shader_hunter_collect_indirect_events_enabled() {
    static const bool enabled =
        !env_flag_enabled("UEVR_SHADER_HUNTER_SKIP_INDIRECT", false) &&
        !env_flag_enabled("UEVR_SHADER_HUNTER_SKIP_INDIRECT_COLLECTION", false) &&
        env_flag_enabled("UEVR_SHADER_HUNTER_COLLECT_INDIRECT", true);
    return enabled;
}

bool shader_hunter_disable_compute_dispatch_hook_enabled() {
    static const bool enabled = env_flag_enabled("UEVR_SHADER_HUNTER_DISABLE_COMPUTE", false);
    return enabled;
}

bool shader_hunter_freeze_after_window_enabled() {
    static const bool enabled = env_flag_enabled("UEVR_SHADER_HUNTER_FREEZE_AFTER_WINDOW", false);
    return enabled;
}

bool shader_hunter_stats_log_enabled() {
    static const bool enabled = env_flag_enabled("UEVR_SHADER_HUNTER_STATS_LOG", true);
    return enabled;
}

int shader_hunter_env_frame_window() {
    static const int frames = []() {
        char value[32]{};
        const DWORD len = GetEnvironmentVariableA("UEVR_SHADER_HUNTER_FRAME_WINDOW", value, sizeof(value));
        if (len == 0 || len >= sizeof(value)) {
            return -1;
        }

        char* end = nullptr;
        const long parsed = std::strtol(value, &end, 10);
        return end != value ? static_cast<int>(std::max<long>(0, parsed)) : -1;
    }();
    return frames;
}

const std::unordered_set<std::string>& shader_hunter_suppression_blocklist() {
    static const std::unordered_set<std::string> blocklist = [] {
        std::unordered_set<std::string> values{};
        char raw[2048]{};
        const DWORD len = GetEnvironmentVariableA(
            "UEVR_SHADER_HUNTER_SUPPRESSION_BLOCKLIST",
            raw,
            static_cast<DWORD>(sizeof(raw)));

        if (len == 0) {
            return values;
        }

        std::string token{};
        const std::string_view text{raw, std::min<DWORD>(len, static_cast<DWORD>(sizeof(raw) - 1))};
        for (char c : text) {
            if (c == ',' || c == ';' || std::isspace(static_cast<unsigned char>(c)) != 0) {
                auto normalized = normalize_hash(token);
                if (!normalized.empty()) {
                    values.insert(std::move(normalized));
                }
                token.clear();
            } else {
                token.push_back(c);
            }
        }

        auto normalized = normalize_hash(token);
        if (!normalized.empty()) {
            values.insert(std::move(normalized));
        }

        return values;
    }();

    return blocklist;
}

// Env-driven GLOBAL suppress set — the headless equivalent of the Shader Hunter
// overlay's "Suppress active". Any PS/VS/CS hash (16-hex FNV) or PS CRC32 (8-hex)
// listed in UEVR_SHADER_HUNTER_SUPPRESS is dropped for BOTH eyes at bind time,
// via the same hunter_should_suppress_locked path the overlay uses — no UI, no
// eye_bucket dependency (which the per-eye SKIP_*_ONLY knobs can't satisfy at
// SetPipelineState, where the eye isn't yet known).
const std::unordered_set<std::string>& shader_hunter_global_suppress_set() {
    static const std::unordered_set<std::string> values = [] {
        std::unordered_set<std::string> out{};
        char raw[2048]{};
        const DWORD len = GetEnvironmentVariableA(
            "UEVR_SHADER_HUNTER_SUPPRESS",
            raw,
            static_cast<DWORD>(sizeof(raw)));
        if (len == 0) {
            return out;
        }
        std::string token{};
        const std::string_view text{raw, std::min<DWORD>(len, static_cast<DWORD>(sizeof(raw) - 1))};
        auto flush = [&] {
            if (token.empty()) return;
            auto normalized = normalize_hash(token);
            if (!normalized.empty()) {
                out.insert(std::move(normalized));
            }
            token.clear();
        };
        for (char c : text) {
            if (c == ',' || c == ';' || std::isspace(static_cast<unsigned char>(c)) != 0) {
                flush();
            } else {
                token.push_back(c);
            }
        }
        flush();
        return out;
    }();
    return values;
}

std::string default_profile(render::ShaderOverrideRegistry::Backend backend, render::ShaderOverrideRegistry::Stage stage) {
    if (backend == render::ShaderOverrideRegistry::Backend::D3D12) {
        switch (stage) {
        case render::ShaderOverrideRegistry::Stage::Vertex: return "vs_6_0";
        case render::ShaderOverrideRegistry::Stage::Pixel: return "ps_6_0";
        case render::ShaderOverrideRegistry::Stage::Geometry: return "gs_6_0";
        case render::ShaderOverrideRegistry::Stage::Compute: return "cs_6_0";
        case render::ShaderOverrideRegistry::Stage::Amplification: return "as_6_5";
        case render::ShaderOverrideRegistry::Stage::Mesh: return "ms_6_5";
        default: return "ps_6_0";
        }
    }

    return stage == render::ShaderOverrideRegistry::Stage::Vertex ? "vs_5_0" : "ps_5_0";
}

std::string compiler_to_string(render::ShaderCompilerBackend compiler) {
    switch (compiler) {
    case render::ShaderCompilerBackend::Dxc:
        return "dxc";
    case render::ShaderCompilerBackend::Fxc:
        return "fxc";
    case render::ShaderCompilerBackend::Auto:
    default:
    return "auto";
    }
}

std::string source_kind_to_string(render::ShaderOverrideRegistry::OverrideSourceKind kind) {
    switch (kind) {
    case render::ShaderOverrideRegistry::OverrideSourceKind::Hlsl:
        return "hlsl";
    case render::ShaderOverrideRegistry::OverrideSourceKind::Bytecode:
        return "bytecode";
    case render::ShaderOverrideRegistry::OverrideSourceKind::DxilPatch:
        return "dxil_patch";
    case render::ShaderOverrideRegistry::OverrideSourceKind::DxilTextPatch:
        return "dxil_text_patch";
    case render::ShaderOverrideRegistry::OverrideSourceKind::ContainerPatch:
        return "container_patch";
    case render::ShaderOverrideRegistry::OverrideSourceKind::DxilTransform:
        return "dxil_transform";
    case render::ShaderOverrideRegistry::OverrideSourceKind::DxilSemanticTransform:
        return "dxil_semantic_transform";
    default:
        return "unknown";
    }
}

std::string eye_target_to_string(render::ShaderOverrideRegistry::EyeTarget eye) {
    switch (eye) {
    case render::ShaderOverrideRegistry::EyeTarget::Any: return "any";
    case render::ShaderOverrideRegistry::EyeTarget::Unknown: return "unknown";
    case render::ShaderOverrideRegistry::EyeTarget::Left: return "left";
    case render::ShaderOverrideRegistry::EyeTarget::Right: return "right";
    case render::ShaderOverrideRegistry::EyeTarget::Full: return "full";
    case render::ShaderOverrideRegistry::EyeTarget::Multi: return "multi";
    default: return "any";
    }
}

std::optional<render::ShaderOverrideRegistry::EyeTarget> parse_eye_target(std::string_view value) {
    if (value.empty() || _stricmp(value.data(), "any") == 0 || _stricmp(value.data(), "both") == 0) {
        return render::ShaderOverrideRegistry::EyeTarget::Any;
    }
    if (_stricmp(value.data(), "unknown") == 0) return render::ShaderOverrideRegistry::EyeTarget::Unknown;
    if (_stricmp(value.data(), "left") == 0 || _stricmp(value.data(), "l") == 0) return render::ShaderOverrideRegistry::EyeTarget::Left;
    if (_stricmp(value.data(), "right") == 0 || _stricmp(value.data(), "r") == 0) return render::ShaderOverrideRegistry::EyeTarget::Right;
    if (_stricmp(value.data(), "full") == 0) return render::ShaderOverrideRegistry::EyeTarget::Full;
    if (_stricmp(value.data(), "multi") == 0) return render::ShaderOverrideRegistry::EyeTarget::Multi;
    return std::nullopt;
}

render::ShaderOverrideRegistry::EyeTarget eye_bucket_to_target(int eye_bucket) {
    switch (eye_bucket) {
    case 0: return render::ShaderOverrideRegistry::EyeTarget::Unknown;
    case 1: return render::ShaderOverrideRegistry::EyeTarget::Left;
    case 2: return render::ShaderOverrideRegistry::EyeTarget::Right;
    case 3: return render::ShaderOverrideRegistry::EyeTarget::Full;
    case 4: return render::ShaderOverrideRegistry::EyeTarget::Multi;
    default: return render::ShaderOverrideRegistry::EyeTarget::Unknown;
    }
}

std::string bind_override_kind_to_string(render::ShaderOverrideRegistry::BindOverrideKind kind) {
    switch (kind) {
    case render::ShaderOverrideRegistry::BindOverrideKind::Cbv: return "cbv";
    case render::ShaderOverrideRegistry::BindOverrideKind::RootConstants: return "root_constants";
    default: return "unknown";
    }
}

std::optional<render::ShaderOverrideRegistry::BindOverrideKind> parse_bind_override_kind(std::string_view value) {
    if (_stricmp(value.data(), "cbv") == 0 || _stricmp(value.data(), "constant_buffer") == 0) {
        return render::ShaderOverrideRegistry::BindOverrideKind::Cbv;
    }
    if (_stricmp(value.data(), "constants") == 0 ||
        _stricmp(value.data(), "root_constants") == 0 ||
        _stricmp(value.data(), "32bit_constants") == 0) {
        return render::ShaderOverrideRegistry::BindOverrideKind::RootConstants;
    }
    return std::nullopt;
}

std::filesystem::file_time_type file_write_time_or_empty(const std::filesystem::path& path) {
    std::error_code ec{};
    if (path.empty() || !std::filesystem::exists(path, ec)) {
        return std::filesystem::file_time_type{};
    }

    ec.clear();
    return std::filesystem::last_write_time(path, ec);
}

std::filesystem::path resolve_manifest_relative_path(const std::filesystem::path& manifest_path, const std::string& raw_path) {
    std::filesystem::path path{raw_path};
    if (path.is_relative()) {
        path = manifest_path.parent_path() / path;
    }

    return path.lexically_normal();
}

bool read_binary_file(const std::filesystem::path& path, std::vector<uint8_t>& out, std::string& error_out) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        error_out = "Failed to open " + path.string();
        return false;
    }

    file.seekg(0, std::ios::end);
    const auto end = file.tellg();
    if (end < 0) {
        error_out = "Failed to size " + path.string();
        return false;
    }

    file.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(end));
    if (!out.empty()) {
        file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
        if (!file) {
            error_out = "Failed to read " + path.string();
            out.clear();
            return false;
        }
    }

    return true;
}

bool write_binary_file(const std::filesystem::path& path, const void* data, size_t size, std::string& error_out) {
    std::error_code ec{};
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error_out = "Failed to create " + path.parent_path().string() + ": " + ec.message();
        return false;
    }

    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    if (!file) {
        error_out = "Failed to open " + path.string() + " for writing";
        return false;
    }

    if (size > 0) {
        file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    }

    if (!file) {
        error_out = "Failed to write " + path.string();
        return false;
    }

    return true;
}

std::vector<uint32_t> json_u32_array(const json& value) {
    std::vector<uint32_t> out{};
    if (!value.is_array()) {
        return out;
    }

    out.reserve(value.size());
    for (const auto& item : value) {
        if (item.is_number_unsigned()) {
            out.emplace_back(item.get<uint32_t>());
        } else if (item.is_number_integer()) {
            out.emplace_back(static_cast<uint32_t>(item.get<int64_t>()));
        } else if (item.is_string()) {
            const auto raw = item.get<std::string>();
            out.emplace_back(static_cast<uint32_t>(std::stoul(raw, nullptr, raw.starts_with("0x") || raw.starts_with("0X") ? 16 : 10)));
        }
    }

    return out;
}

std::vector<uint8_t> u32_vector_to_bytes(const std::vector<uint32_t>& values) {
    std::vector<uint8_t> bytes(values.size() * sizeof(uint32_t));
    if (!values.empty()) {
        std::memcpy(bytes.data(), values.data(), bytes.size());
    }
    return bytes;
}

std::vector<uint8_t> hex_string_to_bytes(std::string value) {
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0 || ch == ',' || ch == '_' || ch == '-';
    }), value.end());

    if (value.starts_with("0x") || value.starts_with("0X")) {
        value.erase(0, 2);
    }

    if ((value.size() % 2) != 0) {
        value.insert(value.begin(), '0');
    }

    std::vector<uint8_t> bytes{};
    bytes.reserve(value.size() / 2);
    for (size_t i = 0; i + 1 < value.size(); i += 2) {
        bytes.emplace_back(static_cast<uint8_t>(std::stoul(value.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

std::filesystem::path current_module_dir() {
    HMODULE module{};
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&current_module_dir),
            &module)) {
        return {};
    }

    wchar_t path[MAX_PATH]{};
    const auto len = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
    if (len == 0 || len >= std::size(path)) {
        return {};
    }

    return std::filesystem::path{std::wstring_view{path, len}}.parent_path();
}

std::filesystem::path default_dxil_patch_tool_path() {
    wchar_t env[32768]{};
    const auto len = GetEnvironmentVariableW(L"UEVR_DXIL_PATCH_TOOL", env, static_cast<DWORD>(std::size(env)));
    if (len > 0 && len < std::size(env)) {
        return std::filesystem::path{std::wstring_view{env, len}};
    }

    const auto module_dir = current_module_dir();
    if (!module_dir.empty()) {
        return module_dir / "dxil-patch.exe";
    }

    return "dxil-patch.exe";
}

std::filesystem::path default_dxil_semantic_tool_path() {
    wchar_t env[32768]{};
    const auto len = GetEnvironmentVariableW(L"UEVR_DXIL_SEMANTIC_TOOL", env, static_cast<DWORD>(std::size(env)));
    if (len > 0 && len < std::size(env)) {
        return std::filesystem::path{std::wstring_view{env, len}};
    }

    const auto module_dir = current_module_dir();
    if (!module_dir.empty()) {
        return module_dir / "uevr-dxil-semantic-pass.exe";
    }

    return "uevr-dxil-semantic-pass.exe";
}

std::wstring quote_command_arg(const std::filesystem::path& arg) {
    std::wstring raw = arg.wstring();
    std::wstring out = L"\"";
    for (wchar_t ch : raw) {
        if (ch == L'"') {
            out += L"\\\"";
        } else {
            out += ch;
        }
    }
    out += L"\"";
    return out;
}

std::wstring quote_command_arg(std::wstring_view arg) {
    std::wstring out = L"\"";
    for (wchar_t ch : arg) {
        if (ch == L'"') {
            out += L"\\\"";
        } else {
            out += ch;
        }
    }
    out += L"\"";
    return out;
}

bool run_process_wait(const std::filesystem::path& exe, const std::wstring& arguments, DWORD timeout_ms, DWORD& exit_code, std::string& error_out) {
    std::wstring command_line = quote_command_arg(exe) + L" " + arguments;

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    if (!CreateProcessW(
            nullptr,
            command_line.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &process)) {
        error_out = "Failed to start " + exe.string() + " (GetLastError=" + std::to_string(GetLastError()) + ")";
        return false;
    }

    const DWORD wait_result = WaitForSingleObject(process.hProcess, timeout_ms);
    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 0xFFFFFFFFu);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        error_out = "Timed out running " + exe.string();
        return false;
    }

    if (wait_result != WAIT_OBJECT_0) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        error_out = "Failed waiting for " + exe.string();
        return false;
    }

    if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        error_out = "Failed to get exit code for " + exe.string();
        return false;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

thread_local bool g_inside_d3d12_override_pipeline_creation = false;

class ScopedD3D12OverridePipelineCreation {
public:
    ScopedD3D12OverridePipelineCreation() {
        g_inside_d3d12_override_pipeline_creation = true;
    }

    ~ScopedD3D12OverridePipelineCreation() {
        g_inside_d3d12_override_pipeline_creation = false;
    }
};

std::vector<uint8_t> copy_shader_bytecode_blob(const D3D12_SHADER_BYTECODE& shader) {
    if (shader.pShaderBytecode == nullptr || shader.BytecodeLength == 0) {
        return {};
    }

    const auto* bytes = static_cast<const uint8_t*>(shader.pShaderBytecode);
    return std::vector<uint8_t>{bytes, bytes + shader.BytecodeLength};
}

D3D12_SHADER_BYTECODE make_shader_bytecode_blob(const std::vector<uint8_t>& shader) {
    D3D12_SHADER_BYTECODE out{};
    out.pShaderBytecode = shader.empty() ? nullptr : shader.data();
    out.BytecodeLength = shader.size();
    return out;
}

std::string format_hresult(HRESULT hr) {
    std::ostringstream ss{};
    ss << "0x" << std::hex << std::uppercase << static_cast<uint32_t>(hr);
    return ss.str();
}

constexpr size_t INVALID_STREAM_OFFSET = static_cast<size_t>(-1);

template <typename T>
struct alignas(void*) PipelineStateStreamSubobject {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
    T payload;
};

template <typename T>
constexpr size_t stream_payload_offset() {
    return offsetof(PipelineStateStreamSubobject<T>, payload);
}

template <typename T>
constexpr size_t stream_subobject_size() {
    return sizeof(PipelineStateStreamSubobject<T>);
}

template <typename T>
bool read_stream_payload(const uint8_t* stream_bytes, size_t stream_size, size_t subobject_offset, T& out) {
    const auto payload_offset = subobject_offset + stream_payload_offset<T>();
    const auto payload_end = subobject_offset + stream_subobject_size<T>();

    if (payload_offset > stream_size || payload_end > stream_size) {
        return false;
    }

    std::memcpy(&out, stream_bytes + payload_offset, sizeof(T));
    return true;
}

template <typename T>
bool write_stream_payload(std::vector<uint8_t>& stream_bytes, size_t subobject_offset, const T& value) {
    const auto payload_offset = subobject_offset + stream_payload_offset<T>();
    const auto payload_end = subobject_offset + stream_subobject_size<T>();

    if (payload_offset > stream_bytes.size() || payload_end > stream_bytes.size()) {
        return false;
    }

    std::memcpy(stream_bytes.data() + payload_offset, &value, sizeof(T));
    return true;
}

size_t get_pipeline_stream_subobject_size(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type) {
    switch (type) {
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE:
        return stream_subobject_size<ID3D12RootSignature*>();
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS:
        return stream_subobject_size<D3D12_SHADER_BYTECODE>();
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT:
        return stream_subobject_size<D3D12_STREAM_OUTPUT_DESC>();
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND:
        return stream_subobject_size<D3D12_BLEND_DESC>();
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK:
        return stream_subobject_size<UINT>();
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER:
        return stream_subobject_size<D3D12_RASTERIZER_DESC>();
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL:
        return stream_subobject_size<D3D12_DEPTH_STENCIL_DESC>();
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT:
        return stream_subobject_size<D3D12_INPUT_LAYOUT_DESC>();
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE:
        return stream_subobject_size<D3D12_INDEX_BUFFER_STRIP_CUT_VALUE>();
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY:
        return stream_subobject_size<D3D12_PRIMITIVE_TOPOLOGY_TYPE>();
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS:
        return stream_subobject_size<D3D12_RT_FORMAT_ARRAY>();
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT:
        return stream_subobject_size<DXGI_FORMAT>();
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC:
        return stream_subobject_size<DXGI_SAMPLE_DESC>();
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK:
        return stream_subobject_size<UINT>();
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO:
        return stream_subobject_size<D3D12_CACHED_PIPELINE_STATE>();
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS:
        return stream_subobject_size<D3D12_PIPELINE_STATE_FLAGS>();
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1:
        return stream_subobject_size<D3D12_DEPTH_STENCIL_DESC1>();
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING:
        return stream_subobject_size<D3D12_VIEW_INSTANCING_DESC>();
    default:
        return 0;
    }
}

bool same_bound_shader_info(
    const render::ShaderOverrideRegistry::BoundShaderInfo& lhs,
    const render::ShaderOverrideRegistry::BoundShaderInfo& rhs
) {
    return lhs.known == rhs.known &&
           lhs.backend == rhs.backend &&
           lhs.stage == rhs.stage &&
           lhs.original_pointer == rhs.original_pointer &&
           lhs.bound_pointer == rhs.bound_pointer &&
           lhs.hash == rhs.hash &&
           lhs.crc32 == rhs.crc32 &&
           lhs.override_active == rhs.override_active &&
           lhs.override_name == rhs.override_name &&
           lhs.note == rhs.note;
}

bool same_d3d12_pipeline_pair(
    const render::ShaderOverrideRegistry::D3D12PipelinePairInfo& lhs,
    const render::ShaderOverrideRegistry::D3D12PipelinePairInfo& rhs
) {
    return lhs.original_pipeline_state == rhs.original_pipeline_state &&
           lhs.bound_pipeline_state == rhs.bound_pipeline_state &&
           lhs.pipeline_stream == rhs.pipeline_stream &&
           lhs.tracking_note == rhs.tracking_note &&
           same_bound_shader_info(lhs.vertex_shader, rhs.vertex_shader) &&
           same_bound_shader_info(lhs.pixel_shader, rhs.pixel_shader) &&
           same_bound_shader_info(lhs.geometry_shader, rhs.geometry_shader);
}
} // namespace

namespace render {
void ShaderOverrideRegistry::OwnedD3D12GraphicsPipelineStateDesc::refresh_views() {
    desc.pRootSignature = root_signature.Get();
    desc.VS = make_shader_bytecode_blob(vertex_shader);
    desc.PS = make_shader_bytecode_blob(pixel_shader);
    desc.DS = make_shader_bytecode_blob(domain_shader);
    desc.HS = make_shader_bytecode_blob(hull_shader);
    desc.GS = make_shader_bytecode_blob(geometry_shader);

    for (size_t i = 0; i < input_elements.size() && i < input_semantic_names.size(); ++i) {
        input_elements[i].SemanticName = input_semantic_names[i].c_str();
    }

    desc.InputLayout.pInputElementDescs = input_elements.empty() ? nullptr : input_elements.data();
    desc.InputLayout.NumElements = static_cast<UINT>(input_elements.size());

    for (size_t i = 0; i < stream_output_declarations.size() && i < stream_output_semantic_names.size(); ++i) {
        stream_output_declarations[i].SemanticName = stream_output_semantic_names[i].c_str();
    }

    desc.StreamOutput.pSODeclaration = stream_output_declarations.empty() ? nullptr : stream_output_declarations.data();
    desc.StreamOutput.NumEntries = static_cast<UINT>(stream_output_declarations.size());
    desc.StreamOutput.pBufferStrides = stream_output_strides.empty() ? nullptr : stream_output_strides.data();
    desc.StreamOutput.NumStrides = static_cast<UINT>(stream_output_strides.size());
    desc.CachedPSO = {};
}

void ShaderOverrideRegistry::OwnedD3D12PipelineStateStream::refresh_views() {
    desc.SizeInBytes = stream_bytes.size();
    desc.pPipelineStateSubobjectStream = stream_bytes.empty() ? nullptr : stream_bytes.data();

    if (root_signature_offset != INVALID_STREAM_OFFSET) {
        auto root_signature_ptr = root_signature.Get();
        write_stream_payload<ID3D12RootSignature*>(stream_bytes, root_signature_offset, root_signature_ptr);
    }

    if (vertex_shader_offset != INVALID_STREAM_OFFSET) {
        write_stream_payload<D3D12_SHADER_BYTECODE>(stream_bytes, vertex_shader_offset, make_shader_bytecode_blob(vertex_shader));
    }

    if (pixel_shader_offset != INVALID_STREAM_OFFSET) {
        write_stream_payload<D3D12_SHADER_BYTECODE>(stream_bytes, pixel_shader_offset, make_shader_bytecode_blob(pixel_shader));
    }

    if (domain_shader_offset != INVALID_STREAM_OFFSET) {
        write_stream_payload<D3D12_SHADER_BYTECODE>(stream_bytes, domain_shader_offset, make_shader_bytecode_blob(domain_shader));
    }

    if (hull_shader_offset != INVALID_STREAM_OFFSET) {
        write_stream_payload<D3D12_SHADER_BYTECODE>(stream_bytes, hull_shader_offset, make_shader_bytecode_blob(hull_shader));
    }

    if (geometry_shader_offset != INVALID_STREAM_OFFSET) {
        write_stream_payload<D3D12_SHADER_BYTECODE>(stream_bytes, geometry_shader_offset, make_shader_bytecode_blob(geometry_shader));
    }

    if (compute_shader_offset != INVALID_STREAM_OFFSET) {
        write_stream_payload<D3D12_SHADER_BYTECODE>(stream_bytes, compute_shader_offset, make_shader_bytecode_blob(compute_shader));
    }

    if (amplification_shader_offset != INVALID_STREAM_OFFSET) {
        write_stream_payload<D3D12_SHADER_BYTECODE>(stream_bytes, amplification_shader_offset, make_shader_bytecode_blob(amplification_shader));
    }

    if (mesh_shader_offset != INVALID_STREAM_OFFSET) {
        write_stream_payload<D3D12_SHADER_BYTECODE>(stream_bytes, mesh_shader_offset, make_shader_bytecode_blob(mesh_shader));
    }

    if (input_layout_offset != INVALID_STREAM_OFFSET) {
        for (size_t i = 0; i < input_elements.size() && i < input_semantic_names.size(); ++i) {
            input_elements[i].SemanticName = input_semantic_names[i].c_str();
        }

        D3D12_INPUT_LAYOUT_DESC input_layout{};
        input_layout.pInputElementDescs = input_elements.empty() ? nullptr : input_elements.data();
        input_layout.NumElements = static_cast<UINT>(input_elements.size());
        write_stream_payload<D3D12_INPUT_LAYOUT_DESC>(stream_bytes, input_layout_offset, input_layout);
    }

    if (stream_output_offset != INVALID_STREAM_OFFSET) {
        for (size_t i = 0; i < stream_output_declarations.size() && i < stream_output_semantic_names.size(); ++i) {
            stream_output_declarations[i].SemanticName = stream_output_semantic_names[i].c_str();
        }

        D3D12_STREAM_OUTPUT_DESC stream_output{};
        stream_output.pSODeclaration = stream_output_declarations.empty() ? nullptr : stream_output_declarations.data();
        stream_output.NumEntries = static_cast<UINT>(stream_output_declarations.size());
        stream_output.pBufferStrides = stream_output_strides.empty() ? nullptr : stream_output_strides.data();
        stream_output.NumStrides = static_cast<UINT>(stream_output_strides.size());

        D3D12_STREAM_OUTPUT_DESC existing{};
        if (read_stream_payload<D3D12_STREAM_OUTPUT_DESC>(stream_bytes.data(), stream_bytes.size(), stream_output_offset, existing)) {
            stream_output.RasterizedStream = existing.RasterizedStream;
        }

        write_stream_payload<D3D12_STREAM_OUTPUT_DESC>(stream_bytes, stream_output_offset, stream_output);
    }

    if (cached_pso_offset != INVALID_STREAM_OFFSET) {
        const D3D12_CACHED_PIPELINE_STATE cached_pso{};
        write_stream_payload<D3D12_CACHED_PIPELINE_STATE>(stream_bytes, cached_pso_offset, cached_pso);
    }

    if (view_instancing_offset != INVALID_STREAM_OFFSET) {
        D3D12_VIEW_INSTANCING_DESC view_instancing{};
        if (read_stream_payload<D3D12_VIEW_INSTANCING_DESC>(stream_bytes.data(), stream_bytes.size(), view_instancing_offset, view_instancing)) {
            view_instancing.ViewInstanceCount = static_cast<UINT>(view_instance_locations.size());
            view_instancing.pViewInstanceLocations = view_instance_locations.empty() ? nullptr : view_instance_locations.data();
            write_stream_payload<D3D12_VIEW_INSTANCING_DESC>(stream_bytes, view_instancing_offset, view_instancing);
        }
    }
}

bool ShaderOverrideRegistry::copy_pipeline_state_stream(
    const D3D12_PIPELINE_STATE_STREAM_DESC* desc,
    ShaderOverrideRegistry::OwnedD3D12PipelineStateStream& out,
    std::string& error_out
) {
    if (desc == nullptr || desc->pPipelineStateSubobjectStream == nullptr || desc->SizeInBytes == 0) {
        error_out = "Pipeline stream descriptor was empty";
        return false;
    }

    out = {};
    const auto* source_bytes = static_cast<const uint8_t*>(desc->pPipelineStateSubobjectStream);
    out.stream_bytes.assign(source_bytes, source_bytes + desc->SizeInBytes);

    size_t offset = 0;
    while (offset < out.stream_bytes.size()) {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type{};
        if (offset + sizeof(type) > out.stream_bytes.size()) {
            error_out = "Pipeline stream ended before subobject type";
            return false;
        }

        std::memcpy(&type, out.stream_bytes.data() + offset, sizeof(type));
        const auto subobject_size = get_pipeline_stream_subobject_size(type);

        if (subobject_size == 0 || offset + subobject_size > out.stream_bytes.size()) {
            std::ostringstream ss{};
            ss << "Unsupported or truncated pipeline stream subobject type " << static_cast<uint32_t>(type);
            error_out = ss.str();
            return false;
        }

        switch (type) {
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE: {
            ID3D12RootSignature* root_signature{};
            if (!read_stream_payload<ID3D12RootSignature*>(out.stream_bytes.data(), out.stream_bytes.size(), offset, root_signature)) {
                error_out = "Failed to read pipeline stream root signature";
                return false;
            }

            out.root_signature = root_signature;
            out.root_signature_offset = offset;
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS: {
            D3D12_SHADER_BYTECODE shader{};
            if (!read_stream_payload<D3D12_SHADER_BYTECODE>(out.stream_bytes.data(), out.stream_bytes.size(), offset, shader)) {
                error_out = "Failed to read pipeline stream shader bytecode";
                return false;
            }

            auto* target = &out.vertex_shader;
            auto* target_offset = &out.vertex_shader_offset;

            switch (type) {
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
                target = &out.vertex_shader;
                target_offset = &out.vertex_shader_offset;
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
                target = &out.pixel_shader;
                target_offset = &out.pixel_shader_offset;
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
                target = &out.domain_shader;
                target_offset = &out.domain_shader_offset;
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
                target = &out.hull_shader;
                target_offset = &out.hull_shader_offset;
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
                target = &out.geometry_shader;
                target_offset = &out.geometry_shader_offset;
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
                target = &out.compute_shader;
                target_offset = &out.compute_shader_offset;
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
                target = &out.amplification_shader;
                target_offset = &out.amplification_shader_offset;
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS:
                target = &out.mesh_shader;
                target_offset = &out.mesh_shader_offset;
                break;
            default:
                break;
            }

            *target = copy_shader_bytecode_blob(shader);
            *target_offset = offset;
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT: {
            D3D12_INPUT_LAYOUT_DESC input_layout{};
            if (!read_stream_payload<D3D12_INPUT_LAYOUT_DESC>(out.stream_bytes.data(), out.stream_bytes.size(), offset, input_layout)) {
                error_out = "Failed to read pipeline stream input layout";
                return false;
            }

            out.input_layout_offset = offset;
            out.input_semantic_names.clear();
            out.input_elements.clear();

            if (input_layout.pInputElementDescs != nullptr && input_layout.NumElements > 0) {
                out.input_semantic_names.reserve(input_layout.NumElements);
                out.input_elements.reserve(input_layout.NumElements);

                for (UINT i = 0; i < input_layout.NumElements; ++i) {
                    auto element = input_layout.pInputElementDescs[i];
                    out.input_semantic_names.emplace_back(element.SemanticName != nullptr ? element.SemanticName : "");
                    out.input_elements.emplace_back(element);
                }
            }

            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT: {
            D3D12_STREAM_OUTPUT_DESC stream_output{};
            if (!read_stream_payload<D3D12_STREAM_OUTPUT_DESC>(out.stream_bytes.data(), out.stream_bytes.size(), offset, stream_output)) {
                error_out = "Failed to read pipeline stream output";
                return false;
            }

            out.stream_output_offset = offset;
            out.stream_output_semantic_names.clear();
            out.stream_output_declarations.clear();
            out.stream_output_strides.clear();

            if (stream_output.pSODeclaration != nullptr && stream_output.NumEntries > 0) {
                out.stream_output_semantic_names.reserve(stream_output.NumEntries);
                out.stream_output_declarations.reserve(stream_output.NumEntries);

                for (UINT i = 0; i < stream_output.NumEntries; ++i) {
                    auto declaration = stream_output.pSODeclaration[i];
                    out.stream_output_semantic_names.emplace_back(declaration.SemanticName != nullptr ? declaration.SemanticName : "");
                    out.stream_output_declarations.emplace_back(declaration);
                }
            }

            if (stream_output.pBufferStrides != nullptr && stream_output.NumStrides > 0) {
                out.stream_output_strides.assign(stream_output.pBufferStrides, stream_output.pBufferStrides + stream_output.NumStrides);
            }

            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO:
            out.cached_pso_offset = offset;
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING: {
            D3D12_VIEW_INSTANCING_DESC view_instancing{};
            if (!read_stream_payload<D3D12_VIEW_INSTANCING_DESC>(out.stream_bytes.data(), out.stream_bytes.size(), offset, view_instancing)) {
                error_out = "Failed to read pipeline stream view instancing";
                return false;
            }

            out.view_instancing_offset = offset;
            out.view_instance_locations.clear();

            if (view_instancing.pViewInstanceLocations != nullptr && view_instancing.ViewInstanceCount > 0) {
                out.view_instance_locations.assign(
                    view_instancing.pViewInstanceLocations,
                    view_instancing.pViewInstanceLocations + view_instancing.ViewInstanceCount
                );
            }

            break;
        }
        default:
            break;
        }

        offset += subobject_size;
    }

    out.refresh_views();
    return true;
}

// === ShaderHunter self-test counters ===
// Atomic counters incremented from hot paths so we can verify which hooks
// actually fire and how often. Drained periodically by on_present so a
// "ShaderHunter stats" log line tells us if draw-skip is reaching the GPU.
// Auto-enable EyeDiff from env var. Useful for headless/scripted hunting
// where the user isn't going to click the UI checkbox.
static bool eyediff_auto_enable_from_env() {
    static const bool enabled = []() {
        char buf[4]{};
        return GetEnvironmentVariableA("UEVR_EYE_DIFF_AUTO_ENABLE", buf, sizeof(buf)) > 0
            && buf[0] == '1';
    }();
    return enabled;
}

static std::atomic<uint64_t> g_hunter_setpso_calls{0};
static std::atomic<uint64_t> g_hunter_setpso_skip_true{0};
static std::atomic<uint64_t> g_hunter_draw_hits{0};
static std::atomic<uint64_t> g_hunter_draw_skipped{0};
static std::atomic<uint64_t> g_hunter_draw_indexed_hits{0};
static std::atomic<uint64_t> g_hunter_draw_indexed_skipped{0};
static std::atomic<uint64_t> g_hunter_dispatch_hits{0};
static std::atomic<uint64_t> g_hunter_dispatch_skipped{0};
static std::atomic<uint64_t> g_hunter_execute_indirect_hits{0};
static std::atomic<uint64_t> g_hunter_execute_indirect_skipped{0};
static std::atomic<uint64_t> g_hunter_execute_bundle_hits{0};
static std::atomic<uint64_t> g_hunter_execute_bundle_skipped{0};
static std::atomic<uint64_t> g_hunter_dispatch_mesh_hits{0};
static std::atomic<uint64_t> g_hunter_dispatch_mesh_skipped{0};

ShaderOverrideRegistry& ShaderOverrideRegistry::get() {
    static ShaderOverrideRegistry instance{};
    return instance;
}

void ShaderOverrideRegistry::on_present(Framework&) {
    std::scoped_lock _{m_mutex};
    ++m_frame;

    m_recent_d3d12_pso_churn.push_back(D3D12PsoChurnFrameInfo{
        m_frame,
        m_d3d12_graphics_pso_creations_this_frame,
        m_d3d12_compute_pso_creations_this_frame,
        m_d3d12_stream_pso_creations_this_frame
    });
    if (m_recent_d3d12_pso_churn.size() > 120) {
        m_recent_d3d12_pso_churn.erase(m_recent_d3d12_pso_churn.begin());
    }
    m_d3d12_graphics_pso_creations_this_frame = 0;
    m_d3d12_compute_pso_creations_this_frame = 0;
    m_d3d12_stream_pso_creations_this_frame = 0;

    // === ShaderHunter periodic stats dump ===
    // Every 120 frames (~2s at 60fps), log hook-firing counters so we can tell
    // whether draw-skip is actually reaching the GPU. If draw_hits stays at
    // zero while hunting is active, the diagnostic command-list hooks weren't
    // installed (likely UEVR_ENABLE_D3D12_DIAGNOSTIC_COMMAND_LIST_HOOKS env
    // var didn't propagate to the game process).
    // Auto-enable EyeDiff from env var (once) so headless scripted hunting
    // can find divergent PSOs without user clicking the UI checkbox.
    if (eyediff_auto_enable_from_env() && !m_eyediff_enabled.load(std::memory_order_relaxed)) {
        m_eyediff_enabled.store(true, std::memory_order_relaxed);
        spdlog::info("[EyeDiff] auto-enabled from UEVR_EYE_DIFF_AUTO_ENABLE=1");
    }
    // Periodically dump top divergent PSOs to log so a script can grep them.
    if (m_eyediff_enabled.load(std::memory_order_relaxed) && (m_frame % 300) == 0 && m_frame > 0) {
        const auto entries = eyediff_snapshot_top_divergent(20);
        spdlog::info("[EyeDiff] top {} divergent PSOs (frame {})", entries.size(), m_frame);
        for (size_t i = 0; i < entries.size(); ++i) {
            const auto& e = entries[i];
            spdlog::info("[EyeDiff]  #{} ps={} L_hits={} R_hits={} rtv_div={} desc_div={}",
                i, e.ps_hash, e.bind_count_left, e.bind_count_right,
                e.rtv_divergence_seen, e.desc_divergence_seen);
        }
    }
    if (m_hunter_active.load(std::memory_order_relaxed) &&
            shader_hunter_stats_log_enabled() &&
            (m_frame % 120) == 0) {
        const auto setpso = g_hunter_setpso_calls.exchange(0, std::memory_order_relaxed);
        const auto setpso_skip = g_hunter_setpso_skip_true.exchange(0, std::memory_order_relaxed);
        const auto draws = g_hunter_draw_hits.exchange(0, std::memory_order_relaxed);
        const auto draws_skipped = g_hunter_draw_skipped.exchange(0, std::memory_order_relaxed);
        const auto draws_i = g_hunter_draw_indexed_hits.exchange(0, std::memory_order_relaxed);
        const auto draws_i_skipped = g_hunter_draw_indexed_skipped.exchange(0, std::memory_order_relaxed);
        const auto dispatches = g_hunter_dispatch_hits.exchange(0, std::memory_order_relaxed);
        const auto dispatches_skipped = g_hunter_dispatch_skipped.exchange(0, std::memory_order_relaxed);
        const auto indirect = g_hunter_execute_indirect_hits.exchange(0, std::memory_order_relaxed);
        const auto indirect_skipped = g_hunter_execute_indirect_skipped.exchange(0, std::memory_order_relaxed);
        const auto bundles = g_hunter_execute_bundle_hits.exchange(0, std::memory_order_relaxed);
        const auto bundles_skipped = g_hunter_execute_bundle_skipped.exchange(0, std::memory_order_relaxed);
        const auto meshes = g_hunter_dispatch_mesh_hits.exchange(0, std::memory_order_relaxed);
        const auto meshes_skipped = g_hunter_dispatch_mesh_skipped.exchange(0, std::memory_order_relaxed);
        spdlog::info("[ShaderHunter] stats(2s): set_pipeline_state={} skip_true={} | "
            "draw_instanced={} (skipped {}) | draw_indexed_instanced={} (skipped {}) | "
            "dispatch={} (skipped {}) | execute_indirect={} (skipped {}) | "
            "execute_bundle={} (skipped {}) | dispatch_mesh={} (skipped {}) | "
            "active={} marked={} hunted={}",
            setpso, setpso_skip,
            draws, draws_skipped, draws_i, draws_i_skipped,
            dispatches, dispatches_skipped, indirect, indirect_skipped,
            bundles, bundles_skipped, meshes, meshes_skipped,
            m_hunter_collected.size(), m_hunter_marked.size(), m_hunter_active_hash);
    }

    const auto now = std::chrono::steady_clock::now();
    const auto full_tracking_active = should_track_d3d11_shaders() || should_track_d3d12_pipelines();
    const auto reload_interval = full_tracking_active ? AUTO_RELOAD_INTERVAL : IDLE_AUTO_RELOAD_INTERVAL;

    if (m_force_reload || m_last_scan_time.time_since_epoch().count() == 0 || (now - m_last_scan_time) >= reload_interval) {
        m_force_reload = false;
        m_last_scan_time = now;
        scan_override_directories();
    }
}

void ShaderOverrideRegistry::scan_override_directories_now() {
    std::scoped_lock _{m_mutex};
    m_force_reload = false;
    m_last_scan_time = std::chrono::steady_clock::now();
    scan_override_directories();
}

void ShaderOverrideRegistry::set_inspector_tracking_enabled(bool enabled) {
    m_inspector_tracking_enabled.store(enabled, std::memory_order_relaxed);
}

bool ShaderOverrideRegistry::should_track_d3d11_shaders() const {
    return m_has_active_d3d11_overrides.load(std::memory_order_relaxed) ||
        m_inspector_tracking_enabled.load(std::memory_order_relaxed);
}

bool ShaderOverrideRegistry::should_track_d3d12_pipelines() const {
    // When a headless per-eye skip is configured (UEVR_SHADER_HUNTER_SKIP_{LEFT,RIGHT}_ONLY),
    // enable full pipeline tracking so the bind-time collection records + hash-extracts the
    // actually-bound PSO each frame — exactly what active hunting does. Without this the
    // record only holds PSOs that were created AFTER recording started, so PSOs created during
    // early scene load (e.g. the SkyAtmosphere pass) never resolve at draw-time and the skip
    // can't match them. Cached once (env is read-only at runtime).
    //
    // VALUE-aware, not presence-aware: launchers scrub inherited diagnostics by
    // setting these to "0", which a presence check would read as "enabled" and
    // silently re-engage the SetPipelineState slow tracking path on every bind.
    static const bool skip_configured = []() {
        const auto truthy = [](const char* name) {
            char buf[32]{};
            const auto len = GetEnvironmentVariableA(name, buf, sizeof(buf));
            if (len == 0 || len >= sizeof(buf)) {
                return false;
            }
            const std::string_view v{buf, len};
            return !v.empty() && v != "0" && v != "false" && v != "FALSE" && v != "off" && v != "OFF";
        };
        return truthy("UEVR_SHADER_HUNTER_SKIP_RIGHT_ONLY") ||
               truthy("UEVR_SHADER_HUNTER_SKIP_LEFT_ONLY") ||
               truthy("UEVR_SHADER_HUNTER_SUPPRESS");
    }();
    return m_has_active_d3d12_overrides.load(std::memory_order_relaxed) ||
        m_inspector_tracking_enabled.load(std::memory_order_relaxed) ||
        m_hunter_active.load(std::memory_order_relaxed) ||
        m_hunter_hide_marked.load(std::memory_order_relaxed) ||
        m_capture_next_d3d12_change_hot_path.load(std::memory_order_relaxed) ||
        skip_configured;
}

bool ShaderOverrideRegistry::should_record_d3d12_pipeline_creations() const {
    return should_track_d3d12_pipelines() || shader_hunter_pretrack_enabled();
}

void ShaderOverrideRegistry::request_reload() {
    std::scoped_lock _{m_mutex};
    m_force_reload = true;
}

void ShaderOverrideRegistry::set_runtime_overrides_enabled(bool enabled) {
    m_runtime_overrides_enabled.store(enabled, std::memory_order_relaxed);
    std::scoped_lock _{m_mutex};
    for (auto& [_, record] : m_d3d12_graphics_pso_records) {
        record.logged_substitution = false;
    }
    push_event(enabled ? "Runtime shader overrides enabled" : "Runtime shader overrides disabled");
}

bool ShaderOverrideRegistry::runtime_overrides_enabled() const {
    return m_runtime_overrides_enabled.load(std::memory_order_relaxed);
}

void ShaderOverrideRegistry::request_capture_next_d3d12_change() {
    std::scoped_lock _{m_mutex};
    m_capture_next_d3d12_change = true;
    m_capture_next_d3d12_change_hot_path.store(true, std::memory_order_relaxed);
}

void ShaderOverrideRegistry::clear_captured_d3d12_change() {
    std::scoped_lock _{m_mutex};
    m_capture_next_d3d12_change = false;
    m_capture_next_d3d12_change_hot_path.store(false, std::memory_order_relaxed);
    m_captured_d3d12_pair.reset();
}

bool ShaderOverrideRegistry::export_d3d12_pairs_json(std::filesystem::path& out_path, std::string& error_out) {
    std::scoped_lock _{m_mutex};

    try {
        out_path = make_d3d12_pair_export_path("json");
        std::filesystem::create_directories(out_path.parent_path());

        json root{};
        root["frame"] = m_frame;
        root["total_samples"] = m_total_d3d12_pair_samples;
        root["distinct_pairs"] = json::array();

        std::vector<D3D12PipelinePairInfo> pairs = m_distinct_d3d12_pairs;
        std::stable_sort(pairs.begin(), pairs.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.hit_count != rhs.hit_count) {
                return lhs.hit_count > rhs.hit_count;
            }

            return lhs.last_seen_frame > rhs.last_seen_frame;
        });

        for (const auto& pair : pairs) {
            json entry{};
            entry["first_seen_frame"] = pair.first_seen_frame;
            entry["last_seen_frame"] = pair.last_seen_frame;
            entry["hit_count"] = pair.hit_count;
            entry["sample_share"] = m_total_d3d12_pair_samples > 0
                ? static_cast<double>(pair.hit_count) / static_cast<double>(m_total_d3d12_pair_samples)
                : 0.0;
            entry["original_pipeline_state"] = format_pointer_to_hex(pair.original_pipeline_state);
            entry["bound_pipeline_state"] = format_pointer_to_hex(pair.bound_pipeline_state);
            entry["pipeline_stream"] = pair.pipeline_stream;
            entry["tracking_note"] = pair.tracking_note;
            entry["vertex_shader"] = {
                {"hash", pair.vertex_shader.hash},
                {"known", pair.vertex_shader.known},
                {"override_active", pair.vertex_shader.override_active},
                {"override_name", pair.vertex_shader.override_name},
                {"note", pair.vertex_shader.note}
            };
            entry["pixel_shader"] = {
                {"hash", pair.pixel_shader.hash},
                {"known", pair.pixel_shader.known},
                {"override_active", pair.pixel_shader.override_active},
                {"override_name", pair.pixel_shader.override_name},
                {"note", pair.pixel_shader.note}
            };
            entry["geometry_shader"] = {
                {"hash", pair.geometry_shader.hash},
                {"known", pair.geometry_shader.known},
                {"override_active", pair.geometry_shader.override_active},
                {"override_name", pair.geometry_shader.override_name},
                {"note", pair.geometry_shader.note}
            };

            root["distinct_pairs"].push_back(std::move(entry));
        }

        std::ofstream file{out_path, std::ios::binary | std::ios::trunc};
        file << root.dump(2);
        file.close();

        std::ostringstream ss{};
        ss << "Exported DX12 pair analysis to " << out_path.string();
        push_event(ss.str());
        return true;
    } catch (const std::exception& e) {
        error_out = e.what();
        return false;
    }
}

bool ShaderOverrideRegistry::export_d3d12_pairs_csv(std::filesystem::path& out_path, std::string& error_out) {
    std::scoped_lock _{m_mutex};

    try {
        out_path = make_d3d12_pair_export_path("csv");
        std::filesystem::create_directories(out_path.parent_path());

        std::vector<D3D12PipelinePairInfo> pairs = m_distinct_d3d12_pairs;
        std::stable_sort(pairs.begin(), pairs.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.hit_count != rhs.hit_count) {
                return lhs.hit_count > rhs.hit_count;
            }

            return lhs.last_seen_frame > rhs.last_seen_frame;
        });

        std::ofstream file{out_path, std::ios::binary | std::ios::trunc};
        file << "first_seen_frame,last_seen_frame,hit_count,sample_share,original_pso,bound_pso,pipeline_stream,tracking_note,vs_hash,ps_hash,gs_hash,vs_override,ps_override,gs_override,vs_note,ps_note,gs_note\n";

        auto csv_escape = [](std::string_view value) {
            std::string out{value};
            size_t pos = 0;
            while ((pos = out.find('"', pos)) != std::string::npos) {
                out.insert(pos, 1, '"');
                pos += 2;
            }

            if (out.find_first_of(",\"\n\r") != std::string::npos) {
                out.insert(out.begin(), '"');
                out.push_back('"');
            }

            return out;
        };

        for (const auto& pair : pairs) {
            const auto share = m_total_d3d12_pair_samples > 0
                ? static_cast<double>(pair.hit_count) / static_cast<double>(m_total_d3d12_pair_samples)
                : 0.0;
            file << pair.first_seen_frame << ','
                 << pair.last_seen_frame << ','
                 << pair.hit_count << ','
                 << std::fixed << std::setprecision(6) << share << ','
                 << csv_escape(format_pointer_to_hex(pair.original_pipeline_state)) << ','
                 << csv_escape(format_pointer_to_hex(pair.bound_pipeline_state)) << ','
                 << (pair.pipeline_stream ? "yes" : "no") << ','
                 << csv_escape(pair.tracking_note) << ','
                 << csv_escape(pair.vertex_shader.hash) << ','
                 << csv_escape(pair.pixel_shader.hash) << ','
                 << csv_escape(pair.geometry_shader.hash) << ','
                 << csv_escape(pair.vertex_shader.override_name) << ','
                 << csv_escape(pair.pixel_shader.override_name) << ','
                 << csv_escape(pair.geometry_shader.override_name) << ','
                 << csv_escape(pair.vertex_shader.note) << ','
                 << csv_escape(pair.pixel_shader.note) << ','
                 << csv_escape(pair.geometry_shader.note) << '\n';
        }
        file.close();

        std::ostringstream ss{};
        ss << "Exported DX12 pair analysis to " << out_path.string();
        push_event(ss.str());
        return true;
    } catch (const std::exception& e) {
        error_out = e.what();
        return false;
    }
}

ShaderOverrideRegistry::D3D12ShaderBytecodeInspection ShaderOverrideRegistry::inspect_d3d12_shader_bytecode(
    std::string_view stage,
    std::string_view hash,
    bool disassemble,
    size_t max_disassembly_chars
) const {
    D3D12ShaderBytecodeInspection result{};
    result.requested_stage = std::string{stage};
    result.requested_hash = std::string{hash};
    const std::string requested_hash{hash};

    auto normalized_stage = std::string{stage};
    std::transform(normalized_stage.begin(), normalized_stage.end(), normalized_stage.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    std::vector<uint8_t> bytecode{};

    {
        std::scoped_lock _{m_mutex};

        auto crc_matches = [](uint32_t crc, const std::string& target) {
            if (crc == 0 || target.empty()) {
                return false;
            }

            std::string normalized = target;
            if (normalized.size() > 2 && normalized[0] == '0' && (normalized[1] == 'x' || normalized[1] == 'X')) {
                normalized.erase(0, 2);
            }
            if (normalized.size() != 8) {
                return false;
            }

            char buf[16]{};
            std::snprintf(buf, sizeof(buf), "%08x", crc);
            return _stricmp(buf, normalized.c_str()) == 0;
        };

        auto match_stage = [&](const D3D12GraphicsPsoRecord& record, const char* stage_name, const std::string& candidate_hash, const std::vector<uint8_t>& candidate_bytecode) {
            if (result.found || candidate_hash.empty() || candidate_bytecode.empty()) {
                return;
            }

            const bool stage_matches =
                normalized_stage.empty() ||
                normalized_stage == "any" ||
                normalized_stage == stage_name ||
                (normalized_stage == "vertex" && std::strcmp(stage_name, "vs") == 0) ||
                (normalized_stage == "pixel" && std::strcmp(stage_name, "ps") == 0) ||
                (normalized_stage == "geometry" && std::strcmp(stage_name, "gs") == 0) ||
                (normalized_stage == "compute" && std::strcmp(stage_name, "cs") == 0) ||
                (normalized_stage == "amplification" && std::strcmp(stage_name, "as") == 0) ||
                (normalized_stage == "mesh" && std::strcmp(stage_name, "ms") == 0);

            uint32_t candidate_crc{};
            if (std::strcmp(stage_name, "vs") == 0) {
                candidate_crc = record.vertex_crc32;
            } else if (std::strcmp(stage_name, "ps") == 0) {
                candidate_crc = record.pixel_crc32;
            } else if (std::strcmp(stage_name, "gs") == 0) {
                candidate_crc = record.geometry_crc32;
            } else if (std::strcmp(stage_name, "cs") == 0) {
                candidate_crc = record.compute_crc32;
            } else if (std::strcmp(stage_name, "as") == 0) {
                candidate_crc = record.amplification_crc32;
            } else if (std::strcmp(stage_name, "ms") == 0) {
                candidate_crc = record.mesh_crc32;
            }

            if (!stage_matches || (candidate_hash != requested_hash && !crc_matches(candidate_crc, requested_hash))) {
                return;
            }

            result.found = true;
            result.matched_stage = stage_name;
            result.pipeline_state = record.pipeline_state_pointer;
            result.root_signature = reinterpret_cast<uintptr_t>(
                record.is_pipeline_stream
                    ? record.owned_stream.root_signature.Get()
                    : record.owned_desc.root_signature.Get());
            bytecode = candidate_bytecode;
        };

        for (const auto& [_, record] : m_d3d12_graphics_pso_records) {
            const auto& vs = record.is_pipeline_stream ? record.owned_stream.vertex_shader : record.owned_desc.vertex_shader;
            const auto& ps = record.is_pipeline_stream ? record.owned_stream.pixel_shader : record.owned_desc.pixel_shader;
            const auto& ds = record.is_pipeline_stream ? record.owned_stream.domain_shader : record.owned_desc.domain_shader;
            const auto& hs = record.is_pipeline_stream ? record.owned_stream.hull_shader : record.owned_desc.hull_shader;
            const auto& gs = record.is_pipeline_stream ? record.owned_stream.geometry_shader : record.owned_desc.geometry_shader;

            match_stage(record, "vs", record.vertex_hash, vs);
            match_stage(record, "ps", record.pixel_hash, ps);
            match_stage(record, "gs", record.geometry_hash, gs);
            match_stage(record, "cs", record.compute_hash, record.owned_stream.compute_shader);
            match_stage(record, "as", record.amplification_hash, record.owned_stream.amplification_shader);
            match_stage(record, "ms", record.mesh_hash, record.owned_stream.mesh_shader);

            // These stages are not currently surfaced as hashes in the UI, but
            // allow stage=any lookups by recomputing when needed.
            if (!result.found && (normalized_stage.empty() || normalized_stage == "any" || normalized_stage == "ds")) {
                const auto ds_hash = hash_shader_bytecode(ds.data(), ds.size());
                match_stage(record, "ds", ds_hash, ds);
            }
            if (!result.found && (normalized_stage.empty() || normalized_stage == "any" || normalized_stage == "hs")) {
                const auto hs_hash = hash_shader_bytecode(hs.data(), hs.size());
                match_stage(record, "hs", hs_hash, hs);
            }
            if (!result.found && (normalized_stage.empty() || normalized_stage == "any" || normalized_stage == "gs" || normalized_stage == "geometry")) {
                const auto gs_hash = hash_shader_bytecode(gs.data(), gs.size());
                match_stage(record, "gs", gs_hash, gs);
            }

            if (result.found) {
                break;
            }
        }
    }

    if (!result.found) {
        result.bytecode.error = "No tracked D3D12 shader bytecode matched the requested stage/hash";
        return result;
    }

    result.bytecode = inspect_shader_bytecode(bytecode.data(), bytecode.size(), disassemble, max_disassembly_chars);
    return result;
}

ShaderOverrideRegistry::Snapshot ShaderOverrideRegistry::snapshot() const {
    std::scoped_lock _{m_mutex};

    Snapshot out{};
    out.runtime_overrides_enabled = m_runtime_overrides_enabled.load(std::memory_order_relaxed);
    out.frame = m_frame;
    out.global_override_dir = global_override_dir().string();
    out.profile_override_dir = profile_override_dir().string();
    out.bound_vertex_shader = m_bound_vertex_shader;
    out.bound_pixel_shader = m_bound_pixel_shader;
    out.current_d3d12_pair = m_last_d3d12_pair;
    out.capture_next_d3d12_change_armed = m_capture_next_d3d12_change;
    out.captured_d3d12_pair = m_captured_d3d12_pair;
    out.total_d3d12_pair_samples = m_total_d3d12_pair_samples;
    out.distinct_d3d12_pairs = m_distinct_d3d12_pairs;
    out.total_d3d12_pso_samples = m_total_d3d12_pso_samples;
    out.recent_events = m_recent_events;

    out.d3d12_pso_churn.tracked_pso_count = m_d3d12_graphics_pso_records.size();
    out.d3d12_pso_churn.current_frame_graphics_creations = m_d3d12_graphics_pso_creations_this_frame;
    out.d3d12_pso_churn.current_frame_compute_creations = m_d3d12_compute_pso_creations_this_frame;
    out.d3d12_pso_churn.current_frame_stream_creations = m_d3d12_stream_pso_creations_this_frame;
    out.d3d12_pso_churn.recent_window_frames = m_recent_d3d12_pso_churn.size();
    out.d3d12_pso_churn.recent_frames = m_recent_d3d12_pso_churn;
    for (const auto& frame : m_recent_d3d12_pso_churn) {
        out.d3d12_pso_churn.recent_graphics_creations += frame.graphics_creations;
        out.d3d12_pso_churn.recent_compute_creations += frame.compute_creations;
        out.d3d12_pso_churn.recent_stream_creations += frame.stream_creations;
    }

    out.d3d12_pso_aggregates.reserve(m_d3d12_pso_aggregates.size());
    for (const auto& [_, aggregate] : m_d3d12_pso_aggregates) {
        D3D12PsoAggregateInfo info{};
        info.total_samples = aggregate.total_samples;
        info.sample_share = m_total_d3d12_pso_samples > 0
            ? static_cast<double>(aggregate.total_samples) / static_cast<double>(m_total_d3d12_pso_samples)
            : 0.0;
        info.bind_count_with_known_targets = aggregate.bind_count_with_known_targets;
        info.first_seen_frame = aggregate.first_seen_frame;
        info.last_seen_frame = aggregate.last_seen_frame;
        info.original_pso = aggregate.original_pso;
        info.last_bound_pso = aggregate.last_bound_pso;
        info.pipeline_stream = aggregate.pipeline_stream;
        info.tracking_note = aggregate.tracking_note;
        info.vs_hash = aggregate.vs_hash;
        info.ps_hash = aggregate.ps_hash;
        info.gs_hash = aggregate.gs_hash;
        info.vs_crc32 = aggregate.vs_crc32;
        info.ps_crc32 = aggregate.ps_crc32;
        info.gs_crc32 = aggregate.gs_crc32;
        info.vs_override = aggregate.vs_override;
        info.ps_override = aggregate.ps_override;
        info.gs_override = aggregate.gs_override;

        std::vector<PsoRenderUsageInfo> usages{};
        usages.reserve(aggregate.usage_by_key.size());
        for (const auto& [usage_key, usage] : aggregate.usage_by_key) {
            (void)usage_key;
            PsoRenderUsageInfo usage_info{};
            usage_info.render_target_name = usage.render_target_name;
            usage_info.depth_target_name = usage.depth_target_name;
            usage_info.render_target_key = usage.render_target_key;
            usage_info.depth_target_key = usage.depth_target_key;
            usage_info.hit_count = usage.hit_count;
            usage_info.share = aggregate.bind_count_with_known_targets > 0
                ? static_cast<double>(usage.hit_count) / static_cast<double>(aggregate.bind_count_with_known_targets)
                : 0.0;
            usages.emplace_back(std::move(usage_info));
        }

        std::stable_sort(usages.begin(), usages.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.hit_count != rhs.hit_count) {
                return lhs.hit_count > rhs.hit_count;
            }

            return lhs.render_target_name < rhs.render_target_name;
        });

        if (usages.size() > MAX_PSO_USAGE_ENTRIES) {
            usages.resize(MAX_PSO_USAGE_ENTRIES);
        }

        info.likely_targets = std::move(usages);
        out.d3d12_pso_aggregates.emplace_back(std::move(info));
    }

    std::stable_sort(out.d3d12_pso_aggregates.begin(), out.d3d12_pso_aggregates.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.total_samples != rhs.total_samples) {
            return lhs.total_samples > rhs.total_samples;
        }

        return lhs.last_seen_frame > rhs.last_seen_frame;
    });

    out.overrides.reserve(m_overrides.size());
    for (const auto& [_, entry] : m_overrides) {
        OverrideEntryInfo info{};
        info.key = entry.key;
        info.name = entry.name;
        info.backend = entry.backend;
        info.stage = entry.stage;
        info.target_hash = entry.target_hash;
        info.manifest_path = entry.manifest_path.string();
        info.source_path = entry.source_path.string();
        info.source_kind = source_kind_to_string(entry.source_kind);
        info.entry_point = entry.entry_point;
        info.profile = entry.profile;
        info.enabled = entry.enabled;
        info.compiled = entry.compiled;
        info.apply_supported = entry.apply_supported;
        info.from_profile_dir = entry.from_profile_dir;
        info.per_eye_variants = entry.per_eye_variants;
        info.has_left_payload = entry.left_payload.present;
        info.has_right_payload = entry.right_payload.present;
        info.left_source_kind = entry.left_payload.present ? source_kind_to_string(entry.left_payload.source_kind) : std::string{};
        info.right_source_kind = entry.right_payload.present ? source_kind_to_string(entry.right_payload.source_kind) : std::string{};
        info.generation = entry.generation;
        info.status = entry.status;
        info.compiler = entry.compiler;
        info.last_error = entry.last_error;
        out.overrides.emplace_back(std::move(info));
    }

    std::sort(out.overrides.begin(), out.overrides.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.backend != rhs.backend) {
            return lhs.backend < rhs.backend;
        }
        if (lhs.stage != rhs.stage) {
            return lhs.stage < rhs.stage;
        }
        return lhs.target_hash < rhs.target_hash;
    });

    out.bind_overrides.reserve(m_bind_overrides.size());
    for (const auto& [_, entry] : m_bind_overrides) {
        BindOverrideEntryInfo info{};
        info.key = entry.key;
        info.name = entry.name;
        info.target_hash = entry.target_hash;
        info.stage = entry.any_stage ? "any" : stage_to_string(entry.stage);
        info.pipeline = entry.graphics && entry.compute ? "any" : (entry.graphics ? "graphics" : "compute");
        info.eye = eye_target_to_string(entry.eye);
        info.kind = bind_override_kind_to_string(entry.kind);
        info.root_parameter = entry.root_parameter;
        info.value_count = entry.kind == BindOverrideKind::RootConstants
            ? static_cast<uint32_t>(entry.constants.size())
            : static_cast<uint32_t>(entry.cbv_data.size());
        info.dest_offset = entry.dest_offset;
        info.enabled = entry.enabled;
        info.from_profile_dir = entry.from_profile_dir;
        info.manifest_path = entry.manifest_path.string();
        info.status = entry.status;
        info.last_error = entry.last_error;
        out.bind_overrides.emplace_back(std::move(info));
    }

    std::sort(out.bind_overrides.begin(), out.bind_overrides.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.enabled != rhs.enabled) {
            return lhs.enabled > rhs.enabled;
        }
        return lhs.key < rhs.key;
    });

    return out;
}

void ShaderOverrideRegistry::set_d3d11_create_callbacks(CreateVertexShaderFn create_vs, CreatePixelShaderFn create_ps) {
    std::scoped_lock _{m_mutex};
    m_create_vertex_shader = create_vs;
    m_create_pixel_shader = create_ps;
}

void ShaderOverrideRegistry::register_d3d11_shader_creation(Stage stage, ID3D11Device* device, IUnknown* shader, const void* bytecode, size_t bytecode_size) {
    if (!should_track_d3d11_shaders()) {
        return;
    }

    if (shader == nullptr || bytecode == nullptr || bytecode_size == 0) {
        return;
    }

    std::scoped_lock _{m_mutex};

    const auto shader_ptr = reinterpret_cast<uintptr_t>(shader);
    auto& record = m_d3d11_shader_records[shader_ptr];
    record.stage = stage;
    record.shader_pointer = shader_ptr;
    record.device_pointer = reinterpret_cast<uintptr_t>(device);
    record.hash = hash_shader_bytecode(bytecode, bytecode_size);
    if (record.first_seen_frame == 0) {
        record.first_seen_frame = m_frame;
    }
    record.last_seen_frame = m_frame;
    ++record.seen_count;

    update_d3d11_override_shader(record, device);
}

ID3D11VertexShader* ShaderOverrideRegistry::resolve_d3d11_vertex_shader(ID3D11Device* device, ID3D11VertexShader* shader) {
    if (!should_track_d3d11_shaders()) {
        return shader;
    }

    if (!m_runtime_overrides_enabled.load(std::memory_order_relaxed)) {
        return shader;
    }

    std::scoped_lock _{m_mutex};

    if (shader == nullptr) {
        return nullptr;
    }

    auto it = m_d3d11_shader_records.find(reinterpret_cast<uintptr_t>(shader));
    if (it == m_d3d11_shader_records.end()) {
        return shader;
    }

    auto& record = it->second;
    update_d3d11_override_shader(record, device);

    if (record.override_active && record.override_shader != nullptr) {
        return static_cast<ID3D11VertexShader*>(record.override_shader.Get());
    }

    return shader;
}

ID3D11PixelShader* ShaderOverrideRegistry::resolve_d3d11_pixel_shader(ID3D11Device* device, ID3D11PixelShader* shader) {
    if (!should_track_d3d11_shaders()) {
        return shader;
    }

    if (!m_runtime_overrides_enabled.load(std::memory_order_relaxed)) {
        return shader;
    }

    std::scoped_lock _{m_mutex};

    if (shader == nullptr) {
        return nullptr;
    }

    auto it = m_d3d11_shader_records.find(reinterpret_cast<uintptr_t>(shader));
    if (it == m_d3d11_shader_records.end()) {
        return shader;
    }

    auto& record = it->second;
    update_d3d11_override_shader(record, device);

    if (record.override_active && record.override_shader != nullptr) {
        return static_cast<ID3D11PixelShader*>(record.override_shader.Get());
    }

    return shader;
}

void ShaderOverrideRegistry::note_d3d11_shader_bound(Stage stage, IUnknown* original_shader, IUnknown* bound_shader) {
    if (!should_track_d3d11_shaders()) {
        return;
    }

    std::scoped_lock _{m_mutex};

    BoundShaderInfo info{};
    info.backend = Backend::D3D11;
    info.stage = stage;
    info.original_pointer = reinterpret_cast<uintptr_t>(original_shader);
    info.bound_pointer = reinterpret_cast<uintptr_t>(bound_shader);
    info.last_bound_frame = m_frame;

    if (original_shader == nullptr) {
        info.known = false;
        info.note = "null";
    } else if (const auto it = m_d3d11_shader_records.find(reinterpret_cast<uintptr_t>(original_shader)); it != m_d3d11_shader_records.end()) {
        const auto& record = it->second;
        info.known = true;
        info.hash = record.hash;
        info.override_active = record.override_active;
        info.override_name = record.override_name;
        if (!record.override_active) {
            info.note = "original";
        }
    } else {
        info.known = false;
        info.note = "hash unavailable (created before hook or unsupported stage)";
    }

    if (stage == Stage::Vertex) {
        m_bound_vertex_shader = std::move(info);
    } else {
        m_bound_pixel_shader = std::move(info);
    }
}

void ShaderOverrideRegistry::register_d3d12_graphics_pipeline_state_creation(
    ID3D12Device* device,
    ID3D12PipelineState* pipeline_state,
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc
) {
    if (!should_record_d3d12_pipeline_creations()) {
        return;
    }

    if (device == nullptr || pipeline_state == nullptr || desc == nullptr) {
        return;
    }

    if (g_inside_d3d12_override_pipeline_creation) {
        return;
    }

    std::scoped_lock _{m_mutex};

    auto& record = m_d3d12_graphics_pso_records[reinterpret_cast<uintptr_t>(pipeline_state)];
    record.pipeline_state_pointer = reinterpret_cast<uintptr_t>(pipeline_state);
    record.device = device;
    record.is_pipeline_stream = false;
    record.tracking_note.clear();
    record.owned_stream = {};
    record.compute_desc = {};
    record.owned_desc.desc = *desc;
    record.owned_desc.root_signature = desc->pRootSignature;
    record.owned_desc.vertex_shader = copy_shader_bytecode_blob(desc->VS);
    record.owned_desc.pixel_shader = copy_shader_bytecode_blob(desc->PS);
    record.owned_desc.domain_shader = copy_shader_bytecode_blob(desc->DS);
    record.owned_desc.hull_shader = copy_shader_bytecode_blob(desc->HS);
    record.owned_desc.geometry_shader = copy_shader_bytecode_blob(desc->GS);
    record.owned_desc.input_semantic_names.clear();
    record.owned_desc.input_elements.clear();
    record.owned_desc.stream_output_semantic_names.clear();
    record.owned_desc.stream_output_declarations.clear();
    record.owned_desc.stream_output_strides.clear();

    if (desc->InputLayout.pInputElementDescs != nullptr && desc->InputLayout.NumElements > 0) {
        record.owned_desc.input_semantic_names.reserve(desc->InputLayout.NumElements);
        record.owned_desc.input_elements.reserve(desc->InputLayout.NumElements);

        for (UINT i = 0; i < desc->InputLayout.NumElements; ++i) {
            auto element = desc->InputLayout.pInputElementDescs[i];
            record.owned_desc.input_semantic_names.emplace_back(element.SemanticName != nullptr ? element.SemanticName : "");
            record.owned_desc.input_elements.emplace_back(element);
        }
    }

    if (desc->StreamOutput.pSODeclaration != nullptr && desc->StreamOutput.NumEntries > 0) {
        record.owned_desc.stream_output_semantic_names.reserve(desc->StreamOutput.NumEntries);
        record.owned_desc.stream_output_declarations.reserve(desc->StreamOutput.NumEntries);

        for (UINT i = 0; i < desc->StreamOutput.NumEntries; ++i) {
            auto declaration = desc->StreamOutput.pSODeclaration[i];
            record.owned_desc.stream_output_semantic_names.emplace_back(declaration.SemanticName != nullptr ? declaration.SemanticName : "");
            record.owned_desc.stream_output_declarations.emplace_back(declaration);
        }
    }

    if (desc->StreamOutput.pBufferStrides != nullptr && desc->StreamOutput.NumStrides > 0) {
        record.owned_desc.stream_output_strides.assign(
            desc->StreamOutput.pBufferStrides,
            desc->StreamOutput.pBufferStrides + desc->StreamOutput.NumStrides
        );
    }

    record.owned_desc.refresh_views();
    record.vertex_hash = hash_shader_bytecode(desc->VS.pShaderBytecode, desc->VS.BytecodeLength);
    record.pixel_hash = hash_shader_bytecode(desc->PS.pShaderBytecode, desc->PS.BytecodeLength);
    record.geometry_hash = hash_shader_bytecode(desc->GS.pShaderBytecode, desc->GS.BytecodeLength);
    record.compute_hash.clear();
    record.amplification_hash.clear();
    record.mesh_hash.clear();
    record.vertex_crc32 = (desc->VS.pShaderBytecode != nullptr && desc->VS.BytecodeLength > 0)
        ? crc32_ieee(desc->VS.pShaderBytecode, desc->VS.BytecodeLength) : 0;
    record.pixel_crc32 = (desc->PS.pShaderBytecode != nullptr && desc->PS.BytecodeLength > 0)
        ? crc32_ieee(desc->PS.pShaderBytecode, desc->PS.BytecodeLength) : 0;
    record.geometry_crc32 = (desc->GS.pShaderBytecode != nullptr && desc->GS.BytecodeLength > 0)
        ? crc32_ieee(desc->GS.pShaderBytecode, desc->GS.BytecodeLength) : 0;
    record.compute_crc32 = 0;
    record.amplification_crc32 = 0;
    record.mesh_crc32 = 0;
    record.last_seen_frame = m_frame;
    if (record.first_seen_frame == 0) {
        ++m_d3d12_graphics_pso_creations_this_frame;
        record.first_seen_frame = m_frame;
        record.applied_override_revision = (std::numeric_limits<uint64_t>::max)();
        if (verbose_pso_logging_enabled()) {
            spdlog::info("[ShaderOverrideRegistry] GFX PSO new pso=0x{:x} vs={} ps={} ps_crc32=0x{:08x} ps_size={}",
                record.pipeline_state_pointer, record.vertex_hash, record.pixel_hash, record.pixel_crc32, desc->PS.BytecodeLength);
        }
        // Dump always when we see a new PSO — gives the user the raw DXBC for
        // disassembly/analysis regardless of whether verbose logging is on.
        dump_dxbc_once(desc->PS.pShaderBytecode, desc->PS.BytecodeLength, record.pixel_crc32);
    }
    ++record.seen_count;

    if (m_has_active_d3d12_overrides.load(std::memory_order_relaxed)) {
        update_d3d12_override_pipeline_state(record);
    }
}

void ShaderOverrideRegistry::register_d3d12_compute_pipeline_state_creation(
    ID3D12Device* device,
    ID3D12PipelineState* pipeline_state,
    const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc
) {
    if (!should_record_d3d12_pipeline_creations()) {
        return;
    }

    if (device == nullptr || pipeline_state == nullptr || desc == nullptr) {
        return;
    }

    if (g_inside_d3d12_override_pipeline_creation) {
        return;
    }

    std::scoped_lock _{m_mutex};

    auto& record = m_d3d12_graphics_pso_records[reinterpret_cast<uintptr_t>(pipeline_state)];
    record.pipeline_state_pointer = reinterpret_cast<uintptr_t>(pipeline_state);
    record.device = device;
    record.is_pipeline_stream = false;
    record.tracking_note = "compute pso";
    record.last_error.clear();
    record.override_pipeline_state.Reset();
    record.override_pipeline_state_left.Reset();
    record.override_pipeline_state_right.Reset();
    record.owned_desc = {};
    record.owned_stream = {};
    record.compute_desc = {};
    record.compute_desc = *desc;
    record.owned_stream.root_signature = desc->pRootSignature;
    record.owned_stream.compute_shader = copy_shader_bytecode_blob(desc->CS);
    record.vertex_hash.clear();
    record.pixel_hash.clear();
    record.geometry_hash.clear();
    record.compute_hash = hash_shader_bytecode(desc->CS.pShaderBytecode, desc->CS.BytecodeLength);
    record.amplification_hash.clear();
    record.mesh_hash.clear();
    record.vertex_crc32 = 0;
    record.pixel_crc32 = 0;
    record.geometry_crc32 = 0;
    record.compute_crc32 = (desc->CS.pShaderBytecode != nullptr && desc->CS.BytecodeLength > 0)
        ? crc32_ieee(desc->CS.pShaderBytecode, desc->CS.BytecodeLength) : 0;
    record.amplification_crc32 = 0;
    record.mesh_crc32 = 0;
    record.last_seen_frame = m_frame;

    if (record.first_seen_frame == 0) {
        ++m_d3d12_compute_pso_creations_this_frame;
        record.first_seen_frame = m_frame;
        record.applied_override_revision = (std::numeric_limits<uint64_t>::max)();
        if (verbose_pso_logging_enabled()) {
            spdlog::info("[ShaderOverrideRegistry] COMPUTE PSO new pso=0x{:x} cs={} cs_crc32=0x{:08x} cs_size={}",
                record.pipeline_state_pointer, record.compute_hash, record.compute_crc32, desc->CS.BytecodeLength);
        }
        dump_dxbc_once(desc->CS.pShaderBytecode, desc->CS.BytecodeLength, record.compute_crc32);
    }
    ++record.seen_count;
}

void ShaderOverrideRegistry::register_d3d12_pipeline_state_stream_creation(
    ID3D12Device* device,
    ID3D12PipelineState* pipeline_state,
    const D3D12_PIPELINE_STATE_STREAM_DESC* desc
) {
    if (!should_record_d3d12_pipeline_creations()) {
        return;
    }

    if (device == nullptr || pipeline_state == nullptr || desc == nullptr) {
        return;
    }

    if (g_inside_d3d12_override_pipeline_creation) {
        return;
    }

    std::scoped_lock _{m_mutex};

    auto& record = m_d3d12_graphics_pso_records[reinterpret_cast<uintptr_t>(pipeline_state)];
    record.pipeline_state_pointer = reinterpret_cast<uintptr_t>(pipeline_state);
    record.device = device;
    record.is_pipeline_stream = true;
    record.tracking_note.clear();
    record.last_error.clear();
    record.override_pipeline_state.Reset();
    record.owned_desc = {};
    record.owned_stream = {};

    std::string stream_error{};
    if (!copy_pipeline_state_stream(desc, record.owned_stream, stream_error)) {
        record.tracking_note = "pipeline-stream pso not tracked";
        record.last_error = stream_error;
        std::ostringstream ss{};
        ss << "Failed to track DX12 pipeline-stream PSO 0x" << std::hex << std::uppercase << record.pipeline_state_pointer << ": " << stream_error;
        push_event(ss.str());
        spdlog::warn("[ShaderOverrideRegistry] {}", ss.str());
    }

    record.vertex_hash = hash_shader_bytecode(record.owned_stream.vertex_shader.data(), record.owned_stream.vertex_shader.size());
    record.pixel_hash = hash_shader_bytecode(record.owned_stream.pixel_shader.data(), record.owned_stream.pixel_shader.size());
    record.geometry_hash = hash_shader_bytecode(record.owned_stream.geometry_shader.data(), record.owned_stream.geometry_shader.size());
    record.compute_hash = hash_shader_bytecode(record.owned_stream.compute_shader.data(), record.owned_stream.compute_shader.size());
    record.amplification_hash = hash_shader_bytecode(record.owned_stream.amplification_shader.data(), record.owned_stream.amplification_shader.size());
    record.mesh_hash = hash_shader_bytecode(record.owned_stream.mesh_shader.data(), record.owned_stream.mesh_shader.size());
    record.vertex_crc32 = !record.owned_stream.vertex_shader.empty()
        ? crc32_ieee(record.owned_stream.vertex_shader.data(), record.owned_stream.vertex_shader.size()) : 0;
    record.pixel_crc32 = !record.owned_stream.pixel_shader.empty()
        ? crc32_ieee(record.owned_stream.pixel_shader.data(), record.owned_stream.pixel_shader.size()) : 0;
    record.geometry_crc32 = !record.owned_stream.geometry_shader.empty()
        ? crc32_ieee(record.owned_stream.geometry_shader.data(), record.owned_stream.geometry_shader.size()) : 0;
    record.compute_crc32 = !record.owned_stream.compute_shader.empty()
        ? crc32_ieee(record.owned_stream.compute_shader.data(), record.owned_stream.compute_shader.size()) : 0;
    record.amplification_crc32 = !record.owned_stream.amplification_shader.empty()
        ? crc32_ieee(record.owned_stream.amplification_shader.data(), record.owned_stream.amplification_shader.size()) : 0;
    record.mesh_crc32 = !record.owned_stream.mesh_shader.empty()
        ? crc32_ieee(record.owned_stream.mesh_shader.data(), record.owned_stream.mesh_shader.size()) : 0;
    record.last_seen_frame = m_frame;

    // [SN2-ComputeRec] Unconditional: log EVERY distinct stream-form COMPUTE PSO recorded,
    // so we can tell whether the fog-resolve 0x0930dd4e is created-after-hook (recorded here)
    // or never (precached before the hook). Capped at first 300 distinct crcs.
    if (record.first_seen_frame == 0 && record.compute_crc32 != 0 &&
        env_flag_enabled("UEVR_SN2_LOG_COMPUTE_REC")) {
        static std::atomic<uint64_t> rec_n{0};
        const auto rn = rec_n.fetch_add(1, std::memory_order_relaxed);
        const bool is_resolve = (record.compute_crc32 == 0x0930dd4eu);
        if (rn < 4000 || is_resolve) {
            spdlog::warn("[SN2-ComputeRec] stream COMPUTE PSO #{} cs_crc32=0x{:08x} cs_size={}{}",
                rn + 1, record.compute_crc32, record.owned_stream.compute_shader.size(),
                is_resolve ? "  <== RESOLVE 0930dd4e!" : "");
        }
    }

    if (record.first_seen_frame == 0) {
        ++m_d3d12_stream_pso_creations_this_frame;
        record.first_seen_frame = m_frame;
        record.applied_override_revision = (std::numeric_limits<uint64_t>::max)();
        if (verbose_pso_logging_enabled()) {
            spdlog::info("[ShaderOverrideRegistry] STREAM PSO new pso=0x{:x} vs={} ps={} ps_crc32=0x{:08x} ps_size={}",
                record.pipeline_state_pointer, record.vertex_hash, record.pixel_hash, record.pixel_crc32, record.owned_stream.pixel_shader.size());
        }
        if (!record.owned_stream.pixel_shader.empty()) {
            dump_dxbc_once(record.owned_stream.pixel_shader.data(), record.owned_stream.pixel_shader.size(), record.pixel_crc32);
        }
        if (!record.owned_stream.compute_shader.empty()) {
            dump_dxbc_once(record.owned_stream.compute_shader.data(), record.owned_stream.compute_shader.size(), record.compute_crc32);
        }
    }

    ++record.seen_count;

    if (record.tracking_note.empty()) {
        const bool has_graphics_shader =
            !record.owned_stream.vertex_shader.empty() ||
            !record.owned_stream.pixel_shader.empty() ||
            !record.owned_stream.domain_shader.empty() ||
            !record.owned_stream.hull_shader.empty() ||
            !record.owned_stream.geometry_shader.empty();
        const bool has_mesh_shader =
            !record.owned_stream.amplification_shader.empty() ||
            !record.owned_stream.mesh_shader.empty();

        if (!record.owned_stream.compute_shader.empty() && !has_graphics_shader && !has_mesh_shader) {
            record.tracking_note = "pipeline-stream compute pso";
        } else if (has_mesh_shader && !has_graphics_shader && record.owned_stream.compute_shader.empty()) {
            record.tracking_note = "pipeline-stream mesh pso";
        } else if (!has_graphics_shader && !has_mesh_shader && record.owned_stream.compute_shader.empty()) {
            record.tracking_note = "pipeline-stream pso has no shader bytecode";
        }
    }

    if (m_has_active_d3d12_overrides.load(std::memory_order_relaxed)) {
        update_d3d12_override_pipeline_state(record);
    }
}

ID3D12PipelineState* ShaderOverrideRegistry::resolve_d3d12_pipeline_state(ID3D12PipelineState* pipeline_state) {
    if (!should_track_d3d12_pipelines()) {
        return pipeline_state;
    }

    if (!m_runtime_overrides_enabled.load(std::memory_order_relaxed)) {
        return pipeline_state;
    }

    std::scoped_lock _{m_mutex};

    if (pipeline_state == nullptr) {
        return nullptr;
    }

    const auto it = m_d3d12_graphics_pso_records.find(reinterpret_cast<uintptr_t>(pipeline_state));
    if (it == m_d3d12_graphics_pso_records.end()) {
        return pipeline_state;
    }

    if (!m_has_active_d3d12_overrides.load(std::memory_order_relaxed)) {
        return pipeline_state;
    }

    auto& record = it->second;
    update_d3d12_override_pipeline_state(record);

    if (record.override_active && record.override_pipeline_state != nullptr) {
        if (!record.logged_substitution) {
            record.logged_substitution = true;
            const bool compute_only =
                !record.compute_hash.empty() &&
                record.vertex_hash.empty() &&
                record.pixel_hash.empty() &&
                record.geometry_hash.empty() &&
                record.amplification_hash.empty() &&
                record.mesh_hash.empty();
            spdlog::info("[ShaderOverrideRegistry] >>> SUBSTITUTED stage={} pso=0x{:x} vs_hash={} ps_hash={} gs_hash={} cs_hash={} cs_crc32=0x{:08x} vs_override={} ps_override={} gs_override={} cs_override={}",
                compute_only ? "compute" : "graphics",
                record.pipeline_state_pointer, record.vertex_hash, record.pixel_hash, record.geometry_hash, record.compute_hash, record.compute_crc32,
                record.vertex_override_name, record.pixel_override_name, record.geometry_override_name, record.compute_override_name);
        }
        return record.override_pipeline_state.Get();
    }

    return pipeline_state;
}

ID3D12PipelineState* ShaderOverrideRegistry::resolve_d3d12_pipeline_state_for_eye(ID3D12PipelineState* pipeline_state, int eye_bucket) {
    if (!should_track_d3d12_pipelines() || !m_runtime_overrides_enabled.load(std::memory_order_relaxed)) {
        return pipeline_state;
    }

    std::scoped_lock _{m_mutex};

    const auto it = m_d3d12_graphics_pso_records.find(reinterpret_cast<uintptr_t>(pipeline_state));
    if (it == m_d3d12_graphics_pso_records.end()) {
        return pipeline_state;
    }

    auto& record = it->second;
    update_d3d12_override_pipeline_state(record);

    if (record.override_active && eye_bucket == 1 && record.override_pipeline_state_left != nullptr) {
        return record.override_pipeline_state_left.Get();
    }

    if (record.override_active && eye_bucket == 2 && record.override_pipeline_state_right != nullptr) {
        return record.override_pipeline_state_right.Get();
    }

    if (record.override_active && record.override_pipeline_state != nullptr) {
        return record.override_pipeline_state.Get();
    }

    return pipeline_state;
}

bool ShaderOverrideRegistry::record_matches_bind_override(const D3D12GraphicsPsoRecord& record, const BindOverrideEntry& entry) const {
    auto crc_matches = [](uint32_t crc, const std::string& target) {
        if (crc == 0 || target.size() != 8) {
            return false;
        }
        char buf[16]{};
        std::snprintf(buf, sizeof(buf), "%08x", crc);
        return _stricmp(buf, target.c_str()) == 0;
    };

    auto stage_matches = [&](Stage stage) {
        switch (stage) {
        case Stage::Vertex:
            return record.vertex_hash == entry.target_hash || crc_matches(record.vertex_crc32, entry.target_hash);
        case Stage::Pixel:
            return record.pixel_hash == entry.target_hash || crc_matches(record.pixel_crc32, entry.target_hash);
        case Stage::Geometry:
            return record.geometry_hash == entry.target_hash || crc_matches(record.geometry_crc32, entry.target_hash);
        case Stage::Compute:
            return record.compute_hash == entry.target_hash || crc_matches(record.compute_crc32, entry.target_hash);
        case Stage::Amplification:
            return record.amplification_hash == entry.target_hash || crc_matches(record.amplification_crc32, entry.target_hash);
        case Stage::Mesh:
            return record.mesh_hash == entry.target_hash || crc_matches(record.mesh_crc32, entry.target_hash);
        default:
            return false;
        }
    };

    if (!entry.any_stage) {
        return stage_matches(entry.stage);
    }

    return stage_matches(Stage::Vertex) ||
        stage_matches(Stage::Pixel) ||
        stage_matches(Stage::Geometry) ||
        stage_matches(Stage::Compute) ||
        stage_matches(Stage::Amplification) ||
        stage_matches(Stage::Mesh);
}

std::optional<ShaderOverrideRegistry::D3D12CbvBindOverride> ShaderOverrideRegistry::resolve_d3d12_cbv_bind_override(
    bool graphics,
    uintptr_t pipeline_state,
    int eye_bucket,
    uint32_t root_parameter
) const {
    if (pipeline_state == 0 || !m_runtime_overrides_enabled.load(std::memory_order_relaxed)) {
        return std::nullopt;
    }

    std::scoped_lock _{m_mutex};
    const auto record_it = m_d3d12_graphics_pso_records.find(pipeline_state);
    if (record_it == m_d3d12_graphics_pso_records.end()) {
        return std::nullopt;
    }

    const auto current_eye = eye_bucket_to_target(eye_bucket);
    for (const auto& [_, entry] : m_bind_overrides) {
        if (!entry.enabled ||
            entry.kind != BindOverrideKind::Cbv ||
            entry.root_parameter != root_parameter ||
            (graphics && !entry.graphics) ||
            (!graphics && !entry.compute) ||
            (entry.eye != EyeTarget::Any && entry.eye != current_eye) ||
            !record_matches_bind_override(record_it->second, entry)) {
            continue;
        }

        return D3D12CbvBindOverride{entry.name, entry.cbv_data};
    }

    return std::nullopt;
}

std::optional<ShaderOverrideRegistry::D3D12RootConstantsBindOverride> ShaderOverrideRegistry::resolve_d3d12_root_constants_bind_override(
    bool graphics,
    uintptr_t pipeline_state,
    int eye_bucket,
    uint32_t root_parameter
) const {
    if (pipeline_state == 0 || !m_runtime_overrides_enabled.load(std::memory_order_relaxed)) {
        return std::nullopt;
    }

    std::scoped_lock _{m_mutex};
    const auto record_it = m_d3d12_graphics_pso_records.find(pipeline_state);
    if (record_it == m_d3d12_graphics_pso_records.end()) {
        return std::nullopt;
    }

    const auto current_eye = eye_bucket_to_target(eye_bucket);
    for (const auto& [_, entry] : m_bind_overrides) {
        if (!entry.enabled ||
            entry.kind != BindOverrideKind::RootConstants ||
            entry.root_parameter != root_parameter ||
            (graphics && !entry.graphics) ||
            (!graphics && !entry.compute) ||
            (entry.eye != EyeTarget::Any && entry.eye != current_eye) ||
            !record_matches_bind_override(record_it->second, entry)) {
            continue;
        }

        return D3D12RootConstantsBindOverride{entry.name, entry.constants, entry.dest_offset};
    }

    return std::nullopt;
}

void ShaderOverrideRegistry::note_d3d12_pipeline_state_bound(ID3D12PipelineState* original_pipeline_state, ID3D12PipelineState* bound_pipeline_state) {
    if (!should_track_d3d12_pipelines()) {
        return;
    }

    std::scoped_lock _{m_mutex};

    auto fill_info = [this, original_pipeline_state, bound_pipeline_state](Stage stage) {
        BoundShaderInfo info{};
        info.backend = Backend::D3D12;
        info.stage = stage;
        info.original_pointer = reinterpret_cast<uintptr_t>(original_pipeline_state);
        info.bound_pointer = reinterpret_cast<uintptr_t>(bound_pipeline_state);
        info.last_bound_frame = m_frame;

        if (original_pipeline_state == nullptr) {
            info.note = "null pso";
            return info;
        }

        const auto it = m_d3d12_graphics_pso_records.find(reinterpret_cast<uintptr_t>(original_pipeline_state));
        if (it == m_d3d12_graphics_pso_records.end()) {
            info.note = "untracked pso (created before injection)";
            return info;
        }

        const auto& record = it->second;
        const std::string* hash = nullptr;
        const std::string* override_name = nullptr;
        const char* missing_note = "no shader bytecode";
        switch (stage) {
        case Stage::Vertex:
            info.crc32 = record.vertex_crc32;
            hash = &record.vertex_hash;
            override_name = &record.vertex_override_name;
            missing_note = "no vertex shader bytecode";
            break;
        case Stage::Pixel:
            info.crc32 = record.pixel_crc32;
            hash = &record.pixel_hash;
            override_name = &record.pixel_override_name;
            missing_note = "no pixel shader bytecode";
            break;
        case Stage::Geometry:
            info.crc32 = record.geometry_crc32;
            hash = &record.geometry_hash;
            override_name = &record.geometry_override_name;
            missing_note = "no geometry shader bytecode";
            break;
        case Stage::Compute:
            info.crc32 = record.compute_crc32;
            hash = &record.compute_hash;
            override_name = &record.compute_override_name;
            missing_note = "no compute shader bytecode";
            break;
        case Stage::Amplification:
            info.crc32 = record.amplification_crc32;
            hash = &record.amplification_hash;
            override_name = &record.amplification_override_name;
            missing_note = "no amplification shader bytecode";
            break;
        case Stage::Mesh:
            info.crc32 = record.mesh_crc32;
            hash = &record.mesh_hash;
            override_name = &record.mesh_override_name;
            missing_note = "no mesh shader bytecode";
            break;
        }

        if (hash == nullptr || hash->empty()) {
            info.note = record.tracking_note.empty() ? missing_note : record.tracking_note;
            return info;
        }

        info.known = true;
        info.hash = *hash;
        info.override_active = override_name != nullptr && !override_name->empty();
        info.override_name = override_name != nullptr ? *override_name : std::string{};

        if (bound_pipeline_state != nullptr && bound_pipeline_state != original_pipeline_state) {
            info.note = "replacement pso";
        } else if (!record.tracking_note.empty()) {
            info.note = record.tracking_note;
        } else {
            info.note = "original pso";
        }

        return info;
    };

    m_bound_vertex_shader = fill_info(Stage::Vertex);
    m_bound_pixel_shader = fill_info(Stage::Pixel);
    auto bound_geometry_shader = fill_info(Stage::Geometry);

    D3D12PipelinePairInfo pair{};
    pair.frame = m_frame;
    pair.original_pipeline_state = reinterpret_cast<uintptr_t>(original_pipeline_state);
    pair.bound_pipeline_state = reinterpret_cast<uintptr_t>(bound_pipeline_state);
    pair.vertex_shader = m_bound_vertex_shader;
    pair.pixel_shader = m_bound_pixel_shader;
    pair.geometry_shader = std::move(bound_geometry_shader);

    if (original_pipeline_state != nullptr) {
        if (const auto it = m_d3d12_graphics_pso_records.find(reinterpret_cast<uintptr_t>(original_pipeline_state)); it != m_d3d12_graphics_pso_records.end()) {
            pair.pipeline_stream = it->second.is_pipeline_stream;
            pair.tracking_note = it->second.tracking_note;
        } else {
            pair.tracking_note = "created before injection";
        }
    } else {
        pair.tracking_note = "null pso";
    }

    record_d3d12_pso_sample(pair);
    record_d3d12_pipeline_pair(pair);
}

bool ShaderOverrideRegistry::is_d3d12_pipeline_state_tracked(uintptr_t pipeline_state) const {
    if (pipeline_state == 0) {
        return false;
    }

    std::scoped_lock _{m_mutex};
    return m_d3d12_graphics_pso_records.find(pipeline_state) != m_d3d12_graphics_pso_records.end();
}

void ShaderOverrideRegistry::scan_override_directories() {
    std::unordered_map<std::string, std::filesystem::path> discovered_entries{};
    std::unordered_map<std::string, std::filesystem::path> discovered_bind_overrides{};

    // Snapshot the override revision so we can detect whether this scan added /
    // updated / removed any override (each such change bumps m_override_revision
    // in scan_single_directory). If it did, we retroactively re-run the per-PSO
    // matcher below for already-tracked PSOs.
    const uint64_t revision_before_scan = m_override_revision;

    const auto global_dir = global_override_dir();
    const auto profile_dir = profile_override_dir();
    spdlog::info("[ShaderOverrideRegistry] scan tick: global={} profile={} overrides_before={}",
        global_dir.string(), profile_dir.string(), m_overrides.size());

    scan_single_directory(global_dir, false, discovered_bind_overrides);
    for (const auto& [key, entry] : m_overrides) {
        discovered_entries[key] = entry.manifest_path;
    }

    scan_single_directory(profile_dir, true, discovered_bind_overrides);
    for (const auto& [key, entry] : m_overrides) {
        discovered_entries[key] = entry.manifest_path;
    }

    remove_deleted_entries(discovered_entries);
    remove_deleted_bind_overrides(discovered_bind_overrides);
    refresh_active_override_flags_locked();

    spdlog::info("[ShaderOverrideRegistry] scan done: overrides_after={} d3d12_active={} d3d11_active={}",
        m_overrides.size(),
        m_has_active_d3d12_overrides.load(std::memory_order_relaxed),
        m_has_active_d3d11_overrides.load(std::memory_order_relaxed));

    // === Retroactive re-substitution ===
    // The per-PSO matcher (update_d3d12_override_pipeline_state) normally runs
    // only at PSO *creation*. But an override can become available AFTER the PSO
    // it targets was already created — e.g. a manifest dropped in while the game
    // is running, or (the SN2 case) a precached/early PSO whose creation lost the
    // race against the first 30s override scan. Those records are tracked (they
    // carry owned_stream/owned_desc + the per-stage CRCs) but were last evaluated
    // at an older revision, so they never picked up the new override. When this
    // scan changed the override set, re-run the matcher across every tracked
    // D3D12 PSO record; the applied_override_revision guard inside makes records
    // already at the current revision a cheap early-out, so only the stale ones
    // actually rebuild a replacement PSO.
    if (m_override_revision != revision_before_scan && m_runtime_overrides_enabled.load(std::memory_order_relaxed)) {
        size_t reevaluated = 0;
        size_t now_active = 0;
        for (auto& [_, record] : m_d3d12_graphics_pso_records) {
            if (record.applied_override_revision == m_override_revision) {
                continue;
            }
            const bool was_active = record.override_active;
            update_d3d12_override_pipeline_state(record);
            ++reevaluated;
            if (record.override_active && !was_active) {
                ++now_active;
            }
        }
        if (reevaluated > 0) {
            spdlog::info("[ShaderOverrideRegistry] retroactive re-substitution: re-evaluated {} stale PSO record(s), {} newly overridden (override set changed: rev {} -> {})",
                reevaluated, now_active, revision_before_scan, m_override_revision);
        }
    }

    if (verbose_override_scan_log_enabled()) {
        for (const auto& [k, e] : m_overrides) {
            spdlog::info("[ShaderOverrideRegistry]   override key={} hash={} compiled={} bytes={} status={} err={}",
                k, e.target_hash, e.compiled, e.compiled_bytecode.size(), e.status, e.last_error);
        }
    }
}

void ShaderOverrideRegistry::scan_single_directory(
    const std::filesystem::path& dir,
    bool from_profile_dir,
    std::unordered_map<std::string, std::filesystem::path>& discovered_bind_overrides
) {
    std::error_code ec{};
    std::filesystem::create_directories(dir, ec);

    if (ec || !std::filesystem::exists(dir)) {
        return;
    }

    for (const auto& file : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (ec) {
            break;
        }

        if (!file.is_regular_file()) {
            continue;
        }

        bool under_cache = false;
        for (const auto& part : file.path().lexically_relative(dir)) {
            if (part == "cache") {
                under_cache = true;
                break;
            }
        }
        if (under_cache) {
            continue;
        }

        if (file.path().extension() != ".json") {
            continue;
        }

        if (auto bind = parse_bind_override_manifest(file.path(), from_profile_dir); bind.has_value()) {
            auto& entry = bind.value();
            discovered_bind_overrides[entry.key] = entry.manifest_path;
            auto existing = m_bind_overrides.find(entry.key);
            if (existing == m_bind_overrides.end()) {
                push_event("Loaded bind override " + entry.key);
                m_bind_overrides[entry.key] = std::move(entry);
            } else {
                auto& current = existing->second;
                const bool profile_override_replaces_global = from_profile_dir && !current.from_profile_dir;
                const bool same_origin = current.manifest_path == entry.manifest_path;
                if (!profile_override_replaces_global && !same_origin && current.from_profile_dir && !from_profile_dir) {
                    continue;
                }

                if (current.manifest_write_time != entry.manifest_write_time ||
                    current.enabled != entry.enabled ||
                    current.from_profile_dir != entry.from_profile_dir) {
                    push_event("Reloaded bind override " + entry.key);
                }
                current = std::move(entry);
            }
            continue;
        }

        auto parsed = parse_manifest(file.path(), from_profile_dir);
        if (!parsed.has_value()) {
            continue;
        }

        auto& entry = parsed.value();
        auto existing = m_overrides.find(entry.key);

        if (existing == m_overrides.end()) {
            compile_or_refresh_entry(entry);
            m_overrides[entry.key] = std::move(entry);
            continue;
        }

        auto& current = existing->second;
        const bool profile_override_replaces_global = from_profile_dir && !current.from_profile_dir;
        const bool same_origin = current.manifest_path == entry.manifest_path;

        if (!profile_override_replaces_global && !same_origin && current.from_profile_dir && !from_profile_dir) {
            continue;
        }

        const bool changed =
            current.manifest_path != entry.manifest_path ||
            current.source_kind != entry.source_kind ||
            current.source_path != entry.source_path ||
            current.bytecode_path != entry.bytecode_path ||
            current.patch_path != entry.patch_path ||
            current.patch_tool_path != entry.patch_tool_path ||
            current.left_payload.present != entry.left_payload.present ||
            current.right_payload.present != entry.right_payload.present ||
            current.left_payload.source_kind != entry.left_payload.source_kind ||
            current.right_payload.source_kind != entry.right_payload.source_kind ||
            current.left_payload.bytecode_path != entry.left_payload.bytecode_path ||
            current.right_payload.bytecode_path != entry.right_payload.bytecode_path ||
            current.left_payload.patch_path != entry.left_payload.patch_path ||
            current.right_payload.patch_path != entry.right_payload.patch_path ||
            current.left_payload.bytecode_write_time != entry.left_payload.bytecode_write_time ||
            current.right_payload.bytecode_write_time != entry.right_payload.bytecode_write_time ||
            current.left_payload.patch_write_time != entry.left_payload.patch_write_time ||
            current.right_payload.patch_write_time != entry.right_payload.patch_write_time ||
            current.enabled != entry.enabled ||
            current.per_eye_variants != entry.per_eye_variants ||
            current.entry_point != entry.entry_point ||
            current.profile != entry.profile ||
            current.name != entry.name ||
            current.manifest_write_time != entry.manifest_write_time ||
            current.source_write_time != entry.source_write_time ||
            current.bytecode_write_time != entry.bytecode_write_time ||
            current.patch_write_time != entry.patch_write_time ||
            current.from_profile_dir != entry.from_profile_dir;

        entry.generation = current.generation;
        entry.compiled_bytecode = current.compiled_bytecode;
        entry.compiled = current.compiled;
        entry.status = current.status;
        entry.last_error = current.last_error;
        entry.cached_bytecode_path = current.cached_bytecode_path;
        entry.compiled_original_hash = current.compiled_original_hash;
        entry.left_payload.compiled_bytecode = current.left_payload.compiled_bytecode;
        entry.right_payload.compiled_bytecode = current.right_payload.compiled_bytecode;
        entry.left_payload.compiled_original_hash = current.left_payload.compiled_original_hash;
        entry.right_payload.compiled_original_hash = current.right_payload.compiled_original_hash;
        entry.left_payload.cached_bytecode_path = current.left_payload.cached_bytecode_path;
        entry.right_payload.cached_bytecode_path = current.right_payload.cached_bytecode_path;

        if (changed) {
            compile_or_refresh_entry(entry);
        }

        current = std::move(entry);
    }
}

void ShaderOverrideRegistry::remove_deleted_entries(const std::unordered_map<std::string, std::filesystem::path>& discovered_entries) {
    std::vector<std::string> dead_keys{};

    for (const auto& [key, entry] : m_overrides) {
        if (!discovered_entries.contains(key)) {
            dead_keys.emplace_back(key);
        }
    }

    for (const auto& key : dead_keys) {
        push_event("Removed shader override " + key);
        m_overrides.erase(key);
        ++m_override_revision;
    }
}

void ShaderOverrideRegistry::remove_deleted_bind_overrides(const std::unordered_map<std::string, std::filesystem::path>& discovered_entries) {
    std::vector<std::string> dead_keys{};

    for (const auto& [key, entry] : m_bind_overrides) {
        if (!discovered_entries.contains(key)) {
            dead_keys.emplace_back(key);
        }
    }

    for (const auto& key : dead_keys) {
        push_event("Removed bind override " + key);
        m_bind_overrides.erase(key);
    }
}

void ShaderOverrideRegistry::compile_or_refresh_entry(OverrideEntry& entry) {
    if (!entry.enabled) {
        entry.status = "Disabled";
        entry.last_error.clear();
        ++m_override_revision;
        return;
    }

    std::string error{};
    if (compile_entry(entry, error)) {
        ++entry.generation;
        const bool deferred_patch =
            entry.source_kind == OverrideSourceKind::DxilPatch ||
            entry.source_kind == OverrideSourceKind::DxilTextPatch ||
            entry.source_kind == OverrideSourceKind::ContainerPatch ||
            entry.source_kind == OverrideSourceKind::DxilTransform ||
            entry.source_kind == OverrideSourceKind::DxilSemanticTransform ||
            (entry.left_payload.present && entry.left_payload.source_kind != OverrideSourceKind::Bytecode) ||
            (entry.right_payload.present && entry.right_payload.source_kind != OverrideSourceKind::Bytecode);
        entry.compiled = !deferred_patch ||
            !entry.compiled_bytecode.empty() ||
            !entry.left_payload.compiled_bytecode.empty() ||
            !entry.right_payload.compiled_bytecode.empty();
        entry.last_error.clear();
        ++m_override_revision;
        push_event((deferred_patch ? "Loaded shader override " : "Compiled shader override ") + entry.key + " with " + entry.compiler);
    } else {
        entry.compiled = !entry.compiled_bytecode.empty();
        entry.status = "Compile failed";
        entry.last_error = error;
        const auto compiler_name = entry.compiler.empty() ? compiler_to_string(entry.preferred_compiler) : entry.compiler;
        push_event("Failed to compile shader override " + entry.key + " with " + compiler_name);
        spdlog::error("[ShaderOverrideRegistry] Failed to compile {}: {}", entry.key, error);
    }
}

std::optional<ShaderOverrideRegistry::BindOverrideEntry> ShaderOverrideRegistry::parse_bind_override_manifest(const std::filesystem::path& manifest_path, bool from_profile_dir) {
    try {
        std::ifstream file{manifest_path};
        if (!file) {
            return std::nullopt;
        }

        const auto manifest = json::parse(file);
        if (!manifest.is_object()) {
            return std::nullopt;
        }

        const auto kind_value = manifest.value("kind", std::string{});
        const bool explicit_bind_manifest =
            _stricmp(kind_value.c_str(), "bind_override") == 0 ||
            _stricmp(kind_value.c_str(), "root_bind_override") == 0;
        const bool shape_matches =
            manifest.contains("target_hash") &&
            manifest.contains("root_parameter") &&
            (manifest.contains("override") || manifest.contains("bind_kind") || manifest.contains("type")) &&
            (manifest.contains("values_u32") || manifest.contains("data_u32") || manifest.contains("data_hex") || manifest.contains("data_bytes"));

        if (!explicit_bind_manifest && !shape_matches) {
            return std::nullopt;
        }

        BindOverrideEntry entry{};
        entry.manifest_path = manifest_path;
        entry.from_profile_dir = from_profile_dir;
        entry.enabled = manifest.value("enabled", true);
        entry.name = manifest.value("name", manifest_path.stem().string());
        entry.target_hash = normalize_hash(manifest.at("target_hash").get<std::string>());
        entry.root_parameter = manifest.value("root_parameter", 0u);
        entry.dest_offset = manifest.value("dest_offset", 0u);

        const auto stage_value = manifest.value("stage", std::string{"any"});
        if (_stricmp(stage_value.c_str(), "any") == 0 || stage_value.empty()) {
            entry.any_stage = true;
        } else if (auto stage = parse_stage(stage_value); stage.has_value()) {
            entry.any_stage = false;
            entry.stage = *stage;
        } else {
            entry.status = "Invalid";
            entry.last_error = "invalid stage: " + stage_value;
            return entry;
        }

        const auto pipeline_value = manifest.value("pipeline", std::string{"graphics"});
        if (_stricmp(pipeline_value.c_str(), "any") == 0) {
            entry.graphics = true;
            entry.compute = true;
        } else if (_stricmp(pipeline_value.c_str(), "graphics") == 0) {
            entry.graphics = true;
            entry.compute = false;
        } else if (_stricmp(pipeline_value.c_str(), "compute") == 0) {
            entry.graphics = false;
            entry.compute = true;
        } else {
            entry.status = "Invalid";
            entry.last_error = "invalid pipeline: " + pipeline_value;
            return entry;
        }

        const auto eye_value = manifest.value("eye", std::string{"any"});
        if (auto eye = parse_eye_target(eye_value); eye.has_value()) {
            entry.eye = *eye;
        } else {
            entry.status = "Invalid";
            entry.last_error = "invalid eye: " + eye_value;
            return entry;
        }

        const auto bind_kind_value =
            manifest.contains("override") ? manifest.at("override").get<std::string>() :
            manifest.contains("bind_kind") ? manifest.at("bind_kind").get<std::string>() :
            manifest.value("type", std::string{"cbv"});
        if (auto bind_kind = parse_bind_override_kind(bind_kind_value); bind_kind.has_value()) {
            entry.kind = *bind_kind;
        } else {
            entry.status = "Invalid";
            entry.last_error = "invalid bind override kind: " + bind_kind_value;
            return entry;
        }

        std::vector<uint32_t> values{};
        if (manifest.contains("values_u32")) {
            values = json_u32_array(manifest.at("values_u32"));
        } else if (manifest.contains("data_u32")) {
            values = json_u32_array(manifest.at("data_u32"));
        }

        if (entry.kind == BindOverrideKind::RootConstants) {
            entry.constants = std::move(values);
            if (entry.constants.empty()) {
                entry.status = "Invalid";
                entry.last_error = "root constant override has no values_u32/data_u32";
                return entry;
            }
        } else {
            if (!values.empty()) {
                entry.cbv_data = u32_vector_to_bytes(values);
            } else if (manifest.contains("data_hex")) {
                entry.cbv_data = hex_string_to_bytes(manifest.at("data_hex").get<std::string>());
            } else if (manifest.contains("data_bytes")) {
                auto raw = json_u32_array(manifest.at("data_bytes"));
                entry.cbv_data.reserve(raw.size());
                for (const auto v : raw) {
                    entry.cbv_data.emplace_back(static_cast<uint8_t>(v & 0xffu));
                }
            }

            if (entry.cbv_data.empty()) {
                entry.status = "Invalid";
                entry.last_error = "CBV override has no data_u32/data_hex/data_bytes";
                return entry;
            }

            const auto aligned = (entry.cbv_data.size() + 255u) & ~size_t{255u};
            entry.cbv_data.resize(aligned, 0);
        }

        std::ostringstream key{};
        key << entry.target_hash << ':' << (entry.any_stage ? "any" : stage_to_string(entry.stage))
            << ':' << (entry.graphics && entry.compute ? "any" : (entry.graphics ? "graphics" : "compute"))
            << ':' << entry.root_parameter << ':' << bind_override_kind_to_string(entry.kind)
            << ':' << eye_target_to_string(entry.eye) << ':' << entry.name;
        entry.key = key.str();

        std::error_code ec{};
        entry.manifest_write_time = std::filesystem::last_write_time(entry.manifest_path, ec);
        entry.status = entry.enabled ? "Ready" : "Disabled";
        return entry;
    } catch (const std::exception& e) {
        spdlog::error("[ShaderOverrideRegistry] Failed to parse bind override {}: {}", manifest_path.string(), e.what());
        return std::nullopt;
    }
}

std::optional<ShaderOverrideRegistry::OverrideEntry> ShaderOverrideRegistry::parse_manifest(const std::filesystem::path& manifest_path, bool from_profile_dir) {
    try {
        std::ifstream file{manifest_path};
        if (!file) {
            return std::nullopt;
        }

        const auto manifest = json::parse(file);
        if (!manifest.is_object()) {
            return std::nullopt;
        }

        const bool has_manifest_keys =
            manifest.contains("backend") &&
            manifest.contains("stage") &&
            manifest.contains("target_hash");
        if (!has_manifest_keys) {
            if (manifest.contains("patches") ||
                manifest.contains("replacements") ||
                manifest.contains("transforms") ||
                manifest.contains("container_edits")) {
                return std::nullopt;
            }

            push_event("Skipped invalid shader override manifest " + manifest_path.string());
            spdlog::error("[ShaderOverrideRegistry] {} is missing backend, stage, or target_hash", manifest_path.string());
            return std::nullopt;
        }

        const auto backend_value = manifest.at("backend").get<std::string>();
        const auto stage_value = manifest.at("stage").get<std::string>();
        const auto hash_value = normalize_hash(manifest.at("target_hash").get<std::string>());

        const auto backend = parse_backend(backend_value);
        const auto stage = parse_stage(stage_value);

        if (!backend.has_value() || !stage.has_value() || hash_value.empty()) {
            push_event("Skipped invalid shader override manifest " + manifest_path.string());
            return std::nullopt;
        }

        OverrideEntry entry{};
        entry.backend = *backend;
        entry.stage = *stage;
        entry.target_hash = hash_value;
        entry.key = make_override_key(*backend, *stage, hash_value);
        entry.name = manifest.value("name", manifest_path.stem().string());
        entry.manifest_path = manifest_path;
        entry.enabled = manifest.value("enabled", true);
        entry.entry_point = manifest.value("entry_point", "main");
        entry.profile = manifest.value("profile", default_profile(*backend, *stage));
        entry.preferred_compiler = ShaderCompilerBackend::Auto;
        entry.from_profile_dir = from_profile_dir;
        entry.apply_supported = true;
        entry.per_eye_variants = manifest.value("per_eye_variants", false);

        if (manifest.contains("compiler")) {
            const auto compiler_value = manifest.at("compiler").get<std::string>();
            if (const auto compiler = parse_compiler(compiler_value); compiler.has_value()) {
                entry.preferred_compiler = *compiler;
            }
        }

        const bool has_source = manifest.contains("source");
        const bool has_bytecode = manifest.contains("bytecode");
        const bool has_patch = manifest.contains("patch") || manifest.contains("dxil_patch");
        const bool has_text_patch = manifest.contains("dxil_text_patch") || manifest.contains("dxil_ir_patch");
        const bool has_container_patch = manifest.contains("container_patch") || manifest.contains("container_edits");
        const bool has_transform = manifest.contains("dxil_transform") || manifest.contains("dxil_stereo_transform");
        const bool has_semantic_transform =
            manifest.contains("dxil_semantic_transform") ||
            manifest.contains("dxil_module_transform") ||
            manifest.contains("semantic_transform");
        const bool has_left_payload =
            manifest.contains("left_bytecode") ||
            manifest.contains("left_dxil_transform") ||
            manifest.contains("left_transform") ||
            manifest.contains("left_dxil_semantic_transform") ||
            manifest.contains("left_semantic_transform") ||
            manifest.contains("left_dxil_text_patch") ||
            manifest.contains("left_container_patch");
        const bool has_right_payload =
            manifest.contains("right_bytecode") ||
            manifest.contains("right_dxil_transform") ||
            manifest.contains("right_transform") ||
            manifest.contains("right_dxil_semantic_transform") ||
            manifest.contains("right_semantic_transform") ||
            manifest.contains("right_dxil_text_patch") ||
            manifest.contains("right_container_patch");
        const int source_count =
            static_cast<int>(has_source) +
            static_cast<int>(has_bytecode) +
            static_cast<int>(has_patch) +
            static_cast<int>(has_text_patch) +
            static_cast<int>(has_container_patch) +
            static_cast<int>(has_transform) +
            static_cast<int>(has_semantic_transform);
        if (source_count != 1 && !(source_count == 0 && (has_left_payload || has_right_payload))) {
            push_event("Skipped invalid shader override manifest " + manifest_path.string());
            spdlog::error("[ShaderOverrideRegistry] {} must specify exactly one of source, bytecode, patch/dxil_patch, dxil_text_patch, container_patch, dxil_transform, dxil_semantic_transform, or at least one left_/right_ payload", manifest_path.string());
            return std::nullopt;
        }

        if (has_source) {
            entry.source_kind = OverrideSourceKind::Hlsl;
            entry.source_path = resolve_manifest_relative_path(manifest_path, manifest.at("source").get<std::string>());
        } else if (has_bytecode) {
            entry.source_kind = OverrideSourceKind::Bytecode;
            entry.bytecode_path = resolve_manifest_relative_path(manifest_path, manifest.at("bytecode").get<std::string>());
            entry.source_path = entry.bytecode_path;
            entry.compiler = "bytecode";
        } else if (has_patch) {
            entry.source_kind = OverrideSourceKind::DxilPatch;
            const auto patch_key = manifest.contains("patch") ? "patch" : "dxil_patch";
            entry.patch_path = resolve_manifest_relative_path(manifest_path, manifest.at(patch_key).get<std::string>());
            entry.source_path = entry.patch_path;
            entry.compiler = "dxil-patch";
            if (manifest.contains("patch_tool")) {
                entry.patch_tool_path = resolve_manifest_relative_path(manifest_path, manifest.at("patch_tool").get<std::string>());
            }
        } else if (has_transform) {
            entry.source_kind = OverrideSourceKind::DxilTransform;
            const auto patch_key = manifest.contains("dxil_transform") ? "dxil_transform" : "dxil_stereo_transform";
            entry.patch_path = resolve_manifest_relative_path(manifest_path, manifest.at(patch_key).get<std::string>());
            entry.source_path = entry.patch_path;
            entry.compiler = "dxil-transform";
            if (manifest.contains("patch_tool")) {
                entry.patch_tool_path = resolve_manifest_relative_path(manifest_path, manifest.at("patch_tool").get<std::string>());
            }
        } else if (has_semantic_transform) {
            entry.source_kind = OverrideSourceKind::DxilSemanticTransform;
            const auto patch_key = manifest.contains("dxil_semantic_transform")
                ? "dxil_semantic_transform"
                : (manifest.contains("dxil_module_transform") ? "dxil_module_transform" : "semantic_transform");
            entry.patch_path = resolve_manifest_relative_path(manifest_path, manifest.at(patch_key).get<std::string>());
            entry.source_path = entry.patch_path;
            entry.compiler = "dxil-semantic-transform";
            if (manifest.contains("semantic_tool")) {
                entry.patch_tool_path = resolve_manifest_relative_path(manifest_path, manifest.at("semantic_tool").get<std::string>());
            } else if (manifest.contains("dxil_semantic_tool")) {
                entry.patch_tool_path = resolve_manifest_relative_path(manifest_path, manifest.at("dxil_semantic_tool").get<std::string>());
            } else if (manifest.contains("patch_tool")) {
                entry.patch_tool_path = resolve_manifest_relative_path(manifest_path, manifest.at("patch_tool").get<std::string>());
            }
        } else if (has_text_patch) {
            entry.source_kind = OverrideSourceKind::DxilTextPatch;
            const auto patch_key = manifest.contains("dxil_text_patch") ? "dxil_text_patch" : "dxil_ir_patch";
            const auto& patch_value = manifest.at(patch_key);
            entry.compiler = "dxc-assembler";
            if (patch_value.is_string()) {
                entry.patch_path = resolve_manifest_relative_path(manifest_path, patch_value.get<std::string>());
                entry.source_path = entry.patch_path;

                std::ifstream patch_file{entry.patch_path};
                if (!patch_file) {
                    push_event("Skipped invalid DXIL text patch manifest " + manifest_path.string());
                    spdlog::error("[ShaderOverrideRegistry] failed to open DXIL text patch file {}", entry.patch_path.string());
                    return std::nullopt;
                }

                const auto patch_json = json::parse(patch_file);
                const auto& replacements = patch_json.contains("replacements") ? patch_json.at("replacements") : patch_json;
                if (!replacements.is_array()) {
                    spdlog::error("[ShaderOverrideRegistry] DXIL text patch file {} must contain replacements[]", entry.patch_path.string());
                    return std::nullopt;
                }

                for (const auto& replacement : replacements) {
                    entry.dxil_text_patches.push_back({
                        replacement.value("find", std::string{}),
                        replacement.value("replace", std::string{})
                    });
                }
            } else if (patch_value.is_array()) {
                entry.source_path = manifest_path;
                for (const auto& replacement : patch_value) {
                    entry.dxil_text_patches.push_back({
                        replacement.value("find", std::string{}),
                        replacement.value("replace", std::string{})
                    });
                }
            } else if (patch_value.is_object()) {
                entry.source_path = manifest_path;
                const json replacements = patch_value.contains("replacements") ? patch_value.at("replacements") : json::array({patch_value});
                if (!replacements.is_array()) {
                    return std::nullopt;
                }
                for (const auto& replacement : replacements) {
                    entry.dxil_text_patches.push_back({
                        replacement.value("find", std::string{}),
                        replacement.value("replace", std::string{})
                    });
                }
            }
        } else if (has_container_patch) {
            entry.source_kind = OverrideSourceKind::ContainerPatch;
            entry.compiler = "dxc-container-builder";
            entry.source_path = manifest_path;

            const auto patch_key = manifest.contains("container_patch") ? "container_patch" : "container_edits";
            const auto& patch_value = manifest.at(patch_key);
            json edits_json{};
            if (patch_value.is_string()) {
                entry.patch_path = resolve_manifest_relative_path(manifest_path, patch_value.get<std::string>());
                entry.source_path = entry.patch_path;
                std::ifstream patch_file{entry.patch_path};
                if (!patch_file) {
                    spdlog::error("[ShaderOverrideRegistry] failed to open container patch file {}", entry.patch_path.string());
                    return std::nullopt;
                }
                const auto patch_doc = json::parse(patch_file);
                edits_json = patch_doc.contains("edits") ? patch_doc.at("edits") : patch_doc;
            } else if (patch_value.is_object()) {
                edits_json = patch_value.contains("edits") ? patch_value.at("edits") : json::array({patch_value});
            } else {
                edits_json = patch_value;
            }

            if (!edits_json.is_array()) {
                spdlog::error("[ShaderOverrideRegistry] container patch {} must be an edit array", manifest_path.string());
                return std::nullopt;
            }

            for (const auto& edit_json : edits_json) {
                ShaderContainerEdit edit{};
                edit.fourcc = edit_json.value("fourcc", std::string{});
                edit.remove = edit_json.value("remove", false);
                if (!edit.remove) {
                    if (edit_json.contains("path")) {
                        std::vector<uint8_t> data{};
                        std::string read_error{};
                        if (!read_binary_file(resolve_manifest_relative_path(manifest_path, edit_json.at("path").get<std::string>()), data, read_error)) {
                            spdlog::error("[ShaderOverrideRegistry] failed to read container patch part: {}", read_error);
                            return std::nullopt;
                        }
                        edit.data = std::move(data);
                    } else if (edit_json.contains("data_hex")) {
                        edit.data = hex_string_to_bytes(edit_json.at("data_hex").get<std::string>());
                    } else if (edit_json.contains("data_u32")) {
                        edit.data = u32_vector_to_bytes(json_u32_array(edit_json.at("data_u32")));
                    }
                }
                entry.container_edits.emplace_back(std::move(edit));
            }
        }

        if (entry.patch_tool_path.empty()) {
            if (manifest.contains("semantic_tool")) {
                entry.patch_tool_path = resolve_manifest_relative_path(manifest_path, manifest.at("semantic_tool").get<std::string>());
            } else if (manifest.contains("dxil_semantic_tool")) {
                entry.patch_tool_path = resolve_manifest_relative_path(manifest_path, manifest.at("dxil_semantic_tool").get<std::string>());
            } else if (manifest.contains("patch_tool")) {
                entry.patch_tool_path = resolve_manifest_relative_path(manifest_path, manifest.at("patch_tool").get<std::string>());
            }
        }

        auto parse_eye_payload = [&](const char* label, OverrideEntry::EyePayload& payload) -> bool {
            const std::string prefix = std::string{label} + "_";
            const auto bytecode_key = prefix + "bytecode";
            const auto transform_key = prefix + "dxil_transform";
            const auto transform_alias_key = prefix + "transform";
            const auto semantic_transform_key = prefix + "dxil_semantic_transform";
            const auto semantic_transform_alias_key = prefix + "semantic_transform";
            const auto text_patch_key = prefix + "dxil_text_patch";
            const auto container_patch_key = prefix + "container_patch";

            const bool eye_has_bytecode = manifest.contains(bytecode_key);
            const bool eye_has_transform = manifest.contains(transform_key) || manifest.contains(transform_alias_key);
            const bool eye_has_semantic_transform = manifest.contains(semantic_transform_key) || manifest.contains(semantic_transform_alias_key);
            const bool eye_has_text_patch = manifest.contains(text_patch_key);
            const bool eye_has_container_patch = manifest.contains(container_patch_key);
            const int eye_source_count =
                static_cast<int>(eye_has_bytecode) +
                static_cast<int>(eye_has_transform) +
                static_cast<int>(eye_has_semantic_transform) +
                static_cast<int>(eye_has_text_patch) +
                static_cast<int>(eye_has_container_patch);
            if (eye_source_count == 0) {
                return true;
            }
            if (eye_source_count != 1) {
                spdlog::error("[ShaderOverrideRegistry] {} has conflicting {} eye payload fields", manifest_path.string(), label);
                return false;
            }

            payload.present = true;
            entry.per_eye_variants = true;
            if (eye_has_bytecode) {
                payload.source_kind = OverrideSourceKind::Bytecode;
                payload.bytecode_path = resolve_manifest_relative_path(manifest_path, manifest.at(bytecode_key).get<std::string>());
                payload.bytecode_write_time = file_write_time_or_empty(payload.bytecode_path);
                return true;
            }

            if (eye_has_transform) {
                payload.source_kind = OverrideSourceKind::DxilTransform;
                const auto& key = manifest.contains(transform_key) ? transform_key : transform_alias_key;
                payload.patch_path = resolve_manifest_relative_path(manifest_path, manifest.at(key).get<std::string>());
                payload.patch_write_time = file_write_time_or_empty(payload.patch_path);
                return true;
            }

            if (eye_has_semantic_transform) {
                payload.source_kind = OverrideSourceKind::DxilSemanticTransform;
                const auto& key = manifest.contains(semantic_transform_key) ? semantic_transform_key : semantic_transform_alias_key;
                payload.patch_path = resolve_manifest_relative_path(manifest_path, manifest.at(key).get<std::string>());
                payload.patch_write_time = file_write_time_or_empty(payload.patch_path);
                return true;
            }

            if (eye_has_text_patch) {
                payload.source_kind = OverrideSourceKind::DxilTextPatch;
                payload.patch_path = resolve_manifest_relative_path(manifest_path, manifest.at(text_patch_key).get<std::string>());
                payload.patch_write_time = file_write_time_or_empty(payload.patch_path);
                std::ifstream patch_file{payload.patch_path};
                if (!patch_file) {
                    spdlog::error("[ShaderOverrideRegistry] failed to open {} DXIL text patch file {}", label, payload.patch_path.string());
                    return false;
                }
                const auto patch_json = json::parse(patch_file);
                const auto& replacements = patch_json.contains("replacements") ? patch_json.at("replacements") : patch_json;
                if (!replacements.is_array()) {
                    spdlog::error("[ShaderOverrideRegistry] {} DXIL text patch file {} must contain replacements[]", label, payload.patch_path.string());
                    return false;
                }
                for (const auto& replacement : replacements) {
                    payload.dxil_text_patches.push_back({
                        replacement.value("find", std::string{}),
                        replacement.value("replace", std::string{})
                    });
                }
                return true;
            }

            payload.source_kind = OverrideSourceKind::ContainerPatch;
            payload.patch_path = resolve_manifest_relative_path(manifest_path, manifest.at(container_patch_key).get<std::string>());
            payload.patch_write_time = file_write_time_or_empty(payload.patch_path);
            std::ifstream patch_file{payload.patch_path};
            if (!patch_file) {
                spdlog::error("[ShaderOverrideRegistry] failed to open {} container patch file {}", label, payload.patch_path.string());
                return false;
            }
            const auto patch_doc = json::parse(patch_file);
            const auto& edits_json = patch_doc.contains("edits") ? patch_doc.at("edits") : patch_doc;
            if (!edits_json.is_array()) {
                spdlog::error("[ShaderOverrideRegistry] {} container patch file {} must contain edits[]", label, payload.patch_path.string());
                return false;
            }
            for (const auto& edit_json : edits_json) {
                ShaderContainerEdit edit{};
                edit.fourcc = edit_json.value("fourcc", std::string{});
                edit.remove = edit_json.value("remove", false);
                if (!edit.remove) {
                    if (edit_json.contains("path")) {
                        std::vector<uint8_t> data{};
                        std::string read_error{};
                        if (!read_binary_file(resolve_manifest_relative_path(manifest_path, edit_json.at("path").get<std::string>()), data, read_error)) {
                            spdlog::error("[ShaderOverrideRegistry] failed to read {} container patch part: {}", label, read_error);
                            return false;
                        }
                        edit.data = std::move(data);
                    } else if (edit_json.contains("data_hex")) {
                        edit.data = hex_string_to_bytes(edit_json.at("data_hex").get<std::string>());
                    } else if (edit_json.contains("data_u32")) {
                        edit.data = u32_vector_to_bytes(json_u32_array(edit_json.at("data_u32")));
                    }
                }
                payload.container_edits.emplace_back(std::move(edit));
            }
            return true;
        };

        if (!parse_eye_payload("left", entry.left_payload) ||
            !parse_eye_payload("right", entry.right_payload)) {
            return std::nullopt;
        }

        std::error_code ec{};
        entry.manifest_write_time = std::filesystem::last_write_time(entry.manifest_path, ec);
        entry.source_write_time = file_write_time_or_empty(entry.source_path);
        entry.bytecode_write_time = file_write_time_or_empty(entry.bytecode_path);
        entry.patch_write_time = file_write_time_or_empty(entry.patch_path);

        return entry;
    } catch (const std::exception& e) {
        push_event("Failed to parse shader override manifest " + manifest_path.string());
        spdlog::error("[ShaderOverrideRegistry] Failed to parse {}: {}", manifest_path.string(), e.what());
        return std::nullopt;
    }
}

bool ShaderOverrideRegistry::compile_entry(OverrideEntry& entry, std::string& error_out) {
    const bool has_main_source = !entry.source_path.empty();
    if (!has_main_source && !entry.left_payload.present && !entry.right_payload.present) {
        error_out = "Shader override has no source, bytecode, patch, transform, or per-eye payload";
        return false;
    }

    if (has_main_source && !std::filesystem::exists(entry.source_path)) {
        error_out = "Source file does not exist: " + entry.source_path.string();
        return false;
    }

    if (entry.backend == Backend::D3D11 &&
        entry.stage != Stage::Vertex &&
        entry.stage != Stage::Pixel) {
        error_out = "DX11 live shader overrides only support VS and PS stages";
        return false;
    }

    entry.compiled_bytecode.clear();
    entry.compiled_original_hash.clear();
    entry.cached_bytecode_path.clear();
    entry.left_payload.compiled_bytecode.clear();
    entry.right_payload.compiled_bytecode.clear();
    entry.left_payload.compiled_original_hash.clear();
    entry.right_payload.compiled_original_hash.clear();
    entry.left_payload.cached_bytecode_path.clear();
    entry.right_payload.cached_bytecode_path.clear();

    auto load_eye_bytecode_payload = [&](OverrideEntry::EyePayload& payload, const char* label) -> bool {
        if (!payload.present || payload.source_kind != OverrideSourceKind::Bytecode) {
            return true;
        }

        if (!read_binary_file(payload.bytecode_path, payload.compiled_bytecode, error_out)) {
            return false;
        }

        if (payload.compiled_bytecode.empty()) {
            error_out = std::string{label} + " bytecode file is empty: " + payload.bytecode_path.string();
            return false;
        }

        payload.status = "Loaded bytecode";
        return true;
    };

    if (!load_eye_bytecode_payload(entry.left_payload, "left") ||
        !load_eye_bytecode_payload(entry.right_payload, "right")) {
        return false;
    }

    if (!has_main_source) {
        entry.compiler = "per-eye-payload";
        entry.status = "Deferred per-eye payload";
        entry.compiled = !entry.left_payload.compiled_bytecode.empty() || !entry.right_payload.compiled_bytecode.empty();
        return true;
    }

    if (entry.source_kind == OverrideSourceKind::Bytecode) {
        if (entry.backend == Backend::D3D11 && entry.profile.find("_6_") != std::string::npos) {
            error_out = "DX11 live bytecode overrides require DXBC-compatible shader models";
            return false;
        }

        if (!read_binary_file(entry.bytecode_path, entry.compiled_bytecode, error_out)) {
            return false;
        }

        if (entry.compiled_bytecode.empty()) {
            error_out = "Bytecode file is empty: " + entry.bytecode_path.string();
            return false;
        }

        entry.compiler = "bytecode";
        entry.status = "Loaded bytecode";
        return true;
    }

    if (entry.source_kind == OverrideSourceKind::DxilPatch) {
        if (entry.backend != Backend::D3D12) {
            error_out = "DXIL patch overrides are only supported for DX12";
            return false;
        }

        if (!std::filesystem::exists(entry.patch_path)) {
            error_out = "Patch file does not exist: " + entry.patch_path.string();
            return false;
        }

        entry.compiler = "dxil-patch";
        entry.status = "Deferred DXIL patch";
        return true;
    }

    if (entry.source_kind == OverrideSourceKind::DxilTransform) {
        if (entry.backend != Backend::D3D12) {
            error_out = "DXIL transform overrides are only supported for DX12";
            return false;
        }

        if (!std::filesystem::exists(entry.patch_path)) {
            error_out = "DXIL transform file does not exist: " + entry.patch_path.string();
            return false;
        }

        entry.compiler = "dxil-transform";
        entry.status = "Deferred DXIL transform";
        return true;
    }

    if (entry.source_kind == OverrideSourceKind::DxilSemanticTransform) {
        if (entry.backend != Backend::D3D12) {
            error_out = "DXIL semantic transform overrides are only supported for DX12";
            return false;
        }

        if (!std::filesystem::exists(entry.patch_path)) {
            error_out = "DXIL semantic transform file does not exist: " + entry.patch_path.string();
            return false;
        }

        entry.compiler = "dxil-semantic-transform";
        entry.status = "Deferred DXIL semantic transform";
        return true;
    }

    if (entry.source_kind == OverrideSourceKind::DxilTextPatch) {
        if (entry.backend != Backend::D3D12) {
            error_out = "DXIL text patch overrides are only supported for DX12";
            return false;
        }

        if (entry.dxil_text_patches.empty()) {
            error_out = "DXIL text patch override has no replacements";
            return false;
        }

        entry.compiler = "dxc-assembler";
        entry.status = "Deferred DXIL text patch";
        return true;
    }

    if (entry.source_kind == OverrideSourceKind::ContainerPatch) {
        if (entry.backend != Backend::D3D12) {
            error_out = "Container patch overrides are only supported for DX12";
            return false;
        }

        if (entry.container_edits.empty()) {
            error_out = "Container patch override has no edits";
            return false;
        }

        for (const auto& edit : entry.container_edits) {
            if (edit.fourcc.empty()) {
                error_out = "Container patch override has an edit with no fourcc";
                return false;
            }

            if (!edit.remove && edit.data.empty()) {
                error_out = "Container patch replacement for " + edit.fourcc + " has no data";
                return false;
            }
        }

        entry.compiler = "dxc-container-builder";
        entry.status = "Deferred container patch";
        return true;
    }

    if (entry.backend == Backend::D3D11 && entry.profile.find("_6_") != std::string::npos) {
        error_out = "DX11 live overrides require DXBC-compatible shader models (use vs_5_0/ps_5_0 or compiler=fxc)";
        return false;
    }

    ShaderCompileRequest request{};
    request.source_path = entry.source_path;
    request.entry_point = entry.entry_point;
    request.profile = entry.profile;
    request.preferred_backend = entry.preferred_compiler;

    if (entry.backend == Backend::D3D12 && request.preferred_backend == ShaderCompilerBackend::Auto) {
        request.preferred_backend = ShaderCompilerBackend::Dxc;
    }

    // #13: Embed RenderDoc-friendly debug info on shaders WE author (HLSL-source
    // overrides only). This path is unreachable for dxil_text_patch / container /
    // transform / bytecode overrides — each of those returns earlier above — so
    // patched copies of shipped DXIL never receive these flags and their bytecode
    // is unaffected. Gate behind manifest "debug_info": true OR UEVR_SN2_SHADER_DEBUG=1.
    bool embed_shader_debug = env_truthy("UEVR_SN2_SHADER_DEBUG");
    if (!embed_shader_debug && !entry.manifest_path.empty()) {
        std::error_code manifest_ec{};
        if (std::filesystem::exists(entry.manifest_path, manifest_ec)) {
            try {
                std::ifstream manifest_stream{entry.manifest_path};
                if (manifest_stream.good()) {
                    const auto manifest = json::parse(manifest_stream, nullptr, false);
                    if (!manifest.is_discarded()) {
                        embed_shader_debug = manifest.value("debug_info", false);
                    }
                }
            } catch (...) {
                // Manifest already parsed successfully during scan; a read failure
                // here just leaves debug info off.
            }
        }
    }

    if (embed_shader_debug) {
        request.debug_info = true;
        request.strip_debug = false;
        request.strip_reflection = false;
    }

    entry.compiler = compiler_to_string(request.preferred_backend);
    const auto result = compile_shader_file(request);
    if (!result.compiler.empty()) {
        entry.compiler = result.compiler;
    }

    if (!result.succeeded) {
        error_out = result.error;
        if (!result.notes.empty()) {
            error_out += "\n";
            error_out += result.notes;
        }
        return false;
    }

    entry.compiled_bytecode = result.bytecode;
    entry.status = "Compiled (" + entry.compiler + ")";
    if (!result.notes.empty()) {
        entry.last_error = result.notes;
    }

    return true;
}

bool ShaderOverrideRegistry::ensure_d3d12_patch_entry_compiled(
    OverrideEntry& entry,
    const void* original_bytecode,
    size_t original_bytecode_size,
    std::string_view original_hash,
    std::string& error_out
) {
    if (entry.source_kind != OverrideSourceKind::DxilPatch &&
        entry.source_kind != OverrideSourceKind::DxilTextPatch &&
        entry.source_kind != OverrideSourceKind::ContainerPatch &&
        entry.source_kind != OverrideSourceKind::DxilTransform &&
        entry.source_kind != OverrideSourceKind::DxilSemanticTransform) {
        return true;
    }

    if (original_bytecode == nullptr || original_bytecode_size == 0) {
        error_out = "Original DXIL bytecode is empty";
        return false;
    }

    if (!entry.compiled_bytecode.empty() && entry.compiled_original_hash == original_hash) {
        return true;
    }

    if (entry.source_kind == OverrideSourceKind::DxilTextPatch) {
        const auto cache_root = profile_override_dir() / "cache" / "dxil_text_patch";
        std::error_code ec{};
        std::filesystem::create_directories(cache_root, ec);
        if (ec) {
            error_out = "Failed to create DXIL text patch cache: " + ec.message();
            return false;
        }

        const auto manifest_stamp = std::filesystem::exists(entry.manifest_path)
            ? std::filesystem::last_write_time(entry.manifest_path, ec).time_since_epoch().count()
            : 0;
        ec.clear();
        const auto patch_stamp = std::filesystem::exists(entry.patch_path)
            ? std::filesystem::last_write_time(entry.patch_path, ec).time_since_epoch().count()
            : 0;

        std::ostringstream key{};
        key << entry.target_hash << "_" << original_hash << "_" << manifest_stamp << "_" << patch_stamp;
        const auto output_path = cache_root / (key.str() + ".patched.dxbc");

        if (!std::filesystem::exists(output_path)) {
            ShaderDxilTextPatchRequest request{};
            request.bytecode = original_bytecode;
            request.bytecode_size = original_bytecode_size;
            request.patches = entry.dxil_text_patches;
            request.validate_and_sign = true;
            const auto patched = patch_dxil_text(request);
            if (!patched.succeeded) {
                error_out = patched.error;
                return false;
            }

            if (!write_binary_file(output_path, patched.bytecode.data(), patched.bytecode.size(), error_out)) {
                return false;
            }
        }

        if (!read_binary_file(output_path, entry.compiled_bytecode, error_out)) {
            return false;
        }

        if (entry.compiled_bytecode.empty()) {
            error_out = "DXIL text patch produced an empty output: " + output_path.string();
            return false;
        }

        entry.compiled_original_hash = std::string{original_hash};
        entry.cached_bytecode_path = output_path;
        entry.compiled = true;
        entry.status = "Patched DXIL text";
        entry.compiler = "dxc-assembler";
        entry.last_error.clear();
        push_event("Patched DXIL text override " + entry.key + " -> " + output_path.string());
        return true;
    }

    if (entry.source_kind == OverrideSourceKind::ContainerPatch) {
        const auto cache_root = profile_override_dir() / "cache" / "container_patch";
        std::error_code ec{};
        std::filesystem::create_directories(cache_root, ec);
        if (ec) {
            error_out = "Failed to create container patch cache: " + ec.message();
            return false;
        }

        const auto manifest_stamp = std::filesystem::exists(entry.manifest_path)
            ? std::filesystem::last_write_time(entry.manifest_path, ec).time_since_epoch().count()
            : 0;
        ec.clear();
        const auto patch_stamp = std::filesystem::exists(entry.patch_path)
            ? std::filesystem::last_write_time(entry.patch_path, ec).time_since_epoch().count()
            : 0;

        std::ostringstream key{};
        key << entry.target_hash << "_" << original_hash << "_" << manifest_stamp << "_" << patch_stamp;
        const auto output_path = cache_root / (key.str() + ".patched.dxbc");

        if (!std::filesystem::exists(output_path)) {
            ShaderContainerEditRequest request{};
            request.bytecode = original_bytecode;
            request.bytecode_size = original_bytecode_size;
            request.edits = entry.container_edits;
            request.validate_and_sign = true;

            const auto patched = edit_shader_container(request);
            if (!patched.succeeded) {
                error_out = patched.error;
                return false;
            }

            if (!write_binary_file(output_path, patched.bytecode.data(), patched.bytecode.size(), error_out)) {
                return false;
            }
        }

        if (!read_binary_file(output_path, entry.compiled_bytecode, error_out)) {
            return false;
        }

        if (entry.compiled_bytecode.empty()) {
            error_out = "Container patch produced an empty output: " + output_path.string();
            return false;
        }

        entry.compiled_original_hash = std::string{original_hash};
        entry.cached_bytecode_path = output_path;
        entry.compiled = true;
        entry.status = "Patched container";
        entry.compiler = "dxc-container-builder";
        entry.last_error.clear();
        push_event("Patched container override " + entry.key + " -> " + output_path.string());
        return true;
    }

    const bool is_transform = entry.source_kind == OverrideSourceKind::DxilTransform;
    const bool is_semantic_transform = entry.source_kind == OverrideSourceKind::DxilSemanticTransform;
    const auto tool_path = entry.patch_tool_path.empty()
        ? (is_semantic_transform ? default_dxil_semantic_tool_path() : default_dxil_patch_tool_path())
        : entry.patch_tool_path;
    if (tool_path.empty() || !std::filesystem::exists(tool_path)) {
        error_out = std::string{is_semantic_transform ? "DXIL semantic transform tool not found: " : "dxil-patch tool not found: "} +
            tool_path.string() +
            (is_semantic_transform
                ? " (set UEVR_DXIL_SEMANTIC_TOOL or use manifest semantic_tool)"
                : " (set UEVR_DXIL_PATCH_TOOL or use manifest patch_tool)");
        return false;
    }

    const auto cache_root = profile_override_dir() / "cache" /
        (is_semantic_transform ? "dxil_semantic_transform" : (is_transform ? "dxil_transform" : "dxil_patch"));
    std::error_code ec{};
    std::filesystem::create_directories(cache_root, ec);
    if (ec) {
        error_out = std::string{"Failed to create "} +
            (is_semantic_transform ? "DXIL semantic transform" : (is_transform ? "DXIL transform" : "DXIL patch")) +
            " cache: " + ec.message();
        return false;
    }

    const auto manifest_stamp = std::filesystem::exists(entry.manifest_path)
        ? std::filesystem::last_write_time(entry.manifest_path, ec).time_since_epoch().count()
        : 0;
    ec.clear();
    const auto patch_stamp = std::filesystem::exists(entry.patch_path)
        ? std::filesystem::last_write_time(entry.patch_path, ec).time_since_epoch().count()
        : 0;

    std::ostringstream key{};
    key << entry.target_hash << "_" << original_hash << "_" << manifest_stamp << "_" << patch_stamp;
    const auto original_path = cache_root / (key.str() + ".original.dxbc");
    const auto output_path = cache_root / (key.str() +
        (is_semantic_transform ? ".semantic.dxbc" : (is_transform ? ".transformed.dxbc" : ".patched.dxbc")));
    const auto report_path = cache_root / (key.str() + ".report.json");

    if (!std::filesystem::exists(output_path)) {
        if (!write_binary_file(original_path, original_bytecode, original_bytecode_size, error_out)) {
            return false;
        }

        const std::wstring args = is_semantic_transform
            ? (quote_command_arg(original_path) +
                L" " +
                quote_command_arg(entry.patch_path) +
                L" -o " +
                quote_command_arg(output_path) +
                L" --report " +
                quote_command_arg(report_path))
            : (std::wstring{is_transform ? L"transform " : L"patch "} +
                quote_command_arg(original_path) +
                L" " +
                quote_command_arg(entry.patch_path) +
                L" -o " +
                quote_command_arg(output_path) +
                L" --report " +
                quote_command_arg(report_path));

        DWORD exit_code = 0;
        std::string process_error{};
        if (!run_process_wait(tool_path, args, 30000, exit_code, process_error)) {
            error_out = process_error;
            return false;
        }

        if (exit_code != 0) {
            error_out = std::string{is_semantic_transform ? "dxil-semantic-transform" : (is_transform ? "dxil-transform" : "dxil-patch")} +
                " failed with exit code " + std::to_string(exit_code);
            if (std::filesystem::exists(report_path)) {
                std::ifstream report{report_path, std::ios::binary};
                if (report) {
                    std::ostringstream ss{};
                    ss << report.rdbuf();
                    error_out += ": " + ss.str();
                }
            }
            return false;
        }
    }

    std::vector<uint8_t> patched{};
    if (!read_binary_file(output_path, patched, error_out)) {
        return false;
    }

    if (patched.empty()) {
        error_out = std::string{is_semantic_transform ? "dxil-semantic-transform" : (is_transform ? "dxil-transform" : "dxil-patch")} +
            " produced an empty output: " + output_path.string();
        return false;
    }

    entry.compiled_bytecode = std::move(patched);
    entry.compiled_original_hash = std::string{original_hash};
    entry.cached_bytecode_path = output_path;
    entry.compiled = true;
    entry.status = is_semantic_transform ? "Semantically transformed DXIL" : (is_transform ? "Transformed DXIL" : "Patched DXIL");
    entry.compiler = is_semantic_transform ? "dxil-semantic-transform" : (is_transform ? "dxil-transform" : "dxil-patch");
    entry.last_error.clear();

    push_event(std::string{is_semantic_transform ? "Semantically transformed DXIL override " : (is_transform ? "Transformed DXIL override " : "Patched DXIL override ")} + entry.key + " -> " + output_path.string());
    spdlog::info("[ShaderOverrideRegistry] {} DXIL override key={} input={} output={} bytes={}",
        is_semantic_transform ? "Semantically transformed" : (is_transform ? "Transformed" : "Patched"),
        entry.key, original_path.string(), output_path.string(), entry.compiled_bytecode.size());
    return true;
}

void ShaderOverrideRegistry::push_event(std::string message) {
    if (m_recent_events.size() >= MAX_RECENT_EVENTS) {
        m_recent_events.erase(m_recent_events.begin());
    }

    m_recent_events.emplace_back(std::move(message));
}

void ShaderOverrideRegistry::refresh_active_override_flags_locked() {
    bool has_d3d11 = false;
    bool has_d3d12 = false;

    for (const auto& [_, entry] : m_overrides) {
        const bool can_activate = entry.enabled &&
            entry.apply_supported &&
            (entry.compiled ||
                entry.source_kind == OverrideSourceKind::DxilPatch ||
                entry.source_kind == OverrideSourceKind::DxilTextPatch ||
                entry.source_kind == OverrideSourceKind::ContainerPatch ||
                entry.source_kind == OverrideSourceKind::DxilTransform ||
                entry.source_kind == OverrideSourceKind::DxilSemanticTransform ||
                entry.left_payload.present ||
                entry.right_payload.present);
        if (!can_activate) {
            continue;
        }

        if (entry.backend == Backend::D3D11) {
            has_d3d11 = true;
        } else if (entry.backend == Backend::D3D12) {
            has_d3d12 = true;
        }
    }

    m_has_active_d3d11_overrides.store(has_d3d11, std::memory_order_relaxed);
    m_has_active_d3d12_overrides.store(has_d3d12, std::memory_order_relaxed);
}

void ShaderOverrideRegistry::record_d3d12_pipeline_pair(const D3D12PipelinePairInfo& info) {
    ++m_total_d3d12_pair_samples;

    auto pair = info;
    const auto pair_key = make_d3d12_pair_key(pair);
    const auto pair_changed = !m_last_d3d12_pair.has_value() || !same_d3d12_pipeline_pair(*m_last_d3d12_pair, info);

    if (const auto index_it = m_distinct_d3d12_pair_indices.find(pair_key); index_it != m_distinct_d3d12_pair_indices.end()) {
        auto& aggregate = m_distinct_d3d12_pairs[index_it->second];
        aggregate.frame = info.frame;
        aggregate.last_seen_frame = info.frame;
        ++aggregate.hit_count;
        pair.first_seen_frame = aggregate.first_seen_frame;
        pair.last_seen_frame = aggregate.last_seen_frame;
        pair.hit_count = aggregate.hit_count;
        aggregate.vertex_shader = pair.vertex_shader;
        aggregate.pixel_shader = pair.pixel_shader;
        aggregate.geometry_shader = pair.geometry_shader;
        aggregate.tracking_note = pair.tracking_note;
        aggregate.bound_pipeline_state = pair.bound_pipeline_state;
    } else {
        pair.first_seen_frame = info.frame;
        pair.last_seen_frame = info.frame;
        pair.hit_count = 1;
        m_distinct_d3d12_pair_indices.emplace(pair_key, m_distinct_d3d12_pairs.size());
        m_distinct_d3d12_pairs.emplace_back(pair);
    }

    m_last_d3d12_pair = pair;

    if (pair_changed && m_capture_next_d3d12_change) {
        m_captured_d3d12_pair = pair;
        m_capture_next_d3d12_change = false;
        m_capture_next_d3d12_change_hot_path.store(false, std::memory_order_relaxed);

        std::ostringstream ss{};
        ss << "Captured DX12 shader change at frame " << pair.frame;
        if (pair.original_pipeline_state != 0) {
            ss << " PSO=0x" << std::hex << std::uppercase << pair.original_pipeline_state;
        }
        push_event(ss.str());
    }
}

void ShaderOverrideRegistry::record_d3d12_pso_sample(const D3D12PipelinePairInfo& info) {
    ++m_total_d3d12_pso_samples;

    auto& aggregate = m_d3d12_pso_aggregates[make_d3d12_pso_key(info)];
    if (aggregate.first_seen_frame == 0) {
        aggregate.first_seen_frame = info.frame;
        aggregate.original_pso = info.original_pipeline_state;
        aggregate.pipeline_stream = info.pipeline_stream;
        aggregate.tracking_note = info.tracking_note;
        aggregate.vs_hash = info.vertex_shader.hash;
        aggregate.ps_hash = info.pixel_shader.hash;
        aggregate.gs_hash = info.geometry_shader.hash;
        aggregate.vs_crc32 = info.vertex_shader.crc32;
        aggregate.ps_crc32 = info.pixel_shader.crc32;
        aggregate.gs_crc32 = info.geometry_shader.crc32;
    }

    aggregate.last_seen_frame = info.frame;
    aggregate.last_bound_pso = info.bound_pipeline_state;
    aggregate.pipeline_stream = info.pipeline_stream;
    aggregate.tracking_note = info.tracking_note;
    aggregate.vs_hash = info.vertex_shader.hash;
    aggregate.ps_hash = info.pixel_shader.hash;
    aggregate.gs_hash = info.geometry_shader.hash;
    aggregate.vs_crc32 = info.vertex_shader.crc32;
    aggregate.ps_crc32 = info.pixel_shader.crc32;
    aggregate.gs_crc32 = info.geometry_shader.crc32;
    aggregate.vs_override = info.vertex_shader.override_active ? info.vertex_shader.override_name : "";
    aggregate.ps_override = info.pixel_shader.override_active ? info.pixel_shader.override_name : "";
    aggregate.gs_override = info.geometry_shader.override_active ? info.geometry_shader.override_name : "";
    ++aggregate.total_samples;

    const auto bind_context = D3D12Diagnostics::get().current_bind_context();
    if (!bind_context.has_value() || bind_context->frame > info.frame || (info.frame - bind_context->frame) > MAX_PSO_BIND_CONTEXT_AGE_FRAMES) {
        return;
    }

    const auto render_target_name = join_target_names(bind_context->render_targets);
    const auto render_target_key = join_target_keys(bind_context->render_targets);
    const auto depth_target_name = bind_context->depth_target.has_value()
        ? bind_context->depth_target->name
        : std::string{};
    const auto depth_target_key = bind_context->depth_target.has_value()
        ? format_pointer_to_hex(bind_context->depth_target->handle)
        : std::string{};

    if (render_target_name.empty() && depth_target_name.empty()) {
        return;
    }

    std::ostringstream usage_key{};
    usage_key << render_target_key << '|' << depth_target_key;

    auto& usage = aggregate.usage_by_key[usage_key.str()];
    if (usage.hit_count == 0) {
        usage.render_target_name = render_target_name;
        usage.depth_target_name = depth_target_name;
        usage.render_target_key = render_target_key;
        usage.depth_target_key = depth_target_key;
    }

    ++usage.hit_count;
    ++aggregate.bind_count_with_known_targets;
}

std::string ShaderOverrideRegistry::make_d3d12_pair_key(const D3D12PipelinePairInfo& info) const {
    std::ostringstream ss{};
    ss << std::hex << std::uppercase
       << info.original_pipeline_state << ':'
       << info.bound_pipeline_state << ':'
       << info.vertex_shader.hash << ':'
       << info.pixel_shader.hash << ':'
       << info.geometry_shader.hash << ':'
       << info.vertex_shader.crc32 << ':'
       << info.pixel_shader.crc32 << ':'
       << info.geometry_shader.crc32 << ':'
       << info.tracking_note;
    return ss.str();
}

std::string ShaderOverrideRegistry::make_d3d12_pso_key(const D3D12PipelinePairInfo& info) const {
    std::ostringstream ss{};
    ss << std::hex << std::uppercase
       << info.original_pipeline_state << ':'
       << info.vertex_shader.hash << ':'
       << info.pixel_shader.hash << ':'
       << info.geometry_shader.hash << ':'
       << info.vertex_shader.crc32 << ':'
       << info.pixel_shader.crc32 << ':'
       << info.geometry_shader.crc32 << ':'
       << static_cast<uint32_t>(info.pipeline_stream) << ':'
       << info.tracking_note;
    return ss.str();
}

std::filesystem::path ShaderOverrideRegistry::make_d3d12_pair_export_path(const char* extension) const {
    const auto export_dir = Framework::get_persistent_dir("render_inspector");
    const auto now = std::chrono::system_clock::now();
    const auto now_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
    localtime_s(&local_time, &now_time);

    std::ostringstream file_name{};
    file_name << "dx12_shader_pairs_" << std::put_time(&local_time, "%Y%m%d_%H%M%S") << '.' << extension;
    return export_dir / file_name.str();
}

void ShaderOverrideRegistry::update_d3d11_override_shader(D3D11ShaderRecord& record, ID3D11Device* device) {
    const auto key = make_override_key(Backend::D3D11, record.stage, record.hash);
    const auto override_it = m_overrides.find(key);

    if (override_it == m_overrides.end() || !override_it->second.enabled || !override_it->second.compiled) {
        record.override_active = false;
        record.override_name.clear();
        record.override_shader.Reset();
        record.override_generation = 0;
        return;
    }

    auto& entry = override_it->second;
    if (record.override_generation == entry.generation && record.override_shader != nullptr) {
        record.override_active = true;
        record.override_name = entry.name;
        return;
    }

    if (device == nullptr) {
        record.override_active = false;
        record.override_name.clear();
        record.override_shader.Reset();
        return;
    }

    HRESULT hr = E_FAIL;
    Microsoft::WRL::ComPtr<ID3D11DeviceChild> new_shader{};

    if (record.stage == Stage::Vertex) {
        if (m_create_vertex_shader == nullptr) {
            record.override_active = false;
            record.override_name.clear();
            record.override_shader.Reset();
            return;
        }

        ID3D11VertexShader* created = nullptr;
        hr = m_create_vertex_shader(device, entry.compiled_bytecode.data(), entry.compiled_bytecode.size(), nullptr, &created);
        if (SUCCEEDED(hr) && created != nullptr) {
            new_shader.Attach(created);
        }
    } else {
        if (m_create_pixel_shader == nullptr) {
            record.override_active = false;
            record.override_name.clear();
            record.override_shader.Reset();
            return;
        }

        ID3D11PixelShader* created = nullptr;
        hr = m_create_pixel_shader(device, entry.compiled_bytecode.data(), entry.compiled_bytecode.size(), nullptr, &created);
        if (SUCCEEDED(hr) && created != nullptr) {
            new_shader.Attach(created);
        }
    }

    if (FAILED(hr) || new_shader == nullptr) {
        std::ostringstream ss{};
        ss << "Failed to create D3D11 override shader for " << key << " (HRESULT 0x" << std::hex << std::uppercase << static_cast<uint32_t>(hr) << ")";
        push_event(ss.str());
        record.override_active = false;
        record.override_name.clear();
        record.override_shader.Reset();
        return;
    }

    record.override_generation = entry.generation;
    record.override_shader = new_shader;
    record.override_active = true;
    record.override_name = entry.name;
}

void ShaderOverrideRegistry::update_d3d12_override_pipeline_state(D3D12GraphicsPsoRecord& record) {
    const auto vertex_key = record.vertex_hash.empty() ? std::string{} : make_override_key(Backend::D3D12, Stage::Vertex, record.vertex_hash);
    const auto pixel_key = record.pixel_hash.empty() ? std::string{} : make_override_key(Backend::D3D12, Stage::Pixel, record.pixel_hash);
    const auto geometry_key = record.geometry_hash.empty() ? std::string{} : make_override_key(Backend::D3D12, Stage::Geometry, record.geometry_hash);
    const auto compute_key = record.compute_hash.empty() ? std::string{} : make_override_key(Backend::D3D12, Stage::Compute, record.compute_hash);
    const auto amplification_key = record.amplification_hash.empty() ? std::string{} : make_override_key(Backend::D3D12, Stage::Amplification, record.amplification_hash);
    const auto mesh_key = record.mesh_hash.empty() ? std::string{} : make_override_key(Backend::D3D12, Stage::Mesh, record.mesh_hash);
    // Parallel CRC32 lookup keys. A manifest whose target_hash is 8 hex chars
    // (== ShaderToggler-format CRC32) lives in m_overrides at this key. This
    // lets users drop their ShaderToggler hashes straight into UEVR manifests.
    char crc_buf[32]{};
    std::snprintf(crc_buf, sizeof(crc_buf), "%08x", record.vertex_crc32);
    const auto vertex_crc_key = record.vertex_crc32 == 0 ? std::string{}
        : make_override_key(Backend::D3D12, Stage::Vertex, crc_buf);
    std::snprintf(crc_buf, sizeof(crc_buf), "%08x", record.pixel_crc32);
    const auto pixel_crc_key = record.pixel_crc32 == 0 ? std::string{}
        : make_override_key(Backend::D3D12, Stage::Pixel, crc_buf);
    std::snprintf(crc_buf, sizeof(crc_buf), "%08x", record.geometry_crc32);
    const auto geometry_crc_key = record.geometry_crc32 == 0 ? std::string{}
        : make_override_key(Backend::D3D12, Stage::Geometry, crc_buf);
    std::snprintf(crc_buf, sizeof(crc_buf), "%08x", record.compute_crc32);
    const auto compute_crc_key = record.compute_crc32 == 0 ? std::string{}
        : make_override_key(Backend::D3D12, Stage::Compute, crc_buf);
    std::snprintf(crc_buf, sizeof(crc_buf), "%08x", record.amplification_crc32);
    const auto amplification_crc_key = record.amplification_crc32 == 0 ? std::string{}
        : make_override_key(Backend::D3D12, Stage::Amplification, crc_buf);
    std::snprintf(crc_buf, sizeof(crc_buf), "%08x", record.mesh_crc32);
    const auto mesh_crc_key = record.mesh_crc32 == 0 ? std::string{}
        : make_override_key(Backend::D3D12, Stage::Mesh, crc_buf);

    OverrideEntry* vertex_entry = nullptr;
    OverrideEntry* pixel_entry = nullptr;
    OverrideEntry* geometry_entry = nullptr;
    OverrideEntry* compute_entry = nullptr;
    OverrideEntry* amplification_entry = nullptr;
    OverrideEntry* mesh_entry = nullptr;

    auto try_lookup = [this](const std::string& key) -> OverrideEntry* {
        if (key.empty()) return nullptr;
        const auto it = m_overrides.find(key);
        if (it == m_overrides.end() ||
            !it->second.enabled ||
            !it->second.apply_supported ||
            (!it->second.compiled &&
                it->second.source_kind != OverrideSourceKind::DxilPatch &&
                it->second.source_kind != OverrideSourceKind::DxilTextPatch &&
                it->second.source_kind != OverrideSourceKind::ContainerPatch &&
                it->second.source_kind != OverrideSourceKind::DxilTransform &&
                it->second.source_kind != OverrideSourceKind::DxilSemanticTransform &&
                !it->second.left_payload.present &&
                !it->second.right_payload.present)) {
            return nullptr;
        }
        return &it->second;
    };

    vertex_entry = try_lookup(vertex_key);
    if (vertex_entry == nullptr) vertex_entry = try_lookup(vertex_crc_key);
    pixel_entry = try_lookup(pixel_key);
    if (pixel_entry == nullptr) pixel_entry = try_lookup(pixel_crc_key);
    geometry_entry = try_lookup(geometry_key);
    if (geometry_entry == nullptr) geometry_entry = try_lookup(geometry_crc_key);
    compute_entry = try_lookup(compute_key);
    if (compute_entry == nullptr) compute_entry = try_lookup(compute_crc_key);
    amplification_entry = try_lookup(amplification_key);
    if (amplification_entry == nullptr) amplification_entry = try_lookup(amplification_crc_key);
    mesh_entry = try_lookup(mesh_key);
    if (mesh_entry == nullptr) mesh_entry = try_lookup(mesh_crc_key);

    // === Highlight mode: if PS hash is in highlight set OR the cycle-mode
    // highlight flag is on AND the hash matches the active hunted PS hash,
    // synthesize a transient OverrideEntry whose compiled_bytecode is the
    // magenta PS variant matching the PSO's RT count. ===
    static thread_local OverrideEntry hunter_highlight_transient{};
    const bool cycle_highlight = m_hunter_cycle_highlight_mode.load(std::memory_order_relaxed);
    char pixel_crc_str[16]{};
    if (record.pixel_crc32 != 0) {
        std::snprintf(pixel_crc_str, sizeof(pixel_crc_str), "%08x", record.pixel_crc32);
    }
    const std::string pixel_crc_hash = pixel_crc_str;
    const bool match_per_row_highlight =
        (!record.pixel_hash.empty() && m_hunter_highlight.count(record.pixel_hash) > 0) ||
        (!pixel_crc_hash.empty() && m_hunter_highlight.count(pixel_crc_hash) > 0);
    const bool match_cycle_active_ps = cycle_highlight && !record.pixel_hash.empty() && record.pixel_hash == m_hunter_active_hash;
    const bool match_cycle_active_vs = cycle_highlight && !record.vertex_hash.empty() && record.vertex_hash == m_hunter_active_hash_vs;
    if (pixel_entry == nullptr && (match_per_row_highlight || match_cycle_active_ps || match_cycle_active_vs)) {
        // Pick the RT count: graphics-desc gives it directly; stream PSOs
        // require trial-and-error (the rt_formats subobject isn't parsed
        // out). For stream PSOs we'll retry with each variant from 8 down
        // to 1 below if CreatePipelineState fails. Start with a best guess.
        int rt_count = -1;
        if (!record.is_pipeline_stream) {
            const auto& d = record.owned_desc.desc;
            if (d.NumRenderTargets >= 1 && d.NumRenderTargets <= 8) {
                rt_count = static_cast<int>(d.NumRenderTargets);
            }
        }
        // For stream PSOs: try the LARGEST variant first (8) — most likely
        // to match a UE5 deferred basepass with 4-6 RTs. The PSO-create call
        // below will reject mismatches; retry logic at the bottom of this
        // function falls back to smaller variants.
        if (rt_count < 1) rt_count = 8;
        if (rt_count >= 1 && rt_count <= 8 && !m_hunter_magenta_ps[rt_count].empty()) {
            hunter_highlight_transient = OverrideEntry{};
            hunter_highlight_transient.backend = Backend::D3D12;
            hunter_highlight_transient.stage = Stage::Pixel;
            hunter_highlight_transient.target_hash = record.pixel_hash;
            hunter_highlight_transient.key = "dx12:ps:highlight:" + record.pixel_hash;
            hunter_highlight_transient.name = "hunter_highlight";
            hunter_highlight_transient.enabled = true;
            hunter_highlight_transient.entry_point = "main";
            hunter_highlight_transient.profile = "ps_6_0";
            hunter_highlight_transient.preferred_compiler = ShaderCompilerBackend::Dxc;
            hunter_highlight_transient.compiled = true;
            hunter_highlight_transient.apply_supported = true;
            hunter_highlight_transient.compiled_bytecode = m_hunter_magenta_ps[rt_count];
            hunter_highlight_transient.status = "Hunter-highlight (RT" + std::to_string(rt_count) + ")";
            pixel_entry = &hunter_highlight_transient;
        }
    }

    auto ensure_patch_entry = [this, &record](OverrideEntry*& entry, Stage stage) {
        if (entry == nullptr ||
            (entry->source_kind != OverrideSourceKind::DxilPatch &&
                entry->source_kind != OverrideSourceKind::DxilTextPatch &&
                entry->source_kind != OverrideSourceKind::ContainerPatch &&
                entry->source_kind != OverrideSourceKind::DxilTransform &&
                entry->source_kind != OverrideSourceKind::DxilSemanticTransform)) {
            return;
        }

        const std::vector<uint8_t>* original = nullptr;
        std::string_view original_hash{};
        if (record.is_pipeline_stream) {
            switch (stage) {
            case Stage::Vertex:
                original = &record.owned_stream.vertex_shader; original_hash = record.vertex_hash; break;
            case Stage::Pixel:
                original = &record.owned_stream.pixel_shader; original_hash = record.pixel_hash; break;
            case Stage::Geometry:
                original = &record.owned_stream.geometry_shader; original_hash = record.geometry_hash; break;
            case Stage::Compute:
                original = &record.owned_stream.compute_shader; original_hash = record.compute_hash; break;
            case Stage::Amplification:
                original = &record.owned_stream.amplification_shader; original_hash = record.amplification_hash; break;
            case Stage::Mesh:
                original = &record.owned_stream.mesh_shader; original_hash = record.mesh_hash; break;
            }
        } else {
            switch (stage) {
            case Stage::Vertex:
                original = &record.owned_desc.vertex_shader; original_hash = record.vertex_hash; break;
            case Stage::Pixel:
                original = &record.owned_desc.pixel_shader; original_hash = record.pixel_hash; break;
            case Stage::Geometry:
                original = &record.owned_desc.geometry_shader; original_hash = record.geometry_hash; break;
            case Stage::Compute:
                original = &record.owned_stream.compute_shader; original_hash = record.compute_hash; break;
            case Stage::Amplification:
                original = &record.owned_stream.amplification_shader; original_hash = record.amplification_hash; break;
            case Stage::Mesh:
                original = &record.owned_stream.mesh_shader; original_hash = record.mesh_hash; break;
            }
        }

        std::string patch_error{};
        if (original == nullptr || original->empty() ||
            !ensure_d3d12_patch_entry_compiled(*entry, original->data(), original->size(), original_hash, patch_error)) {
            const auto stage_name = stage_to_string(stage);
            record.last_error = std::string{"Failed to build DXIL override "} + stage_name + "=" + entry->name + ": " + patch_error;
            entry->last_error = record.last_error;
            entry->status = "DXIL override failed";
            entry->compiled = false;
            entry = nullptr;
            push_event(record.last_error);
            spdlog::error("[ShaderOverrideRegistry] {}", record.last_error);
            return;
        }

        if (entry->compiled_bytecode.empty()) {
            record.last_error = "DXIL patch produced no bytecode for " + entry->name;
            entry->last_error = record.last_error;
            entry->compiled = false;
            entry = nullptr;
            push_event(record.last_error);
        }
    };

    auto original_bytecode_for_stage = [&record](Stage stage, const std::vector<uint8_t>*& original, std::string_view& original_hash) {
        original = nullptr;
        original_hash = {};
        if (record.is_pipeline_stream) {
            switch (stage) {
            case Stage::Vertex: original = &record.owned_stream.vertex_shader; original_hash = record.vertex_hash; break;
            case Stage::Pixel: original = &record.owned_stream.pixel_shader; original_hash = record.pixel_hash; break;
            case Stage::Geometry: original = &record.owned_stream.geometry_shader; original_hash = record.geometry_hash; break;
            case Stage::Compute: original = &record.owned_stream.compute_shader; original_hash = record.compute_hash; break;
            case Stage::Amplification: original = &record.owned_stream.amplification_shader; original_hash = record.amplification_hash; break;
            case Stage::Mesh: original = &record.owned_stream.mesh_shader; original_hash = record.mesh_hash; break;
            }
            return;
        }

        switch (stage) {
        case Stage::Vertex: original = &record.owned_desc.vertex_shader; original_hash = record.vertex_hash; break;
        case Stage::Pixel: original = &record.owned_desc.pixel_shader; original_hash = record.pixel_hash; break;
        case Stage::Geometry: original = &record.owned_desc.geometry_shader; original_hash = record.geometry_hash; break;
        case Stage::Compute: original = &record.owned_stream.compute_shader; original_hash = record.compute_hash; break;
        case Stage::Amplification: original = &record.owned_stream.amplification_shader; original_hash = record.amplification_hash; break;
        case Stage::Mesh: original = &record.owned_stream.mesh_shader; original_hash = record.mesh_hash; break;
        }
    };

    auto ensure_eye_payload = [this, &record, &original_bytecode_for_stage](OverrideEntry* entry, Stage stage, bool left_eye) -> bool {
        if (entry == nullptr) {
            return true;
        }

        auto& payload = left_eye ? entry->left_payload : entry->right_payload;
        if (!payload.present) {
            return true;
        }

        const char* eye_name = left_eye ? "left" : "right";
        if (payload.source_kind == OverrideSourceKind::Bytecode) {
            if (!payload.compiled_bytecode.empty()) {
                return true;
            }

            std::string error{};
            if (!read_binary_file(payload.bytecode_path, payload.compiled_bytecode, error) || payload.compiled_bytecode.empty()) {
                record.last_error = std::string{"Failed to load "} + eye_name + " bytecode for " + entry->name + ": " + error;
                payload.last_error = record.last_error;
                entry->last_error = record.last_error;
                push_event(record.last_error);
                return false;
            }

            payload.status = "Loaded bytecode";
            return true;
        }

        const std::vector<uint8_t>* original = nullptr;
        std::string_view original_hash{};
        original_bytecode_for_stage(stage, original, original_hash);
        if (original == nullptr || original->empty()) {
            record.last_error = std::string{"Original bytecode unavailable for "} + eye_name + " payload " + entry->name;
            payload.last_error = record.last_error;
            entry->last_error = record.last_error;
            push_event(record.last_error);
            return false;
        }

        if (!payload.compiled_bytecode.empty() && payload.compiled_original_hash == original_hash) {
            return true;
        }

        OverrideEntry temp{};
        temp.key = entry->key + ":" + eye_name;
        temp.name = entry->name + ":" + eye_name;
        temp.backend = entry->backend;
        temp.stage = entry->stage;
        temp.target_hash = entry->target_hash;
        temp.source_kind = payload.source_kind;
        temp.manifest_path = entry->manifest_path;
        temp.patch_path = payload.patch_path;
        temp.patch_tool_path = entry->patch_tool_path;
        temp.dxil_text_patches = payload.dxil_text_patches;
        temp.container_edits = payload.container_edits;
        temp.enabled = true;
        temp.apply_supported = true;

        std::string error{};
        if (!ensure_d3d12_patch_entry_compiled(temp, original->data(), original->size(), original_hash, error)) {
            record.last_error = std::string{"Failed to build "} + eye_name + " DXIL payload " + entry->name + ": " + error;
            payload.last_error = record.last_error;
            entry->last_error = record.last_error;
            push_event(record.last_error);
            spdlog::error("[ShaderOverrideRegistry] {}", record.last_error);
            return false;
        }

        payload.compiled_bytecode = std::move(temp.compiled_bytecode);
        payload.compiled_original_hash = temp.compiled_original_hash;
        payload.cached_bytecode_path = temp.cached_bytecode_path;
        payload.status = temp.status;
        payload.last_error.clear();
        return !payload.compiled_bytecode.empty();
    };

    if (record.applied_override_revision == m_override_revision) {
        return;
    }

    ensure_patch_entry(vertex_entry, Stage::Vertex);
    ensure_patch_entry(pixel_entry, Stage::Pixel);
    ensure_patch_entry(geometry_entry, Stage::Geometry);
    ensure_patch_entry(compute_entry, Stage::Compute);
    ensure_patch_entry(amplification_entry, Stage::Amplification);
    ensure_patch_entry(mesh_entry, Stage::Mesh);
    bool eye_payloads_ready = true;
    auto require_eye_payload = [&](OverrideEntry* entry, Stage stage, bool left_eye) {
        eye_payloads_ready = ensure_eye_payload(entry, stage, left_eye) && eye_payloads_ready;
    };
    require_eye_payload(vertex_entry, Stage::Vertex, true);
    require_eye_payload(vertex_entry, Stage::Vertex, false);
    require_eye_payload(pixel_entry, Stage::Pixel, true);
    require_eye_payload(pixel_entry, Stage::Pixel, false);
    require_eye_payload(geometry_entry, Stage::Geometry, true);
    require_eye_payload(geometry_entry, Stage::Geometry, false);
    require_eye_payload(compute_entry, Stage::Compute, true);
    require_eye_payload(compute_entry, Stage::Compute, false);
    require_eye_payload(amplification_entry, Stage::Amplification, true);
    require_eye_payload(amplification_entry, Stage::Amplification, false);
    require_eye_payload(mesh_entry, Stage::Mesh, true);
    require_eye_payload(mesh_entry, Stage::Mesh, false);
    const auto preflight_error = record.last_error;

    record.applied_override_revision = m_override_revision;
    record.override_active = false;
    record.vertex_override_name.clear();
    record.pixel_override_name.clear();
    record.geometry_override_name.clear();
    record.compute_override_name.clear();
    record.amplification_override_name.clear();
    record.mesh_override_name.clear();
    record.last_error.clear();
    record.override_pipeline_state.Reset();
    record.override_pipeline_state_left.Reset();
    record.override_pipeline_state_right.Reset();

    if (!eye_payloads_ready) {
        record.last_error = !preflight_error.empty()
            ? preflight_error
            : "Per-eye shader payload preflight failed";
        return;
    }

    if (record.device == nullptr) {
        record.last_error = "Device unavailable";
        return;
    }

    if (vertex_entry == nullptr &&
        pixel_entry == nullptr &&
        geometry_entry == nullptr &&
        compute_entry == nullptr &&
        amplification_entry == nullptr &&
        mesh_entry == nullptr) {
        if (!preflight_error.empty()) {
            record.last_error = preflight_error;
        }
        return;
    }

    if (vertex_entry != nullptr) {
        record.vertex_override_name = vertex_entry->name;
    }

    if (pixel_entry != nullptr) {
        record.pixel_override_name = pixel_entry->name;
    }

    if (geometry_entry != nullptr) {
        record.geometry_override_name = geometry_entry->name;
    }

    if (compute_entry != nullptr) {
        record.compute_override_name = compute_entry->name;
    }

    if (amplification_entry != nullptr) {
        record.amplification_override_name = amplification_entry->name;
    }

    if (mesh_entry != nullptr) {
        record.mesh_override_name = mesh_entry->name;
    }

    Microsoft::WRL::ComPtr<ID3D12PipelineState> replacement_pso{};
    HRESULT hr = E_FAIL;
    const bool wants_per_eye_variants =
        (vertex_entry != nullptr && vertex_entry->per_eye_variants) ||
        (pixel_entry != nullptr && pixel_entry->per_eye_variants) ||
        (geometry_entry != nullptr && geometry_entry->per_eye_variants) ||
        (compute_entry != nullptr && compute_entry->per_eye_variants) ||
        (amplification_entry != nullptr && amplification_entry->per_eye_variants) ||
        (mesh_entry != nullptr && mesh_entry->per_eye_variants);

    auto select_entry_bytecode = [](const OverrideEntry* entry, int eye_bucket) -> const std::vector<uint8_t>* {
        if (entry == nullptr) {
            return nullptr;
        }

        if (eye_bucket == 1 && entry->left_payload.present && !entry->left_payload.compiled_bytecode.empty()) {
            return &entry->left_payload.compiled_bytecode;
        }

        if (eye_bucket == 2 && entry->right_payload.present && !entry->right_payload.compiled_bytecode.empty()) {
            return &entry->right_payload.compiled_bytecode;
        }

        if (!entry->compiled_bytecode.empty()) {
            return &entry->compiled_bytecode;
        }

        if (eye_bucket == 0) {
            if (entry->left_payload.present && !entry->left_payload.compiled_bytecode.empty()) {
                return &entry->left_payload.compiled_bytecode;
            }
            if (entry->right_payload.present && !entry->right_payload.compiled_bytecode.empty()) {
                return &entry->right_payload.compiled_bytecode;
            }
        }

        return nullptr;
    };

    auto apply_stream_bytecode = [&](OwnedD3D12PipelineStateStream& stream, int eye_bucket) {
        if (const auto* bytes = select_entry_bytecode(vertex_entry, eye_bucket); bytes != nullptr) {
            stream.vertex_shader = *bytes;
        }
        if (const auto* bytes = select_entry_bytecode(pixel_entry, eye_bucket); bytes != nullptr) {
            stream.pixel_shader = *bytes;
        }
        if (const auto* bytes = select_entry_bytecode(geometry_entry, eye_bucket); bytes != nullptr) {
            stream.geometry_shader = *bytes;
        }
        if (const auto* bytes = select_entry_bytecode(compute_entry, eye_bucket); bytes != nullptr) {
            stream.compute_shader = *bytes;
        }
        if (const auto* bytes = select_entry_bytecode(amplification_entry, eye_bucket); bytes != nullptr) {
            stream.amplification_shader = *bytes;
        }
        if (const auto* bytes = select_entry_bytecode(mesh_entry, eye_bucket); bytes != nullptr) {
            stream.mesh_shader = *bytes;
        }
        stream.refresh_views();
    };

    auto apply_graphics_desc_bytecode = [&](OwnedD3D12GraphicsPipelineStateDesc& desc, int eye_bucket) {
        if (const auto* bytes = select_entry_bytecode(vertex_entry, eye_bucket); bytes != nullptr) {
            desc.vertex_shader = *bytes;
        }
        if (const auto* bytes = select_entry_bytecode(pixel_entry, eye_bucket); bytes != nullptr) {
            desc.pixel_shader = *bytes;
        }
        if (const auto* bytes = select_entry_bytecode(geometry_entry, eye_bucket); bytes != nullptr) {
            desc.geometry_shader = *bytes;
        }
        desc.refresh_views();
    };

    if (record.is_pipeline_stream) {
        if (record.owned_stream.empty()) {
            record.last_error = record.tracking_note.empty() ? "pipeline-stream pso not tracked" : record.tracking_note;
            return;
        }

        auto replacement_stream = record.owned_stream;
        apply_stream_bytecode(replacement_stream, 0);

        Microsoft::WRL::ComPtr<ID3D12Device2> device2{};
        hr = record.device->QueryInterface(IID_PPV_ARGS(&device2));

        if (FAILED(hr) || device2 == nullptr) {
            record.last_error = "ID3D12Device2 unavailable for pipeline-stream override";
            record.vertex_override_name.clear();
            record.pixel_override_name.clear();
            record.geometry_override_name.clear();
            record.compute_override_name.clear();
            record.amplification_override_name.clear();
            record.mesh_override_name.clear();
            return;
        }

        ScopedD3D12OverridePipelineCreation scoped_creation{};
        hr = device2->CreatePipelineState(&replacement_stream.desc, IID_PPV_ARGS(&replacement_pso));
        // Highlight-mode RT-count fallback: if the magenta-8 variant failed
        // (PSO has fewer RTs), retry from 7 down to 1. Cheap because variants
        // are pre-compiled and we typically converge in 1-2 tries.
        if (FAILED(hr) && pixel_entry == &hunter_highlight_transient) {
            for (int try_n = 7; try_n >= 1 && FAILED(hr); --try_n) {
                if (m_hunter_magenta_ps[try_n].empty()) continue;
                replacement_stream.pixel_shader = m_hunter_magenta_ps[try_n];
                replacement_stream.refresh_views();
                replacement_pso.Reset();
                hr = device2->CreatePipelineState(&replacement_stream.desc, IID_PPV_ARGS(&replacement_pso));
                if (SUCCEEDED(hr)) {
                    hunter_highlight_transient.status = "Hunter-highlight (RT" + std::to_string(try_n) + " fallback)";
                }
            }
        }
        if (SUCCEEDED(hr) && wants_per_eye_variants) {
            auto left_stream = record.owned_stream;
            apply_stream_bytecode(left_stream, 1);
            auto right_stream = record.owned_stream;
            apply_stream_bytecode(right_stream, 2);
            (void)device2->CreatePipelineState(&left_stream.desc, IID_PPV_ARGS(&record.override_pipeline_state_left));
            (void)device2->CreatePipelineState(&right_stream.desc, IID_PPV_ARGS(&record.override_pipeline_state_right));
        }
    } else if (compute_entry != nullptr && record.vertex_hash.empty() && record.pixel_hash.empty() && !record.compute_hash.empty()) {
        auto replacement_desc = record.compute_desc;
        replacement_desc.pRootSignature = record.owned_stream.root_signature.Get();
        if (const auto* bytes = select_entry_bytecode(compute_entry, 0); bytes != nullptr) {
            replacement_desc.CS = make_shader_bytecode_blob(*bytes);
        }

        ScopedD3D12OverridePipelineCreation scoped_creation{};
        hr = record.device->CreateComputePipelineState(&replacement_desc, IID_PPV_ARGS(&replacement_pso));
        if (SUCCEEDED(hr) && wants_per_eye_variants) {
            auto left_desc = replacement_desc;
            if (const auto* bytes = select_entry_bytecode(compute_entry, 1); bytes != nullptr) {
                left_desc.CS = make_shader_bytecode_blob(*bytes);
            }
            auto right_desc = replacement_desc;
            if (const auto* bytes = select_entry_bytecode(compute_entry, 2); bytes != nullptr) {
                right_desc.CS = make_shader_bytecode_blob(*bytes);
            }
            (void)record.device->CreateComputePipelineState(&left_desc, IID_PPV_ARGS(&record.override_pipeline_state_left));
            (void)record.device->CreateComputePipelineState(&right_desc, IID_PPV_ARGS(&record.override_pipeline_state_right));
        }
    } else {
        if (amplification_entry != nullptr || mesh_entry != nullptr) {
            record.last_error = "AS/MS overrides require a tracked pipeline-state stream PSO";
            record.amplification_override_name.clear();
            record.mesh_override_name.clear();
            return;
        }

        auto replacement_desc = record.owned_desc;
        apply_graphics_desc_bytecode(replacement_desc, 0);

        ScopedD3D12OverridePipelineCreation scoped_creation{};
        hr = record.device->CreateGraphicsPipelineState(&replacement_desc.desc, IID_PPV_ARGS(&replacement_pso));
        if (SUCCEEDED(hr) && wants_per_eye_variants) {
            auto left_desc = record.owned_desc;
            apply_graphics_desc_bytecode(left_desc, 1);
            auto right_desc = record.owned_desc;
            apply_graphics_desc_bytecode(right_desc, 2);
            (void)record.device->CreateGraphicsPipelineState(&left_desc.desc, IID_PPV_ARGS(&record.override_pipeline_state_left));
            (void)record.device->CreateGraphicsPipelineState(&right_desc.desc, IID_PPV_ARGS(&record.override_pipeline_state_right));
        }
    }

    if (FAILED(hr) || replacement_pso == nullptr) {
        std::ostringstream ss{};
        ss << "Failed to create DX12 override PSO";
        if (!record.vertex_override_name.empty()) {
            ss << " VS=" << record.vertex_override_name;
        }
        if (!record.pixel_override_name.empty()) {
            ss << " PS=" << record.pixel_override_name;
        }
        if (!record.geometry_override_name.empty()) {
            ss << " GS=" << record.geometry_override_name;
        }
        if (!record.compute_override_name.empty()) {
            ss << " CS=" << record.compute_override_name;
        }
        if (!record.amplification_override_name.empty()) {
            ss << " AS=" << record.amplification_override_name;
        }
        if (!record.mesh_override_name.empty()) {
            ss << " MS=" << record.mesh_override_name;
        }
        ss << " (" << format_hresult(hr) << ")";

        record.last_error = ss.str();
        push_event(ss.str());
        spdlog::error("[ShaderOverrideRegistry] {}", ss.str());
        record.vertex_override_name.clear();
        record.pixel_override_name.clear();
        record.geometry_override_name.clear();
        record.compute_override_name.clear();
        record.amplification_override_name.clear();
        record.mesh_override_name.clear();
        return;
    }

    record.override_pipeline_state = replacement_pso;
    record.override_active = true;

    std::ostringstream ss{};
    ss << "Created DX12 override PSO for 0x" << std::hex << std::uppercase << record.pipeline_state_pointer;
    if (!record.vertex_override_name.empty()) {
        ss << " VS=" << record.vertex_override_name;
    }
    if (!record.pixel_override_name.empty()) {
        ss << " PS=" << record.pixel_override_name;
    }
    if (!record.geometry_override_name.empty()) {
        ss << " GS=" << record.geometry_override_name;
    }
    if (!record.compute_override_name.empty()) {
        ss << " CS=" << record.compute_override_name;
    }
    if (!record.amplification_override_name.empty()) {
        ss << " AS=" << record.amplification_override_name;
    }
    if (!record.mesh_override_name.empty()) {
        ss << " MS=" << record.mesh_override_name;
    }
    if (record.override_pipeline_state_left != nullptr || record.override_pipeline_state_right != nullptr) {
        ss << " per-eye-variants";
    }
    push_event(ss.str());
}

std::string ShaderOverrideRegistry::make_override_key(Backend backend, Stage stage, std::string_view target_hash) const {
    return backend_to_string(backend) + ":" + stage_to_string(stage) + ":" + normalize_hash(std::string{target_hash});
}

std::string ShaderOverrideRegistry::hash_shader_bytecode(const void* bytecode, size_t bytecode_size) const {
    if (bytecode == nullptr || bytecode_size == 0) {
        return {};
    }

    constexpr uint64_t fnv_offset = 1469598103934665603ull;
    constexpr uint64_t fnv_prime = 1099511628211ull;

    uint64_t hash = fnv_offset;
    const auto* bytes = static_cast<const uint8_t*>(bytecode);

    for (size_t i = 0; i < bytecode_size; ++i) {
        hash ^= bytes[i];
        hash *= fnv_prime;
    }

    std::ostringstream ss{};
    ss << std::hex << std::setfill('0') << std::setw(16) << std::nouppercase << hash;
    return normalize_hash(ss.str());
}

std::filesystem::path ShaderOverrideRegistry::global_override_dir() const {
    return Framework::get_persistent_dir().parent_path() / "shader_overrides";
}

std::filesystem::path ShaderOverrideRegistry::profile_override_dir() const {
    return Framework::get_persistent_dir("shader_overrides");
}

// =====================================================================
// Shader Hunter implementation
// =====================================================================
//
// Lets users interactively cycle through every PS hash bound during the
// last N frames, suppressing one at a time so they can see what each
// shader draws. Marked hashes are persisted to JSON manifests using
// CRC32 (the 8-hex-char form the registry treats as a ShaderToggler
// hash) for portability with ShaderToggler's own format.
void ShaderOverrideRegistry::hunter_ensure_discard_compiled_locked() {
    if (m_hunter_tried_compile_discard) return;
    m_hunter_tried_compile_discard = true;
    // Write a tiny discard PS source to the profile override dir
    // (an unobtrusive location that already exists) and compile via the
    // existing DXC pipeline.
    namespace fs = std::filesystem;
    fs::path dir = profile_override_dir();
    std::error_code ec{};
    fs::create_directories(dir, ec);
    fs::path src_path = dir / "_uevr_hunter_discard.hlsl";
    // Always rewrite so old variants are replaced. D3D12 rule: PS output
    // signature must have <= RT count of the PSO. An 8-output PS substituted
    // into a 1-RT PSO -> E_INVALIDARG. A 1-output PS substituted into a
    // 4-RT MRT PSO -> GPU TDR (writes nothing to slots 1..3, undefined).
    // The most-portable PS: ZERO outputs (void return + discard). D3D12
    // accepts a PS with no output signature for ANY RT count because the
    // discard kills the pixel before any write would occur.
    {
        std::ofstream out{src_path, std::ios::binary | std::ios::trunc};
        out << "// auto-generated by UEVR Shader Hunter — discards every fragment.\n";
        out << "// Zero output signature so it's valid for PSOs with any RT count.\n";
        out << "void main() { discard; }\n";
    }
    ShaderCompileRequest req{};
    req.source_path = src_path;
    req.entry_point = "main";
    req.profile = "ps_6_0";
    req.preferred_backend = ShaderCompilerBackend::Dxc;
    auto result = compile_shader_file(req);
    if (result.succeeded) {
        m_hunter_discard_ps = std::move(result.bytecode);
        spdlog::info("[ShaderHunter] discard PS compiled, {} bytes", m_hunter_discard_ps.size());
    } else {
        spdlog::error("[ShaderHunter] discard PS compile failed: {}", result.error);
    }
}

void ShaderOverrideRegistry::hunter_record_bind_locked(const D3D12GraphicsPsoRecord& record) {
    if (!m_hunter_active.load(std::memory_order_relaxed)) return;
    const bool compute_only = record.pixel_hash.empty() && !record.compute_hash.empty();
    if (compute_only && !shader_hunter_collect_compute_events_enabled()) return;
    const auto& hunted_hash = compute_only ? record.compute_hash : record.pixel_hash;
    if (hunted_hash.empty()) return;

    const bool window_expired = m_hunter_frame_window > 0 &&
        m_frame >= m_hunter_window_start_frame + static_cast<uint64_t>(m_hunter_frame_window);
    if (window_expired && !m_hunter_window_stopped.load(std::memory_order_relaxed)) {
        m_hunter_window_stopped.store(true, std::memory_order_relaxed);
        spdlog::info("[ShaderHunter] frame window expired ({} frames). Collection paused; {} hashes captured.",
            m_hunter_frame_window, m_hunter_collected.size());
    }
    const bool collection_paused = m_hunter_window_stopped.load(std::memory_order_relaxed) || window_expired;

    auto it = m_hunter_collected.find(hunted_hash);
    if (it == m_hunter_collected.end()) {
        // After the frame window expires, keep live/hit data fresh for captured
        // hashes, but do not let new hashes shift the list being hunted.
        if (collection_paused) {
            return;
        }

        auto [inserted_it, inserted] = m_hunter_collected.emplace(hunted_hash, HunterCollectedEntry{});
        it = inserted_it;
        it->second.first_seen_frame = m_frame;
        it->second.last_seen_frame = m_frame;
        m_hunter_order.push_back(hunted_hash);
    }

    auto& e = it->second;
    e.crc32 = compute_only ? record.compute_crc32 : record.pixel_crc32;
    e.vs_hash = compute_only ? std::string{"CS"} : (!record.vertex_hash.empty() ? record.vertex_hash : record.mesh_hash);
    e.ps_size = compute_only ? record.owned_stream.compute_shader.size() : record.owned_stream.pixel_shader.size();
    if (!compute_only && e.ps_size == 0 && record.owned_desc.pixel_shader.size() > 0) {
        e.ps_size = record.owned_desc.pixel_shader.size();
    }
    e.last_seen_frame = m_frame;
    e.hits += 1;
    e.stage = compute_only ? HunterStage::Compute : HunterStage::Pixel;

    // === Vertex-stage walk tracking ===
    // VS hashes get their own map so the VS walk (hotkeys 4/5/6) iterates
    // unique vertex shaders only. We also record the most-recently-seen
    // companion PS hash and PSO bytecode size for context in the UI.
    if (!compute_only && !record.vertex_hash.empty()) {
        auto vs_it = m_hunter_collected_vs.find(record.vertex_hash);
        if (vs_it == m_hunter_collected_vs.end() && !collection_paused) {
            auto [inserted, _] = m_hunter_collected_vs.emplace(record.vertex_hash, HunterCollectedEntry{});
            vs_it = inserted;
            vs_it->second.first_seen_frame = m_frame;
            vs_it->second.last_seen_frame = m_frame;
            vs_it->second.stage = HunterStage::Vertex;
            m_hunter_order_vs.push_back(record.vertex_hash);
        }
        if (vs_it != m_hunter_collected_vs.end()) {
            vs_it->second.vs_hash = record.pixel_hash; // companion PS hash for context
            vs_it->second.crc32 = record.vertex_crc32;
            vs_it->second.ps_size = record.owned_stream.vertex_shader.size();
            if (vs_it->second.ps_size == 0 && record.owned_desc.vertex_shader.size() > 0) {
                vs_it->second.ps_size = record.owned_desc.vertex_shader.size();
            }
            vs_it->second.last_seen_frame = m_frame;
            vs_it->second.hits += 1;
        }
    }

    // === Compute-stage walk tracking ===
    // For compute_only PSOs the main map already holds the entry but we
    // mirror it into a CS-only map so the UI can iterate just compute shaders.
    if (compute_only) {
        auto cs_it = m_hunter_collected_cs.find(record.compute_hash);
        if (cs_it == m_hunter_collected_cs.end() && !collection_paused) {
            auto [inserted, _] = m_hunter_collected_cs.emplace(record.compute_hash, HunterCollectedEntry{});
            cs_it = inserted;
            cs_it->second.first_seen_frame = m_frame;
            cs_it->second.last_seen_frame = m_frame;
            cs_it->second.stage = HunterStage::Compute;
            m_hunter_order_cs.push_back(record.compute_hash);
        }
        if (cs_it != m_hunter_collected_cs.end()) {
            cs_it->second.crc32 = record.compute_crc32;
            cs_it->second.ps_size = record.owned_stream.compute_shader.size();
            cs_it->second.last_seen_frame = m_frame;
            cs_it->second.hits += 1;
        }
    }
}

void ShaderOverrideRegistry::hunter_record_draw_event(uintptr_t pso_pointer, int eye_bucket, bool compute, bool indexed, bool indirect) {
    if (!m_hunter_active.load(std::memory_order_relaxed)) return;
    if (pso_pointer == 0) return;
    if (compute && !shader_hunter_collect_compute_events_enabled()) return;
    if (indirect && !shader_hunter_collect_indirect_events_enabled()) return;
    if (shader_hunter_freeze_after_window_enabled() &&
            m_hunter_window_stopped.load(std::memory_order_relaxed)) {
        return;
    }
    if (eye_bucket < 0 || eye_bucket > 4) eye_bucket = 0;

    std::scoped_lock _{m_mutex};
    const auto rec_it = m_d3d12_graphics_pso_records.find(pso_pointer);
    if (rec_it == m_d3d12_graphics_pso_records.end()) return;

    const auto& record = rec_it->second;
    const bool compute_only = compute || (record.pixel_hash.empty() && !record.compute_hash.empty());
    if (compute_only && !shader_hunter_collect_compute_events_enabled()) return;
    const std::string& hunted_hash = compute_only ? record.compute_hash : record.pixel_hash;
    if (hunted_hash.empty()) return;

    // Keep the original bind-based list populated for compatibility when a
    // draw reaches us before the SetPipelineState collection path did.
    const bool needs_primary_entry = m_hunter_collected.find(hunted_hash) == m_hunter_collected.end();
    const bool needs_vs_entry = !compute_only && !record.vertex_hash.empty() &&
        m_hunter_collected_vs.find(record.vertex_hash) == m_hunter_collected_vs.end();
    const bool needs_cs_entry = compute_only && !record.compute_hash.empty() &&
        m_hunter_collected_cs.find(record.compute_hash) == m_hunter_collected_cs.end();
    if (needs_primary_entry || needs_vs_entry || needs_cs_entry) {
        hunter_record_bind_locked(record);
    }

    auto update_common = [&](HunterCollectedEntry& e, HunterStage stage, const std::string& companion_hash) {
        e.stage = stage;
        e.last_draw_frame = m_frame;
        e.last_seen_frame = m_frame;
        e.last_pso = pso_pointer;
        e.eye_hits[static_cast<size_t>(eye_bucket)] += 1;
        if (compute_only) {
            e.dispatch_hits += 1;
        } else {
            e.draw_hits += 1;
            if (indexed) {
                e.indexed_draw_hits += 1;
            }
        }

        if (!companion_hash.empty()) {
            e.vs_hash = companion_hash;
        }

        const auto bind_context = D3D12Diagnostics::get().current_bind_context();
        if (bind_context.has_value()) {
            const auto render_target_name = join_target_names(bind_context->render_targets);
            const auto render_target_key = join_target_keys(bind_context->render_targets);
            const auto depth_target_name = bind_context->depth_target.has_value()
                ? bind_context->depth_target->name
                : std::string{};
            const auto depth_target_key = bind_context->depth_target.has_value()
                ? format_pointer_to_hex(bind_context->depth_target->handle)
                : std::string{};
            if (!render_target_name.empty()) {
                e.last_render_targets = render_target_name;
                e.last_render_target_key = render_target_key;
            }
            if (!depth_target_name.empty() || !depth_target_key.empty()) {
                e.last_depth_target = depth_target_name.empty() ? depth_target_key : depth_target_name;
                e.last_depth_target_key = depth_target_key;
            }
        }
    };

    auto it = m_hunter_collected.find(hunted_hash);
    if (it != m_hunter_collected.end()) {
        it->second.crc32 = compute_only ? record.compute_crc32 : record.pixel_crc32;
        update_common(it->second, compute_only ? HunterStage::Compute : HunterStage::Pixel,
            compute_only ? std::string{"CS"} : (!record.vertex_hash.empty() ? record.vertex_hash : record.mesh_hash));
    }

    if (!compute_only && !record.vertex_hash.empty()) {
        auto vs_it = m_hunter_collected_vs.find(record.vertex_hash);
        if (vs_it != m_hunter_collected_vs.end()) {
            vs_it->second.crc32 = record.vertex_crc32;
            update_common(vs_it->second, HunterStage::Vertex, record.pixel_hash);
        }
    }

    if (compute_only && !record.compute_hash.empty()) {
        auto cs_it = m_hunter_collected_cs.find(record.compute_hash);
        if (cs_it != m_hunter_collected_cs.end()) {
            cs_it->second.crc32 = record.compute_crc32;
            update_common(cs_it->second, HunterStage::Compute, std::string{"CS"});
        }
    }
}

bool ShaderOverrideRegistry::hunter_should_suppress_locked(const D3D12GraphicsPsoRecord& record) const {
    const bool compute_only = record.pixel_hash.empty() && !record.compute_hash.empty();
    const auto& hunted_hash = compute_only ? record.compute_hash : record.pixel_hash;
    if (hunted_hash.empty()) return false;
    if (shader_hunter_suppression_blocklist().count(hunted_hash) > 0) return false;
    if (compute_only && !shader_hunter_compute_suppression_enabled()) return false;
    if (!hunter_record_is_safe_suppression_candidate_locked(record)) return false;
    // Runtime blocklist (UI-flagged crashy hashes for this session).
    if (m_hunter_runtime_blocklist.count(record.pixel_hash) > 0) return false;
    if (!record.vertex_hash.empty() && m_hunter_runtime_blocklist.count(record.vertex_hash) > 0) return false;
    if (!record.compute_hash.empty() && m_hunter_runtime_blocklist.count(record.compute_hash) > 0) return false;
    // === Env-driven GLOBAL suppress (headless "Suppress active") ===
    // Ungated: no overlay, no hunting_active, no eye_bucket. Matches PS/VS/CS
    // hash or PS CRC32. This is what reproduces the user's manual hunter
    // suppression of e.g. bb7b1616d81d6bb3 / 1f958d46 (SkyAtmosphere over-draw).
    {
        const auto& global_suppress = shader_hunter_global_suppress_set();
        if (!global_suppress.empty()) {
            if (!record.pixel_hash.empty() && global_suppress.count(record.pixel_hash) > 0) return true;
            if (!record.vertex_hash.empty() && global_suppress.count(record.vertex_hash) > 0) return true;
            if (!record.compute_hash.empty() && global_suppress.count(record.compute_hash) > 0) return true;
            if (record.pixel_crc32 != 0) {
                char crc_str[16]{};
                std::snprintf(crc_str, sizeof(crc_str), "%08x", record.pixel_crc32);
                if (global_suppress.count(std::string{crc_str}) > 0) return true;
            }
        }
    }
    const bool suppress_active = m_hunter_suppression_enabled.load(std::memory_order_relaxed);
    const bool hunting_active = m_hunter_active.load(std::memory_order_relaxed);
    const bool hide_marked = m_hunter_hide_marked.load(std::memory_order_relaxed);
    // When cycle-highlight mode is on, the active hash gets a magenta PSO
    // substitution (via the highlight path in update_d3d12_override_pipeline_state)
    // instead of being skipped. Don't double-action by also skipping.
    const bool highlight_mode = m_hunter_cycle_highlight_mode.load(std::memory_order_relaxed);
    // === Pixel stage ===
    if (suppress_active && hunting_active && hunted_hash == m_hunter_active_hash && !highlight_mode) return true;
    if (hide_marked && m_hunter_marked.count(hunted_hash) > 0) return true;
    // === Vertex stage ===
    if (!compute_only && !record.vertex_hash.empty()) {
        if (suppress_active && hunting_active && record.vertex_hash == m_hunter_active_hash_vs && !highlight_mode) return true;
        if (hide_marked && m_hunter_marked_vs.count(record.vertex_hash) > 0) return true;
    }
    // === Compute stage ===
    if (compute_only) {
        if (suppress_active && hunting_active && record.compute_hash == m_hunter_active_hash_cs && !highlight_mode) return true;
        if (hide_marked && m_hunter_marked_cs.count(record.compute_hash) > 0) return true;
    }
    return false;
}

bool ShaderOverrideRegistry::hunter_record_is_safe_suppression_candidate_locked(const D3D12GraphicsPsoRecord& record) const {
    if (record.pixel_hash.empty() && !record.compute_hash.empty()) {
        return record.owned_stream.compute_shader.size() >= HUNTER_MIN_SCENE_PS_SIZE;
    }

    size_t ps_size = record.owned_stream.pixel_shader.size();
    if (ps_size == 0 && record.owned_desc.pixel_shader.size() > 0) {
        ps_size = record.owned_desc.pixel_shader.size();
    }

    return (!record.vertex_hash.empty() || !record.mesh_hash.empty()) && ps_size >= HUNTER_MIN_SCENE_PS_SIZE;
}

bool ShaderOverrideRegistry::hunter_entry_is_scene_candidate_locked(const HunterCollectedEntry& entry) const {
    const bool has_actual_work = (entry.draw_hits + entry.dispatch_hits) > 0;
    if (!has_actual_work) {
        return false;
    }

    if (entry.stage == HunterStage::Vertex) {
        return !entry.vs_hash.empty();
    }

    if (entry.ps_size < HUNTER_MIN_SCENE_PS_SIZE) {
        return false;
    }

    if (entry.stage == HunterStage::Compute) {
        return true;
    }

    return !entry.vs_hash.empty();
}

void ShaderOverrideRegistry::hunter_rebuild_active_locked() {
    auto clear_active = [&]() {
        const std::string old_hash = m_hunter_active_hash;
        m_hunter_active_hash.clear();
        m_hunter_active_index = -1;
        if (!old_hash.empty()) {
            for (auto& [ptr, rec] : m_d3d12_graphics_pso_records) {
                if (rec.pixel_hash == old_hash || rec.compute_hash == old_hash) {
                    rec.applied_override_revision = (std::numeric_limits<uint64_t>::max)();
                }
            }
        }
        std::scoped_lock skip_lock{m_hunter_skip_mutex};
        m_hunter_skip_by_cmdlist.clear();
    };

    if (m_hunter_order.empty()) {
        clear_active();
        return;
    }
    if (m_hunter_active_index < 0) m_hunter_active_index = 0;
    if (m_hunter_active_index >= static_cast<int>(m_hunter_order.size())) {
        m_hunter_active_index = static_cast<int>(m_hunter_order.size()) - 1;
    }
    auto is_pixel_index = [&](int idx) {
        if (idx < 0 || idx >= static_cast<int>(m_hunter_order.size())) {
            return false;
        }
        const auto it = m_hunter_collected.find(m_hunter_order[static_cast<size_t>(idx)]);
        return it != m_hunter_collected.end() && it->second.stage != HunterStage::Compute;
    };
    if (!is_pixel_index(m_hunter_active_index)) {
        m_hunter_active_index = -1;
        for (int i = 0; i < static_cast<int>(m_hunter_order.size()); ++i) {
            if (is_pixel_index(i)) {
                m_hunter_active_index = i;
                break;
            }
        }
        if (m_hunter_active_index < 0) {
            clear_active();
            return;
        }
    }
    const std::string new_hash = m_hunter_order[static_cast<size_t>(m_hunter_active_index)];
    const std::string old_hash = m_hunter_active_hash;
    if (new_hash == old_hash) return;
    m_hunter_active_hash = new_hash;
    // Only invalidate records whose pixel_hash matches the OLD hunted hash
    // (need to UN-suppress) or the NEW hunted hash (need to suppress).
    // Avoids bumping the global revision counter which would re-evaluate
    // every tracked PSO and trigger driver pressure / GPU TDR.
    for (auto& [ptr, rec] : m_d3d12_graphics_pso_records) {
        if ((!old_hash.empty() && (rec.pixel_hash == old_hash || rec.compute_hash == old_hash)) ||
                (!new_hash.empty() && (rec.pixel_hash == new_hash || rec.compute_hash == new_hash))) {
            rec.applied_override_revision = (std::numeric_limits<uint64_t>::max)();
        }
    }
    {
        std::scoped_lock skip_lock{m_hunter_skip_mutex};
        m_hunter_skip_by_cmdlist.clear();
    }
}

void ShaderOverrideRegistry::hunter_rebuild_stage_active_locked(HunterStage stage) {
    if (stage == HunterStage::Pixel) {
        hunter_rebuild_active_locked();
        return;
    }

    auto& order = (stage == HunterStage::Vertex) ? m_hunter_order_vs : m_hunter_order_cs;
    auto& idx = (stage == HunterStage::Vertex) ? m_hunter_active_index_vs : m_hunter_active_index_cs;
    auto& hash = (stage == HunterStage::Vertex) ? m_hunter_active_hash_vs : m_hunter_active_hash_cs;
    const std::string old_hash = hash;

    if (order.empty()) {
        idx = -1;
        hash.clear();
    } else {
        if (idx < 0) idx = 0;
        if (idx >= static_cast<int>(order.size())) {
            idx = static_cast<int>(order.size()) - 1;
        }
        hash = order[static_cast<size_t>(idx)];
    }

    const std::string new_hash = hash;
    if (old_hash == new_hash) {
        return;
    }

    for (auto& [ptr, rec] : m_d3d12_graphics_pso_records) {
        const auto& h = (stage == HunterStage::Vertex) ? rec.vertex_hash : rec.compute_hash;
        if ((!old_hash.empty() && h == old_hash) || (!new_hash.empty() && h == new_hash)) {
            rec.applied_override_revision = (std::numeric_limits<uint64_t>::max)();
        }
    }
    {
        std::scoped_lock skip_lock{m_hunter_skip_mutex};
        m_hunter_skip_by_cmdlist.clear();
    }
}

void ShaderOverrideRegistry::hunter_start() {
    std::scoped_lock _{m_mutex};
    const int env_frame_window = shader_hunter_env_frame_window();
    if (env_frame_window >= 0) {
        m_hunter_frame_window = env_frame_window;
    }
    m_hunter_active = true;
    m_hunter_collected.clear();
    m_hunter_collected_vs.clear();
    m_hunter_collected_cs.clear();
    m_hunter_order.clear();
    m_hunter_order_vs.clear();
    m_hunter_order_cs.clear();
    m_hunter_active_index = -1;
    m_hunter_active_hash.clear();
    m_hunter_active_index_vs = -1;
    m_hunter_active_hash_vs.clear();
    m_hunter_active_index_cs = -1;
    m_hunter_active_hash_cs.clear();
    m_hunter_window_start_frame = m_frame;
    m_hunter_window_stopped.store(false, std::memory_order_relaxed);
    {
        std::scoped_lock skip_lock{m_hunter_skip_mutex};
        m_hunter_skip_by_cmdlist.clear();
    }
    push_event("Shader Hunter: started");
    spdlog::info("[ShaderHunter] started (frame_window={} collect_compute={} collect_indirect={} freeze_after_window={})",
        m_hunter_frame_window,
        shader_hunter_collect_compute_events_enabled() ? 1 : 0,
        shader_hunter_collect_indirect_events_enabled() ? 1 : 0,
        shader_hunter_freeze_after_window_enabled() ? 1 : 0);
}

void ShaderOverrideRegistry::hunter_stop() {
    std::scoped_lock _{m_mutex};
    m_hunter_active = false;
    m_hunter_active_index = -1;
    m_hunter_active_hash.clear();
    m_hunter_active_index_vs = -1;
    m_hunter_active_hash_vs.clear();
    m_hunter_active_index_cs = -1;
    m_hunter_active_hash_cs.clear();
    {
        std::scoped_lock skip_lock{m_hunter_skip_mutex};
        m_hunter_skip_by_cmdlist.clear();
    }
    ++m_override_revision;
    refresh_active_override_flags_locked();
    push_event("Shader Hunter: stopped");
    spdlog::info("[ShaderHunter] stopped");
}

void ShaderOverrideRegistry::hunter_clear_collected() {
    std::scoped_lock _{m_mutex};
    m_hunter_collected.clear();
    m_hunter_collected_vs.clear();
    m_hunter_collected_cs.clear();
    m_hunter_order.clear();
    m_hunter_order_vs.clear();
    m_hunter_order_cs.clear();
    m_hunter_active_index = -1;
    m_hunter_active_hash.clear();
    m_hunter_active_index_vs = -1;
    m_hunter_active_hash_vs.clear();
    m_hunter_active_index_cs = -1;
    m_hunter_active_hash_cs.clear();
    // Reset frame window so a fresh collection starts from now.
    m_hunter_window_start_frame = m_frame;
    m_hunter_window_stopped.store(false, std::memory_order_relaxed);
    {
        std::scoped_lock skip_lock{m_hunter_skip_mutex};
        m_hunter_skip_by_cmdlist.clear();
    }
    ++m_override_revision;
    push_event("Shader Hunter: collection cleared");
    spdlog::info("[ShaderHunter] cleared collected hashes; collection {}",
        m_hunter_active.load(std::memory_order_relaxed) ? "restarted" : "idle");
}

void ShaderOverrideRegistry::hunter_set_frame_window(int frames) {
    std::scoped_lock _{m_mutex};
    m_hunter_frame_window = frames < 0 ? 0 : frames;
    // Reset the start point so a newly-set window starts counting from now.
    m_hunter_window_start_frame = m_frame;
    m_hunter_window_stopped.store(false, std::memory_order_relaxed);
}

void ShaderOverrideRegistry::hunter_set_recent_frame_age(int frames) {
    std::scoped_lock _{m_mutex};
    m_hunter_recent_frame_age = frames < 0 ? HUNTER_DEFAULT_RECENT_FRAME_AGE : frames;
}

void ShaderOverrideRegistry::hunter_step(int delta) {
    std::scoped_lock _{m_mutex};
    if (m_hunter_order.empty()) {
        hunter_rebuild_active_locked();
        return;
    }

    std::vector<int> scene_live_candidates{};
    std::vector<int> live_candidates{};
    std::vector<int> scene_candidates{};
    std::vector<int> all_candidates{};
    scene_live_candidates.reserve(m_hunter_order.size());
    live_candidates.reserve(m_hunter_order.size());
    scene_candidates.reserve(m_hunter_order.size());
    all_candidates.reserve(m_hunter_order.size());

    for (int i = 0; i < static_cast<int>(m_hunter_order.size()); ++i) {
        const auto it = m_hunter_collected.find(m_hunter_order[static_cast<size_t>(i)]);
        if (it == m_hunter_collected.end()) {
            continue;
        }
        if (it->second.stage == HunterStage::Compute) {
            continue;
        }

        all_candidates.push_back(i);
        const bool scene_candidate = hunter_entry_is_scene_candidate_locked(it->second);
        if (scene_candidate) {
            scene_candidates.push_back(i);
        }

        const uint64_t activity_frame = it->second.last_draw_frame != 0
            ? it->second.last_draw_frame
            : it->second.last_seen_frame;
        const auto age = m_frame >= activity_frame
            ? (m_frame - activity_frame)
            : 0;
        if ((it->second.draw_hits + it->second.dispatch_hits) > 0 &&
                age <= static_cast<uint64_t>(m_hunter_recent_frame_age)) {
            live_candidates.push_back(i);
            if (scene_candidate) {
                scene_live_candidates.push_back(i);
            }
        }
    }

    const auto live_candidate_count = live_candidates.size();
    const auto scene_live_candidate_count = scene_live_candidates.size();
    const auto* candidates = &scene_live_candidates;
    if (candidates->empty() && !scene_candidates.empty()) {
        candidates = &scene_candidates;
    }
    if (candidates->empty() && !live_candidates.empty()) {
        candidates = &live_candidates;
    }
    if (candidates->empty()) {
        candidates = &all_candidates;
    }

    int current_pos = -1;
    for (int i = 0; i < static_cast<int>(candidates->size()); ++i) {
        if ((*candidates)[static_cast<size_t>(i)] == m_hunter_active_index) {
            current_pos = i;
            break;
        }
    }

    const int n = static_cast<int>(candidates->size());
    if (n == 0) {
        hunter_rebuild_active_locked();
        return;
    }
    int next_pos = current_pos + delta;
    if (current_pos < 0) {
        next_pos = delta < 0 ? (n - 1) : 0;
    }
    while (next_pos < 0) next_pos += n;
    next_pos = next_pos % n;
    m_hunter_active_index = (*candidates)[static_cast<size_t>(next_pos)];
    hunter_rebuild_active_locked();
    spdlog::info("[ShaderHunter] hunting idx={} hash={} (total={} scene_live_candidates={} live_candidates={})",
        m_hunter_active_index, m_hunter_active_hash, m_hunter_order.size(),
        scene_live_candidate_count, live_candidate_count);
}

void ShaderOverrideRegistry::hunter_set_index(int index) {
    std::scoped_lock _{m_mutex};
    if (m_hunter_order.empty()) {
        hunter_rebuild_active_locked();
        return;
    }

    std::vector<int> pixel_indices{};
    pixel_indices.reserve(m_hunter_order.size());
    for (int i = 0; i < static_cast<int>(m_hunter_order.size()); ++i) {
        const auto it = m_hunter_collected.find(m_hunter_order[static_cast<size_t>(i)]);
        if (it == m_hunter_collected.end() || it->second.stage == HunterStage::Compute) {
            continue;
        }
        pixel_indices.push_back(i);
    }
    if (pixel_indices.empty()) {
        m_hunter_active_index = -1;
        hunter_rebuild_active_locked();
        return;
    }

    const int n = static_cast<int>(pixel_indices.size());
    if (index < 0) index = 0;
    if (index >= n) index = n - 1;
    m_hunter_active_index = pixel_indices[static_cast<size_t>(index)];
    hunter_rebuild_active_locked();
}

// === Stage-aware overloads (new) ===
// Pixel stage dispatches to the existing single-stage impls (which contain
// the more sophisticated scene-candidate / live-candidate fallback logic).
// VS and CS get simpler implementations that just step through the per-stage
// order vector.
void ShaderOverrideRegistry::hunter_step(HunterStage stage, int delta) {
    if (stage == HunterStage::Pixel) { hunter_step(delta); return; }
    std::scoped_lock _{m_mutex};
    auto& order = (stage == HunterStage::Vertex) ? m_hunter_order_vs : m_hunter_order_cs;
    auto& collected = (stage == HunterStage::Vertex) ? m_hunter_collected_vs : m_hunter_collected_cs;
    auto& idx = (stage == HunterStage::Vertex) ? m_hunter_active_index_vs : m_hunter_active_index_cs;
    auto& hash = (stage == HunterStage::Vertex) ? m_hunter_active_hash_vs : m_hunter_active_hash_cs;
    if (order.empty()) {
        hunter_rebuild_stage_active_locked(stage);
        return;
    }

    std::vector<int> scene_live_candidates{};
    std::vector<int> live_candidates{};
    std::vector<int> scene_candidates{};
    std::vector<int> all_candidates{};
    scene_live_candidates.reserve(order.size());
    live_candidates.reserve(order.size());
    scene_candidates.reserve(order.size());
    all_candidates.reserve(order.size());

    for (int i = 0; i < static_cast<int>(order.size()); ++i) {
        const auto it = collected.find(order[static_cast<size_t>(i)]);
        if (it == collected.end()) {
            continue;
        }

        all_candidates.push_back(i);
        const bool scene_candidate = hunter_entry_is_scene_candidate_locked(it->second);
        if (scene_candidate) {
            scene_candidates.push_back(i);
        }

        const uint64_t activity_frame = it->second.last_draw_frame != 0
            ? it->second.last_draw_frame
            : it->second.last_seen_frame;
        const auto age = m_frame >= activity_frame
            ? (m_frame - activity_frame)
            : 0;
        if ((it->second.draw_hits + it->second.dispatch_hits) > 0 &&
                age <= static_cast<uint64_t>(m_hunter_recent_frame_age)) {
            live_candidates.push_back(i);
            if (scene_candidate) {
                scene_live_candidates.push_back(i);
            }
        }
    }

    const auto* candidates = &scene_live_candidates;
    if (candidates->empty() && !scene_candidates.empty()) {
        candidates = &scene_candidates;
    }
    if (candidates->empty() && !live_candidates.empty()) {
        candidates = &live_candidates;
    }
    if (candidates->empty()) {
        candidates = &all_candidates;
    }

    const int n = static_cast<int>(candidates->size());
    if (n == 0) {
        hunter_rebuild_stage_active_locked(stage);
        return;
    }

    int current_pos = -1;
    for (int i = 0; i < n; ++i) {
        if ((*candidates)[static_cast<size_t>(i)] == idx) {
            current_pos = i;
            break;
        }
    }

    int next = current_pos + delta;
    if (current_pos < 0) next = (delta < 0) ? (n - 1) : 0;
    while (next < 0) next += n;
    next = next % n;
    idx = (*candidates)[static_cast<size_t>(next)];
    hunter_rebuild_stage_active_locked(stage);
    spdlog::info("[ShaderHunter] hunting stage={} idx={} hash={} (total={})",
        static_cast<int>(stage), idx, hash, n);
}

void ShaderOverrideRegistry::hunter_set_index(HunterStage stage, int index) {
    if (stage == HunterStage::Pixel) { hunter_set_index(index); return; }
    std::scoped_lock _{m_mutex};
    auto& order = (stage == HunterStage::Vertex) ? m_hunter_order_vs : m_hunter_order_cs;
    auto& idx = (stage == HunterStage::Vertex) ? m_hunter_active_index_vs : m_hunter_active_index_cs;
    if (order.empty()) {
        hunter_rebuild_stage_active_locked(stage);
        return;
    }
    const int n = static_cast<int>(order.size());
    if (index < 0) index = 0;
    if (index >= n) index = n - 1;
    idx = index;
    hunter_rebuild_stage_active_locked(stage);
}

void ShaderOverrideRegistry::hunter_toggle_mark_active(HunterStage stage) {
    if (stage == HunterStage::Pixel) { hunter_toggle_mark_active(); return; }
    std::scoped_lock _{m_mutex};
    auto& hash = (stage == HunterStage::Vertex) ? m_hunter_active_hash_vs : m_hunter_active_hash_cs;
    auto& marked = (stage == HunterStage::Vertex) ? m_hunter_marked_vs : m_hunter_marked_cs;
    if (hash.empty()) return;
    if (marked.count(hash) > 0) {
        marked.erase(hash);
        spdlog::info("[ShaderHunter] unmarked stage={} {}", static_cast<int>(stage), hash);
    } else {
        marked.insert(hash);
        spdlog::info("[ShaderHunter] marked stage={} {}", static_cast<int>(stage), hash);
    }
    for (auto& [ptr, rec] : m_d3d12_graphics_pso_records) {
        const auto& h = (stage == HunterStage::Vertex) ? rec.vertex_hash : rec.compute_hash;
        if (h == hash) {
            rec.applied_override_revision = (std::numeric_limits<uint64_t>::max)();
        }
    }
    {
        std::scoped_lock skip_lock{m_hunter_skip_mutex};
        m_hunter_skip_by_cmdlist.clear();
    }
}

void ShaderOverrideRegistry::hunter_toggle_mark_active() {
    std::scoped_lock _{m_mutex};
    if (m_hunter_active_hash.empty()) return;
    if (m_hunter_marked.count(m_hunter_active_hash) > 0) {
        m_hunter_marked.erase(m_hunter_active_hash);
        spdlog::info("[ShaderHunter] unmarked {}", m_hunter_active_hash);
    } else {
        m_hunter_marked.insert(m_hunter_active_hash);
        spdlog::info("[ShaderHunter] marked {}", m_hunter_active_hash);
    }
    // Only invalidate records with this PS hash (avoid global revision bump
    // which would force every tracked PSO to re-create on next bind).
    for (auto& [ptr, rec] : m_d3d12_graphics_pso_records) {
        if (rec.pixel_hash == m_hunter_active_hash || rec.compute_hash == m_hunter_active_hash) {
            rec.applied_override_revision = (std::numeric_limits<uint64_t>::max)();
        }
    }
    {
        std::scoped_lock skip_lock{m_hunter_skip_mutex};
        m_hunter_skip_by_cmdlist.clear();
    }
}

void ShaderOverrideRegistry::hunter_toggle_mark_hash(std::string_view hash) {
    hunter_toggle_mark_hash(HunterStage::Pixel, hash);
}

void ShaderOverrideRegistry::hunter_toggle_mark_hash(HunterStage stage, std::string_view hash) {
    std::scoped_lock _{m_mutex};
    std::string h{hash};
    if (h.empty()) return;
    auto& marked = stage == HunterStage::Vertex
        ? m_hunter_marked_vs
        : (stage == HunterStage::Compute ? m_hunter_marked_cs : m_hunter_marked);
    if (marked.count(h) > 0) marked.erase(h);
    else marked.insert(h);
    for (auto& [ptr, rec] : m_d3d12_graphics_pso_records) {
        const bool touched =
            (stage == HunterStage::Pixel && (rec.pixel_hash == h || rec.compute_hash == h)) ||
            (stage == HunterStage::Vertex && rec.vertex_hash == h) ||
            (stage == HunterStage::Compute && rec.compute_hash == h);
        if (touched) {
            rec.applied_override_revision = (std::numeric_limits<uint64_t>::max)();
        }
    }
    {
        std::scoped_lock skip_lock{m_hunter_skip_mutex};
        m_hunter_skip_by_cmdlist.clear();
    }
}

void ShaderOverrideRegistry::hunter_add_runtime_blocklist(std::string_view hash) {
    std::scoped_lock _{m_mutex};
    std::string h{hash};
    if (h.empty()) return;
    m_hunter_runtime_blocklist.insert(h);
    spdlog::info("[ShaderHunter] runtime blocklist += {} (now {} entries)",
        h, m_hunter_runtime_blocklist.size());
    // Invalidate any records this hash might match so they recompute and
    // un-suppress next bind.
    for (auto& [ptr, rec] : m_d3d12_graphics_pso_records) {
        if (rec.pixel_hash == h || rec.vertex_hash == h || rec.compute_hash == h) {
            rec.applied_override_revision = (std::numeric_limits<uint64_t>::max)();
        }
    }
    {
        std::scoped_lock skip_lock{m_hunter_skip_mutex};
        m_hunter_skip_by_cmdlist.clear();
    }
}

// === Eye-Diff Tracker implementation ===
void ShaderOverrideRegistry::eyediff_set_enabled(bool v) {
    m_eyediff_enabled.store(v, std::memory_order_relaxed);
    spdlog::info("[EyeDiff] enabled={}", v ? 1 : 0);
}

void ShaderOverrideRegistry::eyediff_clear() {
    std::scoped_lock _{m_eyediff_mutex};
    m_eyediff_by_pshash.clear();
}

std::pair<std::string, std::string> ShaderOverrideRegistry::snapshot_pso_hashes_for(uintptr_t pso_pointer) const {
    std::scoped_lock _{m_mutex};
    auto it = m_d3d12_graphics_pso_records.find(pso_pointer);
    if (it == m_d3d12_graphics_pso_records.end()) return {std::string{}, std::string{}};
    return {it->second.pixel_hash, it->second.vertex_hash};
}

// Raw per-draw log gated on UEVR_EYE_DIFF_LOG=1. Heavy — emits ~thousands of
// lines per frame. Use only briefly when collecting diff data for grep+awk.
static bool eyediff_log_enabled() {
    static const bool enabled = []() {
        char buf[8]{};
        return GetEnvironmentVariableA("UEVR_EYE_DIFF_LOG", buf, sizeof(buf)) > 0 && buf[0] == '1';
    }();
    return enabled;
}

void ShaderOverrideRegistry::eyediff_record_draw(const std::string& ps_hash, const std::string& vs_hash,
                                                  int eye_bucket, uint64_t rtv0_handle, uint64_t desc_table0_handle) {
    if (!m_eyediff_enabled.load(std::memory_order_relaxed)) return;
    if (ps_hash.empty()) return;
    if (eye_bucket < 0 || eye_bucket > 4) eye_bucket = 0;
    if (eyediff_log_enabled()) {
        static const char* k_bucket[] = {"U", "L", "R", "F", "M"};
        spdlog::info("[EyeDiff] eye={} ps={} vs={} rtv=0x{:x} desc0=0x{:x}",
            k_bucket[eye_bucket], ps_hash, vs_hash, rtv0_handle, desc_table0_handle);
    }
    std::scoped_lock _{m_eyediff_mutex};
    auto& rec = m_eyediff_by_pshash[ps_hash];
    if (rec.first_seen_frame == 0) rec.first_seen_frame = m_frame;
    rec.last_seen_frame = m_frame;
    rec.ps_hash = ps_hash;
    rec.vs_hash = vs_hash;
    rec.bind_count_per_eye[eye_bucket] += 1;
    // Update last-seen RTV / descriptor handle for this eye bucket.
    if (rtv0_handle != 0) rec.last_rtv_handle_per_eye[eye_bucket] = rtv0_handle;
    if (desc_table0_handle != 0) rec.last_descriptor_table0_per_eye[eye_bucket] = desc_table0_handle;
    // Divergence flags: only meaningful when BOTH left (1) and right (2) eyes
    // have at least one binding and the most-recent handles differ.
    if (rec.bind_count_per_eye[1] > 0 && rec.bind_count_per_eye[2] > 0) {
        if (rec.last_rtv_handle_per_eye[1] != 0 && rec.last_rtv_handle_per_eye[2] != 0 &&
                rec.last_rtv_handle_per_eye[1] != rec.last_rtv_handle_per_eye[2]) {
            rec.rtv_divergence_seen += 1;
        }
        if (rec.last_descriptor_table0_per_eye[1] != 0 && rec.last_descriptor_table0_per_eye[2] != 0 &&
                rec.last_descriptor_table0_per_eye[1] != rec.last_descriptor_table0_per_eye[2]) {
            rec.desc_divergence_seen += 1;
        }
    }
}

std::vector<ShaderOverrideRegistry::EyeDiffEntry> ShaderOverrideRegistry::eyediff_snapshot_top_divergent(size_t max_entries) const {
    std::vector<EyeDiffEntry> out{};
    std::scoped_lock _{m_eyediff_mutex};
    out.reserve(m_eyediff_by_pshash.size());
    for (const auto& [hash, rec] : m_eyediff_by_pshash) {
        EyeDiffEntry e{};
        e.ps_hash = rec.ps_hash;
        e.vs_hash = rec.vs_hash;
        e.bind_count_left = rec.bind_count_per_eye[1];
        e.bind_count_right = rec.bind_count_per_eye[2];
        e.bind_count_full = rec.bind_count_per_eye[3];
        e.bind_count_other = rec.bind_count_per_eye[0] + rec.bind_count_per_eye[4];
        e.last_rtv_left = rec.last_rtv_handle_per_eye[1];
        e.last_rtv_right = rec.last_rtv_handle_per_eye[2];
        e.last_desc_left = rec.last_descriptor_table0_per_eye[1];
        e.last_desc_right = rec.last_descriptor_table0_per_eye[2];
        e.rtv_divergence_seen = rec.rtv_divergence_seen;
        e.desc_divergence_seen = rec.desc_divergence_seen;
        e.last_seen_frame = rec.last_seen_frame;
        out.push_back(std::move(e));
    }
    // Sort by divergence-score desc: prefer entries with desc-table divergence
    // (most direct evidence of "different inputs per eye"), then RTV divergence,
    // then asymmetric bind counts.
    std::sort(out.begin(), out.end(), [](const EyeDiffEntry& a, const EyeDiffEntry& b) {
        auto score = [](const EyeDiffEntry& x) {
            const uint64_t asym = x.bind_count_left > x.bind_count_right
                ? (x.bind_count_left - x.bind_count_right)
                : (x.bind_count_right - x.bind_count_left);
            return x.desc_divergence_seen * 1000 + x.rtv_divergence_seen * 100 + asym;
        };
        return score(a) > score(b);
    });
    if (out.size() > max_entries) out.resize(max_entries);
    return out;
}

void ShaderOverrideRegistry::hunter_ensure_magenta_compiled_locked() {
    if (m_hunter_tried_compile_magenta) return;
    m_hunter_tried_compile_magenta = true;
    namespace fs = std::filesystem;
    fs::path dir = profile_override_dir();
    std::error_code ec{};
    fs::create_directories(dir, ec);
    // Compile 8 variants of the magenta PS — one per SV_Target count from 1 to 8.
    // At substitute time we'll pick the variant matching the original PSO's
    // NumRenderTargets so D3D12 doesn't reject the swap.
    for (int n = 1; n <= 8; ++n) {
        char fname[128]{};
        std::snprintf(fname, sizeof(fname), "_uevr_hunter_magenta_rt%d.hlsl", n);
        fs::path src_path = dir / fname;
        {
            std::ofstream out{src_path, std::ios::binary | std::ios::trunc};
            out << "// auto-generated UEVR Shader Hunter magenta PS, " << n << " RT(s)\n";
            out << "struct Out {\n";
            for (int i = 0; i < n; ++i) {
                out << "    float4 t" << i << " : SV_Target" << i << ";\n";
            }
            out << "};\n";
            out << "Out main() {\n";
            out << "    Out o;\n";
            for (int i = 0; i < n; ++i) {
                out << "    o.t" << i << " = float4(1.0, 0.0, 1.0, 1.0);\n";
            }
            out << "    return o;\n";
            out << "}\n";
        }
        ShaderCompileRequest req{};
        req.source_path = src_path;
        req.entry_point = "main";
        req.profile = "ps_6_0";
        req.preferred_backend = ShaderCompilerBackend::Dxc;
        auto result = compile_shader_file(req);
        if (result.succeeded) {
            m_hunter_magenta_ps[n] = std::move(result.bytecode);
            spdlog::info("[ShaderHunter] magenta PS rt{} compiled, {} bytes", n, m_hunter_magenta_ps[n].size());
        } else {
            spdlog::error("[ShaderHunter] magenta PS rt{} compile failed: {}", n, result.error);
        }
    }
}

void ShaderOverrideRegistry::hunter_toggle_highlight_hash(std::string_view hash) {
    std::scoped_lock _{m_mutex};
    std::string h = normalize_hash(std::string{hash});
    if (h.empty()) return;
    if (m_hunter_highlight.count(h) > 0) {
        m_hunter_highlight.erase(h);
        spdlog::info("[ShaderHunter] highlight - {}", h);
    } else {
        m_hunter_highlight.insert(h);
        hunter_ensure_magenta_compiled_locked();
        spdlog::info("[ShaderHunter] highlight + {} (set size now {})", h, m_hunter_highlight.size());
    }
    // Force re-eval for any record whose PS hash matches so substitution kicks
    // in (or stops) on the next bind.
    for (auto& [ptr, rec] : m_d3d12_graphics_pso_records) {
        char crc_str[16]{};
        if (rec.pixel_crc32 != 0) {
            std::snprintf(crc_str, sizeof(crc_str), "%08x", rec.pixel_crc32);
        }
        if (rec.pixel_hash == h || (!h.empty() && h == crc_str)) {
            rec.applied_override_revision = (std::numeric_limits<uint64_t>::max)();
        }
    }
    // Make sure registry tracks D3D12 so the substitution path runs.
    m_has_active_d3d12_overrides.store(true, std::memory_order_relaxed);
    {
        std::scoped_lock skip_lock{m_hunter_skip_mutex};
        m_hunter_skip_by_cmdlist.clear();
    }
}

std::vector<std::string> ShaderOverrideRegistry::hunter_highlight_snapshot() const {
    std::scoped_lock _{m_mutex};
    return {m_hunter_highlight.begin(), m_hunter_highlight.end()};
}

// Optional pre-populate of per-eye-skip sets from env vars. Called once on
// first hunter operation. Lets the launch script pre-bake known-broken
// shaders so the right eye is fixed from frame 1.
static void seed_per_eye_skip_from_env(std::unordered_set<std::string>& left_set,
                                       std::unordered_set<std::string>& right_set) {
    static bool seeded = false;
    if (seeded) return;
    seeded = true;
    auto parse = [](const char* env_name, std::unordered_set<std::string>& set) {
        char raw[2048]{};
        const DWORD n = GetEnvironmentVariableA(env_name, raw, sizeof(raw));
        if (n == 0 || n >= sizeof(raw)) return;
        std::string token{};
        for (char c : std::string_view{raw, n}) {
            if (c == ',' || c == ';' || std::isspace(static_cast<unsigned char>(c)) != 0) {
                auto h = normalize_hash(token);
                if (!h.empty()) set.insert(std::move(h));
                token.clear();
            } else {
                token.push_back(c);
            }
        }
        auto h = normalize_hash(token);
        if (!h.empty()) set.insert(std::move(h));
    };
    parse("UEVR_SHADER_HUNTER_SKIP_LEFT_ONLY", left_set);
    parse("UEVR_SHADER_HUNTER_SKIP_RIGHT_ONLY", right_set);
}

void ShaderOverrideRegistry::hunter_toggle_skip_left_only(std::string_view hash) {
    std::scoped_lock _{m_mutex};
    std::string h = normalize_hash(std::string{hash});
    if (h.empty()) return;
    if (m_hunter_skip_left_only.count(h) > 0) {
        m_hunter_skip_left_only.erase(h);
        spdlog::info("[ShaderHunter] skip_left_only - {}", h);
    } else {
        m_hunter_skip_left_only.insert(h);
        spdlog::info("[ShaderHunter] skip_left_only + {}", h);
    }
    // Invalidate matching records so SetPipelineState re-evaluates.
    for (auto& [ptr, rec] : m_d3d12_graphics_pso_records) {
        if (rec.pixel_hash == h) rec.applied_override_revision = (std::numeric_limits<uint64_t>::max)();
    }
    {
        std::scoped_lock skip_lock{m_hunter_skip_mutex};
        m_hunter_skip_by_cmdlist.clear();
    }
}

void ShaderOverrideRegistry::hunter_toggle_skip_right_only(std::string_view hash) {
    std::scoped_lock _{m_mutex};
    std::string h = normalize_hash(std::string{hash});
    if (h.empty()) return;
    if (m_hunter_skip_right_only.count(h) > 0) {
        m_hunter_skip_right_only.erase(h);
        spdlog::info("[ShaderHunter] skip_right_only - {}", h);
    } else {
        m_hunter_skip_right_only.insert(h);
        spdlog::info("[ShaderHunter] skip_right_only + {}", h);
    }
    for (auto& [ptr, rec] : m_d3d12_graphics_pso_records) {
        if (rec.pixel_hash == h) rec.applied_override_revision = (std::numeric_limits<uint64_t>::max)();
    }
    {
        std::scoped_lock skip_lock{m_hunter_skip_mutex};
        m_hunter_skip_by_cmdlist.clear();
    }
}

bool ShaderOverrideRegistry::hunter_is_skip_left_only(std::string_view hash) const {
    std::scoped_lock _{m_mutex};
    return m_hunter_skip_left_only.count(normalize_hash(std::string{hash})) > 0;
}

bool ShaderOverrideRegistry::hunter_is_skip_right_only(std::string_view hash) const {
    std::scoped_lock _{m_mutex};
    return m_hunter_skip_right_only.count(normalize_hash(std::string{hash})) > 0;
}

void ShaderOverrideRegistry::hunter_set_cycle_highlight_mode(bool v) {
    std::scoped_lock _{m_mutex};
    m_hunter_cycle_highlight_mode.store(v, std::memory_order_relaxed);
    if (v) hunter_ensure_magenta_compiled_locked();
    // Force re-eval of all hunter-active records.
    const auto h_ps = m_hunter_active_hash;
    const auto h_vs = m_hunter_active_hash_vs;
    const auto h_cs = m_hunter_active_hash_cs;
    for (auto& [ptr, rec] : m_d3d12_graphics_pso_records) {
        if ((!h_ps.empty() && rec.pixel_hash == h_ps) ||
                (!h_vs.empty() && rec.vertex_hash == h_vs) ||
                (!h_cs.empty() && rec.compute_hash == h_cs)) {
            rec.applied_override_revision = (std::numeric_limits<uint64_t>::max)();
        }
    }
    m_has_active_d3d12_overrides.store(true, std::memory_order_relaxed);
    {
        std::scoped_lock skip_lock{m_hunter_skip_mutex};
        m_hunter_skip_by_cmdlist.clear();
    }
    spdlog::info("[ShaderHunter] cycle_highlight_mode={}", v ? 1 : 0);
}

size_t ShaderOverrideRegistry::hunter_trim_collected(bool scene_only, bool live_only) {
    std::scoped_lock _{m_mutex};
    const size_t before = m_hunter_order.size() + m_hunter_order_vs.size() + m_hunter_order_cs.size();

    auto should_keep = [&](const HunterCollectedEntry& info) {
        if (live_only) {
            const uint64_t activity_frame = info.last_draw_frame != 0
                ? info.last_draw_frame
                : info.last_seen_frame;
            const auto age = m_frame >= activity_frame
                ? (m_frame - activity_frame) : 0;
            if (age > static_cast<uint64_t>(m_hunter_recent_frame_age)) {
                return false;
            }
        }
        if (scene_only && !hunter_entry_is_scene_candidate_locked(info)) {
            return false;
        }
        return true;
    };

    auto trim_stage = [&](std::vector<std::string>& order, auto& collected, bool skip_compute) {
        std::vector<std::string> new_order{};
        new_order.reserve(order.size());
        for (const auto& hash : order) {
            auto it = collected.find(hash);
            if (it == collected.end()) continue;
            if (skip_compute && it->second.stage == HunterStage::Compute) {
                collected.erase(it);
                continue;
            }
            if (!should_keep(it->second)) {
                collected.erase(it);
                continue;
            }
            new_order.push_back(hash);
        }
        order = std::move(new_order);
        return order.size();
    };

    const size_t kept_main = trim_stage(m_hunter_order, m_hunter_collected, true);
    const size_t kept_vs = trim_stage(m_hunter_order_vs, m_hunter_collected_vs, false);
    const size_t kept_cs = trim_stage(m_hunter_order_cs, m_hunter_collected_cs, false);
    // Snap active idx back into range.
    if (m_hunter_active_index >= static_cast<int>(m_hunter_order.size())) {
        m_hunter_active_index = static_cast<int>(m_hunter_order.size()) - 1;
    }
    if (m_hunter_active_index < 0 && !m_hunter_order.empty()) m_hunter_active_index = 0;
    hunter_rebuild_active_locked();
    hunter_rebuild_stage_active_locked(HunterStage::Vertex);
    hunter_rebuild_stage_active_locked(HunterStage::Compute);
    // Also auto-pause collection so the trimmed list stays stable.
    m_hunter_window_stopped.store(true, std::memory_order_relaxed);
    const size_t after = kept_main + kept_vs + kept_cs;
    spdlog::info("[ShaderHunter] trim_collected: {} -> {} (main={} vs={} cs={} scene_only={} live_only={})",
        before, after, kept_main, kept_vs, kept_cs, scene_only ? 1 : 0, live_only ? 1 : 0);
    return after;
}

size_t ShaderOverrideRegistry::hunter_trim_to_top_hits(size_t keep_count) {
    std::scoped_lock _{m_mutex};
    if (keep_count == 0) return 0;
    const size_t before = m_hunter_order.size() + m_hunter_order_vs.size() + m_hunter_order_cs.size();

    struct TopHitCandidate {
        uint64_t hits{};
        HunterStage stage{HunterStage::Pixel};
        std::string hash{};
    };
    std::vector<TopHitCandidate> by_hits{};
    by_hits.reserve(before);
    bool has_actual_hits = false;

    auto scan_actual = [&](const std::vector<std::string>& order, const auto& collected, bool skip_compute) {
        for (const auto& hash : order) {
            const auto it = collected.find(hash);
            if (it == collected.end()) continue;
            if (skip_compute && it->second.stage == HunterStage::Compute) continue;
            if ((it->second.draw_hits + it->second.dispatch_hits) > 0) {
                has_actual_hits = true;
                return;
            }
        }
    };
    scan_actual(m_hunter_order, m_hunter_collected, true);
    if (!has_actual_hits) scan_actual(m_hunter_order_vs, m_hunter_collected_vs, false);
    if (!has_actual_hits) scan_actual(m_hunter_order_cs, m_hunter_collected_cs, false);

    auto collect_candidates = [&](const std::vector<std::string>& order, const auto& collected, HunterStage stage, bool skip_compute) {
        for (const auto& hash : order) {
            const auto it = collected.find(hash);
            if (it == collected.end()) continue;
            if (skip_compute && it->second.stage == HunterStage::Compute) continue;
            const uint64_t actual_hits = it->second.draw_hits + it->second.dispatch_hits;
            if (has_actual_hits && actual_hits == 0) {
                continue;
            }
            by_hits.push_back(TopHitCandidate{
                has_actual_hits ? actual_hits : it->second.hits,
                stage,
                hash});
        }
    };
    collect_candidates(m_hunter_order, m_hunter_collected, HunterStage::Pixel, true);
    collect_candidates(m_hunter_order_vs, m_hunter_collected_vs, HunterStage::Vertex, false);
    collect_candidates(m_hunter_order_cs, m_hunter_collected_cs, HunterStage::Compute, false);

    std::sort(by_hits.begin(), by_hits.end(),
        [](const auto& a, const auto& b) { return a.hits > b.hits; });
    std::unordered_set<std::string> keep_pixel{};
    std::unordered_set<std::string> keep_vertex{};
    std::unordered_set<std::string> keep_compute{};
    keep_pixel.reserve(keep_count);
    keep_vertex.reserve(keep_count);
    keep_compute.reserve(keep_count);
    for (size_t i = 0; i < keep_count && i < by_hits.size(); ++i) {
        if (by_hits[i].stage == HunterStage::Vertex) {
            keep_vertex.insert(by_hits[i].hash);
        } else if (by_hits[i].stage == HunterStage::Compute) {
            keep_compute.insert(by_hits[i].hash);
        } else {
            keep_pixel.insert(by_hits[i].hash);
        }
    }

    auto trim_to_keep = [](std::vector<std::string>& order, auto& collected, const std::unordered_set<std::string>& keep, bool skip_compute) {
        std::vector<std::string> new_order{};
        new_order.reserve(keep.size());
        for (const auto& hash : order) {
            auto it = collected.find(hash);
            if (it == collected.end()) continue;
            if (skip_compute && it->second.stage == HunterStage::Compute) {
                collected.erase(it);
                continue;
            }
            if (keep.count(hash) > 0) {
                new_order.push_back(hash);
            } else {
                collected.erase(it);
            }
        }
        order = std::move(new_order);
        return order.size();
    };
    const size_t kept_main = trim_to_keep(m_hunter_order, m_hunter_collected, keep_pixel, true);
    const size_t kept_vs = trim_to_keep(m_hunter_order_vs, m_hunter_collected_vs, keep_vertex, false);
    const size_t kept_cs = trim_to_keep(m_hunter_order_cs, m_hunter_collected_cs, keep_compute, false);

    if (m_hunter_active_index >= static_cast<int>(m_hunter_order.size())) {
        m_hunter_active_index = static_cast<int>(m_hunter_order.size()) - 1;
    }
    if (m_hunter_active_index < 0 && !m_hunter_order.empty()) m_hunter_active_index = 0;
    hunter_rebuild_active_locked();
    hunter_rebuild_stage_active_locked(HunterStage::Vertex);
    hunter_rebuild_stage_active_locked(HunterStage::Compute);
    m_hunter_window_stopped.store(true, std::memory_order_relaxed);
    const size_t after = kept_main + kept_vs + kept_cs;
    spdlog::info("[ShaderHunter] trim_to_top_hits: {} -> {} (main={} vs={} cs={})",
        before, after, kept_main, kept_vs, kept_cs);
    return after;
}

void ShaderOverrideRegistry::hunter_clear_all_marks() {
    std::scoped_lock _{m_mutex};
    const auto pixel_marks = m_hunter_marked;
    const auto vertex_marks = m_hunter_marked_vs;
    const auto compute_marks = m_hunter_marked_cs;
    m_hunter_marked.clear();
    m_hunter_marked_vs.clear();
    m_hunter_marked_cs.clear();
    // Invalidate any tracked record whose hash was previously marked so
    // suppression state recomputes on next bind.
    auto touched = [&](const std::string& h) {
        return pixel_marks.count(h) > 0 || vertex_marks.count(h) > 0 || compute_marks.count(h) > 0;
    };
    for (auto& [ptr, rec] : m_d3d12_graphics_pso_records) {
        if (touched(rec.pixel_hash) || touched(rec.vertex_hash) || touched(rec.compute_hash)) {
            rec.applied_override_revision = (std::numeric_limits<uint64_t>::max)();
        }
    }
    {
        std::scoped_lock skip_lock{m_hunter_skip_mutex};
        m_hunter_skip_by_cmdlist.clear();
    }
    spdlog::info("[ShaderHunter] cleared all marks: ps={} vs={} cs={}",
        pixel_marks.size(), vertex_marks.size(), compute_marks.size());
}

size_t ShaderOverrideRegistry::hunter_delete_saved_manifests(std::string& error_out) {
    std::scoped_lock _{m_mutex};
    namespace fs = std::filesystem;
    fs::path dir = profile_override_dir();
    std::error_code ec{};
    if (!fs::exists(dir, ec)) {
        error_out = "Profile shader_overrides dir does not exist: " + dir.string();
        return 0;
    }
    size_t deleted = 0;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        const auto& path = entry.path();
        const auto name = path.filename().string();
        // Match the hunter_save_marked_as_manifests() naming pattern.
        if (name.rfind("hunter_ps_", 0) != 0) continue;
        if (path.extension() != ".json") continue;
        std::error_code rm_ec{};
        fs::remove(path, rm_ec);
        if (!rm_ec) {
            ++deleted;
            spdlog::info("[ShaderHunter] deleted manifest {}", path.string());
        }
    }
    // Drop in-memory entries whose key starts with the dx12:ps: prefix and
    // whose manifest path matches a hunter_ps_*.json under this dir.
    std::vector<std::string> dead_keys;
    for (const auto& [key, entry] : m_overrides) {
        const auto& mp = entry.manifest_path;
        if (!mp.empty() && mp.filename().string().rfind("hunter_ps_", 0) == 0) {
            dead_keys.push_back(key);
        }
    }
    for (const auto& k : dead_keys) {
        m_overrides.erase(k);
    }
    ++m_override_revision;
    refresh_active_override_flags_locked();
    push_event("Shader Hunter: deleted " + std::to_string(deleted) + " saved hunter_ps_*.json manifests");
    spdlog::info("[ShaderHunter] deleted {} hunter_ps_*.json manifests + {} in-memory entries",
        deleted, dead_keys.size());
    return deleted;
}

void ShaderOverrideRegistry::hunter_clear_runtime_blocklist() {
    std::scoped_lock _{m_mutex};
    m_hunter_runtime_blocklist.clear();
    spdlog::info("[ShaderHunter] runtime blocklist cleared");
}

std::vector<std::string> ShaderOverrideRegistry::hunter_runtime_blocklist_snapshot() const {
    std::scoped_lock _{m_mutex};
    return {m_hunter_runtime_blocklist.begin(), m_hunter_runtime_blocklist.end()};
}

void ShaderOverrideRegistry::hunter_set_hide_marked(bool v) {
    std::scoped_lock _{m_mutex};
    m_hunter_hide_marked.store(v, std::memory_order_relaxed);
    // Invalidate all marked-hash records so they pick up new hide state.
    for (auto& [ptr, rec] : m_d3d12_graphics_pso_records) {
        if (m_hunter_marked.count(rec.pixel_hash) > 0 ||
                m_hunter_marked.count(rec.compute_hash) > 0 ||
                m_hunter_marked_vs.count(rec.vertex_hash) > 0 ||
                m_hunter_marked_cs.count(rec.compute_hash) > 0) {
            rec.applied_override_revision = (std::numeric_limits<uint64_t>::max)();
        }
    }
    {
        std::scoped_lock skip_lock{m_hunter_skip_mutex};
        m_hunter_skip_by_cmdlist.clear();
    }
}

void ShaderOverrideRegistry::hunter_set_suppression_enabled(bool v) {
    std::scoped_lock _{m_mutex};
    m_hunter_suppression_enabled.store(v, std::memory_order_relaxed);
    const auto active_hash = m_hunter_active_hash;
    const auto active_hash_vs = m_hunter_active_hash_vs;
    const auto active_hash_cs = m_hunter_active_hash_cs;
    for (auto& [ptr, rec] : m_d3d12_graphics_pso_records) {
        if ((!active_hash.empty() && (rec.pixel_hash == active_hash || rec.compute_hash == active_hash)) ||
                (!active_hash_vs.empty() && rec.vertex_hash == active_hash_vs) ||
                (!active_hash_cs.empty() && rec.compute_hash == active_hash_cs)) {
            rec.applied_override_revision = (std::numeric_limits<uint64_t>::max)();
        }
    }
    {
        std::scoped_lock skip_lock{m_hunter_skip_mutex};
        m_hunter_skip_by_cmdlist.clear();
    }
    spdlog::info("[ShaderHunter] suppression_enabled={}", v ? 1 : 0);
}

ShaderOverrideRegistry::HunterStateView ShaderOverrideRegistry::hunter_state() const {
    std::scoped_lock _{m_mutex};
    HunterStateView v{};
    v.active = m_hunter_active.load(std::memory_order_relaxed);
    v.hide_marked = m_hunter_hide_marked.load(std::memory_order_relaxed);
    v.suppression_enabled = m_hunter_suppression_enabled.load(std::memory_order_relaxed);
    v.active_index = -1;
    v.active_hash = m_hunter_active_hash;
    // Mirror per-stage state into the per-stage arrays in the view.
    v.active_index_per_stage[0] = -1;
    v.active_hash_per_stage[0] = m_hunter_active_hash;
    v.active_index_per_stage[1] = m_hunter_active_index_vs;
    v.active_hash_per_stage[1] = m_hunter_active_hash_vs;
    v.active_index_per_stage[2] = m_hunter_active_index_cs;
    v.active_hash_per_stage[2] = m_hunter_active_hash_cs;
    v.collected_count_per_stage[0] = 0;
    v.collected_count_per_stage[1] = m_hunter_order_vs.size();
    v.collected_count_per_stage[2] = m_hunter_order_cs.size();
    v.marked_count_per_stage[0] = m_hunter_marked.size();
    v.marked_count_per_stage[1] = m_hunter_marked_vs.size();
    v.marked_count_per_stage[2] = m_hunter_marked_cs.size();
    // Flag whether the currently-hunted hash for each stage is also marked.
    v.active_is_marked_per_stage[0] = !m_hunter_active_hash.empty() &&
        m_hunter_marked.count(m_hunter_active_hash) > 0;
    v.active_is_marked_per_stage[1] = !m_hunter_active_hash_vs.empty() &&
        m_hunter_marked_vs.count(m_hunter_active_hash_vs) > 0;
    v.active_is_marked_per_stage[2] = !m_hunter_active_hash_cs.empty() &&
        m_hunter_marked_cs.count(m_hunter_active_hash_cs) > 0;
    v.collected_count = m_hunter_order.size();
    v.marked_count = m_hunter_marked.size() + m_hunter_marked_vs.size() + m_hunter_marked_cs.size();
    v.frame_window = m_hunter_frame_window;
    v.recent_frame_age = m_hunter_recent_frame_age;
    v.min_scene_ps_size = HUNTER_MIN_SCENE_PS_SIZE;
    v.window_stopped = m_hunter_window_stopped.load(std::memory_order_relaxed);
    if (m_hunter_frame_window > 0 && !v.window_stopped) {
        const uint64_t end_frame = m_hunter_window_start_frame +
            static_cast<uint64_t>(m_hunter_frame_window);
        v.window_frames_left = (end_frame > m_frame) ? (end_frame - m_frame) : 0;
    } else {
        v.window_frames_left = 0;
    }
    v.collected_hashes.reserve(v.collected_count);
    v.collected_vs_hashes.reserve(v.collected_count);
    v.collected_crc32s.reserve(v.collected_count);
    v.collected_sizes.reserve(v.collected_count);
    v.collected_hits.reserve(v.collected_count);
    v.collected_draw_hits.reserve(v.collected_count);
    v.collected_dispatch_hits.reserve(v.collected_count);
    v.collected_eye_left_hits.reserve(v.collected_count);
    v.collected_eye_right_hits.reserve(v.collected_count);
    v.collected_eye_full_hits.reserve(v.collected_count);
    v.collected_eye_other_hits.reserve(v.collected_count);
    v.collected_draw_age_frames.reserve(v.collected_count);
    v.collected_last_render_targets.reserve(v.collected_count);
    v.collected_last_depth_target.reserve(v.collected_count);
    v.collected_age_frames.reserve(v.collected_count);
    v.collected_marked.reserve(v.collected_count);
    for (const auto& hash : m_hunter_order) {
        const auto it = m_hunter_collected.find(hash);
        if (it == m_hunter_collected.end()) {
            continue;
        }

        const auto& info = it->second;
        if (info.stage == HunterStage::Compute) {
            continue;
        }
        const uint64_t activity_frame = info.last_draw_frame != 0 ? info.last_draw_frame : info.last_seen_frame;
        const auto age = m_frame >= info.last_seen_frame ? (m_frame - info.last_seen_frame) : 0;
        const auto draw_age = activity_frame != 0 && m_frame >= activity_frame ? (m_frame - activity_frame) : 0;
        if ((info.draw_hits + info.dispatch_hits) > 0 &&
                draw_age <= static_cast<uint64_t>(m_hunter_recent_frame_age)) {
            ++v.live_count;
            if (hunter_entry_is_scene_candidate_locked(info)) {
                ++v.scene_live_count;
            }
        }

        const int pixel_view_index = static_cast<int>(v.collected_count_per_stage[0]);
        v.collected_hashes.push_back(hash);
        ++v.collected_count_per_stage[0];
        v.collected_vs_hashes.push_back(info.vs_hash);
        v.collected_crc32s.push_back(info.crc32);
        v.collected_sizes.push_back(info.ps_size);
        v.collected_hits.push_back(info.hits);
        v.collected_draw_hits.push_back(info.draw_hits + info.dispatch_hits);
        v.collected_dispatch_hits.push_back(info.dispatch_hits);
        v.collected_eye_left_hits.push_back(info.eye_hits[1]);
        v.collected_eye_right_hits.push_back(info.eye_hits[2]);
        v.collected_eye_full_hits.push_back(info.eye_hits[3]);
        v.collected_eye_other_hits.push_back(info.eye_hits[0] + info.eye_hits[4]);
        v.collected_draw_age_frames.push_back(draw_age);
        v.collected_last_render_targets.push_back(info.last_render_targets);
        v.collected_last_depth_target.push_back(info.last_depth_target);
        v.collected_age_frames.push_back(age);
        v.collected_marked.push_back(m_hunter_marked.count(hash) > 0);
        v.collected_stages.push_back(HunterStage::Pixel);
        if (hash == m_hunter_active_hash) {
            v.active_index = pixel_view_index;
            v.active_index_per_stage[0] = pixel_view_index;
            v.active_crc32 = info.crc32;
            v.active_age_frames = draw_age;
            v.active_crc32_per_stage[0] = info.crc32;
            v.active_age_frames_per_stage[0] = draw_age;
        }
    }
    v.live_count_per_stage[0] = v.live_count;
    // === VS-stage loop ===
    for (const auto& vs_hash : m_hunter_order_vs) {
        const auto it = m_hunter_collected_vs.find(vs_hash);
        if (it == m_hunter_collected_vs.end()) continue;
        const auto& info = it->second;
        const uint64_t activity_frame = info.last_draw_frame != 0 ? info.last_draw_frame : info.last_seen_frame;
        const auto age = m_frame >= info.last_seen_frame ? (m_frame - info.last_seen_frame) : 0;
        const auto draw_age = activity_frame != 0 && m_frame >= activity_frame ? (m_frame - activity_frame) : 0;
        if ((info.draw_hits + info.dispatch_hits) > 0 &&
                draw_age <= static_cast<uint64_t>(m_hunter_recent_frame_age)) {
            ++v.live_count;
            ++v.live_count_per_stage[1];
            if (hunter_entry_is_scene_candidate_locked(info)) {
                ++v.scene_live_count;
            }
        }
        v.collected_hashes.push_back(vs_hash);
        v.collected_vs_hashes.push_back(info.vs_hash);  // companion PS for context
        v.collected_crc32s.push_back(info.crc32);
        v.collected_sizes.push_back(info.ps_size);
        v.collected_hits.push_back(info.hits);
        v.collected_draw_hits.push_back(info.draw_hits + info.dispatch_hits);
        v.collected_dispatch_hits.push_back(info.dispatch_hits);
        v.collected_eye_left_hits.push_back(info.eye_hits[1]);
        v.collected_eye_right_hits.push_back(info.eye_hits[2]);
        v.collected_eye_full_hits.push_back(info.eye_hits[3]);
        v.collected_eye_other_hits.push_back(info.eye_hits[0] + info.eye_hits[4]);
        v.collected_draw_age_frames.push_back(draw_age);
        v.collected_last_render_targets.push_back(info.last_render_targets);
        v.collected_last_depth_target.push_back(info.last_depth_target);
        v.collected_age_frames.push_back(age);
        v.collected_marked.push_back(m_hunter_marked_vs.count(vs_hash) > 0);
        v.collected_stages.push_back(HunterStage::Vertex);
        if (vs_hash == m_hunter_active_hash_vs) {
            v.active_crc32_per_stage[1] = info.crc32;
            v.active_age_frames_per_stage[1] = draw_age;
        }
    }
    // === CS-stage loop ===
    for (const auto& cs_hash : m_hunter_order_cs) {
        const auto it = m_hunter_collected_cs.find(cs_hash);
        if (it == m_hunter_collected_cs.end()) continue;
        const auto& info = it->second;
        const uint64_t activity_frame = info.last_draw_frame != 0 ? info.last_draw_frame : info.last_seen_frame;
        const auto age = m_frame >= info.last_seen_frame ? (m_frame - info.last_seen_frame) : 0;
        const auto draw_age = activity_frame != 0 && m_frame >= activity_frame ? (m_frame - activity_frame) : 0;
        if ((info.draw_hits + info.dispatch_hits) > 0 &&
                draw_age <= static_cast<uint64_t>(m_hunter_recent_frame_age)) {
            ++v.live_count;
            ++v.live_count_per_stage[2];
            if (hunter_entry_is_scene_candidate_locked(info)) {
                ++v.scene_live_count;
            }
        }
        v.collected_hashes.push_back(cs_hash);
        v.collected_vs_hashes.push_back("");
        v.collected_crc32s.push_back(info.crc32);
        v.collected_sizes.push_back(info.ps_size);
        v.collected_hits.push_back(info.hits);
        v.collected_draw_hits.push_back(info.draw_hits + info.dispatch_hits);
        v.collected_dispatch_hits.push_back(info.dispatch_hits);
        v.collected_eye_left_hits.push_back(info.eye_hits[1]);
        v.collected_eye_right_hits.push_back(info.eye_hits[2]);
        v.collected_eye_full_hits.push_back(info.eye_hits[3]);
        v.collected_eye_other_hits.push_back(info.eye_hits[0] + info.eye_hits[4]);
        v.collected_draw_age_frames.push_back(draw_age);
        v.collected_last_render_targets.push_back(info.last_render_targets);
        v.collected_last_depth_target.push_back(info.last_depth_target);
        v.collected_age_frames.push_back(age);
        v.collected_marked.push_back(m_hunter_marked_cs.count(cs_hash) > 0);
        v.collected_stages.push_back(HunterStage::Compute);
        if (cs_hash == m_hunter_active_hash_cs) {
            v.active_crc32_per_stage[2] = info.crc32;
            v.active_age_frames_per_stage[2] = draw_age;
        }
    }
    v.collected_count = v.collected_hashes.size();
    return v;
}

void ShaderOverrideRegistry::hunter_record_set_pipeline_state(void* command_list, void* original_pso) {
    // Legacy variant — delegates with unknown eye bucket. Caller that wants
    // per-eye selective skip should call hunter_record_set_pipeline_state_with_eye.
    hunter_record_set_pipeline_state_with_eye(command_list, original_pso, 0);
}

namespace {
bool sn2_diag_clean_disables_shader_skips() {
    static const bool enabled = []() {
        auto env_enabled = [](const char* name) {
            char value[32]{};
            const auto len = GetEnvironmentVariableA(name, value, static_cast<DWORD>(sizeof(value)));
            if (len == 0 || len >= sizeof(value)) return false;
            std::string_view raw{value, std::min<DWORD>(len, static_cast<DWORD>(sizeof(value) - 1))};
            return raw != "0" && raw != "false" && raw != "FALSE" && raw != "off" && raw != "OFF";
        };
        return env_enabled("UEVR_SN2_DIAG_CLEAN") || env_enabled("UEVR_SN2_DIAG_STRICT");
    }();
    return enabled;
}
}

void ShaderOverrideRegistry::hunter_record_set_pipeline_state_with_eye(void* command_list, void* original_pso, int eye_bucket) {
    g_hunter_setpso_calls.fetch_add(1, std::memory_order_relaxed);
    // Fast path: hunter not active, no marked-suppress, no per-eye-skip targets — nothing to do.
    const bool clean_diag = sn2_diag_clean_disables_shader_skips();
    if (!clean_diag) {
        seed_per_eye_skip_from_env(m_hunter_skip_left_only, m_hunter_skip_right_only);
    }
    const bool has_per_eye = !clean_diag && (!m_hunter_skip_left_only.empty() || !m_hunter_skip_right_only.empty());
    const bool has_global_suppress = !clean_diag && !shader_hunter_global_suppress_set().empty();
    if (!m_hunter_active.load(std::memory_order_relaxed) &&
            !m_hunter_hide_marked.load(std::memory_order_relaxed) &&
            !has_per_eye &&
            !has_global_suppress) {
        // Clear any stale skip flag for this CL so we don't skip after a
        // previous bound suppression.
        std::scoped_lock _{m_hunter_skip_mutex};
        m_hunter_skip_by_cmdlist.erase(command_list);
        return;
    }

    // ShaderToggler's model: pipeline init records handle -> shader hashes;
    // bind_pipeline updates the command-list active handle and, during the
    // collection window, records that active handle. Keep all hunter collection
    // on this bind path instead of forcing the normal override/PSO update path.
    bool skip = false;
    bool is_compute = false;
    bool is_graphics = false;
    {
        std::scoped_lock _{m_mutex};
        auto it = m_d3d12_graphics_pso_records.find(reinterpret_cast<uintptr_t>(original_pso));
        if (it != m_d3d12_graphics_pso_records.end()) {
            auto& record = it->second;
            // Backfill the stream-form PS CRC. UE5.6 stream-form PSOs get a pixel_hash
            // recorded but NOT a pixel_crc32 (only the bytecode dumper resolves the CRC).
            // Without this, CRC-keyed global suppress (UEVR_SHADER_HUNTER_SUPPRESS) and
            // per-eye skip silently miss every stream PSO — e.g. the water chain
            // 0x4e86dc09/0xb9be2499/etc. which we only know by CRC. Cached once.
            if (record.pixel_crc32 == 0 && !record.pixel_hash.empty()) {
                const uint32_t fb = sn2_pso_bytecode_dumper::ps_crc_for_pso(
                    reinterpret_cast<ID3D12PipelineState*>(original_pso));
                if (fb != 0 && fb != sn2_pso_bytecode_dumper::NOPS_CRC) {
                    record.pixel_crc32 = fb;
                }
            }
            is_compute = !record.compute_hash.empty() && record.pixel_hash.empty() && record.mesh_hash.empty();
            is_graphics = !record.pixel_hash.empty() || !record.mesh_hash.empty();
            hunter_record_bind_locked(record);
            skip = clean_diag ? false : hunter_should_suppress_locked(record);
            // Per-eye selective skip: eye_bucket 1 = Left, 2 = Right.
            // Matches against both FNV1a-64 (16-char) and CRC32 (8-char) entries.
            if (!clean_diag && !skip && (!record.pixel_hash.empty() || record.pixel_crc32 != 0)) {
                char crc_str[16]{};
                std::snprintf(crc_str, sizeof(crc_str), "%08x", record.pixel_crc32);
                const std::string crc_key = crc_str;
                if (eye_bucket == 1 && (m_hunter_skip_left_only.count(record.pixel_hash) > 0 ||
                                         m_hunter_skip_left_only.count(crc_key) > 0)) skip = true;
                if (eye_bucket == 2 && (m_hunter_skip_right_only.count(record.pixel_hash) > 0 ||
                                         m_hunter_skip_right_only.count(crc_key) > 0)) skip = true;
            }
        }
    }
    if (skip) g_hunter_setpso_skip_true.fetch_add(1, std::memory_order_relaxed);
    std::scoped_lock _{m_hunter_skip_mutex};
    if (!is_compute && !is_graphics) {
        m_hunter_skip_by_cmdlist.erase(command_list);
        return;
    }

    auto& state = m_hunter_skip_by_cmdlist[command_list];
    if (is_compute) {
        state.compute = skip;
    }
    if (is_graphics) {
        state.graphics = skip;
    }
}

uint32_t ShaderOverrideRegistry::d3d12_pso_vertex_crc32(uintptr_t pso_pointer) const {
    std::scoped_lock _{m_mutex};
    auto it = m_d3d12_graphics_pso_records.find(pso_pointer);
    if (it == m_d3d12_graphics_pso_records.end()) return 0;
    return it->second.vertex_crc32;
}

uint32_t ShaderOverrideRegistry::d3d12_pso_pixel_crc32(uintptr_t pso_pointer) const {
    std::scoped_lock _{m_mutex};
    auto it = m_d3d12_graphics_pso_records.find(pso_pointer);
    if (it == m_d3d12_graphics_pso_records.end()) return 0;
    return it->second.pixel_crc32;
}

uint32_t ShaderOverrideRegistry::d3d12_pso_geometry_crc32(uintptr_t pso_pointer) const {
    std::scoped_lock _{m_mutex};
    auto it = m_d3d12_graphics_pso_records.find(pso_pointer);
    if (it == m_d3d12_graphics_pso_records.end()) return 0;
    return it->second.geometry_crc32;
}

uint32_t ShaderOverrideRegistry::d3d12_pso_compute_crc32(uintptr_t pso_pointer) const {
    std::scoped_lock _{m_mutex};
    auto it = m_d3d12_graphics_pso_records.find(pso_pointer);
    if (it == m_d3d12_graphics_pso_records.end()) return 0;
    return it->second.compute_crc32;
}

uint32_t ShaderOverrideRegistry::d3d12_pso_amplification_crc32(uintptr_t pso_pointer) const {
    std::scoped_lock _{m_mutex};
    auto it = m_d3d12_graphics_pso_records.find(pso_pointer);
    if (it == m_d3d12_graphics_pso_records.end()) return 0;
    return it->second.amplification_crc32;
}

uint32_t ShaderOverrideRegistry::d3d12_pso_mesh_crc32(uintptr_t pso_pointer) const {
    std::scoped_lock _{m_mutex};
    auto it = m_d3d12_graphics_pso_records.find(pso_pointer);
    if (it == m_d3d12_graphics_pso_records.end()) return 0;
    return it->second.mesh_crc32;
}

bool ShaderOverrideRegistry::hunter_should_skip_draw_per_eye(uintptr_t pso_pointer, int eye_bucket) const {
    if (sn2_diag_clean_disables_shader_skips()) {
        return false;
    }
    // === Extreme-test env vars: kill EVERY draw on one eye. ===
    // UEVR_SHADER_HUNTER_KILL_RIGHT_EYE=1 → skip all right-eye draws. If the
    // right eye then goes black, the per-eye skip mechanism is firing
    // correctly and the bug is just shader-selection. If right eye still
    // renders normally, the mechanism itself is broken.
    static const int kill_eye = []() {
        char buf[4]{};
        if (GetEnvironmentVariableA("UEVR_SHADER_HUNTER_KILL_RIGHT_EYE", buf, sizeof(buf)) > 0
                && buf[0] == '1') return 2;
        if (GetEnvironmentVariableA("UEVR_SHADER_HUNTER_KILL_LEFT_EYE", buf, sizeof(buf)) > 0
                && buf[0] == '1') return 1;
        return 0;
    }();
    if (kill_eye != 0 && eye_bucket == kill_eye) {
        // Log once per process so we know it's firing.
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true, std::memory_order_relaxed)) {
            spdlog::info("[ShaderHunter] KILL_{}_EYE active — skipping all draws on eye_bucket={}",
                kill_eye == 1 ? "LEFT" : "RIGHT", eye_bucket);
        }
        return true;
    }
    // Ensure env-var seeds are loaded (idempotent).
    seed_per_eye_skip_from_env(const_cast<std::unordered_set<std::string>&>(m_hunter_skip_left_only),
                               const_cast<std::unordered_set<std::string>&>(m_hunter_skip_right_only));
    // Cheap fast-path when nothing is configured per-eye.
    if (m_hunter_skip_left_only.empty() && m_hunter_skip_right_only.empty()) return false;
    if (eye_bucket != 1 && eye_bucket != 2) return false;
    // Resolve this draw's PS/CS identity. Copy out of the record map under the
    // lock so it can be released before the separately-locked dumper fallback.
    std::string which_hash;
    uint32_t which_crc = 0;
    {
        std::scoped_lock _{m_mutex};
        auto it = m_d3d12_graphics_pso_records.find(pso_pointer);
        if (it != m_d3d12_graphics_pso_records.end()) {
            const auto& rec = it->second;
            const bool is_pixel = !rec.pixel_hash.empty();
            which_hash = is_pixel ? rec.pixel_hash : rec.compute_hash;
            which_crc  = is_pixel ? rec.pixel_crc32 : rec.compute_crc32;
        }
    }
    // Stream-form PSOs (UE5.6 ID3D12Device2::CreatePipelineState) are NOT in
    // m_d3d12_graphics_pso_records, so the lookup above misses them entirely
    // (e.g. the SkyAtmosphere ApplyLowerHemisphereColorPS, ps_crc 0x1F958D46).
    // Fall back to the bytecode-dumper's stream-aware PS-CRC resolver so per-eye
    // skip can match stream PSOs by CRC. NOPS_CRC = "seen but no pixel shader".
    if (which_hash.empty() && which_crc == 0) {
        const uint32_t fb = sn2_pso_bytecode_dumper::ps_crc_for_pso(
            reinterpret_cast<ID3D12PipelineState*>(pso_pointer));
        if (fb != 0 && fb != sn2_pso_bytecode_dumper::NOPS_CRC) {
            which_crc = fb;
        }
    }
    // DIAG: log the first N right-eye (eye2) draws' resolved identity so we can see
    // whether the sky draw resolves to 0x1f958d46 at draw-time (PSO-pointer check).
    if (eye_bucket == 2) {
        static std::atomic<uint64_t> dn{0};
        const auto d = dn.fetch_add(1, std::memory_order_relaxed);
        if (d < 60) {
            char dk[16]{}; std::snprintf(dk, sizeof(dk), "%08x", which_crc);
            spdlog::warn("[SN2-SkipDiag] eye2 draw #{} which_hash={} which_crc=0x{}",
                d + 1, which_hash.empty() ? "(none)" : which_hash.c_str(), dk);
        }
    }
    if (which_hash.empty() && which_crc == 0) return false;
    char crc_str[16]{};
    std::snprintf(crc_str, sizeof(crc_str), "%08x", which_crc);
    const std::string crc_key = crc_str;
    std::scoped_lock _{m_mutex};
    const auto& set = (eye_bucket == 1) ? m_hunter_skip_left_only : m_hunter_skip_right_only;
    const bool skip = (!which_hash.empty() && set.count(which_hash) > 0) || set.count(crc_key) > 0;
    if (skip) {
        static std::atomic<uint64_t> skip_log{0};
        const auto n = skip_log.fetch_add(1, std::memory_order_relaxed);
        if (n < 8 || (n % 1000) == 0) {
            spdlog::warn("[SN2-SkipPerEye] skipped draw eye_bucket={} hash={} crc=0x{} (n={})",
                eye_bucket, which_hash.empty() ? "(stream)" : which_hash, crc_key, n + 1);
        }
    }
    return skip;
}

bool ShaderOverrideRegistry::hunter_should_skip_draw(void* command_list) const {
    const bool skip = hunter_should_skip_graphics(command_list);
    if (skip) {
        g_hunter_draw_skipped.fetch_add(1, std::memory_order_relaxed);
    }
    return skip;
}

bool ShaderOverrideRegistry::hunter_should_skip_graphics(void* command_list) const {
    std::scoped_lock _{m_hunter_skip_mutex};
    auto it = m_hunter_skip_by_cmdlist.find(command_list);
    return it != m_hunter_skip_by_cmdlist.end() && it->second.graphics;
}

bool ShaderOverrideRegistry::hunter_should_skip_compute(void* command_list) const {
    std::scoped_lock _{m_hunter_skip_mutex};
    auto it = m_hunter_skip_by_cmdlist.find(command_list);
    return it != m_hunter_skip_by_cmdlist.end() && it->second.compute;
}

bool ShaderOverrideRegistry::hunter_collect_compute_events() const {
    return shader_hunter_collect_compute_events_enabled();
}

bool ShaderOverrideRegistry::hunter_collect_indirect_events() const {
    return shader_hunter_collect_indirect_events_enabled();
}

bool ShaderOverrideRegistry::hunter_disable_compute_dispatch_hook() const {
    return shader_hunter_disable_compute_dispatch_hook_enabled();
}

void ShaderOverrideRegistry::hunter_clear_command_list(void* command_list) {
    std::scoped_lock _{m_hunter_skip_mutex};
    m_hunter_skip_by_cmdlist.erase(command_list);
}

void ShaderOverrideRegistry::hunter_inc_draw_hit() {
    g_hunter_draw_hits.fetch_add(1, std::memory_order_relaxed);
}

void ShaderOverrideRegistry::hunter_inc_draw_skipped() {
    g_hunter_draw_skipped.fetch_add(1, std::memory_order_relaxed);
}

void ShaderOverrideRegistry::hunter_inc_draw_indexed_hit() {
    g_hunter_draw_indexed_hits.fetch_add(1, std::memory_order_relaxed);
}

void ShaderOverrideRegistry::hunter_inc_draw_indexed_skipped() {
    g_hunter_draw_indexed_skipped.fetch_add(1, std::memory_order_relaxed);
}

void ShaderOverrideRegistry::hunter_inc_dispatch_hit() {
    g_hunter_dispatch_hits.fetch_add(1, std::memory_order_relaxed);
}

void ShaderOverrideRegistry::hunter_inc_dispatch_skipped() {
    g_hunter_dispatch_skipped.fetch_add(1, std::memory_order_relaxed);
}

void ShaderOverrideRegistry::hunter_inc_execute_indirect_hit() {
    g_hunter_execute_indirect_hits.fetch_add(1, std::memory_order_relaxed);
}

void ShaderOverrideRegistry::hunter_inc_execute_indirect_skipped() {
    g_hunter_execute_indirect_skipped.fetch_add(1, std::memory_order_relaxed);
}

void ShaderOverrideRegistry::hunter_inc_execute_bundle_hit() {
    g_hunter_execute_bundle_hits.fetch_add(1, std::memory_order_relaxed);
}

void ShaderOverrideRegistry::hunter_inc_execute_bundle_skipped() {
    g_hunter_execute_bundle_skipped.fetch_add(1, std::memory_order_relaxed);
}

void ShaderOverrideRegistry::hunter_inc_dispatch_mesh_hit() {
    g_hunter_dispatch_mesh_hits.fetch_add(1, std::memory_order_relaxed);
}

void ShaderOverrideRegistry::hunter_inc_dispatch_mesh_skipped() {
    g_hunter_dispatch_mesh_skipped.fetch_add(1, std::memory_order_relaxed);
}

bool ShaderOverrideRegistry::hunter_capture_active_as_override_stub(
    HunterStage hunter_stage,
    std::filesystem::path& manifest_path,
    std::filesystem::path& source_path,
    std::string& error_out
) {
    std::scoped_lock _{m_mutex};

    const std::string hash =
        hunter_stage == HunterStage::Vertex ? m_hunter_active_hash_vs :
        hunter_stage == HunterStage::Compute ? m_hunter_active_hash_cs :
        m_hunter_active_hash;

    if (hash.empty()) {
        error_out = "no active hunter hash for the requested stage";
        return false;
    }

    Stage stage = Stage::Pixel;
    switch (hunter_stage) {
    case HunterStage::Vertex:
        stage = Stage::Vertex;
        break;
    case HunterStage::Compute:
        stage = Stage::Compute;
        break;
    default:
        stage = Stage::Pixel;
        break;
    }

    namespace fs = std::filesystem;
    const auto stage_name = stage_to_string(stage);
    fs::path dir = profile_override_dir() / ("override_" + stage_name + "_" + hash);
    std::error_code ec{};
    fs::create_directories(dir, ec);
    if (ec) {
        error_out = ec.message();
        return false;
    }

    source_path = dir / "main.hlsl";
    manifest_path = dir / "manifest.json";

    if (!fs::exists(source_path, ec)) {
        std::ofstream source{source_path, std::ios::binary | std::ios::trunc};
        source << "// Disabled starter override captured from Shader Hunter.\n";
        source << "// Fill this with compatible HLSL, then set enabled=true in manifest.json.\n";
        source << "// target_hash=" << hash << " stage=" << stage_name << "\n";
    }

    std::ofstream manifest{manifest_path, std::ios::binary | std::ios::trunc};
    if (!manifest) {
        error_out = "failed to open manifest for writing";
        return false;
    }

    manifest << "{\n";
    manifest << "  \"backend\": \"dx12\",\n";
    manifest << "  \"stage\": \"" << stage_name << "\",\n";
    manifest << "  \"target_hash\": \"" << hash << "\",\n";
    manifest << "  \"name\": \"override_" << stage_name << "_" << hash << "\",\n";
    manifest << "  \"enabled\": false,\n";
    manifest << "  \"entry_point\": \"main\",\n";
    manifest << "  \"profile\": \"" << default_profile(Backend::D3D12, stage) << "\",\n";
    manifest << "  \"compiler\": \"dxc\",\n";
    manifest << "  \"source\": \"main.hlsl\"\n";
    manifest << "}\n";

    push_event("Captured disabled override stub for " + stage_name + " hash " + hash);
    request_reload();
    return true;
}

bool ShaderOverrideRegistry::hunter_export_scene_list_json(std::filesystem::path& out_path, std::string& error_out) const {
    std::scoped_lock _{m_mutex};
    namespace fs = std::filesystem;

    std::vector<json> rows{};
    rows.reserve(m_hunter_collected.size() + m_hunter_collected_vs.size() + m_hunter_collected_cs.size());

    auto add_row = [&](HunterStage stage, const std::string& hash, const HunterCollectedEntry& info, bool marked) {
        const uint64_t actual_hits = info.draw_hits + info.dispatch_hits;
        if (actual_hits == 0) {
            return;
        }

        const uint64_t activity_frame = info.last_draw_frame != 0 ? info.last_draw_frame : info.last_seen_frame;
        const uint64_t draw_age = activity_frame != 0 && m_frame >= activity_frame ? (m_frame - activity_frame) : 0;
        const bool scene_candidate = hunter_entry_is_scene_candidate_locked(info);
        const char* companion_stage =
            stage == HunterStage::Pixel ? "vertex_or_mesh" :
            stage == HunterStage::Vertex ? "pixel" :
            "compute";

        json row{};
        row["stage"] = hunter_stage_to_string(stage);
        row["hash"] = hash;
        row["crc32"] = info.crc32;
        row["companion_stage"] = companion_stage;
        row["companion_hash"] = info.vs_hash;
        row["bytecode_size"] = info.ps_size;
        row["bind_hits"] = info.hits;
        row["actual_hits"] = actual_hits;
        row["draw_hits"] = info.draw_hits;
        row["indexed_draw_hits"] = info.indexed_draw_hits;
        row["dispatch_hits"] = info.dispatch_hits;
        row["eye_hits"] = {
            {"unknown", info.eye_hits[0]},
            {"left", info.eye_hits[1]},
            {"right", info.eye_hits[2]},
            {"full", info.eye_hits[3]},
            {"multi", info.eye_hits[4]},
        };
        row["first_seen_frame"] = info.first_seen_frame;
        row["last_bind_frame"] = info.last_seen_frame;
        row["last_draw_frame"] = info.last_draw_frame;
        row["draw_age_frames"] = draw_age;
        row["last_pso"] = info.last_pso != 0 ? format_pointer_to_hex(info.last_pso) : std::string{};
        row["last_render_targets"] = info.last_render_targets;
        row["last_render_target_key"] = info.last_render_target_key;
        row["last_depth_target"] = info.last_depth_target;
        row["last_depth_target_key"] = info.last_depth_target_key;
        row["scene_candidate"] = scene_candidate;
        row["marked"] = marked;
        rows.push_back(std::move(row));
    };

    std::unordered_set<std::string> emitted{};
    emitted.reserve(rows.capacity());
    auto add_unique = [&](HunterStage stage, const std::string& hash, const HunterCollectedEntry& info, bool marked) {
        std::string key = hunter_stage_to_string(stage);
        key.push_back(':');
        key += hash;
        if (!emitted.insert(key).second) {
            return;
        }
        add_row(stage, hash, info, marked);
    };

    for (const auto& hash : m_hunter_order) {
        const auto it = m_hunter_collected.find(hash);
        if (it == m_hunter_collected.end()) {
            continue;
        }

        const HunterStage stage = it->second.stage == HunterStage::Compute
            ? HunterStage::Compute
            : HunterStage::Pixel;
        const bool marked = stage == HunterStage::Compute
            ? m_hunter_marked_cs.count(hash) > 0
            : m_hunter_marked.count(hash) > 0;
        add_unique(stage, hash, it->second, marked);
    }

    for (const auto& hash : m_hunter_order_vs) {
        const auto it = m_hunter_collected_vs.find(hash);
        if (it == m_hunter_collected_vs.end()) {
            continue;
        }
        add_unique(HunterStage::Vertex, hash, it->second, m_hunter_marked_vs.count(hash) > 0);
    }

    for (const auto& hash : m_hunter_order_cs) {
        const auto it = m_hunter_collected_cs.find(hash);
        if (it == m_hunter_collected_cs.end()) {
            continue;
        }
        add_unique(HunterStage::Compute, hash, it->second, m_hunter_marked_cs.count(hash) > 0);
    }

    std::sort(rows.begin(), rows.end(), [](const json& a, const json& b) {
        const uint64_t a_hits = a.value("actual_hits", uint64_t{0});
        const uint64_t b_hits = b.value("actual_hits", uint64_t{0});
        if (a_hits != b_hits) {
            return a_hits > b_hits;
        }
        const uint64_t a_age = a.value("draw_age_frames", uint64_t{0});
        const uint64_t b_age = b.value("draw_age_frames", uint64_t{0});
        if (a_age != b_age) {
            return a_age < b_age;
        }
        return a.value("hash", std::string{}) < b.value("hash", std::string{});
    });

    json doc{};
    doc["generated_by"] = "UEVR Shader Hunter";
    doc["frame"] = m_frame;
    doc["active"] = m_hunter_active.load(std::memory_order_relaxed);
    doc["recent_frame_age"] = m_hunter_recent_frame_age;
    doc["frame_window"] = m_hunter_frame_window;
    doc["window_stopped"] = m_hunter_window_stopped.load(std::memory_order_relaxed);
    doc["min_scene_bytecode_size"] = HUNTER_MIN_SCENE_PS_SIZE;
    doc["entry_count"] = rows.size();
    doc["entries"] = json::array();
    for (auto& row : rows) {
        doc["entries"].push_back(std::move(row));
    }

    const fs::path export_dir = Framework::get_persistent_dir("shader_hunter");
    std::error_code ec{};
    fs::create_directories(export_dir, ec);
    if (ec) {
        error_out = "Failed to create shader_hunter export dir: " + ec.message();
        return false;
    }

    out_path = export_dir / ("scene_shaders_frame_" + std::to_string(m_frame) + ".json");
    std::ofstream out{out_path, std::ios::binary | std::ios::trunc};
    if (!out) {
        error_out = "Failed to open scene shader export for writing: " + out_path.string();
        return false;
    }

    out << doc.dump(2) << "\n";
    if (!out) {
        error_out = "Failed while writing scene shader export: " + out_path.string();
        return false;
    }

    spdlog::info("[ShaderHunter] exported {} scene shader entries to {}", doc["entry_count"].get<size_t>(), out_path.string());
    error_out.clear();
    return true;
}

bool ShaderOverrideRegistry::hunter_save_marked_as_manifests(std::string& error_out) {
    std::scoped_lock _{m_mutex};
    if (m_hunter_marked.empty()) {
        error_out = "no marked shaders to save";
        return false;
    }
    namespace fs = std::filesystem;
    fs::path dir = profile_override_dir();
    std::error_code ec{};
    fs::create_directories(dir, ec);
    fs::path src_path = dir / "_uevr_hunter_discard.hlsl";
    if (!fs::exists(src_path, ec)) {
        std::ofstream out{src_path, std::ios::binary | std::ios::trunc};
        out << "float4 main() : SV_Target { discard; return float4(0,0,0,0); }\n";
    }
    int written = 0;
    for (const auto& hash : m_hunter_marked) {
        // Locate the CRC32 for this hash from the collected map
        uint32_t crc = 0;
        if (auto it = m_hunter_collected.find(hash); it != m_hunter_collected.end()) crc = it->second.crc32;
        char fname[128]{};
        std::snprintf(fname, sizeof(fname), "hunter_ps_%s.json", hash.c_str());
        fs::path mpath = dir / fname;
        std::ofstream mfile{mpath, std::ios::binary | std::ios::trunc};
        // Write a manifest that uses the FNV1a-64 hash. (CRC32 also works
        // via the same map since 8-hex-char target_hash is treated as CRC32,
        // but using the FNV hash makes the file deterministic from this
        // session's captures.)
        mfile << "{\n";
        mfile << "  \"backend\": \"dx12\",\n";
        mfile << "  \"stage\": \"pixel\",\n";
        mfile << "  \"target_hash\": \"" << hash << "\",\n";
        mfile << "  \"name\": \"hunter_ps_" << hash << "\",\n";
        mfile << "  \"enabled\": true,\n";
        mfile << "  \"entry_point\": \"main\",\n";
        mfile << "  \"profile\": \"ps_6_0\",\n";
        mfile << "  \"compiler\": \"dxc\",\n";
        mfile << "  \"source\": \"_uevr_hunter_discard.hlsl\",\n";
        mfile << "  \"_uevr_hunter_crc32\": \"" << std::hex << crc << std::dec << "\"\n";
        mfile << "}\n";
        ++written;
    }
    spdlog::info("[ShaderHunter] saved {} marked shader manifests to {}", written, dir.string());
    request_reload();
    return true;
}
} // namespace render
