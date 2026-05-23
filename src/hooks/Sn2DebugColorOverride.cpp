// Sn2DebugColorOverride.cpp — implementation
//
// Runtime PSO override: replace PS with magenta-output shader for configured
// PS CRCs. Caches the cloned PSO per source PSO so steady-state cost is just
// the map lookup.
//
// The replacement PS is a tiny precompiled DXBC blob that ignores all inputs
// and writes a configurable RGBA to SV_Target0. Compiled at startup via the
// embedded HLSL string + D3DCompile (link against d3dcompiler.lib).

#include "Sn2DebugColorOverride.hpp"

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <vector>

#include <Windows.h>
#include <d3dcompiler.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "render/StereoEye.hpp"
#include "render/StereoForensics.hpp"

#pragma comment(lib, "d3dcompiler.lib")

namespace sn2_debug_color {

namespace {

std::string env_str(const char* name) {
    char buf[2048]{};
    const auto len = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return {};
    return std::string{buf, len};
}

std::unordered_set<uint32_t> parse_crc_csv(const std::string& s) {
    std::unordered_set<uint32_t> out;
    size_t pos = 0;
    while (pos < s.size()) {
        while (pos < s.size() && (s[pos] == ',' || s[pos] == ' ' ||
                                   s[pos] == '\n' || s[pos] == '\r' ||
                                   s[pos] == '\t')) ++pos;
        if (pos >= s.size()) break;
        if (s[pos] == '#') {
            while (pos < s.size() && s[pos] != '\n') ++pos;
            continue;
        }
        size_t end = pos;
        while (end < s.size() && s[end] != ',' && s[end] != ' ' &&
               s[end] != '\n' && s[end] != '\r' && s[end] != '\t' && s[end] != '#') ++end;
        std::string tok = s.substr(pos, end - pos);
        pos = end;
        if (tok.empty()) continue;
        char* tail = nullptr;
        const auto v = std::strtoul(tok.c_str(), &tail, 0);
        if (tail != tok.c_str()) out.insert(static_cast<uint32_t>(v));
    }
    return out;
}

bool env_truthy(const char* name) {
    const auto v = env_str(name);
    return !v.empty() && v != "0" && v != "false" && v != "FALSE";
}

uint32_t parse_crc_json(const nlohmann::json& value) {
    if (value.is_number_unsigned()) {
        return static_cast<uint32_t>(value.get<uint64_t>());
    }
    if (value.is_string()) {
        const auto s = value.get<std::string>();
        char* tail = nullptr;
        const auto v = std::strtoul(s.c_str(), &tail, 0);
        return tail != s.c_str() ? static_cast<uint32_t>(v) : 0u;
    }
    return 0u;
}

int parse_eye_json(const nlohmann::json& value) {
    if (value.is_number_integer()) {
        return render::canonicalize_stereo_eye_bucket(value.get<int>(), render::kStereoEyeAny);
    }
    if (!value.is_string()) {
        return render::kStereoEyeAny;
    }
    return render::parse_stereo_eye_bucket(value.get<std::string>(), render::kStereoEyeAny);
}

struct Storage {
    std::mutex mu;
    std::unordered_set<uint32_t> live_set;
    std::unordered_map<uint32_t, int> forensics_color_eye_by_crc;
    std::unordered_map<uint32_t, std::vector<std::pair<int, std::string>>> forensics_color_rules_by_crc;
    int64_t last_mtime = 0;
    int64_t forensics_last_mtime = 0;
    std::atomic<uint64_t> poll_counter{0};

    // Per-original-PSO cache: PSO* → cloned replacement PSO
    std::unordered_map<ID3D12PipelineState*, Microsoft::WRL::ComPtr<ID3D12PipelineState>> replacements;

    // Cached original descs (we need them to clone with PS replaced).
    // Note: storing the FULL desc is expensive but necessary.
    struct OriginalDesc {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc;
        std::vector<uint8_t> vs_bc;   // owned copies because the original pointer is transient
        std::vector<uint8_t> ps_bc;
        std::vector<uint8_t> gs_bc;
        std::vector<uint8_t> hs_bc;
        std::vector<uint8_t> ds_bc;
        std::vector<uint8_t> rs_bc;   // root sig blob if STREAM PSO; else null
        std::vector<D3D12_INPUT_ELEMENT_DESC> input_layout;
        std::vector<std::string> semantic_names;
    };
    std::unordered_map<ID3D12PipelineState*, OriginalDesc> original_descs;

    // Compiled magenta PS bytecode (one copy, reused).
    Microsoft::WRL::ComPtr<ID3DBlob> magenta_ps_blob;
};

Storage& storage() { static Storage s; return s; }

// 6-MRT replacement that outputs teal to SV_Target0..4 and SV_Target6.
// Matches the SLW basepass MainPS (0xDE7C3822) output signature so D3D12
// pipeline validation accepts the substituted PSO. RGB tuned for the
// right-eye underwater fix: pushes the right eye towards left-eye teal.
constexpr const char* kMagentaHlsl = R"HLSL(
struct PSIn { float4 pos : SV_Position; };
struct PSOut {
    float4 t0 : SV_Target0;
    float4 t1 : SV_Target1;
    float4 t2 : SV_Target2;
    float4 t3 : SV_Target3;
    float4 t4 : SV_Target4;
    float4 t6 : SV_Target6;
};
PSOut main(PSIn i) {
    PSOut o;
    float4 teal = float4(0.05, 0.35, 0.45, 1.0);
    o.t0 = teal;
    o.t1 = teal;
    o.t2 = teal;
    o.t3 = teal;
    o.t4 = teal;
    o.t6 = teal;
    return o;
}
)HLSL";

bool compile_magenta_ps() {
    auto& s = storage();
    if (s.magenta_ps_blob != nullptr) return true;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompile(kMagentaHlsl, std::strlen(kMagentaHlsl),
                            "magenta_ps", nullptr, nullptr,
                            "main", "ps_5_0",
                            D3DCOMPILE_OPTIMIZATION_LEVEL3,
                            0, &s.magenta_ps_blob, &errors);
    if (FAILED(hr)) {
        SPDLOG_WARN("[SN2-DebugColor] magenta PS compile failed: {}",
                    errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()),
                                          errors->GetBufferSize()) : "?");
        return false;
    }
    return true;
}

}  // namespace

bool env_enabled() {
    static const bool e = []() {
        const bool x = !env_str("UEVR_SN2_DEBUG_COLOR_OVERRIDE_FILE").empty() ||
            (env_truthy("UEVR_STEREO_EXPERIMENTS") && !env_str("UEVR_STEREO_EXPERIMENTS_FILE").empty());
        SPDLOG_WARN("[SN2-DebugColor] env_enabled first-eval: {} file='{}' eye='{}'",
                    x ? "true" : "false",
                    env_str("UEVR_SN2_DEBUG_COLOR_OVERRIDE_FILE"),
                    env_str("UEVR_SN2_DEBUG_COLOR_OVERRIDE_EYE"));
        return x;
    }();
    return e;
}

const std::string& override_file_path() {
    static const std::string p = env_str("UEVR_SN2_DEBUG_COLOR_OVERRIDE_FILE");
    return p;
}

int target_eye_bucket() {
    static const int v = []() -> int {
        const auto s = env_str("UEVR_SN2_DEBUG_COLOR_OVERRIDE_EYE");
        if (s == "left")  return 1;
        if (s == "right") return 2;
        return -1;
    }();
    return v;
}

void refresh_forensics_color_rules() {
    if (!env_truthy("UEVR_STEREO_EXPERIMENTS")) return;
    const auto path = env_str("UEVR_STEREO_EXPERIMENTS_FILE");
    if (path.empty()) return;

    auto& s = storage();
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fad)) {
        return;
    }
    const int64_t mtime = (static_cast<int64_t>(fad.ftLastWriteTime.dwHighDateTime) << 32)
                          | fad.ftLastWriteTime.dwLowDateTime;
    if (mtime == s.forensics_last_mtime) {
        return;
    }

    try {
        std::ifstream f(path);
        if (!f.good()) return;
        const auto doc = nlohmann::json::parse(f);
        const auto& arr = doc.contains("rules")
            ? doc.at("rules")
            : (doc.contains("experiments") ? doc.at("experiments") : doc);
        if (!arr.is_array()) return;

        std::unordered_map<uint32_t, int> parsed;
        std::unordered_map<uint32_t, std::vector<std::pair<int, std::string>>> parsed_rules;
        for (const auto& item : arr) {
            if (!item.value("enabled", true)) continue;
            const auto match = item.value("match", nlohmann::json::object());
            const auto action = item.value("action", nlohmann::json{});
            std::string action_type;
            if (action.is_string()) {
                action_type = action.get<std::string>();
            } else if (action.is_object()) {
                action_type = action.value("type", std::string{});
            }
            if (action_type != "color_override") continue;
            const auto ps_crc = parse_crc_json(match.value("ps_crc", item.value("ps_crc", nlohmann::json{})));
            if (ps_crc == 0) continue;
            const int eye = parse_eye_json(match.value("eye", item.value("eye", nlohmann::json{"both"})));
            parsed[ps_crc] = eye;
            parsed_rules[ps_crc].push_back({eye, item.value("name", std::string{"color_override_" + std::to_string(ps_crc)})});
        }

        std::scoped_lock _{s.mu};
        s.forensics_color_eye_by_crc = std::move(parsed);
        s.forensics_color_rules_by_crc = std::move(parsed_rules);
        s.forensics_last_mtime = mtime;
    } catch (const std::exception& e) {
        SPDLOG_WARN("[SN2-DebugColor] failed to parse Stereo Forensics color rules: {}", e.what());
    }
}

std::unordered_set<uint32_t> override_crcs() {
    if (!env_enabled()) return {};
    auto& s = storage();
    const auto n = s.poll_counter.fetch_add(1, std::memory_order_relaxed);
    if ((n % 60) == 0) {
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (GetFileAttributesExA(override_file_path().c_str(), GetFileExInfoStandard, &fad)) {
            const int64_t mtime = (static_cast<int64_t>(fad.ftLastWriteTime.dwHighDateTime) << 32)
                                  | fad.ftLastWriteTime.dwLowDateTime;
            if (mtime != s.last_mtime) {
                std::ifstream f(override_file_path());
                if (f.good()) {
                    std::string content((std::istreambuf_iterator<char>(f)),
                                         std::istreambuf_iterator<char>());
                    auto parsed = parse_crc_csv(content);
                    std::scoped_lock _{s.mu};
                    s.live_set = std::move(parsed);
                    s.last_mtime = mtime;
                }
            }
        }
        refresh_forensics_color_rules();
    }
    std::scoped_lock _{s.mu};
    auto out = s.live_set;
    for (const auto& [crc, _eye] : s.forensics_color_eye_by_crc) {
        out.insert(crc);
    }
    return out;
}

bool should_override(uint32_t ps_crc, int eye_bucket) {
    if (!env_enabled()) return false;
    (void)override_crcs();
    auto& s = storage();
    std::scoped_lock _{s.mu};
    if (s.live_set.find(ps_crc) != s.live_set.end()) {
        const int t = target_eye_bucket();
        return t == -1 || eye_bucket == t;
    }
    const auto it = s.forensics_color_eye_by_crc.find(ps_crc);
    if (it == s.forensics_color_eye_by_crc.end()) {
        return false;
    }
    return it->second == -1 || it->second == eye_bucket;
}

void note_override_applied(uint32_t ps_crc, int eye_bucket, const char* kind) {
    if (!env_enabled() || ps_crc == 0) {
        return;
    }
    (void)override_crcs();

    std::vector<std::string> names;
    {
        auto& s = storage();
        std::scoped_lock _{s.mu};
        const auto it = s.forensics_color_rules_by_crc.find(ps_crc);
        if (it != s.forensics_color_rules_by_crc.end()) {
            for (const auto& [eye, name] : it->second) {
                if (eye == render::kStereoEyeAny || eye == eye_bucket) {
                    names.push_back(name);
                }
            }
        }
    }
    for (const auto& name : names) {
        render::StereoForensics::get().record_experiment_observation(
            name,
            "color_override",
            "applied",
            kind != nullptr ? kind : "draw_indexed",
            ps_crc,
            0,
            eye_bucket);
    }
}

void note_create_graphics_pso(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,
                              ID3D12PipelineState* pso) {
    if (!env_enabled() || desc == nullptr || pso == nullptr) return;
    auto& s = storage();
    Storage::OriginalDesc od{};
    od.desc = *desc;
    // Deep-copy the bytecode blobs (the pointers in `desc` are caller-owned
    // and likely transient).
    auto copy_bc = [](const D3D12_SHADER_BYTECODE& sb, std::vector<uint8_t>& dst) {
        if (sb.pShaderBytecode == nullptr || sb.BytecodeLength == 0) {
            dst.clear();
            return;
        }
        dst.assign(static_cast<const uint8_t*>(sb.pShaderBytecode),
                   static_cast<const uint8_t*>(sb.pShaderBytecode) + sb.BytecodeLength);
    };
    copy_bc(desc->VS, od.vs_bc);
    copy_bc(desc->PS, od.ps_bc);
    copy_bc(desc->GS, od.gs_bc);
    copy_bc(desc->HS, od.hs_bc);
    copy_bc(desc->DS, od.ds_bc);
    // Input layout copy
    od.input_layout.assign(desc->InputLayout.pInputElementDescs,
                           desc->InputLayout.pInputElementDescs + desc->InputLayout.NumElements);
    od.semantic_names.reserve(od.input_layout.size());
    for (auto& el : od.input_layout) {
        od.semantic_names.emplace_back(el.SemanticName ? el.SemanticName : "");
        el.SemanticName = od.semantic_names.back().c_str();
    }
    std::scoped_lock _{s.mu};
    s.original_descs[pso] = std::move(od);
}

ID3D12PipelineState* get_or_create_replacement(ID3D12Device* device,
                                                ID3D12PipelineState* original) {
    if (device == nullptr || original == nullptr) return nullptr;
    if (!compile_magenta_ps()) return nullptr;
    auto& s = storage();
    {
        std::scoped_lock _{s.mu};
        auto it = s.replacements.find(original);
        if (it != s.replacements.end()) return it->second.Get();
    }
    // Build replacement desc from cached original.
    Storage::OriginalDesc od;
    {
        std::scoped_lock _{s.mu};
        auto it = s.original_descs.find(original);
        if (it == s.original_descs.end()) return nullptr;
        od = it->second;
    }
    auto pin_bc = [](const std::vector<uint8_t>& v) -> D3D12_SHADER_BYTECODE {
        D3D12_SHADER_BYTECODE sb{};
        sb.pShaderBytecode = v.empty() ? nullptr : v.data();
        sb.BytecodeLength = v.size();
        return sb;
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = od.desc;
    desc.VS = pin_bc(od.vs_bc);
    desc.GS = pin_bc(od.gs_bc);
    desc.HS = pin_bc(od.hs_bc);
    desc.DS = pin_bc(od.ds_bc);
    desc.PS.pShaderBytecode = s.magenta_ps_blob->GetBufferPointer();
    desc.PS.BytecodeLength  = s.magenta_ps_blob->GetBufferSize();
    desc.InputLayout.pInputElementDescs = od.input_layout.empty() ? nullptr : od.input_layout.data();
    desc.InputLayout.NumElements = static_cast<UINT>(od.input_layout.size());
    // CachedPSO not used for new PSO
    desc.CachedPSO.pCachedBlob = nullptr;
    desc.CachedPSO.CachedBlobSizeInBytes = 0;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> replacement;
    HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&replacement));
    if (FAILED(hr)) {
        SPDLOG_WARN("[SN2-DebugColor] CreateGraphicsPipelineState(replacement) hr=0x{:08x} for pso=0x{:x}",
                    static_cast<uint32_t>(hr), reinterpret_cast<uintptr_t>(original));
        return nullptr;
    }
    static std::atomic<uint64_t> seq{0};
    const auto n = seq.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 16 || (n % 100) == 0) {
        SPDLOG_WARN("[SN2-DebugColor] #{} created magenta replacement for pso=0x{:x}",
                    n, reinterpret_cast<uintptr_t>(original));
    }
    {
        std::scoped_lock _{s.mu};
        s.replacements[original] = replacement;
    }
    return replacement.Get();
}

}  // namespace sn2_debug_color
