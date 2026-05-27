// Sn2EventLedger.hpp
//
// Per-pass "event ledger" for the StereoScope Python tools. Emits exactly ONE
// compact JSON line per draw / dispatch to a .jsonl file (one JSON object per
// line). Consumed by StereoScope's `contracts.PassEvent` schema.
//
// ADDITIVE + DEFAULT OFF. Gated entirely by UEVR_SN2_EVENT_LEDGER=1. When the
// env is unset/0, every entry point is a cheap early-out (one atomic-free bool
// read) and zero behaviour changes. This module never mutates any D3D12 state.
//
// USAGE
// -----
//   UEVR_SN2_EVENT_LEDGER=1
//       Enable the ledger.
//   UEVR_SN2_EVENT_LEDGER_PATH=C:\path\to\events.jsonl
//       Output path. Default:
//         C:\Users\ellio\AppData\Roaming\UnrealVRMod\Subnautica2-Win64-Shipping\stereoscope_events.jsonl
//   (Also: add "UEVR_SN2_EVENT_LEDGER" to sn2_rootbind_bookkeeping_required()
//    in D3D12Hook.cpp so the per-cmdlist root descriptor tables are actually
//    recorded — otherwise last_*_root_desc_tables are empty and inputs/outputs
//    come back blank.)
//
// SCHEMA (one line, compact; matches StereoScope contracts.PassEvent)
// -------------------------------------------------------------------
//   {"seq":47,"frame":1200,"kind":"dispatch","eye":1,"cs_crc":"0x5af52812",
//    "ps_crc":"","name":"","dims":[7,7,12],
//    "inputs":[{"slot":5,"reg":"t5","via":"root","res":"0x1ec7f0c2060",
//               "desc":{"w":60,"h":34,"d":48,"fmt":"R11G11B10F","dim":"3D"}}],
//    "outputs":[]}
//
//   kind   : "draw" | "dispatch"
//   eye    : 0 (left) | 1 (right) | -1 (unknown)
//   inputs : root-table SRVs that resolve to a resource w/ a desc (reg "tN")
//   outputs: root-table UAVs that resolve to a resource w/ a desc (reg "uN")
//
// KNOWN LIMITATION
// ----------------
//   This ledger only sees resources bound through ROOT DESCRIPTOR TABLES. UE5 /
//   the SN2 fog consumer reads the IntegratedLightScattering volume through the
//   BINDLESS View uniform buffer (an indirected descriptor index), NOT a root
//   slot. Bindless reads are therefore INVISIBLE here — the consumer's bindless
//   fog read will NOT appear as an input. Only root-table bindings are emitted.
//
// IMPLEMENTATION NOTE
// -------------------
//   This header provides the enable gate, the file sink, the DXGI_FORMAT /
//   dimension shorteners, and a small hand-rolled JSON string builder (no JSON
//   library dependency). The two public entry points emit_draw / emit_dispatch
//   are defined in D3D12Hook.cpp (the namespace is reopened there) because they
//   must reference file-scope helpers — CommandListCorrelationState,
//   bindless_heap_registry(), sn2_descriptor_registry, sn2_rt_snapshot's frame
//   counter — that are only fully defined later in that translation unit.

#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>

#include <d3d12.h>

namespace sn2_event_ledger {

// ---- enable gate -----------------------------------------------------------
inline bool enabled() {
    static const bool v = []() {
        const char* e = std::getenv("UEVR_SN2_EVENT_LEDGER");
        return e != nullptr && e[0] != '\0' && e[0] != '0';
    }();
    return v;
}

// Hard cap on total emitted events to bound file size.
inline constexpr uint64_t kMaxEvents = 200000;
// Flush to disk every N lines so a crash still leaves a mostly-complete file.
inline constexpr uint64_t kFlushEvery = 64;

// ---- output sink (lazy-open, append, mutex-guarded) ------------------------
class Sink {
public:
    static Sink& get() {
        static Sink s;
        return s;
    }

    // Returns false once the cap is hit (caller can skip the line build).
    bool reserve() {
        return m_emitted.load(std::memory_order_relaxed) < kMaxEvents;
    }

    void write_line(const std::string& line) {
        std::scoped_lock _{m_mutex};
        if (m_emitted >= kMaxEvents) {
            return;
        }
        if (!m_opened) {
            open_locked();
        }
        if (!m_file.is_open()) {
            return;
        }
        m_file << line << '\n';
        const uint64_t n = ++m_emitted;
        if ((n % kFlushEvery) == 0) {
            m_file.flush();
        }
    }

private:
    Sink() = default;

    void open_locked() {
        m_opened = true;  // only attempt once
        const char* env = std::getenv("UEVR_SN2_EVENT_LEDGER_PATH");
        std::string path = (env != nullptr && env[0] != '\0')
            ? std::string(env)
            : std::string("C:\\Users\\ellio\\AppData\\Roaming\\UnrealVRMod\\"
                          "Subnautica2-Win64-Shipping\\stereoscope_events.jsonl");
        m_file.open(path, std::ios::out | std::ios::app | std::ios::binary);
    }

    std::mutex m_mutex;
    std::ofstream m_file;
    bool m_opened = false;
    std::atomic<uint64_t> m_emitted{0};
};

// Global monotonically-increasing event sequence id.
inline std::atomic<uint64_t>& seq_counter() {
    static std::atomic<uint64_t> s{0};
    return s;
}

// ---- small helpers ---------------------------------------------------------
inline const char* fmt_short(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R11G11B10_FLOAT:        return "R11G11B10F";
    case DXGI_FORMAT_R16G16B16A16_FLOAT:     return "R16G16B16A16F";
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:      return "R8G8B8A8";
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:   return "R10G10B10A2";
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_TYPELESS:           return "D32";
    default:                                 return nullptr;  // caller emits fmt<NN>
    }
}

inline const char* dim_short(D3D12_RESOURCE_DIMENSION d) {
    switch (d) {
    case D3D12_RESOURCE_DIMENSION_TEXTURE2D: return "2D";
    case D3D12_RESOURCE_DIMENSION_TEXTURE3D: return "3D";
    case D3D12_RESOURCE_DIMENSION_BUFFER:    return "BUF";
    default:                                 return "?";
    }
}

// Append `"key":"hex"` style fields with a tiny hand-rolled builder (no JSON lib).
inline void append_hex32(std::string& out, uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08x", v);
    out += buf;
}

inline void append_hexptr(std::string& out, const void* p) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "0x%llx",
                  static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(p)));
    out += buf;
}

inline void append_hex64(std::string& out, uint64_t v) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(v));
    out += buf;
}

inline void append_u64(std::string& out, uint64_t v) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(v));
    out += buf;
}

inline void append_i64(std::string& out, int64_t v) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
    out += buf;
}

inline void append_json_string(std::string& out, const std::string& s) {
    out += "\"";
    for (char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:   out += c; break;
        }
    }
    out += "\"";
}

// Append one binding object: {"slot":S,"reg":"tN"/"uN","via":"root","res":"0x..","desc":{...}}
inline void append_binding(std::string& out, int slot, char reg_prefix, int reg_index,
                           const void* res, const D3D12_RESOURCE_DESC& desc) {
    out += "{\"slot\":";
    append_i64(out, slot);
    out += ",\"reg\":\"";
    out += reg_prefix;
    append_i64(out, reg_index);
    out += "\",\"via\":\"root\",\"res\":\"";
    append_hexptr(out, res);
    out += "\",\"desc\":{\"w\":";
    append_u64(out, static_cast<uint64_t>(desc.Width));
    out += ",\"h\":";
    append_u64(out, static_cast<uint64_t>(desc.Height));
    out += ",\"d\":";
    append_u64(out, static_cast<uint64_t>(desc.DepthOrArraySize));
    out += ",\"fmt\":\"";
    const char* fs = fmt_short(desc.Format);
    if (fs != nullptr) {
        out += fs;
    } else {
        out += "fmt";
        append_i64(out, static_cast<int>(desc.Format));
    }
    out += "\",\"dim\":\"";
    out += dim_short(desc.Dimension);
    out += "\"}}";
}

// Map a StereoTraceBucket-style int (Left=1, Right=2) to eye 0/1/-1.
inline int bucket_to_eye(int bucket) {
    if (bucket == 1) return 0;   // Left
    if (bucket == 2) return 1;   // Right
    return -1;
}

// Build the final JSON line and push it to the sink. Caller supplies the
// already-built `inputs` / `outputs` binding-array bodies (without the [ ]).
inline void emit_line(uint64_t seq, uint64_t frame, const char* kind, int eye,
                      uint32_t cs_crc, uint32_t ps_crc,
                      int dx, int dy, int dz,
                      const std::string& inputs, const std::string& outputs,
                      const std::string& extra_json = {}) {
    std::string line;
    line.reserve(256 + inputs.size() + outputs.size() + extra_json.size());
    line += "{\"seq\":";
    append_u64(line, seq);
    line += ",\"frame\":";
    append_u64(line, frame);
    line += ",\"kind\":\"";
    line += kind;
    line += "\",\"eye\":";
    append_i64(line, eye);
    line += ",\"cs_crc\":\"";
    if (cs_crc != 0) { append_hex32(line, cs_crc); }
    line += "\",\"ps_crc\":\"";
    if (ps_crc != 0) { append_hex32(line, ps_crc); }
    line += "\",\"name\":\"\",\"dims\":[";
    append_i64(line, dx);
    line += ",";
    append_i64(line, dy);
    line += ",";
    append_i64(line, dz);
    line += "],\"inputs\":[";
    line += inputs;
    line += "],\"outputs\":[";
    line += outputs;
    line += "]";
    if (!extra_json.empty()) {
        line += ",";
        line += extra_json;
    }
    line += "}";
    Sink::get().write_line(line);
}

} // namespace sn2_event_ledger
