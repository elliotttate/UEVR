// Sn2MeshDump.cpp — implementation
//
// Records the bind state of vertex/index buffers per draw for the configured
// target PSOs. Doesn't currently dump raw bytes (would need readback).
// Useful as a starting point — we can see what VB/IB views look like + the
// draw counts.

#include "Sn2MeshDump.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

#include <Windows.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace sn2_mesh_dump {

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
    std::mutex mu;
    std::unordered_map<uint32_t, uint64_t> counts;
};

State& state() { static State s; return s; }

}  // namespace

bool env_enabled() {
    static const bool e = []() {
        return !env_str("UEVR_SN2_MESH_DUMP_DIR").empty() &&
               !env_str("UEVR_SN2_MESH_DUMP_PS_CRCS").empty();
    }();
    return e;
}

const std::string& output_dir() {
    static const std::string s = env_str("UEVR_SN2_MESH_DUMP_DIR");
    return s;
}

uint64_t max_dumps_per_pso() {
    static const uint64_t v = []() -> uint64_t {
        const auto s = env_str("UEVR_SN2_MESH_DUMP_MAX");
        if (s.empty()) return 8;
        char* tail = nullptr;
        const auto n = std::strtoull(s.c_str(), &tail, 0);
        return (tail != s.c_str() && n > 0) ? n : 8;
    }();
    return v;
}

const std::unordered_set<uint32_t>& target_ps_crcs() {
    static const auto s = parse_crc_csv(env_str("UEVR_SN2_MESH_DUMP_PS_CRCS"));
    return s;
}

bool should_dump(uint32_t ps_crc) {
    if (!env_enabled()) return false;
    if (target_ps_crcs().count(ps_crc) == 0) return false;
    auto& s = state();
    std::scoped_lock _{s.mu};
    auto& c = s.counts[ps_crc];
    ++c;
    return c <= max_dumps_per_pso();
}

void note_draw(
    uint32_t ps_crc,
    void* pso_ptr,
    UINT index_count, UINT instance_count, UINT start_index, INT base_vertex,
    const D3D12_VERTEX_BUFFER_VIEW* vbs, UINT vb_count,
    const D3D12_INDEX_BUFFER_VIEW* ib)
{
    if (!should_dump(ps_crc)) return;
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(output_dir(), ec);

    auto& s = state();
    uint64_t seq;
    {
        std::scoped_lock _{s.mu};
        seq = s.counts[ps_crc];
    }

    nlohmann::json doc;
    doc["ps_crc"] = ps_crc;
    doc["pso_ptr"] = reinterpret_cast<uintptr_t>(pso_ptr);
    doc["seq"] = seq;
    doc["draw"] = {
        {"index_count", index_count},
        {"instance_count", instance_count},
        {"start_index", start_index},
        {"base_vertex", base_vertex}
    };
    nlohmann::json vbs_arr = nlohmann::json::array();
    for (UINT i = 0; i < vb_count; ++i) {
        if (vbs == nullptr) break;
        nlohmann::json v;
        v["slot"] = i;
        v["gpu_va"] = (uint64_t)vbs[i].BufferLocation;
        v["size_in_bytes"] = vbs[i].SizeInBytes;
        v["stride_in_bytes"] = vbs[i].StrideInBytes;
        vbs_arr.push_back(v);
    }
    doc["vertex_buffers"] = vbs_arr;
    if (ib != nullptr) {
        doc["index_buffer"] = {
            {"gpu_va", (uint64_t)ib->BufferLocation},
            {"size_in_bytes", ib->SizeInBytes},
            {"format", (unsigned)ib->Format}
        };
    } else {
        doc["index_buffer"] = nullptr;
    }

    char fname[256];
    std::snprintf(fname, sizeof(fname),
                  "mesh_0x%08x_seq%04llu.json",
                  ps_crc, (unsigned long long)seq);
    const auto path = (fs::path(output_dir()) / fname).string();
    std::ofstream f(path);
    if (f.good()) f << doc.dump(2);

    static std::atomic<uint64_t> total{0};
    const auto n = total.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 8 || (n % 100) == 0) {
        SPDLOG_WARN("[SN2-MeshDump] #{} wrote {}", n, path);
    }
}

}  // namespace sn2_mesh_dump
