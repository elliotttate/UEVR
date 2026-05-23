// Sn2RdCapture.cpp — implementation
//
// Minimal subset of RenderDoc's in-app API (renderdoc_app.h v1.6.0).
// Loads renderdoc.dll from in-process, triggers captures, writes .rdc files
// through RD's standard machinery.

#include "Sn2RdCapture.hpp"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>

#include <Windows.h>
#include <spdlog/spdlog.h>

namespace sn2_capture_sidecar {
void emit(uint64_t seq);
bool env_enabled();
}

namespace sn2_rd_capture {

namespace {

// ---------------------------------------------------------------------------
// Minimal subset of renderdoc_app.h v1.6.0 (MIT licensed).
// We only declare what we use — full API is much larger.
// ---------------------------------------------------------------------------

typedef enum RENDERDOC_Version_ {
    eRENDERDOC_API_Version_1_6_0 = 10600,
} RENDERDOC_Version;

typedef enum RENDERDOC_CaptureOption_ {
    eRENDERDOC_Option_AllowVSync = 0,
    eRENDERDOC_Option_AllowFullscreen = 1,
    eRENDERDOC_Option_APIValidation = 2,
    eRENDERDOC_Option_CaptureCallstacks = 3,
    eRENDERDOC_Option_CaptureCallstacksOnlyDraws = 4,
    eRENDERDOC_Option_DelayForDebugger = 5,
    eRENDERDOC_Option_VerifyMapWrites = 6,
    eRENDERDOC_Option_HookIntoChildren = 7,
    eRENDERDOC_Option_RefAllResources = 8,
    eRENDERDOC_Option_CaptureAllCmdLists = 10,
    eRENDERDOC_Option_DebugOutputMute = 11,
} RENDERDOC_CaptureOption;

typedef int (*pRENDERDOC_SetCaptureOptionU32)(RENDERDOC_CaptureOption opt, uint32_t val);
typedef void (*pRENDERDOC_SetCaptureFilePathTemplate)(const char* pathtemplate);
typedef const char* (*pRENDERDOC_GetCaptureFilePathTemplate)();
typedef void (*pRENDERDOC_TriggerCapture)();
typedef uint32_t (*pRENDERDOC_GetNumCaptures)();
typedef uint32_t (*pRENDERDOC_GetCapture)(uint32_t idx, char* logfile,
                                          uint32_t* pathlength, uint64_t* timestamp);
typedef int (*pRENDERDOC_IsTargetControlConnected)();
typedef void (*pRENDERDOC_StartFrameCapture)(void* device, void* wndHandle);
typedef uint32_t (*pRENDERDOC_EndFrameCapture)(void* device, void* wndHandle);

// Late-injected RD needs SetActiveWindow(device, hwnd) before it can latch
// the API pair. For D3D12 the "device" pointer is the COMMAND QUEUE.
typedef void (*pRENDERDOC_SetActiveWindow)(void* device, void* hwnd);

// Minimal API struct — same offsets as official renderdoc_app.h. Only fields
// we use are non-null after RENDERDOC_GetAPI. Others are present so offset
// matches RD's layout.
struct RENDERDOC_API_1_6_0 {
    void* GetAPIVersion;
    pRENDERDOC_SetCaptureOptionU32 SetCaptureOptionU32;
    void* SetCaptureOptionF32;
    void* GetCaptureOptionU32;
    void* GetCaptureOptionF32;
    void* SetFocusToggleKeys;
    void* SetCaptureKeys;
    void* GetOverlayBits;
    void* MaskOverlayBits;
    void* RemoveHooks_LEGACY;
    void* UnloadCrashHandler;
    pRENDERDOC_SetCaptureFilePathTemplate SetCaptureFilePathTemplate;
    pRENDERDOC_GetCaptureFilePathTemplate GetCaptureFilePathTemplate;
    pRENDERDOC_GetNumCaptures GetNumCaptures;
    pRENDERDOC_GetCapture GetCapture;
    pRENDERDOC_TriggerCapture TriggerCapture;
    pRENDERDOC_IsTargetControlConnected IsTargetControlConnected;
    void* LaunchReplayUI;
    pRENDERDOC_SetActiveWindow SetActiveWindow;
    pRENDERDOC_StartFrameCapture StartFrameCapture;
    void* IsFrameCapturing;
    pRENDERDOC_EndFrameCapture EndFrameCapture;
    void* TriggerMultiFrameCapture;
    void* SetCaptureFileComments;
    void* DiscardFrameCapture;
    void* ShowReplayUI;
    void* SetCaptureTitle;
};

typedef int (*pRENDERDOC_GetAPI)(RENDERDOC_Version version, void** outAPIPointers);

// ---------------------------------------------------------------------------

std::string env_str(const char* name) {
    char buf[2048]{};
    const auto len = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return {};
    return std::string{buf, len};
}

struct State {
    std::mutex mu;
    bool init_attempted = false;
    bool loaded = false;
    HMODULE dll = nullptr;
    RENDERDOC_API_1_6_0* api = nullptr;
    std::atomic<bool> trigger_pending{false};
    // Wildcard-capture state machine. Late-injected RD ignores TriggerCapture()
    // because no window/device pair has been auto-selected. We instead call
    // StartFrameCapture(NULL, NULL) on one Present and EndFrameCapture on the
    // next — that wildcard-captures whichever active graphics API pair
    // happens to be live.
    std::atomic<bool> end_capture_pending{false};
    std::atomic<uint64_t> capture_count_{0};
    std::string last_path;
};

State& state() { static State s; return s; }

// Probe common RD install paths.
HMODULE probe_renderdoc_dll() {
    // Honor explicit env override
    const auto explicit_path = env_str("UEVR_SN2_RD_CAPTURE_DLL");
    if (!explicit_path.empty()) {
        HMODULE h = LoadLibraryA(explicit_path.c_str());
        if (h != nullptr) return h;
        SPDLOG_WARN("[SN2-RdCapture] LoadLibrary('{}') failed (err={})",
                    explicit_path, GetLastError());
    }
    // If already loaded in the process, GetModuleHandle returns it.
    if (HMODULE h = GetModuleHandleA("renderdoc.dll")) return h;
    // Try common install paths.
    const char* candidates[] = {
        R"(C:\Program Files\RenderDoc\renderdoc.dll)",
        R"(C:\Program Files (x86)\RenderDoc\renderdoc.dll)",
        R"(E:\Github\renderdoc\x64\Development\renderdoc.dll)",
        R"(E:\Github\renderdoc\x64\Release\renderdoc.dll)",
        "renderdoc.dll",  // PATH search
    };
    for (const char* p : candidates) {
        if (HMODULE h = LoadLibraryA(p)) {
            SPDLOG_WARN("[SN2-RdCapture] loaded renderdoc.dll from '{}'", p);
            return h;
        }
    }
    return nullptr;
}

}  // namespace

bool env_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_RD_CAPTURE");
        return v && v[0] && v[0] != '0';
    }();
    return e;
}

const std::string& trigger_file_path() {
    static const std::string s = []() -> std::string {
        const auto v = env_str("UEVR_SN2_RD_CAPTURE_TRIGGER_FILE");
        return v.empty() ? std::string{"C:\\tmp\\rd_capture.txt"} : v;
    }();
    return s;
}

const std::string& output_template() {
    static const std::string s = []() -> std::string {
        const auto v = env_str("UEVR_SN2_RD_CAPTURE_OUT_TEMPLATE");
        return v.empty() ? std::string{"C:\\tmp\\uevr_captures\\sn2"} : v;
    }();
    return s;
}

bool also_emit_sidecar() {
    static const bool b = []() {
        const char* v = std::getenv("UEVR_SN2_RD_CAPTURE_ALSO_EMIT_SIDECAR");
        return v && v[0] && v[0] != '0';
    }();
    return b;
}

bool init() {
    if (!env_enabled()) return false;
    auto& s = state();
    std::scoped_lock _{s.mu};
    if (s.init_attempted) return s.loaded;
    s.init_attempted = true;

    s.dll = probe_renderdoc_dll();
    if (s.dll == nullptr) {
        SPDLOG_WARN("[SN2-RdCapture] renderdoc.dll not found. Install RenderDoc "
                    "or set UEVR_SN2_RD_CAPTURE_DLL=<path>");
        return false;
    }
    auto get_api = reinterpret_cast<pRENDERDOC_GetAPI>(
        GetProcAddress(s.dll, "RENDERDOC_GetAPI"));
    if (get_api == nullptr) {
        SPDLOG_WARN("[SN2-RdCapture] renderdoc.dll missing RENDERDOC_GetAPI export");
        return false;
    }
    void* api_ptr = nullptr;
    int ok = get_api(eRENDERDOC_API_Version_1_6_0, &api_ptr);
    if (ok != 1 || api_ptr == nullptr) {
        SPDLOG_WARN("[SN2-RdCapture] RENDERDOC_GetAPI(1.6.0) returned {} api={}",
                    ok, api_ptr);
        return false;
    }
    s.api = static_cast<RENDERDOC_API_1_6_0*>(api_ptr);

    // Ensure output dir exists.
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path out{output_template()};
    fs::create_directories(out.parent_path(), ec);

    // Configure capture options.
    if (s.api->SetCaptureFilePathTemplate) {
        s.api->SetCaptureFilePathTemplate(output_template().c_str());
    }
    if (s.api->SetCaptureOptionU32) {
        // Capture all command lists (important for UEVR's multi-CL workflows)
        s.api->SetCaptureOptionU32(eRENDERDOC_Option_CaptureAllCmdLists, 1);
        // Mute D3D12 debug spam to log
        s.api->SetCaptureOptionU32(eRENDERDOC_Option_DebugOutputMute, 1);
    }

    s.loaded = true;
    SPDLOG_WARN("[SN2-RdCapture] initialized. dll=0x{:x} api=0x{:x}",
                reinterpret_cast<uintptr_t>(s.dll),
                reinterpret_cast<uintptr_t>(s.api));
    SPDLOG_WARN("[SN2-RdCapture] output template: '{}'", output_template());
    SPDLOG_WARN("[SN2-RdCapture] trigger file:    '{}'", trigger_file_path());
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

void on_present(uint64_t frame_count, void* d3d12_queue, void* hwnd) {
    if (!env_enabled()) return;
    auto& s = state();
    if (!s.loaded) {
        // Lazy init — Present is the first reliable place we know graphics is alive.
        if (!s.init_attempted) {
            init();
        }
        if (!s.loaded) return;
    }

    // Tell RD which (queue, window) pair is active. Required for late-injected
    // sessions where RD didn't auto-detect the API pair at device-create time.
    // Cheap to call every frame — RD only does work when the pair changes.
    if (d3d12_queue != nullptr && hwnd != nullptr && s.api && s.api->SetActiveWindow) {
        s.api->SetActiveWindow(d3d12_queue, hwnd);
    }

    // Poll trigger file every 30 frames (~0.5s @ 60fps).
    if ((frame_count % 30) == 0) {
        DWORD attrs = GetFileAttributesA(trigger_file_path().c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES &&
            !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            s.trigger_pending.store(true, std::memory_order_release);
            DeleteFileA(trigger_file_path().c_str());
        }
    }

    // Phase 2: if a wildcard EndFrameCapture is pending from a prior frame,
    // close it now. This finalizes the .rdc that StartFrameCapture armed.
    if (s.end_capture_pending.exchange(false, std::memory_order_acq_rel)) {
        if (s.api && s.api->EndFrameCapture) {
            const uint32_t end_ok = s.api->EndFrameCapture(nullptr, nullptr);
            SPDLOG_WARN("[SN2-RdCapture] EndFrameCapture(NULL,NULL) -> {} at frame {}", end_ok, frame_count);
        }
        // Also fire sidecar / increment counter / record path.
        if (also_emit_sidecar() && sn2_capture_sidecar::env_enabled()) {
            const auto n = s.capture_count_.fetch_add(1, std::memory_order_relaxed) + 1;
            sn2_capture_sidecar::emit(n);
            SPDLOG_WARN("[SN2-RdCapture] auto-emitted sidecar seq={}", n);
        } else {
            s.capture_count_.fetch_add(1, std::memory_order_relaxed);
        }
        if (s.api && s.api->GetNumCaptures && s.api->GetCapture) {
            const uint32_t num = s.api->GetNumCaptures();
            if (num > 0) {
                char pathbuf[1024]{};
                uint32_t pathlen = sizeof(pathbuf);
                uint64_t ts = 0;
                if (s.api->GetCapture(num - 1, pathbuf, &pathlen, &ts)) {
                    std::scoped_lock _{s.mu};
                    s.last_path = std::string(pathbuf, pathlen ? pathlen - 1 : 0);
                    SPDLOG_WARN("[SN2-RdCapture] capture written: {}", s.last_path);
                }
            }
        }
    }

    // Phase 1: if a new capture was triggered, arm wildcard StartFrameCapture
    // (works for late-injected RD where TriggerCapture is a no-op). Also fire
    // TriggerCapture as a belt-and-suspenders fallback for sessions where
    // RD did latch a window/device pair.
    if (s.trigger_pending.exchange(false, std::memory_order_acq_rel)) {
        SPDLOG_WARN("[SN2-RdCapture] arming capture at frame {} (Start + Trigger)", frame_count);
        if (s.api && s.api->StartFrameCapture) {
            s.api->StartFrameCapture(nullptr, nullptr);
        }
        if (s.api && s.api->TriggerCapture) {
            s.api->TriggerCapture();
        }
        s.end_capture_pending.store(true, std::memory_order_release);
    }
}

uint64_t capture_count() {
    return state().capture_count_.load(std::memory_order_relaxed);
}

const std::string& last_capture_file() {
    auto& s = state();
    std::scoped_lock _{s.mu};
    return s.last_path;
}

}  // namespace sn2_rd_capture
