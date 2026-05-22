// Sn2RightCbSynth.cpp — implementation

#include "Sn2RightCbSynth.hpp"

#include <atomic>
#include <cstring>
#include <mutex>

#include <wrl/client.h>
#include <spdlog/spdlog.h>

namespace sn2_upload_buf_map {
uint8_t* gpu_va_to_cpu(D3D12_GPU_VIRTUAL_ADDRESS gpu_va, uint64_t min_size);
}

namespace sn2_right_cb_synth {

namespace {

struct State {
    std::mutex mu;
    Microsoft::WRL::ComPtr<ID3D12Resource> upload_buf;
    uint8_t* cpu_ptr{nullptr};
    D3D12_GPU_VIRTUAL_ADDRESS gpu_va{0};
    uint64_t buf_size{0};
    bool initialized{false};
    std::atomic<uint64_t> snapshot_count{0};
    std::atomic<uint64_t> last_snapshot_frame{0};
};

State& state() {
    static State s;
    return s;
}

// SEH-guarded memcpy. Must live in its own function with no C++ objects
// having destructors, because __try/__except can't unwind through them in
// MSVC. Returns true on success, false if src faulted.
bool safe_memcpy_seh(void* dst, const void* src, size_t bytes) {
    __try {
        std::memcpy(dst, src, bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

bool init(ID3D12Device* device) {
    if (!env_enabled() || device == nullptr) return false;
    auto& s = state();
    std::scoped_lock _{s.mu};
    if (s.initialized) return true;

    const uint64_t size = snapshot_size();

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = size;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&s.upload_buf));
    if (FAILED(hr) || s.upload_buf == nullptr) {
        SPDLOG_WARN("[SN2-RightCbSynth] CreateCommittedResource failed hr=0x{:08x}", static_cast<uint32_t>(hr));
        return false;
    }
    D3D12_RANGE no_read{0, 0};
    void* mapped = nullptr;
    hr = s.upload_buf->Map(0, &no_read, &mapped);
    if (FAILED(hr) || mapped == nullptr) {
        SPDLOG_WARN("[SN2-RightCbSynth] Map failed hr=0x{:08x}", static_cast<uint32_t>(hr));
        return false;
    }
    s.cpu_ptr = static_cast<uint8_t*>(mapped);
    std::memset(s.cpu_ptr, 0, size);
    s.gpu_va = s.upload_buf->GetGPUVirtualAddress();
    s.buf_size = size;
    s.initialized = true;
    SPDLOG_WARN("[SN2-RightCbSynth] initialized size={} cpu=0x{:x} gpu_va=0x{:x}",
                size, reinterpret_cast<uintptr_t>(s.cpu_ptr), s.gpu_va);
    return true;
}

bool initialized() {
    return state().initialized;
}

void update_donor(uint32_t crc, int eye, uint32_t root, D3D12_GPU_VIRTUAL_ADDRESS gpu_va) {
    if (!env_enabled()) return;
    if (crc != donor_crc()) return;
    if (root != donor_root()) return;
    if (eye != 1) return;  // only snapshot RIGHT-eye bindings
    if (gpu_va == 0) return;
    auto& s = state();
    if (!s.initialized) return;

    const uint64_t size = snapshot_size();
    uint8_t* src = sn2_upload_buf_map::gpu_va_to_cpu(gpu_va, size);
    if (src == nullptr) return;

    // Throttle.
    const auto n = s.snapshot_count.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n > 1 && (n % 30) != 0) return;

    // SEH-guarded memcpy. The donor's upload buffer can be unmapped/freed
    // between when we cached its CPU pointer and when we try to read it.
    bool ok = false;
    {
        std::scoped_lock _{s.mu};
        ok = safe_memcpy_seh(s.cpu_ptr, src, size);
    }
    if (!ok) {
        static std::atomic<uint64_t> seh_count{0};
        const auto sn = seh_count.fetch_add(1, std::memory_order_relaxed) + 1;
        if (sn <= 4 || (sn % 100) == 0) {
            SPDLOG_WARN("[SN2-RightCbSynth] memcpy AV swallowed (count={}); src=0x{:x}",
                        sn, reinterpret_cast<uintptr_t>(src));
        }
        return;
    }

    if (n <= 4 || (n % 600) == 0) {
        SPDLOG_INFO("[SN2-RightCbSynth] snapshotted donor 0x{:08x} root={} eye={} bytes={} (n={})",
                    crc, root, eye, size, n);
    }
}

D3D12_GPU_VIRTUAL_ADDRESS get_right_va() {
    auto& s = state();
    if (!s.initialized) return 0;
    if (s.snapshot_count.load(std::memory_order_relaxed) == 0) return 0;
    return s.gpu_va;
}

size_t copy_snapshot_bytes(void* out_buf, size_t buf_size) {
    auto& s = state();
    if (!s.initialized) return 0;
    if (s.snapshot_count.load(std::memory_order_relaxed) == 0) return 0;
    if (out_buf == nullptr || buf_size == 0) return 0;
    std::scoped_lock _{s.mu};
    const size_t n = (buf_size < s.buf_size) ? buf_size : static_cast<size_t>(s.buf_size);
    std::memcpy(out_buf, s.cpu_ptr, n);
    return n;
}

uint64_t snapshot_count() {
    return state().snapshot_count.load(std::memory_order_relaxed);
}

}  // namespace sn2_right_cb_synth
