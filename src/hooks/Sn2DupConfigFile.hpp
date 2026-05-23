// Sn2DupConfigFile.hpp
//
// Loads a per-PSO dup config from a JSON file (path via UEVR_SN2_DUP_CONFIG_FILE).
// Provides per-CRC view_cb_roots + mode lookups consumed by Sn2WaterBasepassDupHook.
// Supplements (but does not replace) the existing env-CSV path
// UEVR_SN2_DUP_ANY_MRT_PS_CRCS — if both env and file have entries, the union
// is used and the file's per-CRC details (roots, mode) override the global env
// defaults.
//
// FILE FORMAT
// -----------
//
// {
//   "schema": "uevr.sn2.dup_config.v1",
//   "default_view_cb_delta": -10240,
//   "default_view_cb_roots": [4, 6],
//   "entries": {
//     "0x13b00f0c": {
//       "view_cb_roots": [4, 6],
//       "view_cb_delta": -10240,
//       "mode": "viewport_shift",
//       "note": "MainPS — BasePass scene color, both PS b0 and VS b0 read View"
//     },
//     "0x9d14fcf0": {
//       "view_cb_roots": [3],
//       "mode": "viewport_shift"
//     },
//     "0x166dba88": {
//       "view_cb_roots": [4],
//       "mode": "view_cb_only",
//       "note": "Translucent water — no viewport shift needed"
//     }
//   }
// }
//
// LOOKUP CONTRACT
// ---------------
//
// `is_active_ps_crc(crc)`: true if either env or file lists this CRC.
// `entry_for(crc)`: returns the file entry if present; nullopt if env-only.
// `view_cb_roots_for(crc)`: per-entry roots if file has them, else
//                          `active_view_cb_roots()` from Sn2WaterBasepassDupHook
//                          (which falls back to env or hard default {4,6}).
//
// PERF
// ----
//
// File is parsed ONCE at first lookup, cached in a static. Empty file or
// missing env => zero overhead beyond a single env-var check.

#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Sn2WaterBasepassDupHook.hpp"

namespace sn2_dup_config {

enum class DupMode : uint8_t {
    ViewportShift = 0,       // Re-issue with right viewport + right View CB swap
    ViewCbOnly = 1,          // Re-issue with right View CB swap but NO viewport shift
    Skip = 2,                // Don't dup this CRC even if env says to
    SynthesizeRightCb = 3,   // Re-issue with the SYNTHESIZED right View CB from
                             // sn2_right_cb_synth (donor PSO's right-eye snapshot).
                             // For LEFT-only PSOs that have no observed right CBV.
};

inline DupMode mode_from_string(std::string_view s) {
    if (s == "view_cb_only") return DupMode::ViewCbOnly;
    if (s == "skip") return DupMode::Skip;
    if (s == "synthesize_right_cb") return DupMode::SynthesizeRightCb;
    return DupMode::ViewportShift;  // default
}

inline std::string_view mode_to_string(DupMode m) {
    switch (m) {
        case DupMode::ViewCbOnly: return "view_cb_only";
        case DupMode::Skip: return "skip";
        case DupMode::SynthesizeRightCb: return "synthesize_right_cb";
        default: return "viewport_shift";
    }
}

struct Entry {
    std::vector<uint32_t> view_cb_roots{};
    std::optional<int64_t> view_cb_delta{};
    DupMode mode{DupMode::ViewportShift};
    std::string note{};
};

struct Config {
    int64_t default_view_cb_delta{-10240};
    std::vector<uint32_t> default_view_cb_roots{};
    std::unordered_map<uint32_t, Entry> entries{};
    bool loaded{};
    std::string source_path{};
    std::string load_error{};
};

inline const char* env_path_cstr() {
    return std::getenv("UEVR_SN2_DUP_CONFIG_FILE");
}

inline bool file_path_set() {
    const char* p = env_path_cstr();
    return p != nullptr && p[0] != '\0';
}

inline Config& cache() {
    static Config c{};
    return c;
}

inline std::once_flag& load_once_flag() {
    static std::once_flag f{};
    return f;
}

inline void load_config_locked() {
    auto& c = cache();
    const char* path_c = env_path_cstr();
    if (path_c == nullptr || path_c[0] == '\0') {
        c.loaded = true;  // no file => "empty config" is "loaded"
        return;
    }
    c.source_path = path_c;
    try {
        std::ifstream in{c.source_path, std::ios::binary};
        if (!in.good()) {
            c.load_error = "open failed";
            c.loaded = true;
            SPDLOG_WARN("[Sn2DupConfig] failed to open {}", c.source_path);
            return;
        }
        nlohmann::json doc{};
        in >> doc;
        c.default_view_cb_delta = doc.value("default_view_cb_delta", static_cast<int64_t>(-10240));
        if (doc.contains("default_view_cb_roots") && doc["default_view_cb_roots"].is_array()) {
            for (const auto& v : doc["default_view_cb_roots"]) {
                if (v.is_number_unsigned()) {
                    c.default_view_cb_roots.push_back(v.get<uint32_t>());
                }
            }
        }
        if (doc.contains("entries") && doc["entries"].is_object()) {
            for (const auto& [k, v] : doc["entries"].items()) {
                // Parse CRC key: "0x13b00f0c" or "13b00f0c" or decimal
                char* end = nullptr;
                const auto crc = static_cast<uint32_t>(std::strtoul(k.c_str(), &end, 0));
                if (end == k.c_str()) continue;
                Entry e{};
                if (v.contains("view_cb_roots") && v["view_cb_roots"].is_array()) {
                    for (const auto& r : v["view_cb_roots"]) {
                        if (r.is_number_unsigned()) {
                            e.view_cb_roots.push_back(r.get<uint32_t>());
                        }
                    }
                }
                if (v.contains("view_cb_delta") && v["view_cb_delta"].is_number_integer()) {
                    e.view_cb_delta = v["view_cb_delta"].get<int64_t>();
                } else if (v.contains("delta") && v["delta"].is_number_integer()) {
                    // Back-compat with the older docs/tooling name.
                    e.view_cb_delta = v["delta"].get<int64_t>();
                }
                if (v.contains("mode") && v["mode"].is_string()) {
                    e.mode = mode_from_string(v["mode"].get<std::string>());
                }
                if (v.contains("note") && v["note"].is_string()) {
                    e.note = v["note"].get<std::string>();
                }
                c.entries[crc] = std::move(e);
            }
        }
        c.loaded = true;
        SPDLOG_WARN("[Sn2DupConfig] loaded {} entries from {} (default_delta={}, default_roots={})",
                    c.entries.size(),
                    c.source_path,
                    c.default_view_cb_delta,
                    c.default_view_cb_roots.size());
    } catch (const std::exception& ex) {
        c.load_error = ex.what();
        c.loaded = true;
        SPDLOG_WARN("[Sn2DupConfig] parse error in {}: {}", c.source_path, ex.what());
    }
}

inline const Config& config() {
    std::call_once(load_once_flag(), load_config_locked);
    return cache();
}

// True if this CRC is configured in the JSON file (env-CSV checked separately
// via sn2_water_basepass_dup::is_any_mrt_dup_ps_crc).
inline bool has_entry(uint32_t crc) {
    return config().entries.find(crc) != config().entries.end();
}

inline std::optional<Entry> entry_for(uint32_t crc) {
    const auto& c = config();
    const auto it = c.entries.find(crc);
    if (it == c.entries.end()) return std::nullopt;
    return it->second;
}

// Combined "should dup this CRC" — env CSV OR file entry (unless mode=skip).
inline bool is_active_ps_crc(uint32_t crc) {
    const auto& c = config();
    const auto it = c.entries.find(crc);
    if (it != c.entries.end()) {
        return it->second.mode != DupMode::Skip;
    }
    return sn2_water_basepass_dup::is_any_mrt_dup_ps_crc(crc);
}

// Returns the roots to remap for a given CRC. Priority:
//   1. File entry's view_cb_roots if non-empty
//   2. File's default_view_cb_roots if non-empty
//   3. sn2_water_basepass_dup::active_view_cb_roots() (env/default {4,6})
inline std::vector<uint32_t> view_cb_roots_for(uint32_t crc) {
    const auto& c = config();
    const auto it = c.entries.find(crc);
    if (it != c.entries.end() && !it->second.view_cb_roots.empty()) {
        return it->second.view_cb_roots;
    }
    if (!c.default_view_cb_roots.empty()) {
        return c.default_view_cb_roots;
    }
    return sn2_water_basepass_dup::active_view_cb_roots();
}

inline DupMode mode_for(uint32_t crc) {
    const auto e = entry_for(crc);
    if (e) return e->mode;
    return DupMode::ViewportShift;
}

inline int64_t view_cb_delta() {
    const auto& c = config();
    if (file_path_set()) return c.default_view_cb_delta;
    return sn2_water_basepass_dup::view_cb_offset_delta();
}

inline int64_t view_cb_delta_for(uint32_t crc) {
    const auto& c = config();
    const auto it = c.entries.find(crc);
    if (it != c.entries.end() && it->second.view_cb_delta.has_value()) {
        return *it->second.view_cb_delta;
    }
    return view_cb_delta();
}

inline uint64_t compute_right_view_va_for(uint32_t crc, uint64_t left_va) {
    if (left_va == 0) return 0;
    const int64_t delta = view_cb_delta_for(crc);
    if (delta < 0 && left_va < static_cast<uint64_t>(-delta)) return 0;
    return static_cast<uint64_t>(static_cast<int64_t>(left_va) + delta);
}

}  // namespace sn2_dup_config
