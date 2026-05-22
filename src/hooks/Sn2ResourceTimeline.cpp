// Sn2ResourceTimeline.cpp
//
// Per-resource access timeline. Records write and read events for a configurable
// set of resources. Flushes to disk every N frames.

#include "Sn2ResourceTimeline.hpp"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>

#include <Windows.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace sn2_resource_timeline {

namespace {

std::string env_str(const char* name) {
    char buf[2048]{};
    const auto len = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return {};
    return std::string{buf, len};
}

std::unordered_set<uintptr_t> parse_ptr_csv(const std::string& s) {
    std::unordered_set<uintptr_t> out;
    size_t pos = 0;
    while (pos < s.size()) {
        while (pos < s.size() && (s[pos] == ',' || s[pos] == ' ' ||
                                   s[pos] == '\n' || s[pos] == '\r' ||
                                   s[pos] == '\t')) ++pos;
        if (pos >= s.size()) break;
        size_t end = pos;
        while (end < s.size() && s[end] != ',' && s[end] != ' ' &&
               s[end] != '\n' && s[end] != '\r' && s[end] != '\t') ++end;
        std::string tok = s.substr(pos, end - pos);
        pos = end;
        if (tok.empty()) continue;
        char* tail = nullptr;
        const auto v = std::strtoull(tok.c_str(), &tail, 0);
        if (tail != tok.c_str()) out.insert(static_cast<uintptr_t>(v));
    }
    return out;
}

struct State {
    std::mutex mu;
    std::unordered_set<ID3D12Resource*> tracked;
    std::unordered_map<ID3D12Resource*, std::vector<Event>> events;
    std::atomic<uint64_t> frame_counter{0};
    std::atomic<uint64_t> event_in_frame{0};
};

State& state() {
    static State s;
    return s;
}

}  // namespace

bool env_enabled() {
    static const bool e = []() {
        return !env_str("UEVR_SN2_RES_TIMELINE_DIR").empty();
    }();
    return e;
}

const std::string& output_dir() {
    static const std::string s = env_str("UEVR_SN2_RES_TIMELINE_DIR");
    return s;
}

const std::unordered_set<ID3D12Resource*>& tracked_resources_ptrs() {
    auto& s = state();
    std::scoped_lock _{s.mu};
    return s.tracked;
}

void register_track(ID3D12Resource* res) {
    if (res == nullptr || !env_enabled()) return;
    auto& s = state();
    std::scoped_lock _{s.mu};
    s.tracked.insert(res);
}

void note_access(ID3D12Resource* res,
                 uint32_t ps_crc,
                 uint32_t cs_crc,
                 int eye_bucket,
                 bool is_write) {
    if (res == nullptr || !env_enabled()) return;
    auto& s = state();
    {
        std::scoped_lock _{s.mu};
        if (s.tracked.find(res) == s.tracked.end()) return;
        Event e;
        e.ps_crc = ps_crc;
        e.cs_crc = cs_crc;
        e.eye_bucket = eye_bucket;
        e.frame = s.frame_counter.load(std::memory_order_relaxed);
        e.event_in_frame = s.event_in_frame.fetch_add(1, std::memory_order_relaxed);
        e.kind = is_write ? Event::Kind::Write : Event::Kind::Read;
        s.events[res].push_back(e);
    }
}

void on_present() {
    if (!env_enabled()) return;
    auto& s = state();
    const auto frame = s.frame_counter.fetch_add(1, std::memory_order_relaxed) + 1;
    s.event_in_frame.store(0, std::memory_order_release);

    // Flush every 600 frames (~10s at 60fps).
    if ((frame % 600) != 0) return;

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(output_dir(), ec);

    std::unordered_map<ID3D12Resource*, std::vector<Event>> snapshot;
    {
        std::scoped_lock _{s.mu};
        snapshot = s.events;
    }

    for (const auto& [res, evts] : snapshot) {
        nlohmann::json doc;
        doc["resource_ptr"] = reinterpret_cast<uintptr_t>(res);
        doc["event_count"] = evts.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : evts) {
            nlohmann::json j;
            j["ps_crc"] = e.ps_crc;
            j["cs_crc"] = e.cs_crc;
            j["eye"] = e.eye_bucket;
            j["frame"] = e.frame;
            j["event_in_frame"] = e.event_in_frame;
            j["kind"] = (e.kind == Event::Kind::Write) ? "write" : "read";
            arr.push_back(j);
        }
        doc["events"] = arr;
        char fname[256];
        std::snprintf(fname, sizeof(fname), "res_0x%llx.json",
                      (unsigned long long)reinterpret_cast<uintptr_t>(res));
        const auto path = (fs::path(output_dir()) / fname).string();
        std::ofstream f(path);
        if (f.good()) {
            f << doc.dump(2);
        }
    }

    static std::atomic<uint64_t> flush_count{0};
    const auto n = flush_count.fetch_add(1, std::memory_order_relaxed) + 1;
    SPDLOG_WARN("[SN2-ResTimeline] flush#{} {} resources tracked, {} total events",
                n, snapshot.size(),
                [&snapshot]() {
                    size_t t = 0;
                    for (const auto& [r, evs] : snapshot) t += evs.size();
                    return t;
                }());
}

}  // namespace sn2_resource_timeline
