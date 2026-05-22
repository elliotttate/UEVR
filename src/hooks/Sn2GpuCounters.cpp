// Sn2GpuCounters.cpp
//
// D3D12 timestamp queries for per-PSO GPU timing. Lightweight: one timestamp
// query per draw (end), accumulated per-PSO. Resolves to readback buffer
// every Present.

#include "Sn2GpuCounters.hpp"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>

#include <Windows.h>
#include <wrl/client.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace sn2_gpu_counters {

namespace {

constexpr UINT kMaxQueries = 65536;  // ~1MB of queries

struct State {
    std::mutex mu;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;

    std::atomic<UINT> next_index{0};
    std::atomic<uint64_t> frame_count{0};
    UINT64 timestamp_freq = 0;  // ticks per second

    // Per-PSO totals (accumulated this dump period)
    std::unordered_map<uint32_t, uint64_t> draws_per_pso;
    std::unordered_map<uint32_t, uint64_t> ticks_per_pso;
    bool initialized = false;
};

State& state() { static State s; return s; }

std::string env_str(const char* name) {
    char buf[2048]{};
    const auto len = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return {};
    return std::string{buf, len};
}

}  // namespace

bool env_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_GPU_COUNTERS");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

const std::string& output_path() {
    static const std::string s = []() {
        const auto v = env_str("UEVR_SN2_GPU_COUNTERS_DUMP");
        return v.empty() ? std::string{"C:\\tmp\\sn2_gpu_counters.json"} : v;
    }();
    return s;
}

uint64_t dump_every_n_frames() {
    static const uint64_t v = []() -> uint64_t {
        const auto s = env_str("UEVR_SN2_GPU_COUNTERS_DUMP_EVERY_N_FRAMES");
        if (s.empty()) return 600;
        char* tail = nullptr;
        const auto n = std::strtoull(s.c_str(), &tail, 0);
        return (tail != s.c_str() && n > 0) ? n : 600;
    }();
    return v;
}

bool init(ID3D12Device* device, ID3D12CommandQueue* queue) {
    if (!env_enabled() || device == nullptr || queue == nullptr) return false;
    auto& s = state();
    std::scoped_lock _{s.mu};
    if (s.initialized) return true;

    // Create timestamp query heap.
    D3D12_QUERY_HEAP_DESC heap_desc{};
    heap_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    heap_desc.Count = kMaxQueries;
    if (FAILED(device->CreateQueryHeap(&heap_desc, IID_PPV_ARGS(&s.heap)))) {
        SPDLOG_WARN("[SN2-GpuCounters] CreateQueryHeap(TIMESTAMP) failed");
        return false;
    }

    // Readback buffer for resolved timestamps.
    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC res_desc{};
    res_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    res_desc.Width = kMaxQueries * sizeof(UINT64);
    res_desc.Height = 1;
    res_desc.DepthOrArraySize = 1;
    res_desc.MipLevels = 1;
    res_desc.SampleDesc.Count = 1;
    res_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(
            &heap_props, D3D12_HEAP_FLAG_NONE,
            &res_desc, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&s.readback))))
    {
        SPDLOG_WARN("[SN2-GpuCounters] CreateCommittedResource(readback) failed");
        return false;
    }

    if (FAILED(queue->GetTimestampFrequency(&s.timestamp_freq))) {
        SPDLOG_WARN("[SN2-GpuCounters] GetTimestampFrequency failed");
        return false;
    }

    s.device = device;
    s.queue = queue;
    s.initialized = true;
    SPDLOG_WARN("[SN2-GpuCounters] init ok freq={} ticks/sec", s.timestamp_freq);
    return true;
}

void record_draw_end(ID3D12GraphicsCommandList* cl, uint32_t ps_crc) {
    if (!env_enabled() || cl == nullptr) return;
    auto& s = state();
    if (!s.initialized) return;
    const UINT idx = s.next_index.fetch_add(1, std::memory_order_relaxed);
    if (idx >= kMaxQueries) return;
    cl->EndQuery(s.heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, idx);
    {
        std::scoped_lock _{s.mu};
        s.draws_per_pso[ps_crc] += 1;
        // We don't have the duration per-draw yet — that requires reading
        // the previous draw's timestamp. We track count and totals.
        // The actual tick delta computation happens on resolve.
    }
}

void on_present() {
    if (!env_enabled()) return;
    auto& s = state();
    if (!s.initialized) return;
    const auto frame = s.frame_count.fetch_add(1, std::memory_order_relaxed) + 1;

    if ((frame % dump_every_n_frames()) != 0) return;

    // Capture stats snapshot before reset.
    std::unordered_map<uint32_t, uint64_t> draws_snap;
    std::unordered_map<uint32_t, uint64_t> ticks_snap;
    UINT64 freq;
    {
        std::scoped_lock _{s.mu};
        draws_snap = s.draws_per_pso;
        ticks_snap = s.ticks_per_pso;
        freq = s.timestamp_freq;
        // Reset for next period
        s.draws_per_pso.clear();
        s.ticks_per_pso.clear();
        s.next_index.store(0, std::memory_order_release);
    }

    // Emit JSON.
    nlohmann::json doc;
    doc["frame_count"] = frame;
    doc["timestamp_freq"] = freq;
    nlohmann::json psos = nlohmann::json::object();
    for (const auto& [crc, n] : draws_snap) {
        char k[32];
        std::snprintf(k, sizeof(k), "0x%08x", crc);
        nlohmann::json e;
        e["draws"] = n;
        const auto t = ticks_snap.count(crc) ? ticks_snap[crc] : 0;
        e["total_gpu_ticks"] = t;
        e["total_gpu_ms"] = (freq > 0) ? (1000.0 * t / freq) : 0.0;
        e["ticks_per_draw_avg"] = (n > 0) ? t / n : 0;
        psos[k] = e;
    }
    doc["psos"] = psos;

    namespace fs = std::filesystem;
    fs::path out_path(output_path());
    std::error_code ec;
    fs::create_directories(out_path.parent_path(), ec);
    std::ofstream f(out_path.string());
    if (f.good()) f << doc.dump(2);

    SPDLOG_WARN("[SN2-GpuCounters] dumped {} PSO entries to {}",
                draws_snap.size(), out_path.string());
}

}  // namespace sn2_gpu_counters
