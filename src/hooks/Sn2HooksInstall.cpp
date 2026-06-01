// Sn2HooksInstall.cpp
//
// Install bodies for Sn2RDGPassHook and Sn2MaterialNameHook.
//
// The .hpp headers carry the recording functions + caches; this .cpp carries
// the safetyhook installations + trampolines. Done as a single TU so:
//   - The SafetyHookInline storage objects have one definition
//   - The trampoline functions have stable addresses (required by safetyhook)
//   - The .hpp headers stay light (no <safetyhook.hpp> dependency in headers)
//
// All hooks gated by their respective UEVR_SN2_*_HOOK=1 env vars. Installation
// also requires the corresponding RVA constants to be non-zero (so the hooks
// can ship in builds where the RVAs haven't been verified for a specific
// binary build).

#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <Windows.h>
#include <safetyhook.hpp>
#include <utility/Module.hpp>
#include <spdlog/spdlog.h>

#include "Sn2RDGPassHook.hpp"
#include "Sn2MaterialNameHook.hpp"
#include "Sn2RuntimeState.hpp"
#include "Sn2VsmUbClampPatch.hpp"
#include "mods/VR.hpp"  // VR::get()->get_runtime()->got_first_valid_poses (VR-ready gate)

static bool sn2_is_executable_process_range(uintptr_t address, size_t size) {
    if (address == 0 || size == 0 || address + size < address) {
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) == 0) {
        return false;
    }

    const auto base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    if (address + size > base + mbi.RegionSize) {
        return false;
    }

    if ((mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }

    const auto protect = mbi.Protect & 0xff;
    return protect == PAGE_EXECUTE ||
           protect == PAGE_EXECUTE_READ ||
           protect == PAGE_EXECUTE_READWRITE ||
           protect == PAGE_EXECUTE_WRITECOPY;
}

namespace sn2_hooks_install {

static bool sn2_safe_read(const void* p, void* out, size_t n);

static int sn2_env_int(const char* name, int fallback) {
    char value[64]{};
    const auto len = GetEnvironmentVariableA(name, value, static_cast<DWORD>(sizeof(value)));
    if (len == 0 || len >= sizeof(value)) {
        return fallback;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 0);
    if (end == value) {
        return fallback;
    }
    return static_cast<int>(parsed);
}

static bool sn2_env_flag(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && v[0] != '\0' && v[0] != '0';
}

// ============================================================================
// Sn2RDGPassHook: hook FRDGBuilder::SetupParameterPass(this, FRDGPass*)
// ============================================================================
//
// Layout per IDA round 11c:
//   FRDGPass + 0x10 = FRDGEventName.Name (const wchar_t*)

static SafetyHookInline g_setup_parameter_pass_hook{};
static SafetyHookInline g_execute_pass_hook{};

// Original signature (from IDA): FRDGPass* SetupParameterPass(FRDGBuilder* this, FRDGPass* pass)
static void* __fastcall setup_parameter_pass_trampoline(void* this_ptr, void* pass_ptr) {
    // Call the original first so the pass is fully set up.
    auto result = g_setup_parameter_pass_hook.call<void*>(this_ptr, pass_ptr);

    // Read the pass name field at +0x10. The pass was just constructed by
    // FRDGPass::FRDGPass (round 11c) which always wrote to +0x10 before
    // SetupParameterPass runs, so the read is safe.
    try {
        if (pass_ptr != nullptr) {
            const wchar_t* name = *reinterpret_cast<const wchar_t* const*>(
                reinterpret_cast<const uint8_t*>(pass_ptr) + sn2_rdg_pass_hook::FRDGPASS_NAME_FIELD_OFFSET);
            sn2_rdg_pass_hook::on_setup_parameter_pass(reinterpret_cast<uintptr_t>(pass_ptr), name);
            // RDG event strings are stripped in shipping — reconstruct the pass's render PHASE
            // from the (single-threaded, render-thread) call stack instead.
            sn2_rdg_pass_hook::on_setup_stack(reinterpret_cast<uintptr_t>(pass_ptr));
        }
    } catch (const std::exception& e) {
        SPDLOG_WARN("[SN2-RDGPassHook] setup record failed: {}", e.what());
    } catch (...) {
        SPDLOG_WARN("[SN2-RDGPassHook] setup record failed: unknown exception");
    }

    return result;
}

// Original signature:
//   void FRDGBuilder::ExecutePass(FRHIComputeCommandList& RHICmdList, FRDGPass* Pass)
static void __fastcall execute_pass_trampoline(void* rhi_cmd_list, void* pass_ptr) {
    uintptr_t previous_pass = 0;
    std::string previous_name;
    try {
        previous_pass = sn2_rdg_pass_hook::s_tls_current_rdg_pass;
        previous_name = sn2_rdg_pass_hook::s_tls_current_rdg_pass_name;

        if (pass_ptr != nullptr) {
            const wchar_t* name = *reinterpret_cast<const wchar_t* const*>(
                reinterpret_cast<const uint8_t*>(pass_ptr) + sn2_rdg_pass_hook::FRDGPASS_NAME_FIELD_OFFSET);
            sn2_rdg_pass_hook::on_setup_parameter_pass(reinterpret_cast<uintptr_t>(pass_ptr), name);
            sn2_rdg_pass_hook::on_execute_pass_begin(reinterpret_cast<uintptr_t>(pass_ptr));
        }
    } catch (const std::exception& e) {
        SPDLOG_WARN("[SN2-RDGPassHook] execute begin failed: {}", e.what());
    } catch (...) {
        SPDLOG_WARN("[SN2-RDGPassHook] execute begin failed: unknown exception");
    }

    g_execute_pass_hook.call<void>(rhi_cmd_list, pass_ptr);

    try {
        if (pass_ptr != nullptr &&
            sn2_rdg_pass_hook::s_tls_current_rdg_pass == reinterpret_cast<uintptr_t>(pass_ptr)) {
            sn2_rdg_pass_hook::s_tls_current_rdg_pass = previous_pass;
            sn2_rdg_pass_hook::s_tls_current_rdg_pass_name = std::move(previous_name);
        }
    } catch (const std::exception& e) {
        SPDLOG_WARN("[SN2-RDGPassHook] execute end failed: {}", e.what());
    } catch (...) {
        SPDLOG_WARN("[SN2-RDGPassHook] execute end failed: unknown exception");
    }
}

static bool install_rdg_pass_hook() {
    if (!sn2_rdg_pass_hook::env_enabled()) {
        SPDLOG_INFO("[SN2-RDGPassHook] disabled (UEVR_SN2_RDG_PASS_HOOK not set)");
        return false;
    }
    if (sn2_rdg_pass_hook::SUBNAUTICA2_FRDG_SETUP_PARAMETER_PASS_RVA == 0) {
        SPDLOG_WARN("[SN2-RDGPassHook] RVA is 0; skipping install");
        return false;
    }

    const auto exe_base = reinterpret_cast<uintptr_t>(utility::get_executable());
    const auto target = exe_base + sn2_rdg_pass_hook::SUBNAUTICA2_FRDG_SETUP_PARAMETER_PASS_RVA;
    if (exe_base == 0 || !sn2_is_executable_process_range(target, 0x20)) {
        SPDLOG_WARN("[SN2-RDGPassHook] bad target VA 0x{:x}", target);
        return false;
    }

    // Sanity: SetupParameterPass should start with `40 56 57 48 83 EC 68` per
    // IDA round 11c. Mismatch → skip rather than risk a hook on the wrong code.
    static constexpr uint8_t k_expected_prologue[] = {
        0x40, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x68
    };
    if (std::memcmp(reinterpret_cast<void*>(target), k_expected_prologue, sizeof(k_expected_prologue)) != 0) {
        SPDLOG_WARN("[SN2-RDGPassHook] prologue mismatch at 0x{:x}; binary likely changed", target);
        return false;
    }

    g_setup_parameter_pass_hook = safetyhook::create_inline(
        reinterpret_cast<void*>(target),
        reinterpret_cast<void*>(&setup_parameter_pass_trampoline),
        safetyhook::InlineHook::StartDisabled);

    if (!g_setup_parameter_pass_hook) {
        SPDLOG_WARN("[SN2-RDGPassHook] safetyhook create failed at 0x{:x}", target);
        return false;
    }
    if (auto e = g_setup_parameter_pass_hook.enable(); !e.has_value()) {
        SPDLOG_WARN("[SN2-RDGPassHook] enable failed at 0x{:x}: {}", target, static_cast<int>(e.error().type));
        return false;
    }
    SPDLOG_INFO("[SN2-RDGPassHook] installed at 0x{:x} (RVA 0x{:x})",
        target, sn2_rdg_pass_hook::SUBNAUTICA2_FRDG_SETUP_PARAMETER_PASS_RVA);

    if (sn2_rdg_pass_hook::SUBNAUTICA2_FRDG_EXECUTE_PASS_RVA == 0) {
        SPDLOG_WARN("[SN2-RDGPassHook] ExecutePass RVA is 0; pass execution will not be bracketed");
        return true;
    }
    const auto execute_target = exe_base + sn2_rdg_pass_hook::SUBNAUTICA2_FRDG_EXECUTE_PASS_RVA;
    if (!sn2_is_executable_process_range(execute_target, 0x20)) {
        SPDLOG_WARN("[SN2-RDGPassHook] bad ExecutePass target VA 0x{:x}", execute_target);
        return true;
    }

    // Binfold-symbolized Subnautica 2 build, FRDGBuilder::ExecutePass:
    // 48 89 5c 24 10 48 89 6c 24 18 48 89 74 24 20 57 ...
    static constexpr uint8_t k_execute_pass_prologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C
    };
    if (std::memcmp(reinterpret_cast<void*>(execute_target), k_execute_pass_prologue, sizeof(k_execute_pass_prologue)) != 0) {
        SPDLOG_WARN("[SN2-RDGPassHook] ExecutePass prologue mismatch at 0x{:x}; binary likely changed", execute_target);
        return true;
    }

    g_execute_pass_hook = safetyhook::create_inline(
        reinterpret_cast<void*>(execute_target),
        reinterpret_cast<void*>(&execute_pass_trampoline),
        safetyhook::InlineHook::StartDisabled);
    if (!g_execute_pass_hook) {
        SPDLOG_WARN("[SN2-RDGPassHook] ExecutePass safetyhook create failed at 0x{:x}", execute_target);
        return true;
    }
    if (auto e = g_execute_pass_hook.enable(); !e.has_value()) {
        SPDLOG_WARN("[SN2-RDGPassHook] ExecutePass enable failed at 0x{:x}: {}", execute_target, static_cast<int>(e.error().type));
        return true;
    }
    SPDLOG_INFO("[SN2-RDGPassHook] ExecutePass installed at 0x{:x} (RVA 0x{:x})",
        execute_target, sn2_rdg_pass_hook::SUBNAUTICA2_FRDG_EXECUTE_PASS_RVA);
    return true;
}

// ============================================================================
// Sn2MaterialNameHook: hook FBasePassMeshProcessor::AddMeshBatch
// ============================================================================
//
// Layout per IDA round 12:
//   FMeshBatch + 0xA8 (168) = MaterialRenderProxy (FMaterialRenderProxy*)
//   FMaterialRenderProxy vtable+0x38 (vtable[7]) = GetFallback
//   FMaterialRenderProxy vtable+0x30 (vtable[6]) = GetMaterialNoFallback (returns FMaterial*)
//
// We don't call those vtable methods — they're virtual and per-stereo-feature-
// level, and risk reentering UE5 systems. Instead we cache the raw proxy
// pointer and emit it; downstream tooling can resolve names via the binfold
// symbol map.

inline constexpr uint64_t k_fmeshbatch_material_render_proxy_offset = 0xA8;

static SafetyHookInline g_add_mesh_batch_hook{};
static SafetyHookInline g_try_add_mesh_batch_hook{};
static SafetyHookInline g_slw_add_mesh_batch_hook{};
static SafetyHookInline g_frdg_builder_execute_hook{};
static SafetyHookInline g_generate_dynamic_mesh_draw_commands_hook{};
static SafetyHookMid g_finish_gather_viewcommands_hook{};

// Pass-id-probe observations (UEVR_SN2_PASS_ID_PROBE=1). Two parallel
// AddMeshBatch hooks (FBasePassMeshProcessor + FSingleLayerWaterPassMeshProcessor)
// each tag observations of the target shape. Their counts together resolve
// "which mesh pass owns 0x13B00F0C" deterministically.
struct Sn2PassObservation {
    std::atomic<uint64_t> count{0};
    std::atomic<uint64_t> shape_hit_count{0};
    std::atomic<uint64_t> left_eye_hit_count{0};
    std::atomic<uint64_t> right_eye_hit_count{0};
};
static Sn2PassObservation g_sn2_obs_basepass;
static Sn2PassObservation g_sn2_obs_slw;
static std::atomic<uintptr_t> g_sn2_target_mesh_ptr{0};
static std::atomic<uintptr_t> g_sn2_target_primitive_ptr{0};
static std::atomic<uintptr_t> g_sn2_target_static_mesh_request_ptr{0};
static std::atomic<uintptr_t> g_sn2_target_primitive_scene_info_ptr{0};
static std::atomic<uintptr_t> g_sn2_target_scene_ptr{0};
static std::atomic<uintptr_t> g_sn2_target_scene_primitives_off{0};
static std::atomic<uintptr_t> g_sn2_target_scene_proxies_off{0};
static std::atomic<uintptr_t> g_sn2_visibility_task_scene_ptr{0};
static std::atomic<int32_t> g_sn2_target_scene_primitive_index{-1};
static std::atomic<int32_t> g_sn2_target_static_mesh_id{-1};
static std::atomic<int32_t> g_sn2_target_primitive_index_hint{-1};

static bool sn2_tryadd_trace_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_TRYADD_TRACE");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

static int sn2_tryadd_trace_max_rows() {
    static const int rows = []() {
        const int v = sn2_env_int("UEVR_SN2_TRYADD_TRACE_MAX", 512);
        return v < 0 ? 0 : v;
    }();
    return rows;
}

static bool sn2_tryadd_trace_target_shape_only() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_TRYADD_TRACE_TARGET_SHAPE_ONLY");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

static bool sn2_addmesh_trace_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_ADDMESH_TRACE");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

static int sn2_addmesh_trace_max_rows() {
    static const int rows = []() {
        const int v = sn2_env_int("UEVR_SN2_ADDMESH_TRACE_MAX", 512);
        return v < 0 ? 0 : v;
    }();
    return rows;
}

static bool sn2_addmesh_trace_target_shape_only() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_ADDMESH_TRACE_TARGET_SHAPE_ONLY");
        return v == nullptr || v[0] == '\0' || v[0] != '0';
    }();
    return e;
}

static bool sn2_addmesh_trace_stack_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_ADDMESH_TRACE_STACK");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

static bool sn2_target_identity_scan_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_TARGET_IDENTITY_SCAN");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

static bool sn2_target_identity_verbose_scan_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_TARGET_IDENTITY_VERBOSE_SCAN");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

static int sn2_target_identity_scan_max_rows() {
    static const int rows = []() {
        const int v = sn2_env_int("UEVR_SN2_TARGET_IDENTITY_SCAN_MAX", 24);
        return v < 0 ? 0 : v;
    }();
    return rows;
}

static int sn2_target_identity_scene_scan_bytes() {
    static const int bytes = []() {
        const int v = sn2_env_int("UEVR_SN2_TARGET_IDENTITY_SCENE_SCAN_BYTES", 0x8000);
        return v < 0 ? 0 : v;
    }();
    return bytes;
}

static int sn2_target_identity_scene_scan_max_elems() {
    static const int elems = []() {
        const int v = sn2_env_int("UEVR_SN2_TARGET_IDENTITY_SCENE_SCAN_MAX_ELEMS", 200000);
        return v < 0 ? 0 : v;
    }();
    return elems;
}

static bool sn2_view_vis_scan_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_VIEW_VIS_SCAN");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

static int sn2_view_vis_scan_max_rows() {
    static const int rows = []() {
        const int v = sn2_env_int("UEVR_SN2_VIEW_VIS_SCAN_MAX", 64);
        return v < 0 ? 0 : v;
    }();
    return rows;
}

static bool sn2_fviewinfo_indexed_array_scan_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_FVIEWINFO_INDEXED_ARRAY_SCAN");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

static int sn2_fviewinfo_indexed_array_scan_max_rows() {
    static const int rows = []() {
        const int v = sn2_env_int("UEVR_SN2_FVIEWINFO_INDEXED_ARRAY_SCAN_MAX", 64);
        return v < 0 ? 0 : v;
    }();
    return rows;
}

static bool sn2_viewcommands_trace_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_VIEWCOMMANDS_TRACE");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

static int sn2_viewcommands_trace_max_rows() {
    static const int rows = []() {
        const int v = sn2_env_int("UEVR_SN2_VIEWCOMMANDS_TRACE_MAX", 48);
        return v < 0 ? 0 : v;
    }();
    return rows;
}

static bool sn2_viewcommands_copy_right_13b_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_VIEWCOMMANDS_COPY_RIGHT_13B");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

static bool sn2_meshcmd_copy_right_13b_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_MESHCMD_COPY_RIGHT_13B");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

static bool sn2_alias_slw_pass_ptr_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_ALIAS_SLW_PASS_PTR");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

static bool sn2_force_dev_build_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_FORCE_DEV_BUILD");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

static int sn2_viewcommands_copy_delay_ms() {
    static const int ms = []() {
        const int v = sn2_env_int("UEVR_SN2_VIEWCOMMANDS_COPY_DELAY_MS", 25000);
        return v < 0 ? 0 : v;
    }();
    return ms;
}

static bool sn2_viewcommands_copy_debug_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_VIEWCOMMANDS_COPY_DEBUG");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

static int sn2_viewcommands_copy_debug_max() {
    static const int rows = []() {
        const int v = sn2_env_int("UEVR_SN2_VIEWCOMMANDS_COPY_DEBUG_MAX", 128);
        return v < 0 ? 0 : v;
    }();
    return rows;
}

static int sn2_stability_streak_required() {
    static const int frames = []() {
        const int v = sn2_env_int("UEVR_SN2_STABILITY_STREAK", 60);
        return v < 0 ? 0 : v;
    }();
    return frames;
}

static int sn2_stability_quarantine_frames() {
    static const int frames = []() {
        const int v = sn2_env_int("UEVR_SN2_STABILITY_QUARANTINE", 0);
        return v < 0 ? 0 : v;
    }();
    return frames;
}

static bool sn2_stability_require_scene_count() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_STABILITY_REQUIRE_SCENE_COUNT");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

static bool sn2_viewcommands_copy_past_delay() {
    static const ULONGLONG start_tick = GetTickCount64();
    const auto delay_ms = static_cast<ULONGLONG>(sn2_viewcommands_copy_delay_ms());
    return GetTickCount64() - start_tick >= delay_ms;
}

static void sn2_log_viewcommands_copy_delay_once() {
    static std::atomic<bool> logged{false};
    bool expected = false;
    if (logged.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
        SPDLOG_WARN(
            "[SN2-ViewCommandsCopy] delayed for {} ms after process start to avoid framework/init races",
            sn2_viewcommands_copy_delay_ms());
    }
}

// True only once the VR runtime has begun submitting real, tracked frames.
// NOTE: runtime->ready()/session_ready goes true too early (during OpenXR's
// "waiting for first valid poses" window), so we key off got_first_valid_poses
// — the actual "OpenXR/OpenVR is completely open and rendering" signal.
static bool sn2_vr_rendering_started() {
    auto& vr = VR::get();
    if (!vr) {
        return false;
    }
    auto* rt = vr->get_runtime();
    if (rt == nullptr) {
        return false;
    }
    if (rt->got_first_valid_poses) {
        return true;
    }
    // Escape hatch for a DEGRADED OpenXR runtime that reports valid-but-untracked
    // view poses (the headless sim sometimes never sets got_first_valid_poses, so
    // the strict gate would deadlock every secondary-view path forever). Default
    // OFF — the clean behaviour is got_first_valid_poses only. When the env is set
    // to N>0, accept "rendering started" once we are WELL past any init race:
    // got_first_poses seen AND internal_frame_count >= N. Keep N high (thousands)
    // so this honours "don't run before OpenXR is fully open".
    static const uint32_t fallback_frames = []() -> uint32_t {
        const int v = sn2_env_int("UEVR_SN2_VR_READY_FRAME_FALLBACK", 0);
        return v > 0 ? static_cast<uint32_t>(v) : 0u;
    }();
    if (fallback_frames != 0 && rt->got_first_poses &&
        rt->internal_frame_count >= fallback_frames) {
        static std::atomic<bool> logged{false};
        bool expected = false;
        if (logged.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
            SPDLOG_WARN(
                "[SN2-VRReady] FALLBACK engaged: got_first_valid_poses still false but "
                "got_first_poses=1 and internal_frame_count={} >= {} — treating VR as "
                "rendering (degraded runtime). Secondary-view paths now permitted.",
                rt->internal_frame_count, fallback_frames);
        }
        return true;
    }
    return false;
}

// Combined gate for SN2 secondary-view MUTATIONS (the per-fire copies/brute
// scans in the ViewCommands midhook). Three guards, all env-tunable:
//   1. Never run before VR is fully open + rendering (sn2_vr_rendering_started).
//   2. Throttle to 1-in-N midhook fires (UEVR_SN2_MUTATION_THROTTLE, default 30)
//      so the per-fire MeshCommands brute scan can't pin the frame rate (~0.1fps).
//   3. Hard-cap total attempts (UEVR_SN2_MUTATION_MAX_ATTEMPTS, default 600) so a
//      never-succeeding lever eventually stops scanning entirely.
static bool sn2_secondary_mutation_gate_ok(uint64_t fire_seq) {
    if (!sn2_vr_rendering_started()) {
        return false;
    }
    static const uint64_t throttle = []() {
        const int v = sn2_env_int("UEVR_SN2_MUTATION_THROTTLE", 30);
        return v > 0 ? static_cast<uint64_t>(v) : 1ull;
    }();
    if (throttle > 1 && (fire_seq % throttle) != 0) {
        return false;
    }
    static const int max_attempts = sn2_env_int("UEVR_SN2_MUTATION_MAX_ATTEMPTS", 600);
    if (max_attempts > 0) {
        static std::atomic<int> attempts{0};
        if (attempts.fetch_add(1, std::memory_order_relaxed) >= max_attempts) {
            return false;
        }
    }
    return true;
}

static bool sn2_fviewinfo_bundle_copy_right_13b_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_FVIEWINFO_BUNDLE_COPY_RIGHT_13B");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

// Pass-id probe — settles "which EMeshPass owns 0x13B00F0C" via parallel
// BasePass + SLW AddMeshBatch hooks. Each logs `[SN2-PassIdProbe]
// processor=BASEPASS|SLW pass=2|5 stereo=1|2 ...` on shape-hit. Companion to
// the RenderDoc capture path in SN2_PASS17_OPEN_QUESTIONS_2026_05_28.md §2.
static bool sn2_pass_id_probe_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_PASS_ID_PROBE");
        return v != nullptr && v[0] != '\0' && v[0] != '0' && v[0] != 'f' && v[0] != 'F';
    }();
    return e;
}

static int sn2_pass_id_probe_max() {
    static const int v = sn2_env_int("UEVR_SN2_PASS_ID_PROBE_MAX", 64);
    return v < 0 ? 0 : v;
}

// Path C — read FRDGPass names from the runtime FRDGBuilder.Passes array
// to bypass the WITH_PROFILEGPU=0 compile-time strip of RDG event markers.
// FRDGPass.Name @ +0x10 is FRDGEventName; FRDGEventName.EventFormat @ +0x00
// is a const wchar_t* pointing to the literal passed to RDG_EVENT_NAME(...).
// That pointer SURVIVES shipping strip even though the PIX3*Event calls don't.
static bool sn2_rdg_pass_names_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_RDG_PASS_NAMES");
        return v != nullptr && v[0] != '\0' && v[0] != '0' && v[0] != 'f' && v[0] != 'F';
    }();
    return e;
}

static int sn2_rdg_pass_names_max() {
    static const int v = sn2_env_int("UEVR_SN2_RDG_PASS_NAMES_MAX", 300);
    return v < 0 ? 0 : v;
}

static bool sn2_rdg_pass_names_every_frame() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_RDG_PASS_NAMES_EVERY_FRAME");
        return v != nullptr && v[0] != '\0' && v[0] != '0' && v[0] != 'f' && v[0] != 'F';
    }();
    return e;
}

// Legacy PMDCP alias probe from the pass-5/17 branch. The SN2 pass enum is
// shifted versus stock UE (SLW=6, pass17=TranslucencyAfterDOF), but the
// 0x13B00F0C draw-owning processor still needs to be resolved independently.
// Aliasing a pass pointer is never parallax-safe and should not be used as a fix.
static int sn2_alias_pmdcp_slot() {
    static const int v = sn2_env_int("UEVR_SN2_ALIAS_PMDCP_SLOT", -1);
    return v;  // -1 = disabled, [0..40] = slot index to alias
}

// Configurable target mesh-pass — supersedes hardcoded `mesh_pass == 17`
// in the runtime copy infrastructure. Runtime AddMesh/D3D12 traces currently
// point at Translucent BasePass for 0x13B00F0C, so the default is BasePass (2).
// Use 6 for the shifted SingleLayerWater pass if follow-up identity work proves
// the SLW processor owns the final teal draw.
static int sn2_target_mesh_pass() {
    static const int v = sn2_env_int("UEVR_SN2_TARGET_MESH_PASS", 2);
    return v;
}

static int sn2_target_primitive_index_override() {
    static const int v = []() {
        return sn2_env_int("UEVR_SN2_TARGET_PRIMITIVE_INDEX_OVERRIDE", -1);
    }();
    return v;
}

static uintptr_t sn2_numvisible_offset_override() {
    static const uintptr_t v = []() -> uintptr_t {
        const int raw = sn2_env_int("UEVR_SN2_NUMVISIBLE_OFFSET_OVERRIDE", 0);
        return raw > 0 ? static_cast<uintptr_t>(raw) : 0;
    }();
    return v;
}

static int32_t sn2_underwater_target_mesh_pass() {
    static const int32_t pass = []() -> int32_t {
        // 0x13B00F0C binds TranslucentBasePass and the live AddMesh/D3D12
        // traces currently point at FBasePassMeshProcessor. Keep this
        // configurable because SLW can share basepass uniform-buffer layouts.
        const int raw = sn2_env_int("UEVR_SN2_UNDERWATER_TARGET_MESH_PASS", 2);
        return (raw >= 0 && raw < 64) ? static_cast<int32_t>(raw) : 2;
    }();
    return pass;
}

static int sn2_addmesh_trace_stack_max() {
    static const int rows = []() {
        const int v = sn2_env_int("UEVR_SN2_ADDMESH_TRACE_STACK_MAX", 32);
        return v < 0 ? 0 : v;
    }();
    return rows;
}

static bool sn2_gendyn_trace_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_GENDYN_TRACE");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

static int sn2_gendyn_trace_max_rows() {
    static const int rows = []() {
        const int v = sn2_env_int("UEVR_SN2_GENDYN_TRACE_MAX", 256);
        return v < 0 ? 0 : v;
    }();
    return rows;
}

static int sn2_gendyn_trace_max_elems() {
    static const int rows = []() {
        const int v = sn2_env_int("UEVR_SN2_GENDYN_TRACE_MAX_ELEMS", 512);
        return v < 0 ? 0 : v;
    }();
    return rows;
}

static int sn2_gendyn_trace_scan_bytes_per_elem() {
    static const int bytes = []() {
        const int v = sn2_env_int("UEVR_SN2_GENDYN_TRACE_SCAN_BYTES_PER_ELEM", 0x18);
        return v < 0 ? 0 : v;
    }();
    return bytes;
}

static int sn2_gendyn_trace_pass_filter() {
    static const int pass = []() {
        return sn2_env_int("UEVR_SN2_GENDYN_TRACE_PASS", 2);
    }();
    return pass;
}

static bool sn2_gendyn_dup_static_right_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_GENDYN_DUP_STATIC_RIGHT_13B");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

static int sn2_gendyn_dup_static_right_delay_ms() {
    static const int ms = []() {
        const int v = sn2_env_int("UEVR_SN2_GENDYN_DUP_STATIC_RIGHT_DELAY_MS", 25000);
        return v < 0 ? 0 : v;
    }();
    return ms;
}

static int sn2_gendyn_dup_static_right_max_total() {
    static const int rows = []() {
        const int v = sn2_env_int("UEVR_SN2_GENDYN_DUP_STATIC_RIGHT_MAX_TOTAL", 16);
        return v < 0 ? 0 : v;
    }();
    return rows;
}

static uintptr_t sn2_main_module_base() {
    static const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    return base;
}

static uint32_t sn2_main_module_size() {
    static const uint32_t size = []() -> uint32_t {
        const auto base = sn2_main_module_base();
        if (base == 0) {
            return 0;
        }
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
            return 0;
        }
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) {
            return 0;
        }
        return nt->OptionalHeader.SizeOfImage;
    }();
    return size;
}

static bool sn2_main_module_rva(uintptr_t address, uintptr_t& rva_out) {
    const auto base = sn2_main_module_base();
    const auto size = static_cast<uintptr_t>(sn2_main_module_size());
    if (base == 0 || size == 0 || address < base || address >= base + size) {
        return false;
    }
    rva_out = address - base;
    return true;
}

static uintptr_t sn2_read_vtable_rva(const void* object_ptr) {
    uintptr_t vtable = 0;
    if (object_ptr == nullptr || !sn2_safe_read(object_ptr, &vtable, sizeof(vtable))) {
        return 0;
    }
    uintptr_t rva = 0;
    return sn2_main_module_rva(vtable, rva) ? rva : 0;
}

static std::string sn2_format_vtable_func_rvas(const void* object_ptr, uint32_t max_entries) {
    uintptr_t vtable = 0;
    if (object_ptr == nullptr || !sn2_safe_read(object_ptr, &vtable, sizeof(vtable)) || vtable == 0) {
        return "none";
    }

    std::string out;
    char chunk[64]{};
    for (uint32_t i = 0; i < max_entries; ++i) {
        uintptr_t fn = 0;
        if (!sn2_safe_read(reinterpret_cast<const void*>(vtable + sizeof(uintptr_t) * i), &fn, sizeof(fn))) {
            break;
        }
        uintptr_t rva = 0;
        if (!sn2_main_module_rva(fn, rva)) {
            continue;
        }
        std::snprintf(chunk, sizeof(chunk), "%s%u:0x%llx", out.empty() ? "" : ",",
            i,
            static_cast<unsigned long long>(rva));
        out += chunk;
    }
    return out.empty() ? "none" : out;
}

static bool sn2_addmesh_force_right_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_ADDMESH_FORCE_RIGHT_13B");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

static int sn2_addmesh_force_right_max_age() {
    static const int rows = []() {
        const int v = sn2_env_int("UEVR_SN2_ADDMESH_FORCE_RIGHT_MAX_AGE", 8192);
        return v < 0 ? 0 : v;
    }();
    return rows;
}

static int sn2_addmesh_force_right_delay_ms() {
    static const int ms = []() {
        const int v = sn2_env_int("UEVR_SN2_ADDMESH_FORCE_RIGHT_DELAY_MS", 25000);
        return v < 0 ? 0 : v;
    }();
    return ms;
}

static int sn2_addmesh_force_right_max_total() {
    static const int rows = []() {
        const int v = sn2_env_int("UEVR_SN2_ADDMESH_FORCE_RIGHT_MAX_TOTAL", 16);
        return v < 0 ? 0 : v;
    }();
    return rows;
}

static bool sn2_addmesh_force_right_global_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_ADDMESH_FORCE_RIGHT_USE_GLOBAL");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

static bool sn2_addmesh_force_right_execute_unsafe_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_ADDMESH_FORCE_RIGHT_EXECUTE_UNSAFE");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

static bool sn2_fviewinfo_dynamic_range_copy_right_13b_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_FVIEWINFO_DYNAMIC_RANGE_COPY_RIGHT_13B");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

static bool sn2_meshcmd_bruteforce_enabled();  // fwd decl (defined ~:1553)

static bool sn2_target_mesh_discovery_needed() {
    return sn2_addmesh_trace_enabled() ||
           sn2_addmesh_force_right_enabled() ||
           sn2_gendyn_dup_static_right_enabled() ||
           sn2_viewcommands_copy_right_13b_enabled() ||
           sn2_fviewinfo_bundle_copy_right_13b_enabled() ||
           sn2_fviewinfo_dynamic_range_copy_right_13b_enabled() ||
           // MESHCMD copy/brute also need the target mesh discovered, else
           // g_sn2_target_mesh_ptr stays 0 and the copy bails at :1739
           // (cached_target_mesh=0x0). Was missing -> lever never fired.
           sn2_meshcmd_copy_right_13b_enabled() ||
           sn2_meshcmd_bruteforce_enabled();
}

struct Sn2TryAddMeshProbe {
    uint32_t direct_hit_value{0};
    uint32_t direct_hit_offset{0xffffffffu};
    uintptr_t element_array{0};
    int32_t element_count{0};
    uint32_t element_hit_value{0};
    uint32_t element_hit_offset{0xffffffffu};
};

struct Sn2TArrayHeader {
    uintptr_t data{0};
    int32_t count{0};
    int32_t capacity{0};
    bool valid{false};
};

struct Sn2GenDynTls {
    bool active{false};
    uint64_t seq{0};
    uintptr_t view{0};
    int32_t view_index{0};
    int32_t stereo_pass{-999};
    int32_t shading_path{0};
    int32_t mesh_pass{0};
    uintptr_t processor{0};
    Sn2TArrayHeader dynamic_meshes{};
    Sn2TArrayHeader pass_masks{};
    Sn2TArrayHeader static_mesh_requests{};
};

thread_local Sn2GenDynTls g_sn2_gendyn_tls{};

static bool sn2_safe_write(void* p, const void* src, size_t n) {
    if (p == nullptr || src == nullptr) return false;
    __try {
        std::memcpy(p, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

inline constexpr uintptr_t SN2_VIS_TASK_SCENE_OFF = 0x18;
inline constexpr uintptr_t SN2_VIS_TASK_VIEWS_DATA_OFF = 0x20;
inline constexpr uintptr_t SN2_VIS_TASK_VIEWS_COUNT_OFF = 0x28;
inline constexpr uintptr_t SN2_VIS_TASK_VIEWCOMMANDS_INLINE_OFF = 0xA0;
inline constexpr uintptr_t SN2_VIS_TASK_VIEWCOMMANDS_DATA_OFF = 0x2200;
inline constexpr uintptr_t SN2_VIS_TASK_VIEWCOMMANDS_COUNT_OFF = 0x2208;
inline constexpr uintptr_t SN2_FSCENE_PRIMITIVES_DIA_OFF = 0x2BB8;
inline constexpr uintptr_t SN2_FSCENE_PRIMITIVE_PROXIES_DIA_OFF = 0x2BE0;
// Editor DIA says FPrimitiveSceneProxy::PrimitiveSceneInfo is +0x208.
// SN2 shipping has shown a stable -0x40 drift for this field in live logs
// (+0x1C8). Try both and require FPrimitiveSceneInfo::Proxy (+0x8) to
// round-trip before trusting either.
inline constexpr uintptr_t SN2_FPRIMITIVE_PROXY_SCENE_INFO_DIA_OFF = 0x208;
inline constexpr uintptr_t SN2_FPRIMITIVE_PROXY_SCENE_INFO_SHIPPING_OFF = 0x1C8;
// Editor DIA says FPrimitiveSceneInfo::PackedIndex is +0x188. Shipping may
// drift, so use this only as a cross-check against the robust FScene.Primitives
// array index, not as the primary resolver.
inline constexpr uintptr_t SN2_FPRIMITIVE_SCENE_INFO_PACKED_INDEX_DIA_OFF = 0x188;
inline constexpr uintptr_t SN2_FVIEWINFO_STRIDE = 0x29D0;
inline constexpr uintptr_t SN2_FVIEWCOMMANDS_SIZE = 0x858;
inline constexpr uintptr_t SN2_FVIEWCOMMANDS_MESHCOMMANDS_OFF = 0x000;
inline constexpr uintptr_t SN2_FVIEWCOMMANDS_NUM_BUILD_OFF = 0x290;
inline constexpr uintptr_t SN2_FVIEWCOMMANDS_BUILD_REQUESTS_OFF = 0x338;
inline constexpr uintptr_t SN2_FVIEWCOMMANDS_BUILD_FLAGS_OFF = 0x5C8;
inline constexpr uintptr_t SN2_FVIEW_PRIMITIVE_VISIBILITY_MAP_OFF = 0x1C30;
inline constexpr uintptr_t SN2_FVIEW_PRIMITIVE_RAYTRACING_VISIBILITY_MAP_OFF = 0x1C50;
inline constexpr uintptr_t SN2_FVIEW_PRIMITIVE_DEFINITELY_UNOCCLUDED_MAP_OFF = 0x1C70;
inline constexpr uintptr_t SN2_FVIEW_POTENTIALLY_FADING_PRIMITIVE_MAP_OFF = 0x1C90;
inline constexpr uintptr_t SN2_FVIEW_PRIMITIVE_FADE_UNIFORM_BUFFERS_OFF = 0x1CB0;
inline constexpr uintptr_t SN2_FVIEW_PRIMITIVE_FADE_UNIFORM_BUFFER_MAP_OFF = 0x1CC0;
inline constexpr uintptr_t SN2_FVIEW_DITHER_FADE_IN_UNIFORM_BUFFER_OFF = 0x1CE0;
inline constexpr uintptr_t SN2_FVIEW_DITHER_FADE_OUT_UNIFORM_BUFFER_OFF = 0x1CE8;

struct Sn2ProxyPrimitiveSceneInfo {
    bool found{false};
    uintptr_t psi{0};
    uintptr_t proxy_off{UINTPTR_MAX};
    uintptr_t proxy_backref{0};
};

static Sn2ProxyPrimitiveSceneInfo sn2_resolve_proxy_primitive_scene_info(const void* primitive_proxy) {
    Sn2ProxyPrimitiveSceneInfo out{};
    const auto proxy = reinterpret_cast<uintptr_t>(primitive_proxy);
    if (proxy < 0x10000) {
        return out;
    }

    static constexpr uintptr_t kOffsets[] = {
        SN2_FPRIMITIVE_PROXY_SCENE_INFO_DIA_OFF,
        SN2_FPRIMITIVE_PROXY_SCENE_INFO_SHIPPING_OFF,
    };

    for (const uintptr_t off : kOffsets) {
        uintptr_t psi = 0;
        uintptr_t backref = 0;
        if (!sn2_safe_read(reinterpret_cast<const void*>(proxy + off), &psi, sizeof(psi)) ||
            psi < 0x10000 ||
            !sn2_safe_read(reinterpret_cast<const void*>(psi + 0x8), &backref, sizeof(backref)) ||
            backref != proxy) {
            continue;
        }

        out.found = true;
        out.psi = psi;
        out.proxy_off = off;
        out.proxy_backref = backref;
        return out;
    }

    return out;
}
// Live shipping validation wins over editor DIA naming here: 0x1CE0 decodes
// as the 432-entry FPrimitiveViewRelevance TArray in SN2 112084 captures.
inline constexpr uintptr_t SN2_FVIEW_PRIMITIVE_VIEW_RELEVANCE_MAP_OFF = 0x1CE0;
inline constexpr uintptr_t SN2_FVIEW_STATIC_MESH_VISIBILITY_MAP_OFF = 0x1D00;
inline constexpr uintptr_t SN2_FVIEW_STATIC_MESH_FADE_OUT_DITHERED_LOD_MAP_OFF = 0x1D20;
inline constexpr uintptr_t SN2_FVIEW_STATIC_MESH_FADE_IN_DITHERED_LOD_MAP_OFF = 0x1D40;
// Same live layout note as above: 0x1D50 is the valid FLODMask TArray in the
// captured shipping view, while the editor PDB-derived 0x1D60 reads garbage.
inline constexpr uintptr_t SN2_FVIEW_PRIMITIVES_LOD_MASK_OFF = 0x1D50;
inline constexpr uintptr_t SN2_FVIEW_NUM_VISIBLE_DYNAMIC_MESH_ELEMENTS_OFF = 0x1D88;
inline constexpr uintptr_t SN2_FVIEW_DYNAMIC_MESH_ELEMENTS_OFF = 0x1FF8;
inline constexpr uintptr_t SN2_FVIEW_DYNAMIC_MESH_ELEMENT_RANGES_OFF = 0x2008;
inline constexpr uintptr_t SN2_FVIEW_DYNAMIC_MESH_ELEMENTS_PASS_RELEVANCE_OFF = 0x2018;
inline constexpr uintptr_t SN2_FVIEW_RENDER_FLAGS_DWORD_OFF = 0x22D8;
inline constexpr uint32_t SN2_FVIEW_HAS_SLW_MATERIAL_BIT = (1u << 14);

// SN2 (UWE) EMeshPass mapping — decoded from GetMeshPassName() switch at
// RVA 0x1426545F0 + jump table at 0x14265475C + UTF-16-LE name table at
// 0x149C6D210 (per Agent D's RE 2026-05-28, runs/SN2_PASS17_DECOMP_STATIC).
//
// UWE inserted TWO custom passes vs stock UE 5.6:
//   - "SkyPassTranslucent" at index 5 (between SkyPass and SingleLayerWaterPass)
//   - "DistortionDiscard" at index 12 (between Distortion and Velocity)
// Everything from index 5 onward is shifted +2 vs stock UE 5.6. 41 passes total.
//
// CRITICAL: pass 17 in SN2 is `TranslucencyAfterDOF`, NOT stock UE's
// `TranslucencyAfterMotionBlur`. Runtime code targeting `mesh_pass == 17`
// for the SLW fix is wrong by structural enum shift — SLW lives at SN2 index 6.
static const char* sn2_mesh_pass_name(int32_t pass) {
    switch (pass) {
    case 0:  return "DepthPass";
    case 1:  return "SecondStageDepthPass";
    case 2:  return "BasePass";
    case 3:  return "AnisotropyPass";
    case 4:  return "SkyPass";
    case 5:  return "SkyPassTranslucent";          // UWE-added
    case 6:  return "SingleLayerWaterPass";        // shifted from stock 5
    case 7:  return "SingleLayerWaterDepthPrepass";// shifted from stock 6
    case 8:  return "CSMShadowDepth";              // shifted from stock 7
    case 9:  return "VSMShadowDepth";              // shifted from stock 8
    case 10: return "OnePassPointLightShadowDepth";// shifted from stock 9
    case 11: return "Distortion";                  // shifted from stock 10
    case 12: return "DistortionDiscard";           // UWE-added
    case 13: return "Velocity";                    // shifted from stock 11
    case 14: return "TranslucentVelocity";         // shifted from stock 12
    case 15: return "TranslucencyStandard";        // shifted from stock 13
    case 16: return "TranslucencyStandardModulate";// shifted from stock 14
    case 17: return "TranslucencyAfterDOF";        // shifted from stock 15 (NOT TranslucencyAfterMotionBlur)
    case 18: return "TranslucencyAfterDOFModulate";// shifted from stock 16
    case 19: return "TranslucencyAfterMotionBlur"; // shifted from stock 17
    case 20: return "TranslucencyHoldout";
    case 21: return "TranslucencyAll";
    case 22: return "LightmapDensity";
    case 23: return "DebugViewMode";
    case 24: return "EditorSelection";
    case 25: return "LumenCardCapture";
    case 26: return "LumenCardNanite";
    case 27: return "VirtualTexture";
    case 28: return "DitheredLODFadingOutMaskPass";
    case 29: return "NaniteMeshPass";
    // SN2 has 41 passes total (vs stock 39); 30..40 derived from continuation of stock enum +2
    default: return "Other";
    }
}

static bool sn2_tryadd_target_value(uint32_t v) {
    // 0x13B00F0C is a 76608-index TriangleList draw: 25536 primitives.
    return v == 76608u || v == 25536u;
}

static Sn2TryAddMeshProbe sn2_probe_mesh_batch_shape(const void* mesh_ptr);

static Sn2TArrayHeader sn2_read_tarray_header(const void* tarray_ptr) {
    Sn2TArrayHeader out{};
    if (tarray_ptr == nullptr) {
        return out;
    }
    const auto* p = reinterpret_cast<const uint8_t*>(tarray_ptr);
    out.valid =
        sn2_safe_read(p, &out.data, sizeof(out.data)) &&
        sn2_safe_read(p + 8, &out.count, sizeof(out.count)) &&
        sn2_safe_read(p + 12, &out.capacity, sizeof(out.capacity));
    if (!out.valid) {
        out = {};
        return out;
    }
    if (out.count < 0 || out.count > 100000 || out.capacity < out.count || out.capacity > 100000) {
        out.valid = false;
    }
    return out;
}

struct Sn2TArrayEntryProbe {
    Sn2TArrayHeader hdr{};
    bool entry_read{false};
    bool entry_nonzero{false};
    uint64_t raw0{0};
    uint64_t raw1{0};
};

static Sn2TArrayEntryProbe sn2_probe_tarray_entry(uintptr_t owner,
                                                  uintptr_t field_off,
                                                  int32_t elem_index,
                                                  size_t elem_size) {
    Sn2TArrayEntryProbe out{};
    if (owner == 0 || elem_index < 0 || elem_size == 0 || elem_size > 16) {
        return out;
    }

    out.hdr = sn2_read_tarray_header(reinterpret_cast<const void*>(owner + field_off));
    if (!out.hdr.valid || out.hdr.data < 0x10000 || out.hdr.count <= elem_index) {
        return out;
    }

    uint8_t bytes[16]{};
    const uintptr_t entry = out.hdr.data + static_cast<uintptr_t>(elem_index) * elem_size;
    out.entry_read = sn2_safe_read(reinterpret_cast<const void*>(entry), bytes, elem_size);
    if (!out.entry_read) {
        return out;
    }

    std::memcpy(&out.raw0, bytes, elem_size > sizeof(uint64_t) ? sizeof(uint64_t) : elem_size);
    if (elem_size > sizeof(uint64_t)) {
        std::memcpy(&out.raw1, bytes + sizeof(uint64_t), elem_size - sizeof(uint64_t));
    }
    for (size_t i = 0; i < elem_size; ++i) {
        if (bytes[i] != 0) {
            out.entry_nonzero = true;
            break;
        }
    }
    return out;
}

static uint64_t sn2_read_u64_or_zero(const void* p) {
    uint64_t v = 0;
    if (p != nullptr) {
        sn2_safe_read(p, &v, sizeof(v));
    }
    return v;
}

static std::string sn2_read_qwords_hex(const void* p, size_t qword_count) {
    if (p == nullptr || qword_count == 0) {
        return "none";
    }
    qword_count = std::min<size_t>(qword_count, 8);
    std::string out;
    char chunk[40]{};
    for (size_t i = 0; i < qword_count; ++i) {
        const uint64_t v = sn2_read_u64_or_zero(reinterpret_cast<const uint8_t*>(p) + i * sizeof(uint64_t));
        std::snprintf(
            chunk,
            sizeof(chunk),
            "%s0x%llx",
            out.empty() ? "" : ",",
            static_cast<unsigned long long>(v));
        out += chunk;
    }
    return out;
}

static int sn2_read_bit_from_words(uintptr_t words, int32_t bit_index, uint32_t& word_out) {
    word_out = 0;
    if (words == 0 || bit_index < 0) {
        return -1;
    }
    const auto word_index = static_cast<uintptr_t>(bit_index / 32);
    if (!sn2_safe_read(reinterpret_cast<const void*>(words + word_index * sizeof(uint32_t)),
            &word_out,
            sizeof(word_out))) {
        return -1;
    }
    return (word_out & (1u << (bit_index & 31))) != 0 ? 1 : 0;
}

static bool sn2_decode_bitarray_candidate(
    uintptr_t object,
    size_t field_off,
    size_t ptr_off,
    size_t num_off,
    int32_t bit_index,
    uintptr_t& data_out,
    int32_t& num_bits_out,
    int32_t& max_bits_out,
    int& bit_out,
    uint32_t& word_out)
{
    data_out = 0;
    num_bits_out = 0;
    max_bits_out = 0;
    bit_out = -1;
    word_out = 0;

    uintptr_t data = 0;
    int32_t num_bits = 0;
    int32_t max_bits = 0;
    const auto base = object + field_off;
    if (!sn2_safe_read(reinterpret_cast<const void*>(base + ptr_off), &data, sizeof(data)) ||
        !sn2_safe_read(reinterpret_cast<const void*>(base + num_off), &num_bits, sizeof(num_bits)) ||
        !sn2_safe_read(reinterpret_cast<const void*>(base + num_off + sizeof(int32_t)), &max_bits, sizeof(max_bits))) {
        return false;
    }

    if (data < 0x10000 ||
        num_bits <= bit_index ||
        num_bits > 2000000 ||
        max_bits < num_bits ||
        max_bits > 4000000 ||
        (max_bits & 31) != 0) {
        return false;
    }

    const int bit = sn2_read_bit_from_words(data, bit_index, word_out);
    if (bit < 0) {
        return false;
    }
    data_out = data;
    num_bits_out = num_bits;
    max_bits_out = max_bits;
    bit_out = bit;
    return true;
}

static void sn2_scan_view_visibility_state(
    uint64_t gen_seq,
    const void* view_ptr,
    int32_t view_index,
    int32_t stereo_pass,
    int32_t mesh_pass)
{
    if (!sn2_view_vis_scan_enabled() || view_ptr == nullptr) {
        return;
    }

    static std::atomic<uint64_t> emitted{0};
    const auto row = emitted.fetch_add(1, std::memory_order_relaxed) + 1;
    if (row > static_cast<uint64_t>(sn2_view_vis_scan_max_rows())) {
        return;
    }

    const auto view = reinterpret_cast<uintptr_t>(view_ptr);
    int32_t primitive_index = g_sn2_target_primitive_index_hint.load(std::memory_order_relaxed);
    if (primitive_index <= 0) {
        primitive_index = 644;
    }
    int32_t static_mesh_id = g_sn2_target_static_mesh_id.load(std::memory_order_relaxed);
    if (static_mesh_id <= 0) {
        static_mesh_id = 403;
    }

    std::string bit_candidates;
    char chunk[256]{};
    uint32_t bit_candidate_count = 0;
    for (size_t field_off = 0x1b80; field_off < 0x2110 && bit_candidate_count < 32; field_off += 8) {
        static constexpr size_t k_ptr_offsets[] = {0, 8, 16, 24, 32};
        static constexpr size_t k_num_offsets[] = {8, 16, 24, 32, 40, 48};
        for (size_t ptr_off : k_ptr_offsets) {
            for (size_t num_off : k_num_offsets) {
                if (num_off == ptr_off) {
                    continue;
                }
                uintptr_t data = 0;
                int32_t num_bits = 0;
                int32_t max_bits = 0;
                int prim_bit = -1;
                uint32_t prim_word = 0;
                if (!sn2_decode_bitarray_candidate(
                        view,
                        field_off,
                        ptr_off,
                        num_off,
                        primitive_index,
                        data,
                        num_bits,
                        max_bits,
                        prim_bit,
                        prim_word)) {
                    continue;
                }
                uint32_t static_word = 0;
                const int static_bit = sn2_read_bit_from_words(data, static_mesh_id, static_word);
                std::snprintf(
                    chunk,
                    sizeof(chunk),
                    "%soff=0x%zx/ptr+0x%zx/num+0x%zx data=0x%llx num=%d max=%d prim%d=%d w=0x%08x static%d=%d sw=0x%08x",
                    bit_candidates.empty() ? "" : " | ",
                    field_off,
                    ptr_off,
                    num_off,
                    static_cast<unsigned long long>(data),
                    num_bits,
                    max_bits,
                    primitive_index,
                    prim_bit,
                    prim_word,
                    static_mesh_id,
                    static_bit,
                    static_word);
                bit_candidates += chunk;
                ++bit_candidate_count;
                break;
            }
            if (bit_candidate_count >= 32) {
                break;
            }
        }
    }
    if (bit_candidates.empty()) {
        bit_candidates = "none";
    }

    std::string array_candidates;
    uint32_t array_candidate_count = 0;
    for (size_t field_off = 0x1b80; field_off < 0x2110 && array_candidate_count < 24; field_off += 8) {
        const auto hdr = sn2_read_tarray_header(reinterpret_cast<const void*>(view + field_off));
        if (!hdr.valid || hdr.data < 0x10000 || hdr.count <= primitive_index || hdr.count > 2000000) {
            continue;
        }

        uint64_t raw16_a = 0;
        uint64_t raw16_b = 0;
        const auto rel16 = hdr.data + static_cast<uintptr_t>(primitive_index) * 16u;
        sn2_safe_read(reinterpret_cast<const void*>(rel16), &raw16_a, sizeof(raw16_a));
        sn2_safe_read(reinterpret_cast<const void*>(rel16 + sizeof(uint64_t)), &raw16_b, sizeof(raw16_b));

        std::snprintf(
            chunk,
            sizeof(chunk),
            "%soff=0x%zx data=0x%llx count=%d cap=%d rel16[%d]=0x%016llx_%016llx",
            array_candidates.empty() ? "" : " | ",
            field_off,
            static_cast<unsigned long long>(hdr.data),
            hdr.count,
            hdr.capacity,
            primitive_index,
            static_cast<unsigned long long>(raw16_b),
            static_cast<unsigned long long>(raw16_a));
        array_candidates += chunk;
        ++array_candidate_count;
    }
    if (array_candidates.empty()) {
        array_candidates = "none";
    }

    SPDLOG_WARN(
        "[SN2-ViewVisScan] row={} gen#{} view=0x{:x} view_index={} stereo={} pass={} "
        "primitive_index={} static_mesh_id={} bitarrays={} arrays={}",
        row,
        gen_seq,
        view,
        view_index,
        stereo_pass,
        mesh_pass,
        primitive_index,
        static_mesh_id,
        bit_candidates.c_str(),
        array_candidates.c_str());
}

struct Sn2DynamicMeshArrayProbe {
    uint32_t target_values{0};
    uint32_t elems_with_target{0};
    uint32_t known_mesh_hits{0};
    uint32_t known_primitive_hits{0};
    uint32_t first_elem{0xffffffffu};
    uint32_t first_offset{0xffffffffu};
    uint32_t first_value{0};
    uintptr_t first_mesh_candidate{0};
    uintptr_t first_primitive{0};
    uint32_t first_relevance_flags{0};
    uint64_t first_mask{0};
    uint32_t first_known_elem{0xffffffffu};
    uintptr_t first_known_mesh{0};
    uintptr_t first_known_primitive{0};
    uint32_t first_known_relevance_flags{0};
    uint64_t first_known_mask{0};
};

static uint64_t sn2_read_mesh_pass_mask(const Sn2TArrayHeader& masks, uint32_t elem) {
    if (!masks.valid || masks.data == 0 || elem >= static_cast<uint32_t>(masks.count)) {
        return 0;
    }
    uint64_t mask = 0;
    sn2_safe_read(reinterpret_cast<const void*>(masks.data + static_cast<uintptr_t>(elem) * sizeof(uint64_t)),
        &mask,
        sizeof(mask));
    return mask;
}

struct Sn2DynamicMeshElementLookup {
    bool searched{false};
    bool found{false};
    uint32_t elem{0xffffffffu};
    uint32_t mesh_hits{0};
    uint32_t primitive_hits{0};
    uintptr_t mesh{0};
    uintptr_t primitive{0};
    uint32_t relevance_flags{0};
    uint64_t pass_mask{0};
};

struct Sn2PointerArrayLookup {
    bool searched{false};
    bool found{false};
    uint32_t elem{0xffffffffu};
    uint32_t hits{0};
    uintptr_t value{0};
};

struct Sn2MeshCommandsShapeLookup {
    bool searched{false};
    bool found{false};
    uint32_t elem{0xffffffffu};
    uint32_t hits{0};
    uintptr_t visible_elem{0};
    uintptr_t mesh_draw_command{0};
    uint64_t sort_key{0};
    uint64_t run_array{0};
    uint32_t num_runs{0};
    uint32_t state_bucket_id{0};
    uint32_t primitive_id_buffer_offset{0};
    uint16_t culling_payload_flags{0};
    uint64_t visible_flags{0};
    uint32_t first_index{0};
    uint32_t num_primitives{0};
    uint32_t num_instances{0};
    uint8_t primitive_type{0};
    uint32_t visible_target_value{0};
    uint32_t visible_target_off{0xffffffffu};
    uint32_t cmd_target_value{0};
    uint32_t cmd_target_off{0xffffffffu};
    uint32_t cmd_read_ok{0};
    uint32_t cmd_ptr_nonzero{0};
    uint32_t max_num_primitives{0};
    uint32_t max_elem{0xffffffffu};
    uintptr_t max_cmd{0};
};

static void sn2_log_viewcommands_copy_debug(
    const char* path,
    uint64_t seq,
    const char* reason,
    int32_t pass,
    uintptr_t cached_target_static,
    uintptr_t target_static,
    const Sn2TArrayHeader& primary_req,
    const Sn2PointerArrayLookup& primary_hit,
    const Sn2TArrayHeader& secondary_req,
    const Sn2PointerArrayLookup& secondary_hit)
{
    if (!sn2_viewcommands_copy_debug_enabled()) {
        return;
    }
    static std::atomic<uint64_t> rows{0};
    const auto n = rows.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n > static_cast<uint64_t>(sn2_viewcommands_copy_debug_max())) {
        return;
    }

    SPDLOG_WARN(
        "[SN2-ViewCommandsCopyDebug] #{} path={} trace#{} reason={} pass={}({}) "
        "cached_target_static=0x{:x} target_static=0x{:x} "
        "primary_req[data=0x{:x} count={} cap={} valid={}] primary_hit[found={} elem={} hits={} value=0x{:x}] "
        "secondary_req[data=0x{:x} count={} cap={} valid={}] secondary_hit[found={} elem={} hits={} value=0x{:x}]",
        n,
        path ? path : "?",
        seq,
        reason ? reason : "?",
        pass,
        sn2_mesh_pass_name(pass),
        cached_target_static,
        target_static,
        primary_req.data,
        primary_req.count,
        primary_req.capacity,
        primary_req.valid ? 1 : 0,
        primary_hit.found ? 1 : 0,
        primary_hit.elem,
        primary_hit.hits,
        primary_hit.value,
        secondary_req.data,
        secondary_req.count,
        secondary_req.capacity,
        secondary_req.valid ? 1 : 0,
        secondary_hit.found ? 1 : 0,
        secondary_hit.elem,
        secondary_hit.hits,
        secondary_hit.value);
}

static Sn2PointerArrayLookup sn2_find_pointer_array_element_limited(
    const Sn2TArrayHeader& array,
    uintptr_t target,
    int max_elems)
{
    Sn2PointerArrayLookup out{};
    if (!array.valid || array.data == 0 || array.count <= 0 || target == 0) {
        return out;
    }
    if (max_elems <= 0) {
        return out;
    }

    out.searched = true;
    const uint32_t elem_count =
        static_cast<uint32_t>(array.count < max_elems ? array.count : max_elems);
    const auto* base = reinterpret_cast<const uint8_t*>(array.data);
    for (uint32_t elem = 0; elem < elem_count; ++elem) {
        uintptr_t value = 0;
        if (!sn2_safe_read(base + static_cast<uintptr_t>(elem) * sizeof(uintptr_t),
                &value,
                sizeof(value))) {
            continue;
        }
        if (value == target) {
            ++out.hits;
            if (!out.found) {
                out.found = true;
                out.elem = elem;
                out.value = value;
            }
        }
    }
    return out;
}

static Sn2PointerArrayLookup sn2_find_pointer_array_element(
    const Sn2TArrayHeader& array,
    uintptr_t target)
{
    return sn2_find_pointer_array_element_limited(array, target, sn2_gendyn_trace_max_elems());
}

// FViewCommands::MeshCommands[] is TArray<FVisibleMeshDrawCommand,
// SceneRenderingAllocator>. Editor PDB pins FVisibleMeshDrawCommand at 0x48
// bytes with FMeshDrawCommand* at +0, and FMeshDrawCommand::NumPrimitives at
// +0xF4. This scanner identifies the actual command that produces the
// 76608-index/25536-triangle draw instead of looking only at build-request
// pointer arrays.
static Sn2MeshCommandsShapeLookup sn2_find_mesh_commands_target_shape(
    const Sn2TArrayHeader& mesh_commands)
{
    Sn2MeshCommandsShapeLookup out{};
    if (!mesh_commands.valid || mesh_commands.data == 0 || mesh_commands.count <= 0) {
        return out;
    }

    const int max_elems = sn2_gendyn_trace_max_elems();
    if (max_elems <= 0) {
        return out;
    }

    static constexpr uintptr_t k_visible_stride = 0x48;
    static constexpr uintptr_t k_visible_cmd_off = 0x00;
    static constexpr uintptr_t k_visible_sort_key_off = 0x08;
    static constexpr uintptr_t k_visible_primitive_id_buffer_offset_off = 0x1c;
    static constexpr uintptr_t k_visible_state_bucket_id_off = 0x20;
    static constexpr uintptr_t k_visible_run_array_off = 0x28;
    static constexpr uintptr_t k_visible_num_runs_off = 0x30;
    static constexpr uintptr_t k_visible_culling_payload_flags_off = 0x38;
    static constexpr uintptr_t k_visible_flags_off = 0x40;
    static constexpr uintptr_t k_cmd_first_index_off = 0xF0;
    static constexpr uintptr_t k_cmd_num_primitives_off = 0xF4;
    static constexpr uintptr_t k_cmd_num_instances_off = 0xF8;
    static constexpr uintptr_t k_cmd_primitive_type_off = 0x114;

    out.searched = true;
    const uint32_t elem_count =
        static_cast<uint32_t>(mesh_commands.count < max_elems ? mesh_commands.count : max_elems);
    const auto* base = reinterpret_cast<const uint8_t*>(mesh_commands.data);

    for (uint32_t elem = 0; elem < elem_count; ++elem) {
        const uintptr_t visible_elem =
            mesh_commands.data + static_cast<uintptr_t>(elem) * k_visible_stride;
        const auto* visible = base + static_cast<uintptr_t>(elem) * k_visible_stride;

        uint32_t visible_target_value = 0;
        uint32_t visible_target_off = 0xffffffffu;
        for (uint32_t off = 0; off < k_visible_stride; off += sizeof(uint32_t)) {
            uint32_t v = 0;
            if (sn2_safe_read(visible + off, &v, sizeof(v)) && sn2_tryadd_target_value(v)) {
                visible_target_value = v;
                visible_target_off = off;
                break;
            }
        }

        uintptr_t cmd = 0;
        sn2_safe_read(visible + k_visible_cmd_off, &cmd, sizeof(cmd));
        if (cmd >= 0x10000) {
            ++out.cmd_ptr_nonzero;
        }

        uintptr_t cmd_to_read = cmd;
        uint32_t first_index = 0;
        uint32_t num_primitives = 0;
        uint32_t num_instances = 0;
        bool cmd_read_ok =
            cmd_to_read >= 0x10000 &&
            sn2_safe_read(reinterpret_cast<const void*>(cmd_to_read + k_cmd_num_primitives_off),
                &num_primitives,
                sizeof(num_primitives));
        if (!cmd_read_ok && cmd >= 0x10000) {
            cmd_to_read = cmd & ~uintptr_t{0xf};
            cmd_read_ok =
                cmd_to_read >= 0x10000 &&
                sn2_safe_read(reinterpret_cast<const void*>(cmd_to_read + k_cmd_num_primitives_off),
                    &num_primitives,
                    sizeof(num_primitives));
        }
        if (cmd_read_ok) {
            ++out.cmd_read_ok;
            sn2_safe_read(reinterpret_cast<const void*>(cmd_to_read + k_cmd_first_index_off), &first_index, sizeof(first_index));
            sn2_safe_read(reinterpret_cast<const void*>(cmd_to_read + k_cmd_num_instances_off), &num_instances, sizeof(num_instances));
            if (num_primitives > out.max_num_primitives) {
                out.max_num_primitives = num_primitives;
                out.max_elem = elem;
                out.max_cmd = cmd_to_read;
            }
        }

        uint32_t cmd_target_value = 0;
        uint32_t cmd_target_off = 0xffffffffu;
        if (cmd_to_read >= 0x10000) {
            for (uint32_t off = 0; off < 0x180; off += sizeof(uint32_t)) {
                uint32_t v = 0;
                if (sn2_safe_read(reinterpret_cast<const void*>(cmd_to_read + off), &v, sizeof(v)) &&
                    sn2_tryadd_target_value(v)) {
                    cmd_target_value = v;
                    cmd_target_off = off;
                    break;
                }
            }
        }

        const uint64_t index_count = static_cast<uint64_t>(num_primitives) * 3ull;
        if (visible_target_value == 0 &&
            cmd_target_value == 0 &&
            (!cmd_read_ok ||
             (!sn2_tryadd_target_value(num_primitives) &&
              (index_count > 0xffffffffull || !sn2_tryadd_target_value(static_cast<uint32_t>(index_count)))))) {
            continue;
        }

        ++out.hits;
        if (!out.found) {
            out.found = true;
            out.elem = elem;
            out.visible_elem = visible_elem;
            out.mesh_draw_command = cmd_to_read;
            out.first_index = first_index;
            out.num_primitives = num_primitives;
            out.num_instances = num_instances;
            out.visible_target_value = visible_target_value;
            out.visible_target_off = visible_target_off;
            out.cmd_target_value = cmd_target_value;
            out.cmd_target_off = cmd_target_off;
            sn2_safe_read(visible + k_visible_sort_key_off, &out.sort_key, sizeof(out.sort_key));
            sn2_safe_read(visible + k_visible_primitive_id_buffer_offset_off,
                &out.primitive_id_buffer_offset,
                sizeof(out.primitive_id_buffer_offset));
            sn2_safe_read(visible + k_visible_state_bucket_id_off, &out.state_bucket_id, sizeof(out.state_bucket_id));
            sn2_safe_read(visible + k_visible_run_array_off, &out.run_array, sizeof(out.run_array));
            sn2_safe_read(visible + k_visible_num_runs_off, &out.num_runs, sizeof(out.num_runs));
            sn2_safe_read(visible + k_visible_culling_payload_flags_off,
                &out.culling_payload_flags,
                sizeof(out.culling_payload_flags));
            sn2_safe_read(visible + k_visible_flags_off, &out.visible_flags, sizeof(out.visible_flags));
            sn2_safe_read(reinterpret_cast<const void*>(cmd + k_cmd_primitive_type_off),
                &out.primitive_type,
                sizeof(out.primitive_type));
        }
    }

    return out;
}

struct Sn2MeshCommandBruteHit {
    bool found{false};
    uint32_t hits{0};
    uintptr_t visible_elem{0};
    uintptr_t candidate_ptr{0};
    uintptr_t candidate_aligned{0};
    uint32_t stride{0};
    uint32_t elem{0xffffffffu};
    uint32_t ptr_off{0xffffffffu};
    uint32_t target_off{0xffffffffu};
    uint32_t target_value{0};
    uint32_t visible_target_off{0xffffffffu};
    uint32_t visible_target_value{0};
};

static bool sn2_meshcmd_bruteforce_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_MESHCMD_BRUTE_SCAN");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

static int sn2_meshcmd_bruteforce_max_logs() {
    static const int rows = []() {
        const int v = sn2_env_int("UEVR_SN2_MESHCMD_BRUTE_SCAN_MAX", 24);
        return v < 0 ? 0 : v;
    }();
    return rows;
}

static int sn2_meshcmd_bruteforce_max_elems() {
    static const int rows = []() {
        const int v = sn2_env_int("UEVR_SN2_MESHCMD_BRUTE_SCAN_MAX_ELEMS", 128);
        return v < 0 ? 0 : v;
    }();
    return rows;
}

static Sn2MeshCommandBruteHit sn2_bruteforce_meshcommands_target_shape(
    const Sn2TArrayHeader& mesh_commands)
{
    Sn2MeshCommandBruteHit out{};
    if (!mesh_commands.valid || mesh_commands.data < 0x10000 || mesh_commands.count <= 0) {
        return out;
    }

    const int max_elems_raw = sn2_meshcmd_bruteforce_max_elems();
    if (max_elems_raw <= 0) {
        return out;
    }

    static constexpr uint32_t k_strides[] = {
        0x28, 0x30, 0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x80
    };

    const uint32_t max_elems = static_cast<uint32_t>(
        mesh_commands.count < max_elems_raw ? mesh_commands.count : max_elems_raw);

    auto record_hit = [&](uint32_t stride,
                          uint32_t elem,
                          uintptr_t visible_elem,
                          uint32_t ptr_off,
                          uintptr_t candidate,
                          uintptr_t aligned,
                          uint32_t target_off,
                          uint32_t target_value,
                          uint32_t visible_target_off,
                          uint32_t visible_target_value) {
        ++out.hits;
        if (!out.found) {
            out.found = true;
            out.stride = stride;
            out.elem = elem;
            out.visible_elem = visible_elem;
            out.ptr_off = ptr_off;
            out.candidate_ptr = candidate;
            out.candidate_aligned = aligned;
            out.target_off = target_off;
            out.target_value = target_value;
            out.visible_target_off = visible_target_off;
            out.visible_target_value = visible_target_value;
        }
    };

    for (uint32_t stride : k_strides) {
        for (uint32_t elem = 0; elem < max_elems; ++elem) {
            const uintptr_t visible_elem =
                mesh_commands.data + static_cast<uintptr_t>(elem) * stride;

            uint32_t visible_target_value = 0;
            uint32_t visible_target_off = 0xffffffffu;
            for (uint32_t off = 0; off + sizeof(uint32_t) <= stride && off < 0x90; off += sizeof(uint32_t)) {
                uint32_t v = 0;
                if (sn2_safe_read(reinterpret_cast<const void*>(visible_elem + off), &v, sizeof(v)) &&
                    sn2_tryadd_target_value(v)) {
                    visible_target_value = v;
                    visible_target_off = off;
                    record_hit(stride, elem, visible_elem, 0xffffffffu, 0, 0, 0xffffffffu, 0,
                        visible_target_off, visible_target_value);
                    break;
                }
            }

            for (uint32_t ptr_off = 0; ptr_off + sizeof(uintptr_t) <= stride && ptr_off < 0x90; ptr_off += sizeof(uintptr_t)) {
                uintptr_t candidate = 0;
                if (!sn2_safe_read(reinterpret_cast<const void*>(visible_elem + ptr_off), &candidate, sizeof(candidate)) ||
                    candidate < 0x10000) {
                    continue;
                }

                const uintptr_t candidates[] = {candidate, candidate & ~uintptr_t{0xf}};
                for (uintptr_t aligned : candidates) {
                    if (aligned < 0x10000) {
                        continue;
                    }
                    for (uint32_t target_off = 0; target_off < 0x300; target_off += sizeof(uint32_t)) {
                        uint32_t v = 0;
                        if (!sn2_safe_read(reinterpret_cast<const void*>(aligned + target_off), &v, sizeof(v))) {
                            continue;
                        }
                        if (sn2_tryadd_target_value(v)) {
                            record_hit(stride, elem, visible_elem, ptr_off, candidate, aligned,
                                target_off, v, visible_target_off, visible_target_value);
                            break;
                        }
                    }
                }
            }
        }
    }

    return out;
}

static void sn2_log_meshcommands_bruteforce_if_enabled(
    uint64_t seq,
    int32_t view_index,
    int32_t stereo_pass,
    int32_t pass,
    const Sn2TArrayHeader& mesh_commands)
{
    if (!sn2_meshcmd_bruteforce_enabled() || pass != sn2_underwater_target_mesh_pass()) {
        return;
    }
    // Do not scan MeshCommands until OpenXR is COMPLETELY open and rendering.
    // This brute scan (up to max_elems per call) was the only MESHCMD path
    // lacking any guard, so it ran heavy from frame ~1 and starved the engine
    // tick during init (OpenXR stuck "waiting for valid poses" / uevr_mcp HTTP
    // never opening). Wall-clock delay wasn't enough (fired before VR opened),
    // so gate on the real VR-ready signal.
    if (!sn2_vr_rendering_started()) {
        return;
    }

    static std::atomic<uint64_t> logs{0};
    const auto n = logs.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n > static_cast<uint64_t>(sn2_meshcmd_bruteforce_max_logs())) {
        return;
    }

    const auto hit = sn2_bruteforce_meshcommands_target_shape(mesh_commands);
    const auto target_mesh = g_sn2_target_mesh_ptr.load(std::memory_order_relaxed);
    const int ptr_matches_target_mesh =
        target_mesh != 0 &&
        (hit.candidate_ptr == target_mesh || hit.candidate_aligned == target_mesh) ? 1 : 0;
    SPDLOG_WARN(
        "[SN2-MeshCmdBrute] #{} trace#{} view_slot={} stereo={} pass={}({}) "
        "mesh_cmds[data=0x{:x} count={} cap={} valid={}] found={} hits={} "
        "stride=0x{:x} elem={} visible=0x{:x} ptr_off=0x{:x} ptr=0x{:x} aligned=0x{:x} "
        "target_off=0x{:x} target_value={} visible_target_off=0x{:x} visible_target_value={} "
        "cached_target_mesh=0x{:x} ptr_matches_target_mesh={}",
        n,
        seq,
        view_index,
        stereo_pass,
        pass,
        sn2_mesh_pass_name(pass),
        mesh_commands.data,
        mesh_commands.count,
        mesh_commands.capacity,
        mesh_commands.valid ? 1 : 0,
        hit.found ? 1 : 0,
        hit.hits,
        hit.stride,
        hit.elem,
        hit.visible_elem,
        hit.ptr_off,
        hit.candidate_ptr,
        hit.candidate_aligned,
        hit.target_off,
        hit.target_value,
        hit.visible_target_off,
        hit.visible_target_value,
        target_mesh,
        ptr_matches_target_mesh);
}

static void sn2_maybe_copy_meshcommand_right_13b(uint64_t seq,
                                                 uintptr_t vc_base,
                                                 int32_t vc_count) {
    if (!sn2_meshcmd_copy_right_13b_enabled() || vc_base == 0 || vc_count < 2) {
        return;
    }
    if (!sn2_viewcommands_copy_past_delay()) {
        sn2_log_viewcommands_copy_delay_once();
        return;
    }

    const uintptr_t target_mesh = g_sn2_target_mesh_ptr.load(std::memory_order_relaxed);
    if (target_mesh < 0x10000) {
        return;
    }

    const int32_t pass = sn2_underwater_target_mesh_pass();
    const uintptr_t primary_vc = vc_base;
    const uintptr_t secondary_vc = vc_base + SN2_FVIEWCOMMANDS_SIZE;
    const uintptr_t primary_mesh_addr =
        primary_vc + SN2_FVIEWCOMMANDS_MESHCOMMANDS_OFF + static_cast<uintptr_t>(pass) * 0x10;
    const uintptr_t secondary_mesh_addr =
        secondary_vc + SN2_FVIEWCOMMANDS_MESHCOMMANDS_OFF + static_cast<uintptr_t>(pass) * 0x10;

    const Sn2TArrayHeader primary_mesh =
        sn2_read_tarray_header(reinterpret_cast<const void*>(primary_mesh_addr));
    const Sn2TArrayHeader secondary_mesh =
        sn2_read_tarray_header(reinterpret_cast<const void*>(secondary_mesh_addr));
    const Sn2MeshCommandBruteHit primary_hit = sn2_bruteforce_meshcommands_target_shape(primary_mesh);
    const Sn2MeshCommandBruteHit secondary_hit = sn2_bruteforce_meshcommands_target_shape(secondary_mesh);

    const bool primary_matches_cached_mesh =
        primary_hit.found &&
        (primary_hit.candidate_ptr == target_mesh || primary_hit.candidate_aligned == target_mesh);
    if (!primary_hit.found || !primary_matches_cached_mesh || secondary_hit.found) {
        return;
    }

    const int32_t replace_elem = secondary_mesh.count > 0 ? (secondary_mesh.count - 1) : -1;
    const bool stride_ok = primary_hit.stride >= 0x20 && primary_hit.stride <= 0x100;
    bool copy_ok = false;
    uintptr_t dst = 0;
    if (replace_elem >= 0 &&
        stride_ok &&
        primary_hit.visible_elem >= 0x10000 &&
        secondary_mesh.data >= 0x10000) {
        dst = secondary_mesh.data + static_cast<uintptr_t>(replace_elem) * primary_hit.stride;
        copy_ok = sn2_safe_write(
            reinterpret_cast<void*>(dst),
            reinterpret_cast<const void*>(primary_hit.visible_elem),
            primary_hit.stride);
    }

    static std::atomic<uint64_t> copy_count{0};
    const auto ncopy = copy_count.fetch_add(1, std::memory_order_relaxed) + 1;
    if (ncopy <= 64 || (ncopy % 300) == 0) {
        SPDLOG_WARN(
            "[SN2-MeshCmdCopy] copy#{} trace#{} pass={}({}) target_mesh=0x{:x} "
            "primary[data=0x{:x} count={} elem={} stride=0x{:x} visible=0x{:x} ptr=0x{:x} aligned=0x{:x}] "
            "secondary[data=0x{:x} count={} replace_elem={} dst=0x{:x} preexisting_target={}] "
            "copy_ok={} stride_ok={}",
            ncopy,
            seq,
            pass,
            sn2_mesh_pass_name(pass),
            target_mesh,
            primary_mesh.data,
            primary_mesh.count,
            primary_hit.elem,
            primary_hit.stride,
            primary_hit.visible_elem,
            primary_hit.candidate_ptr,
            primary_hit.candidate_aligned,
            secondary_mesh.data,
            secondary_mesh.count,
            replace_elem,
            dst,
            secondary_hit.found ? 1 : 0,
            copy_ok ? 1 : 0,
            stride_ok ? 1 : 0);
    }
}

static Sn2PointerArrayLookup sn2_find_target_shape_pointer_array_element(
    const Sn2TArrayHeader& array)
{
    Sn2PointerArrayLookup out{};
    if (!array.valid || array.data == 0 || array.count <= 0) {
        return out;
    }

    const int max_elems = sn2_gendyn_trace_max_elems();
    if (max_elems <= 0) {
        return out;
    }

    out.searched = true;
    const uint32_t elem_count =
        static_cast<uint32_t>(array.count < max_elems ? array.count : max_elems);
    const auto* base = reinterpret_cast<const uint8_t*>(array.data);
    for (uint32_t elem = 0; elem < elem_count; ++elem) {
        uintptr_t value = 0;
        if (!sn2_safe_read(base + static_cast<uintptr_t>(elem) * sizeof(uintptr_t),
                &value,
                sizeof(value)) ||
            value < 0x10000) {
            continue;
        }

        const auto probe = sn2_probe_mesh_batch_shape(reinterpret_cast<const void*>(value));
        if (probe.direct_hit_value == 0 && probe.element_hit_value == 0) {
            continue;
        }

        ++out.hits;
        if (!out.found) {
            out.found = true;
            out.elem = elem;
            out.value = value;
        }
    }
    return out;
}

struct Sn2ScenePrimitiveLookup {
    bool searched{false};
    bool found{false};
    uintptr_t scene_field_off{UINTPTR_MAX};
    uintptr_t scene{0};
    uintptr_t primitives_off{UINTPTR_MAX};
    uintptr_t primitives_data{0};
    int32_t primitives_count{0};
    int32_t primitive_index{-1};
    uint32_t primitive_hits{0};
    uintptr_t proxies_off{UINTPTR_MAX};
    uintptr_t proxies_data{0};
    int32_t proxies_count{0};
    bool proxy_match{false};
};

static bool sn2_is_readable_process_range(uintptr_t address, size_t size) {
    if (address == 0 || size == 0 || address + size < address) {
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi)) == 0) {
        return false;
    }

    const auto base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    if (address < base || address + size > base + mbi.RegionSize) {
        return false;
    }
    if ((mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }

    const auto protect = mbi.Protect & 0xff;
    return protect == PAGE_READONLY ||
           protect == PAGE_READWRITE ||
           protect == PAGE_WRITECOPY ||
           protect == PAGE_EXECUTE_READ ||
           protect == PAGE_EXECUTE_READWRITE ||
           protect == PAGE_EXECUTE_WRITECOPY;
}

static bool sn2_tarray_header_plausible_for_scene_pointers(const Sn2TArrayHeader& hdr) {
    return hdr.valid &&
           hdr.data >= 0x10000 &&
           hdr.count > 0 &&
           hdr.count <= 200000 &&
           hdr.capacity >= hdr.count &&
           hdr.capacity <= 400000 &&
           sn2_is_readable_process_range(hdr.data, sizeof(uintptr_t));
}

static bool sn2_tarray_pointer_at_index_equals(const Sn2TArrayHeader& hdr,
                                               int32_t index,
                                               uintptr_t target) {
    if (!sn2_tarray_header_plausible_for_scene_pointers(hdr) ||
        index < 0 ||
        hdr.count <= index ||
        target == 0) {
        return false;
    }

    uintptr_t value = 0;
    return sn2_safe_read(
               reinterpret_cast<const void*>(hdr.data + static_cast<uintptr_t>(index) * sizeof(uintptr_t)),
               &value,
               sizeof(value)) &&
           value == target;
}

static Sn2ScenePrimitiveLookup sn2_scan_scene_for_primitive_array(uintptr_t scene,
                                                                 uintptr_t primitive_scene_info,
                                                                 uintptr_t primitive_proxy,
                                                                 uintptr_t scene_field_off) {
    Sn2ScenePrimitiveLookup out{};
    out.searched = true;
    out.scene = scene;
    out.scene_field_off = scene_field_off;

    const int scan_bytes = sn2_target_identity_scene_scan_bytes();
    const int max_elems = sn2_target_identity_scene_scan_max_elems();
    if (scene < 0x10000 || primitive_scene_info < 0x10000 || scan_bytes <= 0 || max_elems <= 0 ||
        !sn2_is_readable_process_range(scene, sizeof(uintptr_t))) {
        return out;
    }

    for (uintptr_t off = 0; off < static_cast<uintptr_t>(scan_bytes); off += sizeof(uintptr_t)) {
        const auto primitives = sn2_read_tarray_header(reinterpret_cast<const void*>(scene + off));
        if (!sn2_tarray_header_plausible_for_scene_pointers(primitives)) {
            continue;
        }

        const auto hit = sn2_find_pointer_array_element_limited(primitives, primitive_scene_info, max_elems);
        if (!hit.found) {
            continue;
        }

        out.found = true;
        out.primitives_off = off;
        out.primitives_data = primitives.data;
        out.primitives_count = primitives.count;
        out.primitive_index = static_cast<int32_t>(hit.elem);
        out.primitive_hits = hit.hits;

        // FScene::PrimitiveSceneProxies is near FScene::Primitives in source,
        // but the shipping binary has extra layout. Scan the immediate
        // neighborhood and require the same packed index to point at the proxy.
        for (uintptr_t proxy_off = off + 0x10; proxy_off < off + 0x120; proxy_off += sizeof(uintptr_t)) {
            const auto proxies = sn2_read_tarray_header(reinterpret_cast<const void*>(scene + proxy_off));
            if (!sn2_tarray_pointer_at_index_equals(proxies, out.primitive_index, primitive_proxy)) {
                continue;
            }
            out.proxies_off = proxy_off;
            out.proxies_data = proxies.data;
            out.proxies_count = proxies.count;
            out.proxy_match = true;
            break;
        }

        return out;
    }

    return out;
}

static Sn2ScenePrimitiveLookup sn2_find_scene_primitive_index(uintptr_t primitive_scene_info,
                                                             uintptr_t primitive_proxy,
                                                             uintptr_t mesh_ptr,
                                                             uintptr_t processor) {
    Sn2ScenePrimitiveLookup best{};
    if (primitive_scene_info < 0x10000) {
        return best;
    }

    auto consider_scene = [&](uintptr_t scene, uintptr_t field_off) {
        if (scene < 0x10000 ||
            scene == primitive_scene_info ||
            scene == primitive_proxy ||
            scene == mesh_ptr ||
            (best.found && best.proxy_match)) {
            return;
        }
        const auto lookup = sn2_scan_scene_for_primitive_array(scene, primitive_scene_info, primitive_proxy, field_off);
        if (!lookup.found) {
            if (!best.searched) {
                best.searched = true;
            }
            return;
        }
        if (!best.found || (!best.proxy_match && lookup.proxy_match)) {
            best = lookup;
        }
    };

    // First scan the PrimitiveSceneInfo-like object itself for its FScene*.
    for (uintptr_t off = 0; off < 0x900 && (!best.found || !best.proxy_match); off += sizeof(uintptr_t)) {
        uintptr_t scene = 0;
        if (sn2_safe_read(reinterpret_cast<const void*>(primitive_scene_info + off), &scene, sizeof(scene))) {
            consider_scene(scene, off);
        }
    }

    // FMeshPassProcessor also owns a Scene pointer. This cross-checks the
    // PrimitiveSceneInfo candidate without depending on its field offsets.
    for (uintptr_t off = 0; processor != 0 && off < 0x80 && (!best.found || !best.proxy_match); off += sizeof(uintptr_t)) {
        uintptr_t scene = 0;
        if (sn2_safe_read(reinterpret_cast<const void*>(processor + off), &scene, sizeof(scene))) {
            consider_scene(scene, 0x10000000u | off);
        }
    }

    return best;
}

struct Sn2LiveSceneIndex {
    bool live{false};
    bool proxy_match{false};
    int32_t index{-1};
    int32_t packed_index188{-1};
    int32_t scene_count{0};
    uintptr_t scene{0};
    uintptr_t primitives_off{UINTPTR_MAX};
    uintptr_t proxies_off{UINTPTR_MAX};
};

static Sn2LiveSceneIndex sn2_resolve_scene_primitive_index_live() {
    Sn2LiveSceneIndex out{};
    const uintptr_t primitive_scene_info =
        g_sn2_target_primitive_scene_info_ptr.load(std::memory_order_relaxed);
    const uintptr_t primitive_proxy = g_sn2_target_primitive_ptr.load(std::memory_order_relaxed);
    const uintptr_t task_scene = g_sn2_visibility_task_scene_ptr.load(std::memory_order_relaxed);
    const uintptr_t cached_scene = g_sn2_target_scene_ptr.load(std::memory_order_relaxed);
    const uintptr_t cached_primitives_off = g_sn2_target_scene_primitives_off.load(std::memory_order_relaxed);
    const uintptr_t cached_proxies_off = g_sn2_target_scene_proxies_off.load(std::memory_order_relaxed);

    out.scene = task_scene >= 0x10000 ? task_scene : cached_scene;
    out.primitives_off = task_scene >= 0x10000 ? SN2_FSCENE_PRIMITIVES_DIA_OFF : cached_primitives_off;
    out.proxies_off = task_scene >= 0x10000 ? SN2_FSCENE_PRIMITIVE_PROXIES_DIA_OFF : cached_proxies_off;
    if (primitive_scene_info < 0x10000 ||
        primitive_proxy < 0x10000 ||
        !sn2_is_readable_process_range(primitive_scene_info + 0x8, sizeof(uintptr_t))) {
        return out;
    }

    uintptr_t backref = 0;
    if (!sn2_safe_read(reinterpret_cast<const void*>(primitive_scene_info + 0x8), &backref, sizeof(backref)) ||
        backref != primitive_proxy ||
        sn2_read_vtable_rva(reinterpret_cast<const void*>(primitive_proxy)) == 0) {
        return out;
    }
    {
        int32_t packed = -1;
        if (sn2_safe_read(
                reinterpret_cast<const void*>(primitive_scene_info + SN2_FPRIMITIVE_SCENE_INFO_PACKED_INDEX_DIA_OFF),
                &packed,
                sizeof(packed)) &&
            packed >= 0 &&
            packed < 2000000) {
            out.packed_index188 = packed;
        }
    }

    auto try_resolve = [&](uintptr_t scene,
                           uintptr_t primitives_off,
                           uintptr_t proxies_off,
                           bool tolerate_proxy_offset_mismatch) -> bool {
        if (scene < 0x10000 ||
            primitives_off == UINTPTR_MAX ||
            !sn2_is_readable_process_range(scene + primitives_off, sizeof(Sn2TArrayHeader))) {
            return false;
        }

        const Sn2TArrayHeader primitives =
            sn2_read_tarray_header(reinterpret_cast<const void*>(scene + primitives_off));
        if (!sn2_tarray_header_plausible_for_scene_pointers(primitives)) {
            return false;
        }

        const auto hit = sn2_find_pointer_array_element_limited(
            primitives,
            primitive_scene_info,
            sn2_target_identity_scene_scan_max_elems());
        if (!hit.found || hit.elem > static_cast<uint32_t>(INT32_MAX)) {
            return false;
        }

        out.scene = scene;
        out.primitives_off = primitives_off;
        out.proxies_off = UINTPTR_MAX;
        out.scene_count = primitives.count;
        out.index = static_cast<int32_t>(hit.elem);
        out.live = true;
        out.proxy_match = true;

        if (proxies_off != UINTPTR_MAX &&
            sn2_is_readable_process_range(scene + proxies_off, sizeof(Sn2TArrayHeader))) {
            const Sn2TArrayHeader proxies =
                sn2_read_tarray_header(reinterpret_cast<const void*>(scene + proxies_off));
            const bool proxy_match =
                sn2_tarray_pointer_at_index_equals(proxies, out.index, primitive_proxy);
            if (proxy_match) {
                out.proxies_off = proxies_off;
                out.proxy_match = true;
            } else if (!tolerate_proxy_offset_mismatch) {
                out.proxies_off = proxies_off;
                out.proxy_match = false;
                out.live = false;
                return false;
            }
        }

        g_sn2_target_scene_ptr.store(scene, std::memory_order_relaxed);
        g_sn2_target_scene_primitives_off.store(primitives_off, std::memory_order_relaxed);
        g_sn2_target_scene_proxies_off.store(out.proxies_off, std::memory_order_relaxed);
        g_sn2_target_scene_primitive_index.store(out.index, std::memory_order_relaxed);
        return true;
    };

    // Preferred path: the finish-gather hook owns FVisibilityTaskData*, whose
    // DIA-confirmed +0x18 member is FScene*. This avoids the older
    // primary-view/family scene guessing and goes straight to FScene.Primitives.
    if (try_resolve(
            task_scene,
            SN2_FSCENE_PRIMITIVES_DIA_OFF,
            SN2_FSCENE_PRIMITIVE_PROXIES_DIA_OFF,
            true)) {
        return out;
    }

    if (try_resolve(cached_scene, cached_primitives_off, cached_proxies_off, false)) {
        return out;
    }

    if (task_scene >= 0x10000) {
        const auto scanned = sn2_scan_scene_for_primitive_array(
            task_scene,
            primitive_scene_info,
            primitive_proxy,
            SN2_VIS_TASK_SCENE_OFF);
        if (scanned.found && scanned.primitive_index >= 0) {
            out.scene = scanned.scene;
            out.primitives_off = scanned.primitives_off;
            out.proxies_off = scanned.proxy_match ? scanned.proxies_off : UINTPTR_MAX;
            out.scene_count = scanned.primitives_count;
            out.index = scanned.primitive_index;
            out.live = true;
            out.proxy_match = true;
            g_sn2_target_scene_ptr.store(out.scene, std::memory_order_relaxed);
            g_sn2_target_scene_primitives_off.store(out.primitives_off, std::memory_order_relaxed);
            g_sn2_target_scene_proxies_off.store(out.proxies_off, std::memory_order_relaxed);
            g_sn2_target_scene_primitive_index.store(out.index, std::memory_order_relaxed);
        }
    }
    return out;
}

struct Sn2StablePrimitiveIndex {
    bool stable{false};
    bool live{false};
    int32_t index{-1};
    int32_t packed_index188{-1};
    int32_t scene_count{0};
    int32_t streak{0};
};

static Sn2StablePrimitiveIndex sn2_resolve_stable_target_primitive_index(uint64_t seq) {
    Sn2StablePrimitiveIndex out{};
    const int override_index = sn2_target_primitive_index_override();
    if (override_index >= 32 && override_index < 2000000) {
        out.stable = true;
        out.live = true;
        out.index = override_index;
        out.streak = sn2_stability_streak_required();
        return out;
    }

    static std::mutex mu;
    static uintptr_t last_psi = 0;
    static int32_t last_index = -1;
    static int32_t last_count = 0;
    static int32_t streak = 0;
    static uint64_t last_instability_seq = 0;
    static bool announced_stable = false;

    std::scoped_lock lock{mu};
    const uintptr_t psi = g_sn2_target_primitive_scene_info_ptr.load(std::memory_order_relaxed);
    const Sn2LiveSceneIndex live = sn2_resolve_scene_primitive_index_live();
    out.live = live.live;
    out.index = live.index;
    out.packed_index188 = live.packed_index188;
    out.scene_count = live.scene_count;

    const int required = sn2_stability_streak_required();
    const int quarantine = sn2_stability_quarantine_frames();
    if (!live.live) {
        if (streak != 0 || last_psi != psi) {
            SPDLOG_WARN(
                "[SN2-StabilityGate] LIVENESS FAILED target_psi=0x{:x} live={} proxy_match={} index={} packed188={} count={} scene=0x{:x} prim_off=0x{:x} prox_off=0x{:x}; reset streak from {}",
                psi,
                live.live ? 1 : 0,
                live.proxy_match ? 1 : 0,
                live.index,
                live.packed_index188,
                live.scene_count,
                live.scene,
                live.primitives_off,
                live.proxies_off,
                streak);
        }
        last_psi = psi;
        last_index = -1;
        last_count = 0;
        streak = 0;
        announced_stable = false;
        last_instability_seq = seq;
        return out;
    }

    const bool require_scene_count = sn2_stability_require_scene_count();
    const bool count_changed = live.scene_count != last_count;
    const bool changed =
        psi != last_psi ||
        live.index != last_index ||
        (require_scene_count && count_changed);
    if (changed) {
        if (last_psi != 0 || last_index >= 0) {
            SPDLOG_WARN(
                "[SN2-StabilityGate] INSTABILITY target_psi=0x{:x} index {} -> {} packed188={} count {} -> {}; reset streak from {} to 1 trace#{}",
                psi,
                last_index,
                live.index,
                live.packed_index188,
                last_count,
                live.scene_count,
                streak,
                seq);
        } else {
            SPDLOG_WARN(
                "[SN2-StabilityGate] new target_psi=0x{:x} index={} packed188={} scene_count={} streak=1/{} trace#{}",
                psi,
                live.index,
                live.packed_index188,
                live.scene_count,
                required,
                seq);
        }
        last_psi = psi;
        last_index = live.index;
        last_count = live.scene_count;
        streak = 1;
        announced_stable = false;
        last_instability_seq = seq;
    } else if (count_changed) {
        last_count = live.scene_count;
        if (streak < INT32_MAX) {
            ++streak;
        }
    } else if (streak < INT32_MAX) {
        ++streak;
    }

    out.streak = streak;
    const bool streak_ok = required <= 0 || streak >= required;
    const bool quarantine_ok = quarantine <= 0 || (seq >= last_instability_seq && seq - last_instability_seq >= static_cast<uint64_t>(quarantine));
    out.stable = streak_ok && quarantine_ok;
    if (!out.stable) {
        if (streak == 1 || streak == required / 2 || (required > 0 && streak == required - 1) || (streak % 60) == 0) {
            SPDLOG_WARN(
                "[SN2-StabilityGate] waiting target_psi=0x{:x} index={} packed188={} scene_count={} streak={}/{} quarantine={} trace#{}",
                psi,
                live.index,
                live.packed_index188,
                live.scene_count,
                streak,
                required,
                quarantine,
                seq);
        }
        return out;
    }

    if (!announced_stable) {
        SPDLOG_WARN(
            "[SN2-StabilityGate] STABLE - target_psi=0x{:x} index={} packed188={} match={} scene_count={} streak={} trace#{} - copy ENABLED",
            psi,
            live.index,
            live.packed_index188,
            live.packed_index188 == live.index ? 1 : 0,
            live.scene_count,
            streak,
            seq);
        announced_stable = true;
    }
    g_sn2_target_primitive_index_hint.store(live.index, std::memory_order_relaxed);
    g_sn2_target_scene_primitive_index.store(live.index, std::memory_order_relaxed);
    return out;
}

static Sn2DynamicMeshElementLookup sn2_find_dynamic_mesh_element(
    const Sn2TArrayHeader& array,
    const Sn2TArrayHeader& masks,
    uintptr_t mesh_ptr,
    uintptr_t primitive_ptr)
{
    Sn2DynamicMeshElementLookup out{};
    if (!array.valid || array.data == 0 || array.count <= 0) {
        return out;
    }

    const int max_elems = sn2_gendyn_trace_max_elems();
    const int scan_bytes_per_elem = sn2_gendyn_trace_scan_bytes_per_elem();
    if (max_elems <= 0 || scan_bytes_per_elem <= 0) {
        return out;
    }

    out.searched = true;
    const uint32_t elem_count =
        static_cast<uint32_t>(array.count < max_elems ? array.count : max_elems);
    const uint32_t elem_stride = static_cast<uint32_t>(scan_bytes_per_elem);
    const auto* base = reinterpret_cast<const uint8_t*>(array.data);

    for (uint32_t elem = 0; elem < elem_count; ++elem) {
        const uint32_t elem_base = elem * elem_stride;
        uintptr_t mesh = 0;
        uintptr_t primitive = 0;
        uint32_t relevance_flags = 0;
        if (!sn2_safe_read(base + elem_base, &mesh, sizeof(mesh)) || mesh == 0) {
            continue;
        }
        sn2_safe_read(base + elem_base + 8, &primitive, sizeof(primitive));
        sn2_safe_read(base + elem_base + 16, &relevance_flags, sizeof(relevance_flags));

        const bool mesh_hit = mesh_ptr != 0 && mesh == mesh_ptr;
        const bool primitive_hit = primitive_ptr != 0 && primitive == primitive_ptr;
        if (mesh_hit) {
            ++out.mesh_hits;
        }
        if (primitive_hit) {
            ++out.primitive_hits;
        }
        if ((mesh_hit || primitive_hit) && !out.found) {
            out.found = true;
            out.elem = elem;
            out.mesh = mesh;
            out.primitive = primitive;
            out.relevance_flags = relevance_flags;
            out.pass_mask = sn2_read_mesh_pass_mask(masks, elem);
        }
    }
    return out;
}

static Sn2DynamicMeshArrayProbe sn2_probe_dynamic_mesh_array(
    const Sn2TArrayHeader& array,
    const Sn2TArrayHeader& masks)
{
    Sn2DynamicMeshArrayProbe out{};
    if (!array.valid || array.data == 0 || array.count <= 0) {
        return out;
    }

    const int max_elems = sn2_gendyn_trace_max_elems();
    const int scan_bytes_per_elem = sn2_gendyn_trace_scan_bytes_per_elem();
    if (max_elems <= 0 || scan_bytes_per_elem <= 0) {
        return out;
    }

    const uint32_t elem_count =
        static_cast<uint32_t>(array.count < max_elems ? array.count : max_elems);
    const uint32_t elem_stride = static_cast<uint32_t>(scan_bytes_per_elem);

    // UE 5.x FMeshBatchAndRelevance is { FMeshBatch* Mesh; FPrimitiveSceneProxy*
    // PrimitiveSceneProxy; uint32 relevance_bits; } -> 0x18 bytes on Win64.
    // Keep the stride configurable because this is shipping-code diagnostics,
    // but do not scan beyond each element; a broad scan can accidentally pick up
    // neighboring view arrays and falsely "prove" the right eye has the mesh.
    const auto* base = reinterpret_cast<const uint8_t*>(array.data);
    const uintptr_t known_mesh = g_sn2_target_mesh_ptr.load(std::memory_order_relaxed);
    const uintptr_t known_primitive = g_sn2_target_primitive_ptr.load(std::memory_order_relaxed);
    for (uint32_t elem = 0; elem < elem_count; ++elem) {
        const uint32_t elem_base = elem * elem_stride;
        uintptr_t mesh = 0;
        uintptr_t primitive = 0;
        uint32_t relevance_flags = 0;
        if (!sn2_safe_read(base + elem_base, &mesh, sizeof(mesh)) || mesh == 0) {
            continue;
        }
        sn2_safe_read(base + elem_base + 8, &primitive, sizeof(primitive));
        sn2_safe_read(base + elem_base + 16, &relevance_flags, sizeof(relevance_flags));
        const uint64_t pass_mask = sn2_read_mesh_pass_mask(masks, elem);

        const bool known_mesh_hit = known_mesh != 0 && mesh == known_mesh;
        const bool known_primitive_hit = known_primitive != 0 && primitive == known_primitive;
        if (known_mesh_hit) {
            ++out.known_mesh_hits;
        }
        if (known_primitive_hit) {
            ++out.known_primitive_hits;
        }
        if ((known_mesh_hit || known_primitive_hit) && out.first_known_elem == 0xffffffffu) {
            out.first_known_elem = elem;
            out.first_known_mesh = mesh;
            out.first_known_primitive = primitive;
            out.first_known_relevance_flags = relevance_flags;
            out.first_known_mask = pass_mask;
        }

        const auto mesh_probe = sn2_probe_mesh_batch_shape(reinterpret_cast<const void*>(mesh));
        const uint32_t value = mesh_probe.direct_hit_value != 0 ? mesh_probe.direct_hit_value : mesh_probe.element_hit_value;
        const uint32_t offset = mesh_probe.direct_hit_value != 0 ? mesh_probe.direct_hit_offset : mesh_probe.element_hit_offset;
        if (value != 0) {
            ++out.target_values;
            ++out.elems_with_target;
            if (out.first_value == 0) {
                out.first_elem = elem;
                out.first_offset = offset;
                out.first_value = value;
                out.first_mesh_candidate = mesh;
                out.first_primitive = primitive;
                out.first_relevance_flags = relevance_flags;
                out.first_mask = pass_mask;
            }
        }
    }
    return out;
}

static Sn2TryAddMeshProbe sn2_probe_mesh_batch_shape(const void* mesh_ptr) {
    Sn2TryAddMeshProbe out{};
    if (mesh_ptr == nullptr) {
        return out;
    }

    const auto* base = reinterpret_cast<const uint8_t*>(mesh_ptr);
    for (uint32_t off = 0; off < 0x100; off += 4) {
        uint32_t v = 0;
        if (sn2_safe_read(base + off, &v, sizeof(v)) && sn2_tryadd_target_value(v)) {
            out.direct_hit_value = v;
            out.direct_hit_offset = off;
            break;
        }
    }

    // FMeshBatch owns a TArray<FMeshBatchElement>. Its exact offset has moved
    // across UE5 builds, so scan plausible TArray slots and inspect the first
    // few element records for NumPrimitives/IndexCount-style values. This is
    // diagnostic only and capped by UEVR_SN2_TRYADD_TRACE_MAX.
    for (uint32_t off = 0; off < 0x90 && out.element_hit_value == 0; off += 8) {
        uintptr_t ptr = 0;
        int32_t count = 0;
        int32_t capacity = 0;
        if (!sn2_safe_read(base + off, &ptr, sizeof(ptr)) ||
            !sn2_safe_read(base + off + 8, &count, sizeof(count)) ||
            !sn2_safe_read(base + off + 12, &capacity, sizeof(capacity))) {
            continue;
        }
        if (ptr == 0 || count <= 0 || count > 16 || capacity < count || capacity > 64) {
            continue;
        }
        const auto* elems = reinterpret_cast<const uint8_t*>(ptr);
        const int32_t elem_scan = count < 3 ? count : 3;
        for (int32_t elem = 0; elem < elem_scan && out.element_hit_value == 0; ++elem) {
            const uint32_t elem_base = static_cast<uint32_t>(elem) * 0x100u;
            for (uint32_t eoff = 0; eoff < 0x100; eoff += 4) {
                uint32_t v = 0;
                if (sn2_safe_read(elems + elem_base + eoff, &v, sizeof(v)) &&
                    sn2_tryadd_target_value(v)) {
                    out.element_array = ptr;
                    out.element_count = count;
                    out.element_hit_value = v;
                    out.element_hit_offset = elem_base + eoff;
                    break;
                }
            }
        }
    }

    return out;
}

struct Sn2AddMeshProcessorView {
    uintptr_t processor{0};
    uintptr_t view{0};
    uint32_t view_source_off{UINT32_MAX};
    uint64_t seq{0};
    uint32_t feature_level{0};
    uint32_t pass{0};
    float threshold{0.0f};
    int32_t stereo_pass{-999};
    uint32_t view24c8{0};
    uint32_t view24cc{0};
    int32_t view11ec{-999};
    uint8_t view11d9{0xff};
    uint8_t view11f7{0xff};
    uintptr_t slw_pass_ptr{0};
};

static std::mutex g_sn2_addmesh_right_basepass_mu;
static Sn2AddMeshProcessorView g_sn2_addmesh_right_basepass_global{};

static void sn2_record_right_basepass_processor(const Sn2AddMeshProcessorView& current) {
    if (current.stereo_pass != 2 || current.pass != 2 || current.processor == 0 || current.view == 0) {
        return;
    }
    std::scoped_lock _{g_sn2_addmesh_right_basepass_mu};
    g_sn2_addmesh_right_basepass_global = current;
}

static Sn2AddMeshProcessorView sn2_read_right_basepass_processor_global() {
    std::scoped_lock _{g_sn2_addmesh_right_basepass_mu};
    return g_sn2_addmesh_right_basepass_global;
}

static bool sn2_try_fill_addmesh_view(Sn2AddMeshProcessorView& out,
                                      uintptr_t candidate,
                                      uint32_t source_off) {
    if (candidate < 0x10000 ||
        !sn2_is_readable_process_range(candidate + 0xDD0, sizeof(int32_t)) ||
        !sn2_is_readable_process_range(candidate + 0x24C8, sizeof(uint32_t))) {
        return false;
    }

    int32_t stereo = -999;
    if (!sn2_safe_read(reinterpret_cast<const void*>(candidate + 0xDD0), &stereo, sizeof(stereo)) ||
        (stereo != 1 && stereo != 2)) {
        return false;
    }

    uintptr_t family = 0;
    uintptr_t slw_pass = 0;
    uint8_t view11f7 = 0xff;
    if (!sn2_safe_read(reinterpret_cast<const void*>(candidate + 0x8), &family, sizeof(family)) ||
        family < 0x10000 ||
        !sn2_is_readable_process_range(family, sizeof(uintptr_t)) ||
        !sn2_safe_read(reinterpret_cast<const void*>(candidate + 0x2098), &slw_pass, sizeof(slw_pass)) ||
        (slw_pass != 0 && !sn2_is_readable_process_range(slw_pass, sizeof(uintptr_t))) ||
        !sn2_safe_read(reinterpret_cast<const void*>(candidate + 0x11F7), &view11f7, sizeof(view11f7)) ||
        view11f7 > 1) {
        return false;
    }

    out.view = candidate;
    out.view_source_off = source_off;
    out.stereo_pass = stereo;
    sn2_safe_read(reinterpret_cast<const void*>(candidate + 0x24C8), &out.view24c8, sizeof(out.view24c8));
    sn2_safe_read(reinterpret_cast<const void*>(candidate + 0x24CC), &out.view24cc, sizeof(out.view24cc));
    sn2_safe_read(reinterpret_cast<const void*>(candidate + 0x11EC), &out.view11ec, sizeof(out.view11ec));
    sn2_safe_read(reinterpret_cast<const void*>(candidate + 0x11D9), &out.view11d9, sizeof(out.view11d9));
    out.view11f7 = view11f7;
    out.slw_pass_ptr = slw_pass;
    return true;
}

static Sn2AddMeshProcessorView sn2_read_addmesh_processor_view(void* this_ptr) {
    Sn2AddMeshProcessorView out{};
    out.processor = reinterpret_cast<uintptr_t>(this_ptr);
    if (this_ptr != nullptr) {
        const auto* processor = reinterpret_cast<const uint8_t*>(this_ptr);
        sn2_safe_read(processor + 0x20, &out.feature_level, sizeof(out.feature_level));
        sn2_safe_read(processor + 0x7C, &out.pass, sizeof(out.pass));
        sn2_safe_read(processor + 0x8C, &out.threshold, sizeof(out.threshold));

        // FMeshPassProcessor in SN2 shipping appears to keep FeatureLevel at
        // +0x20 and DrawListContext at +0x28 in the SLW decompile, so the view
        // is earlier than the old +0x28 guess. Probe likely direct slots first,
        // then fall back to a small pointer scan so UWE layout drift is visible
        // in the log instead of silently producing stereo=-999.
        static constexpr uint32_t k_direct_view_offsets[] = {0x18, 0x10, 0x28, 0x30};
        for (const uint32_t off : k_direct_view_offsets) {
            uintptr_t candidate = 0;
            if (sn2_safe_read(processor + off, &candidate, sizeof(candidate)) &&
                sn2_try_fill_addmesh_view(out, candidate, off)) {
                return out;
            }
        }

        for (uint32_t off = 0x08; off < 0x90; off += sizeof(uintptr_t)) {
            uintptr_t candidate = 0;
            if (sn2_safe_read(processor + off, &candidate, sizeof(candidate)) &&
                sn2_try_fill_addmesh_view(out, candidate, off)) {
                return out;
            }
        }
    }
    return out;
}

static void sn2_log_addmesh_stack(
    uint64_t call_seq,
    const Sn2AddMeshProcessorView& current,
    const void* mesh_ptr,
    const void* primitive_proxy,
    uintptr_t material_proxy)
{
    if (!sn2_addmesh_trace_stack_enabled()) {
        return;
    }

    static std::atomic<uint64_t> emitted{0};
    const auto max_rows = static_cast<uint64_t>(sn2_addmesh_trace_stack_max());
    if (max_rows == 0) {
        return;
    }
    const auto row = emitted.fetch_add(1, std::memory_order_relaxed) + 1;
    if (row > max_rows) {
        return;
    }

    void* frames[48]{};
    const USHORT got = RtlCaptureStackBackTrace(0, 48, frames, nullptr);

    std::string game_rvas;
    std::string all_frames;
    char chunk[64]{};
    for (USHORT i = 0; i < got; ++i) {
        const auto address = reinterpret_cast<uintptr_t>(frames[i]);
        uintptr_t rva = 0;
        if (sn2_main_module_rva(address, rva)) {
            std::snprintf(chunk, sizeof(chunk), "%s0x%llx", game_rvas.empty() ? "" : ",",
                static_cast<unsigned long long>(rva));
            game_rvas += chunk;
        }
        if (i < 16) {
            std::snprintf(chunk, sizeof(chunk), "%s0x%llx", all_frames.empty() ? "" : ",",
                static_cast<unsigned long long>(address));
            all_frames += chunk;
        }
    }
    if (game_rvas.empty()) {
        game_rvas = "none";
    }

    SPDLOG_WARN(
        "[SN2-AddMeshStack] #{} row={} stereo={} pass={} view=0x{:x} view_off=0x{:x} slwPass=0x{:x} mesh=0x{:x} "
        "prim=0x{:x} mat_proxy=0x{:x} prim_vft_rva=0x{:x} mat_vft_rva=0x{:x} "
        "prim_vfuncs={} game_rvas={} frames16={}",
        call_seq,
        row,
        current.stereo_pass,
        current.pass,
        current.view,
        current.view_source_off,
        current.slw_pass_ptr,
        reinterpret_cast<uintptr_t>(mesh_ptr),
        reinterpret_cast<uintptr_t>(primitive_proxy),
        material_proxy,
        sn2_read_vtable_rva(primitive_proxy),
        sn2_read_vtable_rva(reinterpret_cast<const void*>(material_proxy)),
        sn2_format_vtable_func_rvas(primitive_proxy, 40),
        game_rvas,
        all_frames);
}

static void sn2_scan_target_primitive_identity(
    uint64_t call_seq,
    const Sn2AddMeshProcessorView& current,
    const void* mesh_ptr,
    const void* primitive_proxy,
    int32_t static_mesh_id)
{
    if (!sn2_target_identity_scan_enabled() || primitive_proxy == nullptr) {
        return;
    }

    static std::atomic<uint64_t> emitted{0};
    const auto max_rows = static_cast<uint64_t>(sn2_target_identity_scan_max_rows());
    if (max_rows == 0) {
        return;
    }
    const auto row = emitted.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool should_log = row <= max_rows;

    const auto proxy_addr = reinterpret_cast<uintptr_t>(primitive_proxy);
    const auto mesh_addr = reinterpret_cast<uintptr_t>(mesh_ptr);
    const auto direct_psi = sn2_resolve_proxy_primitive_scene_info(primitive_proxy);
    if (direct_psi.found) {
        g_sn2_target_primitive_scene_info_ptr.store(direct_psi.psi, std::memory_order_relaxed);

        int32_t packed188 = -1;
        sn2_safe_read(
            reinterpret_cast<const void*>(direct_psi.psi + SN2_FPRIMITIVE_SCENE_INFO_PACKED_INDEX_DIA_OFF),
            &packed188,
            sizeof(packed188));

        const auto scene_lookup =
            sn2_find_scene_primitive_index(direct_psi.psi, proxy_addr, mesh_addr, current.processor);
        if (scene_lookup.found && scene_lookup.primitive_index >= 0) {
            g_sn2_target_primitive_index_hint.store(scene_lookup.primitive_index, std::memory_order_relaxed);
            g_sn2_target_scene_primitive_index.store(scene_lookup.primitive_index, std::memory_order_relaxed);
            g_sn2_target_scene_ptr.store(scene_lookup.scene, std::memory_order_relaxed);
            g_sn2_target_scene_primitives_off.store(scene_lookup.primitives_off, std::memory_order_relaxed);
            g_sn2_target_scene_proxies_off.store(scene_lookup.proxies_off, std::memory_order_relaxed);
        }

        if (should_log) {
            SPDLOG_WARN(
                "[SN2-TargetIdentityDirect] row={} add#{} stereo={} pass={} view=0x{:x} "
                "prim=0x{:x} psi=0x{:x} proxy_off=0x{:x} packed188={} static_mesh_id={} "
                "scene_found={} scene=0x{:x} primitives_off=0x{:x} primitives_count={} primitive_index={} "
                "proxies_off=0x{:x} proxy_match={}",
                row,
                call_seq,
                current.stereo_pass,
                current.pass,
                current.view,
                proxy_addr,
                direct_psi.psi,
                direct_psi.proxy_off,
                packed188,
                static_mesh_id,
                scene_lookup.found ? 1 : 0,
                scene_lookup.scene,
                scene_lookup.primitives_off,
                scene_lookup.primitives_count,
                scene_lookup.primitive_index,
                scene_lookup.proxies_off,
                scene_lookup.proxy_match ? 1 : 0);
        }

        if (!sn2_target_identity_verbose_scan_enabled()) {
            return;
        }
    } else if (!sn2_target_identity_verbose_scan_enabled()) {
        if (should_log) {
            SPDLOG_WARN(
                "[SN2-TargetIdentityDirect] row={} add#{} stereo={} pass={} view=0x{:x} "
                "prim=0x{:x} direct_lookup_failed static_mesh_id={}",
                row,
                call_seq,
                current.stereo_pass,
                current.pass,
                current.view,
                proxy_addr,
                static_mesh_id);
        }
        return;
    }

    int candidate_count = 0;
    for (size_t proxy_off = 0; proxy_off < 0x800 && candidate_count < 12; proxy_off += sizeof(uintptr_t)) {
        uintptr_t candidate = 0;
        if (!sn2_safe_read(reinterpret_cast<const void*>(proxy_addr + proxy_off), &candidate, sizeof(candidate)) ||
            candidate < 0x10000) {
            continue;
        }

        uintptr_t backref_off = UINTPTR_MAX;
        for (size_t info_off = 0; info_off < 0x400; info_off += sizeof(uintptr_t)) {
            uintptr_t value = 0;
            if (sn2_safe_read(reinterpret_cast<const void*>(candidate + info_off), &value, sizeof(value)) &&
                value == proxy_addr) {
                backref_off = info_off;
                break;
            }
        }
        if (backref_off == UINTPTR_MAX) {
            continue;
        }

        std::string plausible_i32s;
        char chunk[48]{};
        for (size_t info_off = 0x40; info_off < 0x900; info_off += sizeof(int32_t)) {
            int32_t value = -1;
            if (sn2_safe_read(reinterpret_cast<const void*>(candidate + info_off), &value, sizeof(value)) &&
                value > 0 &&
                value < 2000000) {
                std::snprintf(chunk, sizeof(chunk), "%s0x%zx:%d", plausible_i32s.empty() ? "" : ",",
                    info_off,
                    value);
                plausible_i32s += chunk;
                if (plausible_i32s.size() > 900) {
                    plausible_i32s += ",[trunc]";
                    break;
                }
            }
        }
        if (plausible_i32s.empty()) {
            plausible_i32s = "none";
        }

        uintptr_t mesh_array_data = 0;
        int32_t mesh_array_count = 0;
        int32_t mesh_array_capacity = 0;
        size_t mesh_array_off = UINTPTR_MAX;
        uintptr_t mesh_delta = 0;
        for (size_t info_off = 0x10; mesh_addr != 0 && info_off < 0x700; info_off += 8) {
            uintptr_t data = 0;
            int32_t count = 0;
            int32_t capacity = 0;
            if (!sn2_safe_read(reinterpret_cast<const void*>(candidate + info_off), &data, sizeof(data)) ||
                !sn2_safe_read(reinterpret_cast<const void*>(candidate + info_off + 8), &count, sizeof(count)) ||
                !sn2_safe_read(reinterpret_cast<const void*>(candidate + info_off + 12), &capacity, sizeof(capacity))) {
                continue;
            }
            if (data < 0x10000 || count <= 0 || count > 4096 || capacity < count || capacity > 4096) {
                continue;
            }
            const uintptr_t max_span = static_cast<uintptr_t>(capacity) * 0x400u;
            if (mesh_addr >= data && mesh_addr - data < max_span) {
                mesh_array_data = data;
                mesh_array_count = count;
                mesh_array_capacity = capacity;
                mesh_array_off = info_off;
                mesh_delta = mesh_addr - data;
                break;
            }
        }

        Sn2ScenePrimitiveLookup scene_lookup{};
        if (backref_off == 0x8 && mesh_array_off != UINTPTR_MAX) {
            g_sn2_target_primitive_scene_info_ptr.store(candidate, std::memory_order_relaxed);
            scene_lookup = sn2_find_scene_primitive_index(candidate, proxy_addr, mesh_addr, current.processor);
            if (scene_lookup.found && scene_lookup.primitive_index >= 0) {
                g_sn2_target_primitive_index_hint.store(scene_lookup.primitive_index, std::memory_order_relaxed);
                g_sn2_target_scene_primitive_index.store(scene_lookup.primitive_index, std::memory_order_relaxed);
                g_sn2_target_scene_ptr.store(scene_lookup.scene, std::memory_order_relaxed);
                g_sn2_target_scene_primitives_off.store(scene_lookup.primitives_off, std::memory_order_relaxed);
                g_sn2_target_scene_proxies_off.store(scene_lookup.proxies_off, std::memory_order_relaxed);
            }
        }

        ++candidate_count;
        if (should_log) {
            SPDLOG_WARN(
                "[SN2-TargetIdentity] row={} add#{} stereo={} pass={} view=0x{:x} "
                "prim=0x{:x} proxy_off=0x{:x} candidate=0x{:x} backref_off=0x{:x} "
                "mesh_array_off=0x{:x} mesh_array_data=0x{:x} mesh_array_count={} mesh_array_cap={} "
                "mesh_delta=0x{:x} static_mesh_id={} "
                "scene_search={} scene_found={} scene_field_off=0x{:x} scene=0x{:x} "
                "primitives_off=0x{:x} primitives_data=0x{:x} primitives_count={} primitive_index={} primitive_hits={} "
                "proxies_off=0x{:x} proxies_data=0x{:x} proxies_count={} proxy_match={} "
                "plausible_i32s={}",
                row,
                call_seq,
                current.stereo_pass,
                current.pass,
                current.view,
                proxy_addr,
                proxy_off,
                candidate,
                backref_off,
                mesh_array_off,
                mesh_array_data,
                mesh_array_count,
                mesh_array_capacity,
                mesh_delta,
                static_mesh_id,
                scene_lookup.searched ? 1 : 0,
                scene_lookup.found ? 1 : 0,
                scene_lookup.scene_field_off,
                scene_lookup.scene,
                scene_lookup.primitives_off,
                scene_lookup.primitives_data,
                scene_lookup.primitives_count,
                scene_lookup.primitive_index,
                scene_lookup.primitive_hits,
                scene_lookup.proxies_off,
                scene_lookup.proxies_data,
                scene_lookup.proxies_count,
                scene_lookup.proxy_match ? 1 : 0,
                plausible_i32s.c_str());
        }
    }

    if (candidate_count == 0 && should_log) {
        SPDLOG_WARN(
            "[SN2-TargetIdentity] row={} add#{} stereo={} pass={} view=0x{:x} "
            "prim=0x{:x} no_primitive_scene_info_candidates static_mesh_id={}",
            row,
            call_seq,
            current.stereo_pass,
            current.pass,
            current.view,
            proxy_addr,
            static_mesh_id);
    }
}

// Original signature: void AddMeshBatch(FBasePassMeshProcessor* this,
//                                       const FMeshBatch& mesh,
//                                       uint64_t batch_element_mask,
//                                       const FPrimitiveSceneProxy* primitive,
//                                       int32 StaticMeshId)
static void __fastcall add_mesh_batch_trampoline(
    void* this_ptr,
    const void* mesh_ptr,
    uint64_t batch_element_mask,
    const void* primitive_proxy,
    int32_t stencil_value)
{
    static std::atomic<uint64_t> addmesh_seq{0};
    const auto call_seq = addmesh_seq.fetch_add(1, std::memory_order_relaxed) + 1;

    thread_local Sn2AddMeshProcessorView last_secondary{};
    auto current = sn2_read_addmesh_processor_view(this_ptr);
    current.seq = call_seq;
    if (current.stereo_pass == 2 && current.pass == 2 && current.processor != 0) {
        last_secondary = current;
        sn2_record_right_basepass_processor(current);
    }

    // Pull the material proxy out of the mesh batch BEFORE the original
    // (the field is immutable for the duration of the call).
    uintptr_t material_proxy = 0;
    if (mesh_ptr != nullptr) {
        material_proxy = *reinterpret_cast<const uintptr_t*>(
            reinterpret_cast<const uint8_t*>(mesh_ptr) + k_fmeshbatch_material_render_proxy_offset);
    }
    const auto primitive_vtable_rva = sn2_read_vtable_rva(primitive_proxy);
    const auto material_proxy_vtable_rva = sn2_read_vtable_rva(reinterpret_cast<const void*>(material_proxy));

    // First-sighting log per proxy. Skip if env disabled.
    if (sn2_material_hook::env_enabled() && material_proxy != 0) {
        bool first_sighting = false;
        {
            std::scoped_lock _{sn2_material_hook::proxy_cache_mu()};
            auto& cache = sn2_material_hook::proxy_cache();
            if (cache.find(material_proxy) == cache.end()) {
                // Store a placeholder name; full name resolution requires
                // calling GetFriendlyName which we can't do safely from here.
                cache.emplace(material_proxy, std::string{}); // empty = "seen but not named"
                first_sighting = true;
            }
        }
        if (first_sighting) {
            SPDLOG_WARN(
                "[SN2-MaterialSeen] proxy=0x{:x} mesh=0x{:x} primitive=0x{:x} static_mesh_id={} batch_mask=0x{:x}",
                material_proxy,
                reinterpret_cast<uintptr_t>(mesh_ptr),
                reinterpret_cast<uintptr_t>(primitive_proxy),
                stencil_value,
                batch_element_mask);
        }
    }

    const bool addmesh_trace = sn2_addmesh_trace_enabled();
    if (sn2_target_mesh_discovery_needed()) {
        Sn2TryAddMeshProbe mesh_probe{};
        const bool target_only = sn2_addmesh_trace_target_shape_only();
        const int max_rows = sn2_addmesh_trace_max_rows();
        if (target_only || static_cast<int>(call_seq) <= max_rows) {
            mesh_probe = sn2_probe_mesh_batch_shape(mesh_ptr);
        }
        const bool shape_hit =
            mesh_probe.direct_hit_value != 0 || mesh_probe.element_hit_value != 0;
        if (shape_hit) {
            sn2_runtime_state::AddMeshLast live_add{};
            live_add.processor_kind = 1;
            live_add.stereo_pass = current.stereo_pass;
            live_add.mesh_pass = current.pass;
            live_add.processor = current.processor;
            live_add.view = current.view;
            live_add.mesh = reinterpret_cast<uintptr_t>(mesh_ptr);
            live_add.primitive = reinterpret_cast<uintptr_t>(primitive_proxy);
            live_add.material_proxy = material_proxy;
            live_add.primitive_vtable_rva = primitive_vtable_rva;
            live_add.material_vtable_rva = material_proxy_vtable_rva;
            live_add.static_mesh_id = stencil_value;
            live_add.batch_element_mask = batch_element_mask;
            live_add.direct_hit_value = mesh_probe.direct_hit_value;
            live_add.direct_hit_offset = mesh_probe.direct_hit_offset;
            live_add.element_hit_value = mesh_probe.element_hit_value;
            live_add.element_hit_offset = mesh_probe.element_hit_offset;
            sn2_runtime_state::note_addmesh_shape(live_add);
            g_sn2_obs_basepass.shape_hit_count.fetch_add(1, std::memory_order_relaxed);
            if (current.stereo_pass == 1) {
                g_sn2_obs_basepass.left_eye_hit_count.fetch_add(1, std::memory_order_relaxed);
            } else if (current.stereo_pass == 2) {
                g_sn2_obs_basepass.right_eye_hit_count.fetch_add(1, std::memory_order_relaxed);
            }
            if (sn2_pass_id_probe_enabled()) {
                const auto pid_n = g_sn2_obs_basepass.shape_hit_count.load(std::memory_order_relaxed);
                if ((int)pid_n <= sn2_pass_id_probe_max() || (pid_n % 100) == 0) {
                    SPDLOG_WARN(
                        "[SN2-PassIdProbe] processor=BASEPASS pass=2 hit#{} stereo={} view=0x{:x} "
                        "view_off=0x{:x} slwPass=0x{:x} proc=0x{:x} mesh=0x{:x} prim=0x{:x} "
                        "static_mesh_id={} mask=0x{:x}",
                        pid_n, current.stereo_pass, current.view,
                        current.view_source_off,
                        current.slw_pass_ptr,
                        reinterpret_cast<uintptr_t>(this_ptr),
                        reinterpret_cast<uintptr_t>(mesh_ptr),
                        reinterpret_cast<uintptr_t>(primitive_proxy),
                        stencil_value, batch_element_mask);
                }
            }
            g_sn2_target_mesh_ptr.store(reinterpret_cast<uintptr_t>(mesh_ptr), std::memory_order_relaxed);
            g_sn2_target_primitive_ptr.store(reinterpret_cast<uintptr_t>(primitive_proxy), std::memory_order_relaxed);
            g_sn2_target_static_mesh_id.store(stencil_value, std::memory_order_relaxed);
            const auto direct_psi = sn2_resolve_proxy_primitive_scene_info(primitive_proxy);
            if (direct_psi.found) {
                g_sn2_target_primitive_scene_info_ptr.store(direct_psi.psi, std::memory_order_relaxed);
            }

            const auto tls = g_sn2_gendyn_tls;
            const auto lookup = tls.active
                ? sn2_find_dynamic_mesh_element(
                      tls.dynamic_meshes,
                      tls.pass_masks,
                      reinterpret_cast<uintptr_t>(mesh_ptr),
                      reinterpret_cast<uintptr_t>(primitive_proxy))
                : Sn2DynamicMeshElementLookup{};
            const auto static_lookup = tls.active
                ? sn2_find_pointer_array_element(
                      tls.static_mesh_requests,
                      reinterpret_cast<uintptr_t>(mesh_ptr))
                : Sn2PointerArrayLookup{};
            if (current.stereo_pass == 1 && current.pass == 2 && (static_lookup.found || !tls.active)) {
                g_sn2_target_static_mesh_request_ptr.store(
                    reinterpret_cast<uintptr_t>(mesh_ptr),
                    std::memory_order_relaxed);
            }
            if (addmesh_trace) {
                SPDLOG_WARN(
                    "[SN2-AddMeshInGenDyn] add#{} tls_active={} gen#{} gen_view=0x{:x} gen_view_index={} "
                    "gen_stereo={} gen_shading={} gen_pass={} gen_proc=0x{:x} dyn_data=0x{:x} dyn_count={} "
                    "masks_data=0x{:x} masks_count={} static_data=0x{:x} static_count={} "
                    "searched={} found={} elem={} mesh_hits={} prim_hits={} "
                    "elem_mesh=0x{:x} elem_prim=0x{:x} elem_flags=0x{:x} elem_mask=0x{:x} "
                    "static_searched={} static_found={} static_elem={} static_hits={} static_value=0x{:x} "
                    "add_stereo={} add_pass={} add_view=0x{:x} mesh=0x{:x} prim=0x{:x} static_mesh_id={}",
                    call_seq,
                    tls.active ? 1 : 0,
                    tls.seq,
                    tls.view,
                    tls.view_index,
                    tls.stereo_pass,
                    tls.shading_path,
                    tls.mesh_pass,
                    tls.processor,
                    tls.dynamic_meshes.data,
                    tls.dynamic_meshes.count,
                    tls.pass_masks.data,
                    tls.pass_masks.count,
                    tls.static_mesh_requests.data,
                    tls.static_mesh_requests.count,
                    lookup.searched ? 1 : 0,
                    lookup.found ? 1 : 0,
                    lookup.elem,
                    lookup.mesh_hits,
                    lookup.primitive_hits,
                    lookup.mesh,
                    lookup.primitive,
                    lookup.relevance_flags,
                    lookup.pass_mask,
                    static_lookup.searched ? 1 : 0,
                    static_lookup.found ? 1 : 0,
                    static_lookup.elem,
                    static_lookup.hits,
                    static_lookup.value,
                    current.stereo_pass,
                    current.pass,
                    current.view,
                    reinterpret_cast<uintptr_t>(mesh_ptr),
                    reinterpret_cast<uintptr_t>(primitive_proxy),
                    stencil_value);
            }
            sn2_scan_target_primitive_identity(
                call_seq,
                current,
                mesh_ptr,
                primitive_proxy,
                stencil_value);
        }
        const bool should_log = target_only ? shape_hit : (static_cast<int>(call_seq) <= max_rows || shape_hit);
        if (addmesh_trace && should_log) {
            const uint64_t secondary_age = last_secondary.seq == 0 ? UINT64_MAX : (call_seq - last_secondary.seq);

            SPDLOG_WARN(
                "[SN2-AddMesh] #{} shape={} this=0x{:x} view=0x{:x} stereo={} "
                "feature={} pass={} thresh={:.3f} view24c8=0x{:08x} view24cc=0x{:08x} "
                "v11d9={} v11ec={} v11f7={} mesh=0x{:x} prim=0x{:x} mat_proxy=0x{:x} "
                "prim_vft_rva=0x{:x} mat_vft_rva=0x{:x} static_mesh_id={} mask=0x{:x} direct_hit={}@0x{:x} elem_hit={}@0x{:x} "
                "elem_array=0x{:x} elem_count={} last_r_proc=0x{:x} last_r_view=0x{:x} last_r_age={}",
                call_seq,
                shape_hit ? 1 : 0,
                current.processor,
                current.view,
                current.stereo_pass,
                current.feature_level,
                current.pass,
                current.threshold,
                current.view24c8,
                current.view24cc,
                static_cast<unsigned>(current.view11d9),
                current.view11ec,
                static_cast<unsigned>(current.view11f7),
                reinterpret_cast<uintptr_t>(mesh_ptr),
                reinterpret_cast<uintptr_t>(primitive_proxy),
                material_proxy,
                primitive_vtable_rva,
                material_proxy_vtable_rva,
                stencil_value,
                batch_element_mask,
                mesh_probe.direct_hit_value,
                mesh_probe.direct_hit_offset,
                mesh_probe.element_hit_value,
                mesh_probe.element_hit_offset,
                mesh_probe.element_array,
                mesh_probe.element_count,
                last_secondary.processor,
                last_secondary.view,
                secondary_age);

            if (shape_hit) {
                sn2_log_addmesh_stack(
                    call_seq,
                    current,
                    mesh_ptr,
                    primitive_proxy,
                    material_proxy);
            }
        }
    }

    // Call original.
    g_add_mesh_batch_hook.call<void>(this_ptr, mesh_ptr, batch_element_mask, primitive_proxy, stencil_value);

    if (sn2_addmesh_force_right_enabled() && current.stereo_pass == 1 && current.pass == 2) {
        static const ULONGLONG start_tick = GetTickCount64();
        static std::atomic<uint64_t> force_total{0};
        const auto delay_ms = static_cast<ULONGLONG>(sn2_addmesh_force_right_delay_ms());
        if (GetTickCount64() - start_tick < delay_ms) {
            return;
        }
        const auto max_total = static_cast<uint64_t>(sn2_addmesh_force_right_max_total());
        if (max_total != 0 && force_total.load(std::memory_order_relaxed) >= max_total) {
            return;
        }

        const auto mesh_probe = sn2_probe_mesh_batch_shape(mesh_ptr);
        const bool shape_hit =
            mesh_probe.direct_hit_value != 0 || mesh_probe.element_hit_value != 0;
        const auto max_age = static_cast<uint64_t>(sn2_addmesh_force_right_max_age());
        Sn2AddMeshProcessorView force_secondary = last_secondary;
        uint64_t secondary_age = force_secondary.seq == 0 || call_seq < force_secondary.seq
            ? UINT64_MAX
            : (call_seq - force_secondary.seq);
        bool using_global_secondary = false;
        if (shape_hit &&
            (force_secondary.processor == 0 || secondary_age > max_age) &&
            sn2_addmesh_force_right_global_enabled()) {
            const auto global_secondary = sn2_read_right_basepass_processor_global();
            const uint64_t global_age = global_secondary.seq == 0 || call_seq < global_secondary.seq
                ? UINT64_MAX
                : (call_seq - global_secondary.seq);
            if (global_secondary.processor != 0 && global_age <= max_age) {
                force_secondary = global_secondary;
                secondary_age = global_age;
                using_global_secondary = true;
            }
        }

        if (shape_hit && force_secondary.processor != 0 && secondary_age <= max_age) {
            const auto force_n = force_total.fetch_add(1, std::memory_order_relaxed) + 1;
            const bool execute_unsafe = sn2_addmesh_force_right_execute_unsafe_enabled();
            SPDLOG_WARN(
                "[SN2-AddMeshForce] {}#{} target shape left_proc=0x{:x} left_view=0x{:x} "
                "right_proc=0x{:x} right_view=0x{:x} age={} global={} execute_unsafe={} "
                "mesh=0x{:x} prim=0x{:x} mat_proxy=0x{:x}",
                execute_unsafe ? "readd" : "would_readd",
                force_n,
                current.processor,
                current.view,
                force_secondary.processor,
                force_secondary.view,
                secondary_age,
                using_global_secondary ? 1 : 0,
                execute_unsafe ? 1 : 0,
                reinterpret_cast<uintptr_t>(mesh_ptr),
                reinterpret_cast<uintptr_t>(primitive_proxy),
                material_proxy);
            if (execute_unsafe) {
                g_add_mesh_batch_hook.call<void>(
                    reinterpret_cast<void*>(force_secondary.processor),
                    mesh_ptr,
                    batch_element_mask,
                    primitive_proxy,
                    stencil_value);
            }
        } else if (shape_hit) {
            const auto global_secondary = sn2_read_right_basepass_processor_global();
            const uint64_t global_age = global_secondary.seq == 0 || call_seq < global_secondary.seq
                ? UINT64_MAX
                : (call_seq - global_secondary.seq);
            SPDLOG_WARN(
                "[SN2-AddMeshForce] skip target shape reason=no_recent_right_proc "
                "tls_age={} global_age={} max_age={} tls_right_proc=0x{:x} global_right_proc=0x{:x} use_global={}",
                secondary_age,
                global_age,
                max_age,
                last_secondary.processor,
                global_secondary.processor,
                sn2_addmesh_force_right_global_enabled() ? 1 : 0);
        }
    }
}

// Original signature:
//   bool FBasePassMeshProcessor::TryAddMeshBatch(
//       const FMeshBatch& mesh,
//       uint64 batch_element_mask,
//       const FPrimitiveSceneProxy* primitive,
//       int32 stencil_value,
//       const FMaterialRenderProxy& material_proxy,
//       const FMaterial& material)
static bool __fastcall try_add_mesh_batch_trampoline(
    void* this_ptr,
    const void* mesh_ptr,
    uint64_t batch_element_mask,
    const void* primitive_proxy,
    int32_t stencil_value,
    const void* material_proxy,
    const void* material)
{
    const bool result = g_try_add_mesh_batch_hook.call<bool>(
        this_ptr,
        mesh_ptr,
        batch_element_mask,
        primitive_proxy,
        stencil_value,
        material_proxy,
        material);

    if (!sn2_tryadd_trace_enabled()) {
        return result;
    }

    static std::atomic<uint64_t> seq{0};
    const auto n = seq.fetch_add(1, std::memory_order_relaxed) + 1;

    Sn2TryAddMeshProbe mesh_probe{};
    const bool target_only = sn2_tryadd_trace_target_shape_only();
    const int max_rows = sn2_tryadd_trace_max_rows();
    if (target_only || static_cast<int>(n) <= max_rows) {
        mesh_probe = sn2_probe_mesh_batch_shape(mesh_ptr);
    }
    const bool shape_hit =
        mesh_probe.direct_hit_value != 0 || mesh_probe.element_hit_value != 0;
    if (target_only && !shape_hit) {
        return result;
    }
    if (!target_only && static_cast<int>(n) > max_rows && !shape_hit) {
        return result;
    }

    uintptr_t view = 0;
    uint32_t feature_level = 0;
    uint32_t translucency_pass = 0;
    float threshold = 0.0f;
    if (this_ptr != nullptr) {
        const auto* processor = reinterpret_cast<const uint8_t*>(this_ptr);
        sn2_safe_read(processor + 0x28, &view, sizeof(view));
        sn2_safe_read(processor + 0x20, &feature_level, sizeof(feature_level));
        sn2_safe_read(processor + 0x7C, &translucency_pass, sizeof(translucency_pass));
        sn2_safe_read(processor + 0x8C, &threshold, sizeof(threshold));
    }

    int32_t stereo_pass = -999;
    uint32_t view24c8 = 0;
    uint32_t view24cc = 0;
    int32_t view11ec = -999;
    uint8_t view11d9 = 0xff;
    uint8_t view11f7 = 0xff;
    if (view != 0) {
        sn2_safe_read(reinterpret_cast<const void*>(view + 0xDD0), &stereo_pass, sizeof(stereo_pass));
        sn2_safe_read(reinterpret_cast<const void*>(view + 0x24C8), &view24c8, sizeof(view24c8));
        sn2_safe_read(reinterpret_cast<const void*>(view + 0x24CC), &view24cc, sizeof(view24cc));
        sn2_safe_read(reinterpret_cast<const void*>(view + 0x11EC), &view11ec, sizeof(view11ec));
        sn2_safe_read(reinterpret_cast<const void*>(view + 0x11D9), &view11d9, sizeof(view11d9));
        sn2_safe_read(reinterpret_cast<const void*>(view + 0x11F7), &view11f7, sizeof(view11f7));
    }

    SPDLOG_WARN(
        "[SN2-TryAdd] #{} ok={} shape={} this=0x{:x} view=0x{:x} stereo={} "
        "feature={} pass={} thresh={:.3f} view24c8=0x{:08x} view24cc=0x{:08x} "
        "v11d9={} v11ec={} v11f7={} mesh=0x{:x} prim=0x{:x} mat_proxy=0x{:x} "
        "mat=0x{:x} prim_vft_rva=0x{:x} mat_vft_rva=0x{:x} stencil={} mask=0x{:x} direct_hit={}@0x{:x} elem_hit={}@0x{:x} "
        "elem_array=0x{:x} elem_count={}",
        n,
        result ? 1 : 0,
        shape_hit ? 1 : 0,
        reinterpret_cast<uintptr_t>(this_ptr),
        view,
        stereo_pass,
        feature_level,
        translucency_pass,
        threshold,
        view24c8,
        view24cc,
        static_cast<unsigned>(view11d9),
        view11ec,
        static_cast<unsigned>(view11f7),
        reinterpret_cast<uintptr_t>(mesh_ptr),
        reinterpret_cast<uintptr_t>(primitive_proxy),
        reinterpret_cast<uintptr_t>(material_proxy),
        reinterpret_cast<uintptr_t>(material),
        sn2_read_vtable_rva(primitive_proxy),
        sn2_read_vtable_rva(material_proxy),
        stencil_value,
        batch_element_mask,
        mesh_probe.direct_hit_value,
        mesh_probe.direct_hit_offset,
        mesh_probe.element_hit_value,
        mesh_probe.element_hit_offset,
        mesh_probe.element_array,
        mesh_probe.element_count);

    return result;
}

// ============================================================================
// Pass-id probe — parallel hook on FSingleLayerWaterPassMeshProcessor::AddMeshBatch
// (FBasePassMeshProcessor::AddMeshBatch is already hooked above via
// add_mesh_batch_trampoline; that path tags BASEPASS via g_sn2_obs_basepass).
// Together the two paths definitively identify which mesh pass owns the
// target-shape mesh (~25k tri / 76608 indices). See Sn2MaterialNameHook.hpp
// for the RVA constant; see SN2_PASS17_OPEN_QUESTIONS_2026_05_28.md §2 + §8.
//
// Signature matches the basepass overload — both inherit from FMeshPassProcessor:
//   virtual void AddMeshBatch(const FMeshBatch&, uint64 BatchElementMask,
//                              const FPrimitiveSceneProxy*, int32 StaticMeshId);
// ============================================================================

static void __fastcall slw_add_mesh_batch_probe_trampoline(
    void* this_ptr,
    const void* mesh_ptr,
    uint64_t batch_element_mask,
    const void* primitive_proxy,
    int32_t stencil_value)
{
    g_sn2_obs_slw.count.fetch_add(1, std::memory_order_relaxed);

    if ((sn2_pass_id_probe_enabled() || sn2_target_mesh_discovery_needed()) && mesh_ptr != nullptr) {
        Sn2TryAddMeshProbe shape = sn2_probe_mesh_batch_shape(mesh_ptr);
        const bool shape_hit = shape.direct_hit_value != 0 || shape.element_hit_value != 0;
        if (shape_hit) {
            g_sn2_obs_slw.shape_hit_count.fetch_add(1, std::memory_order_relaxed);
            Sn2AddMeshProcessorView current = sn2_read_addmesh_processor_view(this_ptr);
            uintptr_t material_proxy = 0;
            sn2_safe_read(
                reinterpret_cast<const uint8_t*>(mesh_ptr) + k_fmeshbatch_material_render_proxy_offset,
                &material_proxy,
                sizeof(material_proxy));
            sn2_runtime_state::AddMeshLast live_add{};
            live_add.processor_kind = 2;
            live_add.stereo_pass = current.stereo_pass;
            live_add.mesh_pass = current.pass;
            live_add.processor = current.processor;
            live_add.view = current.view;
            live_add.mesh = reinterpret_cast<uintptr_t>(mesh_ptr);
            live_add.primitive = reinterpret_cast<uintptr_t>(primitive_proxy);
            live_add.material_proxy = material_proxy;
            live_add.primitive_vtable_rva = sn2_read_vtable_rva(primitive_proxy);
            live_add.material_vtable_rva = sn2_read_vtable_rva(reinterpret_cast<const void*>(material_proxy));
            live_add.static_mesh_id = stencil_value;
            live_add.batch_element_mask = batch_element_mask;
            live_add.direct_hit_value = shape.direct_hit_value;
            live_add.direct_hit_offset = shape.direct_hit_offset;
            live_add.element_hit_value = shape.element_hit_value;
            live_add.element_hit_offset = shape.element_hit_offset;
            sn2_runtime_state::note_addmesh_shape(live_add);

            if (sn2_target_mesh_discovery_needed()) {
                g_sn2_target_mesh_ptr.store(reinterpret_cast<uintptr_t>(mesh_ptr), std::memory_order_relaxed);
                g_sn2_target_primitive_ptr.store(reinterpret_cast<uintptr_t>(primitive_proxy), std::memory_order_relaxed);

                const auto direct_psi = sn2_resolve_proxy_primitive_scene_info(primitive_proxy);
                if (direct_psi.found) {
                    g_sn2_target_primitive_scene_info_ptr.store(direct_psi.psi, std::memory_order_relaxed);
                }

                if (sn2_underwater_target_mesh_pass() == 6) {
                    g_sn2_target_static_mesh_request_ptr.store(
                        reinterpret_cast<uintptr_t>(mesh_ptr),
                        std::memory_order_relaxed);
                }

                sn2_scan_target_primitive_identity(
                    0,
                    current,
                    mesh_ptr,
                    primitive_proxy,
                    stencil_value);
            }

            if (current.stereo_pass == 1) {
                g_sn2_obs_slw.left_eye_hit_count.fetch_add(1, std::memory_order_relaxed);
            } else if (current.stereo_pass == 2) {
                g_sn2_obs_slw.right_eye_hit_count.fetch_add(1, std::memory_order_relaxed);
            }
            const auto n = g_sn2_obs_slw.shape_hit_count.load(std::memory_order_relaxed);
            if (sn2_pass_id_probe_enabled() && ((int)n <= sn2_pass_id_probe_max() || (n % 100) == 0)) {
                SPDLOG_WARN(
                    "[SN2-PassIdProbe] processor=SLW pass=6 hit#{} stereo={} view=0x{:x} "
                    "view_off=0x{:x} slwPass=0x{:x} proc=0x{:x} mesh=0x{:x} prim=0x{:x} "
                    "mat_proxy=0x{:x} static_mesh_id={} mask=0x{:x} "
                    "shape_direct={}@0x{:x} shape_elem={}@0x{:x}",
                    n, current.stereo_pass, current.view,
                    current.view_source_off,
                    current.slw_pass_ptr,
                    reinterpret_cast<uintptr_t>(this_ptr),
                    reinterpret_cast<uintptr_t>(mesh_ptr),
                    reinterpret_cast<uintptr_t>(primitive_proxy),
                    material_proxy,
                    stencil_value, batch_element_mask,
                    shape.direct_hit_value, shape.direct_hit_offset,
                    shape.element_hit_value, shape.element_hit_offset);
            }
        }
    }

    g_slw_add_mesh_batch_hook.call<void>(this_ptr, mesh_ptr, batch_element_mask,
                                          primitive_proxy, stencil_value);
}

// ============================================================================
// Path C — FRDGBuilder::Execute hook that enumerates pass names by reading
// each FRDGPass.Name (FRDGEventName).EventFormat const wchar_t*.
// Works in shipping despite WITH_PROFILEGPU=0 stripping the PIX3*Event macros.
// ============================================================================

static void __fastcall frdg_builder_execute_trampoline(void* builder_void) {
    do {
        if (!sn2_rdg_pass_names_enabled() || builder_void == nullptr) break;

        static std::atomic<uint64_t> frame_counter{0};
        static std::atomic<bool> first_dump_done{false};
        const auto frame_n = frame_counter.fetch_add(1, std::memory_order_relaxed) + 1;
        const bool every_frame = sn2_rdg_pass_names_every_frame();
        const bool first_only = first_dump_done.load(std::memory_order_relaxed);
        if (!every_frame && first_only) break;

        const uintptr_t b = reinterpret_cast<uintptr_t>(builder_void);
        // FRDGBuilder.Passes is at +0x1B8 per editor PDB.
        // It's a TArray; Data layout may be TArray<FRDGPass*> (most likely) or
        // TArray<FRDGPass> by-value (less likely given 384B element size).
        Sn2TArrayHeader passes = sn2_read_tarray_header(reinterpret_cast<const void*>(b + 0x1B8));
        if (!passes.valid || passes.data < 0x10000 || passes.count <= 0 || passes.count > 100000) {
            // log once per session for diagnostics
            static std::atomic<bool> logged_bad_passes{false};
            bool e = false;
            if (logged_bad_passes.compare_exchange_strong(e, true, std::memory_order_relaxed)) {
                SPDLOG_WARN("[SN2-RDGPass] frame#{} builder=0x{:x} passes header looked invalid: "
                            "data=0x{:x} count={} cap={} (no enumeration this run)",
                    frame_n, b, passes.data, passes.count, passes.capacity);
            }
            break;
        }

        const int max_log = sn2_rdg_pass_names_max();
        int logged = 0;
        int by_pointer_hits = 0;
        int by_value_hits = 0;
        bool announced_layout = false;

        SPDLOG_WARN("[SN2-RDGPass] frame#{} builder=0x{:x} passes data=0x{:x} count={} cap={}",
            frame_n, b, passes.data, passes.count, passes.capacity);

        for (int32_t i = 0; i < passes.count && logged < max_log; ++i) {
            // Try by-pointer interpretation first: TArray<FRDGPass*>
            uintptr_t pass_ptr = 0;
            if (sn2_safe_read(reinterpret_cast<const void*>(passes.data + i * 8),
                              &pass_ptr, sizeof(pass_ptr)) && pass_ptr >= 0x10000) {
                // Read pass_ptr+0x10 = FRDGEventName.EventFormat (const wchar_t*)
                uintptr_t name_ptr = 0;
                if (sn2_safe_read(reinterpret_cast<const void*>(pass_ptr + 0x10),
                                  &name_ptr, sizeof(name_ptr)) && name_ptr >= 0x10000) {
                    char buf[256]; int j = 0;
                    while (j < 255) {
                        uint16_t wc = 0;
                        if (!sn2_safe_read(reinterpret_cast<const void*>(name_ptr + j*2),
                                           &wc, sizeof(wc)) || wc == 0) break;
                        buf[j++] = (wc < 128 && wc >= 32) ? (char)wc : '?';
                    }
                    buf[j] = 0;
                    if (j > 0 && j < 200) {
                        if (!announced_layout) {
                            SPDLOG_WARN("[SN2-RDGPass]   layout=by_pointer (TArray<FRDGPass*>)");
                            announced_layout = true;
                        }
                        by_pointer_hits++;
                        SPDLOG_WARN("[SN2-RDGPass]   idx={} pass=0x{:x} name=\"{}\"",
                            i, pass_ptr, buf);
                        logged++;
                        continue;
                    }
                }
            }
            // Fallback: by-value interpretation TArray<FRDGPass> (384 B stride)
            const uintptr_t by_val_pass = passes.data + i * 0x180;
            uintptr_t name_ptr_v = 0;
            if (sn2_safe_read(reinterpret_cast<const void*>(by_val_pass + 0x10),
                              &name_ptr_v, sizeof(name_ptr_v)) && name_ptr_v >= 0x10000) {
                char buf[256]; int j = 0;
                while (j < 255) {
                    uint16_t wc = 0;
                    if (!sn2_safe_read(reinterpret_cast<const void*>(name_ptr_v + j*2),
                                       &wc, sizeof(wc)) || wc == 0) break;
                    buf[j++] = (wc < 128 && wc >= 32) ? (char)wc : '?';
                }
                buf[j] = 0;
                if (j > 0 && j < 200) {
                    if (!announced_layout) {
                        SPDLOG_WARN("[SN2-RDGPass]   layout=by_value (TArray<FRDGPass> stride=0x180)");
                        announced_layout = true;
                    }
                    by_value_hits++;
                    SPDLOG_WARN("[SN2-RDGPass]   idx={} pass=0x{:x} name=\"{}\"",
                        i, by_val_pass, buf);
                    logged++;
                }
            }
        }

        SPDLOG_WARN("[SN2-RDGPass] frame#{} done; logged={} by_ptr_hits={} by_val_hits={}",
            frame_n, logged, by_pointer_hits, by_value_hits);
        first_dump_done.store(true, std::memory_order_relaxed);
    } while (false);

    g_frdg_builder_execute_hook.call<void>(builder_void);
}

static bool install_frdg_builder_execute_hook() {
    if (!sn2_rdg_pass_names_enabled()) {
        SPDLOG_INFO("[SN2-RDGPass] disabled (UEVR_SN2_RDG_PASS_NAMES not set)");
        return false;
    }
    const auto exe_base = reinterpret_cast<uintptr_t>(utility::get_executable());
    const auto target = exe_base + sn2_material_hook::SUBNAUTICA2_FRDGBUILDER_EXECUTE_RVA;
    if (exe_base == 0 || !sn2_is_executable_process_range(target, 0x20)) {
        SPDLOG_WARN("[SN2-RDGPass] bad target VA 0x{:x}", target);
        return false;
    }
    g_frdg_builder_execute_hook = safetyhook::create_inline(
        reinterpret_cast<void*>(target),
        reinterpret_cast<void*>(&frdg_builder_execute_trampoline),
        safetyhook::InlineHook::StartDisabled);
    if (!g_frdg_builder_execute_hook) {
        SPDLOG_WARN("[SN2-RDGPass] safetyhook create failed at 0x{:x}", target);
        return false;
    }
    if (auto e = g_frdg_builder_execute_hook.enable(); !e.has_value()) {
        SPDLOG_WARN("[SN2-RDGPass] enable failed: {}", static_cast<int>(e.error().type));
        return false;
    }
    SPDLOG_WARN("[SN2-RDGPass] FRDGBuilder::Execute hook installed at 0x{:x} (RVA 0x{:x}); "
                "max={} every_frame={}",
        target, sn2_material_hook::SUBNAUTICA2_FRDGBUILDER_EXECUTE_RVA,
        sn2_rdg_pass_names_max(),
        sn2_rdg_pass_names_every_frame() ? 1 : 0);
    return true;
}

static bool install_slw_pass_id_probe_hook() {
    if (!sn2_pass_id_probe_enabled() && !sn2_target_mesh_discovery_needed()) {
        SPDLOG_INFO("[SN2-PassIdProbe] disabled (UEVR_SN2_PASS_ID_PROBE and SN2 target discovery not set)");
        return false;
    }
    const auto exe_base = reinterpret_cast<uintptr_t>(utility::get_executable());
    const auto target = exe_base + sn2_material_hook::SUBNAUTICA2_FSLW_ADDMESHBATCH_RVA;
    if (exe_base == 0 || !sn2_is_executable_process_range(target, 0x20)) {
        SPDLOG_WARN("[SN2-PassIdProbe] bad SLW target VA 0x{:x}", target);
        return false;
    }
    g_slw_add_mesh_batch_hook = safetyhook::create_inline(
        reinterpret_cast<void*>(target),
        reinterpret_cast<void*>(&slw_add_mesh_batch_probe_trampoline),
        safetyhook::InlineHook::StartDisabled);
    if (!g_slw_add_mesh_batch_hook) {
        SPDLOG_WARN("[SN2-PassIdProbe] SLW safetyhook create failed at 0x{:x}", target);
        return false;
    }
    if (auto e = g_slw_add_mesh_batch_hook.enable(); !e.has_value()) {
        SPDLOG_WARN("[SN2-PassIdProbe] SLW enable failed: {}", static_cast<int>(e.error().type));
        return false;
    }
    SPDLOG_WARN("[SN2-PassIdProbe] SLW AddMeshBatch hook installed at 0x{:x} (RVA 0x{:x}); "
                "BASEPASS probe is inline in add_mesh_batch_trampoline; probe={} discovery={} max_rows={}",
        target, sn2_material_hook::SUBNAUTICA2_FSLW_ADDMESHBATCH_RVA,
        sn2_pass_id_probe_enabled() ? 1 : 0,
        sn2_target_mesh_discovery_needed() ? 1 : 0,
        sn2_pass_id_probe_max());
    return true;
}

static bool install_material_name_hook() {
    if (!sn2_material_hook::env_enabled() && !sn2_target_mesh_discovery_needed()) {
        SPDLOG_INFO("[SN2-MaterialHook] disabled (UEVR_SN2_MATERIAL_HOOK or SN2 target discovery not set)");
        return false;
    }

    const auto exe_base = reinterpret_cast<uintptr_t>(utility::get_executable());
    const auto target = exe_base + sn2_material_hook::SUBNAUTICA2_FBASEPASS_ADDMESHBATCH_RVA;
    if (exe_base == 0 || !sn2_is_executable_process_range(target, 0x20)) {
        SPDLOG_WARN("[SN2-MaterialHook] bad target VA 0x{:x}", target);
        return false;
    }
    g_add_mesh_batch_hook = safetyhook::create_inline(
        reinterpret_cast<void*>(target),
        reinterpret_cast<void*>(&add_mesh_batch_trampoline),
        safetyhook::InlineHook::StartDisabled);
    if (!g_add_mesh_batch_hook) {
        SPDLOG_WARN("[SN2-MaterialHook] safetyhook create failed at 0x{:x}", target);
        return false;
    }
    if (auto e = g_add_mesh_batch_hook.enable(); !e.has_value()) {
        SPDLOG_WARN("[SN2-MaterialHook] enable failed: {}", static_cast<int>(e.error().type));
        return false;
    }
    SPDLOG_INFO("[SN2-MaterialHook] installed at 0x{:x} (RVA 0x{:x})",
        target, sn2_material_hook::SUBNAUTICA2_FBASEPASS_ADDMESHBATCH_RVA);
    if (sn2_addmesh_trace_enabled()) {
        SPDLOG_WARN("[SN2-AddMesh] trace enabled; max_rows={} target_shape_only={}",
            sn2_addmesh_trace_max_rows(),
            sn2_addmesh_trace_target_shape_only() ? 1 : 0);
    }
    if (sn2_target_mesh_discovery_needed() && !sn2_addmesh_trace_enabled()) {
        SPDLOG_WARN("[SN2-AddMesh] target discovery enabled without trace; target-shape probe will run silently");
    }
    if (sn2_addmesh_force_right_enabled()) {
        SPDLOG_WARN("[SN2-AddMeshForce] enabled; max_age={} delay_ms={} max_total={} use_global={} execute_unsafe={}",
            sn2_addmesh_force_right_max_age(),
            sn2_addmesh_force_right_delay_ms(),
            sn2_addmesh_force_right_max_total(),
            sn2_addmesh_force_right_global_enabled() ? 1 : 0,
            sn2_addmesh_force_right_execute_unsafe_enabled() ? 1 : 0);
    }
    return true;
}

static bool install_try_add_mesh_batch_hook() {
    if (!sn2_tryadd_trace_enabled()) {
        SPDLOG_INFO("[SN2-TryAdd] disabled (UEVR_SN2_TRYADD_TRACE not set)");
        return false;
    }

    const auto exe_base = reinterpret_cast<uintptr_t>(utility::get_executable());
    const auto target = exe_base + sn2_material_hook::SUBNAUTICA2_FBASEPASS_TRYADDMESHBATCH_RVA;
    if (exe_base == 0 || !sn2_is_executable_process_range(target, 0x20)) {
        SPDLOG_WARN("[SN2-TryAdd] bad target VA 0x{:x}", target);
        return false;
    }

    // 0x142687030 in build 112084:
    // rex push rbp; push rbx; push rsi; push rdi; push r14; push r15.
    static constexpr uint8_t k_expected_prologue[] = {
        0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x41
    };
    if (std::memcmp(reinterpret_cast<void*>(target), k_expected_prologue, sizeof(k_expected_prologue)) != 0) {
        SPDLOG_WARN("[SN2-TryAdd] prologue mismatch at 0x{:x}; binary likely changed", target);
        return false;
    }

    g_try_add_mesh_batch_hook = safetyhook::create_inline(
        reinterpret_cast<void*>(target),
        reinterpret_cast<void*>(&try_add_mesh_batch_trampoline),
        safetyhook::InlineHook::StartDisabled);
    if (!g_try_add_mesh_batch_hook) {
        SPDLOG_WARN("[SN2-TryAdd] safetyhook create failed at 0x{:x}", target);
        return false;
    }
    if (auto e = g_try_add_mesh_batch_hook.enable(); !e.has_value()) {
        SPDLOG_WARN("[SN2-TryAdd] enable failed: {}", static_cast<int>(e.error().type));
        return false;
    }
    SPDLOG_WARN("[SN2-TryAdd] installed at 0x{:x} (RVA 0x{:x}); max_rows={} target_shape_only={}",
        target,
        sn2_material_hook::SUBNAUTICA2_FBASEPASS_TRYADDMESHBATCH_RVA,
        sn2_tryadd_trace_max_rows(),
        sn2_tryadd_trace_target_shape_only() ? 1 : 0);
    return true;
}

// Original signature:
//   void GenerateDynamicMeshDrawCommands(
//       const FViewInfo& view,
//       EShadingPath shading_path,
//       EMeshPass::Type mesh_pass,
//       FMeshPassProcessor* mesh_pass_processor,
//       const TArray<FMeshBatchAndRelevance>& dynamic_meshes,
//       const TArray<FMeshPassMask>* dynamic_meshes_pass_masks,
//       int32 view_index,
//       const TArray<const FStaticMeshBatch*>& visible_static_meshes,
//       TArray<EMeshDrawCommandCullingPayloadFlags> culling_payload_flags,
//       int32 start_index,
//       TArray<FVisibleMeshDrawCommand>& visible_mesh_draw_commands,
//       FDynamicMeshDrawCommandStorage& storage,
//       TRobinHoodHashSet<...>& minimal_pipeline_state_set,
//       bool& needs_shader_initialization);
//
// The aggregate-by-value TArray is ABI-passed as a pointer-sized argument in
// this build. We do not dereference the later arguments; they are forwarded
// unchanged and exist only to preserve the stack layout for the trampoline.
static void __fastcall generate_dynamic_mesh_draw_commands_trampoline(
    const void* view_ptr,
    int32_t shading_path,
    int32_t mesh_pass,
    void* mesh_pass_processor,
    const void* dynamic_meshes,
    const void* dynamic_meshes_pass_masks,
    int32_t view_index,
    const void* visible_static_meshes,
    const void* culling_payload_flags,
    int32_t start_index,
    void* visible_mesh_draw_commands,
    void* storage,
    void* minimal_pipeline_state_set,
    bool* needs_shader_initialization)
{
    static std::atomic<uint64_t> seq{0};
    const auto n = seq.fetch_add(1, std::memory_order_relaxed) + 1;

    int32_t stereo_pass = -999;
    uint32_t view24c8 = 0;
    uint32_t view24cc = 0;
    int32_t view11ec = -999;
    uint8_t view11d9 = 0xff;
    uint8_t view11f7 = 0xff;
    if (view_ptr != nullptr) {
        const auto view = reinterpret_cast<uintptr_t>(view_ptr);
        sn2_safe_read(reinterpret_cast<const void*>(view + 0xDD0), &stereo_pass, sizeof(stereo_pass));
        sn2_safe_read(reinterpret_cast<const void*>(view + 0x24C8), &view24c8, sizeof(view24c8));
        sn2_safe_read(reinterpret_cast<const void*>(view + 0x24CC), &view24cc, sizeof(view24cc));
        sn2_safe_read(reinterpret_cast<const void*>(view + 0x11EC), &view11ec, sizeof(view11ec));
        sn2_safe_read(reinterpret_cast<const void*>(view + 0x11D9), &view11d9, sizeof(view11d9));
        sn2_safe_read(reinterpret_cast<const void*>(view + 0x11F7), &view11f7, sizeof(view11f7));
    }

    Sn2TArrayHeader dyn = sn2_read_tarray_header(dynamic_meshes);
    Sn2TArrayHeader masks = sn2_read_tarray_header(dynamic_meshes_pass_masks);
    Sn2TArrayHeader static_requests = sn2_read_tarray_header(visible_static_meshes);
    const Sn2TArrayHeader visible_before = sn2_read_tarray_header(visible_mesh_draw_commands);
    const std::string storage_before = sn2_read_qwords_hex(storage, 6);
    uint8_t needs_shader_initialization_before = 0xff;
    if (needs_shader_initialization != nullptr) {
        sn2_safe_read(needs_shader_initialization, &needs_shader_initialization_before, sizeof(needs_shader_initialization_before));
    }
    sn2_scan_view_visibility_state(n, view_ptr, view_index, stereo_pass, mesh_pass);
    const Sn2GenDynTls previous_tls = g_sn2_gendyn_tls;
    g_sn2_gendyn_tls = Sn2GenDynTls{
        true,
        n,
        reinterpret_cast<uintptr_t>(view_ptr),
        view_index,
        stereo_pass,
        shading_path,
        mesh_pass,
        reinterpret_cast<uintptr_t>(mesh_pass_processor),
        dyn,
        masks,
        static_requests,
    };

    if (sn2_gendyn_trace_enabled()) {
        const int pass_filter = sn2_gendyn_trace_pass_filter();
        if (pass_filter < 0 || mesh_pass == pass_filter) {
            const int max_rows = sn2_gendyn_trace_max_rows();
            Sn2DynamicMeshArrayProbe probe = sn2_probe_dynamic_mesh_array(dyn, masks);
            if (static_cast<int>(n) <= max_rows ||
                probe.target_values != 0 ||
                probe.known_mesh_hits != 0 ||
                probe.known_primitive_hits != 0) {
                SPDLOG_WARN(
                    "[SN2-GenDyn] #{} view=0x{:x} view_index={} stereo={} shading={} pass={} "
                    "processor=0x{:x} dyn=0x{:x} dyn_data=0x{:x} dyn_count={} dyn_cap={} masks_data=0x{:x} masks_count={} "
                    "static_data=0x{:x} static_count={} static_cap={} "
                    "target_values={} elems_with_target={} known_mesh_hits={} known_prim_hits={} "
                    "first_elem={} first_off=0x{:x} first_value={} first_mesh=0x{:x} first_prim=0x{:x} first_flags=0x{:x} first_mask=0x{:x} "
                    "first_known_elem={} first_known_mesh=0x{:x} first_known_prim=0x{:x} first_known_flags=0x{:x} first_known_mask=0x{:x} "
                    "view24c8=0x{:08x} view24cc=0x{:08x} v11d9={} v11ec={} v11f7={} "
                    "masks=0x{:x} static=0x{:x} cull=0x{:x} start_index={} out=0x{:x} storage=0x{:x}",
                    n,
                    reinterpret_cast<uintptr_t>(view_ptr),
                    view_index,
                    stereo_pass,
                    shading_path,
                    mesh_pass,
                    reinterpret_cast<uintptr_t>(mesh_pass_processor),
                    reinterpret_cast<uintptr_t>(dynamic_meshes),
                    dyn.data,
                    dyn.count,
                    dyn.capacity,
                    masks.data,
                    masks.count,
                    static_requests.data,
                    static_requests.count,
                    static_requests.capacity,
                    probe.target_values,
                    probe.elems_with_target,
                    probe.known_mesh_hits,
                    probe.known_primitive_hits,
                    probe.first_elem,
                    probe.first_offset,
                    probe.first_value,
                    probe.first_mesh_candidate,
                    probe.first_primitive,
                    probe.first_relevance_flags,
                    probe.first_mask,
                    probe.first_known_elem,
                    probe.first_known_mesh,
                    probe.first_known_primitive,
                    probe.first_known_relevance_flags,
                    probe.first_known_mask,
                    view24c8,
                    view24cc,
                    static_cast<unsigned>(view11d9),
                    view11ec,
                    static_cast<unsigned>(view11f7),
                    reinterpret_cast<uintptr_t>(dynamic_meshes_pass_masks),
                    reinterpret_cast<uintptr_t>(visible_static_meshes),
                    reinterpret_cast<uintptr_t>(culling_payload_flags),
                    start_index,
                    reinterpret_cast<uintptr_t>(visible_mesh_draw_commands),
                    reinterpret_cast<uintptr_t>(storage));
            }
        }
    }

    std::vector<uintptr_t> patched_static_values;
    Sn2TArrayHeader patched_static_header{};
    const void* effective_static_meshes = visible_static_meshes;
    int32_t effective_max_build_request_elements = start_index;
    if (sn2_gendyn_dup_static_right_enabled() && stereo_pass == 2 && mesh_pass == sn2_target_mesh_pass()) {
        static const ULONGLONG start_tick = GetTickCount64();
        static std::atomic<uint64_t> dup_total{0};
        const auto delay_ms = static_cast<ULONGLONG>(sn2_gendyn_dup_static_right_delay_ms());
        const auto target = g_sn2_target_static_mesh_request_ptr.load(std::memory_order_relaxed);
        const auto target_static_mesh_id = g_sn2_target_static_mesh_id.load(std::memory_order_relaxed);
        const auto existing = sn2_find_pointer_array_element(static_requests, target);
        const auto max_total = static_cast<uint64_t>(sn2_gendyn_dup_static_right_max_total());
        const bool under_total_limit =
            max_total == 0 || dup_total.load(std::memory_order_relaxed) < max_total;

        if (GetTickCount64() - start_tick >= delay_ms &&
            under_total_limit &&
            target != 0 &&
            static_requests.valid &&
            static_requests.data != 0 &&
            static_requests.count >= 0 &&
            static_requests.count < 512 &&
            !existing.found) {
            patched_static_values.reserve(static_cast<size_t>(static_requests.count) + 1);
            const auto* src = reinterpret_cast<const uint8_t*>(static_requests.data);
            for (int32_t i = 0; i < static_requests.count; ++i) {
                uintptr_t value = 0;
                if (sn2_safe_read(src + static_cast<uintptr_t>(i) * sizeof(uintptr_t),
                        &value,
                        sizeof(value))) {
                    patched_static_values.push_back(value);
                }
            }
            if (patched_static_values.size() == static_cast<size_t>(static_requests.count)) {
                patched_static_values.push_back(target);
                patched_static_header.data =
                    reinterpret_cast<uintptr_t>(patched_static_values.data());
                patched_static_header.count = static_cast<int32_t>(patched_static_values.size());
                patched_static_header.capacity = patched_static_header.count;
                patched_static_header.valid = true;
                effective_static_meshes = &patched_static_header;
                effective_max_build_request_elements = start_index + 1;
                g_sn2_gendyn_tls.static_mesh_requests = patched_static_header;
                const auto ndup = dup_total.fetch_add(1, std::memory_order_relaxed) + 1;
                SPDLOG_WARN(
                    "[SN2-GenDynDupStaticRight] dup#{} gen#{} view=0x{:x} stereo={} pass={} "
                    "orig_static_data=0x{:x} orig_count={} orig_cap={} patched_data=0x{:x} patched_count={} "
                    "target_static=0x{:x} target_static_mesh_id={} max_build_requests={} -> {}",
                    ndup,
                    n,
                    reinterpret_cast<uintptr_t>(view_ptr),
                    stereo_pass,
                    mesh_pass,
                    static_requests.data,
                    static_requests.count,
                    static_requests.capacity,
                    patched_static_header.data,
                    patched_static_header.count,
                    target,
                    target_static_mesh_id,
                    start_index,
                    effective_max_build_request_elements);
            }
        } else if (target != 0 && !existing.found && static_cast<int>(n) <= sn2_gendyn_trace_max_rows()) {
            SPDLOG_WARN(
                "[SN2-GenDynDupStaticRight] skip gen#{} view=0x{:x} reason=delay_or_limit_or_invalid "
                "elapsed_ms={} delay_ms={} under_limit={} target=0x{:x} static_valid={} static_count={} existing_found={}",
                n,
                reinterpret_cast<uintptr_t>(view_ptr),
                static_cast<unsigned long long>(GetTickCount64() - start_tick),
                delay_ms,
                under_total_limit ? 1 : 0,
                target,
                static_requests.valid ? 1 : 0,
                static_requests.count,
                existing.found ? 1 : 0);
        }
    }

    g_generate_dynamic_mesh_draw_commands_hook.call<void>(
        view_ptr,
        shading_path,
        mesh_pass,
        mesh_pass_processor,
        dynamic_meshes,
        dynamic_meshes_pass_masks,
        view_index,
        effective_static_meshes,
        culling_payload_flags,
        effective_max_build_request_elements,
        visible_mesh_draw_commands,
        storage,
        minimal_pipeline_state_set,
        needs_shader_initialization);

    if (sn2_gendyn_trace_enabled()) {
        const int pass_filter = sn2_gendyn_trace_pass_filter();
        const auto target_static = g_sn2_target_static_mesh_request_ptr.load(std::memory_order_relaxed);
        const Sn2TArrayHeader effective_static = sn2_read_tarray_header(effective_static_meshes);
        const auto effective_target = sn2_find_pointer_array_element(effective_static, target_static);
        const auto effective_shape = sn2_find_target_shape_pointer_array_element(effective_static);
        const Sn2TArrayHeader visible_after = sn2_read_tarray_header(visible_mesh_draw_commands);
        uint8_t needs_shader_initialization_after = 0xff;
        if (needs_shader_initialization != nullptr) {
            sn2_safe_read(needs_shader_initialization, &needs_shader_initialization_after, sizeof(needs_shader_initialization_after));
        }
        const std::string storage_after = sn2_read_qwords_hex(storage, 6);
        const bool should_log_post =
            (pass_filter < 0 || mesh_pass == pass_filter) &&
            (mesh_pass == sn2_target_mesh_pass() ||
             effective_target.found ||
             effective_shape.found ||
             (visible_before.valid && visible_after.valid && visible_after.count != visible_before.count) ||
             static_cast<int>(n) <= sn2_gendyn_trace_max_rows());
        if (should_log_post) {
            SPDLOG_WARN(
                "[SN2-GenDynPost] #{} view=0x{:x} view_index={} stereo={} shading={} pass={} "
                "patched_static={} target_static=0x{:x} eff_static_data=0x{:x} eff_count={} "
                "target_found={} target_elem={} target_hits={} shape_found={} shape_elem={} shape_hits={} shape_value=0x{:x} "
                "out=0x{:x} out_before[data=0x{:x} count={} cap={}] out_after[data=0x{:x} count={} cap={}] delta={} "
                "needs_shader_init {}->{} storage=0x{:x} storage_q_before=[{}] storage_q_after=[{}]",
                n,
                reinterpret_cast<uintptr_t>(view_ptr),
                view_index,
                stereo_pass,
                shading_path,
                mesh_pass,
                patched_static_header.valid ? 1 : 0,
                target_static,
                effective_static.data,
                effective_static.count,
                effective_target.found ? 1 : 0,
                effective_target.elem,
                effective_target.hits,
                effective_shape.found ? 1 : 0,
                effective_shape.elem,
                effective_shape.hits,
                effective_shape.value,
                reinterpret_cast<uintptr_t>(visible_mesh_draw_commands),
                visible_before.data,
                visible_before.count,
                visible_before.capacity,
                visible_after.data,
                visible_after.count,
                visible_after.capacity,
                (visible_after.valid && visible_before.valid) ? (visible_after.count - visible_before.count) : 0,
                static_cast<unsigned>(needs_shader_initialization_before),
                static_cast<unsigned>(needs_shader_initialization_after),
                reinterpret_cast<uintptr_t>(storage),
                storage_before.c_str(),
                storage_after.c_str());
        }
    }
    g_sn2_gendyn_tls = previous_tls;
}

static bool install_generate_dynamic_mesh_draw_commands_hook() {
    if (!sn2_gendyn_trace_enabled()) {
        SPDLOG_INFO("[SN2-GenDyn] disabled (UEVR_SN2_GENDYN_TRACE not set)");
        return false;
    }

    const auto exe_base = reinterpret_cast<uintptr_t>(utility::get_executable());
    const auto target = exe_base + sn2_material_hook::SUBNAUTICA2_GENERATE_DYNAMIC_MESH_DRAW_COMMANDS_RVA;
    if (exe_base == 0 || !sn2_is_executable_process_range(target, 0x20)) {
        SPDLOG_WARN("[SN2-GenDyn] bad target VA 0x{:x}", target);
        return false;
    }

    // Build 112084, GenerateDynamicMeshDrawCommands:
    // mov r11,rsp; push rbx; push r13; sub rsp,188h; ...
    static constexpr uint8_t k_expected_prologue[] = {
        0x4C, 0x8B, 0xDC, 0x53, 0x41, 0x55, 0x48, 0x81
    };
    if (std::memcmp(reinterpret_cast<void*>(target), k_expected_prologue, sizeof(k_expected_prologue)) != 0) {
        SPDLOG_WARN("[SN2-GenDyn] prologue mismatch at 0x{:x}; binary likely changed", target);
        return false;
    }

    g_generate_dynamic_mesh_draw_commands_hook = safetyhook::create_inline(
        reinterpret_cast<void*>(target),
        reinterpret_cast<void*>(&generate_dynamic_mesh_draw_commands_trampoline),
        safetyhook::InlineHook::StartDisabled);
    if (!g_generate_dynamic_mesh_draw_commands_hook) {
        SPDLOG_WARN("[SN2-GenDyn] safetyhook create failed at 0x{:x}", target);
        return false;
    }
    if (auto e = g_generate_dynamic_mesh_draw_commands_hook.enable(); !e.has_value()) {
        SPDLOG_WARN("[SN2-GenDyn] enable failed at 0x{:x}: {}", target, static_cast<int>(e.error().type));
        return false;
    }
    SPDLOG_WARN(
        "[SN2-GenDyn] installed at 0x{:x} (RVA 0x{:x}); pass_filter={} max_rows={} max_elems={} scan_bytes_per_elem={}",
        target,
        sn2_material_hook::SUBNAUTICA2_GENERATE_DYNAMIC_MESH_DRAW_COMMANDS_RVA,
        sn2_gendyn_trace_pass_filter(),
        sn2_gendyn_trace_max_rows(),
        sn2_gendyn_trace_max_elems(),
        sn2_gendyn_trace_scan_bytes_per_elem());
    return true;
}

static bool sn2_copy_viewcommands_tarray_header(uintptr_t dst, uintptr_t src) {
    uintptr_t data = 0;
    int32_t count = 0;
    int32_t capacity = 0;
    if (!sn2_safe_read(reinterpret_cast<const void*>(src), &data, sizeof(data)) ||
        !sn2_safe_read(reinterpret_cast<const void*>(src + 8), &count, sizeof(count)) ||
        !sn2_safe_read(reinterpret_cast<const void*>(src + 12), &capacity, sizeof(capacity))) {
        return false;
    }
    if (count < 0 || count > 100000 || capacity < count || capacity > 100000) {
        return false;
    }
    return sn2_safe_write(reinterpret_cast<void*>(dst), &data, sizeof(data)) &&
           sn2_safe_write(reinterpret_cast<void*>(dst + 8), &count, sizeof(count)) &&
           sn2_safe_write(reinterpret_cast<void*>(dst + 12), &capacity, sizeof(capacity));
}

static uintptr_t sn2_read_viewinfo_ptr(uintptr_t views_data, int32_t view_index) {
    uintptr_t view = 0;
    if (views_data == 0 || view_index < 0) {
        return 0;
    }
    // FVisibilityTaskData stores Views as TArrayView<FViewInfo*>: the data is
    // an array of FViewInfo pointers, not a contiguous FViewInfo buffer.
    if (!sn2_safe_read(
            reinterpret_cast<const void*>(views_data + static_cast<uintptr_t>(view_index) * sizeof(uintptr_t)),
            &view,
            sizeof(view))) {
        return 0;
    }
    return view;
}

static bool sn2_read_viewcommand_num_build(uintptr_t vc, int32_t pass, int32_t& value_out) {
    value_out = 0;
    if (vc == 0 || pass < 0 || pass >= 64) {
        return false;
    }
    return sn2_safe_read(
        reinterpret_cast<const void*>(vc + SN2_FVIEWCOMMANDS_NUM_BUILD_OFF + static_cast<uintptr_t>(pass) * sizeof(int32_t)),
        &value_out,
        sizeof(value_out));
}

static bool sn2_small_nonnegative_int(int32_t v) {
    return v >= 0 && v <= 100000;
}

static uintptr_t sn2_find_num_visible_mesh_elements_offset(uintptr_t primary_view,
                                                           uintptr_t secondary_view,
                                                           uintptr_t primary_vc,
                                                           uintptr_t secondary_vc) {
    if (primary_view == 0 || secondary_view == 0 || primary_vc == 0 || secondary_vc == 0) {
        return 0;
    }

    static constexpr int32_t k_passes[] = {0, 2, 5, 6, 17};
    constexpr size_t k_pass_count = sizeof(k_passes) / sizeof(k_passes[0]);
    int32_t primary_expected[k_pass_count]{};
    int32_t secondary_expected[k_pass_count]{};
    for (size_t i = 0; i < k_pass_count; ++i) {
        if (!sn2_read_viewcommand_num_build(primary_vc, k_passes[i], primary_expected[i]) ||
            !sn2_read_viewcommand_num_build(secondary_vc, k_passes[i], secondary_expected[i])) {
            return 0;
        }
    }

    // Known FViewInfo layout puts NumVisibleDynamicMeshElements before the
    // 0x2070 ParallelMeshDrawCommandPasses pointer array. Scan narrowly and
    // require a multi-pass match for both views to avoid arbitrary int matches.
    for (uintptr_t off = 0x1200; off < 0x2070; off += sizeof(int32_t)) {
        bool match = true;
        for (size_t i = 0; i < k_pass_count; ++i) {
            int32_t pv = -1;
            int32_t sv = -1;
            if (!sn2_safe_read(reinterpret_cast<const void*>(primary_view + off + static_cast<uintptr_t>(k_passes[i]) * sizeof(int32_t)), &pv, sizeof(pv)) ||
                !sn2_safe_read(reinterpret_cast<const void*>(secondary_view + off + static_cast<uintptr_t>(k_passes[i]) * sizeof(int32_t)), &sv, sizeof(sv)) ||
                !sn2_small_nonnegative_int(pv) ||
                !sn2_small_nonnegative_int(sv) ||
                pv != primary_expected[i] ||
                sv != secondary_expected[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            return off;
        }
    }
    return 0;
}

static bool sn2_copy_view_bytes(uintptr_t dst_view, uintptr_t src_view, uintptr_t field_off, size_t size) {
    if (dst_view == 0 || src_view == 0 || size == 0 || size > 64) {
        return false;
    }

    uint8_t bytes[64]{};
    if (!sn2_safe_read(reinterpret_cast<const void*>(src_view + field_off), bytes, size)) {
        return false;
    }
    return sn2_safe_write(reinterpret_cast<void*>(dst_view + field_off), bytes, size);
}

static bool sn2_copy_view_range(uintptr_t dst_view, uintptr_t src_view, uintptr_t first_off, size_t size) {
    if (dst_view == 0 || src_view == 0 || size == 0 || first_off + size < first_off) {
        return false;
    }

    size_t copied = 0;
    while (copied < size) {
        const size_t chunk = (size - copied) > 64 ? 64 : (size - copied);
        if (!sn2_copy_view_bytes(dst_view, src_view, first_off + copied, chunk)) {
            return false;
        }
        copied += chunk;
    }
    return true;
}

struct Sn2PrimitiveIndexChoice {
    int32_t index{-1};
    uintptr_t source_off{0};
    int score{0};
    int prim_vis{-1};
    int relevance_present{0};
    int relevance_nonzero{0};
};

static int sn2_read_fview_bitarray_bit(uintptr_t view, uintptr_t field_off, int32_t bit_index) {
    if (view == 0 || bit_index < 0) {
        return -1;
    }

    uintptr_t words = 0;
    int32_t num_bits = 0;
    int32_t max_bits = 0;
    if (!sn2_safe_read(reinterpret_cast<const void*>(view + field_off), &words, sizeof(words)) ||
        !sn2_safe_read(reinterpret_cast<const void*>(view + field_off + 8), &num_bits, sizeof(num_bits)) ||
        !sn2_safe_read(reinterpret_cast<const void*>(view + field_off + 12), &max_bits, sizeof(max_bits))) {
        return -1;
    }

    if (words < 0x10000 ||
        num_bits <= bit_index ||
        num_bits > 2000000 ||
        max_bits < num_bits ||
        max_bits > 4000000) {
        return -1;
    }

    uint32_t word = 0;
    return sn2_read_bit_from_words(words, bit_index, word);
}

static Sn2PrimitiveIndexChoice sn2_score_primitive_index_for_view(uintptr_t primary_view,
                                                                  int32_t index,
                                                                  uintptr_t source_off) {
    Sn2PrimitiveIndexChoice out{};
    out.index = index;
    out.source_off = source_off;
    if (primary_view == 0 || index < 32 || index >= 2000000) {
        return out;
    }

    static constexpr uintptr_t k_maps[] = {
        SN2_FVIEW_PRIMITIVE_VISIBILITY_MAP_OFF,
        SN2_FVIEW_PRIMITIVE_RAYTRACING_VISIBILITY_MAP_OFF,
        SN2_FVIEW_PRIMITIVE_DEFINITELY_UNOCCLUDED_MAP_OFF,
        SN2_FVIEW_POTENTIALLY_FADING_PRIMITIVE_MAP_OFF,
        SN2_FVIEW_PRIMITIVE_FADE_UNIFORM_BUFFER_MAP_OFF,
    };

    for (size_t i = 0; i < sizeof(k_maps) / sizeof(k_maps[0]); ++i) {
        const int bit = sn2_read_fview_bitarray_bit(primary_view, k_maps[i], index);
        if (i == 0) {
            out.prim_vis = bit;
        }
        if (bit >= 0) {
            out.score += 1;
            if (bit != 0) {
                out.score += (i == 0) ? 16 : 6;
            }
        }
    }

    const Sn2TArrayHeader relevance =
        sn2_read_tarray_header(reinterpret_cast<const void*>(primary_view + SN2_FVIEW_PRIMITIVE_VIEW_RELEVANCE_MAP_OFF));
    if (relevance.valid && relevance.data >= 0x10000 && relevance.count > index) {
        out.relevance_present = 1;
        out.score += 40;
        uint8_t bytes[16]{};
        if (sn2_safe_read(reinterpret_cast<const void*>(relevance.data + static_cast<uintptr_t>(index) * 16), bytes, sizeof(bytes))) {
            for (uint8_t b : bytes) {
                if (b != 0) {
                    out.relevance_nonzero = 1;
                    out.score += 100;
                    break;
                }
            }
        }
    }

    const Sn2TArrayHeader lod =
        sn2_read_tarray_header(reinterpret_cast<const void*>(primary_view + SN2_FVIEW_PRIMITIVES_LOD_MASK_OFF));
    if (lod.valid && lod.data >= 0x10000 && lod.count > index) {
        out.score += 20;
    }

    return out;
}

static Sn2PrimitiveIndexChoice sn2_resolve_target_primitive_index_for_view(uintptr_t primary_view) {
    Sn2PrimitiveIndexChoice best{};

    const auto consider = [&](int32_t value, uintptr_t source_off) {
        const auto score = sn2_score_primitive_index_for_view(primary_view, value, source_off);
        if (score.score > best.score) {
            best = score;
        }
    };

    const int override_index = sn2_target_primitive_index_override();
    if (override_index >= 32 && override_index < 2000000) {
        best = sn2_score_primitive_index_for_view(primary_view, override_index, 0xfffffff0u);
        if (best.score == 0) {
            best.index = override_index;
            best.source_off = 0xfffffff0u;
        }
        g_sn2_target_primitive_index_hint.store(override_index, std::memory_order_relaxed);
        return best;
    }

    const int32_t scene_index = g_sn2_target_scene_primitive_index.load(std::memory_order_relaxed);
    if (scene_index >= 32 && scene_index < 2000000) {
        best = sn2_score_primitive_index_for_view(primary_view, scene_index, 0xffffffe0u);
        if (best.score == 0) {
            best.index = scene_index;
            best.source_off = 0xffffffe0u;
        }
        g_sn2_target_primitive_index_hint.store(scene_index, std::memory_order_relaxed);
        return best;
    }

    const int32_t cached = g_sn2_target_primitive_index_hint.load(std::memory_order_relaxed);
    consider(cached, 0xffffffffu);

    const uintptr_t info = g_sn2_target_primitive_scene_info_ptr.load(std::memory_order_relaxed);
    if (info != 0) {
        // Build 112084 has shown several plausible small integers in
        // PrimitiveSceneInfo-like objects. Do not trust any one offset until
        // it resolves inside the active primary view's visibility/relevance
        // containers.
        static constexpr uintptr_t k_candidate_offsets[] = {
            0x10c, 0x44, 0x124, 0x17c, 0x194
        };

        for (uintptr_t off : k_candidate_offsets) {
            int32_t value = -1;
            if (sn2_safe_read(reinterpret_cast<const void*>(info + off), &value, sizeof(value)) &&
                value >= 32) {
                consider(value, off);
            }
        }

        // Fallback for this diagnostic path: scan the first few cache-line
        // sized regions of the candidate PrimitiveSceneInfo for any plausible
        // packed index that the active view actually contains.
        for (uintptr_t off = 0x40; off < 0x900; off += sizeof(int32_t)) {
            int32_t value = -1;
            if (!sn2_safe_read(reinterpret_cast<const void*>(info + off), &value, sizeof(value))) {
                continue;
            }
            if (value >= 32 && value < 2000000) {
                consider(value, off);
            }
        }
    }

    if (best.score > 0 && best.index >= 32 && best.index < 2000000) {
        g_sn2_target_primitive_index_hint.store(best.index, std::memory_order_relaxed);
        return best;
    }

    best.index = -1;
    return best;
}

struct Sn2BitCopyResult {
    bool ok{false};
    int src_bit{-1};
    int dst_bit_before{-1};
    int dst_bit_after{-1};
};

static Sn2BitCopyResult sn2_copy_bitarray_bit(uintptr_t dst_view,
                                              uintptr_t src_view,
                                              uintptr_t field_off,
                                              int32_t bit_index) {
    Sn2BitCopyResult out{};
    if (dst_view == 0 || src_view == 0 || bit_index < 0) {
        return out;
    }

    uintptr_t src_words = 0;
    uintptr_t dst_words = 0;
    int32_t src_num_bits = 0;
    int32_t src_max_bits = 0;
    int32_t dst_num_bits = 0;
    int32_t dst_max_bits = 0;
    if (!sn2_safe_read(reinterpret_cast<const void*>(src_view + field_off), &src_words, sizeof(src_words)) ||
        !sn2_safe_read(reinterpret_cast<const void*>(src_view + field_off + 8), &src_num_bits, sizeof(src_num_bits)) ||
        !sn2_safe_read(reinterpret_cast<const void*>(src_view + field_off + 12), &src_max_bits, sizeof(src_max_bits)) ||
        !sn2_safe_read(reinterpret_cast<const void*>(dst_view + field_off), &dst_words, sizeof(dst_words)) ||
        !sn2_safe_read(reinterpret_cast<const void*>(dst_view + field_off + 8), &dst_num_bits, sizeof(dst_num_bits)) ||
        !sn2_safe_read(reinterpret_cast<const void*>(dst_view + field_off + 12), &dst_max_bits, sizeof(dst_max_bits))) {
        return out;
    }

    if (src_words < 0x10000 ||
        dst_words < 0x10000 ||
        src_num_bits <= bit_index ||
        dst_num_bits <= bit_index ||
        src_num_bits > 2000000 ||
        dst_num_bits > 2000000 ||
        src_max_bits < src_num_bits ||
        dst_max_bits < dst_num_bits) {
        return out;
    }

    const uintptr_t word_index = static_cast<uintptr_t>(bit_index / 32);
    const uint32_t mask = 1u << (bit_index & 31);
    uint32_t src_word = 0;
    uint32_t dst_word = 0;
    if (!sn2_safe_read(reinterpret_cast<const void*>(src_words + word_index * sizeof(uint32_t)), &src_word, sizeof(src_word)) ||
        !sn2_safe_read(reinterpret_cast<const void*>(dst_words + word_index * sizeof(uint32_t)), &dst_word, sizeof(dst_word))) {
        return out;
    }

    const bool src_set = (src_word & mask) != 0;
    out.src_bit = src_set ? 1 : 0;
    out.dst_bit_before = (dst_word & mask) != 0 ? 1 : 0;
    // For this fix path, never clear right-eye state. The primary task can be
    // from a different view family than the one we want, and clearing a valid
    // secondary bit was visible in logs as accidental state damage.
    const uint32_t dst_after = src_set ? (dst_word | mask) : dst_word;
    if (dst_after != dst_word) {
        if (!sn2_safe_write(reinterpret_cast<void*>(dst_words + word_index * sizeof(uint32_t)), &dst_after, sizeof(dst_after))) {
            return out;
        }
    }

    out.dst_bit_after = (dst_after & mask) != 0 ? 1 : 0;
    out.ok = true;
    return out;
}

static bool sn2_or_tarray_entry(uintptr_t dst_view,
                                uintptr_t src_view,
                                uintptr_t field_off,
                                int32_t elem_index,
                                size_t elem_size) {
    if (dst_view == 0 || src_view == 0 || elem_index < 0 || elem_size == 0 || elem_size > 32) {
        return false;
    }

    const Sn2TArrayHeader src = sn2_read_tarray_header(reinterpret_cast<const void*>(src_view + field_off));
    const Sn2TArrayHeader dst = sn2_read_tarray_header(reinterpret_cast<const void*>(dst_view + field_off));
    if (!src.valid || !dst.valid || src.data < 0x10000 || dst.data < 0x10000 ||
        src.count <= elem_index || dst.count <= elem_index) {
        return false;
    }

    uint8_t src_bytes[32]{};
    uint8_t dst_bytes[32]{};
    const uintptr_t src_entry = src.data + static_cast<uintptr_t>(elem_index) * elem_size;
    const uintptr_t dst_entry = dst.data + static_cast<uintptr_t>(elem_index) * elem_size;
    if (!sn2_safe_read(reinterpret_cast<const void*>(src_entry), src_bytes, elem_size) ||
        !sn2_safe_read(reinterpret_cast<const void*>(dst_entry), dst_bytes, elem_size)) {
        return false;
    }

    for (size_t i = 0; i < elem_size; ++i) {
        dst_bytes[i] = static_cast<uint8_t>(dst_bytes[i] | src_bytes[i]);
    }
    return sn2_safe_write(reinterpret_cast<void*>(dst_entry), dst_bytes, elem_size);
}

static bool sn2_copy_num_visible_dynamic_mesh_elements(uintptr_t dst_view,
                                                       uintptr_t src_view,
                                                       int32_t pass,
                                                       int32_t& src_pass_count,
                                                       int32_t& dst_pass_before,
                                                       int32_t& dst_pass_after) {
    src_pass_count = -1;
    dst_pass_before = -1;
    dst_pass_after = -1;
    if (dst_view == 0 || src_view == 0 || pass < 0 || pass >= 37) {
        return false;
    }

    int32_t counts[37]{};
    if (!sn2_safe_read(reinterpret_cast<const void*>(src_view + SN2_FVIEW_NUM_VISIBLE_DYNAMIC_MESH_ELEMENTS_OFF),
            counts,
            sizeof(counts))) {
        return false;
    }
    sn2_safe_read(
        reinterpret_cast<const void*>(dst_view + SN2_FVIEW_NUM_VISIBLE_DYNAMIC_MESH_ELEMENTS_OFF + static_cast<uintptr_t>(pass) * sizeof(int32_t)),
        &dst_pass_before,
        sizeof(dst_pass_before));
    if (!sn2_safe_write(reinterpret_cast<void*>(dst_view + SN2_FVIEW_NUM_VISIBLE_DYNAMIC_MESH_ELEMENTS_OFF),
            counts,
            sizeof(counts))) {
        return false;
    }
    src_pass_count = counts[pass];
    sn2_safe_read(
        reinterpret_cast<const void*>(dst_view + SN2_FVIEW_NUM_VISIBLE_DYNAMIC_MESH_ELEMENTS_OFF + static_cast<uintptr_t>(pass) * sizeof(int32_t)),
        &dst_pass_after,
        sizeof(dst_pass_after));
    return true;
}

static void sn2_log_fviewinfo_indexed_array_scan(uint64_t seq,
                                                 uintptr_t primary_view,
                                                 uintptr_t secondary_view,
                                                 int32_t primitive_index) {
    if (!sn2_fviewinfo_indexed_array_scan_enabled() ||
        primary_view == 0 ||
        secondary_view == 0 ||
        primitive_index < 0) {
        return;
    }

    static std::atomic<uint64_t> rows{0};
    const auto row = rows.fetch_add(1, std::memory_order_relaxed) + 1;
    if (row > static_cast<uint64_t>(sn2_fviewinfo_indexed_array_scan_max_rows())) {
        return;
    }

    std::string candidates;
    char chunk[512]{};
    uint32_t emitted = 0;
    for (uintptr_t off = 0x1B80; off < 0x2110 && emitted < 18; off += 8) {
        const Sn2TArrayHeader p = sn2_read_tarray_header(reinterpret_cast<const void*>(primary_view + off));
        const Sn2TArrayHeader s = sn2_read_tarray_header(reinterpret_cast<const void*>(secondary_view + off));
        if (!p.valid || !s.valid || p.data < 0x10000 || s.data < 0x10000 ||
            p.count <= primitive_index || s.count <= primitive_index) {
            continue;
        }

        bool any_nonzero = false;
        uint64_t p16a = 0, p16b = 0, s16a = 0, s16b = 0;
        uint32_t p4 = 0, s4 = 0;
        uint16_t p2 = 0, s2 = 0;
        const uintptr_t p16 = p.data + static_cast<uintptr_t>(primitive_index) * 16u;
        const uintptr_t s16 = s.data + static_cast<uintptr_t>(primitive_index) * 16u;

        if (sn2_safe_read(reinterpret_cast<const void*>(p16), &p16a, sizeof(p16a))) {
            sn2_safe_read(reinterpret_cast<const void*>(p16 + sizeof(uint64_t)), &p16b, sizeof(p16b));
            sn2_safe_read(reinterpret_cast<const void*>(s16), &s16a, sizeof(s16a));
            sn2_safe_read(reinterpret_cast<const void*>(s16 + sizeof(uint64_t)), &s16b, sizeof(s16b));
            any_nonzero = any_nonzero || p16a != 0 || p16b != 0 || s16a != 0 || s16b != 0;
        }
        sn2_safe_read(reinterpret_cast<const void*>(p.data + static_cast<uintptr_t>(primitive_index) * 4u), &p4, sizeof(p4));
        sn2_safe_read(reinterpret_cast<const void*>(s.data + static_cast<uintptr_t>(primitive_index) * 4u), &s4, sizeof(s4));
        sn2_safe_read(reinterpret_cast<const void*>(p.data + static_cast<uintptr_t>(primitive_index) * 2u), &p2, sizeof(p2));
        sn2_safe_read(reinterpret_cast<const void*>(s.data + static_cast<uintptr_t>(primitive_index) * 2u), &s2, sizeof(s2));
        any_nonzero = any_nonzero || p4 != 0 || s4 != 0 || p2 != 0 || s2 != 0;
        if (!any_nonzero) {
            continue;
        }

        std::snprintf(
            chunk,
            sizeof(chunk),
            "%soff=0x%llx pc=%d sc=%d p2=0x%04x s2=0x%04x p4=0x%08x s4=0x%08x p16=0x%016llx_%016llx s16=0x%016llx_%016llx",
            candidates.empty() ? "" : " | ",
            static_cast<unsigned long long>(off),
            p.count,
            s.count,
            static_cast<unsigned>(p2),
            static_cast<unsigned>(s2),
            p4,
            s4,
            static_cast<unsigned long long>(p16b),
            static_cast<unsigned long long>(p16a),
            static_cast<unsigned long long>(s16b),
            static_cast<unsigned long long>(s16a));
        candidates += chunk;
        ++emitted;
    }

    if (candidates.empty()) {
        candidates = "none";
    }

    SPDLOG_WARN(
        "[SN2-FViewIndexedArrayScan] row={} trace#{} views[P=0x{:x} S=0x{:x}] primitive_index={} candidates={}",
        row,
        seq,
        primary_view,
        secondary_view,
        primitive_index,
        candidates.c_str());
}

static void sn2_maybe_copy_fviewinfo_bundle_right_13b(uint64_t seq,
                                                      uintptr_t views_data,
                                                      uintptr_t vc_base,
                                                      int32_t vc_count) {
    if ((!sn2_fviewinfo_bundle_copy_right_13b_enabled() &&
         !sn2_fviewinfo_dynamic_range_copy_right_13b_enabled()) ||
        views_data == 0) {
        return;
    }

    // Keep this scoped to frames after the target water draw has been observed.
    // Without the target gate this would globally alias secondary visibility maps.
    const uintptr_t cached_target_static = g_sn2_target_static_mesh_request_ptr.load(std::memory_order_relaxed);
    if (cached_target_static == 0 && g_sn2_target_mesh_ptr.load(std::memory_order_relaxed) == 0) {
        return;
    }
    if (vc_base == 0 || vc_count < 2) {
        return;
    }
    if (!sn2_viewcommands_copy_past_delay()) {
        sn2_log_viewcommands_copy_delay_once();
        return;
    }

    // 2026-05-28 state: pass 17 is definitively TranslucencyAfterDOF in SN2.
    // Live AddMesh/D3D12 traces point at Translucent BasePass for 0x13B00F0C,
    // while SLW can share basepass uniform-buffer layouts. Keep the pass
    // configurable and default to BasePass until draw ownership is proven
    // otherwise.
    const int32_t pass = sn2_underwater_target_mesh_pass();
    const uintptr_t primary_vc = vc_base;
    const uintptr_t secondary_vc = vc_base + SN2_FVIEWCOMMANDS_SIZE;
    const uintptr_t primary_req_addr =
        primary_vc + SN2_FVIEWCOMMANDS_BUILD_REQUESTS_OFF + static_cast<uintptr_t>(pass) * 0x10;
    const uintptr_t secondary_req_addr =
        secondary_vc + SN2_FVIEWCOMMANDS_BUILD_REQUESTS_OFF + static_cast<uintptr_t>(pass) * 0x10;
    const Sn2TArrayHeader primary_req = sn2_read_tarray_header(reinterpret_cast<const void*>(primary_req_addr));
    const Sn2TArrayHeader secondary_req = sn2_read_tarray_header(reinterpret_cast<const void*>(secondary_req_addr));
    Sn2PointerArrayLookup primary_hit = sn2_find_pointer_array_element(primary_req, cached_target_static);
    uintptr_t target_static = cached_target_static;
    bool target_from_shape_scan = false;
    if (!primary_hit.found) {
        const auto shape_hit = sn2_find_target_shape_pointer_array_element(primary_req);
        if (shape_hit.found) {
            primary_hit = shape_hit;
            target_static = shape_hit.value;
            target_from_shape_scan = true;
            g_sn2_target_static_mesh_request_ptr.store(target_static, std::memory_order_relaxed);
        }
    }
    const auto secondary_hit = sn2_find_pointer_array_element(secondary_req, target_static);
    if (!primary_hit.found || secondary_hit.found) {
        sn2_log_viewcommands_copy_debug(
            "FViewBundle",
            seq,
            !primary_hit.found ? "primary_missing_target" : "secondary_already_has_target",
            pass,
            cached_target_static,
            target_static,
            primary_req,
            primary_hit,
            secondary_req,
            secondary_hit);
        return;
    }

    const uintptr_t primary_view = sn2_read_viewinfo_ptr(views_data, 0);
    const uintptr_t secondary_view = sn2_read_viewinfo_ptr(views_data, 1);
    if (primary_view == 0 || secondary_view == 0) {
        return;
    }

    const auto stable_index = sn2_resolve_stable_target_primitive_index(seq);
    if (!stable_index.stable) {
        if (sn2_viewcommands_copy_debug_enabled()) {
            Sn2PointerArrayLookup empty_secondary_hit{};
            sn2_log_viewcommands_copy_debug(
                "FViewBundle",
                seq,
                "primitive_index_not_stable",
                pass,
                cached_target_static,
                target_static,
                primary_req,
                primary_hit,
                secondary_req,
                empty_secondary_hit);
        }
        return;
    }
    Sn2PrimitiveIndexChoice primitive_choice =
        sn2_score_primitive_index_for_view(primary_view, stable_index.index, 0xffffffe1u);
    if (primitive_choice.score == 0) {
        primitive_choice.index = stable_index.index;
        primitive_choice.source_off = 0xffffffe1u;
    }
    const int32_t primitive_index = stable_index.index;

    uint32_t primary_flags = 0;
    uint32_t secondary_flags_before = 0;
    uint32_t secondary_flags_after = 0;
    bool flags_read =
        sn2_safe_read(reinterpret_cast<const void*>(primary_view + SN2_FVIEW_RENDER_FLAGS_DWORD_OFF), &primary_flags, sizeof(primary_flags)) &&
        sn2_safe_read(reinterpret_cast<const void*>(secondary_view + SN2_FVIEW_RENDER_FLAGS_DWORD_OFF), &secondary_flags_before, sizeof(secondary_flags_before));
    bool flags_ok = false;
    if (flags_read) {
        secondary_flags_after =
            secondary_flags_before |
            (primary_flags & SN2_FVIEW_HAS_SLW_MATERIAL_BIT);
        flags_ok = sn2_safe_write(
            reinterpret_cast<void*>(secondary_view + SN2_FVIEW_RENDER_FLAGS_DWORD_OFF),
            &secondary_flags_after,
            sizeof(secondary_flags_after));
    }

    int32_t static_mesh_id = g_sn2_target_static_mesh_id.load(std::memory_order_relaxed);
    if (static_mesh_id <= 0 || static_mesh_id > 2000000) {
        static_mesh_id = 403;
    }

    static constexpr uintptr_t k_primitive_bit_maps[] = {
        SN2_FVIEW_PRIMITIVE_VISIBILITY_MAP_OFF,
        SN2_FVIEW_PRIMITIVE_RAYTRACING_VISIBILITY_MAP_OFF,
        SN2_FVIEW_PRIMITIVE_DEFINITELY_UNOCCLUDED_MAP_OFF,
        SN2_FVIEW_POTENTIALLY_FADING_PRIMITIVE_MAP_OFF,
        SN2_FVIEW_PRIMITIVE_FADE_UNIFORM_BUFFER_MAP_OFF,
    };
    static constexpr uintptr_t k_static_bit_maps[] = {
        SN2_FVIEW_STATIC_MESH_VISIBILITY_MAP_OFF,
        SN2_FVIEW_STATIC_MESH_FADE_OUT_DITHERED_LOD_MAP_OFF,
        SN2_FVIEW_STATIC_MESH_FADE_IN_DITHERED_LOD_MAP_OFF,
    };

    int primitive_bits_ok = 0;
    uint64_t primitive_bits_mask = 0;
    Sn2BitCopyResult primitive_visibility{};
    for (size_t i = 0; i < sizeof(k_primitive_bit_maps) / sizeof(k_primitive_bit_maps[0]); ++i) {
        const Sn2BitCopyResult r = sn2_copy_bitarray_bit(
            secondary_view,
            primary_view,
            k_primitive_bit_maps[i],
            primitive_index);
        if (i == 0) {
            primitive_visibility = r;
        }
        if (r.ok) {
            ++primitive_bits_ok;
            primitive_bits_mask |= (uint64_t{1} << i);
        }
    }

    int static_bits_ok = 0;
    uint64_t static_bits_mask = 0;
    Sn2BitCopyResult static_visibility{};
    for (size_t i = 0; i < sizeof(k_static_bit_maps) / sizeof(k_static_bit_maps[0]); ++i) {
        const Sn2BitCopyResult r = sn2_copy_bitarray_bit(
            secondary_view,
            primary_view,
            k_static_bit_maps[i],
            static_mesh_id);
        if (i == 0) {
            static_visibility = r;
        }
        if (r.ok) {
            ++static_bits_ok;
            static_bits_mask |= (uint64_t{1} << i);
        }
    }

    const bool relevance_ok = sn2_or_tarray_entry(
        secondary_view,
        primary_view,
        SN2_FVIEW_PRIMITIVE_VIEW_RELEVANCE_MAP_OFF,
        primitive_index,
        16);
    const bool lod_ok = sn2_or_tarray_entry(
        secondary_view,
        primary_view,
        SN2_FVIEW_PRIMITIVES_LOD_MASK_OFF,
        primitive_index,
        2);

    const Sn2TArrayEntryProbe rel_primary =
        sn2_probe_tarray_entry(primary_view, SN2_FVIEW_PRIMITIVE_VIEW_RELEVANCE_MAP_OFF, primitive_index, 16);
    const Sn2TArrayEntryProbe rel_secondary =
        sn2_probe_tarray_entry(secondary_view, SN2_FVIEW_PRIMITIVE_VIEW_RELEVANCE_MAP_OFF, primitive_index, 16);
    const Sn2TArrayEntryProbe lod_primary =
        sn2_probe_tarray_entry(primary_view, SN2_FVIEW_PRIMITIVES_LOD_MASK_OFF, primitive_index, 2);
    const Sn2TArrayEntryProbe lod_secondary =
        sn2_probe_tarray_entry(secondary_view, SN2_FVIEW_PRIMITIVES_LOD_MASK_OFF, primitive_index, 2);

    int32_t numvis_src_pass = -1;
    int32_t numvis_dst_before = -1;
    int32_t numvis_dst_after = -1;
    const bool numvisible_counts_ok = sn2_copy_num_visible_dynamic_mesh_elements(
        secondary_view,
        primary_view,
        pass,
        numvis_src_pass,
        numvis_dst_before,
        numvis_dst_after);

    sn2_log_fviewinfo_indexed_array_scan(seq, primary_view, secondary_view, primitive_index);

    constexpr uintptr_t k_dynamic_first = 0x1D70;
    constexpr uintptr_t k_dynamic_limit = 0x2070; // Do not copy ParallelMeshDrawCommandPasses.
    const bool dynamic_range_requested = sn2_fviewinfo_dynamic_range_copy_right_13b_enabled();
    const bool dynamic_range_ok = dynamic_range_requested
        ? sn2_copy_view_range(secondary_view, primary_view, k_dynamic_first, k_dynamic_limit - k_dynamic_first)
        : false;

    static std::atomic<uint64_t> copy_count{0};
    const auto ncopy = copy_count.fetch_add(1, std::memory_order_relaxed) + 1;
    SPDLOG_WARN(
        "[SN2-FViewBundleCopy] copy#{} trace#{} views[P=0x{:x} S=0x{:x}] "
        "target_static=0x{:x} pass={} primary_elem={} primary_hits={} secondary_count={} "
        "target_from_shape_scan={} primitive_index={} packed188={} packed188_match={} stable_streak={} stable_scene_count={} primitive_source_off=0x{:x} primitive_score={} primitive_choice_vis={} primitive_choice_rel={} primitive_choice_rel_nz={} "
        "static_mesh_id={} "
        "primitive_bits={}/{} mask=0x{:x} prim_vis[src={} before={} after={} ok={}] "
        "static_bits={}/{} mask=0x{:x} static_vis[src={} before={} after={} ok={}] "
        "relevance_ok={} lod_ok={} numvisible_counts_ok={} numvis_pass[src={} before={} after={}] "
        "relP[data=0x{:x} count={} cap={} read={} nz={} raw=0x{:016x}_{:016x}] "
        "relS[data=0x{:x} count={} cap={} read={} nz={} raw=0x{:016x}_{:016x}] "
        "lodP[data=0x{:x} count={} cap={} read={} nz={} raw=0x{:016x}] "
        "lodS[data=0x{:x} count={} cap={} read={} nz={} raw=0x{:016x}] "
        "dynamic_range_requested={} dynamic_range_ok={} dynamic_range=0x{:x}..0x{:x} "
        "flags_read={} flags_ok={} primary22d8=0x{:08x} secondary22d8_before=0x{:08x} secondary22d8_after=0x{:08x} slw_bit_before={} slw_bit_after={}",
        ncopy,
        seq,
        primary_view,
        secondary_view,
        target_static,
        pass,
        primary_hit.elem,
        primary_hit.hits,
        secondary_req.count,
        target_from_shape_scan ? 1 : 0,
        primitive_index,
        stable_index.packed_index188,
        stable_index.packed_index188 == primitive_index ? 1 : 0,
        stable_index.streak,
        stable_index.scene_count,
        primitive_choice.source_off,
        primitive_choice.score,
        primitive_choice.prim_vis,
        primitive_choice.relevance_present,
        primitive_choice.relevance_nonzero,
        static_mesh_id,
        primitive_bits_ok,
        static_cast<int>(sizeof(k_primitive_bit_maps) / sizeof(k_primitive_bit_maps[0])),
        primitive_bits_mask,
        primitive_visibility.src_bit,
        primitive_visibility.dst_bit_before,
        primitive_visibility.dst_bit_after,
        primitive_visibility.ok ? 1 : 0,
        static_bits_ok,
        static_cast<int>(sizeof(k_static_bit_maps) / sizeof(k_static_bit_maps[0])),
        static_bits_mask,
        static_visibility.src_bit,
        static_visibility.dst_bit_before,
        static_visibility.dst_bit_after,
        static_visibility.ok ? 1 : 0,
        relevance_ok ? 1 : 0,
        lod_ok ? 1 : 0,
        numvisible_counts_ok ? 1 : 0,
        numvis_src_pass,
        numvis_dst_before,
        numvis_dst_after,
        rel_primary.hdr.data,
        rel_primary.hdr.count,
        rel_primary.hdr.capacity,
        rel_primary.entry_read ? 1 : 0,
        rel_primary.entry_nonzero ? 1 : 0,
        rel_primary.raw1,
        rel_primary.raw0,
        rel_secondary.hdr.data,
        rel_secondary.hdr.count,
        rel_secondary.hdr.capacity,
        rel_secondary.entry_read ? 1 : 0,
        rel_secondary.entry_nonzero ? 1 : 0,
        rel_secondary.raw1,
        rel_secondary.raw0,
        lod_primary.hdr.data,
        lod_primary.hdr.count,
        lod_primary.hdr.capacity,
        lod_primary.entry_read ? 1 : 0,
        lod_primary.entry_nonzero ? 1 : 0,
        lod_primary.raw0,
        lod_secondary.hdr.data,
        lod_secondary.hdr.count,
        lod_secondary.hdr.capacity,
        lod_secondary.entry_read ? 1 : 0,
        lod_secondary.entry_nonzero ? 1 : 0,
        lod_secondary.raw0,
        dynamic_range_requested ? 1 : 0,
        dynamic_range_ok ? 1 : 0,
        k_dynamic_first,
        k_dynamic_limit,
        flags_read ? 1 : 0,
        flags_ok ? 1 : 0,
        primary_flags,
        secondary_flags_before,
        secondary_flags_after,
        (secondary_flags_before & SN2_FVIEW_HAS_SLW_MATERIAL_BIT) != 0 ? 1 : 0,
        (secondary_flags_after & SN2_FVIEW_HAS_SLW_MATERIAL_BIT) != 0 ? 1 : 0);
}

static std::atomic<uintptr_t>& sn2_cached_num_visible_mesh_elements_offset() {
    static std::atomic<uintptr_t> v{0};
    return v;
}

static void sn2_maybe_copy_viewcommands_right_13b(uint64_t seq,
                                                  uintptr_t views_data,
                                                  uintptr_t vc_base,
                                                  int32_t vc_count) {
    if (!sn2_viewcommands_copy_right_13b_enabled() || vc_base == 0 || vc_count < 2) {
        return;
    }

    const uintptr_t cached_target_static = g_sn2_target_static_mesh_request_ptr.load(std::memory_order_relaxed);
    if (cached_target_static == 0 && g_sn2_target_mesh_ptr.load(std::memory_order_relaxed) == 0) {
        return;
    }
    if (!sn2_viewcommands_copy_past_delay()) {
        sn2_log_viewcommands_copy_delay_once();
        return;
    }

    static std::atomic<uint64_t> copy_count{0};
    // See the FViewInfo bundle copy path above: default to BasePass, with env
    // override for shifted SN2 SLW (6) or old wrong-bucket diagnostics.
    const int32_t pass = sn2_underwater_target_mesh_pass();
    const uintptr_t primary_vc = vc_base;
    const uintptr_t secondary_vc = vc_base + SN2_FVIEWCOMMANDS_SIZE;
    const uintptr_t primary_req_addr =
        primary_vc + SN2_FVIEWCOMMANDS_BUILD_REQUESTS_OFF + static_cast<uintptr_t>(pass) * 0x10;
    const uintptr_t secondary_req_addr =
        secondary_vc + SN2_FVIEWCOMMANDS_BUILD_REQUESTS_OFF + static_cast<uintptr_t>(pass) * 0x10;
    const uintptr_t primary_flags_addr =
        primary_vc + SN2_FVIEWCOMMANDS_BUILD_FLAGS_OFF + static_cast<uintptr_t>(pass) * 0x10;
    const uintptr_t secondary_flags_addr =
        secondary_vc + SN2_FVIEWCOMMANDS_BUILD_FLAGS_OFF + static_cast<uintptr_t>(pass) * 0x10;
    const uintptr_t primary_num_addr =
        primary_vc + SN2_FVIEWCOMMANDS_NUM_BUILD_OFF + static_cast<uintptr_t>(pass) * sizeof(int32_t);
    const uintptr_t secondary_num_addr =
        secondary_vc + SN2_FVIEWCOMMANDS_NUM_BUILD_OFF + static_cast<uintptr_t>(pass) * sizeof(int32_t);

    const Sn2TArrayHeader primary_req = sn2_read_tarray_header(reinterpret_cast<const void*>(primary_req_addr));
    const Sn2TArrayHeader secondary_req = sn2_read_tarray_header(reinterpret_cast<const void*>(secondary_req_addr));
    Sn2PointerArrayLookup primary_hit = sn2_find_pointer_array_element(primary_req, cached_target_static);
    uintptr_t target_static = cached_target_static;
    bool target_from_shape_scan = false;
    if (!primary_hit.found) {
        const auto shape_hit = sn2_find_target_shape_pointer_array_element(primary_req);
        if (shape_hit.found) {
            primary_hit = shape_hit;
            target_static = shape_hit.value;
            target_from_shape_scan = true;
            g_sn2_target_static_mesh_request_ptr.store(target_static, std::memory_order_relaxed);
        }
    }
    const auto secondary_hit = sn2_find_pointer_array_element(secondary_req, target_static);
    if (!primary_hit.found || secondary_hit.found) {
        sn2_log_viewcommands_copy_debug(
            "ViewCommands",
            seq,
            !primary_hit.found ? "primary_missing_target" : "secondary_already_has_target",
            pass,
            cached_target_static,
            target_static,
            primary_req,
            primary_hit,
            secondary_req,
            secondary_hit);
        return;
    }

    const uintptr_t primary_view = sn2_read_viewinfo_ptr(views_data, 0);
    const uintptr_t secondary_view = sn2_read_viewinfo_ptr(views_data, 1);
    if (primary_view == 0 || secondary_view == 0) {
        return;
    }

    const auto stable_index = sn2_resolve_stable_target_primitive_index(seq);
    if (!stable_index.stable) {
        if (sn2_viewcommands_copy_debug_enabled()) {
            Sn2PointerArrayLookup empty_secondary_hit{};
            sn2_log_viewcommands_copy_debug(
                "ViewCommands",
                seq,
                "primitive_index_not_stable",
                pass,
                cached_target_static,
                target_static,
                primary_req,
                primary_hit,
                secondary_req,
                empty_secondary_hit);
        }
        return;
    }

    int32_t primary_num = 0;
    sn2_safe_read(reinterpret_cast<const void*>(primary_num_addr), &primary_num, sizeof(primary_num));

    uintptr_t primary_request_value = 0;
    const int32_t replace_elem = secondary_req.count > 0 ? (secondary_req.count - 1) : -1;
    bool req_ok = false;
    if (replace_elem >= 0 &&
        primary_req.data >= 0x10000 &&
        secondary_req.data >= 0x10000 &&
        sn2_safe_read(reinterpret_cast<const void*>(primary_req.data + static_cast<uintptr_t>(primary_hit.elem) * sizeof(uintptr_t)),
            &primary_request_value,
            sizeof(primary_request_value)) &&
        primary_request_value == target_static) {
        req_ok = sn2_safe_write(
            reinterpret_cast<void*>(secondary_req.data + static_cast<uintptr_t>(replace_elem) * sizeof(uintptr_t)),
            &primary_request_value,
            sizeof(primary_request_value));
    }

    const Sn2TArrayHeader primary_build_flags =
        sn2_read_tarray_header(reinterpret_cast<const void*>(primary_flags_addr));
    const Sn2TArrayHeader secondary_build_flags =
        sn2_read_tarray_header(reinterpret_cast<const void*>(secondary_flags_addr));
    uint8_t primary_flag_value = 0;
    bool flags_ok = false;
    if (replace_elem >= 0 &&
        primary_build_flags.valid &&
        secondary_build_flags.valid &&
        primary_build_flags.data >= 0x10000 &&
        secondary_build_flags.data >= 0x10000 &&
        primary_build_flags.count > static_cast<int32_t>(primary_hit.elem) &&
        secondary_build_flags.count > replace_elem &&
        sn2_safe_read(reinterpret_cast<const void*>(primary_build_flags.data + primary_hit.elem),
            &primary_flag_value,
            sizeof(primary_flag_value))) {
        flags_ok = sn2_safe_write(
            reinterpret_cast<void*>(secondary_build_flags.data + replace_elem),
            &primary_flag_value,
            sizeof(primary_flag_value));
    }

    // Keep ownership untouched. Header-copying these arrays aliases primary and
    // secondary ViewCommands and crashes when UE later destroys them.
    const bool num_ok = true;
    uintptr_t numvis_off = SN2_FVIEW_NUM_VISIBLE_DYNAMIC_MESH_ELEMENTS_OFF;
    const uintptr_t numvis_override = sn2_numvisible_offset_override();
    if (numvis_override != 0) {
        numvis_off = numvis_override;
    }
    sn2_cached_num_visible_mesh_elements_offset().store(numvis_off, std::memory_order_relaxed);
    int32_t secondary_numvisible_before = -1;
    int32_t numvisible_src_pass = -1;
    int32_t secondary_numvisible_after = -1;
    bool numvisible_ok = false;
    if (numvis_override == 0) {
        numvisible_ok = sn2_copy_num_visible_dynamic_mesh_elements(
            secondary_view,
            primary_view,
            pass,
            numvisible_src_pass,
            secondary_numvisible_before,
            secondary_numvisible_after);
    } else if (numvis_off != 0) {
        const uintptr_t dst = secondary_view + numvis_off + static_cast<uintptr_t>(pass) * sizeof(int32_t);
        sn2_safe_read(reinterpret_cast<const void*>(dst), &secondary_numvisible_before, sizeof(secondary_numvisible_before));
        numvisible_ok = sn2_safe_write(reinterpret_cast<void*>(dst), &primary_num, sizeof(primary_num));
        numvisible_src_pass = primary_num;
        sn2_safe_read(reinterpret_cast<const void*>(dst), &secondary_numvisible_after, sizeof(secondary_numvisible_after));
    }
    const auto ncopy = copy_count.fetch_add(1, std::memory_order_relaxed) + 1;
    SPDLOG_WARN(
        "[SN2-ViewCommandsCopy] copy#{} trace#{} pass={} target_static=0x{:x} "
        "primary_req[data=0x{:x} count={} elem={} hits={}] "
        "secondary_req_before[data=0x{:x} count={} replace_elem={}] replaced_req={} copied_flag={} copied_num={} primary_num={} "
        "views[P=0x{:x} S=0x{:x}] target_from_shape_scan={} stable_index={} packed188={} packed188_match={} stable_streak={} stable_scene_count={} "
        "numvis_off=0x{:x} numvis_override=0x{:x} numvis_src_pass={} secondary_numvis_before={} secondary_numvis_after={} copied_numvis={}",
        ncopy,
        seq,
        pass,
        target_static,
        primary_req.data,
        primary_req.count,
        primary_hit.elem,
        primary_hit.hits,
        secondary_req.data,
        secondary_req.count,
        replace_elem,
        req_ok ? 1 : 0,
        flags_ok ? 1 : 0,
        num_ok ? 1 : 0,
        primary_num,
        primary_view,
        secondary_view,
        target_from_shape_scan ? 1 : 0,
        stable_index.index,
        stable_index.packed_index188,
        stable_index.packed_index188 == stable_index.index ? 1 : 0,
        stable_index.streak,
        stable_index.scene_count,
        numvis_off,
        numvis_override,
        numvisible_src_pass,
        secondary_numvisible_before,
        secondary_numvisible_after,
        numvisible_ok ? 1 : 0);
}

static void sn2_maybe_alias_slw_pass_ptr(uint64_t seq, uintptr_t views_data) {
    if (!sn2_alias_slw_pass_ptr_enabled()) {
        return;
    }
    if (!sn2_viewcommands_copy_past_delay()) {
        sn2_log_viewcommands_copy_delay_once();
        return;
    }

    const uintptr_t primary_view = sn2_read_viewinfo_ptr(views_data, 0);
    const uintptr_t secondary_view = sn2_read_viewinfo_ptr(views_data, 1);
    if (primary_view == 0 || secondary_view == 0) {
        return;
    }

    static constexpr uintptr_t k_pmdcp_base_off = 0x2070;
    const int configured_slot = sn2_alias_pmdcp_slot();
    const int alias_slot = (configured_slot >= 0 && configured_slot < 64) ? configured_slot : 6;
    const uintptr_t slw_pass_ptr_off = k_pmdcp_base_off + static_cast<uintptr_t>(alias_slot) * sizeof(uintptr_t);
    uintptr_t primary_slw = 0;
    uintptr_t secondary_before = 0;
    if (!sn2_safe_read(reinterpret_cast<const void*>(primary_view + slw_pass_ptr_off),
            &primary_slw,
            sizeof(primary_slw)) ||
        !sn2_safe_read(reinterpret_cast<const void*>(secondary_view + slw_pass_ptr_off),
            &secondary_before,
            sizeof(secondary_before))) {
        return;
    }

    if (primary_slw == 0) {
        static std::atomic<uint64_t> null_logs{0};
        const auto n = null_logs.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 16 || (n % 300) == 0) {
            SPDLOG_WARN(
                "[SN2-AliasSLWPassPtr] skipped#{} trace#{} primary SLW pass ptr is null "
                "slot={} off=0x{:x} views[P=0x{:x} S=0x{:x}] secondary_before=0x{:x}",
                n,
                seq,
                alias_slot,
                slw_pass_ptr_off,
                primary_view,
                secondary_view,
                secondary_before);
        }
        return;
    }

    const bool write_ok = sn2_safe_write(
        reinterpret_cast<void*>(secondary_view + slw_pass_ptr_off),
        &primary_slw,
        sizeof(primary_slw));
    uintptr_t secondary_after = 0;
    sn2_safe_read(reinterpret_cast<const void*>(secondary_view + slw_pass_ptr_off),
        &secondary_after,
        sizeof(secondary_after));

    uint32_t primary24c8 = 0;
    uint32_t secondary24c8 = 0;
    uint32_t primary22d8 = 0;
    uint32_t secondary22d8 = 0;
    sn2_safe_read(reinterpret_cast<const void*>(primary_view + 0x24C8), &primary24c8, sizeof(primary24c8));
    sn2_safe_read(reinterpret_cast<const void*>(secondary_view + 0x24C8), &secondary24c8, sizeof(secondary24c8));
    sn2_safe_read(reinterpret_cast<const void*>(primary_view + SN2_FVIEW_RENDER_FLAGS_DWORD_OFF), &primary22d8, sizeof(primary22d8));
    sn2_safe_read(reinterpret_cast<const void*>(secondary_view + SN2_FVIEW_RENDER_FLAGS_DWORD_OFF), &secondary22d8, sizeof(secondary22d8));

    static std::atomic<uint64_t> alias_count{0};
    const auto n = alias_count.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 64 || (n % 300) == 0) {
        SPDLOG_WARN(
            "[SN2-AliasSLWPassPtr] alias#{} trace#{} write_ok={} "
            "slot={} off=0x{:x} views[P=0x{:x} S=0x{:x}] slw_ptr[P=0x{:x} S_before=0x{:x} S_after=0x{:x}] "
            "v24c8[P=0x{:08x} S=0x{:08x}] v22d8[P=0x{:08x} S=0x{:08x}]",
            n,
            seq,
            write_ok ? 1 : 0,
            alias_slot,
            slw_pass_ptr_off,
            primary_view,
            secondary_view,
            primary_slw,
            secondary_before,
            secondary_after,
            primary24c8,
            secondary24c8,
            primary22d8,
            secondary22d8);
    }
}

static void finish_gather_viewcommands_mid(safetyhook::Context& ctx) {
    const bool trace_enabled = sn2_viewcommands_trace_enabled();
    const bool meshcmd_bruteforce_enabled = sn2_meshcmd_bruteforce_enabled();
    const bool copy_enabled =
        sn2_alias_slw_pass_ptr_enabled() ||
        sn2_meshcmd_copy_right_13b_enabled() ||
        sn2_viewcommands_copy_right_13b_enabled() ||
        sn2_fviewinfo_bundle_copy_right_13b_enabled() ||
        sn2_fviewinfo_dynamic_range_copy_right_13b_enabled();
    if (!trace_enabled && !meshcmd_bruteforce_enabled && !copy_enabled) {
        return;
    }

    static std::atomic<uint64_t> seq{0};
    const auto n = seq.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool emit_trace =
        trace_enabled && n <= static_cast<uint64_t>(sn2_viewcommands_trace_max_rows());

    const uintptr_t task = ctx.rbp;
    if (task == 0) {
        return;
    }

    uintptr_t views_data = 0;
    int32_t view_count = 0;
    uintptr_t task_scene = 0;
    uintptr_t vc_data = 0;
    int32_t vc_count = 0;
    int32_t vc_capacity = 0;
    uint8_t setup_flag_22a4 = 0xff;

    sn2_safe_read(reinterpret_cast<const void*>(task + SN2_VIS_TASK_SCENE_OFF), &task_scene, sizeof(task_scene));
    sn2_safe_read(reinterpret_cast<const void*>(task + SN2_VIS_TASK_VIEWS_DATA_OFF), &views_data, sizeof(views_data));
    sn2_safe_read(reinterpret_cast<const void*>(task + SN2_VIS_TASK_VIEWS_COUNT_OFF), &view_count, sizeof(view_count));
    sn2_safe_read(reinterpret_cast<const void*>(task + SN2_VIS_TASK_VIEWCOMMANDS_DATA_OFF), &vc_data, sizeof(vc_data));
    sn2_safe_read(reinterpret_cast<const void*>(task + SN2_VIS_TASK_VIEWCOMMANDS_COUNT_OFF), &vc_count, sizeof(vc_count));
    sn2_safe_read(reinterpret_cast<const void*>(task + SN2_VIS_TASK_VIEWCOMMANDS_COUNT_OFF + sizeof(int32_t)), &vc_capacity, sizeof(vc_capacity));
    sn2_safe_read(reinterpret_cast<const void*>(task + 0x22A4), &setup_flag_22a4, sizeof(setup_flag_22a4));

    if (task_scene >= 0x10000 &&
        sn2_is_readable_process_range(task_scene + SN2_FSCENE_PRIMITIVES_DIA_OFF, sizeof(Sn2TArrayHeader))) {
        g_sn2_visibility_task_scene_ptr.store(task_scene, std::memory_order_relaxed);
    }

    const uintptr_t vc_base = vc_data != 0 ? vc_data : (task + SN2_VIS_TASK_VIEWCOMMANDS_INLINE_OFF);
    const auto target_static = g_sn2_target_static_mesh_request_ptr.load(std::memory_order_relaxed);
    const auto target_mesh = g_sn2_target_mesh_ptr.load(std::memory_order_relaxed);
    const auto target_prim = g_sn2_target_primitive_ptr.load(std::memory_order_relaxed);
    const auto target_static_mesh_id = g_sn2_target_static_mesh_id.load(std::memory_order_relaxed);
    const auto target_prim_info = g_sn2_target_primitive_scene_info_ptr.load(std::memory_order_relaxed);
    const auto target_prim_idx = g_sn2_target_primitive_index_hint.load(std::memory_order_relaxed);

    if (emit_trace) {
        SPDLOG_WARN(
            "[SN2-ViewCommands] #{} task=0x{:x} task_scene=0x{:x} views_data=0x{:x} view_count={} "
            "vc_data=0x{:x} vc_base=0x{:x} vc_count={} vc_cap={} flag22A4={} "
            "target_mesh=0x{:x} target_prim=0x{:x} target_static=0x{:x} target_static_mesh_id={} "
            "target_prim_info=0x{:x} target_prim_idx={}",
            n,
            task,
            task_scene,
            views_data,
            view_count,
            vc_data,
            vc_base,
            vc_count,
            vc_capacity,
            static_cast<unsigned>(setup_flag_22a4),
            target_mesh,
            target_prim,
            target_static,
            target_static_mesh_id,
            target_prim_info,
            target_prim_idx);
    }

    if (views_data == 0 || view_count <= 0 || view_count > 8 || vc_count <= 0 || vc_count > 8) {
        return;
    }

    // Gate ALL secondary-view mutations behind VR-ready + throttle + attempt-cap
    // (see sn2_secondary_mutation_gate_ok): never fire before OpenXR is fully
    // open, and never scan every frame (was tanking to ~0.1 fps).
    if (sn2_secondary_mutation_gate_ok(n)) {
        sn2_maybe_copy_fviewinfo_bundle_right_13b(n, views_data, vc_base, vc_count);
        sn2_maybe_copy_viewcommands_right_13b(n, views_data, vc_base, vc_count);
        sn2_maybe_copy_meshcommand_right_13b(n, vc_base, vc_count);
        sn2_maybe_alias_slw_pass_ptr(n, views_data);
    }

    static constexpr int32_t k_passes[] = {0, 2, 5, 6, 17};
    const int32_t views_to_log = view_count < vc_count ? view_count : vc_count;
    for (int32_t view_index = 0; view_index < views_to_log; ++view_index) {
        const uintptr_t view = sn2_read_viewinfo_ptr(views_data, view_index);
        const uintptr_t vc = vc_base + static_cast<uintptr_t>(view_index) * SN2_FVIEWCOMMANDS_SIZE;
        int32_t stereo_pass = -999;
        uint32_t view24c8 = 0;
        uint8_t view11f7 = 0xff;
        uintptr_t slw_pass_ptr = 0;
        sn2_safe_read(reinterpret_cast<const void*>(view + 0xDD0), &stereo_pass, sizeof(stereo_pass));
        sn2_safe_read(reinterpret_cast<const void*>(view + 0x24C8), &view24c8, sizeof(view24c8));
        sn2_safe_read(reinterpret_cast<const void*>(view + 0x11F7), &view11f7, sizeof(view11f7));
        sn2_safe_read(reinterpret_cast<const void*>(view + 0x2098), &slw_pass_ptr, sizeof(slw_pass_ptr));

        for (int32_t pass : k_passes) {
            Sn2TArrayHeader mesh_commands = sn2_read_tarray_header(
                reinterpret_cast<const void*>(vc + SN2_FVIEWCOMMANDS_MESHCOMMANDS_OFF + static_cast<uintptr_t>(pass) * 0x10));
            Sn2TArrayHeader build_requests = sn2_read_tarray_header(
                reinterpret_cast<const void*>(vc + SN2_FVIEWCOMMANDS_BUILD_REQUESTS_OFF + static_cast<uintptr_t>(pass) * 0x10));
            Sn2TArrayHeader build_flags = sn2_read_tarray_header(
                reinterpret_cast<const void*>(vc + SN2_FVIEWCOMMANDS_BUILD_FLAGS_OFF + static_cast<uintptr_t>(pass) * 0x10));
            int32_t num_build_request_elements = 0;
            sn2_safe_read(
                reinterpret_cast<const void*>(vc + SN2_FVIEWCOMMANDS_NUM_BUILD_OFF + static_cast<uintptr_t>(pass) * sizeof(int32_t)),
                &num_build_request_elements,
                sizeof(num_build_request_elements));

            const auto target_lookup = sn2_find_pointer_array_element(build_requests, target_static);
            const auto mesh_shape_lookup = sn2_find_mesh_commands_target_shape(mesh_commands);
            sn2_log_meshcommands_bruteforce_if_enabled(
                n,
                view_index,
                stereo_pass,
                pass,
                mesh_commands);
            sn2_runtime_state::ViewCommandsLast vc_live{};
            vc_live.view_slot = view_index;
            vc_live.stereo_pass = stereo_pass;
            vc_live.mesh_pass = pass;
            vc_live.view = view;
            vc_live.view_commands = vc;
            vc_live.mesh_commands_count = mesh_commands.count;
            vc_live.build_request_count = build_requests.count;
            vc_live.mesh_shape_hit = mesh_shape_lookup.found;
            vc_live.mesh_hits = mesh_shape_lookup.hits;
            vc_live.mesh_draw_command = mesh_shape_lookup.mesh_draw_command;
            vc_live.max_num_primitives = mesh_shape_lookup.max_num_primitives;
            vc_live.max_elem = mesh_shape_lookup.max_elem;
            vc_live.max_cmd = mesh_shape_lookup.max_cmd;
            sn2_runtime_state::note_viewcommands(vc_live);
            if (emit_trace) {
                SPDLOG_WARN(
                    "[SN2-ViewCommandsPass] #{} view_slot={} view=0x{:x} stereo={} vc=0x{:x} "
                    "pass={}({}) mesh_cmds[data=0x{:x} count={} cap={} valid={}] "
                    "num_build_elems={} req[data=0x{:x} count={} cap={} valid={}] "
                    "flags[data=0x{:x} count={} cap={} valid={}] "
                    "target_static_hit={} elem={} hits={} "
                    "mesh_shape_hit={} mesh_elem={} mesh_hits={} visible=0x{:x} cmd=0x{:x} first_index={} num_prim={} num_inst={} prim_type={} "
                    "visible_target={}@0x{:x} cmd_target={}@0x{:x} cmd_ptrs={} cmd_reads={} max_prim={} max_elem={} max_cmd=0x{:x} "
                    "state_bucket={} prim_id_buf_off={} cull_flags=0x{:x} visible_flags=0x{:x} sort=0x{:x} run_array=0x{:x} num_runs={} "
                    "slw_pass=0x{:x} v11f7={} v24c8=0x{:08x}",
                    n,
                    view_index,
                    view,
                    stereo_pass,
                    vc,
                    pass,
                    sn2_mesh_pass_name(pass),
                    mesh_commands.data,
                    mesh_commands.count,
                    mesh_commands.capacity,
                    mesh_commands.valid ? 1 : 0,
                    num_build_request_elements,
                    build_requests.data,
                    build_requests.count,
                    build_requests.capacity,
                    build_requests.valid ? 1 : 0,
                    build_flags.data,
                    build_flags.count,
                    build_flags.capacity,
                    build_flags.valid ? 1 : 0,
                    target_lookup.found ? 1 : 0,
                    target_lookup.elem,
                    target_lookup.hits,
                    mesh_shape_lookup.found ? 1 : 0,
                    mesh_shape_lookup.elem,
                    mesh_shape_lookup.hits,
                    mesh_shape_lookup.visible_elem,
                    mesh_shape_lookup.mesh_draw_command,
                    mesh_shape_lookup.first_index,
                    mesh_shape_lookup.num_primitives,
                    mesh_shape_lookup.num_instances,
                    static_cast<unsigned>(mesh_shape_lookup.primitive_type),
                    mesh_shape_lookup.visible_target_value,
                    mesh_shape_lookup.visible_target_off,
                    mesh_shape_lookup.cmd_target_value,
                    mesh_shape_lookup.cmd_target_off,
                    mesh_shape_lookup.cmd_ptr_nonzero,
                    mesh_shape_lookup.cmd_read_ok,
                    mesh_shape_lookup.max_num_primitives,
                    mesh_shape_lookup.max_elem,
                    mesh_shape_lookup.max_cmd,
                    mesh_shape_lookup.state_bucket_id,
                    mesh_shape_lookup.primitive_id_buffer_offset,
                    mesh_shape_lookup.culling_payload_flags,
                    mesh_shape_lookup.visible_flags,
                    mesh_shape_lookup.sort_key,
                    mesh_shape_lookup.run_array,
                    mesh_shape_lookup.num_runs,
                    slw_pass_ptr,
                    static_cast<unsigned>(view11f7),
                    view24c8);
            }
        }
    }
}

static bool install_finish_gather_viewcommands_hook() {
    if (!sn2_viewcommands_trace_enabled() &&
        !sn2_meshcmd_bruteforce_enabled() &&
        !sn2_meshcmd_copy_right_13b_enabled() &&
        !sn2_alias_slw_pass_ptr_enabled() &&
        !sn2_viewcommands_copy_right_13b_enabled() &&
        !sn2_fviewinfo_bundle_copy_right_13b_enabled() &&
        !sn2_fviewinfo_dynamic_range_copy_right_13b_enabled()) {
        SPDLOG_INFO("[SN2-ViewCommands] disabled (UEVR_SN2_VIEWCOMMANDS_TRACE/MESHCMD_BRUTE_SCAN/MESHCMD_COPY_RIGHT_13B/ALIAS_SLW_PASS_PTR/COPY_RIGHT_13B/FVIEWINFO_BUNDLE_COPY_RIGHT_13B/FVIEWINFO_DYNAMIC_RANGE_COPY_RIGHT_13B not set)");
        return false;
    }

    const auto exe_base = reinterpret_cast<uintptr_t>(utility::get_executable());
    const auto target = exe_base + sn2_material_hook::SUBNAUTICA2_FINISH_GATHER_VIEWCOMMANDS_PRE_SETUP_RVA;
    if (exe_base == 0 || !sn2_is_executable_process_range(target, 0x10)) {
        SPDLOG_WARN("[SN2-ViewCommands] bad target VA 0x{:x}", target);
        return false;
    }

    // Build 112084: movzx r8d, byte ptr [rbp+22A4h].
    static constexpr uint8_t k_expected_bytes[] = {
        0x44, 0x0F, 0xB6, 0x85, 0xA4, 0x22, 0x00, 0x00
    };
    if (std::memcmp(reinterpret_cast<void*>(target), k_expected_bytes, sizeof(k_expected_bytes)) != 0) {
        SPDLOG_WARN("[SN2-ViewCommands] byte mismatch at 0x{:x}; binary likely changed", target);
        return false;
    }

    g_finish_gather_viewcommands_hook = safetyhook::create_mid(
        reinterpret_cast<void*>(target),
        &finish_gather_viewcommands_mid);
    if (!g_finish_gather_viewcommands_hook) {
        SPDLOG_WARN("[SN2-ViewCommands] safetyhook create_mid failed at 0x{:x}", target);
        return false;
    }

    SPDLOG_WARN(
        "[SN2-ViewCommands] installed mid-hook at 0x{:x} (RVA 0x{:x}); max_rows={} passes=0,2,5,6,17",
        target,
        sn2_material_hook::SUBNAUTICA2_FINISH_GATHER_VIEWCOMMANDS_PRE_SETUP_RVA,
        sn2_viewcommands_trace_max_rows());
    return true;
}

// ============================================================================
// Sn2VsmUbClampPatch: 3-byte binary patch on FVirtualShadowMapArray::GetUniformBuffer
// ============================================================================
//
// Replaces the per-view-index clamp (cmovl eax, edx) with (xor eax, eax; nop)
// so the function always returns CachedUniformBuffers[0]. See the header for
// the rationale and patch coordinates.

static uint8_t g_vsm_ub_clamp_saved_bytes[3]{};
static bool g_vsm_ub_clamp_applied = false;
static uintptr_t g_vsm_ub_clamp_target = 0;

static bool install_vsm_ub_clamp_patch() {
    if (!sn2_vsm_ub_clamp_patch::env_enabled()) {
        SPDLOG_INFO("[SN2-VsmUbClamp] disabled (UEVR_SN2_VSM_UB_CLAMP_PATCH not set)");
        return false;
    }

    const auto exe_base = reinterpret_cast<uintptr_t>(utility::get_executable());
    if (exe_base == 0) {
        SPDLOG_WARN("[SN2-VsmUbClamp] could not resolve executable base");
        return false;
    }

    const auto fn_target = exe_base + sn2_vsm_ub_clamp_patch::SUBNAUTICA2_GETUNIFORMBUFFER_RVA;
    const auto patch_target = exe_base + sn2_vsm_ub_clamp_patch::SUBNAUTICA2_GETUNIFORMBUFFER_CMOVL_RVA;

    if (!sn2_is_executable_process_range(fn_target, sizeof(sn2_vsm_ub_clamp_patch::k_expected_function_prologue))) {
        SPDLOG_WARN("[SN2-VsmUbClamp] function address 0x{:x} not executable; skipping", fn_target);
        return false;
    }

    if (std::memcmp(reinterpret_cast<void*>(fn_target),
                    sn2_vsm_ub_clamp_patch::k_expected_function_prologue,
                    sizeof(sn2_vsm_ub_clamp_patch::k_expected_function_prologue)) != 0) {
        SPDLOG_WARN("[SN2-VsmUbClamp] function prologue mismatch at 0x{:x}; binary likely changed, skipping", fn_target);
        return false;
    }

    if (std::memcmp(reinterpret_cast<void*>(patch_target),
                    sn2_vsm_ub_clamp_patch::k_original_bytes,
                    sizeof(sn2_vsm_ub_clamp_patch::k_original_bytes)) != 0) {
        SPDLOG_WARN("[SN2-VsmUbClamp] patch site bytes mismatch at 0x{:x}; binary likely changed, skipping", patch_target);
        return false;
    }

    std::memcpy(g_vsm_ub_clamp_saved_bytes,
                reinterpret_cast<const void*>(patch_target),
                sizeof(g_vsm_ub_clamp_saved_bytes));

    DWORD old_protect = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(patch_target),
                        sizeof(sn2_vsm_ub_clamp_patch::k_patched_bytes),
                        PAGE_EXECUTE_READWRITE,
                        &old_protect)) {
        SPDLOG_WARN("[SN2-VsmUbClamp] VirtualProtect RWX failed at 0x{:x}", patch_target);
        return false;
    }

    std::memcpy(reinterpret_cast<void*>(patch_target),
                sn2_vsm_ub_clamp_patch::k_patched_bytes,
                sizeof(sn2_vsm_ub_clamp_patch::k_patched_bytes));

    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<void*>(patch_target),
                   sizeof(sn2_vsm_ub_clamp_patch::k_patched_bytes),
                   old_protect,
                   &ignored);
    FlushInstructionCache(GetCurrentProcess(),
                          reinterpret_cast<void*>(patch_target),
                          sizeof(sn2_vsm_ub_clamp_patch::k_patched_bytes));

    g_vsm_ub_clamp_applied = true;
    g_vsm_ub_clamp_target = patch_target;
    SPDLOG_WARN(
        "[SN2-VsmUbClamp] applied 3-byte patch at 0x{:x} (RVA 0x{:x}); GetUniformBuffer now always returns CachedUniformBuffers[0]",
        patch_target,
        sn2_vsm_ub_clamp_patch::SUBNAUTICA2_GETUNIFORMBUFFER_CMOVL_RVA);
    return true;
}

static void revert_vsm_ub_clamp_patch() {
    if (!g_vsm_ub_clamp_applied || g_vsm_ub_clamp_target == 0) return;

    DWORD old_protect = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(g_vsm_ub_clamp_target),
                        sizeof(g_vsm_ub_clamp_saved_bytes),
                        PAGE_EXECUTE_READWRITE,
                        &old_protect)) {
        SPDLOG_WARN("[SN2-VsmUbClamp] revert VirtualProtect failed at 0x{:x}", g_vsm_ub_clamp_target);
        return;
    }
    std::memcpy(reinterpret_cast<void*>(g_vsm_ub_clamp_target),
                g_vsm_ub_clamp_saved_bytes,
                sizeof(g_vsm_ub_clamp_saved_bytes));
    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<void*>(g_vsm_ub_clamp_target),
                   sizeof(g_vsm_ub_clamp_saved_bytes),
                   old_protect,
                   &ignored);
    FlushInstructionCache(GetCurrentProcess(),
                          reinterpret_cast<void*>(g_vsm_ub_clamp_target),
                          sizeof(g_vsm_ub_clamp_saved_bytes));
    g_vsm_ub_clamp_applied = false;
    SPDLOG_INFO("[SN2-VsmUbClamp] reverted patch at 0x{:x}", g_vsm_ub_clamp_target);
}

// ============================================================================
// Sn2 underwater per-view pass probe (U-A1, 2026-05-26)
// ============================================================================
// Hook sub_1426F9B40 (RVA 0x26F9B40): the per-view UWE underwater-fog/integration
// loop. It walks FSceneRenderer.Views and gates each view on FViewInfo+0x11EC
// (int) and FViewInfo+0x1200 (float >0). Consumer-replay of 0x13b00f0c is dead,
// so the real divergence is here. This OBSERVER logs, per view, those two fields
// (+ the shared scene-water bool) so we can see which one gates the right view
// out. No mutation. Gated by UEVR_SN2_UNDERWATER_PASS_PROBE=1.
//
// FSceneRenderer layout (from the decompile): scene=*(u64*)(this+0x08),
// view array base=*(u64*)(this+0x10), count=*(i32*)(this+0x18), stride 0x29D0.
// Water obj = *(u64*)(scene+0x45D8); water bool at +0xA7.
inline constexpr uint64_t SUBNAUTICA2_UWE_UNDERWATER_PASS_RVA = 0x26F9B40;
inline constexpr uint64_t SN2_VIEW_STRIDE   = 0x29D0;
inline constexpr uint64_t SN2_VIEW_F11EC    = 0x11EC;  // int gate
inline constexpr uint64_t SN2_VIEW_F1200    = 0x1200;  // float >0 gate
static SafetyHookInline g_uwe_underwater_pass_hook{};

static bool sn2_uwe_probe_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_UNDERWATER_PASS_PROBE");
        return v && v[0] && v[0] != '0';
    }();
    return e;
}

static bool sn2_safe_read(const void* p, void* out, size_t n) {
    if (p == nullptr) return false;
    __try {
        std::memcpy(out, p, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static char __fastcall uwe_underwater_pass_trampoline(void* this_ptr, void* a2, void* a3, uint32_t a4) {
    if (sn2_uwe_probe_enabled() && this_ptr != nullptr) {
        static std::atomic<uint64_t> seq{0};
        const auto n = seq.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 24 || (n % 600) == 0) {
            uint64_t base = 0; int32_t count = 0; uint64_t scene = 0;
            const auto* t = reinterpret_cast<const uint8_t*>(this_ptr);
            sn2_safe_read(t + 0x10, &base, sizeof(base));
            sn2_safe_read(t + 0x18, &count, sizeof(count));
            sn2_safe_read(t + 0x08, &scene, sizeof(scene));
            int water_bool = -1;
            if (scene != 0) {
                uint64_t water_obj = 0;
                if (sn2_safe_read(reinterpret_cast<const void*>(scene + 0x45D8), &water_obj, sizeof(water_obj)) && water_obj != 0) {
                    uint8_t wb = 0;
                    if (sn2_safe_read(reinterpret_cast<const void*>(water_obj + 0xA7), &wb, sizeof(wb))) water_bool = wb;
                }
            }
            SPDLOG_WARN("[SN2-UWEProbe] #{} this=0x{:x} views_base=0x{:x} count={} sceneWater[0xA7]={}",
                n, reinterpret_cast<uintptr_t>(this_ptr), base, count, water_bool);
            if (base != 0 && count > 0 && count <= 8) {
                for (int i = 0; i < count; ++i) {
                    const uint64_t view = base + static_cast<uint64_t>(i) * SN2_VIEW_STRIDE;
                    int32_t f11ec = -1; float f1200 = -999.0f;
                    sn2_safe_read(reinterpret_cast<const void*>(view + SN2_VIEW_F11EC), &f11ec, sizeof(f11ec));
                    sn2_safe_read(reinterpret_cast<const void*>(view + SN2_VIEW_F1200), &f1200, sizeof(f1200));
                    SPDLOG_WARN("[SN2-UWEProbe]   view[{}] addr=0x{:x} +0x11EC={} +0x1200={:.4f}  (gate: 11EC==0||water={}, 1200>0={})",
                        i, view, f11ec, f1200,
                        (f11ec == 0 || water_bool > 0) ? 1 : 0,
                        (f1200 > 0.0f) ? 1 : 0);
                }
            }
        }
    }
    return g_uwe_underwater_pass_hook.call<char>(this_ptr, a2, a3, a4);
}

static bool install_uwe_underwater_pass_probe() {
    if (!sn2_uwe_probe_enabled()) {
        return false;
    }
    const auto exe_base = reinterpret_cast<uintptr_t>(utility::get_executable());
    const auto target = exe_base + SUBNAUTICA2_UWE_UNDERWATER_PASS_RVA;
    if (exe_base == 0 || !sn2_is_executable_process_range(target, 0x20)) {
        SPDLOG_WARN("[SN2-UWEProbe] target 0x{:x} not in executable range; not installed", target);
        return false;
    }
    g_uwe_underwater_pass_hook = safetyhook::create_inline(
        reinterpret_cast<void*>(target),
        reinterpret_cast<void*>(&uwe_underwater_pass_trampoline),
        safetyhook::InlineHook::StartDisabled);
    if (!g_uwe_underwater_pass_hook) {
        SPDLOG_WARN("[SN2-UWEProbe] safetyhook create failed at 0x{:x}", target);
        return false;
    }
    if (auto e = g_uwe_underwater_pass_hook.enable(); !e.has_value()) {
        SPDLOG_WARN("[SN2-UWEProbe] enable failed at 0x{:x}", target);
        return false;
    }
    SPDLOG_WARN("[SN2-UWEProbe] installed at 0x{:x} (RVA 0x{:x})",
        target, SUBNAUTICA2_UWE_UNDERWATER_PASS_RVA);
    return true;
}

// ============================================================================
// Sn2 fog history-slot allocator probe (2026-05-26)
// ============================================================================
// Mid-hook on the fog temporal-reprojection history slot allocator
// sub_142D941CB (RVA 0x2D941CB). The history is a scene-level indexed array at
// FScene+0x2E30 (*(FScene + 8*slot + 0x2E30) = a history-entry pointer; only 2
// slots used). The allocator iterates candidate objects and assigns the
// winning candidate to slot v30 with the store:
//     mov [r13 + r14*8 + 0x2E30], rax     ; RVA 0x2D9424E
// where rax = selected candidate object (v39), r14 = slot index (v30). v30=r14
// and v27=rbp are *inherited* registers (non-standard calling convention), so a
// plain fastcall entry trampoline cannot read the slot. We must use a
// safetyhook MID-hook at the store so we can read the full register Context.
//
// This OBSERVER logs, per selection store: the slot index (r14), the selected
// candidate object pointer (rax = v39), the derived v40 = *(rax+0x28), and the
// per-candidate slotKey/flags (v40+0x1DF / v40+0x1DB) that the allocator gates
// on. Comparing the selected obj / v40 *per slot across frames* makes a
// two-eye collision on the same history slot visible (both eyes' candidates
// winning the same slot index, or the same obj ping-ponging between slots).
// No mutation. Gated by UEVR_SN2_FOG_SLOT_PROBE=1.
//
// Offsets (verified against the decompile sub_142D941CB__0x142d941cb.c):
//   v40     = *(rax + 40)      = *(rax + 0x28)
//   slotKey = *(uint8_t*)(v40 + 479)  = *(uint8_t*)(v40 + 0x1DF)
//   flags   = *(uint8_t*)(v40 + 475)  = *(uint8_t*)(v40 + 0x1DB)
inline constexpr uint64_t SUBNAUTICA2_FOG_SLOT_ALLOC_RVA   = 0x2D941CB;  // function start (sanity)
inline constexpr uint64_t SUBNAUTICA2_FOG_SLOT_STORE_RVA   = 0x2D9424E;  // mov [r13+r14*8+2E30h], rax
inline constexpr uint64_t SN2_FOGSLOT_V40_OFF              = 0x28;       // *(rax+40)
inline constexpr uint64_t SN2_FOGSLOT_SLOTKEY_OFF          = 0x1DF;      // *(v40+479)
inline constexpr uint64_t SN2_FOGSLOT_FLAGS_OFF            = 0x1DB;      // *(v40+475)
static SafetyHookMid g_fog_slot_probe_hook{};

static bool sn2_fog_slot_probe_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_FOG_SLOT_PROBE");
        return v && v[0] && v[0] != '0';
    }();
    return e;
}

static void fog_slot_probe_mid(safetyhook::Context& ctx) {
    if (!sn2_fog_slot_probe_enabled()) {
        return;
    }
    static std::atomic<uint64_t> seq{0};
    const auto n = seq.fetch_add(1, std::memory_order_relaxed) + 1;
    // Rate limit: first 24 stores in full, then every 600th.
    if (!(n <= 24 || (n % 600) == 0)) {
        return;
    }

    // r14 = slot index (v30), rax = selected candidate object (v39).
    const uint64_t slot = ctx.r14;
    const uint64_t obj  = ctx.rax;

    uint64_t v40 = 0;
    int slot_key = -1;
    int flags1db = -1;
    if (obj != 0 && sn2_safe_read(reinterpret_cast<const void*>(obj + SN2_FOGSLOT_V40_OFF), &v40, sizeof(v40)) && v40 != 0) {
        uint8_t sk = 0, fl = 0;
        if (sn2_safe_read(reinterpret_cast<const void*>(v40 + SN2_FOGSLOT_SLOTKEY_OFF), &sk, sizeof(sk))) slot_key = sk;
        if (sn2_safe_read(reinterpret_cast<const void*>(v40 + SN2_FOGSLOT_FLAGS_OFF), &fl, sizeof(fl))) flags1db = fl;
    }

    SPDLOG_WARN(
        "[SN2-FogSlot] #{} slot={} obj=0x{:x} v40=0x{:x} slotKey={} flags1DB=0x{:x}",
        n, slot, obj, v40, slot_key, flags1db);
}

static bool install_fog_slot_probe() {
    if (!sn2_fog_slot_probe_enabled()) {
        return false;
    }
    const auto exe_base = reinterpret_cast<uintptr_t>(utility::get_executable());
    if (exe_base == 0) {
        SPDLOG_WARN("[SN2-FogSlot] could not resolve executable base; not installed");
        return false;
    }

    // Sanity: log a few bytes at the function start. We don't have a verified
    // prologue for this build, so log-and-proceed (don't hard-fail), but DO
    // require the store target VA to be in an executable range.
    const auto fn_start = exe_base + SUBNAUTICA2_FOG_SLOT_ALLOC_RVA;
    if (sn2_is_executable_process_range(fn_start, 0x10)) {
        uint8_t pro[8] = {};
        if (sn2_safe_read(reinterpret_cast<const void*>(fn_start), pro, sizeof(pro))) {
            SPDLOG_WARN(
                "[SN2-FogSlot] fn start 0x{:x} (RVA 0x{:x}) prologue: {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}",
                fn_start, SUBNAUTICA2_FOG_SLOT_ALLOC_RVA,
                pro[0], pro[1], pro[2], pro[3], pro[4], pro[5], pro[6], pro[7]);
        }
    } else {
        SPDLOG_WARN("[SN2-FogSlot] fn start 0x{:x} not in executable range (proceeding to store check)", fn_start);
    }

    const auto target = exe_base + SUBNAUTICA2_FOG_SLOT_STORE_RVA;
    if (!sn2_is_executable_process_range(target, 0x10)) {
        SPDLOG_WARN("[SN2-FogSlot] store target 0x{:x} not in executable range; not installed", target);
        return false;
    }

    g_fog_slot_probe_hook = safetyhook::create_mid(
        reinterpret_cast<void*>(target),
        &fog_slot_probe_mid);
    if (!g_fog_slot_probe_hook) {
        SPDLOG_WARN("[SN2-FogSlot] safetyhook create_mid failed at 0x{:x}", target);
        return false;
    }
    SPDLOG_WARN("[SN2-FogSlot] installed mid-hook at 0x{:x} (RVA 0x{:x}); reading r14=slot, rax=obj",
        target, SUBNAUTICA2_FOG_SLOT_STORE_RVA);
    return true;
}

// ============================================================================
// MediaVolume per-view PROBE + FIX (2026-05-29). ROOT CAUSE (binary-grounded,
// workflow w4qlw2nad): the SN2 right/secondary view's FUWESceneViewState
// .MediaVolumeCount (+0x5C) is 0 (its per-view populate never ran) ->
// InsideMediaVolume(WorldPos) returns false -> the UWE water/fog material takes
// the above-water/SkyAtmosphere branch -> right eye shows black sky+tan sand,
// not teal. The MediaVolume cluster data is WORLD-SPACE (view-independent), so
// reusing the primary view's buffers at the secondary eye is parallax-correct
// (parallax comes from per-pixel WorldPos in the View UB, not these buffers).
//
// Hook the MediaVolume shader-param binder (RVA 0x2F496F0): rcx = FUWESceneViewState,
// a FViewInfo passed alongside (identify by StereoPass @ view+0xDD0). PROBE reads
// MediaVolumeCount per eye (confirm right=0/left>0). FIX copies the primary's
// populated MediaVolume fields into the secondary's FUWESceneViewState so the
// right eye's OWN 0x13B00F0C SLW pass samples a non-empty medium -> renders its
// OWN teal with its OWN parallax. No-op in interiors (primary count==0).
// FUWESceneViewState layout (offsets, HIGH confidence): +0x10 MediaVolumeBuffer,
// +0x18 UniqueBuffer, +0x20 ClusterRangeBuffer, +0x28/+0x30/+0x38 their SRVs,
// +0x54 buf elem-cap, +0x58 uniq elem-cap, +0x5C MediaVolumeCount.
// ============================================================================
inline constexpr uint64_t SUBNAUTICA2_MEDIAVOLUME_BINDER_RVA = 0x2F496F0;
inline constexpr uintptr_t SN2_UWESVS_MV_BUF_OFF      = 0x10;
inline constexpr uintptr_t SN2_UWESVS_MV_UNIQ_OFF     = 0x18;
inline constexpr uintptr_t SN2_UWESVS_MV_RANGE_OFF    = 0x20;
inline constexpr uintptr_t SN2_UWESVS_MV_BUFSRV_OFF   = 0x28;
inline constexpr uintptr_t SN2_UWESVS_MV_UNIQSRV_OFF  = 0x30;
inline constexpr uintptr_t SN2_UWESVS_MV_RANGESRV_OFF = 0x38;
inline constexpr uintptr_t SN2_UWESVS_MV_BUFCAP_OFF   = 0x54;
inline constexpr uintptr_t SN2_UWESVS_MV_UNIQCAP_OFF  = 0x58;
inline constexpr uintptr_t SN2_UWESVS_MV_COUNT_OFF    = 0x5C;
inline constexpr uintptr_t SN2_UWE_SCENEVIEW_STEREO_PASS_OFF = 0xDD0;

static SafetyHookMid g_mediavolume_binder_hook{};

static bool sn2_mediavolume_probe_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_MEDIAVOLUME_PROBE");
        return v && v[0] && v[0] != '0';
    }();
    return e;
}
static bool sn2_mediavolume_copy_right_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_MEDIAVOLUME_COPY_RIGHT");
        return v && v[0] && v[0] != '0';
    }();
    return e;
}

struct Sn2MediaVolumeSnapshot {
    uintptr_t buf, uniq, range, bufSRV, uniqSRV, rangeSRV;
    int32_t count, bufCap, uniqCap;
};
static Sn2MediaVolumeSnapshot g_sn2_mv_primary{};
static std::atomic<bool> g_sn2_mv_primary_valid{false};

// Find the FViewInfo among the candidate registers by a valid StereoPass (1/2).
static uintptr_t sn2_mv_identify_view(std::initializer_list<uintptr_t> cands, uint32_t* out_stereo) {
    for (uintptr_t v : cands) {
        if (v < 0x10000) continue;
        uint32_t sp = 0xffffffffu;
        if (sn2_safe_read(reinterpret_cast<const void*>(v + SN2_UWE_SCENEVIEW_STEREO_PASS_OFF), &sp, sizeof(sp)) &&
            (sp == 1u || sp == 2u)) {
            if (out_stereo) *out_stereo = sp;
            return v;
        }
    }
    return 0;
}

static void mediavolume_binder_mid(safetyhook::Context& ctx) {
    if (!sn2_mediavolume_probe_enabled() && !sn2_mediavolume_copy_right_enabled()) {
        return;
    }
    const uintptr_t uwe = ctx.rcx;
    if (uwe < 0x10000 || !sn2_is_readable_process_range(uwe + 0x60, 8)) {
        return;
    }
    uint32_t stereo = 0xffffffffu;
    const uintptr_t view = sn2_mv_identify_view({ctx.rdx, ctx.r8, ctx.r9, ctx.r13, ctx.rsi, ctx.rdi, ctx.r12, ctx.r15}, &stereo);

    int32_t count = -1, bufcap = -1, uniqcap = -1;
    uintptr_t buf = 0, uniq = 0, range = 0;
    sn2_safe_read(reinterpret_cast<const void*>(uwe + SN2_UWESVS_MV_COUNT_OFF), &count, sizeof(count));
    sn2_safe_read(reinterpret_cast<const void*>(uwe + SN2_UWESVS_MV_BUFCAP_OFF), &bufcap, sizeof(bufcap));
    sn2_safe_read(reinterpret_cast<const void*>(uwe + SN2_UWESVS_MV_UNIQCAP_OFF), &uniqcap, sizeof(uniqcap));
    sn2_safe_read(reinterpret_cast<const void*>(uwe + SN2_UWESVS_MV_BUF_OFF), &buf, sizeof(buf));
    sn2_safe_read(reinterpret_cast<const void*>(uwe + SN2_UWESVS_MV_UNIQ_OFF), &uniq, sizeof(uniq));
    sn2_safe_read(reinterpret_cast<const void*>(uwe + SN2_UWESVS_MV_RANGE_OFF), &range, sizeof(range));

    // Primary snapshot (stereo==1, populated): capture for the secondary copy.
    if (stereo == 1u && count > 0) {
        Sn2MediaVolumeSnapshot s{};
        s.buf = buf; s.uniq = uniq; s.range = range;
        s.count = count; s.bufCap = bufcap; s.uniqCap = uniqcap;
        sn2_safe_read(reinterpret_cast<const void*>(uwe + SN2_UWESVS_MV_BUFSRV_OFF), &s.bufSRV, sizeof(s.bufSRV));
        sn2_safe_read(reinterpret_cast<const void*>(uwe + SN2_UWESVS_MV_UNIQSRV_OFF), &s.uniqSRV, sizeof(s.uniqSRV));
        sn2_safe_read(reinterpret_cast<const void*>(uwe + SN2_UWESVS_MV_RANGESRV_OFF), &s.rangeSRV, sizeof(s.rangeSRV));
        g_sn2_mv_primary = s;
        g_sn2_mv_primary_valid.store(true, std::memory_order_release);
    }

    // FIX: secondary (stereo==2) with empty media -> copy primary's world-space
    // buffers so the right eye's own water/fog draw samples a real medium.
    bool wrote = false;
    if (sn2_mediavolume_copy_right_enabled() && stereo == 2u && count == 0 &&
        g_sn2_mv_primary_valid.load(std::memory_order_acquire) && uwe != g_sn2_mv_primary.buf) {
        const Sn2MediaVolumeSnapshot s = g_sn2_mv_primary;
        if (s.buf >= 0x10000 && s.count > 0) {
            // Write buffers/SRVs/caps first, MediaVolumeCount last (so count never
            // exceeds the bound extents mid-write). Raw pointer copy (no addref):
            // the primary's RDG/pooled refs are alive for this frame's render.
            bool ok = true;
            ok &= sn2_safe_write(reinterpret_cast<void*>(uwe + SN2_UWESVS_MV_BUF_OFF), &s.buf, sizeof(s.buf));
            ok &= sn2_safe_write(reinterpret_cast<void*>(uwe + SN2_UWESVS_MV_UNIQ_OFF), &s.uniq, sizeof(s.uniq));
            ok &= sn2_safe_write(reinterpret_cast<void*>(uwe + SN2_UWESVS_MV_RANGE_OFF), &s.range, sizeof(s.range));
            ok &= sn2_safe_write(reinterpret_cast<void*>(uwe + SN2_UWESVS_MV_BUFSRV_OFF), &s.bufSRV, sizeof(s.bufSRV));
            ok &= sn2_safe_write(reinterpret_cast<void*>(uwe + SN2_UWESVS_MV_UNIQSRV_OFF), &s.uniqSRV, sizeof(s.uniqSRV));
            ok &= sn2_safe_write(reinterpret_cast<void*>(uwe + SN2_UWESVS_MV_RANGESRV_OFF), &s.rangeSRV, sizeof(s.rangeSRV));
            ok &= sn2_safe_write(reinterpret_cast<void*>(uwe + SN2_UWESVS_MV_BUFCAP_OFF), &s.bufCap, sizeof(s.bufCap));
            ok &= sn2_safe_write(reinterpret_cast<void*>(uwe + SN2_UWESVS_MV_UNIQCAP_OFF), &s.uniqCap, sizeof(s.uniqCap));
            ok &= sn2_safe_write(reinterpret_cast<void*>(uwe + SN2_UWESVS_MV_COUNT_OFF), &s.count, sizeof(s.count));
            wrote = ok;
        }
    }

    static std::atomic<uint64_t> seq{0};
    const auto n = seq.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 48 || (n % 600) == 0) {
        SPDLOG_WARN(
            "[SN2-MediaVolProbe] #{} uwe=0x{:x} view=0x{:x} stereo={} count(+5C)={} bufcap(+54)={} uniqcap(+58)={} "
            "buf=0x{:x} uniq=0x{:x} range=0x{:x} primary_valid={} wrote={}",
            n, uwe, view, static_cast<int>(stereo), count, bufcap, uniqcap,
            buf, uniq, range, g_sn2_mv_primary_valid.load(std::memory_order_relaxed) ? 1 : 0, wrote ? 1 : 0);
    }
}

static bool install_mediavolume_binder_hook() {
    if (!sn2_mediavolume_probe_enabled() && !sn2_mediavolume_copy_right_enabled()) {
        SPDLOG_INFO("[SN2-MediaVolProbe] disabled (UEVR_SN2_MEDIAVOLUME_PROBE / _COPY_RIGHT not set)");
        return false;
    }
    const auto exe_base = reinterpret_cast<uintptr_t>(utility::get_executable());
    const auto target = exe_base + SUBNAUTICA2_MEDIAVOLUME_BINDER_RVA;
    if (exe_base == 0 || !sn2_is_executable_process_range(target, 0x10)) {
        SPDLOG_WARN("[SN2-MediaVolProbe] bad target VA 0x{:x}; not installed", target);
        return false;
    }
    // Verified prologue (w4qlw2nad): mov [rsp+18],rbx; push rbp/rsi/rdi/r12/r13/r14/r15.
    static constexpr uint8_t k_expected[] = {
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57
    };
    uint8_t pro[sizeof(k_expected)]{};
    if (sn2_safe_read(reinterpret_cast<const void*>(target), pro, sizeof(pro)) &&
        std::memcmp(pro, k_expected, sizeof(k_expected)) != 0) {
        SPDLOG_WARN("[SN2-MediaVolProbe] prologue mismatch at 0x{:x} (got {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}); binary changed; not installed",
            target, pro[0], pro[1], pro[2], pro[3], pro[4], pro[5], pro[6], pro[7]);
        return false;
    }
    g_mediavolume_binder_hook = safetyhook::create_mid(reinterpret_cast<void*>(target), &mediavolume_binder_mid);
    if (!g_mediavolume_binder_hook) {
        SPDLOG_WARN("[SN2-MediaVolProbe] safetyhook create_mid failed at 0x{:x}", target);
        return false;
    }
    SPDLOG_WARN("[SN2-MediaVolProbe] installed mid-hook at 0x{:x} (RVA 0x{:x}); probe={} copy_right={}",
        target, SUBNAUTICA2_MEDIAVOLUME_BINDER_RVA,
        sn2_mediavolume_probe_enabled() ? 1 : 0, sn2_mediavolume_copy_right_enabled() ? 1 : 0);
    return true;
}

// ============================================================================
// FMeshDrawCommand::SubmitDrawEnd TLS bridge (2026-05-28)
// ============================================================================
//
// The final underwater teal draw reaches D3D12 as ps_crc=0x13B00F0C from
// Subnautica2+0x309e6b5, but the D3D12 command-list hook cannot see the UE
// FMeshDrawCommand that issued it. Hook SubmitDrawEnd, stash the active command
// in thread-local state, and let the D3D12 draw hook consume it during the
// engine's RHICmdList.DrawIndexedPrimitive call.
inline constexpr uint64_t SUBNAUTICA2_MESHDRAWCMD_SUBMIT_DRAW_END_RVA = 0x2AB9180;
static SafetyHookInline g_submit_draw_end_hook{};

static bool sn2_submitdraw_trace_enabled() {
    static const bool e =
        sn2_env_flag("UEVR_SN2_SUBMITDRAW_TRACE") ||
        sn2_env_flag("UEVR_SN2_13B_DRAW_PROBE");
    return e;
}

static bool sn2_submitdraw_trace_stack_enabled() {
    static const bool e = sn2_env_flag("UEVR_SN2_SUBMITDRAW_TRACE_STACK");
    return e;
}

static std::string sn2_capture_game_stack_rvas() {
    std::string out;
    void* frames[48]{};
    const USHORT got = RtlCaptureStackBackTrace(0, 48, frames, nullptr);
    const uintptr_t exe_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    char buf[24]{};
    for (USHORT i = 0; i < got; ++i) {
        const uintptr_t f = reinterpret_cast<uintptr_t>(frames[i]);
        if (f >= exe_base && f < exe_base + 0x10000000ull) {
            std::snprintf(
                buf,
                sizeof(buf),
                "%s0x%llx",
                out.empty() ? "" : ",",
                static_cast<unsigned long long>(f - exe_base));
            out += buf;
        }
    }
    return out.empty() ? std::string{"off"} : out;
}

static void __fastcall submit_draw_end_trampoline(
    const void* mesh_draw_command,
    const void* scene_args,
    uint32_t instance_factor,
    void* rhi_cmd_list)
{
    sn2_runtime_state::SubmitDrawTls current{};
    current.active = true;
    current.cmd = reinterpret_cast<uintptr_t>(mesh_draw_command);
    current.scene_args = reinterpret_cast<uintptr_t>(scene_args);
    current.rhi = reinterpret_cast<uintptr_t>(rhi_cmd_list);
    current.instance_factor = instance_factor;

    if (mesh_draw_command != nullptr) {
        const auto* cmd = reinterpret_cast<const uint8_t*>(mesh_draw_command);
        uint8_t primitive_stream = 0xff;
        sn2_safe_read(cmd + 0xE0, &current.index_buffer, sizeof(current.index_buffer));
        sn2_safe_read(cmd + 0xE8, &current.cached_pipeline_id, sizeof(current.cached_pipeline_id));
        sn2_safe_read(cmd + 0xF0, &current.first_index, sizeof(current.first_index));
        sn2_safe_read(cmd + 0xF4, &current.num_primitives, sizeof(current.num_primitives));
        sn2_safe_read(cmd + 0xF8, &current.num_instances, sizeof(current.num_instances));
        if (sn2_safe_read(cmd + 0x110, &primitive_stream, sizeof(primitive_stream))) {
            current.primitive_id_stream_index = primitive_stream == 0xff
                ? -1
                : static_cast<int32_t>(primitive_stream);
        }
    }
    if (scene_args != nullptr) {
        const auto* args = reinterpret_cast<const uint8_t*>(scene_args);
        sn2_safe_read(args + 0x00, &current.primitive_ids_buffer, sizeof(current.primitive_ids_buffer));
        sn2_safe_read(args + 0x08, &current.indirect_args_buffer, sizeof(current.indirect_args_buffer));
        sn2_safe_read(args + 0x10, &current.primitive_id_offset, sizeof(current.primitive_id_offset));
        sn2_safe_read(args + 0x14, &current.indirect_args_byte_offset, sizeof(current.indirect_args_byte_offset));
        sn2_safe_read(args + 0x18, &current.batched_primitive_slot, sizeof(current.batched_primitive_slot));
    }

    const bool target_shape = current.num_primitives == 25536;
    if (target_shape && sn2_submitdraw_trace_enabled()) {
        static std::atomic<uint64_t> target_seq{0};
        const auto n = target_seq.fetch_add(1, std::memory_order_relaxed) + 1;
        const int max_rows = (std::max)(1, sn2_env_int("UEVR_SN2_SUBMITDRAW_TRACE_MAX", 48));
        const bool log_row = n <= static_cast<uint64_t>(max_rows) || (n % 600) == 0;

        const std::string stack = sn2_submitdraw_trace_stack_enabled() ? sn2_capture_game_stack_rvas() : "off";
        sn2_runtime_state::SubmitDrawLast row{};
        row.cmd = current.cmd;
        row.scene_args = current.scene_args;
        row.rhi = current.rhi;
        row.instance_factor = current.instance_factor;
        row.first_index = current.first_index;
        row.num_primitives = current.num_primitives;
        row.num_instances = current.num_instances;
        row.index_buffer = current.index_buffer;
        row.cached_pipeline_id = current.cached_pipeline_id;
        row.primitive_id_stream_index = current.primitive_id_stream_index;
        row.primitive_id_offset = current.primitive_id_offset;
        row.indirect_args_byte_offset = current.indirect_args_byte_offset;
        row.batched_primitive_slot = current.batched_primitive_slot;
        row.primitive_ids_buffer = current.primitive_ids_buffer;
        row.indirect_args_buffer = current.indirect_args_buffer;
        row.stack = stack;
        sn2_runtime_state::note_submitdraw_target(std::move(row));

        if (log_row) {
            SPDLOG_WARN(
                "[SN2-SubmitDrawEnd] #{} cmd=0x{:x} scene_args=0x{:x} rhi=0x{:x} "
                "first={} prims={} inst={} instFactor={} indexBuf=0x{:x} pipeId=0x{:x} "
                "primStream={} primIdOffset={} primIdsBuf=0x{:x} indirectBuf=0x{:x} indirectOff={} batchedSlot={} stack={}",
                n,
                current.cmd,
                current.scene_args,
                current.rhi,
                current.first_index,
                current.num_primitives,
                current.num_instances,
                current.instance_factor,
                current.index_buffer,
                current.cached_pipeline_id,
                current.primitive_id_stream_index,
                current.primitive_id_offset,
                current.primitive_ids_buffer,
                current.indirect_args_buffer,
                current.indirect_args_byte_offset,
                current.batched_primitive_slot,
                stack);
        }
    }

    const auto previous = sn2_runtime_state::tls_submit_draw;
    sn2_runtime_state::tls_submit_draw = current;
    g_submit_draw_end_hook.call<void>(mesh_draw_command, scene_args, instance_factor, rhi_cmd_list);
    sn2_runtime_state::tls_submit_draw = previous;
}

static bool install_submit_draw_end_probe() {
    if (!sn2_submitdraw_trace_enabled()) {
        return false;
    }
    const auto exe_base = reinterpret_cast<uintptr_t>(utility::get_executable());
    const auto target = exe_base + SUBNAUTICA2_MESHDRAWCMD_SUBMIT_DRAW_END_RVA;
    if (exe_base == 0 || !sn2_is_executable_process_range(target, 0x20)) {
        SPDLOG_WARN("[SN2-SubmitDrawEnd] target 0x{:x} not in executable range; not installed", target);
        return false;
    }

    uint8_t pro[12]{};
    if (sn2_safe_read(reinterpret_cast<const void*>(target), pro, sizeof(pro))) {
        SPDLOG_WARN(
            "[SN2-SubmitDrawEnd] target 0x{:x} prologue: {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}",
            target,
            pro[0], pro[1], pro[2], pro[3], pro[4], pro[5],
            pro[6], pro[7], pro[8], pro[9], pro[10], pro[11]);
    }

    g_submit_draw_end_hook = safetyhook::create_inline(
        reinterpret_cast<void*>(target),
        reinterpret_cast<void*>(&submit_draw_end_trampoline),
        safetyhook::InlineHook::StartDisabled);
    if (!g_submit_draw_end_hook) {
        SPDLOG_WARN("[SN2-SubmitDrawEnd] safetyhook create failed at 0x{:x}", target);
        return false;
    }
    if (auto e = g_submit_draw_end_hook.enable(); !e.has_value()) {
        SPDLOG_WARN("[SN2-SubmitDrawEnd] enable failed at 0x{:x}: {}", target, static_cast<int>(e.error().type));
        return false;
    }
    SPDLOG_WARN("[SN2-SubmitDrawEnd] installed at 0x{:x} (RVA 0x{:x})",
        target, SUBNAUTICA2_MESHDRAWCMD_SUBMIT_DRAW_END_RVA);
    return true;
}

// ============================================================================
// FRHICommandDrawIndexedPrimitive::Execute TLS bridge (2026-05-28)
// ============================================================================
//
// The visible 0x13B00F0C draw is executed through the queued RHI command path:
// FRHICommandDrawIndexedPrimitive::Execute -> FD3D12CommandContextRedirector
// -> ID3D12GraphicsCommandList::DrawIndexedInstanced. SubmitDrawEnd is not on
// this call stack in SN2 shipping, so bridge the lower-level RHI command too.
inline constexpr uint64_t SUBNAUTICA2_RHI_DRAW_INDEXED_PRIMITIVE_EXECUTE_RVA = 0x321F030;
static SafetyHookInline g_rhi_draw_indexed_execute_hook{};

static void __fastcall rhi_draw_indexed_primitive_execute_trampoline(
    void* rhi_command,
    void* rhi_cmd_list_base)
{
    sn2_runtime_state::RhiDrawTls current{};
    current.active = true;
    current.cmd = reinterpret_cast<uintptr_t>(rhi_command);
    current.cmd_list = reinterpret_cast<uintptr_t>(rhi_cmd_list_base);

    if (rhi_command != nullptr) {
        const auto* cmd = reinterpret_cast<const uint8_t*>(rhi_command);
        auto read_payload = [&](uint32_t payload_off) {
            current.payload_offset = payload_off;
            sn2_safe_read(cmd + payload_off + 0x00, &current.index_buffer, sizeof(current.index_buffer));
            sn2_safe_read(cmd + payload_off + 0x08, &current.base_vertex, sizeof(current.base_vertex));
            sn2_safe_read(cmd + payload_off + 0x0C, &current.first_instance, sizeof(current.first_instance));
            sn2_safe_read(cmd + payload_off + 0x10, &current.num_vertices, sizeof(current.num_vertices));
            sn2_safe_read(cmd + payload_off + 0x14, &current.start_index, sizeof(current.start_index));
            sn2_safe_read(cmd + payload_off + 0x18, &current.num_primitives, sizeof(current.num_primitives));
            sn2_safe_read(cmd + payload_off + 0x1C, &current.num_instances, sizeof(current.num_instances));
        };
        read_payload(0x08);

        if (current.num_primitives != 25536 || current.num_instances != 1) {
            // FRHICOMMAND_MACRO prefixes a variable-sized command header. Locate the
            // payload by scanning for the trailing {StartIndex=0, NumPrimitives=25536,
            // NumInstances=1} triplet instead of assuming the header size.
            for (uint32_t off = 0x00; off + 0x20 <= 0xA0; off += 4) {
                uint32_t start_index = 0xffffffffu;
                uint32_t num_primitives = 0;
                uint32_t num_instances = 0;
                if (!sn2_safe_read(cmd + off + 0x14, &start_index, sizeof(start_index)) ||
                    !sn2_safe_read(cmd + off + 0x18, &num_primitives, sizeof(num_primitives)) ||
                    !sn2_safe_read(cmd + off + 0x1C, &num_instances, sizeof(num_instances))) {
                    continue;
                }
                if (start_index == 0 && num_primitives == 25536 && num_instances == 1) {
                    read_payload(off);
                    break;
                }
            }
        }
    }

    const bool target_shape = current.num_primitives == 25536 && current.num_instances == 1;
    if (target_shape && sn2_submitdraw_trace_enabled()) {
        static std::atomic<uint64_t> target_seq{0};
        const auto n = target_seq.fetch_add(1, std::memory_order_relaxed) + 1;
        const int max_rows = (std::max)(1, sn2_env_int("UEVR_SN2_SUBMITDRAW_TRACE_MAX", 48));
        const bool log_row = n <= static_cast<uint64_t>(max_rows) || (n % 600) == 0;

        const std::string stack = sn2_submitdraw_trace_stack_enabled() ? sn2_capture_game_stack_rvas() : "off";
        sn2_runtime_state::RhiDrawLast row{};
        row.cmd = current.cmd;
        row.cmd_list = current.cmd_list;
        row.payload_offset = current.payload_offset;
        row.index_buffer = current.index_buffer;
        row.base_vertex = current.base_vertex;
        row.first_instance = current.first_instance;
        row.num_vertices = current.num_vertices;
        row.start_index = current.start_index;
        row.num_primitives = current.num_primitives;
        row.num_instances = current.num_instances;
        row.stack = stack;
        sn2_runtime_state::note_rhi_draw_target(std::move(row));

        if (log_row) {
            SPDLOG_WARN(
                "[SN2-RHIDrawIndexedExecute] #{} cmd=0x{:x} cmd_list=0x{:x} indexBuf=0x{:x} "
                "payload=0x{:x} base={} firstInst={} verts={} start={} prims={} inst={} stack={}",
                n,
                current.cmd,
                current.cmd_list,
                current.index_buffer,
                current.payload_offset,
                current.base_vertex,
                current.first_instance,
                current.num_vertices,
                current.start_index,
                current.num_primitives,
                current.num_instances,
                stack);
        }
    }

    const auto previous = sn2_runtime_state::tls_rhi_draw;
    sn2_runtime_state::tls_rhi_draw = current;
    g_rhi_draw_indexed_execute_hook.call<void>(rhi_command, rhi_cmd_list_base);
    sn2_runtime_state::tls_rhi_draw = previous;
}

static bool install_rhi_draw_indexed_execute_probe() {
    if (!sn2_submitdraw_trace_enabled()) {
        return false;
    }
    const auto exe_base = reinterpret_cast<uintptr_t>(utility::get_executable());
    const auto target = exe_base + SUBNAUTICA2_RHI_DRAW_INDEXED_PRIMITIVE_EXECUTE_RVA;
    if (exe_base == 0 || !sn2_is_executable_process_range(target, 0x20)) {
        SPDLOG_WARN("[SN2-RHIDrawIndexedExecute] target 0x{:x} not in executable range; not installed", target);
        return false;
    }

    uint8_t pro[12]{};
    if (sn2_safe_read(reinterpret_cast<const void*>(target), pro, sizeof(pro))) {
        SPDLOG_WARN(
            "[SN2-RHIDrawIndexedExecute] target 0x{:x} prologue: {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}",
            target,
            pro[0], pro[1], pro[2], pro[3], pro[4], pro[5],
            pro[6], pro[7], pro[8], pro[9], pro[10], pro[11]);
    }

    g_rhi_draw_indexed_execute_hook = safetyhook::create_inline(
        reinterpret_cast<void*>(target),
        reinterpret_cast<void*>(&rhi_draw_indexed_primitive_execute_trampoline),
        safetyhook::InlineHook::StartDisabled);
    if (!g_rhi_draw_indexed_execute_hook) {
        SPDLOG_WARN("[SN2-RHIDrawIndexedExecute] safetyhook create failed at 0x{:x}", target);
        return false;
    }
    if (auto e = g_rhi_draw_indexed_execute_hook.enable(); !e.has_value()) {
        SPDLOG_WARN("[SN2-RHIDrawIndexedExecute] enable failed at 0x{:x}: {}", target, static_cast<int>(e.error().type));
        return false;
    }
    SPDLOG_WARN("[SN2-RHIDrawIndexedExecute] installed at 0x{:x} (RVA 0x{:x})",
        target, SUBNAUTICA2_RHI_DRAW_INDEXED_PRIMITIVE_EXECUTE_RVA);
    return true;
}

// ============================================================================
// Force IsDevelopmentBuild() == true so the UWE ImGui diagnostics open-by-name
// gate (sub_145A3C390) passes. See moddingkit/runs/SN2_UWE_RENDER_DIAGNOSTICS_2026_05_28.md.
//
// In the shipping binary, IsDevelopmentBuild() at RVA 0x12924A0 is compiled to
// a constant-false stub:
//   142924A0  32 C0    xor al, al
//   142924A2  C3       ret
// Checking a diagnostic checkbox routes through sub_145A3C390 whose first guard
// is `if (IsDevelopmentBuild())`; with the stub returning false the open branch
// (append name to DiagnosticWindows +0x40 + SaveConfig) never runs and the box
// reverts the same frame. Also blocks the boot-time saved-window restore.
//
// Patching the prologue 32 C0 -> B0 01 (mov al, 1) makes the helper return true
// everywhere, unlocking the in-game diagnostics. Byte-verified before patching.
// ============================================================================

inline constexpr uintptr_t SUBNAUTICA2_IS_DEVELOPMENT_BUILD_RVA = 0x12924A0;
// Pre-patch bytes (xor al, al)
inline constexpr uint8_t k_force_dev_build_original_bytes[2] = {0x32, 0xC0};
// Post-patch bytes (mov al, 1)
inline constexpr uint8_t k_force_dev_build_patched_bytes[2] = {0xB0, 0x01};

static void sn2_maybe_patch_is_development_build() {
    if (!sn2_force_dev_build_enabled()) {
        return;
    }

    // Idempotent: only patch once.
    static std::atomic<bool> s_patched{false};
    bool expected = false;
    if (!s_patched.compare_exchange_strong(expected, true)) {
        return;
    }

    const auto base = sn2_main_module_base();
    if (base == 0) {
        SPDLOG_WARN("[SN2-ForceDevBuild] could not resolve module base; skipping");
        return;
    }

    const auto target = base + SUBNAUTICA2_IS_DEVELOPMENT_BUILD_RVA;

    if (!sn2_is_executable_process_range(target, sizeof(k_force_dev_build_patched_bytes))) {
        SPDLOG_WARN("[SN2-ForceDevBuild] target 0x{:x} (RVA 0x{:x}) not executable; skipping",
            target, SUBNAUTICA2_IS_DEVELOPMENT_BUILD_RVA);
        return;
    }

    // Byte-verify: must see `32 C0` (xor al,al) before patching. Never patch blind.
    if (std::memcmp(reinterpret_cast<void*>(target),
                    k_force_dev_build_original_bytes,
                    sizeof(k_force_dev_build_original_bytes)) != 0) {
        uint8_t cur[2]{};
        sn2_safe_read(reinterpret_cast<const void*>(target), cur, sizeof(cur));
        SPDLOG_WARN(
            "[SN2-ForceDevBuild] byte mismatch at 0x{:x} (RVA 0x{:x}): expected 32 C0, found {:02X} {:02X}; binary likely changed, skipping",
            target, SUBNAUTICA2_IS_DEVELOPMENT_BUILD_RVA, cur[0], cur[1]);
        return;
    }

    DWORD old_protect = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(target),
                        sizeof(k_force_dev_build_patched_bytes),
                        PAGE_EXECUTE_READWRITE,
                        &old_protect)) {
        SPDLOG_WARN("[SN2-ForceDevBuild] VirtualProtect RWX failed at 0x{:x}", target);
        return;
    }

    std::memcpy(reinterpret_cast<void*>(target),
                k_force_dev_build_patched_bytes,
                sizeof(k_force_dev_build_patched_bytes));

    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<void*>(target),
                   sizeof(k_force_dev_build_patched_bytes),
                   old_protect,
                   &ignored);
    FlushInstructionCache(GetCurrentProcess(),
                          reinterpret_cast<void*>(target),
                          sizeof(k_force_dev_build_patched_bytes));

    SPDLOG_WARN("[SN2-ForceDevBuild] patched IsDevelopmentBuild @ 0x{:x} (RVA 0x{:x}) (32C0->B001); UWE ImGui diagnostics unlocked",
        target, SUBNAUTICA2_IS_DEVELOPMENT_BUILD_RVA);
}

// ============================================================================
// Public entry point: call once during UEVR startup, after the executable's
// renderer modules are loaded (i.e. after D3D12CreateDevice).
// ============================================================================
void install_all() {
    static std::atomic<bool> s_installed{false};
    bool expected = false;
    if (!s_installed.compare_exchange_strong(expected, true)) return;
    install_rdg_pass_hook();
    install_finish_gather_viewcommands_hook();
    install_generate_dynamic_mesh_draw_commands_hook();
    install_material_name_hook();
    install_try_add_mesh_batch_hook();
    install_slw_pass_id_probe_hook();
    install_frdg_builder_execute_hook();
    install_vsm_ub_clamp_patch();
    sn2_maybe_patch_is_development_build();
    install_uwe_underwater_pass_probe();
    install_fog_slot_probe();
    install_submit_draw_end_probe();
    install_rhi_draw_indexed_execute_probe();
    install_mediavolume_binder_hook();
}

void revert_all() {
    revert_vsm_ub_clamp_patch();
}

} // namespace sn2_hooks_install

// Public C linkage for ease of calling from the main UEVR init path.
extern "C" void uevr_sn2_install_render_hooks() {
    sn2_hooks_install::install_all();
}

extern "C" void uevr_sn2_revert_render_hooks() {
    sn2_hooks_install::revert_all();
}
