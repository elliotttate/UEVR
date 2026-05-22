// Sn2PsoBytecodeDumper.cpp

#include "Sn2PsoBytecodeDumper.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_set>

#include <spdlog/spdlog.h>

namespace sn2_pso_bytecode_dumper {

namespace {

std::string& output_dir() {
    static std::string s = []() -> std::string {
        char buf[2048]{};
        const auto len = GetEnvironmentVariableA("UEVR_SN2_PSO_BYTECODE_DIR", buf, sizeof(buf));
        if (len == 0 || len >= sizeof(buf)) return std::string{};
        return std::string{buf, len};
    }();
    return s;
}

struct State {
    std::mutex mu;
    std::unordered_set<uint32_t> seen;
    bool dir_created{false};
};

State& state() {
    static State s;
    return s;
}

void ensure_dir() {
    auto& s = state();
    if (s.dir_created) return;
    if (output_dir().empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(output_dir(), ec);
    s.dir_created = true;
}

void dump_one(const void* bytecode, size_t size, uint32_t crc, const char* suffix) {
    if (bytecode == nullptr || size == 0 || crc == 0) return;
    auto& s = state();
    const uint32_t key = crc ^ static_cast<uint32_t>(std::hash<std::string>{}(suffix));
    {
        std::scoped_lock _{s.mu};
        if (s.seen.find(key) != s.seen.end()) return;
        s.seen.insert(key);
    }
    ensure_dir();
    char path[1024];
    std::snprintf(path, sizeof(path), "%s\\%s_0x%08x.dxbc",
                  output_dir().c_str(), suffix, crc);
    try {
        std::ofstream f{path, std::ios::binary};
        if (f.good()) {
            f.write(reinterpret_cast<const char*>(bytecode), size);
            SPDLOG_INFO("[SN2-PsoDump] {} 0x{:08x} ({} bytes) -> {}",
                        suffix, crc, size, path);
        }
    } catch (...) {}
}

}  // namespace

void on_create_graphics_pso(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,
                            uint32_t ps_crc, uint32_t vs_crc) {
    if (!env_enabled() || desc == nullptr) return;
    dump_one(desc->PS.pShaderBytecode, desc->PS.BytecodeLength, ps_crc, "ps");
    dump_one(desc->VS.pShaderBytecode, desc->VS.BytecodeLength, vs_crc, "vs");
    if (desc->GS.BytecodeLength > 0) {
        dump_one(desc->GS.pShaderBytecode, desc->GS.BytecodeLength, ps_crc, "gs");
    }
}

void on_create_compute_pso(const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc,
                            uint32_t cs_crc) {
    if (!env_enabled() || desc == nullptr) return;
    dump_one(desc->CS.pShaderBytecode, desc->CS.BytecodeLength, cs_crc, "cs");
}

void on_create_pso_stream(const void* ps, size_t ps_size, uint32_t ps_crc,
                          const void* vs, size_t vs_size, uint32_t vs_crc,
                          const void* cs, size_t cs_size, uint32_t cs_crc) {
    if (!env_enabled()) return;
    dump_one(ps, ps_size, ps_crc, "ps");
    dump_one(vs, vs_size, vs_crc, "vs");
    dump_one(cs, cs_size, cs_crc, "cs");
}

}  // namespace sn2_pso_bytecode_dumper
