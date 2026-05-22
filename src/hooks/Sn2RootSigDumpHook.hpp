// Sn2RootSigDumpHook.hpp
//
// Dumps every PSO's root signature layout to a JSON file at runtime, joined
// with the PS/VS/CS CRC32 from ShaderOverrideRegistry. Eliminates per-PSO
// "which root param holds View CB" guessing for downstream dup hooks.
//
// USAGE
// -----
//
//   UEVR_SN2_DUMP_ROOTSIG_JSON=C:\path\to\rootsig_map.json   — enable + path
//   UEVR_SN2_DUMP_ROOTSIG_FRAME_THRESHOLD=300                — frame to dump on (default 300)
//   UEVR_SN2_DUMP_ROOTSIG_REPEAT=0                           — set to 1 to re-dump every N frames
//   UEVR_SN2_DUMP_ROOTSIG_REPEAT_INTERVAL=600                — re-dump cadence in frames (default 600)
//
// OUTPUT FORMAT (one object per PSO)
//
//   {
//     "schema": "uevr.sn2.rootsig_map.v1",
//     "frame": 1234,
//     "pso_count": 475,
//     "root_sig_count": 91,
//     "psos": [
//       {
//         "pso_ptr": "0xABCD1234",
//         "ps_crc": "0x13b00f0c",
//         "vs_crc": "0xdeadbeef",
//         "cs_crc": null,
//         "root_sig_ptr": "0xCAFEBABE",
//         "root_sig": {
//           "blob_size": 116,
//           "version": "1.1",
//           "flags": "ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT|DENY_HULL_...",
//           "static_sampler_count": 0,
//           "parameters": [
//             { "index": 0, "type": "DESCRIPTOR_TABLE", "visibility": "PIXEL",
//               "ranges": [{"type":"SRV","base":0,"count":64,"space":0,"offset":0}] },
//             { "index": 4, "type": "CBV", "visibility": "PIXEL", "register": 0, "space": 0 },
//             ...
//           ]
//         }
//       },
//       ...
//     ],
//     "orphan_root_sigs": [ ... ]   // root sigs not referenced by any tracked PSO
//   }
//
// CONSUMERS
// ---------
//
// Downstream UEVR dup hooks read this file at startup to look up:
//   - "which root param holds the View CB for PS_CRC X" → no more env-driven
//     UEVR_SN2_WATER_BASEPASS_VIEW_ROOTS=4,6 hardcoding
//   - "is this PSO compute or graphics" → drives mode selection
//   - "does this PSO have a sampler table at root[1]" → so the dup hook can
//     copy sampler descriptors per-eye if needed.

#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "render/D3D12Diagnostics.hpp"
#include "render/ShaderOverrideRegistry.hpp"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace sn2_root_sig_dump {

inline bool env_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_DUMP_ROOTSIG_JSON");
        return v != nullptr && v[0] != '\0';
    }();
    return e;
}

inline const std::string& output_path() {
    static const std::string p = []() {
        const char* v = std::getenv("UEVR_SN2_DUMP_ROOTSIG_JSON");
        return std::string{v != nullptr ? v : ""};
    }();
    return p;
}

inline uint64_t frame_threshold() {
    static const uint64_t f = []() -> uint64_t {
        const char* v = std::getenv("UEVR_SN2_DUMP_ROOTSIG_FRAME_THRESHOLD");
        if (v == nullptr || *v == '\0') return 300;
        char* end = nullptr;
        const auto n = std::strtoull(v, &end, 0);
        if (end == v) return 300;
        return n;
    }();
    return f;
}

inline bool repeat_mode() {
    static const bool r = []() {
        const char* v = std::getenv("UEVR_SN2_DUMP_ROOTSIG_REPEAT");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return r;
}

inline uint64_t repeat_interval() {
    static const uint64_t i = []() -> uint64_t {
        const char* v = std::getenv("UEVR_SN2_DUMP_ROOTSIG_REPEAT_INTERVAL");
        if (v == nullptr || *v == '\0') return 600;
        char* end = nullptr;
        const auto n = std::strtoull(v, &end, 0);
        if (end == v) return 600;
        return n;
    }();
    return i;
}

inline std::atomic<uint64_t>& last_dump_frame() {
    static std::atomic<uint64_t> v{0};
    return v;
}

inline std::atomic<bool>& already_dumped_once() {
    static std::atomic<bool> v{false};
    return v;
}

// Build the JSON, atomic-write to a `.tmp` then rename to the target path so
// readers never see a partial file.
inline bool write_json_to_disk(const std::string& path, const nlohmann::json& doc) {
    if (path.empty()) return false;
    const std::string tmp = path + ".tmp";
    try {
        std::ofstream out{tmp, std::ios::binary | std::ios::trunc};
        if (!out.good()) {
            SPDLOG_WARN("[Sn2RootSigDump] failed to open {} for write", tmp);
            return false;
        }
        out << doc.dump(2);
        out.flush();
        out.close();
    } catch (const std::exception& e) {
        SPDLOG_WARN("[Sn2RootSigDump] write failed: {}", e.what());
        return false;
    }
    // Best-effort atomic replace. On Windows std::rename onto an existing
    // file fails; remove + rename.
    std::remove(path.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        SPDLOG_WARN("[Sn2RootSigDump] rename {} -> {} failed", tmp, path);
        return false;
    }
    return true;
}

inline nlohmann::json range_to_json(const render::D3D12Diagnostics::RootDescriptorRangeInfo& r) {
    return nlohmann::json{
        {"type", r.type},
        {"base", r.base_shader_register},
        {"count", r.num_descriptors},
        {"space", r.register_space},
        {"offset", r.offset_from_table_start},
    };
}

inline nlohmann::json param_to_json(const render::D3D12Diagnostics::RootParameterInfo& p) {
    nlohmann::json j{
        {"index", p.index},
        {"type", p.parameter_type},
        {"visibility", p.visibility},
    };
    if (p.parameter_type == "32BIT_CONSTANTS") {
        j["register"] = p.shader_register;
        j["space"] = p.register_space;
        j["num_32bit_values"] = p.num_32bit_values;
    } else if (p.parameter_type == "CBV" ||
               p.parameter_type == "SRV" ||
               p.parameter_type == "UAV") {
        j["register"] = p.shader_register;
        j["space"] = p.register_space;
    } else if (p.parameter_type == "DESCRIPTOR_TABLE") {
        auto ranges = nlohmann::json::array();
        for (const auto& r : p.ranges) ranges.push_back(range_to_json(r));
        j["ranges"] = std::move(ranges);
    }
    return j;
}

inline nlohmann::json root_sig_to_json(const render::D3D12Diagnostics::RootSignatureInfo& rs) {
    auto params = nlohmann::json::array();
    for (const auto& p : rs.parameters) params.push_back(param_to_json(p));
    nlohmann::json j{
        {"blob_size", rs.blob_size},
        {"version", rs.version},
        {"flags", rs.flags},
        {"static_sampler_count", rs.static_sampler_count},
        {"parameters", std::move(params)},
        {"first_seen_frame", rs.first_seen_frame},
        {"last_seen_frame", rs.last_seen_frame},
    };
    if (!rs.decode_error.empty()) j["decode_error"] = rs.decode_error;
    return j;
}

inline std::string hex_ptr(uintptr_t p) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(p));
    return std::string{buf};
}

inline std::string hex_crc(uint32_t c) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08x", c);
    return std::string{buf};
}

// Build and write the JSON. Returns the number of PSOs written.
inline size_t dump_now(uint64_t current_frame) {
    if (!env_enabled()) return 0;

    auto& diag = render::D3D12Diagnostics::get();
    auto& reg = render::ShaderOverrideRegistry::get();

    const auto root_sigs = diag.snapshot_root_signatures();
    const auto pso_pairs = diag.snapshot_pso_root_signature_pairs();

    if (root_sigs.empty() && pso_pairs.empty()) {
        SPDLOG_INFO("[Sn2RootSigDump] no PSOs/root sigs tracked yet at frame {}, skipping",
                    current_frame);
        return 0;
    }

    // Index root sigs by pointer.
    std::unordered_map<uintptr_t, const render::D3D12Diagnostics::RootSignatureInfo*> rs_by_ptr{};
    rs_by_ptr.reserve(root_sigs.size());
    for (const auto& rs : root_sigs) {
        rs_by_ptr.emplace(rs.pointer, &rs);
    }

    nlohmann::json doc{
        {"schema", "uevr.sn2.rootsig_map.v1"},
        {"frame", current_frame},
        {"pso_count", pso_pairs.size()},
        {"root_sig_count", root_sigs.size()},
    };

    auto psos = nlohmann::json::array();
    std::unordered_set<uintptr_t> referenced_rs{};
    for (const auto& [pso_ptr, rs_ptr] : pso_pairs) {
        nlohmann::json entry{
            {"pso_ptr", hex_ptr(pso_ptr)},
            {"root_sig_ptr", hex_ptr(rs_ptr)},
        };
        const uint32_t ps = reg.d3d12_pso_pixel_crc32(pso_ptr);
        const uint32_t gs = reg.d3d12_pso_geometry_crc32(pso_ptr);
        const uint32_t cs = reg.d3d12_pso_compute_crc32(pso_ptr);
        entry["ps_crc"] = ps != 0 ? nlohmann::json(hex_crc(ps)) : nlohmann::json{};
        entry["gs_crc"] = gs != 0 ? nlohmann::json(hex_crc(gs)) : nlohmann::json{};
        entry["cs_crc"] = cs != 0 ? nlohmann::json(hex_crc(cs)) : nlohmann::json{};

        if (const auto it = rs_by_ptr.find(rs_ptr); it != rs_by_ptr.end()) {
            entry["root_sig"] = root_sig_to_json(*it->second);
            referenced_rs.insert(rs_ptr);
        }
        psos.push_back(std::move(entry));
    }
    doc["psos"] = std::move(psos);

    auto orphans = nlohmann::json::array();
    for (const auto& rs : root_sigs) {
        if (referenced_rs.count(rs.pointer) != 0) continue;
        nlohmann::json e{
            {"root_sig_ptr", hex_ptr(rs.pointer)},
        };
        e.merge_patch(root_sig_to_json(rs));
        orphans.push_back(std::move(e));
    }
    doc["orphan_root_sigs"] = std::move(orphans);

    const bool ok = write_json_to_disk(output_path(), doc);
    if (ok) {
        SPDLOG_WARN("[Sn2RootSigDump] wrote {} PSOs / {} root sigs to {} at frame {}",
                    pso_pairs.size(), root_sigs.size(), output_path(), current_frame);
    }
    return ok ? pso_pairs.size() : 0;
}

// Self-managed frame counter so callers don't have to thread one through.
inline std::atomic<uint64_t>& session_frame_counter() {
    static std::atomic<uint64_t> v{0};
    return v;
}

// Forward decl (definition below).
inline void try_dump_from_present(uint64_t current_frame);

// Bumps the internal frame counter and dispatches to try_dump_from_present.
// Call once per actual Present from D3D12Hook::present()/present1(). Cheap
// no-op when UEVR_SN2_DUMP_ROOTSIG_JSON is unset.
inline void on_present() {
    if (!env_enabled()) return;
    const auto frame = session_frame_counter().fetch_add(1, std::memory_order_relaxed) + 1;
    try_dump_from_present(frame);
}

// Called from D3D12Hook::on_present. Cheap unless the threshold is hit.
inline void try_dump_from_present(uint64_t current_frame) {
    if (!env_enabled()) return;
    const auto threshold = frame_threshold();
    if (current_frame < threshold) return;

    if (!already_dumped_once().load(std::memory_order_acquire)) {
        if (dump_now(current_frame) > 0) {
            already_dumped_once().store(true, std::memory_order_release);
            last_dump_frame().store(current_frame, std::memory_order_release);
        }
        return;
    }

    if (!repeat_mode()) return;

    const auto last = last_dump_frame().load(std::memory_order_acquire);
    if (current_frame - last < repeat_interval()) return;
    if (dump_now(current_frame) > 0) {
        last_dump_frame().store(current_frame, std::memory_order_release);
    }
}

}  // namespace sn2_root_sig_dump
