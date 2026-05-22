// Sn2MissingPassDiffHook.hpp
//
// Live frame-by-frame per-PSO eye-bucket draw/dispatch counter. Surfaces
// "LEFT-only" or "RIGHT-only" PSOs DYNAMICALLY — static captures only sample
// one frame and miss draws gated on per-frame state (LODs, distance culling,
// stencil tests, etc.).
//
// Output: CSV with one row per (PS_CRC, eye_bucket) pair per dump window.
//
//   ps_crc,left_count,right_count,unknown_count,full_count,multi_count,delta
//   0x13b00f0c,18,0,0,0,0,18
//   0x9d14fcf0,22,0,0,0,0,22
//   0x4c7d0fb9,12,12,0,0,0,0
//   ...
//
// USAGE
// -----
//
//   UEVR_SN2_MISSING_PASS_DIFF_CSV=C:\path\to\diff.csv  — enable + output path
//   UEVR_SN2_MISSING_PASS_DIFF_INTERVAL=60              — dump every N frames (default 60 = 1s @ 60fps)
//   UEVR_SN2_MISSING_PASS_DIFF_DELTA_THRESH=1           — only log rows where |L-R| >= N (default 1)
//   UEVR_SN2_MISSING_PASS_DIFF_INCLUDE_CS=1             — also include compute (default 0; PS-only)
//   UEVR_SN2_MISSING_PASS_DIFF_RING_SIZE=600            — keep last N frames of CSV; truncate before
//
// Each dump APPENDS to the CSV with a frame header so multiple windows
// accumulate in one file. To start fresh, delete the file before launch.
//
// PERF
// ----
//
// Each draw/dispatch call does one atomic increment on a per-(crc,eye)
// counter — backed by an `unordered_map` under a single mutex, but the
// hot path uses a thread-local cache so the mutex is touched only when a
// new (crc,eye) is first seen this window.

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>

namespace sn2_missing_pass_diff {

enum class EyeBucket : uint8_t {
    Unknown = 0,
    Left = 1,
    Right = 2,
    Full = 3,
    Multi = 4,
    Count = 5,
};

struct CrcCounts {
    std::array<uint64_t, static_cast<size_t>(EyeBucket::Count)> n{};
    // Stage discriminator: 'P' (PS / graphics), 'C' (compute). Default 'P'.
    char stage{'P'};
};

inline bool env_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_MISSING_PASS_DIFF_CSV");
        return v != nullptr && v[0] != '\0';
    }();
    return e;
}

inline const std::string& output_path() {
    static const std::string p = []() {
        const char* v = std::getenv("UEVR_SN2_MISSING_PASS_DIFF_CSV");
        return std::string{v != nullptr ? v : ""};
    }();
    return p;
}

inline uint64_t interval_frames() {
    static const uint64_t f = []() -> uint64_t {
        const char* v = std::getenv("UEVR_SN2_MISSING_PASS_DIFF_INTERVAL");
        if (v == nullptr || *v == '\0') return 60;
        char* end = nullptr;
        const auto n = std::strtoull(v, &end, 0);
        if (end == v) return 60;
        return n;
    }();
    return f;
}

inline uint64_t delta_threshold() {
    static const uint64_t t = []() -> uint64_t {
        const char* v = std::getenv("UEVR_SN2_MISSING_PASS_DIFF_DELTA_THRESH");
        if (v == nullptr || *v == '\0') return 1;
        char* end = nullptr;
        const auto n = std::strtoull(v, &end, 0);
        if (end == v) return 1;
        return n;
    }();
    return t;
}

inline bool include_cs() {
    static const bool c = []() {
        const char* v = std::getenv("UEVR_SN2_MISSING_PASS_DIFF_INCLUDE_CS");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return c;
}

inline std::mutex& counters_mutex() {
    static std::mutex m{};
    return m;
}

inline std::unordered_map<uint32_t, CrcCounts>& counters() {
    static std::unordered_map<uint32_t, CrcCounts> m{};
    return m;
}

inline std::atomic<uint64_t>& session_frame() {
    static std::atomic<uint64_t> v{0};
    return v;
}

inline std::atomic<uint64_t>& last_dump_frame() {
    static std::atomic<uint64_t> v{0};
    return v;
}

inline std::atomic<bool>& header_written() {
    static std::atomic<bool> v{false};
    return v;
}

// Hot path. Called from each graphics-draw or compute-dispatch hook.
// `crc` is the PS CRC32 (or CS CRC32 for compute when stage='C').
// `eye_bucket` is the per-draw eye assignment (0=Unknown, 1=Left, 2=Right,
// 3=Full, 4=Multi) — matches D3D12Hook's StereoTraceBucket enum.
inline void record(uint32_t crc, int eye_bucket, char stage) {
    if (!env_enabled()) return;
    if (crc == 0) return;
    if (stage == 'C' && !include_cs()) return;
    const auto bucket = (eye_bucket < 0 || eye_bucket >= static_cast<int>(EyeBucket::Count))
        ? static_cast<size_t>(EyeBucket::Unknown)
        : static_cast<size_t>(eye_bucket);

    std::scoped_lock _{counters_mutex()};
    auto& ent = counters()[crc];
    ent.stage = stage;
    ent.n[bucket]++;
}

inline void reset_counters_locked() {
    counters().clear();
}

// Returns the number of rows written (filtered by delta_threshold).
inline size_t dump_csv_locked(uint64_t frame) {
    const std::string& path = output_path();
    if (path.empty()) return 0;

    std::ofstream out{path, std::ios::app};
    if (!out.good()) {
        SPDLOG_WARN("[Sn2MissingPassDiff] failed to open {} for append", path);
        return 0;
    }

    if (!header_written().exchange(true, std::memory_order_relaxed)) {
        out << "# uevr.sn2.missing_pass_diff.v1\n";
        out << "# columns: frame,stage,crc,left,right,unknown,full,multi,delta\n";
    }

    const auto thresh = delta_threshold();
    size_t written = 0;
    for (const auto& [crc, c] : counters()) {
        const auto L = c.n[static_cast<size_t>(EyeBucket::Left)];
        const auto R = c.n[static_cast<size_t>(EyeBucket::Right)];
        const auto U = c.n[static_cast<size_t>(EyeBucket::Unknown)];
        const auto F = c.n[static_cast<size_t>(EyeBucket::Full)];
        const auto M = c.n[static_cast<size_t>(EyeBucket::Multi)];
        const auto delta = (L > R) ? (L - R) : (R - L);
        if (delta < thresh) continue;
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "%llu,%c,0x%08x,%llu,%llu,%llu,%llu,%llu,%lld\n",
                      static_cast<unsigned long long>(frame),
                      c.stage,
                      crc,
                      static_cast<unsigned long long>(L),
                      static_cast<unsigned long long>(R),
                      static_cast<unsigned long long>(U),
                      static_cast<unsigned long long>(F),
                      static_cast<unsigned long long>(M),
                      static_cast<long long>(static_cast<int64_t>(L) - static_cast<int64_t>(R)));
        out << buf;
        ++written;
    }
    out.flush();
    return written;
}

// Call once per Present after sn2_root_sig_dump::on_present().
inline void on_present() {
    if (!env_enabled()) return;
    const auto frame = session_frame().fetch_add(1, std::memory_order_relaxed) + 1;
    const auto last = last_dump_frame().load(std::memory_order_acquire);
    if (frame - last < interval_frames()) return;

    std::scoped_lock _{counters_mutex()};
    const auto rows = dump_csv_locked(frame);
    if (rows > 0) {
        SPDLOG_INFO("[Sn2MissingPassDiff] frame={} wrote {} rows above delta_threshold={}",
                    frame, rows, delta_threshold());
    }
    reset_counters_locked();
    last_dump_frame().store(frame, std::memory_order_release);
}

}  // namespace sn2_missing_pass_diff
