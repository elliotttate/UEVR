#pragma once

// ── UEVR "Nsight mode" ────────────────────────────────────────────
//
// Opt-in flag that tells UEVR a GPU profiler other than PIX is attached
// to this process — specifically NVIDIA Nsight Graphics. Nsight and PIX
// cannot coexist: both want to be the D3D12 instrumentation layer, and
// PIX's WinPixGpuCapturer.dll detours D3D12 calls in a way that breaks
// Nsight's capture path.
//
// When set, UEVR's Framework constructor skips the WinPixGpuCapturer
// LoadLibrary + sentinel-file watcher. This is the inverse of the
// existing UEVR_DISABLE_PIX_BOOTSTRAP env var — that one was a generic
// "don't load PIX" switch; this one explicitly says *why* (Nsight) so
// the log line and future profiler-specific paths can react.
//
// Independent of dumper mode. You can run Nsight mode + full UEVR for
// captures of normal VR rendering, or Nsight mode + dumper mode for
// reflection-only injection while Nsight watches the GPU.
//
// Two ways to enable (same dual mechanism as DumperMode.hpp):
//   1. Environment variable UEVR_NSIGHT_MODE=1 (also accepts true/yes/on).
//   2. Sentinel file %APPDATA%\UnrealVRMod\<GameExeStem>\nsight_mode
//
// Read once, cached for the lifetime of the process. Zero overhead when
// disabled.

#include <windows.h>
#include <shlobj.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace uevr {

inline bool is_nsight_mode() noexcept {
    static const bool cached = []() noexcept {
        if (const char* env = std::getenv("UEVR_NSIGHT_MODE")) {
            if (std::strcmp(env, "1") == 0) return true;
            if (_stricmp(env, "true") == 0) return true;
            if (_stricmp(env, "yes") == 0) return true;
            if (_stricmp(env, "on") == 0) return true;
        }

        try {
            wchar_t appdata[MAX_PATH] = {0};
            if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appdata) == S_OK) {
                wchar_t exe_path[MAX_PATH] = {0};
                GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
                std::filesystem::path exe_fs(exe_path);
                const auto game_name = exe_fs.stem().wstring();
                std::filesystem::path sentinel = std::filesystem::path(appdata) / L"UnrealVRMod" / game_name / L"nsight_mode";
                if (std::filesystem::exists(sentinel)) return true;
            }
        } catch (...) { /* fall through */ }

        return false;
    }();
    return cached;
}

} // namespace uevr
