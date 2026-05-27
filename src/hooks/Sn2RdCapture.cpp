// Sn2RdCapture.cpp -- SN2-facing trigger wrapper around UEVR's shared
// RenderDoc capture service.

#include "Sn2RdCapture.hpp"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>

#include <spdlog/spdlog.h>

#include "render/RenderDocCaptureService.hpp"

namespace sn2_capture_sidecar {
void emit(uint64_t seq);
bool env_enabled();
}

namespace sn2_rd_capture {
namespace {

namespace rdc = uevr::renderdoc_capture;

struct State {
    std::mutex mu;
    bool init_attempted{};
    bool loaded{};
    std::atomic<bool> trigger_pending{false};
    std::atomic<bool> end_capture_pending{false};
    std::atomic<uint64_t> capture_count_{0};
    rdc::CapturePair pending_pair{};
    std::string last_path;
};

State& state() {
    static State s;
    return s;
}

bool env_flag(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && v[0] != '\0' && v[0] != '0';
}

uint64_t env_u64(const char* name) {
    const char* v = std::getenv(name);
    if (v == nullptr || v[0] == '\0') return 0;
    char* end = nullptr;
    const unsigned long long n = std::strtoull(v, &end, 0);
    if (end == v) return 0;
    return static_cast<uint64_t>(n);
}

bool late_load_allowed() {
    return env_flag("UEVR_SN2_RD_CAPTURE_LOAD_DLL") ||
           env_flag("UEVR_LOAD_RENDERDOC_DLL") ||
           !rdc::env_string_a("UEVR_SN2_RD_CAPTURE_DLL").empty() ||
           !rdc::env_string_a("UEVR_RENDERDOC_DLL").empty();
}

rdc::CapturePair normalise_pair(void* d3d12_device, void* hwnd) {
    if (d3d12_device == nullptr || hwnd == nullptr) {
        return {};
    }
    return {d3d12_device, hwnd};
}

std::string pair_string(rdc::CapturePair pair) {
    char buf[96]{};
    std::snprintf(buf, sizeof(buf), "device=0x%llx hwnd=0x%llx",
                  static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(pair.device)),
                  static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(pair.window)));
    return buf;
}

} // namespace

bool env_enabled() {
    static const bool e = env_flag("UEVR_SN2_RD_CAPTURE");
    return e;
}

const std::string& trigger_file_path() {
    static const std::string s = []() -> std::string {
        const auto v = rdc::env_string_a("UEVR_SN2_RD_CAPTURE_TRIGGER_FILE");
        return v.empty() ? std::string{"C:\\tmp\\rd_capture.txt"} : v;
    }();
    return s;
}

const std::string& output_template() {
    static const std::string s = []() -> std::string {
        const auto v = rdc::env_string_a("UEVR_SN2_RD_CAPTURE_OUT_TEMPLATE");
        return v.empty() ? std::string{"C:\\tmp\\uevr_captures\\sn2"} : v;
    }();
    return s;
}

bool also_emit_sidecar() {
    static const bool b = env_flag("UEVR_SN2_RD_CAPTURE_ALSO_EMIT_SIDECAR");
    return b;
}

uint64_t autocapture_frame() {
    static const uint64_t f = env_u64("UEVR_SN2_RDC_AUTOCAPTURE");
    return f;
}

uint64_t autocapture_every() {
    static const uint64_t k = env_u64("UEVR_SN2_RDC_AUTOCAPTURE_EVERY");
    return k;
}

bool init() {
    if (!env_enabled()) return false;

    auto& s = state();
    std::scoped_lock _{s.mu};
    if (s.init_attempted) return s.loaded;
    s.init_attempted = true;

    const bool allow_late = late_load_allowed();
    auto result = rdc::bootstrap(allow_late);
    if (!result.api_loaded) {
        SPDLOG_WARN("[SN2-RdCapture] RenderDoc API unavailable. Preload renderdoc.dll before "
                    "D3D12 creation, or set UEVR_SN2_RD_CAPTURE_LOAD_DLL=1 for degraded late-load.");
        return false;
    }

    if (!result.capture_safe) {
        SPDLOG_WARN("[SN2-RdCapture] RenderDoc capture safety is degraded from '{}' "
                    "(d3d12_loaded_before_bootstrap={} dxgi_loaded_before_bootstrap={}). "
                    ".rdc capture may be incomplete.",
                    result.loaded_path, result.d3d12_was_loaded, result.dxgi_was_loaded);
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path out{output_template()};
    fs::create_directories(out.parent_path(), ec);

    rdc::set_capture_template(output_template());
    rdc::configure_default_options();

    s.loaded = true;
    SPDLOG_WARN("[SN2-RdCapture] initialized. output template='{}' trigger='{}'",
                output_template(), trigger_file_path());
    return true;
}

bool is_loaded() {
    return state().loaded;
}

void request_capture_next_frame() {
    auto& s = state();
    if (!s.loaded) return;
    s.trigger_pending.store(true, std::memory_order_release);
}

void on_present(uint64_t frame_count, void* d3d12_device, void* hwnd) {
    if (!env_enabled()) return;

    auto& s = state();
    if (!s.loaded) {
        if (!s.init_attempted) {
            init();
        }
        if (!s.loaded) return;
    }

    const rdc::CapturePair pair = normalise_pair(d3d12_device, hwnd);
    rdc::set_active_window(pair);

    if ((frame_count % 30) == 0) {
        DWORD attrs = GetFileAttributesA(trigger_file_path().c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            s.trigger_pending.store(true, std::memory_order_release);
            DeleteFileA(trigger_file_path().c_str());
        }
    }

    {
        static std::atomic<uint64_t> autocap_frame{0};
        const uint64_t af = autocap_frame.fetch_add(1, std::memory_order_relaxed);
        const uint64_t one_shot = autocapture_frame();
        const uint64_t every = autocapture_every();
        bool fire = false;
        if (one_shot != 0 && af == one_shot) fire = true;
        if (every != 0 && af != 0 && (af % every) == 0) fire = true;
        if (fire) {
            SPDLOG_WARN("[SN2-RdCapture] autocapture trigger at internal frame {} (N={} every={})",
                        af, one_shot, every);
            s.trigger_pending.store(true, std::memory_order_release);
        }
    }

    if (s.end_capture_pending.exchange(false, std::memory_order_acq_rel)) {
        rdc::CapturePair pending{};
        {
            std::scoped_lock _{s.mu};
            pending = s.pending_pair;
            s.pending_pair = {};
        }

        bool ended = rdc::end_capture(pending);
        if (!ended && pending.device != nullptr) {
            SPDLOG_WARN("[SN2-RdCapture] EndFrameCapture({}) failed; retrying wildcard",
                        pair_string(pending));
            ended = rdc::end_capture({});
        }
        SPDLOG_WARN("[SN2-RdCapture] EndFrameCapture({}) -> {} at frame {}",
                    pair_string(pending), ended, frame_count);

        if (also_emit_sidecar() && sn2_capture_sidecar::env_enabled()) {
            const auto n = s.capture_count_.fetch_add(1, std::memory_order_relaxed) + 1;
            sn2_capture_sidecar::emit(n);
            SPDLOG_WARN("[SN2-RdCapture] auto-emitted sidecar seq={}", n);
        } else {
            s.capture_count_.fetch_add(1, std::memory_order_relaxed);
        }

        const auto newest = rdc::newest_capture_path();
        if (!newest.empty()) {
            std::scoped_lock _{s.mu};
            s.last_path = newest;
            SPDLOG_WARN("[SN2-RdCapture] capture written: {}", s.last_path);
        }
    }

    if (s.trigger_pending.exchange(false, std::memory_order_acq_rel)) {
        SPDLOG_WARN("[SN2-RdCapture] arming capture at frame {} ({})",
                    frame_count, pair_string(pair));
        if (rdc::start_capture(pair)) {
            {
                std::scoped_lock _{s.mu};
                s.pending_pair = pair;
            }
            s.end_capture_pending.store(true, std::memory_order_release);
        } else {
            SPDLOG_WARN("[SN2-RdCapture] StartFrameCapture failed");
        }
    }
}

uint64_t capture_count() {
    return state().capture_count_.load(std::memory_order_relaxed);
}

const std::string& last_capture_file() {
    static thread_local std::string copy;
    auto& s = state();
    std::scoped_lock _{s.mu};
    copy = s.last_path;
    return copy;
}

} // namespace sn2_rd_capture
