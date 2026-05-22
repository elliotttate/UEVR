// Sn2BindingAnalyzer.cpp — implementation
//
// Observes CBV bindings during the active session, decodes each GPU_VA to
// (parent_resource, byte_offset), accumulates per-(PSO, root_param) samples
// split by eye, and periodically flushes per-PSO LEFT/RIGHT delta JSON to disk.

#include "Sn2BindingAnalyzer.hpp"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace sn2_upload_buf_map {
bool resolve_va(D3D12_GPU_VIRTUAL_ADDRESS gpu_va,
                ID3D12Resource*& out_resource, uint64_t& out_offset);
}

namespace sn2_binding_analyzer {

namespace {

struct Key {
    uintptr_t pso;
    uint32_t root_param;
    char stage;
    bool operator==(const Key& o) const noexcept {
        return pso == o.pso && root_param == o.root_param && stage == o.stage;
    }
};

struct KeyHash {
    std::size_t operator()(const Key& k) const noexcept {
        return std::hash<uintptr_t>{}(k.pso) ^
               (std::hash<uint32_t>{}(k.root_param) << 1) ^
               (static_cast<std::size_t>(k.stage) << 17);
    }
};

struct State {
    std::mutex mu;
    std::unordered_map<Key, PerRootAccum, KeyHash> accum;
    std::atomic<uint64_t> frame_count{0};
    std::atomic<uint64_t> samples_total{0};
    std::atomic<uint64_t> samples_unresolved{0};
};

State& state() {
    static State s;
    return s;
}

// Push a new offset into a per-eye sample list, deduping but keeping order
// (so the JSON is readable). Caps growth so a misbehaving CBV doesn't blow
// memory in long sessions.
inline void push_unique(std::vector<uint64_t>& v, uint64_t off) {
    if (v.size() >= 32) {
        // Replace the oldest if the new offset isn't already present.
        for (auto x : v) if (x == off) return;
        v.erase(v.begin());
        v.push_back(off);
        return;
    }
    for (auto x : v) if (x == off) return;
    v.push_back(off);
}

// Try to derive a single canonical delta from the offset sets. If LEFT and
// RIGHT each have one tight cluster, return their median difference. If
// either is empty, returns 0.
int64_t compute_delta(const std::vector<uint64_t>& left,
                      const std::vector<uint64_t>& right) {
    if (left.empty() || right.empty()) return 0;
    std::vector<uint64_t> l = left;
    std::vector<uint64_t> r = right;
    std::sort(l.begin(), l.end());
    std::sort(r.begin(), r.end());
    const uint64_t lm = l[l.size() / 2];
    const uint64_t rm = r[r.size() / 2];
    return static_cast<int64_t>(rm) - static_cast<int64_t>(lm);
}

std::string hex(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08x", v);
    return std::string{buf};
}

std::string hex_ptr(const void* p) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%llx",
                  static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(p)));
    return std::string{buf};
}

void write_json_locked() {
    if (!env_enabled()) return;
    auto& s = state();
    nlohmann::json doc;
    doc["schema"] = "uevr.sn2.binding_analyzer.v1";
    doc["frame_count"] = s.frame_count.load(std::memory_order_relaxed);
    doc["samples_total"] = s.samples_total.load(std::memory_order_relaxed);
    doc["samples_unresolved"] = s.samples_unresolved.load(std::memory_order_relaxed);

    nlohmann::json psos = nlohmann::json::object();
    for (const auto& [k, a] : s.accum) {
        const std::string pso_key = hex_ptr(reinterpret_cast<const void*>(k.pso));
        if (!psos.contains(pso_key)) {
            psos[pso_key] = nlohmann::json::object();
            psos[pso_key]["ps_crc"] = a.ps_crc != 0 ? hex(a.ps_crc) : "0x00000000";
            psos[pso_key]["cs_crc"] = a.cs_crc != 0 ? hex(a.cs_crc) : "0x00000000";
            psos[pso_key]["roots"] = nlohmann::json::object();
        }
        nlohmann::json& roots = psos[pso_key]["roots"];
        nlohmann::json entry;
        entry["stage"] = std::string{k.stage};
        entry["parent_resource_ptr"] = hex_ptr(a.representative_resource);
        entry["left_offsets"] = a.left_offsets;
        entry["right_offsets"] = a.right_offsets;
        entry["samples_l"] = a.samples_l;
        entry["samples_r"] = a.samples_r;
        entry["samples_unknown"] = a.samples_unknown;
        const int64_t delta = compute_delta(a.left_offsets, a.right_offsets);
        entry["computed_delta"] = delta;
        // Pool-agnostic: only meaningful when both eyes saw the same parent.
        entry["pool_agnostic"] = (a.samples_l > 0 && a.samples_r > 0);
        roots[std::to_string(k.root_param)] = std::move(entry);
    }
    doc["psos"] = std::move(psos);

    std::ofstream f{output_path()};
    if (!f.good()) {
        SPDLOG_WARN("[SN2-BindingAnalyzer] failed to open {}", output_path());
        return;
    }
    f << doc.dump(2);
    SPDLOG_INFO("[SN2-BindingAnalyzer] wrote {} pso entries to {}",
                doc["psos"].size(), output_path());
}

}  // namespace

void record_root_cbv(uintptr_t pso,
                     uint32_t ps_crc,
                     uint32_t cs_crc,
                     int eye_bucket,
                     uint32_t root_param,
                     D3D12_GPU_VIRTUAL_ADDRESS gpu_va,
                     char stage) {
    if (!env_enabled()) return;
    if (pso == 0 || gpu_va == 0) return;

    ID3D12Resource* parent = nullptr;
    uint64_t offset = 0;
    const bool ok = sn2_upload_buf_map::resolve_va(gpu_va, parent, offset);

    auto& s = state();
    s.samples_total.fetch_add(1, std::memory_order_relaxed);
    if (!ok) {
        s.samples_unresolved.fetch_add(1, std::memory_order_relaxed);
        // We still record the gpu_va so callers can see "unresolved" rows.
    }

    const Key key{pso, root_param, stage};
    std::scoped_lock _{s.mu};
    auto& a = s.accum[key];
    a.ps_crc = ps_crc != 0 ? ps_crc : a.ps_crc;
    a.cs_crc = cs_crc != 0 ? cs_crc : a.cs_crc;
    a.stage = stage;
    if (a.representative_resource == nullptr && parent != nullptr) {
        a.representative_resource = parent;
    }

    if (eye_bucket == 0) {
        a.samples_l++;
        if (ok) push_unique(a.left_offsets, offset);
    } else if (eye_bucket == 1) {
        a.samples_r++;
        if (ok) push_unique(a.right_offsets, offset);
    } else {
        a.samples_unknown++;
    }
}

void on_present() {
    if (!env_enabled()) return;
    auto& s = state();
    const auto f = s.frame_count.fetch_add(1, std::memory_order_relaxed) + 1;
    if ((f % flush_every_frames()) != 0) return;
    std::scoped_lock _{s.mu};
    write_json_locked();
}

void flush_now() {
    auto& s = state();
    std::scoped_lock _{s.mu};
    write_json_locked();
}

}  // namespace sn2_binding_analyzer
