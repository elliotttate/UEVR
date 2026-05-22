// Sn2StateInspector.cpp — full JSON dump implementation
//
// Writes a structured snapshot of the GPU state at a matching draw call to
// disk, including every CBV/SRV/UAV/RTV root binding with resolved resource
// pointers, dimensions, and formats.
//
// The caller (D3D12Hook.cpp) prepares the binding data and calls
// write_snapshot(); we serialize via nlohmann::json and write a file per
// matching draw.

#include "Sn2StateInspector.hpp"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include <Windows.h>
#include <d3d12.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace sn2_state_inspector {

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
        size_t end = pos;
        while (end < s.size() && s[end] != ',' && s[end] != ' ' &&
               s[end] != '\n' && s[end] != '\r' && s[end] != '\t') ++end;
        std::string tok = s.substr(pos, end - pos);
        pos = end;
        if (tok.empty()) continue;
        char* tail = nullptr;
        const auto v = std::strtoul(tok.c_str(), &tail, 0);
        if (tail != tok.c_str()) out.insert(static_cast<uint32_t>(v));
    }
    return out;
}

struct State {
    std::unordered_map<uint32_t, std::atomic<uint64_t>> counts;
    std::mutex counts_mu;
};

State& state() { static State s; return s; }

}  // namespace

bool env_enabled() {
    static const bool e = []() {
        return !env_str("UEVR_SN2_STATE_INSPECTOR_DIR").empty()
            && !env_str("UEVR_SN2_STATE_INSPECTOR_PS_CRCS").empty();
    }();
    return e;
}

const std::string& output_dir() {
    static const std::string s = env_str("UEVR_SN2_STATE_INSPECTOR_DIR");
    return s;
}

const std::unordered_set<uint32_t>& target_ps_crcs() {
    static const auto s = parse_crc_csv(env_str("UEVR_SN2_STATE_INSPECTOR_PS_CRCS"));
    return s;
}

int target_eye_bucket() {
    static const int v = []() -> int {
        const auto s = env_str("UEVR_SN2_STATE_INSPECTOR_EYE");
        if (s == "left") return 1;
        if (s == "right") return 2;
        return -1;
    }();
    return v;
}

uint64_t max_dumps_per_crc() {
    static const uint64_t v = []() -> uint64_t {
        const auto s = env_str("UEVR_SN2_STATE_INSPECTOR_MAX");
        if (s.empty()) return 32;
        char* tail = nullptr;
        const auto n = std::strtoull(s.c_str(), &tail, 0);
        return (tail != s.c_str()) ? n : 32;
    }();
    return v;
}

bool should_dump(uint32_t ps_crc, int eye_bucket) {
    if (!env_enabled()) return false;
    if (target_ps_crcs().count(ps_crc) == 0) return false;
    const int targ = target_eye_bucket();
    if (targ != -1 && eye_bucket != targ) return false;
    auto& s = state();
    std::scoped_lock _{s.counts_mu};
    auto& c = s.counts[ps_crc];
    const auto n = c.fetch_add(1, std::memory_order_relaxed) + 1;
    return n <= max_dumps_per_crc();
}

// Serialize a resource's basic descriptor data to JSON.
nlohmann::json resource_to_json(ID3D12Resource* res) {
    nlohmann::json j;
    if (res == nullptr) {
        j["ptr"] = nullptr;
        return j;
    }
    j["ptr"] = reinterpret_cast<uintptr_t>(res);
    const auto desc = res->GetDesc();
    j["dim_type"] = static_cast<unsigned>(desc.Dimension);
    j["width"] = static_cast<unsigned>(desc.Width);
    j["height"] = static_cast<unsigned>(desc.Height);
    j["depth_or_array"] = static_cast<unsigned>(desc.DepthOrArraySize);
    j["format"] = static_cast<unsigned>(desc.Format);
    j["flags"] = static_cast<unsigned>(desc.Flags);
    j["gpu_va"] = static_cast<uint64_t>(res->GetGPUVirtualAddress());
    return j;
}

void write_snapshot(
    uint32_t ps_crc,
    int eye_bucket,
    void* pso_ptr,
    uint32_t viewport_x, uint32_t viewport_y,
    uint32_t viewport_w, uint32_t viewport_h,
    uint32_t rtv_count,
    ID3D12Resource** rtv_resources,   // array of size rtv_count
    const BindingEntry* graphics_bindings,
    size_t graphics_binding_count)
{
    namespace fs = std::filesystem;
    const auto& dir = output_dir();
    if (dir.empty()) return;
    std::error_code ec;
    fs::create_directories(dir, ec);

    auto& s = state();
    uint64_t seq;
    {
        std::scoped_lock _{s.counts_mu};
        seq = s.counts[ps_crc].load(std::memory_order_relaxed);
    }

    nlohmann::json doc;
    doc["ps_crc"] = ps_crc;
    doc["eye_bucket"] = eye_bucket;
    doc["pso_ptr"] = reinterpret_cast<uintptr_t>(pso_ptr);
    doc["seq"] = seq;
    doc["viewport"] = {
        {"x", viewport_x}, {"y", viewport_y},
        {"w", viewport_w}, {"h", viewport_h}
    };

    // RTVs
    nlohmann::json rtv_arr = nlohmann::json::array();
    for (uint32_t i = 0; i < rtv_count; ++i) {
        nlohmann::json e;
        e["slot"] = i;
        e["resource"] = resource_to_json(rtv_resources[i]);
        rtv_arr.push_back(e);
    }
    doc["rtv"] = rtv_arr;

    // Graphics root bindings
    nlohmann::json bindings = nlohmann::json::array();
    for (size_t i = 0; i < graphics_binding_count; ++i) {
        const auto& b = graphics_bindings[i];
        nlohmann::json e;
        static const char* kKindNames[] = {
            "cbv", "srv", "uav", "rtv", "dsv",
            "root_desc_table", "root_cbv", "root_srv", "root_uav"
        };
        e["kind"] = (b.kind < 9) ? kKindNames[b.kind] : "unknown";
        e["slot"] = b.slot;
        e["gpu_va"] = b.gpu_va;
        e["resource"] = resource_to_json(b.resource);
        bindings.push_back(e);
    }
    doc["graphics_root"] = bindings;

    char fname[256];
    std::snprintf(fname, sizeof(fname),
                  "state_0x%08x_eye%d_seq%04llu.json",
                  ps_crc, eye_bucket, static_cast<unsigned long long>(seq));
    const auto path = (fs::path(dir) / fname).string();
    std::ofstream f(path);
    if (f.good()) {
        f << doc.dump(2);
    }
    static std::atomic<uint64_t> total{0};
    const auto n = total.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 8 || (n % 100) == 0) {
        SPDLOG_WARN("[SN2-StateInspector] #{} wrote {}", n, path);
    }
}

}  // namespace sn2_state_inspector
