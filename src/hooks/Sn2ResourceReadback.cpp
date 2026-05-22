// Sn2ResourceReadback.cpp — implementation
//
// Uses a queue of pending requests + a private CL + a readback heap pool.
// On trigger, requests are enqueued. We need an existing game CL to ride
// along on for the copy, OR we run our own CL on the device's queue (the
// pattern Sn2EyeScreenshot uses).
//
// Simplest path: run a private CL + queue Signal after each trigger.

#include "Sn2ResourceReadback.hpp"
#include "Sn2UweFogMirrorHook.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

#include <Windows.h>
#include <wrl/client.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

// Defined in D3D12Hook.cpp. Returns the game's primary direct command queue
// (the same one used to call Present), or nullptr if not yet captured.
extern "C" ID3D12CommandQueue* sn2_eye_screenshot_get_command_queue();

namespace sn2_resource_readback {

namespace {

std::string env_str(const char* name) {
    char buf[2048]{};
    const auto len = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return {};
    return std::string{buf, len};
}

std::unordered_set<ID3D12Resource*> parse_ptr_csv(const std::string& s) {
    std::unordered_set<ID3D12Resource*> out;
    size_t pos = 0;
    while (pos < s.size()) {
        while (pos < s.size() && (s[pos] == ',' || s[pos] == ' ')) ++pos;
        if (pos >= s.size()) break;
        size_t end = pos;
        while (end < s.size() && s[end] != ',' && s[end] != ' ') ++end;
        std::string tok = s.substr(pos, end - pos);
        pos = end;
        char* tail = nullptr;
        const auto v = std::strtoull(tok.c_str(), &tail, 0);
        if (tail != tok.c_str()) {
            out.insert(reinterpret_cast<ID3D12Resource*>(static_cast<uintptr_t>(v)));
        }
    }
    return out;
}

struct PendingRequest {
    uint64_t seq;
    ID3D12Resource* src;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    D3D12_RESOURCE_DESC src_desc{};
    uint64_t row_pitch = 0;
    uint64_t total_size = 0;
    uint64_t fence_val = 0;
    bool drained = false;
};

struct State {
    std::mutex mu;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> alloc;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cl;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    HANDLE fence_event = nullptr;
    std::atomic<uint64_t> next_fence{0};
    std::vector<PendingRequest> pending;
    bool initialized = false;
    std::atomic<uint64_t> total_bytes{0};
};

State& state() { static State s; return s; }

// Compute readback buffer size + row pitch for a 2D/3D texture.
void compute_layout(ID3D12Device* dev, const D3D12_RESOURCE_DESC& desc,
                    uint64_t& total_size, uint64_t& row_pitch) {
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT row_count = 0;
    UINT64 row_byte_size = 0;
    UINT64 size = 0;
    dev->GetCopyableFootprints(&desc, 0, 1, 0, &fp, &row_count, &row_byte_size, &size);
    total_size = size;
    row_pitch = fp.Footprint.RowPitch;
}

}  // namespace

bool env_enabled() {
    static const bool e = []() {
        return !env_str("UEVR_SN2_RES_READBACK_DIR").empty();
    }();
    return e;
}

const std::string& output_dir() {
    static const std::string s = env_str("UEVR_SN2_RES_READBACK_DIR");
    return s;
}

bool capture_all_mirrors() {
    static const bool b = []() {
        const char* v = std::getenv("UEVR_SN2_RES_READBACK_MIRRORS");
        return v && v[0] && v[0] != '0';
    }();
    return b;
}

const std::unordered_set<ID3D12Resource*>& explicit_ptrs() {
    static const auto s = parse_ptr_csv(env_str("UEVR_SN2_RES_READBACK_PTRS"));
    return s;
}

bool init(ID3D12Device* device) {
    if (!env_enabled() || device == nullptr) return false;
    auto& s = state();
    std::scoped_lock _{s.mu};
    if (s.initialized) return true;
    s.device = device;

    // We need a queue. Borrow from D3D12Hook's accessor (same pattern Sn2EyeShot uses).
    s.queue = sn2_eye_screenshot_get_command_queue();
    if (s.queue == nullptr) {
        // Defer init; trigger() will retry.
        return false;
    }

    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&s.alloc)))) return false;
    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            s.alloc.Get(), nullptr, IID_PPV_ARGS(&s.cl)))) return false;
    s.cl->Close();
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&s.fence)))) return false;
    s.fence_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);

    s.initialized = true;
    SPDLOG_WARN("[SN2-ResReadback] initialized. output_dir='{}' all_mirrors={} explicit={}",
                output_dir(), capture_all_mirrors(), explicit_ptrs().size());

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(output_dir(), ec);

    return true;
}

size_t trigger(uint64_t seq) {
    if (!env_enabled()) return 0;
    auto& s = state();
    if (!s.initialized) {
        // Get device from the eye_screenshot accessor's queue (lazy)
        auto* q = sn2_eye_screenshot_get_command_queue();
        if (q == nullptr) {
            SPDLOG_WARN("[SN2-ResReadback] trigger seq={}: no command queue yet (game not fully initialized)", seq);
            return 0;
        }
        Microsoft::WRL::ComPtr<ID3D12Device> dev;
        if (FAILED(q->GetDevice(IID_PPV_ARGS(&dev))) || dev == nullptr) {
            SPDLOG_WARN("[SN2-ResReadback] trigger seq={}: GetDevice failed", seq);
            return 0;
        }
        if (!init(dev.Get())) {
            SPDLOG_WARN("[SN2-ResReadback] trigger seq={}: init failed", seq);
            return 0;
        }
        if (!s.initialized) return 0;
    }

    // Build target set
    std::vector<ID3D12Resource*> targets;
    if (capture_all_mirrors()) {
        auto& reg = sn2_uwe_fog_mirror::registry();
        std::scoped_lock rl{reg.mu};
        targets.reserve(reg.map.size());
        for (const auto& [game_ptr, _] : reg.map) targets.push_back(game_ptr);
    }
    for (auto* p : explicit_ptrs()) targets.push_back(p);

    if (targets.empty()) return 0;

    // Begin recording.
    std::scoped_lock _{s.mu};
    s.alloc->Reset();
    s.cl->Reset(s.alloc.Get(), nullptr);

    std::vector<PendingRequest> batch;
    batch.reserve(targets.size());

    for (auto* src : targets) {
        if (src == nullptr) continue;
        const auto desc = src->GetDesc();
        if (desc.Dimension == D3D12_RESOURCE_DIMENSION_UNKNOWN) continue;

        uint64_t total = 0;
        uint64_t row = 0;
        compute_layout(s.device.Get(), desc, total, row);
        if (total == 0 || total > 64 * 1024 * 1024) continue;  // skip >64MB

        // Allocate readback buffer.
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = total;
        bd.Height = 1;
        bd.DepthOrArraySize = 1;
        bd.MipLevels = 1;
        bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        HRESULT hr = s.device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &bd,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&readback));
        if (FAILED(hr) || readback == nullptr) continue;

        // Queue CopyResource (or CopyTextureRegion for textures).
        if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
            s.cl->CopyBufferRegion(readback.Get(), 0, src, 0, total);
        } else {
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
            UINT rc = 0;
            UINT64 rp = 0, sz = 0;
            s.device->GetCopyableFootprints(&desc, 0, 1, 0, &fp, &rc, &rp, &sz);
            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = readback.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint = fp;
            D3D12_TEXTURE_COPY_LOCATION srcLoc{};
            srcLoc.pResource = src;
            srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            srcLoc.SubresourceIndex = 0;
            s.cl->CopyTextureRegion(&dst, 0, 0, 0, &srcLoc, nullptr);
        }

        PendingRequest req;
        req.seq = seq;
        req.src = src;
        req.readback = readback;
        req.src_desc = desc;
        req.row_pitch = row;
        req.total_size = total;
        batch.push_back(std::move(req));
    }

    s.cl->Close();
    if (batch.empty()) return 0;

    // Execute + signal.
    ID3D12CommandList* lists[] = { s.cl.Get() };
    s.queue->ExecuteCommandLists(1, lists);
    const uint64_t fv = s.next_fence.fetch_add(1, std::memory_order_relaxed) + 1;
    s.queue->Signal(s.fence.Get(), fv);
    for (auto& r : batch) r.fence_val = fv;
    s.pending.insert(s.pending.end(),
                      std::make_move_iterator(batch.begin()),
                      std::make_move_iterator(batch.end()));

    SPDLOG_WARN("[SN2-ResReadback] trigger seq={} queued {} reads (total {} pending)",
                seq, batch.size(), s.pending.size());
    return batch.size();
}

void on_present() {
    if (!env_enabled()) return;
    auto& s = state();
    if (!s.initialized || s.pending.empty()) return;

    namespace fs = std::filesystem;
    const uint64_t completed = s.fence ? s.fence->GetCompletedValue() : 0;

    std::vector<PendingRequest> ready;
    {
        std::scoped_lock _{s.mu};
        for (auto it = s.pending.begin(); it != s.pending.end(); ) {
            if (!it->drained && it->fence_val <= completed) {
                ready.push_back(std::move(*it));
                it = s.pending.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (auto& r : ready) {
        // Map readback buffer + dump bytes to disk.
        D3D12_RANGE all{0, static_cast<size_t>(r.total_size)};
        void* mapped = nullptr;
        if (FAILED(r.readback->Map(0, &all, &mapped)) || mapped == nullptr) continue;

        char bin_name[256];
        std::snprintf(bin_name, sizeof(bin_name),
                      "res_seq%04llu_0x%llx_%ux%ux%u_fmt%u.bin",
                      static_cast<unsigned long long>(r.seq),
                      static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(r.src)),
                      static_cast<unsigned>(r.src_desc.Width),
                      static_cast<unsigned>(r.src_desc.Height),
                      static_cast<unsigned>(r.src_desc.DepthOrArraySize),
                      static_cast<unsigned>(r.src_desc.Format));
        const auto bin_path = (fs::path(output_dir()) / bin_name).string();
        std::ofstream f(bin_path, std::ios::binary);
        if (f.good()) {
            f.write(static_cast<const char*>(mapped),
                    static_cast<std::streamsize>(r.total_size));
            s.total_bytes.fetch_add(r.total_size, std::memory_order_relaxed);
        }

        // Sidecar JSON with metadata.
        nlohmann::json meta;
        meta["resource_ptr"] = reinterpret_cast<uintptr_t>(r.src);
        meta["seq"] = r.seq;
        meta["dim_type"] = static_cast<unsigned>(r.src_desc.Dimension);
        meta["width"] = static_cast<unsigned>(r.src_desc.Width);
        meta["height"] = static_cast<unsigned>(r.src_desc.Height);
        meta["depth_or_array"] = static_cast<unsigned>(r.src_desc.DepthOrArraySize);
        meta["format"] = static_cast<unsigned>(r.src_desc.Format);
        meta["row_pitch"] = r.row_pitch;
        meta["total_bytes"] = r.total_size;
        meta["bin_file"] = bin_name;
        char meta_name[256];
        std::snprintf(meta_name, sizeof(meta_name),
                      "res_seq%04llu_0x%llx_meta.json",
                      static_cast<unsigned long long>(r.seq),
                      static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(r.src)));
        std::ofstream mf((fs::path(output_dir()) / meta_name).string());
        if (mf.good()) mf << meta.dump(2);

        D3D12_RANGE no_write{0, 0};
        r.readback->Unmap(0, &no_write);
        r.drained = true;
    }
}

uint64_t bytes_written_total() {
    return state().total_bytes.load(std::memory_order_relaxed);
}

}  // namespace sn2_resource_readback
