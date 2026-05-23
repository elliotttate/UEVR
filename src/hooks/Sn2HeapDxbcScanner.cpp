// Sn2HeapDxbcScanner.cpp — implementation

#include "Sn2HeapDxbcScanner.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <Windows.h>
#include <spdlog/spdlog.h>

namespace sn2_heap_dxbc_scanner {

namespace {

// DXBC container magic = 'D','X','B','C' little-endian uint32.
constexpr uint32_t kDxbcMagic = 0x43425844u;

// Sanity bounds. Real shaders are between a few hundred bytes and ~256 KB.
constexpr uint32_t kMinContainerSize = 64;
constexpr uint32_t kMaxContainerSize = 1024 * 1024;  // 1 MB hard cap
constexpr uint32_t kMaxChunkCount = 64;

inline std::string env_str(const char* name) {
    char buf[2048]{};
    const auto len = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return {};
    return std::string{buf, len};
}

inline bool env_flag(const char* name) {
    char buf[8]{};
    const auto len = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return false;
    return buf[0] && buf[0] != '0';
}

const std::string& trigger_path() {
    static const std::string s = []() -> std::string {
        const auto v = env_str("UEVR_SN2_HEAP_SCAN_TRIGGER_FILE");
        return v.empty() ? std::string{"C:\\tmp\\scan_dxbc.txt"} : v;
    }();
    return s;
}

const std::string& out_dir() {
    static const std::string s = []() -> std::string {
        const auto v = env_str("UEVR_SN2_HEAP_SCAN_OUT_DIR");
        return v.empty() ? std::string{"C:\\tmp\\dxbc_scan"} : v;
    }();
    return s;
}

const std::unordered_set<uint32_t>& target_crcs() {
    static const auto s = []() -> std::unordered_set<uint32_t> {
        std::unordered_set<uint32_t> out;
        const auto v = env_str("UEVR_SN2_HEAP_SCAN_CRCS");
        if (v.empty()) return out;
        size_t pos = 0;
        while (pos < v.size()) {
            while (pos < v.size() && (v[pos] == ',' || v[pos] == ' ' || v[pos] == '\t' ||
                                       v[pos] == '\n' || v[pos] == '\r')) ++pos;
            if (pos >= v.size()) break;
            size_t end = pos;
            while (end < v.size() && v[end] != ',' && v[end] != ' ' && v[end] != '\t' &&
                   v[end] != '\n' && v[end] != '\r') ++end;
            std::string tok = v.substr(pos, end - pos);
            pos = end;
            if (tok.empty()) continue;
            char* tail = nullptr;
            const auto x = std::strtoul(tok.c_str(), &tail, 0);
            if (tail != tok.c_str()) out.insert(static_cast<uint32_t>(x));
        }
        return out;
    }();
    return s;
}

// IEEE 802.3 CRC-32 (polynomial 0xEDB88320). Matches UEVR's PS CRC computation
// in ShaderOverrideRegistry::d3d12_pso_pixel_crc32.
uint32_t crc32_ieee(const void* data, size_t n) {
    static uint32_t table[256] = {};
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) {
        crc = table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

struct State {
    std::mutex mu;
    bool init_attempted = false;
    std::unordered_set<uint32_t> dumped_crcs;
    std::atomic<bool> scan_pending{false};
    std::atomic<bool> scan_running{false};
};

State& state() {
    static State s;
    return s;
}

// SEH-guarded read. Process memory enumerated via VirtualQuery can race with
// the game freeing pages between query and read; never trust the protection
// bits to be stable.
bool safe_memcmp4(const void* p, uint32_t expected) {
    __try {
        return *static_cast<const uint32_t*>(p) == expected;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool safe_read(void* dst, const void* src, size_t bytes) {
    __try {
        std::memcpy(dst, src, bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool validate_container_header(const uint8_t* p, size_t region_remaining,
                               uint32_t& out_size, uint32_t& out_chunk_count) {
    // Need at least 32 bytes for the basic header.
    if (region_remaining < 32) return false;
    uint8_t hdr[32];
    if (!safe_read(hdr, p, 32)) return false;
    if (*reinterpret_cast<const uint32_t*>(hdr + 0) != kDxbcMagic) return false;
    // bytes 4..19 = checksum (we don't validate it; the game's compiler
    // produced it and we don't want to reject our match here)
    // bytes 20..23 = container version; real DXBCs use 0x00000001
    const uint32_t version = *reinterpret_cast<const uint32_t*>(hdr + 20);
    if (version != 0x00000001u) return false;
    const uint32_t size = *reinterpret_cast<const uint32_t*>(hdr + 24);
    if (size < kMinContainerSize || size > kMaxContainerSize) return false;
    if (size > region_remaining) return false;
    const uint32_t chunk_count = *reinterpret_cast<const uint32_t*>(hdr + 28);
    if (chunk_count == 0 || chunk_count > kMaxChunkCount) return false;
    // chunk offset table must fit before size.
    if (32 + chunk_count * 4 > size) return false;
    // Optional sanity: read chunk offsets and ensure they're inside [32+CO, size-4)
    if (32 + chunk_count * 4 > region_remaining) return false;
    std::vector<uint32_t> offsets(chunk_count);
    if (!safe_read(offsets.data(), p + 32, chunk_count * 4)) return false;
    for (uint32_t i = 0; i < chunk_count; ++i) {
        if (offsets[i] < 32 + chunk_count * 4) return false;
        if (offsets[i] >= size - 8) return false;
    }
    out_size = size;
    out_chunk_count = chunk_count;
    return true;
}

void ensure_out_dir() {
    std::error_code ec;
    std::filesystem::create_directories(out_dir(), ec);
}

bool should_dump(uint32_t crc) {
    if (crc == 0) return false;
    const auto& targets = target_crcs();
    if (targets.empty()) return true;  // dump-all mode
    return targets.find(crc) != targets.end();
}

void write_dxbc(uint32_t crc, const uint8_t* bytes, uint32_t size, const char* tag) {
    auto& s = state();
    {
        std::scoped_lock _{s.mu};
        if (s.dumped_crcs.find(crc) != s.dumped_crcs.end()) return;
    }
    ensure_out_dir();
    char path[1024];
    std::snprintf(path, sizeof(path), "%s\\%s_0x%08x.dxbc",
                  out_dir().c_str(), tag, crc);
    std::ofstream f{path, std::ios::binary};
    if (!f.good()) {
        SPDLOG_WARN("[SN2-HeapDxbc] failed to open {} for write", path);
        return;
    }
    f.write(reinterpret_cast<const char*>(bytes), size);
    {
        std::scoped_lock _{s.mu};
        s.dumped_crcs.insert(crc);
    }
    SPDLOG_WARN("[SN2-HeapDxbc] wrote {} ({} bytes, crc 0x{:08x})", path, size, crc);
}

}  // namespace

bool env_enabled() {
    // No static cache: cheap GetEnvironmentVariableA call lets us hot-toggle
    // via SetEnvironmentVariable from external tooling without restarting.
    return env_flag("UEVR_SN2_HEAP_SCAN_DXBC");
}

size_t scan_now() {
    if (!env_enabled()) return 0;
    auto& s = state();
    const auto t_targets = target_crcs();
    SPDLOG_WARN("[SN2-HeapDxbc] beginning scan. targets={} out_dir='{}'",
                t_targets.empty() ? std::string{"<dump-all>"} : ([&]() {
                    std::string acc;
                    for (auto c : t_targets) {
                        char b[16]; std::snprintf(b, sizeof(b), "0x%08x,", c);
                        acc += b;
                    }
                    if (!acc.empty()) acc.pop_back();
                    return acc;
                }()),
                out_dir());

    size_t dumped_this_pass = 0;
    size_t regions_scanned = 0;
    size_t hits_validated = 0;

    MEMORY_BASIC_INFORMATION mbi{};
    uint8_t* addr = nullptr;
    const uintptr_t kMaxAddr = static_cast<uintptr_t>(0x00007FFFFFFFFFFFull);  // user-mode top

    while (reinterpret_cast<uintptr_t>(addr) < kMaxAddr) {
        const auto qr = VirtualQuery(addr, &mbi, sizeof(mbi));
        if (qr == 0) break;
        uint8_t* next = static_cast<uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;

        // Only inspect committed, readable, non-guard pages.
        const bool committed = (mbi.State == MEM_COMMIT);
        const bool readable = (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE |
                                              PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                              PAGE_WRITECOPY)) != 0;
        const bool guarded = (mbi.Protect & PAGE_GUARD) != 0;
        if (committed && readable && !guarded && mbi.RegionSize >= 64) {
            ++regions_scanned;
            uint8_t* base = static_cast<uint8_t*>(mbi.BaseAddress);
            size_t sz = mbi.RegionSize;

            // Slide a 4-byte window. Stop 32 bytes before end so header read fits.
            const size_t stop = (sz >= 32) ? (sz - 32) : 0;
            for (size_t off = 0; off < stop; ++off) {
                uint8_t* p = base + off;
                // Cheap 4-byte magic check
                if (!safe_memcmp4(p, kDxbcMagic)) continue;
                uint32_t cont_size = 0;
                uint32_t cont_chunks = 0;
                if (!validate_container_header(p, sz - off, cont_size, cont_chunks)) continue;
                // Pull the full container.
                std::vector<uint8_t> blob(cont_size);
                if (!safe_read(blob.data(), p, cont_size)) continue;
                ++hits_validated;
                const uint32_t crc = crc32_ieee(blob.data(), blob.size());
                if (should_dump(crc)) {
                    write_dxbc(crc, blob.data(), cont_size, "ps");
                    {
                        std::scoped_lock _{s.mu};
                        if (s.dumped_crcs.count(crc) > 0) ++dumped_this_pass;
                    }
                }
                // Advance past this container so we don't re-find sub-DXBCs.
                off += cont_size - 1;  // -1 because loop ++off
            }
        }

        addr = next;
        if (next <= mbi.BaseAddress) break;  // guard
    }
    SPDLOG_WARN("[SN2-HeapDxbc] scan complete. regions={} hits_validated={} dumped_this_pass={} dumped_cumulative={}",
                regions_scanned, hits_validated, dumped_this_pass, state().dumped_crcs.size());
    return dumped_this_pass;
}

void on_present(uint64_t frame_count) {
    if (!env_enabled()) return;
    // One-shot startup log so we can confirm the scanner is alive without
    // having to fire a trigger first.
    static std::atomic<bool> alive_logged{false};
    if (!alive_logged.exchange(true)) {
        SPDLOG_WARN("[SN2-HeapDxbc] scanner active. trigger='{}' out_dir='{}' (frame {})",
                    trigger_path(), out_dir(), frame_count);
    }
    // Poll the trigger file at 30 fps (every 2 frames at 60 fps) — cheap.
    if ((frame_count % 30) == 0) {
        const auto attrs = GetFileAttributesA(trigger_path().c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            state().scan_pending.store(true, std::memory_order_release);
            DeleteFileA(trigger_path().c_str());
        }
    }
    if (state().scan_pending.exchange(false, std::memory_order_acq_rel)) {
        // Dispatch on a detached worker thread. The full process-memory walk
        // can take tens of seconds on a multi-GB game and would otherwise
        // trip the OS hang-detect for render-thread non-responsiveness.
        if (!state().scan_running.exchange(true)) {
            std::thread([]{
                scan_now();
                state().scan_running.store(false);
            }).detach();
        }
    }
}

}  // namespace sn2_heap_dxbc_scanner
