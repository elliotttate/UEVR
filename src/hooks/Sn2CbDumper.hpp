// Sn2CbDumper.hpp
//
// Live constant-buffer content dumper. When a configured (PSO, root_param)
// CBV bind fires, the parent buffer's bytes at the bind offset are dumped to
// disk as a binary blob plus a tiny metadata sidecar (eye, pso, root, size).
//
// Together with offline diff_cb_dumps.py this gives us:
//   For each (PSO, root_param):
//     LEFT bytes (from eye=0 dumps)
//     RIGHT bytes (from eye=1 dumps)
//     byte_offset → divergent? map (which fields are per-eye)
//
// Knowing the divergent fields means we can SYNTHESIZE a right-eye CB from
// LEFT bytes by patching only those fields — finally unblocking writer-dup
// for the 144 LEFT-only PSes that currently have no right CBV.
//
// CONFIG (env-gated, zero overhead when disabled)
//   UEVR_SN2_CB_DUMP_DIR=C:\tmp\cb_dumps          — output directory
//   UEVR_SN2_CB_DUMP_PSOS=0x13b00f0c,0xd3ab43c5   — CSV of PS/CS CRCs to dump
//   UEVR_SN2_CB_DUMP_ROOTS=3,4,5                  — CSV of root params to dump
//   UEVR_SN2_CB_DUMP_MAX_PER_KEY=8                — cap per (pso,root,eye)
//   UEVR_SN2_CB_DUMP_MAX_BYTES=4096               — cap dump size per binding
//
// USAGE
// -----
// 1. Set the env vars, run the game in a scene that exercises both eyes
// 2. Files appear in the dump dir as
//      pso_0x13b00f0c_root3_eye0_seq0001.bin
//      pso_0x13b00f0c_root3_eye0_seq0001.json  (metadata)
//      pso_0x13b00f0c_root3_eye1_seq0001.bin
// 3. Run diff_cb_dumps.py — emits divergent-field report

#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <d3d12.h>

namespace sn2_cb_dumper {

inline bool env_enabled() {
    static const bool e = []() {
        return GetEnvironmentVariableW(L"UEVR_SN2_CB_DUMP_DIR", nullptr, 0) > 0;
    }();
    return e;
}

// Parse CSV from a long env var (path-sized).
inline std::vector<uint32_t> parse_csv_u32_env(const char* name) {
    std::vector<uint32_t> out;
    char buf[2048]{};
    const auto len = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return out;
    std::string s{buf, len};
    size_t pos = 0;
    while (pos < s.size()) {
        while (pos < s.size() && (s[pos] == ',' || s[pos] == ' ' || s[pos] == ';')) ++pos;
        if (pos >= s.size()) break;
        size_t end = pos;
        while (end < s.size() && s[end] != ',' && s[end] != ' ' && s[end] != ';') ++end;
        std::string tok = s.substr(pos, end - pos);
        if (!tok.empty()) {
            char* tail = nullptr;
            const auto v = std::strtoul(tok.c_str(), &tail, 0);
            if (tail != tok.c_str()) out.push_back(static_cast<uint32_t>(v));
        }
        pos = end;
    }
    return out;
}

inline const std::unordered_set<uint32_t>& target_crcs() {
    static const std::unordered_set<uint32_t> set = []() {
        auto v = parse_csv_u32_env("UEVR_SN2_CB_DUMP_PSOS");
        return std::unordered_set<uint32_t>{v.begin(), v.end()};
    }();
    return set;
}

inline const std::unordered_set<uint32_t>& target_roots() {
    static const std::unordered_set<uint32_t> set = []() {
        auto v = parse_csv_u32_env("UEVR_SN2_CB_DUMP_ROOTS");
        if (v.empty()) {
            // Default: dump all roots if none specified.
            for (uint32_t i = 0; i < 16; ++i) v.push_back(i);
        }
        return std::unordered_set<uint32_t>{v.begin(), v.end()};
    }();
    return set;
}

inline uint32_t max_per_key() {
    static const uint32_t n = []() -> uint32_t {
        char buf[32]{};
        const auto len = GetEnvironmentVariableA("UEVR_SN2_CB_DUMP_MAX_PER_KEY", buf, sizeof(buf));
        if (len == 0 || len >= sizeof(buf)) return 8;
        char* tail = nullptr;
        const auto v = std::strtoul(buf, &tail, 0);
        return v == 0 ? 8 : v;
    }();
    return n;
}

inline uint32_t max_bytes() {
    static const uint32_t n = []() -> uint32_t {
        char buf[32]{};
        const auto len = GetEnvironmentVariableA("UEVR_SN2_CB_DUMP_MAX_BYTES", buf, sizeof(buf));
        if (len == 0 || len >= sizeof(buf)) return 4096;
        char* tail = nullptr;
        const auto v = std::strtoul(buf, &tail, 0);
        return v == 0 ? 4096 : v;
    }();
    return n;
}

// Hot path: called from each CBV-bind hook AFTER state has been recorded.
// `crc` is PS CRC for graphics, CS CRC for compute.
void record_cbv_bind(uint32_t crc,
                     int eye_bucket,
                     uint32_t root_param,
                     D3D12_GPU_VIRTUAL_ADDRESS gpu_va,
                     char stage);

}  // namespace sn2_cb_dumper
