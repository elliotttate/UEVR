// Sn2DescriptorLineage.cpp — implementation
//
// Two-map data structure:
//   1. handle_to_origin: SIZE_T → LineageEntry*   (current owner of a slot)
//   2. all_entries: list of LineageEntry          (storage)
//
// note_create: allocate entry, link handle_to_origin[handle] = entry
// note_copy: take src's origin, link handle_to_origin[dst] = same origin,
//            append dst to that origin's copy_chain

#include "Sn2DescriptorLineage.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>

#include <Windows.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace sn2_descriptor_lineage {

namespace {

std::string env_str(const char* name) {
    char buf[2048]{};
    const auto len = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return {};
    return std::string{buf, len};
}

struct State {
    std::mutex mu;
    std::vector<LineageEntry> entries;
    std::unordered_map<SIZE_T, size_t> handle_to_entry_idx;  // handle → index into entries
    std::atomic<uint64_t> frame_count{0};
    std::atomic<uint64_t> event_in_frame{0};
};

State& state() { static State s; return s; }

const char* kind_name(CreateKind k) {
    switch (k) {
        case CreateKind::CBV: return "CBV";
        case CreateKind::SRV: return "SRV";
        case CreateKind::UAV: return "UAV";
        case CreateKind::RTV: return "RTV";
        case CreateKind::DSV: return "DSV";
        default: return "Unknown";
    }
}

}  // namespace

bool env_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_DESCRIPTOR_LINEAGE");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

const std::string& output_path() {
    static const std::string s = []() -> std::string {
        const auto v = env_str("UEVR_SN2_DESCRIPTOR_LINEAGE_DUMP");
        return v.empty() ? std::string{"C:\\tmp\\desc_lineage.json"} : v;
    }();
    return s;
}

uint64_t dump_every_n_frames() {
    static const uint64_t v = []() -> uint64_t {
        const auto s = env_str("UEVR_SN2_DESCRIPTOR_LINEAGE_DUMP_EVERY_N_FRAMES");
        if (s.empty()) return 600;
        char* tail = nullptr;
        const auto n = std::strtoull(s.c_str(), &tail, 0);
        return (tail != s.c_str() && n > 0) ? n : 600;
    }();
    return v;
}

void note_create(CreateKind kind, ID3D12Resource* res, SIZE_T cpu_handle) {
    if (!env_enabled() || cpu_handle == 0) return;
    auto& s = state();
    std::scoped_lock _{s.mu};
    LineageEntry entry{};
    entry.kind = kind;
    entry.resource = res;
    entry.create_frame = s.frame_count.load(std::memory_order_relaxed);
    entry.create_event = s.event_in_frame.fetch_add(1, std::memory_order_relaxed);
    entry.copy_chain.push_back(cpu_handle);  // first "copy" is the create itself
    s.entries.push_back(std::move(entry));
    s.handle_to_entry_idx[cpu_handle] = s.entries.size() - 1;
}

void note_copy(SIZE_T src, SIZE_T dst) {
    if (!env_enabled() || src == 0 || dst == 0) return;
    auto& s = state();
    std::scoped_lock _{s.mu};
    auto it = s.handle_to_entry_idx.find(src);
    if (it == s.handle_to_entry_idx.end()) return;
    const size_t idx = it->second;
    s.entries[idx].copy_chain.push_back(dst);
    s.handle_to_entry_idx[dst] = idx;
}

void note_copy_range(SIZE_T src_start, SIZE_T dst_start, UINT count, UINT stride) {
    if (!env_enabled() || count == 0 || stride == 0) return;
    for (UINT i = 0; i < count; ++i) {
        note_copy(src_start + i * stride, dst_start + i * stride);
    }
}

ID3D12Resource* trace_back(SIZE_T cpu_handle) {
    auto& s = state();
    std::scoped_lock _{s.mu};
    auto it = s.handle_to_entry_idx.find(cpu_handle);
    if (it == s.handle_to_entry_idx.end()) return nullptr;
    return s.entries[it->second].resource;
}

const LineageEntry* lookup_lineage(SIZE_T cpu_handle) {
    auto& s = state();
    std::scoped_lock _{s.mu};
    auto it = s.handle_to_entry_idx.find(cpu_handle);
    if (it == s.handle_to_entry_idx.end()) return nullptr;
    return &s.entries[it->second];
}

void on_present() {
    if (!env_enabled()) return;
    auto& s = state();
    const auto frame = s.frame_count.fetch_add(1, std::memory_order_relaxed) + 1;
    s.event_in_frame.store(0, std::memory_order_release);

    if ((frame % dump_every_n_frames()) != 0) return;

    nlohmann::json doc;
    {
        std::scoped_lock _{s.mu};
        doc["frame"] = frame;
        doc["entry_count"] = s.entries.size();
        doc["handle_map_size"] = s.handle_to_entry_idx.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : s.entries) {
            nlohmann::json j;
            j["kind"] = kind_name(e.kind);
            j["resource_ptr"] = reinterpret_cast<uintptr_t>(e.resource);
            j["create_frame"] = e.create_frame;
            j["create_event"] = e.create_event;
            j["copy_chain_length"] = e.copy_chain.size();
            // Only emit up to 8 handles per entry to keep JSON tractable
            nlohmann::json chain = nlohmann::json::array();
            size_t limit = (e.copy_chain.size() < 8u) ? e.copy_chain.size() : 8u;
            for (size_t i = 0; i < limit; ++i) {
                chain.push_back(static_cast<uint64_t>(e.copy_chain[i]));
            }
            j["copy_chain_sample"] = chain;
            arr.push_back(j);
        }
        doc["entries"] = arr;
    }

    namespace fs = std::filesystem;
    fs::path out_path(output_path());
    std::error_code ec;
    fs::create_directories(out_path.parent_path(), ec);
    std::ofstream f(out_path.string());
    if (f.good()) f << doc.dump(2);

    SPDLOG_WARN("[SN2-DescLineage] frame={} entries={} dumped to {}",
                frame, doc["entry_count"].get<size_t>(), out_path.string());
}

}  // namespace sn2_descriptor_lineage
