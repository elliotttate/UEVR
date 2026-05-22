// Sn2CbDumper.cpp — implementation

#include "Sn2CbDumper.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

#include <spdlog/spdlog.h>

#include <d3d12.h>

namespace sn2_upload_buf_map {
uint8_t* gpu_va_to_cpu(D3D12_GPU_VIRTUAL_ADDRESS gpu_va, uint64_t min_size);
bool resolve_va(D3D12_GPU_VIRTUAL_ADDRESS gpu_va,
                ID3D12Resource*& out_resource, uint64_t& out_offset);
}

namespace sn2_cb_dumper {

namespace {

std::string& output_dir() {
    static std::string s = []() -> std::string {
        char buf[2048]{};
        const auto len = GetEnvironmentVariableA("UEVR_SN2_CB_DUMP_DIR", buf, sizeof(buf));
        if (len == 0 || len >= sizeof(buf)) return std::string{};
        return std::string{buf, len};
    }();
    return s;
}

struct Key {
    uint32_t crc;
    uint32_t root;
    int eye;
    char stage;
    bool operator==(const Key& o) const noexcept {
        return crc == o.crc && root == o.root && eye == o.eye && stage == o.stage;
    }
};

struct KeyHash {
    std::size_t operator()(const Key& k) const noexcept {
        return std::hash<uint32_t>{}(k.crc) ^
               (std::hash<uint32_t>{}(k.root) << 1) ^
               (std::hash<int>{}(k.eye) << 7) ^
               (static_cast<std::size_t>(k.stage) << 11);
    }
};

struct State {
    std::mutex mu;
    std::unordered_map<Key, uint32_t, KeyHash> counts;
    bool dir_created{false};
};

State& state() {
    static State s;
    return s;
}

void ensure_dir() {
    auto& s = state();
    std::scoped_lock _{s.mu};
    if (s.dir_created) return;
    if (output_dir().empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(output_dir(), ec);
    s.dir_created = true;
}

}  // namespace

void record_cbv_bind(uint32_t crc,
                     int eye_bucket,
                     uint32_t root_param,
                     D3D12_GPU_VIRTUAL_ADDRESS gpu_va,
                     char stage) {
    if (!env_enabled()) return;
    if (crc == 0 || gpu_va == 0) return;
    if (eye_bucket < 0) return;  // skip unknown-eye dispatches; not useful for diff
    if (!target_crcs().empty() && target_crcs().find(crc) == target_crcs().end()) return;
    if (target_roots().find(root_param) == target_roots().end()) return;

    const Key k{crc, root_param, eye_bucket, stage};
    auto& s = state();
    uint32_t seq = 0;
    {
        std::scoped_lock _{s.mu};
        auto& n = s.counts[k];
        if (n >= max_per_key()) return;
        seq = ++n;
    }

    // Resolve the binding to find the actual size of the parent buffer (so we
    // don't read past it).
    ID3D12Resource* parent = nullptr;
    uint64_t parent_offs = 0;
    uint64_t parent_size = 0;
    if (sn2_upload_buf_map::resolve_va(gpu_va, parent, parent_offs)) {
        if (parent != nullptr) {
            parent_size = parent->GetDesc().Width;
        }
    }

    const uint64_t avail = (parent_size > parent_offs) ? (parent_size - parent_offs) : 0;
    uint64_t bytes_to_read = max_bytes();
    if (avail > 0 && bytes_to_read > avail) bytes_to_read = avail;
    if (bytes_to_read == 0) return;

    uint8_t* cpu = sn2_upload_buf_map::gpu_va_to_cpu(gpu_va, bytes_to_read);
    if (cpu == nullptr) {
        // CPU pointer not available (buffer wasn't Map'd through our hook).
        // Skip — we'd need a CopyResource + readback to dump GPU-only content.
        return;
    }

    ensure_dir();

    // Filenames: pso_0x{crc}_root{n}_eye{e}_seq{NNNN}.bin/.json
    char bin_path[1024];
    char json_path[1024];
    std::snprintf(bin_path, sizeof(bin_path),
                  "%s\\%c_0x%08x_root%u_eye%d_seq%04u.bin",
                  output_dir().c_str(), stage, crc, root_param, eye_bucket, seq);
    std::snprintf(json_path, sizeof(json_path),
                  "%s\\%c_0x%08x_root%u_eye%d_seq%04u.json",
                  output_dir().c_str(), stage, crc, root_param, eye_bucket, seq);

    // Write bytes.
    try {
        std::ofstream f{bin_path, std::ios::binary};
        if (f.good()) {
            f.write(reinterpret_cast<const char*>(cpu), bytes_to_read);
        }
    } catch (...) {
        // Memory may have been remapped or freed; swallow.
        return;
    }

    // Write metadata.
    try {
        std::ofstream f{json_path};
        if (f.good()) {
            f << "{\"crc\":\"0x" << std::hex << crc << std::dec << "\","
              << "\"root\":" << root_param << ","
              << "\"eye\":" << eye_bucket << ","
              << "\"stage\":\"" << stage << "\","
              << "\"seq\":" << seq << ","
              << "\"gpu_va\":\"0x" << std::hex << gpu_va << std::dec << "\","
              << "\"parent_offset\":" << parent_offs << ","
              << "\"parent_size\":" << parent_size << ","
              << "\"bytes_dumped\":" << bytes_to_read << "}";
        }
    } catch (...) {}

    // Log first few hits.
    if (seq == 1) {
        SPDLOG_INFO("[SN2-CbDump] crc=0x{:08x} root={} eye={} stage={} seq=1 bytes={} -> {}",
                    crc, root_param, eye_bucket, stage, bytes_to_read, bin_path);
    }
}

}  // namespace sn2_cb_dumper
