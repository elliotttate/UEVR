// Sn2LiveMutate.hpp
//
// Hot-reloaded "live mutation" runtime for the StereoScope iteration loop.
// Lets us change rendering behaviour WITHOUT relaunching the game by editing a
// small JSON ops file on disk — the file is re-read when its mtime changes.
//
// ADDITIVE + DEFAULT OFF. Gated entirely by UEVR_SN2_LIVE_MUTATE=1. When the
// env is unset/0, every entry point is a cheap early-out (one cached bool read)
// and zero behaviour changes. This module never allocates GPU resources, never
// resolves descriptors, and never mutates any D3D12 state directly — it only
// answers "should this draw/dispatch's original() call be SKIPPED?".
//
// USAGE
// -----
//   UEVR_SN2_LIVE_MUTATE=1
//       Enable the live-mutation runtime.
//   UEVR_SN2_MUTATE_FILE=C:\path\to\stereoscope_mutations.json
//       Ops file path. Default:
//         C:\Users\ellio\AppData\Roaming\UnrealVRMod\Subnautica2-Win64-Shipping\stereoscope_mutations.json
//   (Also: add "UEVR_SN2_LIVE_MUTATE" to sn2_rootbind_bookkeeping_required()
//    in D3D12Hook.cpp so the eye bucket + ps/cs CRC are computed at the call
//    sites — otherwise the skip-rule match has nothing to match against.)
//
// OPS FILE FORMAT (a JSON array of op objects; hand-rolled scanner, no lib)
// ------------------------------------------------------------------------
//   [
//     {"op":"skip_draw","crc":"0xDE7C3822","eye":1},
//     {"op":"skip_draw","crc":"0x9D14FCF0"}
//   ]
//
//   op   : currently only "skip_draw" produces a rule. "clear" (or an empty
//          array, or a missing file) yields zero rules.
//   crc  : the PS CRC32 (for draws) or CS CRC32 (for dispatches) to skip, as a
//          "0x..." hex string (decimal also accepted). Required for skip_draw.
//   eye  : optional. -1 / omitted = any eye; 0 = left only; 1 = right only.
//
// Because the rules are keyed purely on crc + eye, ONE ops file drives both the
// draw path (matched on the pixel-shader CRC) and the dispatch path (matched on
// the compute-shader CRC). Edit the file, save, and the next frame picks it up.

#pragma once

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>
#include <windows.h>

namespace sn2_live_mutate {

// ---- enable gate -----------------------------------------------------------
inline bool enabled() {
    static const bool v = []() {
        const char* e = std::getenv("UEVR_SN2_LIVE_MUTATE");
        return e != nullptr && e[0] != '\0' && e[0] != '0';
    }();
    return v;
}

// ---- ops file path ---------------------------------------------------------
inline const std::string& ops_file_path() {
    static const std::string p = []() {
        const char* e = std::getenv("UEVR_SN2_MUTATE_FILE");
        if (e != nullptr && e[0] != '\0') {
            return std::string{e};
        }
        return std::string{
            "C:\\Users\\ellio\\AppData\\Roaming\\UnrealVRMod\\"
            "Subnautica2-Win64-Shipping\\stereoscope_mutations.json"};
    }();
    return p;
}

// ---- skip rule -------------------------------------------------------------
struct SkipRule {
    uint32_t crc = 0;
    int eye = -1;  // -1 any, 0 left, 1 right
};

// ---- mutation store --------------------------------------------------------
// Mutex-guarded so the hot-reload (which rebuilds the vector) cannot race the
// per-draw / per-dispatch readers.
struct Store {
    std::mutex mu;
    std::vector<SkipRule> skip_rules;
    uint64_t last_mtime = 0;    // FILETIME of the ops file at last successful read
    bool ever_read = false;     // whether we've attempted at least one read
};

inline Store& store() {
    static Store s;
    return s;
}

// ---- tiny JSON op scanner --------------------------------------------------
// Hand-rolled, intentionally small. We only understand a flat JSON array of
// objects, each with a string "op", an optional string/number "crc", and an
// optional number "eye". Anything we don't recognise is skipped. This is NOT a
// general JSON parser — it tolerates whitespace, commas and nesting via brace
// matching but does not validate the document.
namespace detail {

inline uint32_t parse_crc_token(const std::string& tok) {
    // Accept "0x..." hex or plain decimal; ignore surrounding quotes.
    std::string t = tok;
    // strip quotes
    if (!t.empty() && (t.front() == '"' || t.front() == '\'')) t.erase(t.begin());
    if (!t.empty() && (t.back() == '"' || t.back() == '\'')) t.pop_back();
    if (t.empty()) return 0;
    return static_cast<uint32_t>(std::strtoul(t.c_str(), nullptr, 0));
}

// Find the value following a `"key"` in [obj_begin, obj_end) within text.
// Returns the raw token (string contents without quotes, or the numeric/literal
// run). Returns empty string if not found.
inline std::string find_value(const std::string& s, size_t obj_begin, size_t obj_end,
                              const char* key) {
    const std::string needle = std::string{"\""} + key + "\"";
    size_t p = s.find(needle, obj_begin);
    if (p == std::string::npos || p >= obj_end) return {};
    p += needle.size();
    // skip whitespace + ':'
    while (p < obj_end && (std::isspace((unsigned char)s[p]) || s[p] == ':')) ++p;
    if (p >= obj_end) return {};
    if (s[p] == '"') {
        // quoted string value
        size_t start = ++p;
        while (p < obj_end && s[p] != '"') ++p;
        return s.substr(start, p - start);
    }
    // bare token (number / true / false / null) — read until delimiter
    size_t start = p;
    while (p < obj_end && s[p] != ',' && s[p] != '}' && !std::isspace((unsigned char)s[p])) ++p;
    return s.substr(start, p - start);
}

// Parse the whole document text into a fresh rule vector.
inline std::vector<SkipRule> parse_ops(const std::string& s) {
    std::vector<SkipRule> rules;
    size_t pos = 0;
    while (pos < s.size()) {
        const size_t obj_begin = s.find('{', pos);
        if (obj_begin == std::string::npos) break;
        const size_t obj_end = s.find('}', obj_begin);
        if (obj_end == std::string::npos) break;
        pos = obj_end + 1;

        const std::string op = find_value(s, obj_begin, obj_end, "op");
        if (op == "skip_draw") {
            const std::string crc_tok = find_value(s, obj_begin, obj_end, "crc");
            const uint32_t crc = parse_crc_token(crc_tok);
            if (crc != 0) {
                SkipRule r{};
                r.crc = crc;
                const std::string eye_tok = find_value(s, obj_begin, obj_end, "eye");
                if (!eye_tok.empty()) {
                    r.eye = static_cast<int>(std::strtol(eye_tok.c_str(), nullptr, 10));
                } else {
                    r.eye = -1;
                }
                rules.push_back(r);
            }
        }
        // "clear" (and any unknown op) contributes no rule. A full-document
        // reparse already starts from an empty vector, so "clear" == empty.
    }
    return rules;
}

}  // namespace detail

// ---- hot reload ------------------------------------------------------------
// Re-reads the ops file when its mtime changes. To keep this cheap we only stat
// the file roughly once every ~30 calls (a free-running counter). The very
// first call always reads so a file present at startup is honoured.
inline void maybe_reload() {
    static std::atomic<uint32_t> tick{0};
    const uint32_t n = tick.fetch_add(1, std::memory_order_relaxed);

    Store& st = store();
    bool force_first = false;
    if (!st.ever_read) {
        // cheap, no-lock read of the flag; the lock below sets it.
        force_first = true;
    }
    if (!force_first && (n % 30) != 0) {
        return;
    }

    const std::string& path = ops_file_path();

    // Stat the file mtime first (cheap) before deciding to re-read it.
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    uint64_t mtime = 0;
    bool exists = (GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fad) != 0);
    if (exists) {
        mtime = (static_cast<uint64_t>(fad.ftLastWriteTime.dwHighDateTime) << 32) |
                fad.ftLastWriteTime.dwLowDateTime;
    }

    {
        std::scoped_lock _{st.mu};
        if (st.ever_read && exists && mtime == st.last_mtime) {
            return;  // unchanged
        }
        st.ever_read = true;
        st.last_mtime = mtime;

        if (!exists) {
            // Missing file == no rules (tolerated).
            if (!st.skip_rules.empty()) {
                SPDLOG_WARN("[SN2-LiveMutate] ops file gone, clearing {} rule(s): {}",
                            st.skip_rules.size(), path);
            }
            st.skip_rules.clear();
            return;
        }
    }

    // Read the file outside the lock (file IO can be slow); then swap in.
    std::string contents;
    {
        HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER sz{};
            if (GetFileSizeEx(h, &sz) && sz.QuadPart > 0 && sz.QuadPart < (64 * 1024)) {
                contents.resize(static_cast<size_t>(sz.QuadPart));
                DWORD got = 0;
                if (!ReadFile(h, contents.data(), static_cast<DWORD>(contents.size()), &got, nullptr)) {
                    contents.clear();
                } else {
                    contents.resize(got);
                }
            }
            CloseHandle(h);
        }
    }

    std::vector<SkipRule> rules = detail::parse_ops(contents);
    {
        std::scoped_lock _{st.mu};
        st.skip_rules.swap(rules);
        SPDLOG_WARN("[SN2-LiveMutate] reloaded {} skip rule(s) from {}",
                    st.skip_rules.size(), path);
    }
}

// ---- skip decision ---------------------------------------------------------
// true if any loaded rule matches `crc` and (rule.eye < 0 || rule.eye == eye).
// Bumps a global skip counter and logs the first ~16 skips (throttled) so we
// can confirm the mutation actually fired.
inline std::atomic<uint64_t>& skip_counter() {
    static std::atomic<uint64_t> c{0};
    return c;
}

inline bool should_skip(uint32_t crc, int eye) {
    if (crc == 0) return false;
    maybe_reload();

    Store& st = store();
    bool matched = false;
    {
        std::scoped_lock _{st.mu};
        for (const auto& r : st.skip_rules) {
            if (r.crc == crc && (r.eye < 0 || r.eye == eye)) {
                matched = true;
                break;
            }
        }
    }
    if (!matched) return false;

    const uint64_t n = skip_counter().fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 16 || (n % 600) == 0) {
        SPDLOG_WARN("[SN2-LiveMutate] SKIP #{} crc=0x{:08x} eye={}", n, crc, eye);
    }
    return true;
}

}  // namespace sn2_live_mutate
