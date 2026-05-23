// Sn2MagicInk.hpp
//
// LIVE PS skip — for any configured PS CRC, skip the draw entirely at runtime.
// Lets you binary-search which PSO is responsible for a visible artifact by
// flipping env vars without rebuilding.
//
// (Original concept was DXIL substitution to write solid color, but live-skip
// gives equivalent diagnostic value with zero compilation complexity: whatever
// you see DISAPPEAR identifies the PSO's screen contribution.)
//
// CONFIG
//   UEVR_SN2_MAGIC_INK_SKIP_PS_CRCS=0x13b00f0c,0xd3ab43c5  — CSV of PS CRCs to skip
//   UEVR_SN2_MAGIC_INK_EYE=both|left|right                  — which eye(s) to skip
//
// USE
// ---
// 1. Set env, run game
// 2. Observe which screen region(s) disappear or change
// 3. That tells you exactly which PSO contributes to that region

#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_set>

#include "Sn2EyePairingHook.hpp"

namespace sn2_magic_ink {

inline bool env_enabled() {
    static const bool e = []() {
        return GetEnvironmentVariableW(L"UEVR_SN2_MAGIC_INK_SKIP_PS_CRCS", nullptr, 0) > 0
            || GetEnvironmentVariableW(L"UEVR_SN2_MAGIC_INK_SKIP_FILE", nullptr, 0) > 0;
    }();
    return e;
}

// Live-reload state: path to a CRC list file. Re-read every N frames or when
// mtime changes. Lets users tweak the skip list without restarting the game.
inline std::string skip_file_path() {
    static const std::string p = []() -> std::string {
        char buf[2048]{};
        const auto len = GetEnvironmentVariableA("UEVR_SN2_MAGIC_INK_SKIP_FILE", buf, sizeof(buf));
        if (len == 0 || len >= sizeof(buf)) return std::string{};
        return std::string{buf, len};
    }();
    return p;
}

// Parse a CSV/whitespace-separated list of u32 CRCs.
// IMPORTANT: a `#` starts a comment that runs to the END of the line — every
// token after `#` (including hex CRCs embedded in comment prose) is ignored.
// Earlier bug: comments contained example CRCs like "0x37558de4" that got
// silently parsed and skipped, hiding actual fog rendering for hours.
inline std::unordered_set<uint32_t> parse_crc_csv(const std::string& s) {
    std::unordered_set<uint32_t> out;
    size_t pos = 0;
    while (pos < s.size()) {
        // Skip whitespace.
        while (pos < s.size() && (s[pos] == ',' || s[pos] == ' ' || s[pos] == '\n' || s[pos] == '\r' || s[pos] == '\t')) ++pos;
        if (pos >= s.size()) break;
        // If we hit a comment marker, skip to end-of-line.
        if (s[pos] == '#') {
            while (pos < s.size() && s[pos] != '\n') ++pos;
            continue;
        }
        // Read one token.
        size_t end = pos;
        while (end < s.size() && s[end] != ',' && s[end] != ' ' && s[end] != '\n' && s[end] != '\r' && s[end] != '\t' && s[end] != '#') ++end;
        std::string tok = s.substr(pos, end - pos);
        pos = end;
        if (tok.empty()) continue;
        char* tail = nullptr;
        const auto v = std::strtoul(tok.c_str(), &tail, 0);
        if (tail != tok.c_str()) out.insert(static_cast<uint32_t>(v));
    }
    return out;
}

// Returns the currently-effective skip CRC set. If skip_file_path() is set,
// re-reads it when its mtime changes (poll every 60 frames at most).
inline std::unordered_set<uint32_t> skip_crcs() {
    // Static fallback (env-only mode)
    static const std::unordered_set<uint32_t> env_set = []() {
        std::unordered_set<uint32_t> out;
        char buf[2048]{};
        const auto len = GetEnvironmentVariableA("UEVR_SN2_MAGIC_INK_SKIP_PS_CRCS", buf, sizeof(buf));
        if (len == 0 || len >= sizeof(buf)) return out;
        return parse_crc_csv(std::string{buf, len});
    }();
    // Live-reload mode
    if (!skip_file_path().empty()) {
        static std::mutex mu;
        static std::unordered_set<uint32_t> live_set;
        static int64_t last_mtime = 0;
        static std::atomic<uint64_t> poll_counter{0};
        const auto n = poll_counter.fetch_add(1, std::memory_order_relaxed);
        if ((n % 60) == 0) {  // poll every 60 calls
            WIN32_FILE_ATTRIBUTE_DATA fad{};
            if (GetFileAttributesExA(skip_file_path().c_str(), GetFileExInfoStandard, &fad)) {
                const int64_t mtime = (static_cast<int64_t>(fad.ftLastWriteTime.dwHighDateTime) << 32)
                                    | fad.ftLastWriteTime.dwLowDateTime;
                if (mtime != last_mtime) {
                    std::ifstream f{skip_file_path()};
                    if (f.good()) {
                        std::string content((std::istreambuf_iterator<char>(f)),
                                             std::istreambuf_iterator<char>());
                        auto parsed = parse_crc_csv(content);
                        std::scoped_lock _{mu};
                        live_set = std::move(parsed);
                        last_mtime = mtime;
                    }
                }
            }
        }
        std::scoped_lock _{mu};
        return live_set;  // copy out under lock
    }
    return env_set;  // const ref auto-copies into return-by-value
}

// Returns the StereoTraceBucket value the user wants to skip, or -1 = both.
// StereoTraceBucket enum: Unknown=0, Left=1, Right=2, Full=3, Multi=4.
inline int skip_eye_bucket() {
    static const int v = []() -> int {
        char buf[16]{};
        const auto len = GetEnvironmentVariableA("UEVR_SN2_MAGIC_INK_EYE", buf, sizeof(buf));
        if (len == 0 || len >= sizeof(buf)) return -1;
        std::string s{buf, len};
        if (s == "left")  return 1;  // StereoTraceBucket::Left
        if (s == "right") return 2;  // StereoTraceBucket::Right
        return -1;  // both / any
    }();
    return v;
}

// Returns true if this PS_CRC + eye combination should be skipped.
inline bool should_skip(uint32_t ps_crc, int eye_bucket) {
    if (!env_enabled()) return false;
    // Synthetic dup draws bypass magic-ink skip: lets the WaterBasepassDup
    // re-issue LEFT-style draws on right viewport even when right-eye natural
    // draws are being skipped.
    if (sn2_eye_pairing::in_synthetic_draw()) return false;
    const auto set = skip_crcs();
    if (set.find(ps_crc) == set.end()) return false;
    const int target = skip_eye_bucket();
    if (target == -1) return true;  // any eye
    return eye_bucket == target;
}

}  // namespace sn2_magic_ink
