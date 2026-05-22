// Sn2PixelHistory.cpp — implementation
//
// Ring buffer of recent draws. On query, scan and emit matches.

#include "Sn2PixelHistory.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>

#include <Windows.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace sn2_pixel_history {

namespace {

std::string env_str(const char* name) {
    char buf[2048]{};
    const auto len = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return {};
    return std::string{buf, len};
}

std::unordered_set<uint32_t> parse_crc_csv(const std::string& s) {
    std::unordered_set<uint32_t> out;
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
        const auto v = std::strtoul(tok.c_str(), &tail, 0);
        if (tail != tok.c_str()) out.insert(static_cast<uint32_t>(v));
    }
    return out;
}

struct State {
    std::mutex mu;
    std::vector<DrawRecord> ring;
    size_t write_idx = 0;
    std::atomic<uint64_t> frame_count{0};
    std::atomic<uint64_t> event_in_frame{0};
    size_t capacity = 10000;
};

State& state() { static State s; return s; }

}  // namespace

bool env_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_PIXEL_HISTORY");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

const std::string& output_path() {
    static const std::string s = []() -> std::string {
        const auto v = env_str("UEVR_SN2_PIXEL_HISTORY_DUMP");
        return v.empty() ? std::string{"C:\\tmp\\pixel_history.json"} : v;
    }();
    return s;
}

uint64_t max_draws() {
    static const uint64_t v = []() -> uint64_t {
        const auto s = env_str("UEVR_SN2_PIXEL_HISTORY_MAX_DRAWS");
        if (s.empty()) return 10000;
        char* tail = nullptr;
        const auto n = std::strtoull(s.c_str(), &tail, 0);
        return (tail != s.c_str() && n > 100) ? n : 10000;
    }();
    return v;
}

const std::unordered_set<uint32_t>& track_psos() {
    static const auto s = parse_crc_csv(env_str("UEVR_SN2_PIXEL_HISTORY_TRACK_PSOS"));
    return s;
}

void note_draw(uint32_t ps_crc, int eye_bucket,
               const D3D12_VIEWPORT* vp,
               const D3D12_RECT* scissor,
               ID3D12Resource* const* rtvs, uint32_t rtv_count) {
    if (!env_enabled()) return;
    const auto& filter = track_psos();
    if (!filter.empty() && filter.find(ps_crc) == filter.end()) return;
    auto& s = state();
    std::scoped_lock _{s.mu};
    if (s.ring.empty()) {
        s.capacity = max_draws();
        s.ring.resize(s.capacity);
    }
    DrawRecord& r = s.ring[s.write_idx];
    r.frame = s.frame_count.load(std::memory_order_relaxed);
    r.event_in_frame = s.event_in_frame.fetch_add(1, std::memory_order_relaxed);
    r.ps_crc = ps_crc;
    r.eye_bucket = eye_bucket;
    if (vp != nullptr) {
        r.vp_x = vp->TopLeftX; r.vp_y = vp->TopLeftY;
        r.vp_w = vp->Width; r.vp_h = vp->Height;
    } else {
        r.vp_x = r.vp_y = r.vp_w = r.vp_h = 0.0f;
    }
    if (scissor != nullptr) {
        r.sc_left = scissor->left; r.sc_top = scissor->top;
        r.sc_right = scissor->right; r.sc_bottom = scissor->bottom;
    } else {
        r.sc_left = r.sc_top = 0;
        r.sc_right = static_cast<LONG>(r.vp_w);
        r.sc_bottom = static_cast<LONG>(r.vp_h);
    }
    r.rtv0 = (rtvs != nullptr && rtv_count > 0) ? rtvs[0] : nullptr;
    r.rtv_count = rtv_count;
    s.write_idx = (s.write_idx + 1) % s.capacity;
}

std::vector<DrawRecord> query_pixel(float x, float y) {
    auto& s = state();
    std::scoped_lock _{s.mu};
    std::vector<DrawRecord> hits;
    if (s.ring.empty()) return hits;
    for (const auto& r : s.ring) {
        if (r.frame == 0) continue;  // unused slot
        // Viewport covers
        const bool in_vp =
            x >= r.vp_x && x < (r.vp_x + r.vp_w) &&
            y >= r.vp_y && y < (r.vp_y + r.vp_h);
        if (!in_vp) continue;
        // Scissor covers
        const auto xi = static_cast<LONG>(x);
        const auto yi = static_cast<LONG>(y);
        const bool in_scissor = xi >= r.sc_left && xi < r.sc_right &&
                                yi >= r.sc_top  && yi < r.sc_bottom;
        if (!in_scissor) continue;
        hits.push_back(r);
    }
    return hits;
}

void on_present() {
    if (!env_enabled()) return;
    auto& s = state();
    const auto fc = s.frame_count.fetch_add(1, std::memory_order_relaxed) + 1;
    s.event_in_frame.store(0, std::memory_order_release);

    // Poll for query trigger file every 60 frames.
    if ((fc % 60) != 0) return;
    const char* trigger = "C:\\tmp\\pixel_history_query.txt";
    DWORD attrs = GetFileAttributesA(trigger);
    if (attrs == INVALID_FILE_ATTRIBUTES) return;

    std::ifstream qf(trigger);
    if (!qf.good()) return;
    std::string line;
    std::getline(qf, line);
    qf.close();

    // Parse "x,y"
    float x = 0, y = 0;
    if (std::sscanf(line.c_str(), "%f,%f", &x, &y) != 2) {
        SPDLOG_WARN("[SN2-PixelHistory] query file malformed: '{}'", line);
        DeleteFileA(trigger);
        return;
    }

    const auto hits = query_pixel(x, y);
    nlohmann::json doc;
    doc["query_x"] = x;
    doc["query_y"] = y;
    doc["hit_count"] = hits.size();
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : hits) {
        nlohmann::json j;
        j["frame"] = r.frame;
        j["event_in_frame"] = r.event_in_frame;
        j["ps_crc"] = r.ps_crc;
        j["eye"] = r.eye_bucket;
        j["viewport"] = {r.vp_x, r.vp_y, r.vp_w, r.vp_h};
        j["scissor"] = {r.sc_left, r.sc_top, r.sc_right, r.sc_bottom};
        j["rtv0_ptr"] = reinterpret_cast<uintptr_t>(r.rtv0);
        j["rtv_count"] = r.rtv_count;
        arr.push_back(j);
    }
    doc["draws"] = arr;

    namespace fs = std::filesystem;
    fs::path out("C:\\tmp\\pixel_history_query_out.json");
    std::ofstream f(out.string());
    if (f.good()) f << doc.dump(2);
    DeleteFileA(trigger);
    SPDLOG_WARN("[SN2-PixelHistory] query ({}, {}) → {} hits → {}",
                x, y, hits.size(), out.string());
}

}  // namespace sn2_pixel_history
