#define NOMINMAX

#include <windows.h>
#include <winternl.h>

#include <asmjit/asmjit.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <future>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <spdlog/spdlog.h>
#include <utility/Memory.hpp>
#include <utility/Module.hpp>
#include <utility/Scan.hpp>
#include <utility/String.hpp>
#include <utility/Thread.hpp>
#include <utility/Emulation.hpp>
#include <utility/ScopeGuard.hpp>

#include <sdk/EngineModule.hpp>
#include <sdk/UEngine.hpp>
#include <sdk/UGameEngine.hpp>
#include <sdk/CVar.hpp>
#include <sdk/Slate.hpp>
#include <sdk/DynamicRHI.hpp>
#include <sdk/FViewportInfo.hpp>
#include <sdk/Utility.hpp>
#include <sdk/RHICommandList.hpp>
#include <sdk/UGameViewportClient.hpp>
#include <sdk/Globals.hpp>
#include <sdk/FName.hpp>
#include <sdk/UObjectArray.hpp>
#include <sdk/FBoolProperty.hpp>
#include <sdk/FViewport.hpp>
#include <sdk/UKismetRenderingLibrary.hpp>
#include <sdk/UTexture.hpp>
#include <sdk/APlayerCameraManager.hpp>
#include <sdk/FStructProperty.hpp>
#include <sdk/FSceneViewFamily.hpp>

#include <sdk/UGameplayStatics.hpp>
#include <sdk/APawn.hpp>
#include <sdk/APlayerController.hpp>
#include <sdk/USceneCaptureComponent2D.hpp>
#include <sdk/FTextureRenderTargetResource.hpp>

#include "Framework.hpp"
#include "Mods.hpp"
#include "DumperMode.hpp"
#include "mods/UObjectHook.hpp"
#include "mods/GameSpecific.hpp"
#include "hooks/D3D12Hook.hpp"  // for sn2_fog_srv_map::lookup_by_gpu_va

#include <bdshemu.h>
#include <bddisasm.h>
#include <disasmtypes.h>

#include <sdk/threading/GameThreadWorker.hpp>
#include <sdk/threading/RenderThreadWorker.hpp>
#include <sdk/threading/RHIThreadWorker.hpp>
#include "../VR.hpp"
#include "../../utility/Logging.hpp"

#include "FFakeStereoRenderingHook.hpp"

#include <tracy/Tracy.hpp>

//#define FFAKE_STEREO_RENDERING_LOG_ALL_CALLS

FFakeStereoRenderingHook* g_hook = nullptr;
uint32_t g_frame_count{};

// 2026-05-17 evening: file-external linkage so D3D12Hook.cpp can read the
// most recent view-0 fog texture pointer (published by the lightscat
// midhook). Used by the fog descriptor swap to pick which pool entry to
// copy into view 1's basepass binding.
std::atomic<uintptr_t> g_subnautica2_view0_lightscat{0};
std::atomic<uint64_t> g_subnautica2_water_context_id{0};
std::atomic<uintptr_t> g_subnautica2_water_context_views{0};
std::atomic<uintptr_t> g_subnautica2_water_context_color{0};
std::atomic<uintptr_t> g_subnautica2_water_context_depth{0};

namespace {
bool is_readable_process_range(uintptr_t address, size_t size);
bool is_executable_process_range(uintptr_t address, size_t size);
bool is_writable_process_range(uintptr_t address, size_t size);

bool sn2_env_truthy(const char* name) {
    char value[32]{};
    const auto len = GetEnvironmentVariableA(name, value, static_cast<DWORD>(sizeof(value)));
    if (len == 0 || len >= sizeof(value)) {
        return false;
    }

    std::string_view raw{value, std::min<DWORD>(len, static_cast<DWORD>(sizeof(value) - 1))};
    return raw != "0" && raw != "false" && raw != "FALSE" && raw != "off" && raw != "OFF";
}

uint32_t sn2_env_u32(const char* name, uint32_t fallback) {
    char value[32]{};
    const auto len = GetEnvironmentVariableA(name, value, static_cast<DWORD>(sizeof(value)));
    if (len == 0 || len >= sizeof(value)) {
        return fallback;
    }

    char* end{};
    const auto parsed = std::strtoul(value, &end, 0);
    return end != value ? static_cast<uint32_t>(parsed) : fallback;
}

uint32_t sn2_render_fog_view_rect_log_max() {
    static const uint32_t max = sn2_env_u32("UEVR_SN2_RENDER_FOG_VIEW_RECT_LOG_MAX", 8);
    return max;
}

uint32_t sn2_setup_fog_uniform_log_max() {
    static const uint32_t max = sn2_env_u32("UEVR_SN2_SETUP_FOG_UNIFORM_LOG_MAX", 8);
    return max;
}

uint32_t sn2_single_layer_water_inner_log_max() {
    static const uint32_t max = sn2_env_u32("UEVR_SN2_SINGLE_LAYER_WATER_INNER_LOG_MAX", 8);
    return max;
}

uint32_t sn2_scene_without_water_log_max() {
    static const uint32_t max = sn2_env_u32("UEVR_SN2_SCENE_WITHOUT_WATER_LOG_MAX", 8);
    return max;
}

uint32_t sn2_native_stereo_sync_log_max() {
    static const uint32_t max = sn2_env_u32("UEVR_SN2_NATIVE_STEREO_SYNC_LOG_MAX", 8);
    return max;
}

uint32_t sn2_native_stereo_debug_log_max() {
    static const uint32_t max = sn2_env_u32("UEVR_SN2_NATIVE_STEREO_DEBUG_LOG_MAX", 8);
    return max;
}

uint32_t sn2_compose_volumetric_log_max() {
    static const uint32_t max = sn2_env_u32("UEVR_SN2_COMPOSE_VOLUMETRIC_LOG_MAX", 8);
    return max;
}

uint32_t sn2_compose_exit_check_log_max() {
    static const uint32_t max = sn2_env_u32("UEVR_SN2_COMPOSE_EXIT_CHECK_LOG_MAX", 8);
    return max;
}

uint32_t sn2_fix_v5_log_max() {
    static const uint32_t max = sn2_env_u32("UEVR_SN2_FIX_V5_LOG_MAX", 8);
    return max;
}

int subnautica2_view_id_from_view(uintptr_t view) {
    if (view == 0 ||
        !is_readable_process_range(view + 0xDD0, sizeof(uint32_t))) {
        return -1;
    }

    const uint32_t pass = *(uint32_t*)(view + 0xDD0);
    if (pass == EStereoscopicPass::eSSP_PRIMARY) {
        return 0;
    }

    if (pass == EStereoscopicPass::eSSP_SECONDARY) {
        return 1;
    }

    return -1;
}

void disable_native_instanced_stereo_cvars() {
    auto& vr = VR::get();

    if (!vr->is_native_stereo_fix_enabled() || vr->is_native_stereo_fix_same_pass_enabled()) {
        return;
    }

    static bool attempted = false;
    if (attempted) {
        return;
    }

    attempted = true;

    auto set_cvar_to_zero = [](const wchar_t* name) {
        bool set_via_console_manager = false;

        try {
            if (auto console_manager = sdk::FConsoleManager::get(); console_manager != nullptr) {
                if (auto object = console_manager->find(name); object != nullptr) {
                    ((sdk::IConsoleVariable*)object)->Set(L"0");
                    set_via_console_manager = true;
                }
            }
        } catch(...) {
        }

        const bool set_via_data =
            sdk::set_cvar_data_int(L"Engine", name, 0) ||
            sdk::set_cvar_data_int(L"RenderCore", name, 0);

        SPDLOG_INFO("[NativeStereoDebug] Disabled {} for explicit native stereo console_manager={} data={}",
            utility::narrow(name), set_via_console_manager, set_via_data);
    };

    // 2026-05-20: allow keeping vr.InstancedStereo ENABLED on SN2 (gated by
    // UEVR_SN2_KEEP_INSTANCED_STEREO=1). Per the diagnostic chain in
    // sn2_ue5_per_view_root_cause memory note, keeping ISR on is required
    // for View.bIsMultiViewportEnabled to be true, which is required for
    // ShouldDrawSceneViewsInOneNanitePass to enable Nanite per-view rendering.
    // Without this, Nanite + VSM compute passes only fire for the LEFT eye.
    static const bool keep_instanced_stereo_sn2 = []() {
        char buf[8]{};
        return GetEnvironmentVariableA("UEVR_SN2_KEEP_INSTANCED_STEREO", buf, sizeof(buf)) > 0
            && buf[0] == '1';
    }();
    if (!keep_instanced_stereo_sn2) {
        set_cvar_to_zero(L"vr.InstancedStereo");
    } else {
        SPDLOG_WARN("[NativeStereoDebug] SKIPPING vr.InstancedStereo disable (UEVR_SN2_KEEP_INSTANCED_STEREO=1 to enable per-view Nanite)");
        // Also ENABLE the InstancedStereo cvar (in case it defaulted off)
        // and the Nanite multi-view cvar (required for per-view Nanite).
        auto set_cvar_to_one = [](const wchar_t* name) {
            bool ok = false;
            try {
                if (auto console_manager = sdk::FConsoleManager::get(); console_manager != nullptr) {
                    if (auto object = console_manager->find(name); object != nullptr) {
                        ((sdk::IConsoleVariable*)object)->Set(L"1");
                        ok = true;
                    }
                }
            } catch(...) {}
            const bool via_data = sdk::set_cvar_data_int(L"Engine", name, 1) ||
                                  sdk::set_cvar_data_int(L"RenderCore", name, 1) ||
                                  sdk::set_cvar_data_int(L"Renderer", name, 1);
            SPDLOG_WARN("[NativeStereoDebug] Set {} = 1 console={} data={}",
                utility::narrow(name), ok, via_data);
        };
        set_cvar_to_one(L"vr.InstancedStereo");
        set_cvar_to_one(L"r.Nanite.MultipleSceneViewsInOnePass");
    }
    set_cvar_to_zero(L"vr.MobileMultiView");
    set_cvar_to_zero(L"vr.HiddenAreaMask");
}

void force_native_stereo_view_family_show_flag(sdk::FSceneViewFamily& view_family, std::string_view source) {
    auto& vr = VR::get();

    if (!vr->is_hmd_active() || !vr->is_native_stereo_fix_enabled() || vr->is_native_stereo_fix_same_pass_enabled()) {
        return;
    }

    // Subnautica 2's UE 5.6.1 renderer gates SetFinalViewRect on this show flag.
    constexpr auto engine_show_flags_stereo_rendering_dword_offset = 0x58;
    constexpr auto engine_show_flags_stereo_rendering_mask = 0x01000000u;

    auto* stereo_rendering_flags = (uint32_t*)((uintptr_t)&view_family + engine_show_flags_stereo_rendering_dword_offset);

    if (IsBadReadPtr(stereo_rendering_flags, sizeof(uint32_t))) {
        SPDLOG_WARN_ONCE("[NativeStereoDebug] Failed to force ViewFamily StereoRendering flag from {}; bad pointer {:x}",
            source, (uintptr_t)stereo_rendering_flags);
        return;
    }

    const auto before = *stereo_rendering_flags;
    const auto after = before | engine_show_flags_stereo_rendering_mask;
    *stereo_rendering_flags = after;

    if (before != after) {
        SPDLOG_INFO("[NativeStereoDebug] Forced ViewFamily.EngineShowFlags.StereoRendering from {} offset=0x{:x} before=0x{:08x} after=0x{:08x}",
            source, engine_show_flags_stereo_rendering_dword_offset, before, after);
    } else {
        SPDLOG_INFO_ONCE("[NativeStereoDebug] ViewFamily.EngineShowFlags.StereoRendering already set from {} offset=0x{:x} value=0x{:08x}",
            source, engine_show_flags_stereo_rendering_dword_offset, after);
    }
}

std::mutex g_shf_texture_probe_mutex{};
std::unordered_set<uintptr_t> g_shf_logged_texture_probe_keys{};
std::unordered_map<uintptr_t, std::chrono::steady_clock::time_point> g_shf_last_texture_probe_by_base{};
std::unordered_set<uintptr_t> g_shf_logged_rtm_candidate_natives{};
uint64_t g_shf_rtm_candidate_count{};
uint64_t g_shf_rtm_candidate_suppressed{};

constexpr uint32_t AVOWED_NATIVE_FIX_STABLE_FRAMES = 180;
constexpr uint32_t AVOWED_NATIVE_FIX_FAST_REACQUIRE_STABLE_FRAMES = 45;
constexpr auto AVOWED_NATIVE_FIX_RENDER_GAP = std::chrono::milliseconds(250);
constexpr auto AVOWED_NATIVE_FIX_TRANSITION_HOLD = std::chrono::seconds(10);
constexpr auto AVOWED_NATIVE_FIX_FAST_REACQUIRE_HOLD = std::chrono::milliseconds(1500);
constexpr auto AVOWED_NATIVE_FIX_FAST_REACQUIRE_MAX_MISSING = std::chrono::seconds(60);

bool is_deadzone_ue56_executable() {
    static const bool result = []() {
        const auto exe_path = utility::get_module_pathw(utility::get_executable());

        if (!exe_path || exe_path->find(L"DeadzoneSteam-Win64-Shipping") == std::wstring::npos) {
            return false;
        }

        const auto str_version = utility::narrow(sdk::search_for_version(utility::get_executable()).value_or(L"0.00"));
        const auto file_version = sdk::get_file_version_info();

        return str_version.starts_with("5.6") || file_version.dwFileVersionMS == 0x00050006;
    }();

    return result;
}

struct AvowedNativeFixGateState {
    uintptr_t scene{};
    uintptr_t render_target{};
    uintptr_t scene_capture_render_target{};
    uintptr_t scene_capture_native{};
    uintptr_t last_ready_scene{};
    uintptr_t last_ready_render_target{};
    std::chrono::steady_clock::time_point last_update{};
    std::chrono::steady_clock::time_point hold_until{};
    std::chrono::steady_clock::time_point missing_since{};
    uint32_t stable_frames{};
    uint32_t required_stable_frames{AVOWED_NATIVE_FIX_STABLE_FRAMES};
    bool ready{};
    bool has_baseline{};
    bool had_ready_baseline{};
    bool targets_missing{};
    bool fast_reacquire{};
};

std::mutex g_avowed_native_fix_gate_mutex{};
AvowedNativeFixGateState g_avowed_native_fix_gate{};
std::mutex g_ue56_rt_probe_mutex{};
std::unordered_map<uintptr_t, bool> g_ue56_native_resource_probe_cache{};

struct UE51RenderTargetChurnStats {
    uint64_t allocate_seen{};
    uint64_t ui_created{};
    uint64_t ui_reused{};
    uintptr_t last_allocate_return_address{};
    uintptr_t last_ui_create_return_address{};
    uintptr_t last_ui_texture{};
    uint32_t last_ui_width{};
    uint32_t last_ui_height{};
    std::chrono::steady_clock::time_point last_log{};
};

std::mutex g_ue51_rt_churn_mutex{};
UE51RenderTargetChurnStats g_ue51_rt_churn{};
constexpr auto ENGINE_RENDER_TIMING_LOG_INTERVAL = std::chrono::seconds(5);

struct EngineRenderTimingStats {
    uint64_t count{};
    double total_ms{};
    double max_ms{};

    void add(std::chrono::steady_clock::duration duration) {
        const auto ms = std::chrono::duration<double, std::milli>{duration}.count();
        ++count;
        total_ms += ms;
        if (ms > max_ms) {
            max_ms = ms;
        }
    }

    double avg() const {
        return count == 0 ? 0.0 : total_ms / (double)count;
    }

    void reset() {
        count = 0;
        total_ms = 0.0;
        max_ms = 0.0;
    }
};

EngineRenderTimingStats g_begin_render_viewfamily_real_timing{};
EngineRenderTimingStats g_begin_render_viewfamily_timing{};
EngineRenderTimingStats g_prerender_viewfamily_rt_timing{};
std::chrono::steady_clock::time_point g_engine_render_last_log{};

bool shf_is_current_game() {
    static const bool result = []() {
        const auto exe_path = utility::get_module_pathw(utility::get_executable());
        return exe_path && exe_path->find(L"SHf-Win64-Shipping") != std::wstring::npos;
    }();

    return result;
}

bool subnautica2_is_current_game() {
    static const bool result = []() {
        // UEVR_DISABLE_SN2_HOOKS=1 turns off ALL SN2-specific stereo patches
        // and log spam so we can iterate on generic UEVR features without
        // interference. This is the master switch for the SN2-specific code
        // baked into FFakeStereoRenderingHook for SingleLayerWater / volumetric
        // fog / lightscat fixes.
        char disable[8]{};
        if (GetEnvironmentVariableA("UEVR_DISABLE_SN2_HOOKS", disable, sizeof(disable)) > 0
                && disable[0] == '1') {
            return false;
        }
        const auto exe_path = utility::get_module_pathw(utility::get_executable());
        return exe_path && exe_path->find(L"Subnautica2-Win64-Shipping") != std::wstring::npos;
    }();

    return result;
}

bool subnautica2_diag_clean_mode() {
    static const bool result = []() {
        auto enabled = [](const char* name) {
            char value[32]{};
            const auto len = GetEnvironmentVariableA(name, value, static_cast<DWORD>(sizeof(value)));
            if (len == 0 || len >= sizeof(value)) {
                return false;
            }

            std::string_view raw{value, std::min<DWORD>(len, static_cast<DWORD>(sizeof(value) - 1))};
            return raw != "0" && raw != "false" && raw != "FALSE" && raw != "off" && raw != "OFF";
        };
        return enabled("UEVR_SN2_DIAG_CLEAN") || enabled("UEVR_SN2_DIAG_STRICT");
    }();

    return result;
}

bool subnautica2_disable_legacy_fog_uniform_mutations() {
    if (subnautica2_diag_clean_mode()) {
        return true;
    }

    static const bool result = []() {
        char value[32]{};
        const auto len = GetEnvironmentVariableA(
            "UEVR_SUBNAUTICA2_DISABLE_LEGACY_FOG_UNIFORM_MUTATIONS",
            value,
            static_cast<DWORD>(sizeof(value)));
        if (len == 0 || len >= sizeof(value)) {
            return false;
        }

        std::string_view raw{value, std::min<DWORD>(len, static_cast<DWORD>(sizeof(value) - 1))};
        return raw != "0" && raw != "false" && raw != "FALSE" && raw != "off" && raw != "OFF";
    }();

    return result;
}

bool subnautica2_disable_setup_volumetric_fog_ub_hook() {
    if (subnautica2_diag_clean_mode()) {
        return true;
    }

    static const bool result = []() {
        char value[32]{};
        const auto len = GetEnvironmentVariableA(
            "UEVR_SUBNAUTICA2_DISABLE_SETUP_VOLUMETRIC_FOG_UB_HOOK",
            value,
            static_cast<DWORD>(sizeof(value)));
        if (len == 0 || len >= sizeof(value)) {
            return false;
        }

        std::string_view raw{value, std::min<DWORD>(len, static_cast<DWORD>(sizeof(value) - 1))};
        return raw != "0" && raw != "false" && raw != "FALSE" && raw != "off" && raw != "OFF";
    }();

    return result;
}

bool subnautica2_disable_lightscat_store_midhook() {
    if (subnautica2_diag_clean_mode()) {
        return true;
    }

    static const bool result = []() {
        char value[32]{};
        const auto len = GetEnvironmentVariableA(
            "UEVR_SUBNAUTICA2_DISABLE_LIGHTSCAT_STORE_MIDHOOK",
            value,
            static_cast<DWORD>(sizeof(value)));
        if (len == 0 || len >= sizeof(value)) {
            return false;
        }

        std::string_view raw{value, std::min<DWORD>(len, static_cast<DWORD>(sizeof(value) - 1))};
        return raw != "0" && raw != "false" && raw != "FALSE" && raw != "off" && raw != "OFF";
    }();

    return result;
}

bool subnautica2_enable_lightscat_store_diag() {
    static const bool result = []() {
        char value[32]{};
        const auto len = GetEnvironmentVariableA(
            "UEVR_SUBNAUTICA2_LIGHTSCAT_STORE_DIAG",
            value,
            static_cast<DWORD>(sizeof(value)));
        if (len == 0 || len >= sizeof(value)) {
            return false;
        }

        std::string_view raw{value, std::min<DWORD>(len, static_cast<DWORD>(sizeof(value) - 1))};
        return raw != "0" && raw != "false" && raw != "FALSE" && raw != "off" && raw != "OFF";
    }();

    return result;
}

bool subnautica2_enable_volumetric_fog_param_trace() {
    static const bool result = []() {
        char value[32]{};
        const auto len = GetEnvironmentVariableA(
            "UEVR_SUBNAUTICA2_VOLUMETRIC_FOG_PARAM_TRACE",
            value,
            static_cast<DWORD>(sizeof(value)));
        if (len == 0 || len >= sizeof(value)) {
            return false;
        }

        std::string_view raw{value, std::min<DWORD>(len, static_cast<DWORD>(sizeof(value) - 1))};
        return raw != "0" && raw != "false" && raw != "FALSE" && raw != "off" && raw != "OFF";
    }();

    return result;
}

uint32_t subnautica2_fog_alias_thunk_mode() {
    if (subnautica2_diag_clean_mode()) {
        return 0;
    }
    static const uint32_t result = sn2_env_u32("UEVR_SUBNAUTICA2_FOG_ALIAS_THUNK_MODE", 0);
    return result;
}

bool subnautica2_enable_fog_alias_thunk_diag() {
    static const bool result =
        sn2_env_truthy("UEVR_SUBNAUTICA2_FOG_ALIAS_THUNK_DIAG") ||
        subnautica2_fog_alias_thunk_mode() != 0;
    return result;
}

uint32_t subnautica2_light_affects_view_mode() {
    if (subnautica2_diag_clean_mode()) {
        return 0;
    }
    static const uint32_t result = sn2_env_u32("UEVR_SUBNAUTICA2_LIGHT_AFFECTS_VIEW_MODE", 0);
    return result;
}

bool subnautica2_enable_light_affects_view_diag() {
    static const bool result =
        sn2_env_truthy("UEVR_SUBNAUTICA2_LIGHT_AFFECTS_VIEW_DIAG") ||
        subnautica2_light_affects_view_mode() != 0;
    return result;
}

uint32_t subnautica2_visible_light_infos_copy_mode() {
    if (subnautica2_diag_clean_mode()) {
        return 0;
    }
    static const uint32_t result = sn2_env_u32("UEVR_SUBNAUTICA2_VISIBLE_LIGHT_INFOS_COPY_MODE", 0);
    return result;
}

bool subnautica2_enable_lightscat_store_rewrite() {
    if (subnautica2_diag_clean_mode()) {
        return false;
    }

    static const bool result = []() {
        char value[32]{};
        const auto len = GetEnvironmentVariableA(
            "UEVR_SUBNAUTICA2_LIGHTSCAT_STORE_REWRITE",
            value,
            static_cast<DWORD>(sizeof(value)));
        if (len == 0 || len >= sizeof(value)) {
            return false;
        }

        std::string_view raw{value, std::min<DWORD>(len, static_cast<DWORD>(sizeof(value) - 1))};
        return raw != "0" && raw != "false" && raw != "FALSE" && raw != "off" && raw != "OFF";
    }();

    return result;
}

bool subnautica2_force_primary_primary_views() {
    if (subnautica2_diag_clean_mode()) return false;
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_FORCE_PRIMARY_PRIMARY", value, (DWORD)std::size(value));

        if (len == 0 || len >= std::size(value)) {
            return false;
        }

        return value[0] != L'\0' && value[0] != L'0' && value[0] != L'f' && value[0] != L'F';
    }();

    return result;
}

bool subnautica2_disable_water_state_sync() {
    if (subnautica2_diag_clean_mode()) return true;
    static const bool enabled = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_ENABLE_WATER_STATE_SYNC", value, (DWORD)std::size(value));

        if (len == 0 || len >= std::size(value)) {
            return false;
        }

        return value[0] != L'\0' && value[0] != L'0' && value[0] != L'f' && value[0] != L'F';
    }();

    if (!enabled) {
        return true;
    }

    static const bool disabled = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_DISABLE_WATER_STATE_SYNC", value, (DWORD)std::size(value));

        if (len == 0 || len >= std::size(value)) {
            return false;
        }

        return value[0] != L'\0' && value[0] != L'0' && value[0] != L'f' && value[0] != L'F';
    }();

    return disabled;
}

uint32_t subnautica2_water_state_sync_mask() {
    // Bit 0: underwater depth + water intersection.
    // Bit 1: fog-render flags at FViewInfo+0x11E8..0x11ED.
    // Bit 2: BasePass gate byte at FViewInfo+0x11D9.
    // Bit 3: BasePass key dword at FViewInfo+0x24CC. This is unsafe to
    // mirror by default; keep it as an explicit crash-repro/bisect bit only.
    // When UEVR_SUBNAUTICA2_ENABLE_WATER_STATE_SYNC=1 and this is unset,
    // enable only the fields that survived the split stability tests.
    static const uint32_t result = []() {
        wchar_t value[32]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_WATER_STATE_SYNC_MASK", value, (DWORD)std::size(value));

        if (len == 0 || len >= std::size(value)) {
            return 0x7u;
        }

        wchar_t* end = nullptr;
        const auto parsed = wcstoul(value, &end, 0);
        return end != value ? static_cast<uint32_t>(parsed) : 0u;
    }();

    return result;
}

bool subnautica2_disable_compose_volumetric_view_rect_fix() {
    if (subnautica2_diag_clean_mode()) return true;
    // Default ENABLED. SN2's ComposeVolumetricRenderTargetOverScene reads
    // both views' init_options.view_rect and runtime view_rect; under stereo
    // it leaves view 1's rect mirroring view 0's (both starting at x=0),
    // causing the volumetric-fog overlay to write only to the left half of
    // the backbuffer. This produces the "fog only in left eye" symptom.
    // Set UEVR_SUBNAUTICA2_DISABLE_COMPOSE_VOLUMETRIC_VIEW_RECT_FIX=1 to
    // disable the patch and confirm the bug returns.
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_DISABLE_COMPOSE_VOLUMETRIC_VIEW_RECT_FIX", value, (DWORD)std::size(value));

        if (len == 0 || len >= std::size(value)) {
            return false;
        }

        return value[0] != L'\0' && value[0] != L'0' && value[0] != L'f' && value[0] != L'F';
    }();

    return result;
}

// 2026-05-16: New env var to allow ONLY rect-widening (FIX-V3) without enabling
// state-copy or double-dispatch. Default OFF — caused left-eye flicker;
// matrix-swap path is now the preferred experiment.
bool subnautica2_enable_fog_rect_widen() {
    if (subnautica2_diag_clean_mode()) return false;
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_ENABLE_FOG_RECT_WIDEN", value, (DWORD)std::size(value));
        if (len == 0 || len >= std::size(value)) return false;  // default DISABLED
        return value[0] == L'1';
    }();
    return result;
}

// 2026-05-16: NEW strategy — copy view 0's matrix block to view 1 during the
// compute_volumetric_fog dispatch, then restore view 1 immediately after the
// call returns. This makes view 1's compute iteration build its fog volume
// using view 0's projection/view matrices (so the resulting volume is filled
// with view 0's perspective for both eyes' UAVs), then view 1's basepass uses
// its ORIGINAL matrices to sample. Avoids the runtime_view_rect mutation that
// caused left-eye flicker. Range covers FViewMatrices block at offset 0x3B0
// (verified read by sub_142FD6170 starting at *((_OWORD*)a2 + 59)).
bool subnautica2_enable_fog_view1_matrix_swap() {
    if (subnautica2_diag_clean_mode()) return false;
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_ENABLE_FOG_VIEW1_MATRIX_SWAP", value, (DWORD)std::size(value));
        if (len == 0 || len >= std::size(value)) return false;  // default DISABLED
        return value[0] == L'1';
    }();
    return result;
}

uint32_t subnautica2_fog_view1_matrix_swap_offset() {
    static const uint32_t result = []() {
        wchar_t value[32]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_FOG_MATRIX_SWAP_OFFSET", value, (DWORD)std::size(value));
        if (len == 0 || len >= std::size(value)) return (uint32_t)0x3B0;
        return (uint32_t)wcstoul(value, nullptr, 0);
    }();
    return result;
}

uint32_t subnautica2_fog_view1_matrix_swap_size() {
    static const uint32_t result = []() {
        wchar_t value[32]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_FOG_MATRIX_SWAP_SIZE", value, (DWORD)std::size(value));
        if (len == 0 || len >= std::size(value)) return (uint32_t)0x200;  // 512 bytes = ~8 4x4 matrices
        return (uint32_t)wcstoul(value, nullptr, 0);
    }();
    return result;
}

// 2026-05-17 Plan Phase A.1: per-view volumetric fog dispatcher (sub_142FD6170)
// hook for view-tagging fog SRVs created during compute_volumetric_fog. Sets a
// GLOBAL ATOMIC g_sn2_current_fog_view (was thread_local but RHI thread != render
// thread broke that) that D3D12Hook::create_shader_resource_view reads to tag
// captured fog SRVs. Single-writer (compute_volumetric_fog runs sequentially
// per view) so simple atomic is enough.
std::atomic<int> g_sn2_current_fog_view{-1};
extern "C" int sn2_get_current_fog_view() {
    return g_sn2_current_fog_view.load(std::memory_order_relaxed);
}

extern "C" void uevr_sn2_try_install_early_volumetric_fog_per_view_hook() {
    if (g_hook != nullptr) {
        g_hook->attempt_hook_subnautica2_volumetric_fog_per_view();
    }
}

// 2026-05-17 Plan Phase B: descriptor swap via explicit CPU handles. User reads
// FogSRV log to find view 1's SRV cpu_handle (source) and view 0's slot
// cpu_handle (destination). Both gated to be non-zero, else no swap.
uint64_t subnautica2_fog_swap_src_handle() {
    static const uint64_t result = []() {
        wchar_t value[32]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_FOG_SWAP_SRC_HANDLE", value, (DWORD)std::size(value));
        if (len == 0 || len >= std::size(value)) return (uint64_t)0;
        return (uint64_t)wcstoull(value, nullptr, 0);
    }();
    return result;
}

uint64_t subnautica2_fog_swap_dst_handle() {
    static const uint64_t result = []() {
        wchar_t value[32]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_FOG_SWAP_DST_HANDLE", value, (DWORD)std::size(value));
        if (len == 0 || len >= std::size(value)) return (uint64_t)0;
        return (uint64_t)wcstoull(value, nullptr, 0);
    }();
    return result;
}

uint32_t subnautica2_fog_swap_count() {
    static const uint32_t result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_FOG_SWAP_COUNT", value, (DWORD)std::size(value));
        if (len == 0 || len >= std::size(value)) return (uint32_t)2;
        return (uint32_t)wcstoul(value, nullptr, 0);
    }();
    return result;
}

// 2026-05-17 Plan Phase B (index-based variant): more convenient than raw cpu
// handles — user reads FogSRV log, picks SRV creation indices for source (view
// 1) and destination (view 0's slot bound to right-eye SLW PS).
// Read EVERY call (not cached) so user can change via SetEnvironmentVariable
// in a tool that injects into the process — supports faster iteration.
int32_t subnautica2_fog_swap_src_index() {
    wchar_t value[16]{};
    const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_FOG_SWAP_SRC_INDEX", value, (DWORD)std::size(value));
    if (len == 0 || len >= std::size(value)) return -1;
    return (int32_t)wcstol(value, nullptr, 0);
}

int32_t subnautica2_fog_swap_dst_index() {
    wchar_t value[16]{};
    const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_FOG_SWAP_DST_INDEX", value, (DWORD)std::size(value));
    if (len == 0 || len >= std::size(value)) return -1;
    return (int32_t)wcstol(value, nullptr, 0);
}

// 2026-05-17: sweep mode. If enabled, automatically cycle src_idx through 0..15
// every SWAP_SWEEP_INTERVAL_MS milliseconds. dst_idx stays fixed.
// User watches right eye; when fixed, reads log for the active src_idx.
bool subnautica2_fog_swap_sweep_enabled() {
    wchar_t value[16]{};
    const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_FOG_SWAP_SWEEP", value, (DWORD)std::size(value));
    if (len == 0 || len >= std::size(value)) return false;
    return value[0] == L'1';
}

uint32_t subnautica2_fog_swap_sweep_interval_ms() {
    static const uint32_t result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_FOG_SWAP_SWEEP_INTERVAL_MS", value, (DWORD)std::size(value));
        if (len == 0 || len >= std::size(value)) return (uint32_t)3000;
        return (uint32_t)wcstoul(value, nullptr, 0);
    }();
    return result;
}

uint32_t subnautica2_fog_swap_sweep_max_index() {
    static const uint32_t result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_FOG_SWAP_SWEEP_MAX_INDEX", value, (DWORD)std::size(value));
        if (len == 0 || len >= std::size(value)) return (uint32_t)16;
        return (uint32_t)wcstoul(value, nullptr, 0);
    }();
    return result;
}

uint32_t subnautica2_fog_swap_sweep_min_index() {
    static const uint32_t result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_FOG_SWAP_SWEEP_MIN_INDEX", value, (DWORD)std::size(value));
        if (len == 0 || len >= std::size(value)) return (uint32_t)0;
        return (uint32_t)wcstoul(value, nullptr, 0);
    }();
    return result;
}

// Sweep target: 0=src, 1=dst. Default 0 (src).
uint32_t subnautica2_fog_swap_sweep_target() {
    wchar_t value[16]{};
    const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_FOG_SWAP_SWEEP_TARGET", value, (DWORD)std::size(value));
    if (len == 0 || len >= std::size(value)) return 0;
    return (uint32_t)wcstoul(value, nullptr, 0);
}

// 2026-05-17 Task #43: if enabled, override the src descriptor by looking up
// view 1's compute-output UAV-tagged ID3D12Resource (from sn2_fog_uav_map),
// then resolving the matching SRV cpu_handle from sn2_fog_srv_map. This is
// the principled path to find view 1's correct fog volume without manual
// src_idx tuning.
bool subnautica2_fog_swap_use_uav_tag() {
    wchar_t value[16]{};
    const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_FOG_SWAP_USE_UAV_TAG", value, (DWORD)std::size(value));
    if (len == 0 || len >= std::size(value)) return false;
    return value[0] == L'1';
}


bool subnautica2_enable_volumetric_fog_per_view_hook() {
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_ENABLE_VOLUMETRIC_FOG_PER_VIEW_HOOK", value, (DWORD)std::size(value));
        if (len == 0 || len >= std::size(value)) return false;
        return value[0] == L'1';
    }();
    return result;
}

// 2026-05-16 Phase 3: enable per-view SLW hook (sub_142EB72E0) for the bindless
// descriptor swap fix. Default OFF until the hook + heap rewrite is validated.
bool subnautica2_enable_slw_per_view_hook() {
    if (subnautica2_diag_clean_mode()) return false;
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_ENABLE_SLW_PER_VIEW_HOOK", value, (DWORD)std::size(value));
        if (len == 0 || len >= std::size(value)) return false;
        return value[0] == L'1';
    }();
    return result;
}

// 2026-05-16 Phase 3 EXPERIMENT: copy view 0's FViewInfo+0x2630..+0x2648 region
// into view 1 just before view 1's SLW per-view dispatcher runs. The wide-scan
// diagnostic showed these 4 pointers differ between view 0 and view 1 with
// identical stride 0x1D20 — strong signature of RDG resource wrappers. If they
// are the SLW pass's fog volume wrapper refs, copying view 0's into view 1
// makes view 1's SLW use view 0's filled volumes (stereo-collapse on water but
// no black sky).
bool subnautica2_enable_slw_per_view_field_copy() {
    if (subnautica2_diag_clean_mode()) return false;
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_ENABLE_SLW_PER_VIEW_FIELD_COPY", value, (DWORD)std::size(value));
        if (len == 0 || len >= std::size(value)) return false;
        return value[0] == L'1';
    }();
    return result;
}

uint32_t subnautica2_slw_field_copy_start() {
    static const uint32_t result = []() {
        wchar_t value[32]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_SLW_FIELD_COPY_START", value, (DWORD)std::size(value));
        if (len == 0 || len >= std::size(value)) return (uint32_t)0x2630;
        return (uint32_t)wcstoul(value, nullptr, 0);
    }();
    return result;
}

uint32_t subnautica2_slw_field_copy_size() {
    static const uint32_t result = []() {
        wchar_t value[32]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_SLW_FIELD_COPY_SIZE", value, (DWORD)std::size(value));
        if (len == 0 || len >= std::size(value)) return (uint32_t)0x20;  // 4 ptrs * 8 = 32 bytes
        return (uint32_t)wcstoul(value, nullptr, 0);
    }();
    return result;
}

// 2026-05-16 EXPERIMENT MODE 2: skip the post-call restore. Field copy stays
// in view 1's FViewInfo for the rest of this frame; UE5's next-frame setup
// overwrites it. Theory: avoids double-ownership crash because view 1 fully
// commits to view 0's wrappers (no race between two consumers of the same
// wrapper later in the frame).
bool subnautica2_slw_field_copy_no_restore() {
    if (subnautica2_diag_clean_mode()) return false;
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_SLW_FIELD_COPY_NO_RESTORE", value, (DWORD)std::size(value));
        if (len == 0 || len >= std::size(value)) return false;
        return value[0] == L'1';
    }();
    return result;
}

bool subnautica2_enable_uwe_trace() {
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_ENABLE_UWE_TRACE", value, (DWORD)std::size(value));
        if (len == 0 || len >= std::size(value)) return false;
        return value[0] == L'1';
    }();
    return result;
}

bool subnautica2_enable_uwe_fd0a50_detail() {
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_ENABLE_UWE_FD0A50_DETAIL", value, (DWORD)std::size(value));
        if (len == 0 || len >= std::size(value)) return false;
        return value[0] == L'1';
    }();
    return result;
}

bool subnautica2_fd0a50_right_out_from_left() {
    if (subnautica2_diag_clean_mode()) return false;
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_FD0A50_RIGHT_OUT_FROM_LEFT", value, (DWORD)std::size(value));
        if (len == 0 || len >= std::size(value)) return false;
        return value[0] == L'1';
    }();
    return result;
}

// Bitmask gate for the 9 UWELit hooks. Bits 0..8 correspond to the 9 hooks in
// declaration order. UEVR_SUBNAUTICA2_UWELIT_HOOK_MASK env var (decimal). e.g.:
//   "1"   = bit 0  = hook sub_14506DC20 only
//   "3"   = bits 0,1 = hooks 506DC20 + 5BF08A6
//   "511" = bits 0..8 = all 9 hooks (DANGER: 9-hooks-at-once caused hang)
// Default 1 = just the material permutation builder.
uint32_t subnautica2_uwelit_hook_mask() {
    static const uint32_t result = []() -> uint32_t {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_UWELIT_HOOK_MASK", value, (DWORD)std::size(value));
        if (len == 0 || len >= std::size(value)) return 1u;
        try { return (uint32_t)std::stoi(std::wstring(value)); } catch (...) { return 1u; }
    }();
    return result;
}

bool subnautica2_disable_volumetric_fog_view_loop_fix() {
    if (subnautica2_diag_clean_mode()) return true;
    // Default DISABLED: decompile of sub_142FBED20 confirms the per-view fog
    // loop iterates BOTH views unconditionally (Hex-Rays just hides the loop;
    // see ASM at loc_142FBF280..0x142FC1E67). The "second fog build" hook
    // double-dispatches ComputeVolumetricFog with a mutated Views.Num()=1,
    // which poisons view 1's compute pass and produces the "fog only in
    // left eye" symptom. Set the env var to 0 to re-enable the hook.
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_DISABLE_VOLUMETRIC_FOG_VIEW_LOOP_FIX", value, (DWORD)std::size(value));

        if (len == 0 || len >= std::size(value)) {
            return true;
        }

        return value[0] != L'\0' && value[0] != L'0' && value[0] != L'f' && value[0] != L'F';
    }();

    return result;
}

bool subnautica2_disable_volumetric_rt_replay() {
    if (subnautica2_diag_clean_mode()) return true;
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(
            L"UEVR_SUBNAUTICA2_DISABLE_VOLUMETRIC_RT_REPLAY",
            value,
            (DWORD)std::size(value));

        if (len == 0 || len >= std::size(value)) {
            return false;
        }

        return value[0] != L'\0' && value[0] != L'0' && value[0] != L'f' && value[0] != L'F';
    }();

    return result;
}

bool subnautica2_enable_volumetric_fog_view_index_force() {
    if (subnautica2_diag_clean_mode()) return false;
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_ENABLE_VOLUMETRIC_FOG_VIEW_INDEX_FORCE", value, (DWORD)std::size(value));

        if (len == 0 || len >= std::size(value)) {
            return false;
        }

        return value[0] != L'\0' && value[0] != L'0' && value[0] != L'f' && value[0] != L'F';
    }();

    return result;
}

// 2026-05-15: post-compute state-copy fix. After compute_volumetric_fog runs
// for view 0, copy state+0x1E00 (the IntegratedLightScattering texture handle
// per user's offline trace) from view 0's FSceneViewState to view 1's
// FSceneViewState. View 1's basepass walker then reads view 0's valid UB at
// params+0x2C lookup, instead of garbage. Both views render with view 0's fog.
bool subnautica2_enable_volumetric_fog_state_copy() {
    if (subnautica2_diag_clean_mode()) return false;
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_ENABLE_VOLUMETRIC_FOG_STATE_COPY", value, (DWORD)std::size(value));

        if (len == 0 || len >= std::size(value)) {
            return false;
        }

        return value[0] != L'\0' && value[0] != L'0' && value[0] != L'f' && value[0] != L'F';
    }();

    return result;
}

uint32_t subnautica2_volumetric_fog_state_copy_size() {
    static const uint32_t result = []() -> uint32_t {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_VOLUMETRIC_FOG_STATE_COPY_SIZE", value, (DWORD)std::size(value));
        if (len == 0 || len >= std::size(value)) {
            return 8;  // default: 8 bytes (single pointer)
        }
        try {
            return (uint32_t)std::wcstoul(value, nullptr, 0);
        } catch (...) {
            return 8;
        }
    }();
    return result;
}

bool subnautica2_force_secondary_primary_view_index() {
    if (subnautica2_diag_clean_mode()) return false;
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_FORCE_SECONDARY_PRIMARY_VIEW_INDEX", value, (DWORD)std::size(value));

        if (len == 0 || len >= std::size(value)) {
            return false;
        }

        return value[0] != L'\0' && value[0] != L'0' && value[0] != L'f' && value[0] != L'F';
    }();

    return result;
}

bool subnautica2_clear_stereo_aspects() {
    if (subnautica2_diag_clean_mode()) return false;
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_CLEAR_STEREO_ASPECTS", value, (DWORD)std::size(value));

        if (len == 0 || len >= std::size(value)) {
            return false;
        }

        return value[0] != L'\0' && value[0] != L'0' && value[0] != L'f' && value[0] != L'F';
    }();

    return result;
}

bool subnautica2_disable_underwater_fog_view_data_fix() {
    if (subnautica2_diag_clean_mode()) return true;
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_DISABLE_UNDERWATER_FOG_VIEW_DATA_FIX", value, (DWORD)std::size(value));

        if (len == 0 || len >= std::size(value)) {
            return false;
        }

        return value[0] != L'\0' && value[0] != L'0' && value[0] != L'f' && value[0] != L'F';
    }();

    return result;
}

bool subnautica2_force_underwater_fog_per_view() {
    if (subnautica2_diag_clean_mode()) return false;
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_FORCE_UNDERWATER_FOG_PER_VIEW", value, (DWORD)std::size(value));

        if (len == 0 || len >= std::size(value)) {
            return false;
        }

        return value[0] != L'\0' && value[0] != L'0' && value[0] != L'f' && value[0] != L'F';
    }();

    return result;
}

bool subnautica2_disable_render_fog_view_rect_fix() {
    if (subnautica2_diag_clean_mode()) return true;
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_DISABLE_RENDER_FOG_VIEW_RECT_FIX", value, (DWORD)std::size(value));
        return len > 0 && value[0] != L'\0' && value[0] != L'0';
    }();

    return result;
}

bool subnautica2_persist_render_fog_view_rect_fix() {
    if (subnautica2_diag_clean_mode()) return false;
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_PERSIST_RENDER_FOG_VIEW_RECT_FIX", value, (DWORD)std::size(value));

        if (len == 0 || len >= std::size(value)) {
            return false;
        }

        return value[0] != L'\0' && value[0] != L'0' && value[0] != L'f' && value[0] != L'F';
    }();

    return result;
}

bool subnautica2_disable_single_layer_water_pass_fix() {
    if (subnautica2_diag_clean_mode()) return true;
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_DISABLE_SINGLE_LAYER_WATER_PASS_FIX", value, (DWORD)std::size(value));

        if (len == 0 || len >= std::size(value)) {
            return false;
        }

        return value[0] != L'\0' && value[0] != L'0' && value[0] != L'f' && value[0] != L'F';
    }();

    return result;
}

bool subnautica2_disable_single_layer_water_view_rect_fix() {
    if (subnautica2_diag_clean_mode()) return true;
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_DISABLE_SINGLE_LAYER_WATER_VIEW_RECT_FIX", value, (DWORD)std::size(value));

        if (len == 0 || len >= std::size(value)) {
            return true;
        }

        return value[0] != L'\0' && value[0] != L'0' && value[0] != L'f' && value[0] != L'F';
    }();

    return result;
}

bool subnautica2_enable_single_layer_water_secondary_replay() {
    if (subnautica2_diag_clean_mode()) return false;
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_WATER_SECONDARY_REPLAY", value, (DWORD)std::size(value));

        if (len == 0 || len >= std::size(value)) {
            return false;
        }

        return value[0] != L'\0' && value[0] != L'0' && value[0] != L'f' && value[0] != L'F';
    }();

    return result;
}

// 2026-05-16: UWE-trace mid-hooks. Four functions identified by IDA scan of
// .text byte-reads at FEngineShowFlags offset 0xE0 (UWEWaterExtinctionView):
//   sub_14045F890 / sub_1404B2A10 / sub_1405665F0 / sub_140576140
// Each fires ~ once per view per frame for various UWE water passes. Logging
// rcx/rdx/r8/r9 + first 4 stack args + dereference of pointer-shaped args
// reveals which view data each is handed. Gated by UEVR_SUBNAUTICA2_ENABLE_UWE_TRACE.
constexpr uintptr_t SUBNAUTICA2_UWE_TRACE_45F890_RVA = 0x45F890;
constexpr uintptr_t SUBNAUTICA2_UWE_TRACE_4B2A10_RVA = 0x4B2A10;
constexpr uintptr_t SUBNAUTICA2_UWE_TRACE_5665F0_RVA = 0x5665F0;
constexpr uintptr_t SUBNAUTICA2_UWE_TRACE_576140_RVA = 0x576140;
// FVolumetricFogIntegrationParameters setup — per-view integration param
// builder. Takes (params_out, FViewInfo*, integration_data). Called 3x per
// frame from compute_volumetric_fog's per-view loop. Critical for finding
// the per-view-broken site: if both views' calls receive distinct FViewInfo
// pointers, the pre-Execute setup is correct and the bug is in RDG resolution.
constexpr uintptr_t SUBNAUTICA2_UWE_TRACE_FD6170_RVA = 0x2FD6170;
// sub_142FD0A50 = UWE Ray-Traced Volumetric Fog Pass dispatcher. Builds an
// FRayTraceDirectionalLightVolumeShadowMapRGS::FParameters, allocates a per-view
// FRDGTexture (3D fog volume), and registers the pass. Args:
//   a1 = FRDGBuilder*, a2 = FViewInfo*, a3 = FScene*,
//   a4 = FVolumetricFogIntegrationParameterData*, a5 = OUT FRDGTextureRef**
// The OUTPUT FRDGTextureRef written through a5 is the per-view fog volume —
// the one that should differ between view 0 and view 1 but ends up bound to
// the same physical resource at FRDGBuilder::Execute time. Tracing this is
// the definitive per-view path.
constexpr uintptr_t SUBNAUTICA2_UWE_TRACE_FD0A50_RVA = 0x2FD0A50;
// UWEWaterLighting (FEngineShowFlags 0xDF) is the SOLE flag that toggles the
// visible-teal-in-left-eye effect. The 11 functions below all read offset 0xDF;
// hooking them shows which is the per-view-broken render pass.
// (User-confirmed 2026-05-16: ShowFlag.UWEWaterLighting 0 → both eyes match;
//  r.fog 0 → both match; r.UWEFog 0 → NO effect; bug NOT in UWE plugin path.)
constexpr uintptr_t SUBNAUTICA2_UWELIT_TRACE_RVAS[] = {
    0x506DC20,  // 1916 bytes — material permutation builder (writes bit 3 of param +520)
    0x5BF08A6,  // 1100 bytes
    0x5D638D1,  // 1285 bytes
    0x644EDEC,  // 876 bytes
    0x644FA04,  // 804 bytes
    0x6452806,  // 672 bytes
    0x69D828D,  // 749 bytes
    0x7FA6BB5,  // 737 bytes
    0x9131F2A,  // 1715 bytes
};

constexpr uintptr_t SUBNAUTICA2_COMPUTE_VOLUMETRIC_FOG_RVA = 0x2FBED20;
constexpr uintptr_t SUBNAUTICA2_COMPUTE_VOLUMETRIC_FOG_VIEW_INDEX_STORE_RVA = 0x2FBF1AF;
constexpr uintptr_t SUBNAUTICA2_INIT_VOLUMETRIC_RENDER_TARGET_RVA = 0x300F8B0;
constexpr uintptr_t SUBNAUTICA2_RECONSTRUCT_VOLUMETRIC_RENDER_TARGET_RVA = 0x3013530;
constexpr uintptr_t SUBNAUTICA2_COMPOSE_VOLUMETRIC_OVER_SCENE_RVA = 0x2FFB190;
constexpr uintptr_t SUBNAUTICA2_RENDER_FOG_WRAPPER_RVA = 0x26F7F90;
constexpr uintptr_t SUBNAUTICA2_RENDER_FOG_PASS_RVA = 0x26EF210;
constexpr uintptr_t SUBNAUTICA2_RENDER_UNDERWATER_FOG_RVA = 0x26F9B40;
// RenderUnderWaterFog's per-view fog-in-water loop normally skips the loop body
// when view+0x11EC is true and the scene gate byte resolves false. In -emulatestereo
// this suppresses the secondary eye's water fog producer. NOP the secondary JZ:
//   1426F9C4F 0F 84 E9 03 00 00  jz loc_1426FA03E
constexpr uintptr_t SUBNAUTICA2_RENDER_UNDERWATER_FOG_PER_VIEW_GUARD_RVA = 0x26F9C4F;
constexpr uintptr_t SUBNAUTICA2_RENDER_UNDERWATER_FOG_GUARD_PROCEED_RVA = 0x26F9C55;
constexpr uintptr_t SUBNAUTICA2_RENDER_UNDERWATER_FOG_BUILD_BLOCK_RVA = 0x26F9C70;
// Tiny thunk called from RenderFog before the real per-view fog handler:
//   view->IntegratedLightScattering = source_view->IntegratedLightScattering
//   view->IntegratedLightExtinction = source_view->IntegratedLightExtinction
//   jmp sub_142F57D50
// This is observe/fix gated because it can alias the right eye to left-eye fog.
constexpr uintptr_t SUBNAUTICA2_FOG_ALIAS_THUNK_RVA = 0x0D12008;
constexpr uintptr_t SUBNAUTICA2_REAL_RENDER_FOG_HANDLER_RVA = 0x2F57D50;
// FViewInfo::SetupVolumetricFogUniformBufferParameters — per-view function that
// populates FViewUniformShaderParameters' volumetric fog section at offset 0xFC0
// (~116 bytes through 0x1034). The function checks family-level r.VolumetricFog
// cvar but the OUTPUT depends on per-view inputs (view+0x1170 distance, etc).
// SN2 only fills the 3D fog volume for view 0 via compute_volumetric_fog, so
// view 1's UB has fog parameters pointing to empty/zero regions. Hook to copy
// view 0's UB output over view 1's UB output, forcing both views' basepass to
// sample identical fog regions → visible teal in BOTH eyes.
constexpr uintptr_t SUBNAUTICA2_SETUP_VOLUMETRIC_FOG_UB_RVA = 0x2FD6620;
// 2026-05-17 evening REVISION v2 — naive mirror direction was wrong:
//   compute_volumetric_fog runs PER-VIEW. Call 1 (r14=view0) writes view 0's
//   real texture; mirror to view 1's slot works. THEN call 2 (r14=view1)
//   OVERWRITES view 1's slot with view 1's OWN empty texture. Net result:
//   view 1 ends up with empty texture again.
// Fix: midhook fires BEFORE the store at 0x2FC179D. On view 0's call (detected
// via v1_vtable!=0), SAVE ctx.rax/ctx.r13 into globals. On view 1's call
// (v1_vtable==0 means we ARE view 1), OVERWRITE ctx.rax/ctx.r13 with the
// saved view-0 values. The store then writes view 0's texture into view 1's
// slot, and stays there because view 1 has no later call to overwrite it.
constexpr uintptr_t SUBNAUTICA2_LIGHTSCAT_STORE_RVA = 0x2FC179D;
// ComputeVolumetricFog helper entry points from the SN2 112084 binary. These
// are observe-only trace hooks for the family-shared
// FVolumetricFogIntegrationParameterData hypothesis: the per-view loop enters
// both eyes, but some helper dispatches may read stale view-0 fields from the
// shared ViewFamily scratch struct.
constexpr uintptr_t SUBNAUTICA2_VOLFOG_INIT_VOLUME_ATTRS_RVA = 0x2FCFD20;
constexpr uintptr_t SUBNAUTICA2_VOLFOG_VOXELIZE_PRIMS_RVA = 0x2FD0A50;
constexpr uintptr_t SUBNAUTICA2_VOLFOG_CLEAR_PASS_RVA = 0x2FB53B0;
constexpr uintptr_t SUBNAUTICA2_VOLFOG_LIGHT_SCATTER_RVA = 0x2FB5320;
constexpr uintptr_t SUBNAUTICA2_VOLFOG_FINAL_INTEGRATION_RVA = 0x2FB5270;
// FLightSceneInfo::AffectsView-like predicate called inside the per-view fog
// light loop. Reads FViewInfo+0x1F88, a 104-byte-per-light visibility table;
// this is the current best root-cause gate for missing right-eye fog dispatches.
constexpr uintptr_t SUBNAUTICA2_LIGHT_AFFECTS_VIEW_RVA = 0x2917510;
// SetupFogUniformParameters at RVA 0x26FD010 — populates FFogUniformParameters
// from FViewInfo. Reads view+0x2658 to fill the IntegratedLightScattering
// pointer at a3+0x120 (and the +0x2660 companion to a3+0x130). For view 1,
// if my post-compute copy ran in time, view+0x2658 has view 0's value here.
// If not, fall-back default black texture is used and view 1 has no fog.
constexpr uintptr_t SUBNAUTICA2_SETUP_FOG_UNIFORM_PARAMS_RVA = 0x26FD010;
// 2026-05-15 final probe: TryAddMeshBatch fog-skip divergence point. At
// 0x1426870E8 the function checks r15 (some pointer), r14b (a flag), and a
// virtual call result. If ALL non-null/non-zero/return-zero, bl=1 and fog
// is applied to this mesh. Otherwise bl=0 and fog is skipped at 0x142687131
// jz. Per disproven_patches: view 0 has bl=1 (fog), view 1 has bl=0 (no fog).
// Probe which specific condition fails for view 1 so we can patch the
// minimum necessary scope without crashing on downstream null derefs.
constexpr uintptr_t SUBNAUTICA2_TRY_ADD_MESH_BATCH_PROBE_RVA = 0x26870E8;
// sub_142631130 — SN2-customized BasePass PSO selection dispatcher. 7-way
// switch on the 3rd register arg (loaded as *(uint32_t*) of an SN2-added
// pointer arg to FBasePassMeshProcessor::Process<FUniformLightMapPolicy>).
constexpr uintptr_t SUBNAUTICA2_BASEPASS_PSO_SELECT_RVA = 0x2631130;
// sub_14263A3A0 — FBasePassMeshProcessor::FBasePassMeshProcessor. The ctor
// is the convergence point for per-view BasePass setup; its 8th arg (EFlags)
// and 9th arg (ETranslucencyPass) are the only inputs that can vary per
// call. We log them to find if they diverge per view.
constexpr uintptr_t SUBNAUTICA2_BASEPASS_MP_CTOR_RVA = 0x263A3A0;
// In TryAddMeshBatch, immediately AFTER `movss xmm3,[rsi+8Ch]` (8-byte instr
// at 0x142687108). At 0x142687110 xmm3 has just been loaded with the
// per-view cull threshold from FBasePassMeshProcessor+0x8C. A MidHook here
// can override xmm3 to force secondary view to use primary's threshold so
// both views select the same shader-type-getter -> same shader binary ->
// symmetric fog rendering.
constexpr uintptr_t SUBNAUTICA2_BASEPASS_CULL_THRESHOLD_RVA = 0x2687110;
constexpr uintptr_t SUBNAUTICA2_SINGLE_LAYER_WATER_RVA = 0x2EC70C0;
constexpr uintptr_t SUBNAUTICA2_SINGLE_LAYER_WATER_INNER_RVA = 0x2EC8C40;
constexpr uintptr_t SUBNAUTICA2_SINGLE_LAYER_WATER_SCENE_WITHOUT_WATER_READY_RVA = 0x2EC71D7;
// 2026-05-16: per-view SLW dispatcher discovered by disasm of sub_142EC8C40.
// Called once per FViewInfo* inside the SLW inner loop at 0x142EC8F00.
// R8 = FViewInfo* (per-view arg), RCX = scene renderer / builder, RDX = scene
// textures-ish, R9 = extra ctx. Sibling smaller helper sub_142DC1F10 also gets
// the per-view FViewInfo (in RCX) — kept as alternate hook candidate.
constexpr uintptr_t SUBNAUTICA2_SLW_PER_VIEW_RVA = 0x2EB72E0;
constexpr uintptr_t SUBNAUTICA2_SLW_PER_VIEW_HELPER_RVA = 0x2DC1F10;
// 2026-05-17 Plan A.1: per-view volumetric fog integration setup. Called twice
// per frame from compute_volumetric_fog (sub_142FBED20), once per FViewInfo.
// Sets thread_local view tag used by CreateShaderResourceView to identify
// which view a fog SRV belongs to.
constexpr uintptr_t SUBNAUTICA2_VOLUMETRIC_FOG_PER_VIEW_RVA = 0x2FD6170;
constexpr auto SUBNAUTICA2_SCENERENDERER_VIEWS_DATA_OFFSET = 0x10;
constexpr auto SUBNAUTICA2_SCENERENDERER_VIEWS_COUNT_OFFSET = 0x18;
constexpr auto SUBNAUTICA2_SCENERENDERER_VIEWS_MAX_OFFSET = 0x1C;
constexpr auto SUBNAUTICA2_SCENEVIEW_STRIDE = 0x29D0;
constexpr auto SUBNAUTICA2_SCENEVIEW_FAMILY_OFFSET = 0x08;
constexpr auto SUBNAUTICA2_SCENEVIEW_INIT_OPTIONS_OFFSET = 0x50;
constexpr auto SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET = 0xDD0;
constexpr auto SUBNAUTICA2_SCENEVIEW_STEREO_VIEW_INDEX_OFFSET = 0xDD4;
constexpr auto SUBNAUTICA2_SCENEVIEW_PRIMARY_VIEW_INDEX_OFFSET = 0xDD8;
constexpr auto SUBNAUTICA2_SCENEVIEW_FOG_RENDER_FLAG_OFFSET = 0x11EC;
// SN2-added per-view byte at +0x11D9 that the FBasePassMeshProcessor ctor
// reads to decide a gating condition. If this byte differs primary vs
// secondary, the mesh-pass processor's internal state diverges per view and
// downstream shader-map lookup returns different PSO permutations.
constexpr auto SUBNAUTICA2_SCENEVIEW_BASEPASS_GATE_BYTE_OFFSET = 0x11D9;
// SN2-added per-view dword at +0x24CC (right after VISIBILITY_FLAGS=0x24C8)
// that the FBasePassMeshProcessor ctor copies into FBasePassMeshProcessor+0x140.
// Per-view divergence of this dword propagates into per-view PSO selection.
constexpr auto SUBNAUTICA2_SCENEVIEW_BASEPASS_KEY_DWORD_OFFSET = 0x24CC;
// Per-view fog-render bool block. SN2's FSceneView ctor copies these from
// FSceneViewInitOptions+0x2E0..0x2E5. The block covers FOG_RENDER_FLAG (0x11EC)
// plus 4 adjacent bools at 0x11E8..0x11EB that gate which fog code path the
// material shader-map selects (e.g. NEEDS_BASEPASS_PIXEL_VOLUMETRIC_FOGGING).
// View 0 and view 1 are constructed with independent values and end up
// selecting different PS permutations for the same material, producing the
// "fog only in left eye" symptom. We sync these primary->secondary at
// BeginRenderViewFamily so both views pick the same PSO.
constexpr auto SUBNAUTICA2_SCENEVIEW_FOG_RENDER_FLAGS_START_OFFSET = 0x11E8;
constexpr auto SUBNAUTICA2_SCENEVIEW_FOG_RENDER_FLAGS_SIZE = 0x6; // 0x11E8..0x11ED inclusive
constexpr auto SUBNAUTICA2_SCENEVIEW_INSTANCED_STEREO_ENABLED_OFFSET = 0x11F3;
constexpr auto SUBNAUTICA2_SCENEVIEW_MULTI_VIEWPORT_ENABLED_OFFSET = 0x11F4;
constexpr auto SUBNAUTICA2_SCENEVIEW_MOBILE_MULTI_VIEW_ENABLED_OFFSET = 0x11F5;
constexpr auto SUBNAUTICA2_SCENEVIEW_SHOULD_BIND_INSTANCED_VIEW_UB_OFFSET = 0x11F6;
constexpr auto SUBNAUTICA2_SCENEVIEW_SINGLE_PASS_STEREO_OFFSET = 0x11F7;
constexpr auto SUBNAUTICA2_SCENEVIEW_UNDERWATER_DEPTH_OFFSET = 0x1200;
constexpr auto SUBNAUTICA2_SCENEVIEW_WATER_INTERSECTION_OFFSET = 0x1204;
constexpr auto SUBNAUTICA2_SCENEVIEW_STEREO_ASPECTS_FLAGS_OFFSET = 0x1BD2;
constexpr auto SUBNAUTICA2_SCENEVIEW_RUNTIME_VIEW_RECT_OFFSET = 0x1C00;
constexpr auto SUBNAUTICA2_SCENEVIEW_VIEWSTATE_OFFSET = 0x1C10;
constexpr auto SUBNAUTICA2_SCENEVIEW_CACHED_VIEW_UNIFORM_OFFSET = 0x1C18;
constexpr auto SUBNAUTICA2_SCENEVIEW_SINGLE_LAYER_WATER_PASS_OFFSET = 0x2098;
constexpr auto SUBNAUTICA2_SCENEVIEW_LOCAL_FOG_VOLUME_VIEW_DATA_OFFSET = 0x2348;
constexpr auto SUBNAUTICA2_SCENEVIEW_VISIBILITY_FLAGS_OFFSET = 0x24C8;
constexpr auto SUBNAUTICA2_SCENEVIEW_SHADER_MAP_OFFSET = 0x26D8;
constexpr uint8_t SUBNAUTICA2_SCENEVIEW_HAS_NO_VISIBLE_PRIMITIVE_MASK = 0x02;
constexpr auto SUBNAUTICA2_CACHED_VIEW_UNIFORM_REFLECTION_MASK_OFFSET = 0x0CCC;
constexpr auto SUBNAUTICA2_CACHED_VIEW_UNIFORM_ENV_COMPONENT_FLAGS_OFFSET = 0x1410;
constexpr auto SUBNAUTICA2_VIEWSTATE_VOLCLOUD_OFFSET = 0x1EA0;
constexpr auto SUBNAUTICA2_VOLCLOUD_HISTORY_AVAILABLE_OFFSET = 0x0D;
constexpr auto SUBNAUTICA2_VOLCLOUD_VALID_OFFSET = 0x0F;
constexpr auto SUBNAUTICA2_VOLCLOUD_PREV_EXPOSURE_OFFSET = 0x10;
constexpr auto SUBNAUTICA2_VOLCLOUD_CURRENT_PIXEL_OFFSET_OFFSET = 0x24;
constexpr auto SUBNAUTICA2_VOLCLOUD_TRACING_RESOLUTION_OFFSET = 0x3C;
constexpr auto SUBNAUTICA2_VOLCLOUD_RECONSTRUCT_VIEW_RECT_OFFSET = 0x44;
constexpr auto SUBNAUTICA2_VOLCLOUD_TRACING_VIEW_RECT_BASE_OFFSET = 0x80;
constexpr auto SUBNAUTICA2_VOLCLOUD_TEXTURE_INDEX_OFFSET = 0x08;
constexpr auto SUBNAUTICA2_VOLCLOUD_MODE_OFFSET = 0xB0;
constexpr auto SUBNAUTICA2_VOLCLOUD_UPSAMPLING_MODE_OFFSET = 0xB4;
constexpr auto SUBNAUTICA2_SCENE_WITHOUT_WATER_COLOR_OFFSET = 0x08;
constexpr auto SUBNAUTICA2_SCENE_WITHOUT_WATER_DEPTH_OFFSET = 0x10;
constexpr auto SUBNAUTICA2_SCENE_WITHOUT_WATER_VIEWS_DATA_OFFSET = 0x18;
constexpr auto SUBNAUTICA2_SCENE_WITHOUT_WATER_VIEWS_COUNT_OFFSET = 0x20;
constexpr auto SUBNAUTICA2_SCENE_WITHOUT_WATER_VIEWS_MAX_OFFSET = 0x24;
constexpr auto SUBNAUTICA2_SCENE_WITHOUT_WATER_REFRACTION_FACTOR_OFFSET = 0x28;

struct Subnautica2SceneWithoutWaterView {
    int32_t rect[4]{};
    float minmax_uv[4]{};
};

bool subnautica2_rect_valid(const int32_t* rect) {
    return rect != nullptr && rect[2] > rect[0] && rect[3] > rect[1];
}

bool subnautica2_rect_equal(const int32_t* a, const int32_t* b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

float subnautica2_absf(float value) {
    return value < 0.0f ? -value : value;
}

bool subnautica2_uv_close(const float* a, const float* b) {
    constexpr float tolerance = 0.0025f;
    return subnautica2_absf(a[0] - b[0]) <= tolerance &&
           subnautica2_absf(a[1] - b[1]) <= tolerance &&
           subnautica2_absf(a[2] - b[2]) <= tolerance &&
           subnautica2_absf(a[3] - b[3]) <= tolerance;
}

int32_t subnautica2_divide_rect_coord(int32_t value, int32_t factor) {
    if (factor <= 1 || value == 0) {
        return value;
    }

    return value / factor;
}

bool subnautica2_get_effective_scene_view_rect(uint8_t* view, int32_t (&out)[4]) {
    if (view == nullptr) {
        return false;
    }

    if (is_readable_process_range(
            (uintptr_t)view + SUBNAUTICA2_SCENEVIEW_RUNTIME_VIEW_RECT_OFFSET,
            sizeof(out)))
    {
        const auto* runtime_rect = (const int32_t*)(view + SUBNAUTICA2_SCENEVIEW_RUNTIME_VIEW_RECT_OFFSET);

        if (subnautica2_rect_valid(runtime_rect)) {
            memcpy(out, runtime_rect, sizeof(out));
            return true;
        }
    }

    if (!is_readable_process_range(
            (uintptr_t)view + SUBNAUTICA2_SCENEVIEW_INIT_OPTIONS_OFFSET,
            sizeof(sdk::FSceneViewInitOptionsUE5)))
    {
        return false;
    }

    const auto* init_options = (const sdk::FSceneViewInitOptionsUE5*)(view + SUBNAUTICA2_SCENEVIEW_INIT_OPTIONS_OFFSET);
    const auto* init_rect = init_options->view_rect;

    if (!subnautica2_rect_valid(init_rect)) {
        return false;
    }

    memcpy(out, init_rect, sizeof(out));
    return true;
}

bool subnautica2_expected_scene_without_water_view(
    uint8_t* view,
    const int32_t* full_rect,
    int32_t downsample_factor,
    Subnautica2SceneWithoutWaterView& out)
{
    if (view == nullptr ||
        full_rect == nullptr ||
        downsample_factor <= 0 ||
        !subnautica2_rect_valid(full_rect))
    {
        return false;
    }

    int32_t view_rect[4]{};

    if (!subnautica2_get_effective_scene_view_rect(view, view_rect)) {
        return false;
    }

    out.rect[0] = subnautica2_divide_rect_coord(view_rect[0], downsample_factor);
    out.rect[1] = subnautica2_divide_rect_coord(view_rect[1], downsample_factor);
    out.rect[2] = subnautica2_divide_rect_coord(view_rect[2], downsample_factor);
    out.rect[3] = subnautica2_divide_rect_coord(view_rect[3], downsample_factor);

    const int32_t extent_x = std::max(1, subnautica2_divide_rect_coord(full_rect[2], downsample_factor));
    const int32_t extent_y = std::max(1, subnautica2_divide_rect_coord(full_rect[3], downsample_factor));
    constexpr float pixel_safe_guard_band = 0.55f;

    out.minmax_uv[0] = ((float)out.rect[0] + pixel_safe_guard_band) / (float)extent_x;
    out.minmax_uv[1] = ((float)out.rect[1] + pixel_safe_guard_band) / (float)extent_y;
    out.minmax_uv[2] = ((float)out.rect[2] - pixel_safe_guard_band) / (float)extent_x;
    out.minmax_uv[3] = ((float)out.rect[3] - pixel_safe_guard_band) / (float)extent_y;
    return true;
}

struct Subnautica2RuntimeViewRectPatch {
    uint8_t* view{};
    int32_t original[4]{};
    int32_t desired[4]{};
    bool valid{};
    bool patched{};
};

struct Subnautica2PrimaryViewIndexPatch {
    uint8_t* view{};
    int32_t original{};
    int32_t desired{};
    bool valid{};
    bool patched{};
};

struct Subnautica2ArrayViewPatch {
    void* view{};
    uintptr_t original_data{};
    int32_t original_count{};
    uintptr_t replay_data{};
    bool valid{};
    bool patched{};
};

bool subnautica2_patch_array_view_to_secondary(void* views, Subnautica2ArrayViewPatch& patch) {
    if (views == nullptr ||
        !is_writable_process_range((uintptr_t)views, sizeof(uintptr_t) + sizeof(int32_t)))
    {
        return false;
    }

    auto* const data_ptr = (uintptr_t*)views;
    auto* const count_ptr = (int32_t*)((uintptr_t)views + sizeof(uintptr_t));
    const auto original_data = *data_ptr;
    const auto original_count = *count_ptr;

    if (original_data == 0 ||
        original_count < 2 ||
        original_count > 8 ||
        !is_readable_process_range(original_data, SUBNAUTICA2_SCENEVIEW_STRIDE * 2))
    {
        return false;
    }

    patch.view = views;
    patch.original_data = original_data;
    patch.original_count = original_count;
    patch.replay_data = original_data + SUBNAUTICA2_SCENEVIEW_STRIDE;
    patch.valid = true;

    *data_ptr = patch.replay_data;
    *count_ptr = 1;
    patch.patched = true;
    return true;
}

void subnautica2_restore_array_view_patch(const Subnautica2ArrayViewPatch& patch) {
    if (!patch.valid || !patch.patched || patch.view == nullptr ||
        !is_writable_process_range((uintptr_t)patch.view, sizeof(uintptr_t) + sizeof(int32_t)))
    {
        return;
    }

    *(uintptr_t*)patch.view = patch.original_data;
    *(int32_t*)((uintptr_t)patch.view + sizeof(uintptr_t)) = patch.original_count;
}

bool subnautica2_patch_runtime_view_rects(
    const char* source,
    void* views,
    std::array<Subnautica2RuntimeViewRectPatch, 2>& patches)
{
    auto& vr = VR::get();

    if (!subnautica2_is_current_game() ||
        subnautica2_disable_single_layer_water_view_rect_fix() ||
        vr == nullptr ||
        !vr->is_hmd_active() ||
        !vr->is_native_stereo_fix_enabled() ||
        vr->is_native_stereo_fix_same_pass_enabled() ||
        vr->is_using_afr() ||
        views == nullptr)
    {
        return false;
    }

    const auto views_addr = (uintptr_t)views;

    if (!is_readable_process_range(views_addr, sizeof(uintptr_t) + sizeof(int32_t))) {
        return false;
    }

    auto* const views_data = *(uint8_t**)views_addr;
    const auto views_count = *(int32_t*)(views_addr + sizeof(uintptr_t));

    if (views_data == nullptr ||
        views_count < 2 ||
        views_count > 8 ||
        !is_writable_process_range((uintptr_t)views_data, SUBNAUTICA2_SCENEVIEW_STRIDE * 2))
    {
        return false;
    }

    bool patched_any = false;
    bool mismatch_any = false;

    for (int32_t eye = 0; eye < 2; ++eye) {
        auto& patch = patches[eye];
        patch.view = views_data + (SUBNAUTICA2_SCENEVIEW_STRIDE * eye);

        if (!is_readable_process_range(
                (uintptr_t)patch.view + SUBNAUTICA2_SCENEVIEW_INIT_OPTIONS_OFFSET,
                sizeof(sdk::FSceneViewInitOptionsUE5)) ||
            !is_writable_process_range(
                (uintptr_t)patch.view + SUBNAUTICA2_SCENEVIEW_RUNTIME_VIEW_RECT_OFFSET,
                sizeof(patch.original)))
        {
            continue;
        }

        const auto* init_options = (const sdk::FSceneViewInitOptionsUE5*)(patch.view + SUBNAUTICA2_SCENEVIEW_INIT_OPTIONS_OFFSET);
        const auto* init_rect = init_options->view_rect;
        auto* const runtime_rect = (int32_t*)(patch.view + SUBNAUTICA2_SCENEVIEW_RUNTIME_VIEW_RECT_OFFSET);

        if (!subnautica2_rect_valid(init_rect)) {
            continue;
        }

        memcpy(patch.original, runtime_rect, sizeof(patch.original));
        memcpy(patch.desired, init_rect, sizeof(patch.desired));
        patch.valid = true;

        const bool rect_mismatch = !subnautica2_rect_equal(patch.original, patch.desired);
        mismatch_any = mismatch_any || rect_mismatch;

        if (rect_mismatch) {
            memcpy(runtime_rect, patch.desired, sizeof(patch.desired));
            patch.patched = true;
            patched_any = true;
        }
    }

    static std::atomic<uint64_t> logged_calls{0};
    const auto count = logged_calls.fetch_add(1, std::memory_order_relaxed);

    if (count < 48 || patched_any || ((count % 600) == 0 && mismatch_any)) {
        SPDLOG_INFO(
            "[Subnautica2][ViewRect] {} call={} count={} patched={} mismatch={} disabled={} eye0(valid={}, view={:x}, init={} {} {} {}, runtime={} {} {} {}, after={} {} {} {}) eye1(valid={}, view={:x}, init={} {} {} {}, runtime={} {} {} {}, after={} {} {} {})",
            source,
            count + 1,
            views_count,
            patched_any,
            mismatch_any,
            subnautica2_disable_single_layer_water_view_rect_fix(),
            patches[0].valid,
            (uintptr_t)patches[0].view,
            patches[0].desired[0],
            patches[0].desired[1],
            patches[0].desired[2],
            patches[0].desired[3],
            patches[0].original[0],
            patches[0].original[1],
            patches[0].original[2],
            patches[0].original[3],
            patches[0].patched ? patches[0].desired[0] : patches[0].original[0],
            patches[0].patched ? patches[0].desired[1] : patches[0].original[1],
            patches[0].patched ? patches[0].desired[2] : patches[0].original[2],
            patches[0].patched ? patches[0].desired[3] : patches[0].original[3],
            patches[1].valid,
            (uintptr_t)patches[1].view,
            patches[1].desired[0],
            patches[1].desired[1],
            patches[1].desired[2],
            patches[1].desired[3],
            patches[1].original[0],
            patches[1].original[1],
            patches[1].original[2],
            patches[1].original[3],
            patches[1].patched ? patches[1].desired[0] : patches[1].original[0],
            patches[1].patched ? patches[1].desired[1] : patches[1].original[1],
            patches[1].patched ? patches[1].desired[2] : patches[1].original[2],
            patches[1].patched ? patches[1].desired[3] : patches[1].original[3]);
    }

    return patched_any;
}

void subnautica2_restore_runtime_view_rects(const std::array<Subnautica2RuntimeViewRectPatch, 2>& patches) {
    for (const auto& patch : patches) {
        if (!patch.valid || !patch.patched || patch.view == nullptr) {
            continue;
        }

        auto* const runtime_rect = (int32_t*)(patch.view + SUBNAUTICA2_SCENEVIEW_RUNTIME_VIEW_RECT_OFFSET);
        if (is_writable_process_range((uintptr_t)runtime_rect, sizeof(patch.original))) {
            memcpy(runtime_rect, patch.original, sizeof(patch.original));
        }
    }
}

bool subnautica2_patch_renderer_runtime_view_rects_for_fog(
    const char* source,
    void* scene_renderer,
    std::array<Subnautica2RuntimeViewRectPatch, 2>& patches)
{
    auto& vr = VR::get();

    if (!subnautica2_is_current_game() ||
        subnautica2_disable_render_fog_view_rect_fix() ||
        vr == nullptr ||
        !vr->is_hmd_active() ||
        !vr->is_native_stereo_fix_enabled() ||
        vr->is_native_stereo_fix_same_pass_enabled() ||
        vr->is_using_afr() ||
        scene_renderer == nullptr)
    {
        return false;
    }

    const auto renderer = (uintptr_t)scene_renderer;
    if (!is_readable_process_range(renderer + SUBNAUTICA2_SCENERENDERER_VIEWS_DATA_OFFSET, sizeof(uintptr_t) + sizeof(int32_t) * 2)) {
        return false;
    }

    auto* const views_data = *(uint8_t**)(renderer + SUBNAUTICA2_SCENERENDERER_VIEWS_DATA_OFFSET);
    const auto views_count = *(int32_t*)(renderer + SUBNAUTICA2_SCENERENDERER_VIEWS_COUNT_OFFSET);
    const auto views_max = *(int32_t*)(renderer + SUBNAUTICA2_SCENERENDERER_VIEWS_MAX_OFFSET);

    if (views_data == nullptr ||
        views_count < 2 ||
        views_count > 8 ||
        views_max < views_count ||
        !is_writable_process_range((uintptr_t)views_data, SUBNAUTICA2_SCENEVIEW_STRIDE * 2))
    {
        SPDLOG_INFO_EVERY_N_SEC(
            5,
            "[Subnautica2][RenderFogViewRect] {} skipping patch; views={} count={}/{} disabled={}",
            source,
            (uintptr_t)views_data,
            views_count,
            views_max,
            subnautica2_disable_render_fog_view_rect_fix());
        return false;
    }

    bool patched_any = false;
    bool mismatch_any = false;

    for (int32_t eye = 0; eye < 2; ++eye) {
        auto& patch = patches[eye];
        patch.view = views_data + (SUBNAUTICA2_SCENEVIEW_STRIDE * eye);

        if (!is_readable_process_range(
                (uintptr_t)patch.view + SUBNAUTICA2_SCENEVIEW_INIT_OPTIONS_OFFSET,
                sizeof(sdk::FSceneViewInitOptionsUE5)) ||
            !is_writable_process_range(
                (uintptr_t)patch.view + SUBNAUTICA2_SCENEVIEW_RUNTIME_VIEW_RECT_OFFSET,
                sizeof(patch.original)))
        {
            continue;
        }

        const auto* init_options = (const sdk::FSceneViewInitOptionsUE5*)(patch.view + SUBNAUTICA2_SCENEVIEW_INIT_OPTIONS_OFFSET);
        const auto* init_rect = init_options->view_rect;
        auto* const runtime_rect = (int32_t*)(patch.view + SUBNAUTICA2_SCENEVIEW_RUNTIME_VIEW_RECT_OFFSET);

        if (!subnautica2_rect_valid(init_rect) || !subnautica2_rect_valid(runtime_rect)) {
            continue;
        }

        memcpy(patch.original, runtime_rect, sizeof(patch.original));
        memcpy(patch.desired, init_rect, sizeof(patch.desired));
        patch.valid = true;

        const bool rect_mismatch = !subnautica2_rect_equal(patch.original, patch.desired);
        mismatch_any = mismatch_any || rect_mismatch;

        if (rect_mismatch) {
            memcpy(runtime_rect, patch.desired, sizeof(patch.desired));
            patch.patched = true;
            patched_any = true;
        }
    }

    static std::atomic<uint64_t> logged_calls{0};
    const auto count = logged_calls.fetch_add(1, std::memory_order_relaxed);
    const auto log_max = sn2_render_fog_view_rect_log_max();

    if (count < log_max) {
        SPDLOG_INFO(
            "[Subnautica2][RenderFogViewRect] {} call={} views={} count={}/{} patched={} mismatch={} eye0(valid={}, view={:x}, pass={}, stereo_index={}, primary_index={}, fog_flag={}, init={} {} {} {}, runtime_before={} {} {} {}, runtime_used={} {} {} {}) eye1(valid={}, view={:x}, pass={}, stereo_index={}, primary_index={}, fog_flag={}, init={} {} {} {}, runtime_before={} {} {} {}, runtime_used={} {} {} {})",
            source,
            count + 1,
            (uintptr_t)views_data,
            views_count,
            views_max,
            patched_any,
            mismatch_any,
            patches[0].valid,
            (uintptr_t)patches[0].view,
            patches[0].valid ? *(uint32_t*)(patches[0].view + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET) : 0,
            patches[0].valid ? *(int32_t*)(patches[0].view + SUBNAUTICA2_SCENEVIEW_STEREO_VIEW_INDEX_OFFSET) : 0,
            patches[0].valid ? *(int32_t*)(patches[0].view + SUBNAUTICA2_SCENEVIEW_PRIMARY_VIEW_INDEX_OFFSET) : 0,
            patches[0].valid ? patches[0].view[SUBNAUTICA2_SCENEVIEW_FOG_RENDER_FLAG_OFFSET] : 0,
            patches[0].desired[0],
            patches[0].desired[1],
            patches[0].desired[2],
            patches[0].desired[3],
            patches[0].original[0],
            patches[0].original[1],
            patches[0].original[2],
            patches[0].original[3],
            patches[0].patched ? patches[0].desired[0] : patches[0].original[0],
            patches[0].patched ? patches[0].desired[1] : patches[0].original[1],
            patches[0].patched ? patches[0].desired[2] : patches[0].original[2],
            patches[0].patched ? patches[0].desired[3] : patches[0].original[3],
            patches[1].valid,
            (uintptr_t)patches[1].view,
            patches[1].valid ? *(uint32_t*)(patches[1].view + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET) : 0,
            patches[1].valid ? *(int32_t*)(patches[1].view + SUBNAUTICA2_SCENEVIEW_STEREO_VIEW_INDEX_OFFSET) : 0,
            patches[1].valid ? *(int32_t*)(patches[1].view + SUBNAUTICA2_SCENEVIEW_PRIMARY_VIEW_INDEX_OFFSET) : 0,
            patches[1].valid ? patches[1].view[SUBNAUTICA2_SCENEVIEW_FOG_RENDER_FLAG_OFFSET] : 0,
            patches[1].desired[0],
            patches[1].desired[1],
            patches[1].desired[2],
            patches[1].desired[3],
            patches[1].original[0],
            patches[1].original[1],
            patches[1].original[2],
            patches[1].original[3],
            patches[1].patched ? patches[1].desired[0] : patches[1].original[0],
            patches[1].patched ? patches[1].desired[1] : patches[1].original[1],
            patches[1].patched ? patches[1].desired[2] : patches[1].original[2],
            patches[1].patched ? patches[1].desired[3] : patches[1].original[3]);
    }

    return patched_any;
}

bool subnautica2_patch_secondary_primary_view_index(
    const char* source,
    void* views,
    Subnautica2PrimaryViewIndexPatch& patch)
{
    auto& vr = VR::get();

    if (!subnautica2_is_current_game() ||
        !subnautica2_force_secondary_primary_view_index() ||
        vr == nullptr ||
        !vr->is_hmd_active() ||
        !vr->is_native_stereo_fix_enabled() ||
        vr->is_native_stereo_fix_same_pass_enabled() ||
        vr->is_using_afr() ||
        views == nullptr)
    {
        return false;
    }

    const auto views_addr = (uintptr_t)views;

    if (!is_readable_process_range(views_addr, sizeof(uintptr_t) + sizeof(int32_t))) {
        return false;
    }

    auto* const views_data = *(uint8_t**)views_addr;
    const auto views_count = *(int32_t*)(views_addr + sizeof(uintptr_t));

    if (views_data == nullptr ||
        views_count < 2 ||
        views_count > 8 ||
        !is_writable_process_range(
            (uintptr_t)views_data + SUBNAUTICA2_SCENEVIEW_STRIDE + SUBNAUTICA2_SCENEVIEW_PRIMARY_VIEW_INDEX_OFFSET,
            sizeof(int32_t)))
    {
        return false;
    }

    auto* const secondary_view = views_data + SUBNAUTICA2_SCENEVIEW_STRIDE;
    const auto secondary_pass = *(uint32_t*)(secondary_view + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET);
    const auto secondary_stereo_index = *(int32_t*)(secondary_view + SUBNAUTICA2_SCENEVIEW_STEREO_VIEW_INDEX_OFFSET);
    auto& primary_view_index = *(int32_t*)(secondary_view + SUBNAUTICA2_SCENEVIEW_PRIMARY_VIEW_INDEX_OFFSET);

    if (secondary_pass != EStereoscopicPass::eSSP_SECONDARY ||
        secondary_stereo_index < 0 ||
        primary_view_index == secondary_stereo_index)
    {
        return false;
    }

    patch.view = secondary_view;
    patch.original = primary_view_index;
    patch.desired = secondary_stereo_index;
    patch.valid = true;

    primary_view_index = secondary_stereo_index;
    patch.patched = true;

    static std::atomic<uint64_t> logged_calls{0};
    const auto count = logged_calls.fetch_add(1, std::memory_order_relaxed);
    if (count < 32 || (count % 600) == 0) {
        SPDLOG_WARN(
            "[Subnautica2][SingleLayerWater] {} patching secondary PrimaryViewIndex {} -> {} during water pass",
            source,
            patch.original,
            patch.desired);
    }

    return true;
}

void subnautica2_restore_primary_view_index(const Subnautica2PrimaryViewIndexPatch& patch) {
    if (!patch.valid || !patch.patched || patch.view == nullptr ||
        !is_writable_process_range(
            (uintptr_t)patch.view + SUBNAUTICA2_SCENEVIEW_PRIMARY_VIEW_INDEX_OFFSET,
            sizeof(int32_t)))
    {
        return;
    }

    *(int32_t*)(patch.view + SUBNAUTICA2_SCENEVIEW_PRIMARY_VIEW_INDEX_OFFSET) = patch.original;
}

bool subnautica2_patch_scene_without_water_views(
    const char* source,
    void* scene_renderer,
    void* scene_without_water_textures)
{
    auto& vr = VR::get();

    if (!subnautica2_is_current_game() ||
        vr == nullptr ||
        !vr->is_hmd_active() ||
        !vr->is_native_stereo_fix_enabled() ||
        vr->is_native_stereo_fix_same_pass_enabled() ||
        vr->is_using_afr() ||
        scene_renderer == nullptr ||
        scene_without_water_textures == nullptr)
    {
        return false;
    }

    const auto renderer = (uintptr_t)scene_renderer;

    if (!is_readable_process_range(renderer + SUBNAUTICA2_SCENERENDERER_VIEWS_DATA_OFFSET, sizeof(uintptr_t) + sizeof(int32_t) * 2)) {
        return false;
    }

    auto* const views_data = *(uint8_t**)(renderer + SUBNAUTICA2_SCENERENDERER_VIEWS_DATA_OFFSET);
    const auto views_count = *(int32_t*)(renderer + SUBNAUTICA2_SCENERENDERER_VIEWS_COUNT_OFFSET);
    const auto views_max = *(int32_t*)(renderer + SUBNAUTICA2_SCENERENDERER_VIEWS_MAX_OFFSET);

    const auto water = (uintptr_t)scene_without_water_textures;

    if (!is_readable_process_range(water + SUBNAUTICA2_SCENE_WITHOUT_WATER_COLOR_OFFSET, 0x30)) {
        return false;
    }

    auto* const water_views = *(Subnautica2SceneWithoutWaterView**)(water + SUBNAUTICA2_SCENE_WITHOUT_WATER_VIEWS_DATA_OFFSET);
    const auto water_views_count = *(int32_t*)(water + SUBNAUTICA2_SCENE_WITHOUT_WATER_VIEWS_COUNT_OFFSET);
    const auto water_views_max = *(int32_t*)(water + SUBNAUTICA2_SCENE_WITHOUT_WATER_VIEWS_MAX_OFFSET);
    const auto color_texture = *(uintptr_t*)(water + SUBNAUTICA2_SCENE_WITHOUT_WATER_COLOR_OFFSET);
    const auto depth_texture = *(uintptr_t*)(water + SUBNAUTICA2_SCENE_WITHOUT_WATER_DEPTH_OFFSET);
    const auto refraction_factor_raw = *(float*)(water + SUBNAUTICA2_SCENE_WITHOUT_WATER_REFRACTION_FACTOR_OFFSET);

    if (views_data == nullptr ||
        views_count < 2 ||
        views_count > 8 ||
        views_max < views_count ||
        water_views == nullptr ||
        water_views_count < 2 ||
        water_views_count > 8 ||
        water_views_max < water_views_count ||
        !is_readable_process_range((uintptr_t)views_data, SUBNAUTICA2_SCENEVIEW_STRIDE * 2) ||
        !is_writable_process_range((uintptr_t)water_views, sizeof(Subnautica2SceneWithoutWaterView) * 2))
    {
        SPDLOG_INFO_EVERY_N_SEC(
            5,
            "[Subnautica2][SceneWithoutWater] {} skipping probe; renderer_views={} count={}/{} water_views={} count={}/{}",
            source,
            (uintptr_t)views_data,
            views_count,
            views_max,
            (uintptr_t)water_views,
            water_views_count,
            water_views_max);
        return false;
    }

    int32_t full_rect[4]{0, 0, 0, 0};
    int32_t downsample_factor = 1;
    if (refraction_factor_raw >= 1.5f && refraction_factor_raw <= 8.5f) {
        downsample_factor = (int32_t)(refraction_factor_raw + 0.5f);
    }

    std::array<Subnautica2SceneWithoutWaterView, 2> expected{};
    bool expected_valid[2]{};

    for (int32_t eye = 0; eye < 2; ++eye) {
        auto* const view = views_data + (SUBNAUTICA2_SCENEVIEW_STRIDE * eye);

        if (!is_readable_process_range(
                (uintptr_t)view + SUBNAUTICA2_SCENEVIEW_INIT_OPTIONS_OFFSET,
                sizeof(sdk::FSceneViewInitOptionsUE5)))
        {
            continue;
        }

        int32_t rect[4]{};

        if (!subnautica2_get_effective_scene_view_rect(view, rect)) {
            continue;
        }

        full_rect[0] = std::min(full_rect[0], rect[0]);
        full_rect[1] = std::min(full_rect[1], rect[1]);
        full_rect[2] = std::max(full_rect[2], rect[2]);
        full_rect[3] = std::max(full_rect[3], rect[3]);
    }

    for (int32_t eye = 0; eye < 2; ++eye) {
        expected_valid[eye] = subnautica2_expected_scene_without_water_view(
            views_data + (SUBNAUTICA2_SCENEVIEW_STRIDE * eye),
            full_rect,
            downsample_factor,
            expected[eye]);
    }

    bool patched_any = false;
    bool mismatch_any = false;

    if (!subnautica2_disable_underwater_fog_view_data_fix()) {
        for (int32_t eye = 0; eye < 2; ++eye) {
            if (!expected_valid[eye]) {
                continue;
            }

            auto& current = water_views[eye];
            const bool rect_mismatch = !subnautica2_rect_equal(current.rect, expected[eye].rect);
            const bool uv_mismatch = !subnautica2_uv_close(current.minmax_uv, expected[eye].minmax_uv);

            mismatch_any = mismatch_any || rect_mismatch || uv_mismatch;

            if (rect_mismatch || uv_mismatch) {
                current = expected[eye];
                patched_any = true;
            }
        }
    } else {
        for (int32_t eye = 0; eye < 2; ++eye) {
            if (!expected_valid[eye]) {
                continue;
            }

            mismatch_any = mismatch_any ||
                !subnautica2_rect_equal(water_views[eye].rect, expected[eye].rect) ||
                !subnautica2_uv_close(water_views[eye].minmax_uv, expected[eye].minmax_uv);
        }
    }

    static std::atomic<uint64_t> logged_calls{0};
    const auto count = logged_calls.fetch_add(1, std::memory_order_relaxed);
    const uint64_t water_context_id = count + 1;
    g_subnautica2_water_context_id.store(water_context_id, std::memory_order_relaxed);
    g_subnautica2_water_context_views.store((uintptr_t)views_data, std::memory_order_relaxed);
    g_subnautica2_water_context_color.store(color_texture, std::memory_order_relaxed);
    g_subnautica2_water_context_depth.store(depth_texture, std::memory_order_relaxed);
    const auto log_max = sn2_scene_without_water_log_max();
    if (count < log_max) {
        SPDLOG_INFO(
            "[Subnautica2][SceneWithoutWater] {} water_ctx={} call={} renderer_views={} count={}/{} water_views={} count={}/{} color={:x} depth={:x} refraction={} downsample={} full_rect={} {} {} {} patched={} mismatch={}",
            source,
            water_context_id,
            count + 1,
            (uintptr_t)views_data,
            views_count,
            views_max,
            (uintptr_t)water_views,
            water_views_count,
            water_views_max,
            color_texture,
            depth_texture,
            refraction_factor_raw,
            downsample_factor,
            full_rect[0],
            full_rect[1],
            full_rect[2],
            full_rect[3],
            patched_any,
            mismatch_any);

        for (int32_t eye = 0; eye < 2; ++eye) {
            auto* const view = views_data + (SUBNAUTICA2_SCENEVIEW_STRIDE * eye);
            const auto* init_options = (const sdk::FSceneViewInitOptionsUE5*)(view + SUBNAUTICA2_SCENEVIEW_INIT_OPTIONS_OFFSET);
            const auto* runtime_view_rect = (const int32_t*)(view + SUBNAUTICA2_SCENEVIEW_RUNTIME_VIEW_RECT_OFFSET);
            const auto& current = water_views[eye];

            SPDLOG_INFO(
                "[Subnautica2][SceneWithoutWater] {} water_ctx={} eye={} view={:x} pass={} stereo_index={} primary_index={} fog_flag={} instanced={} singlepass={} multiviewport={} mobile_multiview={} bind_instanced_ub={} underwater_depth={} water_intersection={} init_rect={} {} {} {} runtime_rect={} {} {} {} current_rect={} {} {} {} current_uv={:.6f} {:.6f} {:.6f} {:.6f} expected_valid={} expected_rect={} {} {} {} expected_uv={:.6f} {:.6f} {:.6f} {:.6f}",
                source,
                water_context_id,
                eye,
                (uintptr_t)view,
                *(uint32_t*)(view + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET),
                *(int32_t*)(view + SUBNAUTICA2_SCENEVIEW_STEREO_VIEW_INDEX_OFFSET),
                *(int32_t*)(view + SUBNAUTICA2_SCENEVIEW_PRIMARY_VIEW_INDEX_OFFSET),
                view[SUBNAUTICA2_SCENEVIEW_FOG_RENDER_FLAG_OFFSET],
                view[SUBNAUTICA2_SCENEVIEW_INSTANCED_STEREO_ENABLED_OFFSET],
                view[SUBNAUTICA2_SCENEVIEW_SINGLE_PASS_STEREO_OFFSET],
                view[SUBNAUTICA2_SCENEVIEW_MULTI_VIEWPORT_ENABLED_OFFSET],
                view[SUBNAUTICA2_SCENEVIEW_MOBILE_MULTI_VIEW_ENABLED_OFFSET],
                view[SUBNAUTICA2_SCENEVIEW_SHOULD_BIND_INSTANCED_VIEW_UB_OFFSET],
                *(float*)(view + SUBNAUTICA2_SCENEVIEW_UNDERWATER_DEPTH_OFFSET),
                view[SUBNAUTICA2_SCENEVIEW_WATER_INTERSECTION_OFFSET],
                init_options->view_rect[0],
                init_options->view_rect[1],
                init_options->view_rect[2],
                init_options->view_rect[3],
                runtime_view_rect[0],
                runtime_view_rect[1],
                runtime_view_rect[2],
                runtime_view_rect[3],
                current.rect[0],
                current.rect[1],
                current.rect[2],
                current.rect[3],
                current.minmax_uv[0],
                current.minmax_uv[1],
                current.minmax_uv[2],
                current.minmax_uv[3],
                expected_valid[eye],
                expected[eye].rect[0],
                expected[eye].rect[1],
                expected[eye].rect[2],
                expected[eye].rect[3],
                expected[eye].minmax_uv[0],
                expected[eye].minmax_uv[1],
                expected[eye].minmax_uv[2],
                expected[eye].minmax_uv[3]);
        }
    }

    return patched_any;
}

void subnautica2_log_compose_volumetric_render_target(
    const char* source,
    void* views,
    bool compose_with_water,
    void* water_pass_data)
{
    if (!subnautica2_is_current_game() || views == nullptr) {
        return;
    }

    const auto views_addr = (uintptr_t)views;
    if (!is_readable_process_range(views_addr, sizeof(uintptr_t) + sizeof(int32_t))) {
        SPDLOG_INFO_EVERY_N_SEC(
            5,
            "[Subnautica2][ComposeVolumetric] {} skipping probe; bad views header {:x}",
            source,
            views_addr);
        return;
    }

    auto* const views_data = *(uint8_t**)views_addr;
    const auto views_count = *(int32_t*)(views_addr + sizeof(uintptr_t));

    Subnautica2SceneWithoutWaterView* water_views = nullptr;
    int32_t water_views_count = 0;
    uintptr_t water_color = 0;
    uintptr_t water_depth = 0;
    float refraction_factor = 0.0f;

    if (compose_with_water && water_pass_data != nullptr) {
        const auto water = (uintptr_t)water_pass_data;
        if (is_readable_process_range(water + SUBNAUTICA2_SCENE_WITHOUT_WATER_COLOR_OFFSET, 0x30)) {
            water_color = *(uintptr_t*)(water + SUBNAUTICA2_SCENE_WITHOUT_WATER_COLOR_OFFSET);
            water_depth = *(uintptr_t*)(water + SUBNAUTICA2_SCENE_WITHOUT_WATER_DEPTH_OFFSET);
            water_views = *(Subnautica2SceneWithoutWaterView**)(water + SUBNAUTICA2_SCENE_WITHOUT_WATER_VIEWS_DATA_OFFSET);
            water_views_count = *(int32_t*)(water + SUBNAUTICA2_SCENE_WITHOUT_WATER_VIEWS_COUNT_OFFSET);
            refraction_factor = *(float*)(water + SUBNAUTICA2_SCENE_WITHOUT_WATER_REFRACTION_FACTOR_OFFSET);
        }
    }

    if (views_data == nullptr ||
        views_count <= 0 ||
        views_count > 8 ||
        !is_readable_process_range((uintptr_t)views_data, SUBNAUTICA2_SCENEVIEW_STRIDE * std::min(views_count, 2)))
    {
        SPDLOG_INFO_EVERY_N_SEC(
            5,
            "[Subnautica2][ComposeVolumetric] {} skipping probe; views_data={} count={} water_views={} water_count={} compose_with_water={}",
            source,
            (uintptr_t)views_data,
            views_count,
            (uintptr_t)water_views,
            water_views_count,
            compose_with_water);
        return;
    }

    uintptr_t viewstate[2]{};
    uintptr_t cached_uniform[2]{};
    uintptr_t volc_state[2]{};
    bool volc_valid[2]{};
    uint32_t volc_mode[2]{};
    uint32_t volc_upsampling[2]{};
    uint32_t local_fog_count[2]{};
    uint32_t volc_texture_index[2]{};
    bool volc_history_available[2]{};
    float volc_prev_exposure[2]{};
    int32_t volc_pixel_offset[2][2]{};
    int32_t volc_trace_resolution[2][2]{};
    int32_t volc_reconstruct_rect[2][2]{};
    int32_t volc_tracing_rect[2][2]{};
    uintptr_t view_lightscat_2650[2]{};
    uintptr_t view_lightscat_2658[2]{};
    uintptr_t view_lightscat_2660[2]{};

    for (int32_t eye = 0; eye < std::min(views_count, 2); ++eye) {
        auto* const view = views_data + (SUBNAUTICA2_SCENEVIEW_STRIDE * eye);

        if (!is_readable_process_range((uintptr_t)view + SUBNAUTICA2_SCENEVIEW_VIEWSTATE_OFFSET, sizeof(uintptr_t) * 2)) {
            continue;
        }

        viewstate[eye] = *(uintptr_t*)(view + SUBNAUTICA2_SCENEVIEW_VIEWSTATE_OFFSET);
        cached_uniform[eye] = *(uintptr_t*)(view + SUBNAUTICA2_SCENEVIEW_CACHED_VIEW_UNIFORM_OFFSET);
        view_lightscat_2650[eye] = is_readable_process_range((uintptr_t)view + 0x2650, 8)
            ? *(uintptr_t*)(view + 0x2650) : 0;
        view_lightscat_2658[eye] = is_readable_process_range((uintptr_t)view + 0x2658, 8)
            ? *(uintptr_t*)(view + 0x2658) : 0;
        view_lightscat_2660[eye] = is_readable_process_range((uintptr_t)view + 0x2660, 8)
            ? *(uintptr_t*)(view + 0x2660) : 0;

        if (viewstate[eye] != 0 &&
            is_readable_process_range(viewstate[eye] + SUBNAUTICA2_VIEWSTATE_VOLCLOUD_OFFSET, SUBNAUTICA2_VOLCLOUD_UPSAMPLING_MODE_OFFSET + sizeof(uint32_t)))
        {
            volc_state[eye] = viewstate[eye] + SUBNAUTICA2_VIEWSTATE_VOLCLOUD_OFFSET;
            volc_valid[eye] = *(uint8_t*)(volc_state[eye] + SUBNAUTICA2_VOLCLOUD_VALID_OFFSET) != 0;
            volc_history_available[eye] = *(uint8_t*)(volc_state[eye] + SUBNAUTICA2_VOLCLOUD_HISTORY_AVAILABLE_OFFSET) != 0;
            volc_texture_index[eye] = *(uint32_t*)(volc_state[eye] + SUBNAUTICA2_VOLCLOUD_TEXTURE_INDEX_OFFSET);
            volc_prev_exposure[eye] = *(float*)(volc_state[eye] + SUBNAUTICA2_VOLCLOUD_PREV_EXPOSURE_OFFSET);
            memcpy(volc_pixel_offset[eye], (void*)(volc_state[eye] + SUBNAUTICA2_VOLCLOUD_CURRENT_PIXEL_OFFSET_OFFSET), sizeof(volc_pixel_offset[eye]));
            memcpy(volc_trace_resolution[eye], (void*)(volc_state[eye] + SUBNAUTICA2_VOLCLOUD_TRACING_RESOLUTION_OFFSET), sizeof(volc_trace_resolution[eye]));
            memcpy(volc_reconstruct_rect[eye], (void*)(volc_state[eye] + SUBNAUTICA2_VOLCLOUD_RECONSTRUCT_VIEW_RECT_OFFSET), sizeof(volc_reconstruct_rect[eye]));
            memcpy(
                volc_tracing_rect[eye],
                (void*)(volc_state[eye] + SUBNAUTICA2_VOLCLOUD_TRACING_VIEW_RECT_BASE_OFFSET + (8 * volc_texture_index[eye])),
                sizeof(volc_tracing_rect[eye]));
            volc_mode[eye] = *(uint32_t*)(volc_state[eye] + SUBNAUTICA2_VOLCLOUD_MODE_OFFSET);
            volc_upsampling[eye] = *(uint32_t*)(volc_state[eye] + SUBNAUTICA2_VOLCLOUD_UPSAMPLING_MODE_OFFSET);
        }

        const auto local_fog_ptr = *(uintptr_t*)(view + SUBNAUTICA2_SCENEVIEW_LOCAL_FOG_VOLUME_VIEW_DATA_OFFSET);
        if (local_fog_ptr != 0 && is_readable_process_range(local_fog_ptr, sizeof(uint32_t))) {
            local_fog_count[eye] = *(uint32_t*)local_fog_ptr;
        }
    }

    const bool shared_viewstate = views_count >= 2 && viewstate[0] != 0 && viewstate[0] == viewstate[1];
    const bool shared_volc_state = views_count >= 2 && volc_state[0] != 0 && volc_state[0] == volc_state[1];
    const bool asymmetric_volc =
        views_count >= 2 &&
        (volc_valid[0] != volc_valid[1] ||
         volc_mode[0] != volc_mode[1] ||
         volc_upsampling[0] != volc_upsampling[1] ||
         volc_texture_index[0] != volc_texture_index[1] ||
         volc_history_available[0] != volc_history_available[1] ||
         volc_pixel_offset[0][0] != volc_pixel_offset[1][0] ||
         volc_pixel_offset[0][1] != volc_pixel_offset[1][1] ||
         volc_trace_resolution[0][0] != volc_trace_resolution[1][0] ||
         volc_trace_resolution[0][1] != volc_trace_resolution[1][1] ||
         volc_reconstruct_rect[0][0] != volc_reconstruct_rect[1][0] ||
         volc_reconstruct_rect[0][1] != volc_reconstruct_rect[1][1] ||
         volc_tracing_rect[0][0] != volc_tracing_rect[1][0] ||
         volc_tracing_rect[0][1] != volc_tracing_rect[1][1] ||
         local_fog_count[0] != local_fog_count[1]);

    static std::atomic<uint64_t> logged_calls{0};
    const auto count = logged_calls.fetch_add(1, std::memory_order_relaxed);
    const auto compose_log_max = sn2_compose_volumetric_log_max();
    if (count >= compose_log_max) {
        return;
    }
    if (count >= 120 && !shared_viewstate && !shared_volc_state && !asymmetric_volc && (count % 900) != 0) {
        return;
    }

    SPDLOG_INFO(
        "[Subnautica2][ComposeVolumetric] {} call={} views={} count={} compose_with_water={} water_views={} water_count={} water_color={:x} water_depth={:x} refraction={} shared_viewstate={} shared_volc={} asymmetric_volc={}",
        source,
        count + 1,
        (uintptr_t)views_data,
        views_count,
        compose_with_water,
        (uintptr_t)water_views,
        water_views_count,
        water_color,
        water_depth,
        refraction_factor,
        shared_viewstate,
        shared_volc_state,
        asymmetric_volc);

    for (int32_t eye = 0; eye < std::min(views_count, 2); ++eye) {
        auto* const view = views_data + (SUBNAUTICA2_SCENEVIEW_STRIDE * eye);
        const auto* init_options = (const sdk::FSceneViewInitOptionsUE5*)(view + SUBNAUTICA2_SCENEVIEW_INIT_OPTIONS_OFFSET);
        const auto* runtime_view_rect = (const int32_t*)(view + SUBNAUTICA2_SCENEVIEW_RUNTIME_VIEW_RECT_OFFSET);
        const auto* water_view = (compose_with_water &&
                                  water_views != nullptr &&
                                  water_views_count > eye &&
                                  is_readable_process_range((uintptr_t)&water_views[eye], sizeof(Subnautica2SceneWithoutWaterView)))
            ? &water_views[eye]
            : nullptr;

        float reflection_mask = 0.0f;
        uint32_t env_flags = 0;
        if (cached_uniform[eye] != 0 &&
            is_readable_process_range(cached_uniform[eye] + SUBNAUTICA2_CACHED_VIEW_UNIFORM_ENV_COMPONENT_FLAGS_OFFSET, sizeof(uint32_t)))
        {
            reflection_mask = *(float*)(cached_uniform[eye] + SUBNAUTICA2_CACHED_VIEW_UNIFORM_REFLECTION_MASK_OFFSET);
            env_flags = *(uint32_t*)(cached_uniform[eye] + SUBNAUTICA2_CACHED_VIEW_UNIFORM_ENV_COMPONENT_FLAGS_OFFSET);
        }

        SPDLOG_INFO(
            "[Subnautica2][ComposeVolumetric] {} eye={} view={:x} pass={} stereo_index={} primary_index={} family={:x} viewstate={:x} cached_ub={:x} shader_map={:x} fog_flag={} underwater_depth={} water_intersection={} local_fog_count={} lightscat2650={:x} lightscat2658={:x} lightscat2660={:x} reflection_mask={} env_flags=0x{:08x} volc_state={:x} valid={} hist={} tex_index={} prev_exp={} mode={} upsample={} pixel_offset={} {} trace_res={} {} recon_rect={} {} tracing_rect={} {} init_rect={} {} {} {} runtime_rect={} {} {} {} water_rect={} {} {} {} water_uv={:.6f} {:.6f} {:.6f} {:.6f}",
            source,
            eye,
            (uintptr_t)view,
            *(uint32_t*)(view + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET),
            *(int32_t*)(view + SUBNAUTICA2_SCENEVIEW_STEREO_VIEW_INDEX_OFFSET),
            *(int32_t*)(view + SUBNAUTICA2_SCENEVIEW_PRIMARY_VIEW_INDEX_OFFSET),
            *(uintptr_t*)(view + SUBNAUTICA2_SCENEVIEW_FAMILY_OFFSET),
            viewstate[eye],
            cached_uniform[eye],
            *(uintptr_t*)(view + SUBNAUTICA2_SCENEVIEW_SHADER_MAP_OFFSET),
            view[SUBNAUTICA2_SCENEVIEW_FOG_RENDER_FLAG_OFFSET],
            *(float*)(view + SUBNAUTICA2_SCENEVIEW_UNDERWATER_DEPTH_OFFSET),
            view[SUBNAUTICA2_SCENEVIEW_WATER_INTERSECTION_OFFSET],
            local_fog_count[eye],
            view_lightscat_2650[eye],
            view_lightscat_2658[eye],
            view_lightscat_2660[eye],
            reflection_mask,
            env_flags,
            volc_state[eye],
            volc_valid[eye],
            volc_history_available[eye],
            volc_texture_index[eye],
            volc_prev_exposure[eye],
            volc_mode[eye],
            volc_upsampling[eye],
            volc_pixel_offset[eye][0],
            volc_pixel_offset[eye][1],
            volc_trace_resolution[eye][0],
            volc_trace_resolution[eye][1],
            volc_reconstruct_rect[eye][0],
            volc_reconstruct_rect[eye][1],
            volc_tracing_rect[eye][0],
            volc_tracing_rect[eye][1],
            init_options->view_rect[0],
            init_options->view_rect[1],
            init_options->view_rect[2],
            init_options->view_rect[3],
            runtime_view_rect[0],
            runtime_view_rect[1],
            runtime_view_rect[2],
            runtime_view_rect[3],
            water_view != nullptr ? water_view->rect[0] : 0,
            water_view != nullptr ? water_view->rect[1] : 0,
            water_view != nullptr ? water_view->rect[2] : 0,
            water_view != nullptr ? water_view->rect[3] : 0,
            water_view != nullptr ? water_view->minmax_uv[0] : 0.0f,
            water_view != nullptr ? water_view->minmax_uv[1] : 0.0f,
            water_view != nullptr ? water_view->minmax_uv[2] : 0.0f,
            water_view != nullptr ? water_view->minmax_uv[3] : 0.0f);
    }
}

thread_local bool g_subnautica2_force_volumetric_fog_view_index_one = false;

// 2026-05-16 FIX-V2: capture view 1's NATURAL +0x2658 value (the right-eye
// fog texture pointer) before any propagation overwrites it. Used by the
// setup_volumetric_fog_ub_hook to rewrite view 1's UB at the correct offset
// so the basepass material's fog volume binding picks view 1's filtered
// fog (e.g. RD ID 30162) instead of view 0's (RD ID 28166).
static std::atomic<uintptr_t> g_subnautica2_view1_natural_lightscat{0};
// g_subnautica2_view0_lightscat defined outside the anonymous namespace
// near the top of this file (file-external linkage for D3D12Hook to use).
// Empirically discovered offset within the UB fog section where the texture
// handle (8 bytes) lives. Found by scanning view 0's UB+0xFC0..0x1040 for
// the 8-byte sequence matching view 0's +0x2658 value. Captured once per
// session, used to rewrite view 1's UB at the same offset.
static std::atomic<int32_t> g_subnautica2_fog_ub_handle_offset{-1};

void subnautica2_sync_native_stereo_water_state(sdk::FSceneViewFamily& view_family, uint32_t frame_count) {
    auto& vr = VR::get();
    const auto sync_mask = subnautica2_water_state_sync_mask();
    const bool sync_water_values = (sync_mask & 0x1u) != 0;
    const bool sync_fog_flags = (sync_mask & 0x2u) != 0;
    const bool sync_basepass_gate = (sync_mask & 0x4u) != 0;
    const bool sync_basepass_key = (sync_mask & 0x8u) != 0;

    if (!subnautica2_is_current_game() ||
        subnautica2_disable_water_state_sync() ||
        sync_mask == 0 ||
        !vr->is_hmd_active() ||
        !vr->is_native_stereo_fix_enabled() ||
        vr->is_native_stereo_fix_same_pass_enabled())
    {
        return;
    }

    auto views = view_family.get_views();

    if (views == nullptr || views->count < 2 || views->data == nullptr || views->data[0] == nullptr) {
        return;
    }

    auto primary_view = (uint8_t*)views->data[0];

    constexpr size_t REQUIRED_READ_RANGE =
        SUBNAUTICA2_SCENEVIEW_BASEPASS_KEY_DWORD_OFFSET + sizeof(uint32_t);
    static_assert(
        SUBNAUTICA2_SCENEVIEW_FOG_RENDER_FLAGS_START_OFFSET + SUBNAUTICA2_SCENEVIEW_FOG_RENDER_FLAGS_SIZE
            <= REQUIRED_READ_RANGE,
        "fog-render flag block must be covered by sync read range");

    if (!is_readable_process_range((uintptr_t)primary_view, REQUIRED_READ_RANGE)) {
        return;
    }

    const auto primary_pass = *(uint32_t*)(primary_view + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET);

    if (primary_pass != EStereoscopicPass::eSSP_PRIMARY) {
        return;
    }

    const auto primary_depth = *(float*)(primary_view + SUBNAUTICA2_SCENEVIEW_UNDERWATER_DEPTH_OFFSET);
    const auto primary_intersection = *(uint8_t*)(primary_view + SUBNAUTICA2_SCENEVIEW_WATER_INTERSECTION_OFFSET);
    const auto primary_basepass_gate = *(uint8_t*)(primary_view + SUBNAUTICA2_SCENEVIEW_BASEPASS_GATE_BYTE_OFFSET);
    const auto primary_basepass_key = *(uint32_t*)(primary_view + SUBNAUTICA2_SCENEVIEW_BASEPASS_KEY_DWORD_OFFSET);

    uint8_t primary_fog_flags[SUBNAUTICA2_SCENEVIEW_FOG_RENDER_FLAGS_SIZE];
    memcpy(primary_fog_flags,
        primary_view + SUBNAUTICA2_SCENEVIEW_FOG_RENDER_FLAGS_START_OFFSET,
        SUBNAUTICA2_SCENEVIEW_FOG_RENDER_FLAGS_SIZE);

    static uint32_t log_count = 0;
    static uint32_t fog_log_count = 0;
    static uint32_t basepass_log_count = 0;
    const uint32_t sync_log_max = sn2_native_stereo_sync_log_max();

    for (uint32_t i = 1; i < (uint32_t)views->count; ++i) {
        auto secondary_view = (uint8_t*)views->data[i];

        if (secondary_view == nullptr ||
            !is_readable_process_range((uintptr_t)secondary_view, REQUIRED_READ_RANGE) ||
            !is_writable_process_range((uintptr_t)secondary_view, REQUIRED_READ_RANGE))
        {
            continue;
        }

        const auto secondary_pass = *(uint32_t*)(secondary_view + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET);

        if (secondary_pass != EStereoscopicPass::eSSP_SECONDARY) {
            continue;
        }

        if (sync_water_values) {
            auto& secondary_depth = *(float*)(secondary_view + SUBNAUTICA2_SCENEVIEW_UNDERWATER_DEPTH_OFFSET);
            auto& secondary_intersection = *(uint8_t*)(secondary_view + SUBNAUTICA2_SCENEVIEW_WATER_INTERSECTION_OFFSET);
            const auto before_depth = secondary_depth;
            const auto before_intersection = secondary_intersection;

            secondary_depth = primary_depth;
            secondary_intersection = primary_intersection;

            if (log_count < sync_log_max && (before_depth != primary_depth || before_intersection != primary_intersection)) {
                SPDLOG_INFO("[Subnautica2][NativeStereoFix] Synced water state frame={} view={} mask=0x{:x} primary(depth={}, intersection={}) secondary_before(depth={}, intersection={})",
                    frame_count, i, sync_mask, primary_depth, primary_intersection, before_depth, before_intersection);
                ++log_count;
            }
        }

        // Sync the per-view fog-render bool block (0x11E8..0x11ED). These
        // gate which BasePass PSO permutation the material shader-map picks
        // (LEFT lacked the Texture3D5 volumetric-fog binding while RIGHT had
        // it -- different shader compile keys for identical geometry). The
        // existing post-ctor zeroing only touches 0x11F3..0x11F7, so this
        // block was diverging between eyes and selecting different shaders.
        if (sync_fog_flags) {
            uint8_t* secondary_fog_flags_ptr = secondary_view + SUBNAUTICA2_SCENEVIEW_FOG_RENDER_FLAGS_START_OFFSET;
            uint8_t before_fog_flags[SUBNAUTICA2_SCENEVIEW_FOG_RENDER_FLAGS_SIZE];
            memcpy(before_fog_flags, secondary_fog_flags_ptr, SUBNAUTICA2_SCENEVIEW_FOG_RENDER_FLAGS_SIZE);

            const bool fog_flags_differ =
                memcmp(before_fog_flags, primary_fog_flags, SUBNAUTICA2_SCENEVIEW_FOG_RENDER_FLAGS_SIZE) != 0;

            memcpy(secondary_fog_flags_ptr, primary_fog_flags, SUBNAUTICA2_SCENEVIEW_FOG_RENDER_FLAGS_SIZE);

            if (fog_log_count < sync_log_max && fog_flags_differ) {
                SPDLOG_INFO("[Subnautica2][NativeStereoFix] Synced fog-render flags frame={} view={} mask=0x{:x} primary=[{:02x} {:02x} {:02x} {:02x} {:02x} {:02x}] secondary_before=[{:02x} {:02x} {:02x} {:02x} {:02x} {:02x}]",
                    frame_count, i, sync_mask,
                    primary_fog_flags[0], primary_fog_flags[1], primary_fog_flags[2],
                    primary_fog_flags[3], primary_fog_flags[4], primary_fog_flags[5],
                    before_fog_flags[0], before_fog_flags[1], before_fog_flags[2],
                    before_fog_flags[3], before_fog_flags[4], before_fog_flags[5]);
                ++fog_log_count;
            }
        }

        // Sync the FBasePassMeshProcessor-feeding per-view fields. The
        // FBasePassMeshProcessor ctor (called per-view) reads View+0x11D9
        // (byte gate) and View+0x24CC (dword copied into +0x8C of the mesh
        // pass processor). The mesh processor also caches per-view bool
        // flags at +0x78, +0x81, +0x88 which feed into the case-branch
        // shader-type selection inside sub_142631130's case-0 path. To find
        // out which view fields drive those, we widen the sync to cover the
        // entire range around 0x11D9..0x11FF (catches any byte gate adjacent
        // to FOG_RENDER_FLAGS) AND 0x24C8..0x24CF (catches VISIBILITY_FLAGS
        // and BASEPASS_KEY_DWORD).
        if (sync_basepass_gate || sync_basepass_key) {
            auto& secondary_basepass_gate = *(uint8_t*)(secondary_view + SUBNAUTICA2_SCENEVIEW_BASEPASS_GATE_BYTE_OFFSET);
            auto& secondary_basepass_key = *(uint32_t*)(secondary_view + SUBNAUTICA2_SCENEVIEW_BASEPASS_KEY_DWORD_OFFSET);
            const auto before_basepass_gate = secondary_basepass_gate;
            const auto before_basepass_key = secondary_basepass_key;

            if (sync_basepass_gate) {
                secondary_basepass_gate = primary_basepass_gate;
            }

            if (sync_basepass_key) {
                secondary_basepass_key = primary_basepass_key;
            }

            const bool basepass_state_differs =
                (sync_basepass_gate && before_basepass_gate != primary_basepass_gate) ||
                (sync_basepass_key && before_basepass_key != primary_basepass_key);

            if (basepass_log_count < sync_log_max && basepass_state_differs) {
                SPDLOG_INFO("[Subnautica2][NativeStereoFix] Synced BasePass state frame={} view={} mask=0x{:x} primary(gate=0x{:02x} key=0x{:08x}) secondary_before(gate=0x{:02x} key=0x{:08x})",
                    frame_count, i, sync_mask,
                    primary_basepass_gate, primary_basepass_key,
                    before_basepass_gate, before_basepass_key);
                ++basepass_log_count;
            }
        }

    }
}

bool aphelion_is_current_game() {
    static const bool result = []() {
        const auto exe_path = utility::get_module_pathw(utility::get_executable());
        return exe_path &&
            (exe_path->find(L"PIO-WinGDK-Shipping") != std::wstring::npos ||
             exe_path->find(L"PIO-Win64-Shipping") != std::wstring::npos ||
             exe_path->find(L"Aphelion") != std::wstring::npos);
    }();

    return result;
}

bool ark_ascended_is_current_game() {
    static const bool result = []() {
        const auto exe_path = utility::get_module_pathw(utility::get_executable());
        return exe_path &&
            (exe_path->find(L"ArkAscended.exe") != std::wstring::npos ||
             exe_path->find(L"ArkAscended-Win64-Shipping") != std::wstring::npos);
    }();

    return result;
}

bool mechwarrior_clans_is_current_game() {
    static const bool result = []() {
        const auto exe_path = utility::get_module_pathw(utility::get_executable());
        return exe_path &&
            (exe_path->find(L"MechWarrior-Win64-Shipping") != std::wstring::npos ||
             exe_path->find(L"MW5Clans") != std::wstring::npos);
    }();

    return result;
}

bool directive8020_is_current_game() {
    static const bool result = []() {
        const auto exe_path = utility::get_module_pathw(utility::get_executable());
        return exe_path && exe_path->find(L"Directive8020") != std::wstring::npos;
    }();

    return result;
}

bool stalker2_is_current_game() {
    static const bool result = []() {
        const auto exe_path = utility::get_module_pathw(utility::get_executable());
        return exe_path && exe_path->find(L"Stalker2-Win64-Shipping") != std::wstring::npos;
    }();

    return result;
}

bool avowed_is_current_game() {
    static const bool result = []() {
        const auto exe_path = utility::get_module_pathw(utility::get_executable());
        return exe_path && uevr::games::is_avowed_executable_path(*exe_path);
    }();

    return result;
}

void avowed_native_fix_gate_reset(const char* reason) {
    if (!avowed_is_current_game()) {
        return;
    }

    std::scoped_lock _{g_avowed_native_fix_gate_mutex};

    if (g_avowed_native_fix_gate.has_baseline || g_avowed_native_fix_gate.ready || g_avowed_native_fix_gate.stable_frames != 0) {
        SPDLOG_INFO_EVERY_N_SEC(
            2,
            "[Avowed][NativeStereoFix] Resetting render transition gate: {}",
            reason != nullptr ? reason : "<unknown>");
    }

    g_avowed_native_fix_gate = {};
}

uintptr_t avowed_try_get_native_resource(FRHITexture2D* texture) {
    if (!avowed_is_current_game() || texture == nullptr || IsBadReadPtr(texture, sizeof(void*))) {
        return 0;
    }

    try {
        const auto native = texture->get_native_resource();

        if (native == nullptr || IsBadReadPtr(native, sizeof(void*))) {
            return 0;
        }

        return (uintptr_t)native;
    } catch (...) {
        return 0;
    }
}

bool avowed_native_fix_gate_update(
    uintptr_t scene,
    uintptr_t render_target,
    uintptr_t scene_capture_render_target,
    uintptr_t scene_capture_native,
    bool prerequisites_ready,
    uint32_t* out_stable_frames = nullptr,
    uint32_t* out_required_stable_frames = nullptr)
{
    if (!avowed_is_current_game()) {
        return true;
    }

    std::scoped_lock _{g_avowed_native_fix_gate_mutex};

    if (out_stable_frames != nullptr) {
        *out_stable_frames = g_avowed_native_fix_gate.stable_frames;
    }

    if (out_required_stable_frames != nullptr) {
        *out_required_stable_frames = g_avowed_native_fix_gate.required_stable_frames != 0
            ? g_avowed_native_fix_gate.required_stable_frames
            : AVOWED_NATIVE_FIX_STABLE_FRAMES;
    }

    const auto now = std::chrono::steady_clock::now();

    if (g_avowed_native_fix_gate.last_update.time_since_epoch().count() != 0) {
        const auto render_gap = now - g_avowed_native_fix_gate.last_update;

        if (render_gap > AVOWED_NATIVE_FIX_RENDER_GAP) {
            const auto can_fast_reacquire = g_avowed_native_fix_gate.had_ready_baseline || g_avowed_native_fix_gate.ready;
            g_avowed_native_fix_gate.ready = false;
            g_avowed_native_fix_gate.stable_frames = 0;
            g_avowed_native_fix_gate.required_stable_frames =
                can_fast_reacquire ? AVOWED_NATIVE_FIX_FAST_REACQUIRE_STABLE_FRAMES : AVOWED_NATIVE_FIX_STABLE_FRAMES;
            g_avowed_native_fix_gate.fast_reacquire = can_fast_reacquire;
            const auto requested_hold_duration = can_fast_reacquire
                ? std::chrono::steady_clock::duration{AVOWED_NATIVE_FIX_FAST_REACQUIRE_HOLD}
                : std::chrono::steady_clock::duration{AVOWED_NATIVE_FIX_TRANSITION_HOLD};
            g_avowed_native_fix_gate.hold_until = std::max(
                g_avowed_native_fix_gate.hold_until,
                now + requested_hold_duration);

            SPDLOG_INFO_EVERY_N_SEC(
                2,
                "[Avowed][NativeStereoFix] Render gap {:.0f}ms detected; holding one view for transition safety fast_reacquire={}",
                std::chrono::duration<double, std::milli>(render_gap).count(),
                can_fast_reacquire);
        }
    }

    g_avowed_native_fix_gate.last_update = now;

    if (!prerequisites_ready || scene == 0 || render_target == 0 || scene_capture_render_target == 0) {
        if (g_avowed_native_fix_gate.has_baseline || g_avowed_native_fix_gate.ready || g_avowed_native_fix_gate.stable_frames != 0) {
            SPDLOG_INFO_EVERY_N_SEC(
                2,
                "[Avowed][NativeStereoFix] Holding one view while render targets are unavailable scene={:x} target={:x} capture_rt={:x} capture_native={:x} prereqs={}",
                scene,
                render_target,
                scene_capture_render_target,
                scene_capture_native,
                prerequisites_ready);
        }

        if (g_avowed_native_fix_gate.had_ready_baseline || g_avowed_native_fix_gate.ready) {
            if (!g_avowed_native_fix_gate.targets_missing) {
                g_avowed_native_fix_gate.missing_since = now;
            }

            // Inventory/menu transitions briefly remove Avowed's capture target. Keep the last
            // known-good gameplay baseline so reacquiring the same path can use a short gate.
            g_avowed_native_fix_gate.targets_missing = true;
            g_avowed_native_fix_gate.ready = false;
            g_avowed_native_fix_gate.stable_frames = 0;
            g_avowed_native_fix_gate.fast_reacquire = false;
            g_avowed_native_fix_gate.required_stable_frames = AVOWED_NATIVE_FIX_STABLE_FRAMES;
        } else {
            g_avowed_native_fix_gate = {};
        }

        if (out_stable_frames != nullptr) {
            *out_stable_frames = g_avowed_native_fix_gate.stable_frames;
        }

        if (out_required_stable_frames != nullptr) {
            *out_required_stable_frames = g_avowed_native_fix_gate.required_stable_frames != 0
                ? g_avowed_native_fix_gate.required_stable_frames
                : AVOWED_NATIVE_FIX_STABLE_FRAMES;
        }

        return false;
    }

    const auto baseline_changed =
        !g_avowed_native_fix_gate.has_baseline ||
        g_avowed_native_fix_gate.scene != scene ||
        g_avowed_native_fix_gate.render_target != render_target ||
        g_avowed_native_fix_gate.scene_capture_render_target != scene_capture_render_target ||
        g_avowed_native_fix_gate.scene_capture_native != scene_capture_native;

    if (baseline_changed) {
        const auto missing_duration = g_avowed_native_fix_gate.missing_since.time_since_epoch().count() != 0
            ? now - g_avowed_native_fix_gate.missing_since
            : std::chrono::steady_clock::duration{};
        const auto matches_last_ready_path =
            g_avowed_native_fix_gate.had_ready_baseline &&
            g_avowed_native_fix_gate.last_ready_scene == scene &&
            g_avowed_native_fix_gate.last_ready_render_target == render_target;
        const auto can_fast_reacquire =
            g_avowed_native_fix_gate.targets_missing &&
            matches_last_ready_path &&
            missing_duration <= AVOWED_NATIVE_FIX_FAST_REACQUIRE_MAX_MISSING;

        g_avowed_native_fix_gate.scene = scene;
        g_avowed_native_fix_gate.render_target = render_target;
        g_avowed_native_fix_gate.scene_capture_render_target = scene_capture_render_target;
        g_avowed_native_fix_gate.scene_capture_native = scene_capture_native;
        const auto requested_hold_duration = can_fast_reacquire
            ? std::chrono::steady_clock::duration{AVOWED_NATIVE_FIX_FAST_REACQUIRE_HOLD}
            : std::chrono::steady_clock::duration{AVOWED_NATIVE_FIX_TRANSITION_HOLD};
        const auto requested_hold_until = now + requested_hold_duration;
        g_avowed_native_fix_gate.hold_until = can_fast_reacquire
            ? requested_hold_until
            : std::max(g_avowed_native_fix_gate.hold_until, requested_hold_until);
        g_avowed_native_fix_gate.stable_frames = 0;
        g_avowed_native_fix_gate.required_stable_frames =
            can_fast_reacquire ? AVOWED_NATIVE_FIX_FAST_REACQUIRE_STABLE_FRAMES : AVOWED_NATIVE_FIX_STABLE_FRAMES;
        g_avowed_native_fix_gate.ready = false;
        g_avowed_native_fix_gate.has_baseline = true;
        g_avowed_native_fix_gate.targets_missing = false;
        g_avowed_native_fix_gate.missing_since = {};
        g_avowed_native_fix_gate.fast_reacquire = can_fast_reacquire;

        SPDLOG_INFO_EVERY_N_SEC(
            2,
            "[Avowed][NativeStereoFix] Render transition detected; holding one view scene={:x} target={:x} capture_rt={:x} capture_native={:x} fast_reacquire={} stable_required={}",
            scene,
            render_target,
            scene_capture_render_target,
            scene_capture_native,
            can_fast_reacquire,
            g_avowed_native_fix_gate.required_stable_frames);

        if (out_required_stable_frames != nullptr) {
            *out_required_stable_frames = g_avowed_native_fix_gate.required_stable_frames;
        }

        return false;
    }

    if (g_avowed_native_fix_gate.targets_missing) {
        const auto missing_duration = g_avowed_native_fix_gate.missing_since.time_since_epoch().count() != 0
            ? now - g_avowed_native_fix_gate.missing_since
            : std::chrono::steady_clock::duration{};
        const auto matches_last_ready_path =
            g_avowed_native_fix_gate.had_ready_baseline &&
            g_avowed_native_fix_gate.last_ready_scene == scene &&
            g_avowed_native_fix_gate.last_ready_render_target == render_target;
        const auto can_fast_reacquire =
            matches_last_ready_path &&
            missing_duration <= AVOWED_NATIVE_FIX_FAST_REACQUIRE_MAX_MISSING;

        const auto requested_hold_duration = can_fast_reacquire
            ? std::chrono::steady_clock::duration{AVOWED_NATIVE_FIX_FAST_REACQUIRE_HOLD}
            : std::chrono::steady_clock::duration{AVOWED_NATIVE_FIX_TRANSITION_HOLD};
        const auto requested_hold_until = now + requested_hold_duration;
        g_avowed_native_fix_gate.hold_until = can_fast_reacquire
            ? requested_hold_until
            : std::max(g_avowed_native_fix_gate.hold_until, requested_hold_until);
        g_avowed_native_fix_gate.ready = false;
        g_avowed_native_fix_gate.stable_frames = 0;
        g_avowed_native_fix_gate.required_stable_frames =
            can_fast_reacquire ? AVOWED_NATIVE_FIX_FAST_REACQUIRE_STABLE_FRAMES : AVOWED_NATIVE_FIX_STABLE_FRAMES;
        g_avowed_native_fix_gate.targets_missing = false;
        g_avowed_native_fix_gate.missing_since = {};
        g_avowed_native_fix_gate.fast_reacquire = can_fast_reacquire;

        SPDLOG_INFO_EVERY_N_SEC(
            2,
            "[Avowed][NativeStereoFix] Render targets reacquired on existing baseline fast_reacquire={} stable_required={}",
            can_fast_reacquire,
            g_avowed_native_fix_gate.required_stable_frames);
    }

    if (g_avowed_native_fix_gate.hold_until > now) {
        if (out_stable_frames != nullptr) {
            *out_stable_frames = 0;
        }

        if (out_required_stable_frames != nullptr) {
            *out_required_stable_frames = g_avowed_native_fix_gate.required_stable_frames;
        }

        g_avowed_native_fix_gate.ready = false;
        g_avowed_native_fix_gate.stable_frames = 0;

        SPDLOG_INFO_EVERY_N_SEC(
            2,
            "[Avowed][NativeStereoFix] Holding one view during transition grace window remaining={:.1f}s",
            std::chrono::duration<double>(g_avowed_native_fix_gate.hold_until - now).count());

        return false;
    }

    if (!g_avowed_native_fix_gate.ready) {
        const auto required_stable_frames = g_avowed_native_fix_gate.required_stable_frames != 0
            ? g_avowed_native_fix_gate.required_stable_frames
            : AVOWED_NATIVE_FIX_STABLE_FRAMES;

        if (g_avowed_native_fix_gate.stable_frames < required_stable_frames) {
            ++g_avowed_native_fix_gate.stable_frames;
        }

        if (out_stable_frames != nullptr) {
            *out_stable_frames = g_avowed_native_fix_gate.stable_frames;
        }

        if (out_required_stable_frames != nullptr) {
            *out_required_stable_frames = required_stable_frames;
        }

        if (g_avowed_native_fix_gate.stable_frames >= required_stable_frames) {
            g_avowed_native_fix_gate.ready = true;
            g_avowed_native_fix_gate.had_ready_baseline = true;
            g_avowed_native_fix_gate.last_ready_scene = scene;
            g_avowed_native_fix_gate.last_ready_render_target = render_target;
            SPDLOG_INFO(
                "[Avowed][NativeStereoFix] Render transition stabilized after {} frames; enabling two-view native fix fast_reacquire={}",
                g_avowed_native_fix_gate.stable_frames,
                g_avowed_native_fix_gate.fast_reacquire);
        }
    }

    return g_avowed_native_fix_gate.ready;
}

bool avowed_native_fix_gate_ready(uint32_t* out_stable_frames = nullptr, uint32_t* out_required_stable_frames = nullptr) {
    if (!avowed_is_current_game()) {
        return true;
    }

    std::scoped_lock _{g_avowed_native_fix_gate_mutex};

    if (out_stable_frames != nullptr) {
        *out_stable_frames = g_avowed_native_fix_gate.stable_frames;
    }

    if (out_required_stable_frames != nullptr) {
        *out_required_stable_frames = g_avowed_native_fix_gate.required_stable_frames != 0
            ? g_avowed_native_fix_gate.required_stable_frames
            : AVOWED_NATIVE_FIX_STABLE_FRAMES;
    }

    return g_avowed_native_fix_gate.ready;
}

bool is_ue_5_7_or_newer() {
    static const auto disk_version = sdk::get_file_version_info();
    static const auto str_version = utility::narrow(sdk::search_for_version(utility::get_executable()).value_or(L"0.00"));

    if (str_version != "0.00") {
        if (str_version.starts_with("5.7") || str_version.starts_with("5.8") || str_version.starts_with("5.9")) {
            return true;
        }
    }

    return disk_version.dwFileVersionMS >= 0x50007;
}

bool is_ue_5_1_dx12_backend() {
    if (g_framework == nullptr || !g_framework->is_dx12()) {
        return false;
    }

    static const auto disk_version = sdk::get_file_version_info();
    static const auto str_version = utility::narrow(sdk::search_for_version(utility::get_executable()).value_or(L"0.00"));

    if (str_version != "0.00") {
        return str_version.starts_with("5.1");
    }

    return disk_version.dwFileVersionMS >= 0x50001 && disk_version.dwFileVersionMS < 0x50002;
}

bool is_ue_5_1_dx_backend() {
    if (g_framework == nullptr || (!g_framework->is_dx12() && !g_framework->is_dx11())) {
        return false;
    }

    static const auto disk_version = sdk::get_file_version_info();
    static const auto str_version = utility::narrow(sdk::search_for_version(utility::get_executable()).value_or(L"0.00"));

    if (str_version != "0.00") {
        return str_version.starts_with("5.1");
    }

    return disk_version.dwFileVersionMS >= 0x50001 && disk_version.dwFileVersionMS < 0x50002;
}

bool is_ue_5_4_runtime() {
    static const auto disk_version = sdk::get_file_version_info();
    static const auto str_version = utility::narrow(sdk::search_for_version(utility::get_executable()).value_or(L"0.00"));

    if (str_version != "0.00") {
        return str_version.starts_with("5.4");
    }

    return disk_version.dwFileVersionMS >= 0x50004 && disk_version.dwFileVersionMS < 0x50005;
}

bool is_ue_5_4_dx_backend() {
    if (g_framework == nullptr || (!g_framework->is_dx12() && !g_framework->is_dx11())) {
        return false;
    }

    return is_ue_5_4_runtime();
}

bool is_ue_5_5_runtime() {
    static const auto disk_version = sdk::get_file_version_info();
    static const auto str_version = utility::narrow(sdk::search_for_version(utility::get_executable()).value_or(L"0.00"));

    if (str_version != "0.00") {
        return str_version.starts_with("5.5");
    }

    return disk_version.dwFileVersionMS >= 0x50005 && disk_version.dwFileVersionMS < 0x50006;
}

bool is_ue_5_5_dx_backend() {
    if (g_framework == nullptr || (!g_framework->is_dx12() && !g_framework->is_dx11())) {
        return false;
    }

    return is_ue_5_5_runtime();
}

bool is_ue_5_5_dx12_backend() {
    if (g_framework == nullptr || !g_framework->is_dx12()) {
        return false;
    }

    return is_ue_5_5_runtime();
}

bool is_ue_5_6_dx12_backend() {
    if (g_framework == nullptr || !g_framework->is_dx12()) {
        return false;
    }

    static const auto disk_version = sdk::get_file_version_info();
    static const auto str_version = utility::narrow(sdk::search_for_version(utility::get_executable()).value_or(L"0.00"));

    if (str_version != "0.00") {
        return str_version.starts_with("5.6");
    }

    return disk_version.dwFileVersionMS >= 0x50006 && disk_version.dwFileVersionMS < 0x50007;
}

bool ue56_dx12_try_get_native_resource(FRHITexture2D* texture, const char* source, ID3D12Resource** out_native = nullptr, D3D12_RESOURCE_DESC* out_desc = nullptr) {
    if (out_native != nullptr) {
        *out_native = nullptr;
    }

    if (out_desc != nullptr) {
        *out_desc = {};
    }

    if (!is_ue_5_6_dx12_backend()) {
        return true;
    }

    if (texture == nullptr || IsBadReadPtr(texture, sizeof(void*))) {
        SPDLOG_INFO_EVERY_N_SEC(2, "[UE5.6][RT] {} candidate is not readable yet: tex={:x}",
            source != nullptr ? source : "<unknown>", (uintptr_t)texture);
        return false;
    }

    void* vtable = nullptr;

    try {
        vtable = *(void**)texture;
    } catch (...) {
        return false;
    }

    if (vtable == nullptr || IsBadReadPtr(vtable, sizeof(void*))) {
        SPDLOG_INFO_EVERY_N_SEC(2, "[UE5.6][RT] {} candidate has no readable vtable: tex={:x} vtable={:x}",
            source != nullptr ? source : "<unknown>", (uintptr_t)texture, (uintptr_t)vtable);
        return false;
    }

    {
        std::scoped_lock _{g_ue56_rt_probe_mutex};
        if (const auto it = g_ue56_native_resource_probe_cache.find((uintptr_t)vtable);
            it != g_ue56_native_resource_probe_cache.end() && !it->second)
        {
            return false;
        }
    }

    // UE 5.6 can expose Slate/viewport FRHITexture candidates whose native-resource
    // vtable discovery executes unsafe render-thread thunks. Do not probe them from
    // the fallback path; let the D3D12 backbuffer/texture hooks discover the scene.
    SPDLOG_WARNING_EVERY_N_SEC(2, "[UE5.6][RT] Refusing unsafe FRHITexture::GetNativeResource probing for {} candidate: tex={:x} vtable={:x}",
        source != nullptr ? source : "<unknown>", (uintptr_t)texture, (uintptr_t)vtable);
    {
        std::scoped_lock _{g_ue56_rt_probe_mutex};
        g_ue56_native_resource_probe_cache[(uintptr_t)vtable] = false;
    }
    return false;
}

bool is_ue57_dx11_backend() {
    return is_ue_5_7_or_newer() && g_framework != nullptr && g_framework->is_dx11();
}

bool supports_ue57_dedicated_ui_target() {
    if (!is_ue_5_7_or_newer() || g_framework == nullptr) {
        return false;
    }

    return g_framework->is_dx12() || g_framework->is_dx11();
}

bool supports_ue55_dedicated_ui_target_for_current_game() {
    // These UE5.5 titles expose a valid Slate UI texture but route Slate to the
    // wrong target, leaving the HUD clipped in the upper-left/left-eye path.
    // Keep this allowlisted and DX12-only until more UE5.5 games validate it.
    return (aphelion_is_current_game() || ark_ascended_is_current_game() || mechwarrior_clans_is_current_game() || directive8020_is_current_game()) &&
        g_framework != nullptr &&
        g_framework->is_dx12() &&
        !is_ue_5_7_or_newer();
}

bool supports_dedicated_ui_target_for_current_game() {
    return supports_ue57_dedicated_ui_target() || supports_ue55_dedicated_ui_target_for_current_game();
}

bool is_probable_ue57_dx11_texture_desc_prepare_function(uintptr_t fn) {
    if (fn == 0 || IsBadReadPtr((void*)fn, 0x80)) {
        return false;
    }

    try {
        // D3D11 UE 5.7 can put a descriptor prepare/copy helper before the real
        // create wrapper. Calling that helper with the create signature is unsafe.
        return utility::scan(fn, 0x100, "0F B6 42 32").has_value()
            && utility::scan(fn, 0x100, "48 83 C2 38").has_value()
            && (utility::scan(fn, 0x100, "0F 10 42 08").has_value() ||
                utility::scan(fn, 0x100, "48 8B 02").has_value() ||
                utility::scan(fn, 0x100, "48 8B 42 24").has_value());
    } catch (...) {
        return false;
    }
}

void log_engine_render_timing_if_needed() {
    if (!is_ue_5_7_or_newer()) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();

    if (g_engine_render_last_log.time_since_epoch().count() == 0) {
        g_engine_render_last_log = now;
        return;
    }

    if (now - g_engine_render_last_log < ENGINE_RENDER_TIMING_LOG_INTERVAL) {
        return;
    }

    if (g_begin_render_viewfamily_real_timing.count == 0 &&
        g_begin_render_viewfamily_timing.count == 0 &&
        g_prerender_viewfamily_rt_timing.count == 0)
    {
        g_engine_render_last_log = now;
        return;
    }

    auto vr = VR::get();
    const auto hmd_active = vr != nullptr && vr->is_hmd_active();
    const auto native_stereo = vr != nullptr && vr->is_native_stereo_fix_enabled();

    spdlog::info(
        "[UE57][engine-render-profiler] begin_render_viewfamily_real avg={:.2f}ms max={:.2f}ms n={} begin_render_viewfamily avg={:.2f}ms max={:.2f}ms n={} prerender_viewfamily_rt avg={:.2f}ms max={:.2f}ms n={} hmd={} native_stereo={}",
        g_begin_render_viewfamily_real_timing.avg(),
        g_begin_render_viewfamily_real_timing.max_ms,
        g_begin_render_viewfamily_real_timing.count,
        g_begin_render_viewfamily_timing.avg(),
        g_begin_render_viewfamily_timing.max_ms,
        g_begin_render_viewfamily_timing.count,
        g_prerender_viewfamily_rt_timing.avg(),
        g_prerender_viewfamily_rt_timing.max_ms,
        g_prerender_viewfamily_rt_timing.count,
        hmd_active,
        native_stereo
    );

    g_engine_render_last_log = now;
    g_begin_render_viewfamily_real_timing.reset();
    g_begin_render_viewfamily_timing.reset();
    g_prerender_viewfamily_rt_timing.reset();
}

bool shf_is_valid_texture_with_vtable(FRHITexture2D* texture, void* required_vtable) {
    if (texture == nullptr || required_vtable == nullptr || IsBadReadPtr(texture, sizeof(void*))) {
        return false;
    }

    void* vtable{};

    try {
        vtable = *(void**)texture;
    } catch (...) {
        return false;
    }

    if (vtable != required_vtable) {
        return false;
    }

    FRHITexture2D::set_vtable(vtable);
    return true;
}

std::optional<D3D12_RESOURCE_DESC> shf_try_get_d3d12_desc(FRHITexture2D* texture) {
    if (texture == nullptr) {
        return std::nullopt;
    }

    try {
        const auto native = (ID3D12Resource*)texture->get_native_resource();

        if (native == nullptr || IsBadReadPtr(native, sizeof(void*))) {
            return std::nullopt;
        }

        return native->GetDesc();
    } catch (...) {
        return std::nullopt;
    }
}

bool is_probable_d3d_native_resource(void* native) {
    if (native == nullptr || IsBadReadPtr(native, sizeof(void*))) {
        return false;
    }

    void* vtable{};

    try {
        vtable = *(void**)native;
    } catch (...) {
        return false;
    }

    if (vtable == nullptr || IsBadReadPtr(vtable, sizeof(void*))) {
        return false;
    }

    const auto module = utility::get_module_within(vtable);
    if (!module) {
        return false;
    }

    const auto module_path = utility::get_module_path(*module);
    if (!module_path) {
        return false;
    }

    auto module_path_lower = std::string(*module_path);
    std::transform(module_path_lower.begin(), module_path_lower.end(), module_path_lower.begin(), ::tolower);
    return module_path_lower.ends_with("d3d12.dll") ||
        module_path_lower.ends_with("d3d12core.dll") ||
        module_path_lower.ends_with("dxgi.dll") ||
        module_path_lower.ends_with("renderdoc.dll") ||
        module_path_lower.ends_with("d3d12sdklayers.dll");
}

std::optional<uintptr_t> ue55_find_texture_desc_offset(FRHITexture2D* texture) {
    if (texture == nullptr || IsBadReadPtr(texture, sizeof(void*))) {
        return std::nullopt;
    }

    const auto texture_address = (uintptr_t)texture;
    constexpr std::array<uintptr_t, 2> desc_offsets{0x20, 0xf0};

    for (const auto desc_offset : desc_offsets) {
        if (texture_address + desc_offset < texture_address ||
            IsBadReadPtr((void*)(texture_address + desc_offset), 0x38)) {
            continue;
        }

        const auto extent_x = *(const int32_t*)(texture_address + desc_offset + 0x24);
        const auto extent_y = *(const int32_t*)(texture_address + desc_offset + 0x28);
        const auto num_mips = *(const uint8_t*)(texture_address + desc_offset + 0x30);
        const auto num_samples = *(const uint8_t*)(texture_address + desc_offset + 0x31);
        const auto dimension = *(const uint8_t*)(texture_address + desc_offset + 0x32);
        const auto format = *(const uint8_t*)(texture_address + desc_offset + 0x33);

        if (extent_x <= 0 || extent_y <= 0 || extent_x > 65536 || extent_y > 65536) {
            continue;
        }

        if (num_mips == 0 || num_mips > 32) {
            continue;
        }

        if (!(num_samples == 1 || num_samples == 2 || num_samples == 4 || num_samples == 8 || num_samples == 16)) {
            continue;
        }

        if (dimension > 8 || format == 0 || format > 128) {
            continue;
        }

        return desc_offset;
    }

    return std::nullopt;
}

bool ue55_dx12_try_get_native_resource_direct(
    FRHITexture2D* texture,
    const char* source,
    ID3D12Resource** out_native = nullptr,
    D3D12_RESOURCE_DESC* out_desc = nullptr)
{
    if (out_native != nullptr) {
        *out_native = nullptr;
    }

    if (out_desc != nullptr) {
        *out_desc = {};
    }

    if (texture == nullptr || IsBadReadPtr(texture, sizeof(void*))) {
        SPDLOG_INFO_EVERY_N_SEC(2, "[UE5.5][SlateUI] {} candidate is not readable: tex={:x}",
            source != nullptr ? source : "<unknown>", (uintptr_t)texture);
        return false;
    }

    void** vtable{};

    try {
        vtable = *(void***)texture;
    } catch (...) {
        return false;
    }

    if (vtable == nullptr || IsBadReadPtr(vtable, sizeof(void*) * 6)) {
        SPDLOG_INFO_EVERY_N_SEC(2, "[UE5.5][SlateUI] {} candidate has no readable vtable: tex={:x} vtable={:x}",
            source != nullptr ? source : "<unknown>", (uintptr_t)texture, (uintptr_t)vtable);
        return false;
    }

    const auto desc_offset = ue55_find_texture_desc_offset(texture);
    if (!desc_offset) {
        SPDLOG_INFO_EVERY_N_SEC(2, "[UE5.5][SlateUI] refusing {} candidate without a UE5.5/5.6 FRHITextureDesc: tex={:x}",
            source != nullptr ? source : "<unknown>", (uintptr_t)texture);
        return false;
    }

    const std::array<size_t, 2> direct_slots = *desc_offset == 0xf0
        ? std::array<size_t, 2>{5ull, 4ull}
        : std::array<size_t, 2>{4ull, 5ull};

    for (const auto slot : direct_slots) {
        using GetNativeResourceFn = void* (*)(const FRHITexture2D*);
        const auto fn = (GetNativeResourceFn)vtable[slot];

        if (fn == nullptr || IsBadReadPtr((void*)fn, 1) || !utility::get_module_within((void*)fn).has_value()) {
            continue;
        }

        void* native_raw = nullptr;

        try {
            native_raw = fn(texture);
        } catch (...) {
            continue;
        }

        if (!is_probable_d3d_native_resource(native_raw)) {
            continue;
        }

        auto* native = (ID3D12Resource*)native_raw;
        D3D12_RESOURCE_DESC desc{};

        try {
            desc = native->GetDesc();
        } catch (...) {
            continue;
        }

        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
            desc.Width == 0 ||
            desc.Height == 0 ||
            desc.Width > 65536 ||
            desc.Height > 65536)
        {
            continue;
        }

        FRHITexture2D::set_vtable(vtable);

        if (out_native != nullptr) {
            *out_native = native;
        }

        if (out_desc != nullptr) {
            *out_desc = desc;
        }

        return true;
    }

    SPDLOG_INFO_EVERY_N_SEC(2,
        "[UE5.5][SlateUI] direct GetNativeResource slots {} and {} did not produce a valid D3D12 texture for {} tex={:x}",
        direct_slots[0],
        direct_slots[1],
        source != nullptr ? source : "<unknown>",
        (uintptr_t)texture);

    return false;
}

std::optional<D3D12_RESOURCE_DESC> ue55_try_get_d3d12_desc(FRHITexture2D* texture, const char* source) {
    D3D12_RESOURCE_DESC desc{};

    if (!ue55_dx12_try_get_native_resource_direct(texture, source, nullptr, &desc)) {
        return std::nullopt;
    }

    return desc;
}

void shf_log_rtm_candidate(VRRenderTargetManager_Base* rtm, FRHITexture2D* texture, const char* source) {
    if (!shf_is_current_game() || !g_framework->is_dx12() || rtm == nullptr || texture == nullptr) {
        return;
    }

    ID3D12Resource* native = nullptr;
    std::optional<D3D12_RESOURCE_DESC> desc{};

    try {
        native = (ID3D12Resource*)texture->get_native_resource();

        if (native != nullptr && !IsBadReadPtr(native, sizeof(void*))) {
            desc = native->GetDesc();
        }
    } catch (...) {
    }

    bool log_unique = false;
    uint64_t seen = 0;
    uint64_t unique = 0;
    uint64_t suppressed = 0;

    {
        std::scoped_lock _{g_shf_texture_probe_mutex};
        ++g_shf_rtm_candidate_count;
        seen = g_shf_rtm_candidate_count;

        const auto key = (uintptr_t)(native != nullptr ? native : (ID3D12Resource*)texture);

        if (!g_shf_logged_rtm_candidate_natives.contains(key)) {
            g_shf_logged_rtm_candidate_natives.insert(key);
            log_unique = g_shf_logged_rtm_candidate_natives.size() <= 64;
        } else {
            ++g_shf_rtm_candidate_suppressed;
        }

        unique = g_shf_logged_rtm_candidate_natives.size();
        suppressed = g_shf_rtm_candidate_suppressed;
    }

    if (log_unique && desc) {
        SPDLOG_WARN("[SHf][RTM] accepting unique texture candidate #{} source={} tex={:x} native={:x} [{}x{} fmt={} flags=0x{:x}] current_rt={:x}",
            seen, source, (uintptr_t)texture, (uintptr_t)native, desc->Width, desc->Height, (uint32_t)desc->Format,
            (uint32_t)desc->Flags, (uintptr_t)rtm->get_render_target());
    } else if (log_unique) {
        SPDLOG_WARN("[SHf][RTM] accepting unique texture candidate #{} source={} tex={:x} native={:x} desc=<unavailable> current_rt={:x}",
            seen, source, (uintptr_t)texture, (uintptr_t)native, (uintptr_t)rtm->get_render_target());
    } else {
        SPDLOG_INFO_EVERY_N_SEC(2,
            "[SHf][RTM] texture candidate summary seen={} unique_natives={} duplicate_suppressed={} last_source={} last_tex={:x} last_native={:x} current_rt={:x}",
            seen, unique, suppressed, source, (uintptr_t)texture, (uintptr_t)native, (uintptr_t)rtm->get_render_target());
    }
}

bool shf_can_reuse_current_ui_target(VRRenderTargetManager_Base* rtm, uint32_t expected_width, uint32_t expected_height) {
    if (!shf_is_current_game() || !g_framework->is_dx12() || rtm == nullptr || expected_width == 0 || expected_height == 0) {
        return false;
    }

    auto* ui_target = rtm->get_ui_target();

    if (ui_target == nullptr || ui_target == rtm->get_render_target()) {
        return false;
    }

    const auto desc = shf_try_get_d3d12_desc(ui_target);

    if (!desc || desc->Width != expected_width || desc->Height != expected_height) {
        return false;
    }

    SPDLOG_INFO_EVERY_N_SEC(2,
        "[SHf] Reusing stable UI texture {:x} [{}x{} fmt={}]; skipping duplicate UI texture creation",
        (uintptr_t)ui_target, desc->Width, desc->Height, (uint32_t)desc->Format);

    return true;
}

void ue51_log_rt_churn_summary_locked(const char* reason) {
    const auto now = std::chrono::steady_clock::now();

    if (g_ue51_rt_churn.last_log.time_since_epoch().count() != 0 &&
        now - g_ue51_rt_churn.last_log < std::chrono::seconds(30))
    {
        return;
    }

    g_ue51_rt_churn.last_log = now;

    uint32_t current_width = 0;
    uint32_t current_height = 0;

    if (g_framework != nullptr) {
        const auto size = g_framework->get_d3d12_rt_size();
        current_width = (uint32_t)size.x;
        current_height = (uint32_t)size.y;
    }

    SPDLOG_INFO(
        "[UE5.1][RTChurn] summary reason={} alloc_seen={} ui_created={} ui_reused={} last_alloc={:x} last_create={:x} last_ui={:x} last_ui_size={}x{} current_rt_size={}x{}",
        reason != nullptr ? reason : "<unknown>",
        g_ue51_rt_churn.allocate_seen,
        g_ue51_rt_churn.ui_created,
        g_ue51_rt_churn.ui_reused,
        g_ue51_rt_churn.last_allocate_return_address,
        g_ue51_rt_churn.last_ui_create_return_address,
        g_ue51_rt_churn.last_ui_texture,
        g_ue51_rt_churn.last_ui_width,
        g_ue51_rt_churn.last_ui_height,
        current_width,
        current_height);
}

void ue51_note_rt_allocation(uintptr_t relative_return_address) {
    if (!is_ue_5_1_dx12_backend()) {
        return;
    }

    std::scoped_lock _{g_ue51_rt_churn_mutex};
    ++g_ue51_rt_churn.allocate_seen;
    g_ue51_rt_churn.last_allocate_return_address = relative_return_address;
    ue51_log_rt_churn_summary_locked("allocate");
}

void ue51_note_ui_created(FRHITexture2D* ui_texture, uint32_t width, uint32_t height) {
    if (!is_ue_5_1_dx12_backend() || ui_texture == nullptr) {
        return;
    }

    std::scoped_lock _{g_ue51_rt_churn_mutex};
    ++g_ue51_rt_churn.ui_created;
    g_ue51_rt_churn.last_ui_create_return_address = g_ue51_rt_churn.last_allocate_return_address;
    g_ue51_rt_churn.last_ui_texture = (uintptr_t)ui_texture;
    g_ue51_rt_churn.last_ui_width = width;
    g_ue51_rt_churn.last_ui_height = height;
    ue51_log_rt_churn_summary_locked("ui_created");
}

bool ue51_can_reuse_current_ui_target(VRRenderTargetManager_Base* rtm, uint32_t expected_width, uint32_t expected_height) {
    if (!is_ue_5_1_dx12_backend() || rtm == nullptr || expected_width == 0 || expected_height == 0) {
        return false;
    }

    const auto* ui_target = rtm->get_ui_target();

    if (ui_target == nullptr || ui_target == rtm->get_render_target()) {
        return false;
    }

    std::scoped_lock _{g_ue51_rt_churn_mutex};

    if (g_ue51_rt_churn.ui_created == 0 ||
        g_ue51_rt_churn.last_allocate_return_address == 0 ||
        g_ue51_rt_churn.last_allocate_return_address != g_ue51_rt_churn.last_ui_create_return_address ||
        g_ue51_rt_churn.last_ui_texture != (uintptr_t)ui_target ||
        g_ue51_rt_churn.last_ui_width != expected_width ||
        g_ue51_rt_churn.last_ui_height != expected_height)
    {
        return false;
    }

    ++g_ue51_rt_churn.ui_reused;
    ue51_log_rt_churn_summary_locked("ui_reused");

    return true;
}

void shf_log_texture_probe_candidate(
    const char* source,
    const char* base_name,
    uintptr_t base,
    uintptr_t offset,
    int32_t array_index,
    FRHITexture2D* texture,
    const D3D12_RESOURCE_DESC& desc)
{
    const auto key = ((uintptr_t)texture >> 4) ^
                     (base << 9) ^
                     (offset << 21) ^
                     ((uintptr_t)(array_index + 1) << 53);

    {
        std::scoped_lock _{g_shf_texture_probe_mutex};

        if (g_shf_logged_texture_probe_keys.contains(key)) {
            return;
        }

        g_shf_logged_texture_probe_keys.insert(key);
    }

    if (array_index >= 0) {
        SPDLOG_WARN("[SHf] FSceneViewport probe {} {}+0x{:x}[{}] -> tex {:x} [{}x{} fmt={} flags=0x{:x}]",
            source, base_name, offset, array_index, (uintptr_t)texture, desc.Width, desc.Height, (uint32_t)desc.Format, (uint32_t)desc.Flags);
    } else {
        SPDLOG_WARN("[SHf] FSceneViewport probe {} {}+0x{:x} -> tex {:x} [{}x{} fmt={} flags=0x{:x}]",
            source, base_name, offset, (uintptr_t)texture, desc.Width, desc.Height, (uint32_t)desc.Format, (uint32_t)desc.Flags);
    }
}

void shf_probe_scene_viewport_memory(sdk::FViewport* viewport, const char* source, FRHITexture2D* known_texture) {
    if (viewport == nullptr || IsBadReadPtr(viewport, sizeof(void*))) {
        return;
    }

    void* required_vtable = nullptr;

    if (known_texture != nullptr && !IsBadReadPtr(known_texture, sizeof(void*))) {
        required_vtable = *(void**)known_texture;
    }

    if (required_vtable == nullptr) {
        required_vtable = FRHITexture2D::get_vtable();
    }

    if (required_vtable == nullptr) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto viewport_base = (uintptr_t)viewport;

    {
        std::scoped_lock _{g_shf_texture_probe_mutex};
        auto& last_probe = g_shf_last_texture_probe_by_base[viewport_base];

        if (last_probe.time_since_epoch().count() != 0 && now - last_probe < std::chrono::seconds(2)) {
            return;
        }

        last_probe = now;
    }

    auto probe_base = [&](uintptr_t base, const char* base_name) {
        if (base == 0 || IsBadReadPtr((void*)base, 0x340)) {
            return;
        }

        for (uintptr_t offset = 0; offset <= 0x330; offset += sizeof(void*)) try {
            const auto field = base + offset;

            if (IsBadReadPtr((void*)field, sizeof(void*))) {
                continue;
            }

            const auto texture = *(FRHITexture2D**)field;

            if (shf_is_valid_texture_with_vtable(texture, required_vtable)) {
                if (const auto desc = shf_try_get_d3d12_desc(texture)) {
                    shf_log_texture_probe_candidate(source, base_name, base, offset, -1, texture, *desc);
                }
            }

            if (IsBadReadPtr((void*)field, sizeof(void*) + sizeof(int32_t) * 2)) {
                continue;
            }

            const auto array_data = *(FRHITexture2D***)field;
            const auto array_count = *(int32_t*)(field + sizeof(void*));
            const auto array_capacity = *(int32_t*)(field + sizeof(void*) + sizeof(int32_t));

            if (array_data == nullptr || array_count <= 0 || array_count > 8 || array_capacity < array_count ||
                IsBadReadPtr(array_data, sizeof(FRHITexture2D*) * array_count))
            {
                continue;
            }

            for (int32_t i = 0; i < array_count; ++i) {
                const auto array_texture = array_data[i];

                if (!shf_is_valid_texture_with_vtable(array_texture, required_vtable)) {
                    continue;
                }

                if (const auto desc = shf_try_get_d3d12_desc(array_texture)) {
                    shf_log_texture_probe_candidate(source, base_name, base, offset, i, array_texture, *desc);
                }
            }
        } catch (...) {
        }
    };

    probe_base(viewport_base, "FViewport");

    if (viewport_base > 0x1000) {
        probe_base(viewport_base - sizeof(void*), "FSceneViewport");
    }
}

void shf_force_scene_viewport_separate_rt(const sdk::FViewport& viewport, const char* source) {
    if (!shf_is_current_game() || g_framework == nullptr || !g_framework->is_game_data_intialized()) {
        return;
    }

    const auto vr = VR::get();

    if (vr == nullptr || !vr->is_hmd_active() || vr->is_stereo_emulation_enabled() || vr->is_extreme_compatibility_mode_enabled()) {
        return;
    }

    constexpr uintptr_t fviewport_base_offset = 0x08;
    constexpr uintptr_t b_use_separate_rt_full_offset = 0x287;
    constexpr uintptr_t b_force_separate_rt_full_offset = 0x288;
    constexpr uintptr_t b_use_separate_rt_fviewport_offset = b_use_separate_rt_full_offset - fviewport_base_offset;
    constexpr uintptr_t b_force_separate_rt_fviewport_offset = b_force_separate_rt_full_offset - fviewport_base_offset;

    const auto viewport_base = reinterpret_cast<uintptr_t>(&viewport);

    if (viewport_base <= fviewport_base_offset ||
        IsBadReadPtr(reinterpret_cast<void*>(viewport_base - fviewport_base_offset), sizeof(void*)) ||
        IsBadReadPtr(reinterpret_cast<void*>(viewport_base + b_force_separate_rt_fviewport_offset), sizeof(uint8_t)))
    {
        return;
    }

    const auto fscene_viewport_vtable = *reinterpret_cast<uintptr_t*>(viewport_base - fviewport_base_offset);

    if (fscene_viewport_vtable == 0 || !utility::get_module_within(fscene_viewport_vtable).has_value()) {
        return;
    }

    auto* use_separate_rt = reinterpret_cast<uint8_t*>(viewport_base + b_use_separate_rt_fviewport_offset);
    auto* force_separate_rt = reinterpret_cast<uint8_t*>(viewport_base + b_force_separate_rt_fviewport_offset);

    if (*use_separate_rt > 1 || *force_separate_rt > 1) {
        SPDLOG_WARN_ONCE("[SHf] Refusing to force separate RT from {}; unexpected FSceneViewport bool bytes use={} force={}",
            source, *use_separate_rt, *force_separate_rt);
        return;
    }

    if (*use_separate_rt == 0 || *force_separate_rt == 0) {
        SPDLOG_WARN_ONCE("[SHf] Forcing FSceneViewport separate RT from {} at viewport {:x} use+0x{:x} force+0x{:x}",
            source, viewport_base, b_use_separate_rt_fviewport_offset, b_force_separate_rt_fviewport_offset);
    }

    *use_separate_rt = 1;
    *force_separate_rt = 1;
}

constexpr auto UE57_SLATE_THREAD_PREFERENCE_CACHE_KEY = "ue57_prefer_slate_thread";
constexpr auto UE57_VIEW_EXTENSION_DISCOVERY_CACHE_KEY = "ue57_view_extension_discovery";

bool load_ue57_slate_thread_preference() {
    if (!is_ue_5_7_or_newer()) {
        return false;
    }

    if (const auto cached = sdk::discovery_cache::load_entry(UE57_SLATE_THREAD_PREFERENCE_CACHE_KEY, utility::get_executable())) {
        return cached->value("prefer_slate_thread", false);
    }

    return false;
}

void save_ue57_slate_thread_preference(bool prefer) {
    if (!is_ue_5_7_or_newer()) {
        return;
    }

    if (prefer) {
        sdk::discovery_cache::save_entry(UE57_SLATE_THREAD_PREFERENCE_CACHE_KEY, utility::get_executable(), {
            {"prefer_slate_thread", true}
        });
    } else {
        sdk::discovery_cache::invalidate_entry(UE57_SLATE_THREAD_PREFERENCE_CACHE_KEY);
    }
}

enum class UE57RenderTargetLoadAction : uint32_t {
    NoAction = 0,
    Load = 1,
    Clear = 2,
};

struct UE57SlateDrawElementsPassInputsHead {
    FRDGTexture* stencil_texture;
    FRDGTexture* elements_texture;
    FRDGTexture* scene_viewport_texture;
    UE57RenderTargetLoadAction elements_load_action;
};

bool looks_like_ue57_slate_draw_elements_inputs(const UE57SlateDrawElementsPassInputsHead* inputs) {
    if (inputs == nullptr || !is_readable_process_range((uintptr_t)inputs, sizeof(UE57SlateDrawElementsPassInputsHead))) {
        return false;
    }

    const auto action = static_cast<uint32_t>(inputs->elements_load_action);

    if (action > static_cast<uint32_t>(UE57RenderTargetLoadAction::Clear)) {
        return false;
    }

    const auto scene_viewport_texture = inputs->scene_viewport_texture;
    const auto elements_texture = inputs->elements_texture;

    if (scene_viewport_texture == nullptr || elements_texture == nullptr) {
        return false;
    }

    if (!is_readable_process_range((uintptr_t)scene_viewport_texture, sizeof(void*)) ||
        !is_readable_process_range((uintptr_t)elements_texture, sizeof(void*))) {
        return false;
    }

    return true;
}

using RegisterExternalTextureFromRHIFn = FRDGTexture* (*)(FRDGBuilder&, FRHITexture*, const wchar_t*);

bool looks_like_nontrivial_virtual(uintptr_t fn) {
    if (fn == 0 || IsBadReadPtr((void*)fn, 1) || !utility::get_module_within((void*)fn).has_value()) {
        return false;
    }

    size_t decoded_bytes = 0;
    size_t call_count = 0;
    bool saw_terminator = false;

    for (auto ip = (uint8_t*)fn; decoded_bytes < 512; ) {
        const auto decoded = utility::decode_one(ip);

        if (!decoded) {
            break;
        }

        decoded_bytes += decoded->Length;

        if (std::string_view{decoded->Mnemonic}.starts_with("CALL")) {
            ++call_count;
        }

        if (std::string_view{decoded->Mnemonic}.starts_with("RET") || std::string_view{decoded->Mnemonic}.starts_with("INT3")) {
            saw_terminator = true;
            break;
        }

        ip += decoded->Length;
    }

    return saw_terminator && decoded_bytes >= 64 && call_count >= 1;
}

bool looks_like_callable_virtual(uintptr_t fn) {
    if (fn == 0 || IsBadReadPtr((void*)fn, 1) || !utility::get_module_within((void*)fn).has_value()) {
        return false;
    }

    size_t decoded_bytes = 0;

    for (auto ip = (uint8_t*)fn; decoded_bytes < 256;) {
        const auto decoded = utility::decode_one(ip);

        if (!decoded || decoded->Length == 0) {
            break;
        }

        decoded_bytes += decoded->Length;

        const std::string_view mnemonic{decoded->Mnemonic};

        if (mnemonic.starts_with("RET")) {
            return decoded_bytes > 0;
        }

        if (mnemonic.starts_with("JMP")) {
            return decoded_bytes <= 16;
        }

        if (mnemonic.starts_with("INT3")) {
            return false;
        }

        ip += decoded->Length;
    }

    return false;
}

bool looks_like_post_init_properties_virtual(uintptr_t fn) {
    if (fn == 0 || IsBadReadPtr((void*)fn, 1) || !utility::get_module_within((void*)fn).has_value()) {
        return false;
    }

    size_t decoded_bytes = 0;
    size_t call_count = 0;

    for (auto ip = (uint8_t*)fn; decoded_bytes < 256;) {
        const auto decoded = utility::decode_one(ip);

        if (!decoded || decoded->Length == 0) {
            break;
        }

        decoded_bytes += decoded->Length;
        const std::string_view mnemonic{decoded->Mnemonic};

        if (mnemonic.starts_with("CALL")) {
            ++call_count;
        }

        if (mnemonic.starts_with("RET")) {
            return decoded_bytes > 8 || call_count > 0;
        }

        if (mnemonic.starts_with("JMP")) {
            return decoded_bytes > 8 || call_count > 0;
        }

        if (mnemonic.starts_with("INT3")) {
            return false;
        }

        ip += decoded->Length;
    }

    return call_count > 0;
}

std::optional<uint32_t> validate_source_informed_post_init_slot(
    uintptr_t* object_vtable,
    uintptr_t* localplayer_vtable,
    uint32_t slot,
    const char* source_note,
    bool require_inherited_uobject_slot,
    bool allow_localplayer_callable_thunk = false)
{
    if (IsBadReadPtr(&object_vtable[slot], sizeof(uintptr_t)) ||
        IsBadReadPtr(&localplayer_vtable[slot], sizeof(uintptr_t)))
    {
        SPDLOG_WARN("[PostInitProperties] {} slot {} is not readable", source_note, slot);
        return std::nullopt;
    }

    const auto object_fn = object_vtable[slot];
    const auto localplayer_fn = localplayer_vtable[slot];

    const auto localplayer_looks_valid = allow_localplayer_callable_thunk
        ? looks_like_callable_virtual(localplayer_fn)
        : looks_like_post_init_properties_virtual(localplayer_fn);

    if (!looks_like_post_init_properties_virtual(object_fn) || !localplayer_looks_valid)
    {
        SPDLOG_WARN("[PostInitProperties] {} slot {} did not look callable object_fn={:x} localplayer_fn={:x}",
            source_note,
            slot,
            object_fn,
            localplayer_fn);
        return std::nullopt;
    }

    if (require_inherited_uobject_slot && object_fn != localplayer_fn) {
        SPDLOG_WARN("[PostInitProperties] {} slot {} did not inherit UObject function object_fn={:x} localplayer_fn={:x}",
            source_note,
            slot,
            object_fn,
            localplayer_fn);
        return std::nullopt;
    }

    SPDLOG_INFO("[PostInitProperties] Resolved {} slot {} object_fn={:x} localplayer_fn={:x}",
        source_note,
        slot,
        object_fn,
        localplayer_fn);
    return slot;
}

std::optional<uint32_t> resolve_post_init_properties_index_from_uobject(uintptr_t localplayer) {
    auto* object_class = sdk::UObject::static_class();

    if (object_class == nullptr) {
        SPDLOG_WARN("[PostInitProperties] UObject::static_class() is not ready");
        return std::nullopt;
    }

    auto* object_cdo = object_class->get_class_default_object<sdk::UObject>();

    if (object_cdo == nullptr || IsBadReadPtr(object_cdo, sizeof(void*))) {
        SPDLOG_WARN("[PostInitProperties] UObject CDO is not ready");
        return std::nullopt;
    }

    const auto object_vtable = *(uintptr_t**)object_cdo;
    const auto localplayer_vtable = *(uintptr_t**)localplayer;

    if (object_vtable == nullptr || localplayer_vtable == nullptr ||
        IsBadReadPtr(object_vtable, sizeof(void*)) || IsBadReadPtr(localplayer_vtable, sizeof(void*)))
    {
        SPDLOG_WARN("[PostInitProperties] UObject or LocalPlayer vtable is invalid");
        return std::nullopt;
    }

    // UE5.1 source plus Stalker2/SOE PDBs place UObject::PostInitProperties at
    // slot 10 for shipped game layouts. Some UE5.1 games put a LocalPlayer
    // override/thunk at the same slot, so validate UObject strictly and only
    // require the LocalPlayer target to be callable.
    if (is_ue_5_1_dx_backend()) {
        constexpr uint32_t UE51_POST_INIT_PROPERTIES_SLOT = 10;

        if (validate_source_informed_post_init_slot(
                object_vtable,
                localplayer_vtable,
                UE51_POST_INIT_PROPERTIES_SLOT,
                "UE5.1 UObject::PostInitProperties",
                false,
                true))
        {
            return UE51_POST_INIT_PROPERTIES_SLOT;
        }

        SPDLOG_WARN("[PostInitProperties] UE5.1 slot 10 did not validate; skipping LocalPlayer bootstrap");
        return std::nullopt;
    }

    // UE 5.4.4, 5.5.4 and 5.6.1 source/PDB put UObject::PostInitProperties at slot 10
    // for shipped game layouts:
    // UObjectBase has 4 virtuals, UObjectBaseUtility has 5, then UObject adds
    // GetDetailedInfoInternal at 9 and PostInitProperties at 10.
    if (is_ue_5_4_dx_backend() || is_ue_5_5_dx_backend() || is_ue_5_6_dx12_backend() || is_ue_5_7_or_newer()) {
        constexpr uint32_t UE54_PLUS_POST_INIT_PROPERTIES_SLOT = 10;

        if (validate_source_informed_post_init_slot(
                object_vtable,
                localplayer_vtable,
                UE54_PLUS_POST_INIT_PROPERTIES_SLOT,
                "UE5.4+ UObject::PostInitProperties",
                false,
                is_ue_5_4_dx_backend()))
        {
            return UE54_PLUS_POST_INIT_PROPERTIES_SLOT;
        }
    }

    // Keep the nearby slots as a fail-closed fallback for unusual/custom layouts.
    constexpr std::array<uint32_t, 4> candidate_slots{10, 9, 8, 11};

    for (const auto slot : candidate_slots) {
        if (IsBadReadPtr(&object_vtable[slot], sizeof(uintptr_t)) ||
            IsBadReadPtr(&localplayer_vtable[slot], sizeof(uintptr_t)))
        {
            continue;
        }

        const auto object_fn = object_vtable[slot];
        const auto localplayer_fn = localplayer_vtable[slot];

        if (object_fn == 0 || localplayer_fn == 0) {
            continue;
        }

        if (!looks_like_post_init_properties_virtual(object_fn) ||
            !looks_like_post_init_properties_virtual(localplayer_fn))
        {
            continue;
        }

        SPDLOG_INFO("[PostInitProperties] Resolved UObject::PostInitProperties through nearby fallback slot {} object_fn={:x} localplayer_fn={:x}",
            slot,
            object_fn,
            localplayer_fn);
        return slot;
    }

    SPDLOG_WARN("[PostInitProperties] Could not validate the expected UObject::PostInitProperties slots on this build");
    return std::nullopt;
}
}

namespace {
bool is_writable_process_range(uintptr_t address, size_t size) {
    if (address == 0 || size == 0 || address + size < address) {
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery((void*)address, &mbi, sizeof(mbi)) == 0) {
        return false;
    }

    const auto base = (uintptr_t)mbi.BaseAddress;
    if (address + size > base + mbi.RegionSize) {
        return false;
    }

    if ((mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }

    const auto protect = mbi.Protect & 0xff;
    return protect == PAGE_READWRITE ||
           protect == PAGE_WRITECOPY ||
           protect == PAGE_EXECUTE_READWRITE ||
           protect == PAGE_EXECUTE_WRITECOPY;
}

bool is_readable_process_range(uintptr_t address, size_t size) {
    if (address == 0 || size == 0 || address + size < address) {
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery((void*)address, &mbi, sizeof(mbi)) == 0) {
        return false;
    }

    const auto base = (uintptr_t)mbi.BaseAddress;
    if (address + size > base + mbi.RegionSize) {
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

bool is_executable_process_range(uintptr_t address, size_t size) {
    if (address == 0 || size == 0 || address + size < address) {
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery((void*)address, &mbi, sizeof(mbi)) == 0) {
        return false;
    }

    const auto base = (uintptr_t)mbi.BaseAddress;
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

bool looks_like_virtual_function_table(uintptr_t table) {
    if (!is_readable_process_range(table, sizeof(uintptr_t) * 12)) {
        return false;
    }

    auto executable_entries = 0;

    for (auto i = 0; i < 12; ++i) {
        const auto fn = ((uintptr_t*)table)[i];

        if (fn == 0 || !is_executable_process_range(fn, 1)) {
            continue;
        }

        ++executable_entries;
    }

    return executable_entries >= 6;
}

template <typename T>
bool safe_read_value(uintptr_t address, T& out) {
    if (!is_readable_process_range(address, sizeof(T))) {
        return false;
    }

    memcpy(&out, (void*)address, sizeof(T));
    return true;
}

bool avowed_is_live_uobject(uintptr_t object, uintptr_t* out_vtable = nullptr, uintptr_t* out_class = nullptr) {
    if (!avowed_is_current_game() || object == 0) {
        return false;
    }

    uintptr_t vtable{};
    if (!safe_read_value(object, vtable) || !looks_like_virtual_function_table(vtable)) {
        return false;
    }

    uintptr_t cls{};
    if (!safe_read_value(object + sdk::UObjectBase::get_class_private_offset(), cls) || cls == 0) {
        return false;
    }

    uint32_t internal_index{};
    if (!safe_read_value(object + sdk::UObjectBase::get_internal_index_offset(), internal_index)) {
        return false;
    }

    auto object_array = sdk::FUObjectArray::get();
    if (object_array == nullptr) {
        return false;
    }

    const auto object_count = object_array->get_object_count();
    if (object_count <= 0 || internal_index >= (uint32_t)object_count) {
        return false;
    }

    auto item = object_array->get_object((int32_t)internal_index);
    if (item == nullptr || !is_readable_process_range((uintptr_t)item, sizeof(sdk::FUObjectItem))) {
        return false;
    }

    uintptr_t item_object{};
    if (!safe_read_value((uintptr_t)item + sdk::FUObjectArray::get_item_object_offset(), item_object) || item_object != object) {
        return false;
    }

    if (out_vtable != nullptr) {
        *out_vtable = vtable;
    }

    if (out_class != nullptr) {
        *out_class = cls;
    }

    return true;
}

std::optional<uintptr_t> locate_vtable_from_constructor_rip_references(uintptr_t constructor) {
    constexpr auto MAX_CONSTRUCTOR_SCAN_BYTES = 0x800;
    auto best_candidate = std::optional<uintptr_t>{};

    for (auto ip = constructor; ip < constructor + MAX_CONSTRUCTOR_SCAN_BYTES;) {
        const auto decoded = utility::decode_one((uint8_t*)ip);

        if (!decoded || decoded->Length == 0) {
            break;
        }

        if (decoded->OperandsCount >= 2 &&
            decoded->IsRipRelative &&
            (decoded->Instruction == ND_INS_LEA || decoded->Instruction == ND_INS_MOV) &&
            decoded->Operands[0].Type == ND_OP_REG &&
            decoded->Operands[1].Type == ND_OP_MEM)
        {
            const auto referenced_addr = utility::resolve_displacement(ip);

            if (referenced_addr && looks_like_virtual_function_table(*referenced_addr)) {
                SPDLOG_INFO("Found FFakeStereoRendering vtable candidate via constructor RIP reference at {:x} -> {:x}",
                            ip, *referenced_addr);
                best_candidate = *referenced_addr;
                break;
            }
        }

        if (std::string_view{decoded->Mnemonic}.starts_with("RET")) {
            break;
        }

        ip += decoded->Length;
    }

    return best_candidate;
}
}

// Scan through function instructions to detect usage of double
// floating point precision instructions.
bool is_using_double_precision(uintptr_t addr) {
    SPDLOG_INFO("Scanning function at {:x} for double precision usage", addr);

    bool result = false;

    utility::exhaustive_decode((uint8_t*)addr, 50, [&](INSTRUX& ix, uintptr_t ip) -> utility::ExhaustionResult {
        if (std::string_view{ix.Mnemonic}.starts_with("CALL")) {
            return utility::ExhaustionResult::STEP_OVER;
        }

        if (ix.Instruction == ND_INS_MOVSD && ix.Operands[0].Type == ND_OP_MEM && ix.Operands[1].Type == ND_OP_REG) {
            SPDLOG_INFO("[UE5 Detected] Detected Double precision MOVSD at {:x}", (uintptr_t)ip);
            result = true;
            return utility::ExhaustionResult::BREAK;
        }

        if (ix.Instruction == ND_INS_ADDSD) {
            SPDLOG_INFO("[UE5 Detected] Detected Double precision ADDSD at {:x}", (uintptr_t)ip);
            result = true;
            return utility::ExhaustionResult::BREAK;
        }

        return utility::ExhaustionResult::CONTINUE;
    });

    return result;
}

FFakeStereoRenderingHook::FFakeStereoRenderingHook() {
    g_hook = this;
    m_prefer_slate_thread_for_session = load_ue57_slate_thread_preference();
    setup_options();
}

void FFakeStereoRenderingHook::on_frame() {
    // Engine tick hook is always installed — plugins (including dumper-mode
    // clients) need on_pre_engine_tick callbacks to submit game-thread work.
    attempt_hook_game_engine_tick();

    // Render-pipeline hooks are the crash vector on some UE4.26.x forks
    // (RoboQuest, Stellar Blade). Dumper mode skips them entirely so
    // reflection-only plugins can run without triggering the
    // FViewport::GetRenderTargetTexture PointerHook that tears down the
    // render thread. See DumperMode.hpp.
    if (uevr::is_dumper_mode()) {
        return;
    }

    attempt_hook_slate_thread();
    attempt_hook_fsceneview_constructor();

    // Ideally we want to do all hooking
    // from game engine tick. if it fails
    // we will fall back to doing it here.
    if (!m_hooked_game_engine_tick && m_attempted_hook_game_engine_tick) {
        attempt_hooking();
    }
}


void FFakeStereoRenderingHook::on_draw_ui() {
    ZoneScopedN(__FUNCTION__);

    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::TreeNode("Stereo Hook Options")) {
        m_asynchronous_scan->draw("Asynchronous Code Scanning");
        m_recreate_textures_on_reset->draw("Recreate Textures on Reset");
        m_frame_delay_compensation->draw("Frame Delay Compensation");
        m_use_fmalloc_scene_view_extensions->draw("Use FMalloc for ISceneViewExtensions");
        m_safe_tick_hook->draw("Use Safe Tick Hooking");

        if (m_tracking_system_hook != nullptr) {
            m_tracking_system_hook->on_draw_ui();
        }

#if 0
        if (ImGui::Button("Spawn scene capture")) {
            get_render_target_manager()->create_scene_capture();
        }

        ImGui::SameLine();
        if (ImGui::Button("Destroy scene capture")) {
            get_render_target_manager()->destroy_scene_capture();
        }

        ImGui::SameLine();
        if (ImGui::Button("Create texture")) {
            get_render_target_manager()->create_scene_capture_texture();
        }

        ImGui::SameLine();
        if (ImGui::Button("Destroy texture")) {
            get_render_target_manager()->destroy_scene_capture();
        }

        bool status = false;

        if (get_render_target_manager()->get_scene_capture_utexture() != nullptr) {
            if (UObjectHook::get()->exists(get_render_target_manager()->get_scene_capture_utexture())) {
                status = true;
            }
        }
        ImGui::Text("Scene Capture Texture: %s", status ? "Exists" : "Does not exist");
#endif

        auto& data = m_viewport_rt_hook_data;
        std::scoped_lock _{data.retaddr_mutex};

        std::vector<uintptr_t> retaddrs{};
        std::vector<std::string> items{};
        for (auto& addr : data.seen_retaddrs) {
            items.push_back(fmt::format("{:x}", addr));
            retaddrs.push_back(addr);
        }
        
        std::vector<const char*> citems{};
        for (auto& item : items) {
            citems.push_back(item.c_str());
        }

        if (!items.empty()) {
            if (ImGui::BeginCombo("GetRenderTargetTexture Retaddrs", items[data.selected_retaddr].c_str())) {
                for (int n = 0; n < items.size(); n++) {
                    ImGui::PushID(n);
                    auto retaddr = retaddrs[n];
                    const bool is_selected = (data.selected_retaddr == n);

                    // Calculate the text size for the current item
                    const auto text_size = ImGui::CalcTextSize(items[n].c_str(), NULL, true);
                    const auto padding = ImGui::GetStyle().ItemSpacing.x;
                    const auto selectable_size = ImVec2{text_size.x + padding, text_size.y};

                    if (ImGui::Selectable(items[n].c_str(), is_selected, ImGuiSelectableFlags_None, selectable_size)) {
                        data.selected_retaddr = n;
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Call Original")) {
                        data.call_original_retaddrs.insert(retaddr);
                        data.redirected_retaddrs.erase(retaddr);
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Redirect")) {
                        data.redirected_retaddrs.insert(retaddr);
                        data.call_original_retaddrs.erase(retaddr);
                    }

                    ImGui::SameLine();
                    if (data.call_original_retaddrs.contains(retaddr)) {
                        ImGui::Text("[Calling Original]");
                    } else if (data.redirected_retaddrs.contains(retaddr)) {
                        ImGui::Text("[Redirected]");
                    } else {
                        ImGui::Text("[Default]");
                    }

                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }

        }

        ImGui::TreePop();
    }

    ImGui::Separator();
}

bool FFakeStereoRenderingHook::invalidate_ue57_resolution_dependent_state(
    uint32_t old_width,
    uint32_t old_height,
    uint32_t new_width,
    uint32_t new_height) {
    if (!is_ue_5_7_or_newer()) {
        return false;
    }

    SPDLOG_INFO(
        "[UE5.7][OpenXR] Resolution changed [{}x{}]->[{}x{}]; invalidating Slate/UI render targets",
        old_width,
        old_height,
        new_width,
        new_height);

    m_wants_texture_recreation = true;
    m_skip_next_adjust_view_rect = true;

    // Force the UE5.7 Slate/UI path to observe fresh post-resize DrawWindow and
    // PreRenderViewFamily traffic instead of reusing candidates captured at the
    // previous OpenXR scale.
    m_has_seen_stable_slate_draw = false;
    m_has_seen_prerender_viewfamily = false;
    m_first_stable_slate_draw_at = {};

    m_rtm.invalidate_resolution_dependent_targets();
    m_rtm_418.invalidate_resolution_dependent_targets();
    m_rtm_special.invalidate_resolution_dependent_targets();

    return true;
}

void FFakeStereoRenderingHook::attempt_hooking() {
    if (m_finished_hooking || m_tried_hooking) {
        return;
    }

    // TODO: see if this can be threaded; it might not be able to because of TLS or something
    if (!VR::get()->should_skip_uobjectarray_init()) {
        sdk::FName::get_constructor();
        sdk::FName::get_to_string();
        sdk::FUObjectArray::get();
    }

    if (!m_injected_stereo_at_runtime) {
        attempt_runtime_inject_stereo();
        m_injected_stereo_at_runtime = true;
    }
    
    m_hooked = hook();
}

namespace detail{
bool pre_find_engine_tick() {
    ZoneScopedN(__FUNCTION__);
    sdk::UGameEngine::get_tick_address(); // this takes a LONG time to find
    sdk::UGameEngine::get_initialize_hmd_device_address();
    return true;
}
}

void FFakeStereoRenderingHook::attempt_hook_game_engine_tick(uintptr_t return_address) {
    if (m_asynchronous_scan->value()) {
        static std::future<bool> future = std::async(std::launch::async, detail::pre_find_engine_tick);

        // Wait for the future to be valid before attempting to hook
        if (future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            future.get();
        } else if (future.valid()) {
            return;
        }
    }

    if (m_hooked_game_engine_tick) {
        return;
    }

    if (return_address == 0 && m_attempted_hook_game_engine_tick) {
        return;
    }
    
    SPDLOG_INFO("Attempting to hook UGameEngine::Tick!");

    m_attempted_hook_game_engine_tick = true;

    auto func = sdk::UGameEngine::get_tick_address();

    if (!func) {
        if (return_address == 0) {
            SPDLOG_ERROR("Cannot hook UGameEngine::Tick");
            return;
        }

        const auto engine_module = sdk::get_ue_module(L"Engine");
        static const auto negative_delta_time_strings = 
            utility::scan_strings(engine_module, L"Negative delta time!");
        
        if (negative_delta_time_strings.empty()) {
            SPDLOG_ERROR("Cannot hook UGameEngine::Tick (Negative delta time! not found)");
            return;
        }

        static std::vector<uintptr_t> negative_delta_time_funcs = [&]() {
            std::vector<uintptr_t> out{};

            for (auto str : negative_delta_time_strings) {
                const auto ref = utility::scan_displacement_reference(engine_module, str);

                if (!ref) {
                    continue;
                }
                //
                const auto func_start = utility::find_virtual_function_start(*ref);

                if (!func_start) {
                    continue;
                }

                SPDLOG_INFO("Negative delta time string function @ {:x}", *func_start);

                out.push_back(*func_start);
            }

            return out;
        }();

        const auto return_address_func = utility::find_virtual_function_start(return_address);

        if (!return_address_func) {
            SPDLOG_ERROR("Return address is not within a valid function!");
            return;
        }

        // Check if the return address is within one of the negative delta time functions.
        // If it is, then it's UGameEngine::Tick. Set func to the return_address_func.
        for (auto potential : negative_delta_time_funcs) {
            if (potential == *return_address_func) {
                SPDLOG_INFO("Found UGameEngine::Tick @ {:x}", *return_address_func);
                func = *return_address_func;
                break;
            }
        }

        if (!func) {
            SPDLOG_ERROR("Return address is not the correct function!");
            return;
        }
    }

    // TODO: move this to a better place
    m_tick_hook = safetyhook::create_inline((void*)*func, &engine_tick_hook, safetyhook::InlineHook::StartDisabled);

    if (!m_tick_hook) {
        SPDLOG_ERROR("Failed to hook UGameEngine::Tick!");
        return;
    }

    if (auto tick_hook_enable = m_tick_hook.enable(); !tick_hook_enable.has_value()) {
        SPDLOG_ERROR("Failed to enable UGameEngine::Tick hook! {}", (int)tick_hook_enable.error().type);
        return;
    }

    m_hooked_game_engine_tick = true;

    SPDLOG_INFO("Hooked UGameEngine::Tick!");
}

void* FFakeStereoRenderingHook::engine_tick_hook(sdk::UGameEngine* engine, float delta, bool idle) {
    ZoneScopedN("UGameEngine::Tick Hook");
    FrameMarkStart("UGameEngine::Tick");

    sdk::UEngine::set_runtime_engine(engine);

    auto hook = g_hook;
    
    hook->m_in_engine_tick = true;

    utility::ScopeGuard _{[]() {
        g_hook->m_in_engine_tick = false;
        FrameMarkEnd("UGameEngine::Tick");
    }};
    
    static bool once = true;

    if (once) {
        SPDLOG_INFO("First time calling UGameEngine::Tick!");
        once = false;
    }

    if (!g_framework->is_game_data_intialized()) {
        if (hook->m_safe_tick_hook->value()) {
            return hook->m_tick_hook.call<void*>(engine, delta, idle);
        }

        // This allocates memory on the stack.
        static bool check_canary_once = true;
        volatile uint64_t shadow_space[64]{};

#ifdef NDEBUG
        if (check_canary_once) {
#endif
            std::memset((void*)shadow_space, 0, 64 * sizeof(uint64_t));
#ifdef NDEBUG
        }
#endif
        // We're using original here instead of call_unsafe to make sure the canaries are the first thing on the stack.
        void* result = hook->m_tick_hook.original<void* (*)(sdk::UGameEngine*, float, bool)>()(engine, delta, idle);

        // At least do some logic with the shadow space so it doesn't get optimized out for some reason.
        // But only do it once in release builds.
#ifdef NDEBUG
        if (check_canary_once) {
#endif
            for (size_t i = 0; i < 64; ++i) {
                if (shadow_space[i] != 0) {
                    SPDLOG_ERROR("[UGameEngine::Tick] Shadow space was overwritten! {:x} @ {}", shadow_space[i], i);
                }
            }

#ifdef NDEBUG
            check_canary_once = false;
        }
#endif

        return result;
    }

    // Dumper mode: skip render-pipeline hooks (see DumperMode.hpp). Engine
    // tick dispatch below still runs, so plugins receive on_pre_engine_tick.
    if (!uevr::is_dumper_mode()) {
        hook->attempt_hooking();
    }

    // Best place to run game thread jobs.
    GameThreadWorker::get().execute();

    if (hook->m_ignore_next_engine_tick) {
        hook->m_ignored_engine_delta = delta;
        hook->m_ignore_next_engine_tick = false;
        return nullptr;
    }

    // Dumper mode: skip the imgui-frame + engine-thread-enable logic. ImGui
    // needs a D3D device + swapchain that we never installed, and
    // enable_engine_thread is a VR-only optimization. The mod fan-out below
    // still runs, so plugins still get on_pre_engine_tick callbacks.
    if (!uevr::is_dumper_mode()) {
        g_framework->enable_engine_thread();
        g_framework->run_imgui_frame(false);
    }

    delta += hook->m_ignored_engine_delta;
    hook->m_ignored_engine_delta = 0.0f;

    if (hook->m_tracking_system_hook != nullptr) {
        hook->m_tracking_system_hook->on_pre_engine_tick(engine, delta);
    }

    const auto& mods = g_framework->get_mods()->get_mods();
    for (auto& mod : mods) {
        mod->on_pre_engine_tick(engine, delta);
    }

    void* result = nullptr;

    {
        if (hook->m_safe_tick_hook->value()) {
            result = hook->m_tick_hook.call<void*>(engine, delta, idle);
        } else {
            // This allocates memory on the stack.
            static bool check_canary_once = true;
            volatile uint64_t shadow_space[64]{};

#ifdef NDEBUG
            if (check_canary_once) {
#endif
                std::memset((void*)shadow_space, 0, 64 * sizeof(uint64_t));
#ifdef NDEBUG
            }
#endif
            // We're using original here instead of call_unsafe to make sure the canaries are the first thing on the stack.
            result = hook->m_tick_hook.original<void* (*)(sdk::UGameEngine*, float, bool)>()(engine, delta, idle);

            // At least do some logic with the shadow space so it doesn't get optimized out for some reason.
            // But only do it once in release builds.
#ifdef NDEBUG
            if (check_canary_once) {
#endif
                for (size_t i = 0; i < 64; ++i) {
                    if (shadow_space[i] != 0) {
                        SPDLOG_ERROR("[UGameEngine::Tick] Shadow space was overwritten! {:x} @ {}", shadow_space[i], i);
                    }
                }

#ifdef NDEBUG
                check_canary_once = false;
            }
#endif
        }
    }

    for (auto& mod : mods) {
        mod->on_post_engine_tick(engine, delta);
    }

    if (hook->m_tracking_system_hook != nullptr) {
        hook->m_tracking_system_hook->on_post_engine_tick(engine, delta);
    }

    return result;
}

namespace detail{
bool pre_find_slate_thread() {
    sdk::slate::locate_draw_window_renderthread_fn(); // Can take a while to find
    sdk::slate::locate_draw_window_renderthread_fn_alternate();
    return true;
}
}

void FFakeStereoRenderingHook::attempt_hook_slate_thread(uintptr_t return_address, bool alternate) {
    if (m_asynchronous_scan->value()) {
        static std::future<bool> future = std::async(std::launch::async, detail::pre_find_slate_thread);

        // Wait for the future to be valid before attempting to hook
        if (future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            future.get();
        } else if (future.valid()) {
            return;
        }
    }

    if (m_hooked_slate_thread && !alternate) {
        return;
    }

    const auto attempted = alternate ? m_attempted_hook_slate_thread_alternate : m_attempted_hook_slate_thread;

    if (return_address == 0 && attempted) {
        return;
    }

    SPDLOG_INFO("Attempting to hook FSlateRHIRenderer::DrawWindow_RenderThread!");

    if (alternate) {
        SPDLOG_INFO("Using alternate method to hook FSlateRHIRenderer::DrawWindow_RenderThread!");
        m_attempted_hook_slate_thread_alternate = true;
    } else {
        m_attempted_hook_slate_thread = true;
    }

    auto func = alternate ? sdk::slate::locate_draw_window_renderthread_fn_alternate() : sdk::slate::locate_draw_window_renderthread_fn();

    if (!func && !alternate) {
        func = sdk::slate::locate_draw_window_renderthread_fn_alternate();

        if (func) {
            SPDLOG_INFO("Using alternate SlateRHIRenderer::DrawWindow_RenderThread scan result after primary scan failed");
            m_attempted_hook_slate_thread_alternate = true;
        }
    }

    if (!func && return_address == 0) {
        SPDLOG_ERROR("Cannot hook FSlateRHIRenderer::DrawWindow_RenderThread");
        return;
    }

    if (return_address != 0) {
        func = utility::find_function_start_with_call(return_address);

        if (!func) {
            SPDLOG_ERROR("Cannot hook FSlateRHIRenderer::DrawWindow_RenderThread with alternative return address method");
            m_hooked_slate_thread = true; // not actually true but just to stop spamming the scans
            return;
        }

        SPDLOG_INFO("Checking if the assembly listing for {:X} is really small", *func);

        // Check if the assembly listing for this function is really small. It shouldn't be really small.
        // This will happen on UE 5.5+ where RenderTexture_RenderThread is enqueued inside of a lambda.
        size_t distance_to_ret = 0;
        utility::exhaustive_decode((uint8_t*)*func, 1000, [&](utility::ExhaustionContext& ctx2) -> utility::ExhaustionResult {
            ++distance_to_ret;

            if (ctx2.instrux.BranchInfo.IsBranch && std::string_view{ctx2.instrux.Mnemonic}.starts_with("CALL")) {
                return utility::ExhaustionResult::STEP_OVER;
            }

            return utility::ExhaustionResult::CONTINUE;
        });

        if (distance_to_ret < 50) {
            SPDLOG_ERROR("FSlateRHIRenderer::DrawWindow_RenderThread function is too small! Distance to RET: {}", distance_to_ret);
            m_hooked_slate_thread = true; // not actually true but just to stop spamming the scans
            return;
        }

        SPDLOG_INFO("Found FSlateRHIRenderer::DrawWindow_RenderThread with alternative return address method: {:x}", *func);
    }

    m_slate_thread_hook = safetyhook::create_inline((void*)*func, &FFakeStereoRenderingHook::slate_draw_window_render_thread, safetyhook::InlineHook::StartDisabled);
    m_hooked_slate_thread = true;

    if (!m_slate_thread_hook) {
        SPDLOG_ERROR("Failed to hook FSlateRHIRenderer::DrawWindow_RenderThread!");
        return;
    }

    if (auto enable_result = m_slate_thread_hook.enable(); !enable_result.has_value()) {
        SPDLOG_ERROR("Failed to enable FSlateRHIRenderer::DrawWindow_RenderThread hook! {}", (int)enable_result.error().type);
        return;
    }

    SPDLOG_INFO("Hooked FSlateRHIRenderer::DrawWindow_RenderThread!");

    // UE5.7 elements-pass inspection is a fallback only. Let the DrawWindow path
    // try the dedicated UI target first before probing extra callsites.
}

void FFakeStereoRenderingHook::attempt_hook_ue57_slate_elements_pass() {
    if (!is_ue_5_7_or_newer() || !g_framework->is_dx12()) {
        return;
    }

    if (m_attempted_hook_ue57_slate_elements_pass) {
        return;
    }

    if (const auto rtm = get_render_target_manager(); rtm != nullptr && rtm->has_dedicated_ui_target()) {
        SPDLOG_INFO_ONCE("Skipping UE 5.7 Slate elements-pass fallback because the dedicated UI target is active");
        return;
    }

    m_attempted_hook_ue57_slate_elements_pass = true;

    const auto draw_window = g_hook->m_slate_thread_hook.target_address();

    if (draw_window == 0) {
        SPDLOG_ERROR("Cannot scan UE 5.7 DrawWindow_RenderThread for Slate callsites because the Slate hook has no target address");
        return;
    }

    const auto module_within = utility::get_module_within(draw_window);

    if (!module_within.has_value()) {
        SPDLOG_ERROR("Cannot scan UE 5.7 DrawWindow_RenderThread for Slate callsites because the module was not resolved");
        return;
    }

    const auto& slate_symbols = vrmod::get_ue57_slate_symbols();

    if (slate_symbols.add_slate_draw_elements_pass != 0 &&
        is_executable_process_range(slate_symbols.add_slate_draw_elements_pass, 1)) {
        const auto symbol_module = utility::get_module_within(reinterpret_cast<void*>(slate_symbols.add_slate_draw_elements_pass));

        if (symbol_module.has_value()) {
            auto hook_result = safetyhook::create_mid(
                reinterpret_cast<void*>(slate_symbols.add_slate_draw_elements_pass),
                &FFakeStereoRenderingHook::ue57_add_slate_draw_elements_pass_hook);

            if (hook_result) {
                m_ue57_slate_elements_hooks.emplace_back(std::move(hook_result));
                m_hooked_ue57_slate_elements_pass = true;
                SPDLOG_INFO("Hooked UE 5.7 AddSlateDrawElementsPass by symbol at {:x}", slate_symbols.add_slate_draw_elements_pass);
                return;
            }

            SPDLOG_WARN("Failed to hook UE 5.7 AddSlateDrawElementsPass symbol at {:x}", slate_symbols.add_slate_draw_elements_pass);
        } else {
            SPDLOG_INFO("Ignoring UE 5.7 AddSlateDrawElementsPass symbol {:x} because its module could not be resolved",
                slate_symbols.add_slate_draw_elements_pass);
        }
    }

    struct DirectCall {
        uintptr_t callsite{};
        uintptr_t target{};
        uint8_t length{};
    };

    std::vector<DirectCall> direct_calls{};

    utility::exhaustive_decode((uint8_t*)draw_window, 0x1200, [&](utility::ExhaustionContext& ctx) -> utility::ExhaustionResult {
        const auto mnemonic = std::string_view{ctx.instrux.Mnemonic};

        if (!mnemonic.starts_with("CALL")) {
            return utility::ExhaustionResult::CONTINUE;
        }

        const auto target = utility::resolve_displacement(ctx.addr);

        if (!target.has_value()) {
            return utility::ExhaustionResult::CONTINUE;
        }

        const auto called_module = utility::get_module_within((void*)*target);

        if (!called_module.has_value() || *called_module != *module_within) {
            return utility::ExhaustionResult::CONTINUE;
        }

        direct_calls.push_back({ctx.addr, *target, (uint8_t)ctx.instrux.Length});
        return utility::ExhaustionResult::CONTINUE;
    });

    std::unordered_map<uintptr_t, std::vector<DirectCall>> grouped_calls{};

    for (const auto& call : direct_calls) {
        grouped_calls[call.target].push_back(call);
    }

    std::vector<DirectCall> candidate_calls{};
    std::unordered_set<uintptr_t> candidate_calls_seen{};

    for (const auto& [target, calls] : grouped_calls) {
        if (calls.size() >= 2) {
            for (const auto& call : calls) {
                if (candidate_calls_seen.insert(call.callsite).second) {
                    candidate_calls.push_back(call);
                }
            }
        }
    }

    // UE5.7.3 may only emit one direct call to AddSlateDrawElementsPass in optimized shipping builds.
    // Hook a capped set of single-call candidates and let the hook validate the r8 input shape at runtime.
    for (const auto& call : direct_calls) {
        if (candidate_calls_seen.insert(call.callsite).second) {
            candidate_calls.push_back(call);
        }
    }

    if (candidate_calls.empty()) {
        const auto rtm = get_render_target_manager();

        if (rtm != nullptr && rtm->has_dedicated_ui_target()) {
            SPDLOG_INFO("No direct-call candidates inside UE 5.7 DrawWindow_RenderThread; dedicated UI target is already active");
        } else {
            SPDLOG_WARN("Could not find direct-call candidates inside UE 5.7 DrawWindow_RenderThread for ElementsTexture inspection");
        }

        return;
    }

    size_t hooked_count{};
    constexpr size_t max_ue57_slate_callsite_hooks = 32;

    for (const auto& call : candidate_calls) {
        if (hooked_count >= max_ue57_slate_callsite_hooks) {
            break;
        }

        auto hook_result = safetyhook::create_mid(
            reinterpret_cast<void*>(call.callsite),
            &FFakeStereoRenderingHook::ue57_add_slate_draw_elements_pass_hook);

        if (!hook_result) {
            SPDLOG_WARN("Failed to hook UE 5.7 DrawWindow_RenderThread callsite {:x} -> {:x}", call.callsite, call.target);
            continue;
        }

        m_ue57_slate_elements_hooks.emplace_back(std::move(hook_result));
        ++hooked_count;
    }

    if (hooked_count == 0) {
        SPDLOG_WARN("Failed to hook any UE 5.7 DrawWindow_RenderThread callsites for ElementsTexture inspection");
        return;
    }

    m_hooked_ue57_slate_elements_pass = true;
    SPDLOG_INFO(
        "Hooked {} UE 5.7 DrawWindow_RenderThread callsites for validated ElementsTexture inspection ({} same-module calls found)",
        hooked_count,
        direct_calls.size());
}

void FFakeStereoRenderingHook::attempt_hook_ue55_slate_output_texture_register() {
    if (!supports_ue55_dedicated_ui_target_for_current_game()) {
        return;
    }

    if (m_attempted_hook_ue55_slate_output_texture_register) {
        return;
    }

    m_attempted_hook_ue55_slate_output_texture_register = true;

    const auto draw_window = g_hook != nullptr ? g_hook->m_slate_thread_hook.target_address() : 0;

    if (draw_window == 0) {
        SPDLOG_ERROR("[UE5.5][SlateUI] Cannot scan DrawWindow_RenderThread because the Slate hook has no target address");
        return;
    }

    uintptr_t slate_output_ref_ip{};
    uintptr_t register_callsite{};
    uintptr_t register_target{};
    bool after_slate_output_ref = false;

    // Decode linearly here. exhaustive_decode follows early CALL branches in this
    // function and can miss the straight-line RegisterExternalTexture callsite.
    for (auto* ip = (uint8_t*)draw_window; (uintptr_t)ip < draw_window + 0x3000;) {
        const auto decoded = utility::decode_one(ip);

        if (!decoded) {
            break;
        }

        if (!after_slate_output_ref) {
            const auto referenced = utility::resolve_displacement((uintptr_t)ip);

            if (referenced &&
                is_readable_process_range(*referenced, sizeof(wchar_t) * 19) &&
                std::wstring_view{(const wchar_t*)*referenced, 18}.starts_with(L"SlateOutputTexture"))
            {
                slate_output_ref_ip = (uintptr_t)ip;
                after_slate_output_ref = true;
                SPDLOG_INFO("[UE5.5][SlateUI] found SlateOutputTexture reference at {:x}", slate_output_ref_ip);
            }

            ip += decoded->Length;
            continue;
        }

        const auto mnemonic = std::string_view{decoded->Mnemonic};

        if (mnemonic.starts_with("CALL")) {
            const auto target = utility::resolve_displacement((uintptr_t)ip);

            if (target.has_value()) {
                register_callsite = (uintptr_t)ip;
                register_target = *target;
                break;
            }
        }

        if (decoded->Instruction == ND_INS_RETN || decoded->Instruction == ND_INS_INT3) {
            break;
        }

        ip += decoded->Length;
    }

    if (slate_output_ref_ip == 0 || register_callsite == 0) {
        SPDLOG_ERROR("[UE5.5][SlateUI] Failed to find SlateOutputTexture RegisterExternalTexture callsite in DrawWindow_RenderThread");
        return;
    }

    auto hook_result = safetyhook::create_mid(
        reinterpret_cast<void*>(register_callsite),
        &FFakeStereoRenderingHook::ue55_slate_output_texture_register_hook);

    if (!hook_result) {
        SPDLOG_ERROR("[UE5.5][SlateUI] Failed to hook SlateOutputTexture RegisterExternalTexture callsite {:x}", register_callsite);
        return;
    }

    m_ue55_slate_output_texture_register_hook = std::move(hook_result);
    m_hooked_ue55_slate_output_texture_register = true;

    SPDLOG_WARN("[UE5.5][SlateUI] Hooked SlateOutputTexture RegisterExternalTexture callsite {:x} -> {:x}", register_callsite, register_target);
}

namespace detail{
bool pre_find_fsceneview_constructor() {
    sdk::FSceneView::get_constructor_address(); // Can take a while to find
    return true;
}
}

void FFakeStereoRenderingHook::attempt_hook_fsceneview_constructor() {
    if (m_attempted_hook_fsceneview_constructor) {
        return;
    }
    
    // just try to find it before ghosting fix is even enabled
    if (m_asynchronous_scan->value()) {
        static std::future<bool> future = std::async(std::launch::async, detail::pre_find_fsceneview_constructor);

        // Wait for the future to be valid before attempting to hook
        if (future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            future.get();
        } else if (future.valid()) {
            return;
        }
    }

    auto& vr = VR::get();

    if (!vr->is_ghosting_fix_enabled() && !vr->is_splitscreen_compatibility_enabled() && !vr->is_sceneview_compatibility_enabled() && !vr->is_native_stereo_fix_enabled()) {
        return;
    }

    utility::ScopeGuard _{[&]() {
        m_attempted_hook_fsceneview_constructor = true;
    }};

    SPDLOG_INFO("Attempting to hook FSceneView::FSceneView constructor!");
    const auto constructor = sdk::FSceneView::get_constructor_address();

    if (!constructor) {
        SPDLOG_ERROR("Cannot hook FSceneView::FSceneView constructor");
        return;
    }

    g_hook->m_sceneview_data.constructor_hook = safetyhook::create_inline(*constructor, (uintptr_t)&sceneview_constructor, safetyhook::InlineHook::StartDisabled);

    if (!g_hook->m_sceneview_data.constructor_hook) {
        SPDLOG_ERROR("Failed to hook FSceneView::FSceneView constructor!");
        return;
    }

    if (auto enable_result = g_hook->m_sceneview_data.constructor_hook.enable(); !enable_result.has_value()) {
        SPDLOG_ERROR("Failed to enable FSceneView::FSceneView constructor hook! {}", (int)enable_result.error().type);
        return;
    }

    SPDLOG_INFO("Hooked FSceneView::FSceneView constructor!");
}

void FFakeStereoRenderingHook::attempt_hook_subnautica2_compute_volumetric_fog() {
    if (m_attempted_hook_subnautica2_compute_volumetric_fog) {
        return;
    }

    m_attempted_hook_subnautica2_compute_volumetric_fog = true;

    if (!subnautica2_is_current_game()) {
        return;
    }

    if (subnautica2_disable_volumetric_fog_view_loop_fix() &&
        !subnautica2_enable_volumetric_fog_state_copy() &&
        !subnautica2_enable_fog_rect_widen() &&
        !subnautica2_enable_fog_view1_matrix_swap()) {
        SPDLOG_WARN("[Subnautica2][VolumetricFog] Hook disabled (view-loop-fix=disabled AND state-copy=disabled AND rect-widen=disabled AND matrix-swap=disabled)");
        return;
    }
    if (subnautica2_enable_volumetric_fog_state_copy()) {
        SPDLOG_WARN("[Subnautica2][VolumetricFog] State-copy mode enabled (post-compute view0->view1 state copy)");
    }
    if (subnautica2_enable_fog_rect_widen()) {
        SPDLOG_WARN("[Subnautica2][VolumetricFog] Rect-widen mode enabled (FIX-V3: widen view 0 rect to full SBS X range)");
    }
    if (subnautica2_enable_fog_view1_matrix_swap()) {
        SPDLOG_WARN(
            "[Subnautica2][VolumetricFog] View1 matrix-swap mode enabled (copy view 0 matrices to view 1 at offset 0x{:x} size {} bytes for fog compute, then restore)",
            subnautica2_fog_view1_matrix_swap_offset(),
            subnautica2_fog_view1_matrix_swap_size());
    }

    const auto exe_base = (uintptr_t)utility::get_executable();
    const auto target = exe_base + SUBNAUTICA2_COMPUTE_VOLUMETRIC_FOG_RVA;

    if (exe_base == 0 || !is_executable_process_range(target, 0x20)) {
        SPDLOG_WARN("[Subnautica2][VolumetricFog] Cannot hook ComputeVolumetricFog; bad target {:x}", target);
        return;
    }

    constexpr uint8_t expected_prologue[] = {
        0x40, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x57,
        0x48, 0x8D, 0xAC, 0x24, 0xA8, 0xF3, 0xFF, 0xFF,
    };

    if (std::memcmp((void*)target, expected_prologue, sizeof(expected_prologue)) != 0) {
        SPDLOG_WARN("[Subnautica2][VolumetricFog] ComputeVolumetricFog prologue mismatch at {:x}; skipping hook for this build", target);
        return;
    }

    m_subnautica2_compute_volumetric_fog_hook = safetyhook::create_inline(
        (void*)target,
        &FFakeStereoRenderingHook::subnautica2_compute_volumetric_fog_hook,
        safetyhook::InlineHook::StartDisabled);

    if (!m_subnautica2_compute_volumetric_fog_hook) {
        SPDLOG_WARN("[Subnautica2][VolumetricFog] Failed to create ComputeVolumetricFog hook at {:x}", target);
        return;
    }

    if (auto enable_result = m_subnautica2_compute_volumetric_fog_hook.enable(); !enable_result.has_value()) {
        SPDLOG_WARN("[Subnautica2][VolumetricFog] Failed to enable ComputeVolumetricFog hook at {:x}: {}", target, (int)enable_result.error().type);
        return;
    }

    if (subnautica2_enable_volumetric_fog_view_index_force()) {
        const auto view_index_store = exe_base + SUBNAUTICA2_COMPUTE_VOLUMETRIC_FOG_VIEW_INDEX_STORE_RVA;
        constexpr uint8_t expected_view_index_store[] = {
            0x44, 0x89, 0x65, 0xE4, // mov [rbp-1Ch], r12d
        };

        if (!is_executable_process_range(view_index_store, sizeof(expected_view_index_store)) ||
            std::memcmp((void*)view_index_store, expected_view_index_store, sizeof(expected_view_index_store)) != 0)
        {
            SPDLOG_WARN(
                "[Subnautica2][VolumetricFog] ViewIndex store mismatch at {:x}; falling back to shifted single-view second fog build",
                view_index_store);
        } else {
            auto view_index_hook = safetyhook::create_mid(
                (void*)view_index_store,
                &FFakeStereoRenderingHook::subnautica2_compute_volumetric_fog_force_view_index_hook);

            if (view_index_hook) {
                m_subnautica2_compute_volumetric_fog_view_index_hook = std::move(view_index_hook);
                SPDLOG_WARN(
                    "[Subnautica2][VolumetricFog] Hooked ComputeVolumetricFog ViewIndex store at {:x}; second fog build will run as ViewIndex=1",
                    view_index_store);
            } else {
                SPDLOG_WARN(
                    "[Subnautica2][VolumetricFog] Failed to hook ViewIndex store at {:x}; falling back to shifted single-view second fog build",
                    view_index_store);
            }
        }
    } else {
        SPDLOG_WARN(
            "[Subnautica2][VolumetricFog] ViewIndex force is disabled; using shifted single-view second fog build only. Set UEVR_SUBNAUTICA2_ENABLE_VOLUMETRIC_FOG_VIEW_INDEX_FORCE=1 to test the experimental mid-hook.");
    }

    SPDLOG_WARN("[Subnautica2][VolumetricFog] Hooked ComputeVolumetricFog at {:x}; right eye will get a second fog build", target);
}

void FFakeStereoRenderingHook::attempt_hook_subnautica2_init_volumetric_render_target() {
    if (m_attempted_hook_subnautica2_init_volumetric_render_target) {
        return;
    }

    m_attempted_hook_subnautica2_init_volumetric_render_target = true;

    if (!subnautica2_is_current_game()) {
        return;
    }

    const auto exe_base = (uintptr_t)utility::get_executable();
    const auto target = exe_base + SUBNAUTICA2_INIT_VOLUMETRIC_RENDER_TARGET_RVA;

    if (exe_base == 0 || !is_executable_process_range(target, 0x20)) {
        SPDLOG_WARN("[Subnautica2][VRTInit] Cannot hook InitVolumetricRenderTargetForViews; bad target {:x}", target);
        return;
    }

    constexpr uint8_t expected_prologue[] = {
        0x40, 0x55,                   // push rbp
        0x41, 0x56,                   // push r14
        0x48, 0x8D, 0xAC, 0x24, 0x08, 0xCF, 0xFF, 0xFF, // lea rbp, [rsp-30F8h]
        0xB8, 0xF8, 0x31, 0x00, 0x00, // mov eax, 31F8h
    };

    if (std::memcmp((void*)target, expected_prologue, sizeof(expected_prologue)) != 0) {
        SPDLOG_WARN("[Subnautica2][VRTInit] InitVolumetricRenderTargetForViews prologue mismatch at {:x}; skipping hook for this build", target);
        return;
    }

    m_subnautica2_init_volumetric_render_target_hook = safetyhook::create_inline(
        (void*)target,
        &FFakeStereoRenderingHook::subnautica2_init_volumetric_render_target_hook,
        safetyhook::InlineHook::StartDisabled);

    if (!m_subnautica2_init_volumetric_render_target_hook) {
        SPDLOG_WARN("[Subnautica2][VRTInit] Failed to create InitVolumetricRenderTargetForViews hook at {:x}", target);
        return;
    }

    if (auto enable_result = m_subnautica2_init_volumetric_render_target_hook.enable(); !enable_result.has_value()) {
        SPDLOG_WARN("[Subnautica2][VRTInit] Failed to enable InitVolumetricRenderTargetForViews hook at {:x}: {}", target, (int)enable_result.error().type);
        return;
    }

    SPDLOG_WARN("[Subnautica2][VRTInit] Hooked InitVolumetricRenderTargetForViews at {:x}; logging per-eye cloud VRT state before/after init", target);
}

void FFakeStereoRenderingHook::attempt_hook_subnautica2_reconstruct_volumetric_render_target() {
    if (m_attempted_hook_subnautica2_reconstruct_volumetric_render_target) {
        return;
    }

    m_attempted_hook_subnautica2_reconstruct_volumetric_render_target = true;

    if (!subnautica2_is_current_game()) {
        return;
    }

    const auto exe_base = (uintptr_t)utility::get_executable();
    const auto target = exe_base + SUBNAUTICA2_RECONSTRUCT_VOLUMETRIC_RENDER_TARGET_RVA;

    if (exe_base == 0 || !is_executable_process_range(target, 0x20)) {
        SPDLOG_WARN("[Subnautica2][VRTReconstruct] Cannot hook ReconstructVolumetricRenderTarget; bad target {:x}", target);
        return;
    }

    constexpr uint8_t expected_prologue[] = {
        0x4C, 0x8B, 0xDC,             // mov r11, rsp
        0x55,                         // push rbp
        0x57,                         // push rdi
        0x41, 0x56,                   // push r14
        0x41, 0x57,                   // push r15
        0x49, 0x8D, 0xAB, 0xC8, 0xFE, 0xFF, 0xFF, // lea rbp, [r11-138h]
    };

    if (std::memcmp((void*)target, expected_prologue, sizeof(expected_prologue)) != 0) {
        SPDLOG_WARN("[Subnautica2][VRTReconstruct] ReconstructVolumetricRenderTarget prologue mismatch at {:x}; skipping hook for this build", target);
        return;
    }

    m_subnautica2_reconstruct_volumetric_render_target_hook = safetyhook::create_inline(
        (void*)target,
        &FFakeStereoRenderingHook::subnautica2_reconstruct_volumetric_render_target_hook,
        safetyhook::InlineHook::StartDisabled);

    if (!m_subnautica2_reconstruct_volumetric_render_target_hook) {
        SPDLOG_WARN("[Subnautica2][VRTReconstruct] Failed to create ReconstructVolumetricRenderTarget hook at {:x}", target);
        return;
    }

    if (auto enable_result = m_subnautica2_reconstruct_volumetric_render_target_hook.enable(); !enable_result.has_value()) {
        SPDLOG_WARN("[Subnautica2][VRTReconstruct] Failed to enable ReconstructVolumetricRenderTarget hook at {:x}: {}", target, (int)enable_result.error().type);
        return;
    }

    SPDLOG_WARN("[Subnautica2][VRTReconstruct] Hooked ReconstructVolumetricRenderTarget at {:x}; logging per-eye cloud VRT state before/after reconstruct", target);
}

void FFakeStereoRenderingHook::attempt_hook_subnautica2_compose_volumetric_render_target() {
    if (m_attempted_hook_subnautica2_compose_volumetric_render_target) {
        return;
    }

    m_attempted_hook_subnautica2_compose_volumetric_render_target = true;

    if (!subnautica2_is_current_game()) {
        return;
    }

    const auto exe_base = (uintptr_t)utility::get_executable();
    const auto target = exe_base + SUBNAUTICA2_COMPOSE_VOLUMETRIC_OVER_SCENE_RVA;

    if (exe_base == 0 || !is_executable_process_range(target, 0x20)) {
        SPDLOG_WARN("[Subnautica2][ComposeVolumetric] Cannot hook ComposeVolumetricRenderTargetOverScene; bad target {:x}", target);
        return;
    }

    constexpr uint8_t expected_prologue[] = {
        0x4C, 0x8B, 0xDC,             // mov r11, rsp
        0x55,                         // push rbp
        0x57,                         // push rdi
        0x41, 0x54,                   // push r12
        0x41, 0x56,                   // push r14
        0x49, 0x8D, 0xAB, 0x78, 0xFE, 0xFF, 0xFF, // lea rbp, [r11-188h]
    };

    if (std::memcmp((void*)target, expected_prologue, sizeof(expected_prologue)) != 0) {
        SPDLOG_WARN("[Subnautica2][ComposeVolumetric] ComposeVolumetricRenderTargetOverScene prologue mismatch at {:x}; skipping hook for this build", target);
        return;
    }

    m_subnautica2_compose_volumetric_render_target_hook = safetyhook::create_inline(
        (void*)target,
        &FFakeStereoRenderingHook::subnautica2_compose_volumetric_render_target_hook,
        safetyhook::InlineHook::StartDisabled);

    if (!m_subnautica2_compose_volumetric_render_target_hook) {
        SPDLOG_WARN("[Subnautica2][ComposeVolumetric] Failed to create ComposeVolumetricRenderTargetOverScene hook at {:x}", target);
        return;
    }

    if (auto enable_result = m_subnautica2_compose_volumetric_render_target_hook.enable(); !enable_result.has_value()) {
        SPDLOG_WARN("[Subnautica2][ComposeVolumetric] Failed to enable ComposeVolumetricRenderTargetOverScene hook at {:x}: {}", target, (int)enable_result.error().type);
        return;
    }

    SPDLOG_WARN("[Subnautica2][ComposeVolumetric] Hooked ComposeVolumetricRenderTargetOverScene at {:x}; logging per-eye volumetric/cloud/water state", target);
}

void FFakeStereoRenderingHook::attempt_hook_subnautica2_basepass_pso_select() {
    if (m_attempted_hook_subnautica2_basepass_pso_select) {
        return;
    }

    m_attempted_hook_subnautica2_basepass_pso_select = true;

    if (!subnautica2_is_current_game()) {
        return;
    }

    // Default DISABLED: the MidHook fires ~96 times in 17ms on entering the
    // main menu and was correlated with downstream instability. The data it
    // already produced (every call goes through case-0 / a3=0, never the
    // other 6 cases) is sufficient to know the case branch isn't where the
    // per-view divergence lives. Set UEVR_SUBNAUTICA2_ENABLE_BASEPASS_PSO_HOOK=1
    // to re-enable for further diagnostics.
    {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_ENABLE_BASEPASS_PSO_HOOK", value, (DWORD)std::size(value));
        const bool enabled = len > 0 && len < std::size(value) &&
            value[0] != L'\0' && value[0] != L'0' && value[0] != L'f' && value[0] != L'F';
        if (!enabled) {
            SPDLOG_INFO("[Subnautica2][BasePassPSO] Hook skipped (set UEVR_SUBNAUTICA2_ENABLE_BASEPASS_PSO_HOOK=1 to enable)");
            return;
        }
    }

    const auto exe_base = (uintptr_t)utility::get_executable();
    const auto target = exe_base + SUBNAUTICA2_BASEPASS_PSO_SELECT_RVA;

    if (exe_base == 0 || !is_executable_process_range(target, 0x20)) {
        SPDLOG_WARN("[Subnautica2][BasePassPSO] Cannot hook sub_142631130; bad target {:x}", target);
        return;
    }

    constexpr uint8_t expected_prologue[] = {
        0x48, 0x83, 0xEC, 0x58,             // sub rsp, 58h
        0x45, 0x8B, 0xD1,                   // mov r10d, r9d
        0x41, 0x83, 0xF8, 0x06,             // cmp r8d, 6
    };

    if (std::memcmp((void*)target, expected_prologue, sizeof(expected_prologue)) != 0) {
        SPDLOG_WARN("[Subnautica2][BasePassPSO] sub_142631130 prologue mismatch at {:x}; skipping hook for this build", target);
        return;
    }

    // MidHook at function entry. We patch IN PLACE before the original
    // executes, so the original's stack args remain undisturbed. The callback
    // can read/modify r8d (the 7-way switch input) via the Context and the
    // original function continues normally.
    auto hook_result = safetyhook::create_mid(
        (void*)target,
        &FFakeStereoRenderingHook::subnautica2_basepass_pso_select_hook);

    if (!hook_result) {
        SPDLOG_WARN("[Subnautica2][BasePassPSO] Failed to create sub_142631130 mid hook at {:x}", target);
        return;
    }

    m_subnautica2_basepass_pso_select_hook = std::move(hook_result);

    SPDLOG_WARN("[Subnautica2][BasePassPSO] Hooked sub_142631130 at {:x}; logging permutation key per call and forcing via env var UEVR_SUBNAUTICA2_BASEPASS_PSO_FORCE_A3 (0..6) if set", target);
}

void FFakeStereoRenderingHook::attempt_hook_subnautica2_basepass_mp_ctor() {
    if (m_attempted_hook_subnautica2_basepass_mp_ctor) {
        return;
    }

    m_attempted_hook_subnautica2_basepass_mp_ctor = true;

    if (!subnautica2_is_current_game()) {
        return;
    }
    if (subnautica2_diag_clean_mode()) {
        SPDLOG_INFO("[Subnautica2][DiagClean] Skipping BasePassMPCtor hook");
        return;
    }

    // Default DISABLED. The InlineHook with 9 forwarded args appears to
    // hang the game (MetaXR preview goes black, no frames submitted). The
    // calling-convention/arg-forwarding for the 9-arg signature needs more
    // investigation. Set UEVR_SUBNAUTICA2_ENABLE_BASEPASS_MP_CTOR_FIX=1 to
    // re-enable for diagnostics. The fix theory is sound: force secondary
    // view's FBasePassMeshProcessor+0x8C cull threshold to match primary's
    // so both views select the same shader-type-getter -> same shader.
    {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_ENABLE_BASEPASS_MP_CTOR_FIX", value, (DWORD)std::size(value));
        const bool enabled = len > 0 && len < std::size(value) &&
            value[0] != L'\0' && value[0] != L'0' && value[0] != L'f' && value[0] != L'F';
        if (!enabled) {
            SPDLOG_INFO("[Subnautica2][BasePassMPCtor] Hook skipped (set UEVR_SUBNAUTICA2_ENABLE_BASEPASS_MP_CTOR_FIX=1 to enable)");
            return;
        }
    }

    const auto exe_base = (uintptr_t)utility::get_executable();
    const auto target = exe_base + SUBNAUTICA2_BASEPASS_MP_CTOR_RVA;

    if (exe_base == 0 || !is_executable_process_range(target, 0x20)) {
        SPDLOG_WARN("[Subnautica2][BasePassMPCtor] Cannot hook sub_14263A3A0; bad target {:x}", target);
        return;
    }

    constexpr uint8_t expected_prologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08,       // mov [rsp+arg_0], rbx
        0x48, 0x89, 0x6C, 0x24, 0x10,       // mov [rsp+arg_8], rbp
        0x48, 0x89, 0x74, 0x24, 0x18,       // mov [rsp+arg_10], rsi
    };

    if (std::memcmp((void*)target, expected_prologue, sizeof(expected_prologue)) != 0) {
        SPDLOG_WARN("[Subnautica2][BasePassMPCtor] sub_14263A3A0 prologue mismatch at {:x}; skipping hook for this build", target);
        return;
    }

    m_subnautica2_basepass_mp_ctor_hook = safetyhook::create_inline(
        (void*)target,
        &FFakeStereoRenderingHook::subnautica2_basepass_mp_ctor_hook,
        safetyhook::InlineHook::StartDisabled);

    if (!m_subnautica2_basepass_mp_ctor_hook) {
        SPDLOG_WARN("[Subnautica2][BasePassMPCtor] Failed to create sub_14263A3A0 hook at {:x}", target);
        return;
    }

    if (auto enable_result = m_subnautica2_basepass_mp_ctor_hook.enable(); !enable_result.has_value()) {
        SPDLOG_WARN("[Subnautica2][BasePassMPCtor] Failed to enable sub_14263A3A0 hook at {:x}: {}", target, (int)enable_result.error().type);
        return;
    }

    SPDLOG_WARN("[Subnautica2][BasePassMPCtor] Hooked FBasePassMeshProcessor ctor at {:x}; forcing per-view cull threshold (+0x8C) to match primary view's value so both eyes select the same shader", target);
}

void FFakeStereoRenderingHook::attempt_hook_subnautica2_basepass_cull_threshold() {
    if (m_attempted_hook_subnautica2_basepass_cull_threshold) {
        return;
    }

    m_attempted_hook_subnautica2_basepass_cull_threshold = true;

    if (!subnautica2_is_current_game()) {
        return;
    }
    if (subnautica2_diag_clean_mode()) {
        SPDLOG_INFO("[Subnautica2][DiagClean] Skipping BasePassR12bPatch");
        return;
    }

    // Default DISABLED to isolate crash diagnostics. Enable with
    // UEVR_SUBNAUTICA2_ENABLE_BASEPASS_R12B_PATCH=1.
    {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_ENABLE_BASEPASS_R12B_PATCH", value, (DWORD)std::size(value));
        const bool enabled = len > 0 && len < std::size(value) &&
            value[0] != L'\0' && value[0] != L'0' && value[0] != L'f' && value[0] != L'F';
        if (!enabled) {
            SPDLOG_INFO("[Subnautica2][BasePassR12bPatch] Patch skipped (set UEVR_SUBNAUTICA2_ENABLE_BASEPASS_R12B_PATCH=1 to enable)");
            return;
        }
    }

    const auto exe_base = (uintptr_t)utility::get_executable();
    constexpr uintptr_t SETNBE_R12B_RVA = 0x268719B;
    const auto target = exe_base + SETNBE_R12B_RVA;

    if (exe_base == 0 || !is_executable_process_range(target, 0x10)) {
        SPDLOG_WARN("[Subnautica2][BasePassR12bPatch] Cannot patch 0x{:x}; bad target", target);
        return;
    }

    // Expected: `setnbe r12b` = REX.B(0x41) + 0F 97 C4
    constexpr uint8_t expected_bytes[] = {0x41, 0x0F, 0x97, 0xC4};
    if (std::memcmp((void*)target, expected_bytes, sizeof(expected_bytes)) != 0) {
        const auto* p = (const uint8_t*)target;
        SPDLOG_WARN("[Subnautica2][BasePassR12bPatch] Byte mismatch at {:x}; got {:02x} {:02x} {:02x} {:02x}, expected 41 0F 97 C4. Skipping patch for this build.",
            target, p[0], p[1], p[2], p[3]);
        return;
    }

    // Patch: `mov r12b, 1; nop` = REX.B(0x41) + B4 01 + NOP(90)
    constexpr uint8_t patch_bytes[] = {0x41, 0xB4, 0x01, 0x90};

    DWORD old_protect = 0;
    if (!VirtualProtect((void*)target, sizeof(patch_bytes), PAGE_EXECUTE_READWRITE, &old_protect)) {
        SPDLOG_WARN("[Subnautica2][BasePassR12bPatch] VirtualProtect failed at {:x}: err {}", target, GetLastError());
        return;
    }

    std::memcpy((void*)target, patch_bytes, sizeof(patch_bytes));
    FlushInstructionCache(GetCurrentProcess(), (void*)target, sizeof(patch_bytes));

    DWORD restore_protect = 0;
    VirtualProtect((void*)target, sizeof(patch_bytes), old_protect, &restore_protect);

    SPDLOG_WARN("[Subnautica2][BasePassR12bPatch] Patched setnbe r12b -> mov r12b,1;nop at {:x}. r12b is now always 1; both stereo views will select the same BasePass shader-type variant -> symmetric fog rendering.", target);
}

// ============================================================================
// Patch E: jz->jmp branch force at sub_142631130 case-0 PS-selector sites.
//
// Discovery (parallel investigation): case 0 of sub_142631130 selects between
//   sub_14265A370 (TBasePassPSFNoLightMapPolicy, 64-bit RT, 11 textures)
//     -- matches LEFT eye PS 27914 with visible fog
//   sub_14265BEC0 (F128BitRTBasePassPS, 128-bit RT, 17 textures + Texture3D5)
//     -- matches RIGHT eye PS 27923 with missing fog (HDR RT not populated per-view)
//   sub_14265A110 (Lumen/HQ variant)
//
// The selection is via two `jz` branches at 0x142631EC8 and 0x142631ED8.
// Forcing both jz->jmp routes ALL case-0 invocations to sub_14265A370 so
// both eyes use the LEFT eye's working shader. Symmetric fog rendering.
//
// Patch is a 4-byte total static byte rewrite (2 bytes at each site),
// gated by env var UEVR_SUBNAUTICA2_ENABLE_BASEPASS_PS_FORCE_NOLM=1.
// ============================================================================
void FFakeStereoRenderingHook::attempt_hook_subnautica2_basepass_ps_force_nolm() {
    if (m_attempted_hook_subnautica2_basepass_ps_force_nolm) {
        return;
    }

    m_attempted_hook_subnautica2_basepass_ps_force_nolm = true;

    if (!subnautica2_is_current_game()) {
        return;
    }
    if (subnautica2_diag_clean_mode()) {
        SPDLOG_INFO("[Subnautica2][DiagClean] Skipping BasePassPSForceNoLM patch");
        return;
    }

    // Default DISABLED; enable with UEVR_SUBNAUTICA2_ENABLE_BASEPASS_PS_FORCE_NOLM=1.
    {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_ENABLE_BASEPASS_PS_FORCE_NOLM", value, (DWORD)std::size(value));
        const bool enabled = len > 0 && len < std::size(value) &&
            value[0] != L'\0' && value[0] != L'0' && value[0] != L'f' && value[0] != L'F';
        if (!enabled) {
            SPDLOG_INFO("[Subnautica2][BasePassPSForceNoLM] Patch skipped (set UEVR_SUBNAUTICA2_ENABLE_BASEPASS_PS_FORCE_NOLM=1 to enable)");
            return;
        }
    }

    const auto exe_base = (uintptr_t)utility::get_executable();
    if (exe_base == 0) {
        SPDLOG_WARN("[Subnautica2][BasePassPSForceNoLM] exe_base is null; skipping");
        return;
    }

    struct PatchSite {
        uintptr_t rva;
        uint8_t original[2];
        uint8_t patched[2];
        const char* label;
    };

    constexpr PatchSite patches[] = {
        { 0x2631EC8, {0x74, 0x07}, {0xEB, 0x07}, "force-sil-zero-branch" },
        { 0x2631ED8, {0x74, 0x07}, {0xEB, 0x07}, "force-no-skylight-branch" },
    };

    for (const auto& p : patches) {
        const auto target = exe_base + p.rva;

        if (!is_executable_process_range(target, sizeof(p.original))) {
            SPDLOG_WARN("[Subnautica2][BasePassPSForceNoLM] {} target {:x} not executable; skipping", p.label, target);
            continue;
        }

        if (std::memcmp((void*)target, p.original, sizeof(p.original)) != 0) {
            const auto* observed = (const uint8_t*)target;
            SPDLOG_WARN(
                "[Subnautica2][BasePassPSForceNoLM] {} byte mismatch at {:x}; got {:02x} {:02x}, expected {:02x} {:02x}. Skipping for this build.",
                p.label, target, observed[0], observed[1], p.original[0], p.original[1]);
            continue;
        }

        DWORD old_protect = 0;
        if (!VirtualProtect((void*)target, sizeof(p.patched), PAGE_EXECUTE_READWRITE, &old_protect)) {
            SPDLOG_WARN("[Subnautica2][BasePassPSForceNoLM] {} VirtualProtect failed at {:x}: err {}", p.label, target, GetLastError());
            continue;
        }

        std::memcpy((void*)target, p.patched, sizeof(p.patched));
        FlushInstructionCache(GetCurrentProcess(), (void*)target, sizeof(p.patched));

        DWORD restore_protect = 0;
        VirtualProtect((void*)target, sizeof(p.patched), old_protect, &restore_protect);

        SPDLOG_WARN("[Subnautica2][BasePassPSForceNoLM] {} applied at {:x} (jz -> jmp, force branch taken)", p.label, target);
    }

    SPDLOG_WARN("[Subnautica2][BasePassPSForceNoLM] Patch E complete. Case-0 PS selector routes both eyes to TBasePassPSFNoLightMapPolicy (sub_14265A370).");
}

void FFakeStereoRenderingHook::attempt_hook_subnautica2_render_fog_wrapper() {
    if (m_attempted_hook_subnautica2_render_fog_wrapper) {
        return;
    }

    m_attempted_hook_subnautica2_render_fog_wrapper = true;

    if (!subnautica2_is_current_game()) {
        return;
    }

    const auto exe_base = (uintptr_t)utility::get_executable();
    const auto target = exe_base + SUBNAUTICA2_RENDER_FOG_WRAPPER_RVA;

    if (exe_base == 0 || !is_executable_process_range(target, 0x20)) {
        SPDLOG_WARN("[Subnautica2][RenderFog] Cannot hook RenderFog wrapper; bad target {:x}", target);
        return;
    }

    constexpr uint8_t expected_prologue[] = {
        0x4C, 0x89, 0x4C, 0x24, 0x20, // mov [rsp+20h], r9
        0x4C, 0x89, 0x44, 0x24, 0x18, // mov [rsp+18h], r8
        0x53,                         // push rbx
        0x56,                         // push rsi
        0x41, 0x55,                   // push r13
        0x41, 0x56,                   // push r14
        0x48, 0x83, 0xEC, 0x58,       // sub rsp, 58h
    };

    if (std::memcmp((void*)target, expected_prologue, sizeof(expected_prologue)) != 0) {
        SPDLOG_WARN("[Subnautica2][RenderFog] RenderFog wrapper prologue mismatch at {:x}; skipping hook for this build", target);
        return;
    }

    m_subnautica2_render_fog_wrapper_hook = safetyhook::create_inline(
        (void*)target,
        &FFakeStereoRenderingHook::subnautica2_render_fog_wrapper_hook,
        safetyhook::InlineHook::StartDisabled);

    if (!m_subnautica2_render_fog_wrapper_hook) {
        SPDLOG_WARN("[Subnautica2][RenderFog] Failed to create RenderFog wrapper hook at {:x}", target);
        return;
    }

    if (auto enable_result = m_subnautica2_render_fog_wrapper_hook.enable(); !enable_result.has_value()) {
        SPDLOG_WARN("[Subnautica2][RenderFog] Failed to enable RenderFog wrapper hook at {:x}: {}", target, (int)enable_result.error().type);
        return;
    }

    SPDLOG_WARN(
        "[Subnautica2][RenderFog] Hooked RenderFog wrapper at {:x}; fog ViewRect patch {}",
        target,
        subnautica2_disable_render_fog_view_rect_fix() ? "disabled" : "enabled");
}

void FFakeStereoRenderingHook::attempt_hook_subnautica2_render_fog_pass() {
    if (m_attempted_hook_subnautica2_render_fog_pass) {
        return;
    }

    m_attempted_hook_subnautica2_render_fog_pass = true;

    if (!subnautica2_is_current_game()) {
        return;
    }

    const auto exe_base = (uintptr_t)utility::get_executable();
    const auto target = exe_base + SUBNAUTICA2_RENDER_FOG_PASS_RVA;

    if (exe_base == 0 || !is_executable_process_range(target, 0x20)) {
        SPDLOG_WARN("[Subnautica2][HeightFogPass] Cannot hook height fog pass; bad target {:x}", target);
        return;
    }

    constexpr uint8_t expected_prologue[] = {
        0x4C, 0x89, 0x4C, 0x24, 0x20, // mov [rsp+20h], r9
        0x4C, 0x89, 0x44, 0x24, 0x18, // mov [rsp+18h], r8
        0x48, 0x89, 0x4C, 0x24, 0x08, // mov [rsp+8h], rcx
        0x55,                         // push rbp
        0x53,                         // push rbx
        0x56,                         // push rsi
        0x41, 0x54,                   // push r12
        0x41, 0x57,                   // push r15
    };

    if (std::memcmp((void*)target, expected_prologue, sizeof(expected_prologue)) != 0) {
        SPDLOG_WARN("[Subnautica2][HeightFogPass] Height fog pass prologue mismatch at {:x}; skipping hook for this build", target);
        return;
    }

    m_subnautica2_render_fog_pass_hook = safetyhook::create_inline(
        (void*)target,
        &FFakeStereoRenderingHook::subnautica2_render_fog_pass_hook,
        safetyhook::InlineHook::StartDisabled);

    if (!m_subnautica2_render_fog_pass_hook) {
        SPDLOG_WARN("[Subnautica2][HeightFogPass] Failed to create height fog pass hook at {:x}", target);
        return;
    }

    if (auto enable_result = m_subnautica2_render_fog_pass_hook.enable(); !enable_result.has_value()) {
        SPDLOG_WARN("[Subnautica2][HeightFogPass] Failed to enable height fog pass hook at {:x}: {}", target, (int)enable_result.error().type);
        return;
    }

    SPDLOG_WARN(
        "[Subnautica2][HeightFogPass] Hooked height fog pass at {:x}; fog ViewRect patch {}",
        target,
        subnautica2_disable_render_fog_view_rect_fix() ? "disabled" : "enabled");
}

static bool subnautica2_apply_underwater_fog_per_view_guard_patch(uintptr_t exe_base) {
    if (!subnautica2_force_underwater_fog_per_view()) {
        return false;
    }

    static std::atomic<bool> attempted{false};
    static std::atomic<bool> applied{false};
    bool expected_attempted = false;
    if (!attempted.compare_exchange_strong(expected_attempted, true)) {
        return applied.load(std::memory_order_relaxed);
    }

    const auto patch_target = exe_base + SUBNAUTICA2_RENDER_UNDERWATER_FOG_PER_VIEW_GUARD_RVA;
    if (exe_base == 0 || !is_executable_process_range(patch_target, 6)) {
        SPDLOG_WARN("[Subnautica2][UnderwaterFogPerView] Cannot patch guard; bad target {:x}", patch_target);
        return false;
    }

    constexpr uint8_t original_bytes[] = {0x0F, 0x84, 0xE9, 0x03, 0x00, 0x00};
    constexpr uint8_t patched_bytes[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};

    if (std::memcmp((void*)patch_target, patched_bytes, sizeof(patched_bytes)) == 0) {
        applied.store(true, std::memory_order_relaxed);
        SPDLOG_WARN("[Subnautica2][UnderwaterFogPerView] guard already patched at {:x}", patch_target);
        return true;
    }

    if (std::memcmp((void*)patch_target, original_bytes, sizeof(original_bytes)) != 0) {
        uint8_t actual[sizeof(original_bytes)]{};
        std::memcpy(actual, (void*)patch_target, sizeof(actual));
        SPDLOG_WARN(
            "[Subnautica2][UnderwaterFogPerView] guard bytes mismatch at {:x}; expected 0F 84 E9 03 00 00, got {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}; skipping",
            patch_target,
            actual[0], actual[1], actual[2], actual[3], actual[4], actual[5]);
        return false;
    }

    DWORD old_protect = 0;
    if (!VirtualProtect((void*)patch_target, sizeof(patched_bytes), PAGE_EXECUTE_READWRITE, &old_protect)) {
        SPDLOG_WARN("[Subnautica2][UnderwaterFogPerView] VirtualProtect failed at {:x}", patch_target);
        return false;
    }

    std::memcpy((void*)patch_target, patched_bytes, sizeof(patched_bytes));

    DWORD ignored = 0;
    VirtualProtect((void*)patch_target, sizeof(patched_bytes), old_protect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), (void*)patch_target, sizeof(patched_bytes));

    applied.store(true, std::memory_order_relaxed);
    SPDLOG_WARN(
        "[Subnautica2][UnderwaterFogPerView] applied 6-byte NOP at {:x} (RVA 0x{:x}); RenderUnderWaterFog no longer skips view body on view+0x11EC/scene-gate failure",
        patch_target,
        SUBNAUTICA2_RENDER_UNDERWATER_FOG_PER_VIEW_GUARD_RVA);
    return true;
}

static void subnautica2_log_underwater_fog_loop_point(const char* tag, safetyhook::Context& ctx) {
    static std::atomic<uint64_t> logged{0};
    const auto n = logged.fetch_add(1, std::memory_order_relaxed);
    if (n >= 96 && (n % 600) != 0) {
        return;
    }

    const auto view = (uintptr_t)ctx.r14;
    if (view == 0 || !is_readable_process_range(view, SUBNAUTICA2_SCENEVIEW_STRIDE)) {
        SPDLOG_WARN(
            "[Subnautica2][UnderwaterFogLoop] {} n={} bad_view=0x{:x} rcx={} rdx=0x{:x} r8=0x{:x} r12=0x{:x} r15=0x{:x}",
            tag,
            n + 1,
            view,
            (uint32_t)ctx.rcx,
            ctx.rdx,
            ctx.r8,
            ctx.r12,
            ctx.r15);
        return;
    }

    const auto* runtime_view_rect = (const int32_t*)(view + SUBNAUTICA2_SCENEVIEW_RUNTIME_VIEW_RECT_OFFSET);
    const auto pass = *(uint32_t*)(view + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET);
    const auto stereo_index = *(int32_t*)(view + SUBNAUTICA2_SCENEVIEW_STEREO_VIEW_INDEX_OFFSET);
    const auto primary_index = *(int32_t*)(view + SUBNAUTICA2_SCENEVIEW_PRIMARY_VIEW_INDEX_OFFSET);
    const auto fog_gate_mem = *(uint8_t*)(view + SUBNAUTICA2_SCENEVIEW_FOG_RENDER_FLAG_OFFSET);
    const auto underwater_depth = *(float*)(view + SUBNAUTICA2_SCENEVIEW_UNDERWATER_DEPTH_OFFSET);
    const auto water_intersection = *(uint8_t*)(view + SUBNAUTICA2_SCENEVIEW_WATER_INTERSECTION_OFFSET);
    const auto local_fog = *(uintptr_t*)(view + SUBNAUTICA2_SCENEVIEW_LOCAL_FOG_VOLUME_VIEW_DATA_OFFSET);
    const auto cached_view_uniform = *(uintptr_t*)(view + SUBNAUTICA2_SCENEVIEW_CACHED_VIEW_UNIFORM_OFFSET);
    const auto shader_map = *(uintptr_t*)(view + SUBNAUTICA2_SCENEVIEW_SHADER_MAP_OFFSET);

    SPDLOG_WARN(
        "[Subnautica2][UnderwaterFogLoop] {} n={} loop_idx={} view=0x{:x} pass={} stereo_index={} primary_index={} sil_gate=0x{:02x} dil_scene=0x{:02x} mem_gate=0x{:02x} depth={:.4f} water_intersection={} rect={} {} {} {} local_fog=0x{:x} cached_ub=0x{:x} shader_map=0x{:x} rdx=0x{:x} r8=0x{:x} r12=0x{:x} r15=0x{:x} rbx=0x{:x}",
        tag,
        n + 1,
        (uint32_t)ctx.rcx,
        view,
        pass,
        stereo_index,
        primary_index,
        (uint32_t)(ctx.rsi & 0xFF),
        (uint32_t)(ctx.rdi & 0xFF),
        (uint32_t)fog_gate_mem,
        underwater_depth,
        (uint32_t)water_intersection,
        runtime_view_rect[0],
        runtime_view_rect[1],
        runtime_view_rect[2],
        runtime_view_rect[3],
        local_fog,
        cached_view_uniform,
        shader_map,
        ctx.rdx,
        ctx.r8,
        ctx.r12,
        ctx.r15,
        ctx.rbx);
}

void FFakeStereoRenderingHook::subnautica2_underwater_fog_guard_proceed_midhook(safetyhook::Context& ctx) {
    subnautica2_log_underwater_fog_loop_point("guard_proceed", ctx);
}

void FFakeStereoRenderingHook::subnautica2_underwater_fog_build_block_midhook(safetyhook::Context& ctx) {
    subnautica2_log_underwater_fog_loop_point("build_block", ctx);
}

void FFakeStereoRenderingHook::attempt_hook_subnautica2_render_underwater_fog() {
    if (m_attempted_hook_subnautica2_render_underwater_fog) {
        return;
    }

    m_attempted_hook_subnautica2_render_underwater_fog = true;

    if (!subnautica2_is_current_game()) {
        return;
    }

    const auto exe_base = (uintptr_t)utility::get_executable();
    const auto target = exe_base + SUBNAUTICA2_RENDER_UNDERWATER_FOG_RVA;

    if (exe_base == 0 || !is_executable_process_range(target, 0x20)) {
        SPDLOG_WARN("[Subnautica2][UnderwaterFog] Cannot hook RenderUnderWaterFog; bad target {:x}", target);
        return;
    }

    constexpr uint8_t expected_prologue[] = {
        0x4C, 0x89, 0x4C, 0x24, 0x20, // mov [rsp+20h], r9
        0x4C, 0x89, 0x44, 0x24, 0x18, // mov [rsp+18h], r8
        0x48, 0x89, 0x54, 0x24, 0x10, // mov [rsp+10h], rdx
        0x55,                         // push rbp
    };

    if (std::memcmp((void*)target, expected_prologue, sizeof(expected_prologue)) != 0) {
        SPDLOG_WARN("[Subnautica2][UnderwaterFog] RenderUnderWaterFog prologue mismatch at {:x}; skipping hook for this build", target);
        return;
    }

    subnautica2_apply_underwater_fog_per_view_guard_patch(exe_base);

    if (subnautica2_force_underwater_fog_per_view()) {
        const auto guard_proceed_target = exe_base + SUBNAUTICA2_RENDER_UNDERWATER_FOG_GUARD_PROCEED_RVA;
        constexpr uint8_t expected_guard_proceed[] = {
            0x41, 0x0F, 0x2F, 0xBE, 0x00, 0x12, 0x00, 0x00, // comiss xmm7, [r14+1200h]
        };
        if (is_executable_process_range(guard_proceed_target, sizeof(expected_guard_proceed)) &&
            std::memcmp((void*)guard_proceed_target, expected_guard_proceed, sizeof(expected_guard_proceed)) == 0)
        {
            m_subnautica2_underwater_fog_guard_proceed_midhook = safetyhook::create_mid(
                (void*)guard_proceed_target,
                &FFakeStereoRenderingHook::subnautica2_underwater_fog_guard_proceed_midhook);
            SPDLOG_WARN(
                "[Subnautica2][UnderwaterFogLoop] guard_proceed midhook {} at {:x}",
                m_subnautica2_underwater_fog_guard_proceed_midhook ? "installed" : "failed",
                guard_proceed_target);
        } else {
            SPDLOG_WARN("[Subnautica2][UnderwaterFogLoop] guard_proceed bytes mismatch at {:x}; skipping", guard_proceed_target);
        }

        const auto build_block_target = exe_base + SUBNAUTICA2_RENDER_UNDERWATER_FOG_BUILD_BLOCK_RVA;
        constexpr uint8_t expected_build_block[] = {
            0x49, 0x8B, 0xB4, 0x24, 0xC0, 0x00, 0x00, 0x00, // mov rsi, [r12+0C0h]
        };
        if (is_executable_process_range(build_block_target, sizeof(expected_build_block)) &&
            std::memcmp((void*)build_block_target, expected_build_block, sizeof(expected_build_block)) == 0)
        {
            m_subnautica2_underwater_fog_build_block_midhook = safetyhook::create_mid(
                (void*)build_block_target,
                &FFakeStereoRenderingHook::subnautica2_underwater_fog_build_block_midhook);
            SPDLOG_WARN(
                "[Subnautica2][UnderwaterFogLoop] build_block midhook {} at {:x}",
                m_subnautica2_underwater_fog_build_block_midhook ? "installed" : "failed",
                build_block_target);
        } else {
            SPDLOG_WARN("[Subnautica2][UnderwaterFogLoop] build_block bytes mismatch at {:x}; skipping", build_block_target);
        }
    }

    m_subnautica2_render_underwater_fog_hook = safetyhook::create_inline(
        (void*)target,
        &FFakeStereoRenderingHook::subnautica2_render_underwater_fog_hook,
        safetyhook::InlineHook::StartDisabled);

    if (!m_subnautica2_render_underwater_fog_hook) {
        SPDLOG_WARN("[Subnautica2][UnderwaterFog] Failed to create RenderUnderWaterFog hook at {:x}", target);
        return;
    }

    if (auto enable_result = m_subnautica2_render_underwater_fog_hook.enable(); !enable_result.has_value()) {
        SPDLOG_WARN("[Subnautica2][UnderwaterFog] Failed to enable RenderUnderWaterFog hook at {:x}: {}", target, (int)enable_result.error().type);
        return;
    }

    SPDLOG_WARN(
        "[Subnautica2][UnderwaterFog] Hooked RenderUnderWaterFog at {:x}; scene-without-water per-eye rect/UV fix {}",
        target,
        subnautica2_disable_underwater_fog_view_data_fix() ? "disabled" : "enabled");
}

void FFakeStereoRenderingHook::attempt_hook_subnautica2_try_add_mesh_batch_probe() {
    if (m_attempted_hook_subnautica2_try_add_mesh_batch_probe) return;
    m_attempted_hook_subnautica2_try_add_mesh_batch_probe = true;
    if (!subnautica2_is_current_game()) return;

    const auto exe_base = (uintptr_t)utility::get_executable();
    const auto target = exe_base + SUBNAUTICA2_TRY_ADD_MESH_BATCH_PROBE_RVA;
    if (exe_base == 0 || !is_executable_process_range(target, 0x10)) return;

    // Verify the prologue: test r15, r15 = 4D 85 FF
    constexpr uint8_t expected[] = { 0x4D, 0x85, 0xFF };
    if (std::memcmp((void*)target, expected, sizeof(expected)) != 0) {
        SPDLOG_WARN("[Subnautica2][TryAddMeshBatchProbe] Bytes mismatch at {:x}; skipping", target);
        return;
    }

    m_subnautica2_try_add_mesh_batch_probe = safetyhook::create_mid(
        (void*)target,
        &FFakeStereoRenderingHook::subnautica2_try_add_mesh_batch_probe_midhook);

    if (!m_subnautica2_try_add_mesh_batch_probe) {
        SPDLOG_WARN("[Subnautica2][TryAddMeshBatchProbe] Failed to create mid-hook at {:x}", target);
        return;
    }
    SPDLOG_WARN("[Subnautica2][TryAddMeshBatchProbe] Probe installed at {:x}", target);
}

// MidHook fires AT `test r15, r15` in TryAddMeshBatch. Probe r15 and r14
// values for the first N calls to determine which condition causes bl=0 for
// view 1 (the per-view fog-skip divergence).
void FFakeStereoRenderingHook::subnautica2_try_add_mesh_batch_probe_midhook(safetyhook::Context& ctx) {
    static std::atomic<uint64_t> n_total{0};
    static std::atomic<uint64_t> n_r15_null{0};
    static std::atomic<uint64_t> n_r14b_zero{0};

    const uint64_t total = n_total.fetch_add(1, std::memory_order_relaxed);
    if (ctx.r15 == 0) {
        n_r15_null.fetch_add(1, std::memory_order_relaxed);
    }
    if ((ctx.r14 & 0xFF) == 0) {
        n_r14b_zero.fetch_add(1, std::memory_order_relaxed);
    }

    // Log first N for sampling, then a summary every 6000 calls.
    if (total < 16) {
        SPDLOG_WARN(
            "[Subnautica2][TryAddMeshBatchProbe] N={} r15=0x{:x} r14b=0x{:02x} (r15_null_total={} r14b_zero_total={})",
            total + 1, ctx.r15, ctx.r14 & 0xFF,
            n_r15_null.load(), n_r14b_zero.load());
    } else if ((total % 6000) == 0) {
        SPDLOG_WARN(
            "[Subnautica2][TryAddMeshBatchProbe] SUMMARY total={} r15_null={} r14b_zero={}",
            total, n_r15_null.load(), n_r14b_zero.load());
    }
}

void FFakeStereoRenderingHook::attempt_hook_subnautica2_setup_fog_uniform_params() {
    if (m_attempted_hook_subnautica2_setup_fog_uniform_params) return;
    m_attempted_hook_subnautica2_setup_fog_uniform_params = true;
    if (!subnautica2_is_current_game()) return;
    if (subnautica2_disable_legacy_fog_uniform_mutations()) {
        SPDLOG_INFO("[Subnautica2][FogIsolation] Skipping SetupFogUniformParameters hook");
        return;
    }

    const auto exe_base = (uintptr_t)utility::get_executable();
    const auto target = exe_base + SUBNAUTICA2_SETUP_FOG_UNIFORM_PARAMS_RVA;
    if (exe_base == 0 || !is_executable_process_range(target, 0x10)) {
        SPDLOG_WARN("[Subnautica2][SetupFogUniformParams] Bad target {:x}", target);
        return;
    }

    m_subnautica2_setup_fog_uniform_params_hook = safetyhook::create_inline(
        (void*)target,
        &FFakeStereoRenderingHook::subnautica2_setup_fog_uniform_params_hook,
        safetyhook::InlineHook::StartDisabled);

    if (!m_subnautica2_setup_fog_uniform_params_hook) {
        SPDLOG_WARN("[Subnautica2][SetupFogUniformParams] Failed to create hook at {:x}", target);
        return;
    }
    if (auto enable_result = m_subnautica2_setup_fog_uniform_params_hook.enable(); !enable_result.has_value()) {
        SPDLOG_WARN("[Subnautica2][SetupFogUniformParams] Failed to enable hook at {:x}: {}", target, (int)enable_result.error().type);
        return;
    }
    SPDLOG_WARN("[Subnautica2][SetupFogUniformParams] Hooked SetupFogUniformParameters at {:x}", target);
}

// 2026-05-16 FIX-V4 REWRITE: Diagnose + fix the per-view FFogUniformParameters.
// This is the function that fills cbuffer3 (FFogUniformParameters, ~108 bytes)
// with the IntegratedLightScattering bindless index. The basepass material's
// PS reads cbuffer3 at this slot to bind the fog 3D texture.
// Per UE5 source FogRendering.cpp line 100:
//   OutParameters.IntegratedLightScattering = View.VolumetricFogResources.IntegratedLightScatteringTexture;
// In SN2: View+0x2658 = VolumetricFogResources.IntegratedLightScatteringTexture
//         fog_uniform_params (a3) = OutParameters
// If view 1 reads its OWN +0x2658, the bindless index in cbuffer3 should
// differ between view 0 and view 1 (pointing to per-eye textures). If both
// cbuffer3 end up with the same bytes → SN2 has a bug here.
void FFakeStereoRenderingHook::subnautica2_setup_fog_uniform_params_hook(
    void* graph_builder, void* view_info, void* fog_uniform_params, bool a4)
{
    auto* hook = g_hook;
    if (hook == nullptr || !hook->m_subnautica2_setup_fog_uniform_params_hook) {
        return;
    }

    // FIX-V4: pair detection state for cbuffer3 dump comparison.
    static thread_local uintptr_t fix_v4_prev_view = 0;
    static thread_local uint8_t fix_v4_view0_cbuf[0x200]{};
    static thread_local bool fix_v4_has_view0 = false;
    constexpr size_t FOG_PARAMS_SIZE = 0x180;  // covers FFogUniformParameters incl. tex@0x120
    // IDA decode of sub_1426FD010 at 0x1426fd286:
    //   mov [rbx+0x120], rax   ; IntegratedLightScattering handle (8 bytes)
    //   mov [rbx+0x130], rax   ; paired RDG resource (8 bytes)
    //   movss [rbx+0x90], xmm6 ; ApplyVolumetricFog (4 bytes, 0.0 in fallback)
    constexpr size_t FOG_TEX_OFFSET   = 0x120;
    constexpr size_t FOG_PAIRED_OFFSET = 0x130;
    constexpr size_t FOG_APPLY_OFFSET  = 0x90;

    // Pre-call diagnostic + fix: if this view's +0x2658 is null but a sibling
    // view (view-stride away) has a valid value, copy it in BEFORE the function
    // reads.
    auto& vr = VR::get();
    const bool stereo_active = vr != nullptr && vr->is_hmd_active() &&
                                vr->is_native_stereo_fix_enabled() &&
                                !vr->is_native_stereo_fix_same_pass_enabled() &&
                                !vr->is_using_afr();

    static thread_local uintptr_t cached_view0_2658 = 0;
    static thread_local uintptr_t cached_view0_2660 = 0;
    static std::atomic<uint64_t> fire_count{0};

    // PAIR-DETECT: only inject when current view is exactly view0+stride.
    // Random non-stereo views (reflections, shadow captures) should NOT get
    // their +0x2658 overwritten — that crashed SN2 last attempt.
    static thread_local uintptr_t pair_view0_ptr = 0;

    if (view_info != nullptr && stereo_active) {
        const uintptr_t view = (uintptr_t)view_info;
        if (is_readable_process_range(view + 0x2658, 16) && is_writable_process_range(view + 0x2658, 16)) {
            const uintptr_t v_2658 = *(uintptr_t*)(view + 0x2658);
            const uintptr_t v_2660 = *(uintptr_t*)(view + 0x2660);

            static std::atomic<uint64_t> nonnull_count{0};
            static std::atomic<uint64_t> inject_count{0};
            const auto setup_log_max = sn2_setup_fog_uniform_log_max();

            // Did we just see a valid view 0 last call, and is this view 0+stride?
            const bool is_view1_of_pair = (pair_view0_ptr != 0) &&
                                          (view == pair_view0_ptr + SUBNAUTICA2_SCENEVIEW_STRIDE);

            static const bool cbuf_use_view1_2650 = []() {
                char v[8]{};
                const auto n = GetEnvironmentVariableA("UEVR_SUBNAUTICA2_FOG_CBUF_USE_VIEW1_2650", v, sizeof(v));
                return n != 0 && v[0] == '1';
            }();

            if (v_2658 != 0) {
                // This view has valid LightScattering. Cache and treat as view 0
                // candidate for the next call (if next call is +stride, that's view 1).
                cached_view0_2658 = v_2658;
                cached_view0_2660 = v_2660;
                pair_view0_ptr = view;
                const auto nn = nonnull_count.fetch_add(1, std::memory_order_relaxed);
                if (nn < setup_log_max) {
                    SPDLOG_WARN(
                        "[Subnautica2][SetupFogUniformParams] NONNULL nn={} view=0x{:x} 2658=0x{:x} (potential view0)",
                        nn + 1, view, v_2658);
                }
            } else if (!cbuf_use_view1_2650 && is_view1_of_pair && cached_view0_2658 != 0) {
                // This IS view 1 of the stereo pair AND its +0x2658 is null.
                // Inject view 0's value so SetupFogUniformParameters reads non-null.
                *(uintptr_t*)(view + 0x2658) = cached_view0_2658;
                *(uintptr_t*)(view + 0x2660) = cached_view0_2660;
                const auto ic = inject_count.fetch_add(1, std::memory_order_relaxed);
                if (ic < setup_log_max) {
                    SPDLOG_WARN(
                        "[Subnautica2][SetupFogUniformParams] INJECTED ic={} view1=0x{:x} (view0=0x{:x}) 2658: 0x0 -> 0x{:x}",
                        ic + 1, view, pair_view0_ptr, cached_view0_2658);
                }
                pair_view0_ptr = 0;  // Consume the pair marker
            } else {
                // Non-stereo view OR view 1 that already had valid data.
                // Reset pair tracking — only consecutive view0→view1+stride counts.
                pair_view0_ptr = 0;
            }

            const auto n = fire_count.fetch_add(1, std::memory_order_relaxed);
            if (n < setup_log_max && n < 4) {
                SPDLOG_WARN(
                    "[Subnautica2][SetupFogUniformParams] FIRED n={} view=0x{:x} 2658_was=0x{:x} is_view1_pair={}",
                    n + 1, view, v_2658, is_view1_of_pair);
            }

            // 2026-05-17 NEW (env-gated): for view 1, override +0x2658 with
            // the per-view-distinct wrapper at +0x2650. Our POST hook showed
            // +0x2650 differs per view (view 0 vs view 1 have different
            // wrapper pointers there), so this gives SetupFog a per-view-
            // specific FRDGTextureRef to write into cbuf+0x120. RDG will
            // resolve it to a view-1-specific bindless slot index → view 1's
            // basepass binds DIFFERENT content than view 0.
            //
            // The user is responsible for confirming visually whether this
            // produces a different (hopefully correct teal) right-eye fog.
            // Worst case: right eye shows DIFFERENT garbage (different wrong
            // texture) which still confirms the binding mechanism works and
            // narrows further investigation.
            static const bool use_view1_2650 = []() {
                char v[8]{};
                const auto n = GetEnvironmentVariableA("UEVR_SUBNAUTICA2_FOG_USE_VIEW1_2650", v, sizeof(v));
                return n != 0 && v[0] == '1';
            }();
            if (!cbuf_use_view1_2650 && use_view1_2650 && is_view1_of_pair) {
                if (is_readable_process_range(view + 0x2650, 8)) {
                    const uintptr_t v_2650 = *(uintptr_t*)(view + 0x2650);
                    if (v_2650 != 0 && is_writable_process_range(view + 0x2658, 16)) {
                        const uintptr_t orig_2658 = *(uintptr_t*)(view + 0x2658);
                        *(uintptr_t*)(view + 0x2658) = v_2650;
                        static std::atomic<uint64_t> override_n{0};
                        const auto on = override_n.fetch_add(1, std::memory_order_relaxed);
                        if (on < 8 || (on % 600) == 0) {
                            SPDLOG_WARN(
                                "[Subnautica2][SetupFogUniformParams][View1-2650-Override] on={} view1=0x{:x} 2658 was=0x{:x} now=0x{:x} (from +0x2650)",
                                on + 1, view, orig_2658, v_2650);
                        }
                    }
                }
            }
        }
    }

    // Call original.
    hook->m_subnautica2_setup_fog_uniform_params_hook.unsafe_call<void>(
        graph_builder, view_info, fog_uniform_params, a4);

    // FIX-V4: post-call diagnostic — dump cbuffer3 bytes and pair-compare.
    if (fog_uniform_params != nullptr && view_info != nullptr &&
        is_readable_process_range((uintptr_t)fog_uniform_params, FOG_PARAMS_SIZE))
    {
        const uintptr_t this_view = (uintptr_t)view_info;
        uint8_t* cbuf = (uint8_t*)fog_uniform_params;

        const bool is_view1_pair = (fix_v4_prev_view != 0) &&
                                   (this_view == fix_v4_prev_view + SUBNAUTICA2_SCENEVIEW_STRIDE) &&
                                   fix_v4_has_view0;

        if (is_view1_pair) {
            // FIX-V5: view 1 of stereo pair. The IDA-decoded offsets for the
            // fog texture binding in FFogUniformParameters are:
            //   0x120 (8 bytes) = IntegratedLightScattering FRHITexture* (RDG ref)
            //   0x130 (8 bytes) = paired (sampler or RDG resource state)
            //   0x90  (4 bytes) = ApplyVolumetricFog (float 1.0 means enabled)
            //
            // SN2's native call read view1+0x2658 which was either null
            // (fallback to BlackAlphaOne and ApplyVolumetricFog=0) OR pointed
            // to view 1's own UNFILLED fog texture. Either way, view 1's
            // basepass doesn't get visible teal.
            //
            // Fix: read VIEW 1's own +0x2658 (which loop_fix populated with
            // view 1's filled texture pointer). If non-null, write it into
            // view 1's cbuf at 0x120 and force ApplyVolumetricFog=1.0.
            const uintptr_t v1_tex   = is_readable_process_range(this_view + 0x2658, 8)
                                       ? *(uintptr_t*)(this_view + 0x2658) : 0;
            const uintptr_t v1_pair  = is_readable_process_range(this_view + 0x2660, 8)
                                       ? *(uintptr_t*)(this_view + 0x2660) : 0;
            const uintptr_t v1_2650  = is_readable_process_range(this_view + 0x2650, 8)
                                       ? *(uintptr_t*)(this_view + 0x2650) : 0;
            const uintptr_t v0_tex   = *(uintptr_t*)(fix_v4_view0_cbuf + FOG_TEX_OFFSET);
            const uintptr_t v0_pair  = *(uintptr_t*)(fix_v4_view0_cbuf + FOG_PAIRED_OFFSET);

            const uintptr_t cur_v1_tex  = *(uintptr_t*)(cbuf + FOG_TEX_OFFSET);
            const uintptr_t cur_v1_pair = *(uintptr_t*)(cbuf + FOG_PAIRED_OFFSET);

            int patched = 0;
            // If view 1 has its own valid +0x2658 (loop_fix scenario), use it.
            // Otherwise fall back to copying view 0's texture handle so
            // view 1 at least gets fog visible (even if at wrong coords).
            static const bool cbuf_use_view1_2650 = []() {
                char v[8]{};
                const auto n = GetEnvironmentVariableA("UEVR_SUBNAUTICA2_FOG_CBUF_USE_VIEW1_2650", v, sizeof(v));
                return n != 0 && v[0] == '1';
            }();
            uintptr_t target_tex  = (cbuf_use_view1_2650 && v1_2650 != 0) ? v1_2650 : (v1_tex ? v1_tex : v0_tex);
            uintptr_t target_pair = cbuf_use_view1_2650 ? (v1_pair ? v1_pair : cur_v1_pair) : (v1_pair ? v1_pair : v0_pair);
            if (target_tex != 0 && target_tex != cur_v1_tex) {
                *(uintptr_t*)(cbuf + FOG_TEX_OFFSET) = target_tex;
                patched++;
            }
            if (target_pair != 0 && target_pair != cur_v1_pair) {
                *(uintptr_t*)(cbuf + FOG_PAIRED_OFFSET) = target_pair;
                patched++;
            }
            // Force ApplyVolumetricFog = 1.0 if we have a valid texture.
            if (target_tex != 0) {
                float* apply = (float*)(cbuf + FOG_APPLY_OFFSET);
                if (*apply < 0.5f) { *apply = 1.0f; patched++; }
            }

            static std::atomic<uint64_t> diag_count{0};
            const auto dc = diag_count.fetch_add(1, std::memory_order_relaxed);
            if (dc < sn2_fix_v5_log_max()) {
                SPDLOG_WARN(
                    "[Subnautica2][FixV5] call={} mode={} view0=0x{:x} view1=0x{:x} v1_2658=0x{:x} v1_2650=0x{:x} cur_v1_tex=0x{:x} v0_tex=0x{:x} target=0x{:x} patched={}",
                    dc + 1,
                    cbuf_use_view1_2650 ? "cbuf2650" : "default",
                    fix_v4_prev_view,
                    this_view,
                    v1_tex,
                    v1_2650,
                    cur_v1_tex,
                    v0_tex,
                    target_tex,
                    patched);
            }

            // (FRDG-RHI tracker removed 2026-05-16: FRDGTexture wrappers are
            // reallocated each frame by FRDGBuilder, so reading +0x10 at hook
            // entry is always NULL. To inspect post-execute, would need to
            // hook FRDGBuilder::Execute or FRDGTexture::SetRHI — out of scope
            // for this session.)

            // 2026-05-17 Path A (Step 2/3): mirror view 1's post-FixV5 cbuf
            // bytes into UEVR's per-view-1 upload buffer. The basepass
            // redirect (D3D12Hook::set_graphics_root_constant_buffer_view)
            // makes view 1's PS read this buffer instead of SN2's shared
            // FFogUniformParameters cbuffer, which otherwise gets overwritten
            // when SN2 uploads view 0's content over it.
            //
            // What we write here = cbuf AFTER FixV5's patch =
            //   - bytes 0x000..0x180: view 1's native SetupFogUniformParameters
            //     output, except:
            //     - +0x120: target_tex (v1_2658 if non-null, else v0_2658)
            //     - +0x130: target_pair
            //     - +0x90 : 1.0f (ApplyVolumetricFog) if target_tex != 0
            //
            // When v1_2658 IS valid (loop_fix scenario or future Step 3 fix),
            // this gives view 1 its OWN per-view-correct fog volume pointer.
            // When v1_2658 is null (current state), it falls back to v0_tex,
            // matching the current bug behavior but via the redirect path —
            // useful as a control to confirm the write/redirect plumbing.
            if (sn2_fog_path_a::enabled_env()) {
                if (sn2_fog_path_a::buffer_ready()) {
                    sn2_fog_path_a::write_buffer(cbuf, FOG_PARAMS_SIZE);
                }
                // Arm the next 2 matching cbuffer bindings for redirect.
                // Count=2 covers both opaque and translucent basepass binds
                // of the same shared fog cbuffer within view 1's pass.
                // Done EVERY view 1 setup call (not just when buffer_ready)
                // so the arm fires even on the very first frame where the
                // buffer might not yet exist (the redirect hook will lazily
                // create the buffer on first armed binding).
                sn2_fog_path_a::arm_redirect(2);
                static std::atomic<uint64_t> wb_count{0};
                const auto wc = wb_count.fetch_add(1, std::memory_order_relaxed);
                if (wc < sn2_fix_v5_log_max()) {
                    SPDLOG_WARN(
                        "[Subnautica2][FixV5][PathA] wrote view1 cbuf + armed redirect call={} target_tex=0x{:x} apply={:.2f} buffer_ready={}",
                        wc + 1, target_tex, *(float*)(cbuf + FOG_APPLY_OFFSET),
                        sn2_fog_path_a::buffer_ready() ? 1 : 0);
                }
            }

            fix_v4_prev_view = 0;
            fix_v4_has_view0 = false;
        } else {
            // VIEW 0 (or non-stereo) — save cbuf for upcoming view 1 compare.
            memcpy(fix_v4_view0_cbuf, cbuf, FOG_PARAMS_SIZE);
            fix_v4_has_view0 = true;
            fix_v4_prev_view = this_view;
        }
    }
}

void FFakeStereoRenderingHook::attempt_hook_subnautica2_lightscat_store_midhook() {
    if (m_attempted_hook_subnautica2_lightscat_store_midhook) {
        return;
    }
    m_attempted_hook_subnautica2_lightscat_store_midhook = true;
    if (!subnautica2_is_current_game()) {
        return;
    }
    const bool diag_enabled = subnautica2_enable_lightscat_store_diag();
    const bool rewrite_enabled = subnautica2_enable_lightscat_store_rewrite();
    if (subnautica2_disable_lightscat_store_midhook()) {
        SPDLOG_INFO("[Subnautica2][FogIsolation] Skipping LightScatStore midhook");
        return;
    }
    if (!diag_enabled && !rewrite_enabled) {
        SPDLOG_INFO("[Subnautica2][FogIsolation] Skipping LightScatStore midhook (diag/rewrite disabled)");
        return;
    }

    const auto exe_base = (uintptr_t)utility::get_executable();
    const auto target = exe_base + SUBNAUTICA2_LIGHTSCAT_STORE_RVA;
    if (exe_base == 0 || !is_executable_process_range(target, 0x10)) {
        SPDLOG_WARN("[Subnautica2][LightScatStore] Bad target {:x}", target);
        return;
    }

    // Verify the bytes at the target site (0x142FC179D):
    //   49 89 86 58 26 00 00  =  mov [r14+0x2658], rax (the REAL tex write)
    // Midhook fires BEFORE this instruction so we can read or replace ctx.rax.
    constexpr uint8_t expected_bytes[] = { 0x49, 0x89, 0x86, 0x58, 0x26, 0x00, 0x00 };
    if (std::memcmp((void*)target, expected_bytes, sizeof(expected_bytes)) != 0) {
        SPDLOG_WARN("[Subnautica2][LightScatStore] Byte mismatch at {:x}; skipping hook", target);
        return;
    }

    m_subnautica2_lightscat_store_midhook = safetyhook::create_mid(
        (void*)target,
        &FFakeStereoRenderingHook::subnautica2_lightscat_store_midhook);

    if (!m_subnautica2_lightscat_store_midhook) {
        SPDLOG_WARN("[Subnautica2][LightScatStore] Failed to create mid-hook at {:x}", target);
        return;
    }
    SPDLOG_WARN(
        "[Subnautica2][LightScatStore] MidHook installed at {:x} diag={} rewrite={}",
        target,
        diag_enabled ? 1 : 0,
        rewrite_enabled ? 1 : 0);
}

// 2026-05-17 evening REVISION v2: midhook fires BEFORE the store at
// 0x142FC179D ("mov [r14+0x2658], rax"). On view 0's call we SAVE the
// register values; on view 1's call we REPLACE them with the saved view-0
// values so the store writes view 0's good texture into view 1's slot.
//
// View identity detection (no per-view tag from the engine):
//   - "view 0 call" = (memory at r14 + 0x29D0 looks like another FViewInfo,
//     i.e. its vtable is non-null and readable). In stereo, view 0 is
//     followed by view 1 in memory, so v1_vtable!=0 means r14 IS view 0.
//   - "view 1 call" = (memory at r14 + 0x29D0 has null/unreadable vtable),
//     because there's no view 2 after view 1 in a stereo pair.
//   - Non-stereo (mono main-menu pass): same as view 1, treat as skip.
//
// Globals persist across the per-frame call pair. We use thread_local because
// compute_volumetric_fog runs on the render thread; no cross-thread access.
void FFakeStereoRenderingHook::subnautica2_lightscat_store_midhook(safetyhook::Context& ctx) {
    thread_local uintptr_t tl_saved_view0_tex  = 0;
    thread_local uintptr_t tl_saved_view0_comp = 0;
    thread_local uint64_t  tl_saved_frame_n    = 0;

    const bool rewrite_enabled = subnautica2_enable_lightscat_store_rewrite();
    const uintptr_t view_ptr      = ctx.r14;
    const uintptr_t orig_tex      = ctx.rax;
    const uintptr_t orig_comp     = ctx.r13;
    const uintptr_t next_view_ptr = view_ptr + 0x29D0;

    static std::atomic<uint64_t> log_n{0};
    const auto n = log_n.fetch_add(1, std::memory_order_relaxed);

    if (view_ptr == 0 || orig_tex == 0) {
        return;
    }

    int view_id = -1;
    uint32_t stereo_pass = 0xffffffffu;
    if (is_readable_process_range(view_ptr + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET, sizeof(uint32_t))) {
        stereo_pass = *(uint32_t*)(view_ptr + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET);
        if (stereo_pass == EStereoscopicPass::eSSP_PRIMARY) {
            view_id = 0;
        } else if (stereo_pass == EStereoscopicPass::eSSP_SECONDARY) {
            view_id = 1;
        }
    }

    const uintptr_t before_2658 = is_readable_process_range(view_ptr + 0x2658, 8)
        ? *(uintptr_t*)(view_ptr + 0x2658) : 0;
    const uintptr_t before_2660 = is_readable_process_range(view_ptr + 0x2660, 8)
        ? *(uintptr_t*)(view_ptr + 0x2660) : 0;

    bool is_view0 = false;
    if (view_id == 0) {
        is_view0 = true;
    } else if (view_id == 1) {
        is_view0 = false;
    } else if (is_readable_process_range(next_view_ptr, 8)) {
        const uintptr_t next_view_vtable = *(uintptr_t*)next_view_ptr;
        is_view0 = next_view_vtable != 0;
    }

    if (n < 64 || (n % 600) == 0) {
        SPDLOG_WARN(
            "[Subnautica2][LightScatStore][DIAG] call={} view_id={} pass={} class={} view=0x{:x} before2658=0x{:x} before2660=0x{:x} store_tex=0x{:x} store_comp=0x{:x} rewrite={}",
            n + 1,
            view_id,
            stereo_pass,
            is_view0 ? "view0" : "view1_or_mono",
            view_ptr,
            before_2658,
            before_2660,
            orig_tex,
            orig_comp,
            rewrite_enabled ? 1 : 0);
    }

    if (is_view0) {
        // Save view 0's texture pointers for the upcoming view 1 call.
        tl_saved_view0_tex  = orig_tex;
        tl_saved_view0_comp = orig_comp;
        tl_saved_frame_n    = n;
        // ALSO publish to global atomic so D3D12Hook::set_graphics_root_descriptor_table
        // can find the matching pool entry for view-1-eye descriptor swap.
        g_subnautica2_view0_lightscat.store(orig_tex, std::memory_order_relaxed);
        if (n < 16 || (n % 600) == 0) {
            SPDLOG_WARN(
                "[Subnautica2][LightScatStore] call={} VIEW0 saved tex=0x{:x} comp=0x{:x} (published to global)",
                n + 1, orig_tex, orig_comp);
        }
        // Let original instruction run unchanged: writes view 0's tex to view0+0x2658.
        return;
    }

    // is_view0 == false: either view 1 (stereo) or mono (non-stereo pass).
    // Distinguish by checking if we saved a view-0 value in this same "session"
    // (i.e. the save call was very recent — within a frame).
    if (tl_saved_view0_tex == 0 || (n - tl_saved_frame_n) > 4) {
        // No recent view-0 save: this is a mono pass or stale state. Leave
        // registers untouched, original instruction writes whatever was there.
        if (n < 16 || (n % 600) == 0) {
            SPDLOG_WARN(
                "[Subnautica2][LightScatStore] call={} MONO or stale (no recent view0 save) tex_was=0x{:x}",
                n + 1, orig_tex);
        }
        return;
    }

    // Stereo view 1 call: overwrite ctx.rax/ctx.r13 with view 0's saved values
    // so the upcoming store at 0x142FC179D writes view 0's texture into view 1.
    if (!rewrite_enabled) {
        if (n < 64 || (n % 600) == 0) {
            SPDLOG_WARN(
                "[Subnautica2][LightScatStore] call={} VIEW1 no-rewrite own_tex=0x{:x} saved_view0_tex=0x{:x}",
                n + 1,
                orig_tex,
                tl_saved_view0_tex);
        }
        tl_saved_view0_tex = 0;
        tl_saved_view0_comp = 0;
        return;
    }

    ctx.rax = tl_saved_view0_tex;
    ctx.r13 = tl_saved_view0_comp;

    if (n < 16 || (n % 600) == 0) {
        SPDLOG_WARN(
            "[Subnautica2][LightScatStore] call={} VIEW1 REWRITE rax 0x{:x} -> 0x{:x} ; r13 0x{:x} -> 0x{:x}",
            n + 1, orig_tex, tl_saved_view0_tex, orig_comp, tl_saved_view0_comp);
    }

    // Clear so the next mono pass doesn't accidentally use stale values.
    tl_saved_view0_tex = 0;
    tl_saved_view0_comp = 0;
}

static uintptr_t sn2_trace_read_ptr(uintptr_t address) {
    if (address == 0 || !is_readable_process_range(address, sizeof(uintptr_t))) {
        return 0;
    }
    return *(uintptr_t*)address;
}

static uint8_t sn2_trace_read_u8(uintptr_t address) {
    if (address == 0 || !is_readable_process_range(address, sizeof(uint8_t))) {
        return 0xff;
    }
    return *(uint8_t*)address;
}

static uint32_t sn2_trace_read_u32(uintptr_t address) {
    if (address == 0 || !is_readable_process_range(address, sizeof(uint32_t))) {
        return 0xffffffffu;
    }
    return *(uint32_t*)address;
}

static bool sn2_trace_looks_like_view(uintptr_t ptr) {
    if (ptr == 0 ||
        !is_readable_process_range(ptr + SUBNAUTICA2_SCENEVIEW_FAMILY_OFFSET, sizeof(uintptr_t)) ||
        !is_readable_process_range(ptr + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET, sizeof(uint32_t)))
    {
        return false;
    }

    const auto family = sn2_trace_read_ptr(ptr + SUBNAUTICA2_SCENEVIEW_FAMILY_OFFSET);
    const auto pass = sn2_trace_read_u32(ptr + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET);
    return family != 0 && pass <= 4 && is_readable_process_range(family, 0x1B0);
}

static int sn2_trace_view_id(uintptr_t view, uint32_t* stereo_pass_out = nullptr) {
    uint32_t stereo_pass = 0xffffffffu;
    if (view != 0 && is_readable_process_range(view + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET, sizeof(uint32_t))) {
        stereo_pass = *(uint32_t*)(view + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET);
    }
    if (stereo_pass_out != nullptr) {
        *stereo_pass_out = stereo_pass;
    }
    if (stereo_pass == EStereoscopicPass::eSSP_PRIMARY) {
        return 0;
    }
    if (stereo_pass == EStereoscopicPass::eSSP_SECONDARY) {
        return 1;
    }
    return -1;
}

static void sn2_volfog_param_trace_dump(int index, const char* tag, safetyhook::Context& ctx) {
    static std::atomic<uint64_t> counters[5]{};
    const auto n = counters[index].fetch_add(1, std::memory_order_relaxed);
    if (n >= 128 && (n % 1200) != 0) {
        return;
    }

    std::array<uintptr_t, 8> stack{};
    for (size_t i = 0; i < stack.size(); ++i) {
        stack[i] = sn2_trace_read_ptr(ctx.rsp + 0x20 + (i * sizeof(uintptr_t)));
    }

    uintptr_t view = 0;
    const uintptr_t regs[] = {ctx.rcx, ctx.rdx, ctx.r8, ctx.r9};
    const char* view_source = "none";
    const char* reg_names[] = {"rcx", "rdx", "r8", "r9"};
    for (size_t i = 0; i < std::size(regs); ++i) {
        if (sn2_trace_looks_like_view(regs[i])) {
            view = regs[i];
            view_source = reg_names[i];
            break;
        }
    }
    if (view == 0) {
        for (size_t i = 0; i < stack.size(); ++i) {
            if (sn2_trace_looks_like_view(stack[i])) {
                view = stack[i];
                view_source = "stack";
                break;
            }
        }
    }

    uint32_t stereo_pass = 0xffffffffu;
    const int view_id = sn2_trace_view_id(view, &stereo_pass);
    const uintptr_t family = sn2_trace_read_ptr(view + SUBNAUTICA2_SCENEVIEW_FAMILY_OFFSET);
    const uint8_t family_b8 = sn2_trace_read_u8(family + 0xB8);
    const uintptr_t family_param_slot = family + 0x1A8;
    const uintptr_t family_param = sn2_trace_read_ptr(family_param_slot);
    const uintptr_t viewstate = sn2_trace_read_ptr(view + SUBNAUTICA2_SCENEVIEW_VIEWSTATE_OFFSET);
    const uintptr_t view_2650 = sn2_trace_read_ptr(view + 0x2650);
    const uintptr_t view_2658 = sn2_trace_read_ptr(view + 0x2658);
    const uintptr_t view_2660 = sn2_trace_read_ptr(view + 0x2660);

    auto param_q = [](uintptr_t p, size_t qword_index) -> uintptr_t {
        return sn2_trace_read_ptr(p + qword_index * sizeof(uintptr_t));
    };

    SPDLOG_WARN(
        "[SN2-VolFogParam] {} call={} view_src={} view_id={} pass={} "
        "rcx=0x{:x} rdx=0x{:x} r8=0x{:x} r9=0x{:x} "
        "s20=0x{:x} s28=0x{:x} s30=0x{:x} s38=0x{:x} s40=0x{:x} s48=0x{:x} "
        "view=0x{:x} family=0x{:x} family_b8=0x{:02x} family_param_slot=0x{:x} family_param=0x{:x} "
        "viewstate=0x{:x} v2650=0x{:x} v2658=0x{:x} v2660=0x{:x} "
        "fp_q0=0x{:x} fp_q38=0x{:x} fp_q40=0x{:x} fp_q44=0x{:x} fp_q48=0x{:x} "
        "r9_q38=0x{:x} r9_q40=0x{:x} r9_q44=0x{:x}",
        tag,
        n + 1,
        view_source,
        view_id,
        stereo_pass,
        ctx.rcx,
        ctx.rdx,
        ctx.r8,
        ctx.r9,
        stack[0],
        stack[1],
        stack[2],
        stack[3],
        stack[4],
        stack[5],
        view,
        family,
        family_b8,
        family_param_slot,
        family_param,
        viewstate,
        view_2650,
        view_2658,
        view_2660,
        param_q(family_param, 0),
        param_q(family_param, 38),
        param_q(family_param, 40),
        param_q(family_param, 44),
        param_q(family_param, 48),
        param_q(ctx.r9, 38),
        param_q(ctx.r9, 40),
        param_q(ctx.r9, 44));
}

void FFakeStereoRenderingHook::subnautica2_volfog_param_trace_FCFD20(safetyhook::Context& ctx) {
    sn2_volfog_param_trace_dump(0, "FCFD20_InitAttrs", ctx);
}

void FFakeStereoRenderingHook::subnautica2_volfog_param_trace_FD0A50(safetyhook::Context& ctx) {
    sn2_volfog_param_trace_dump(1, "FD0A50_Voxelize", ctx);
}

void FFakeStereoRenderingHook::subnautica2_volfog_param_trace_FB53B0(safetyhook::Context& ctx) {
    sn2_volfog_param_trace_dump(2, "FB53B0_Clear", ctx);
}

void FFakeStereoRenderingHook::subnautica2_volfog_param_trace_FB5320(safetyhook::Context& ctx) {
    sn2_volfog_param_trace_dump(3, "FB5320_LightScatter", ctx);
}

void FFakeStereoRenderingHook::subnautica2_volfog_param_trace_FB5270(safetyhook::Context& ctx) {
    sn2_volfog_param_trace_dump(4, "FB5270_FinalIntegration", ctx);
}

void FFakeStereoRenderingHook::attempt_hook_subnautica2_volumetric_fog_param_trace() {
    if (m_attempted_hook_subnautica2_volumetric_fog_param_trace) {
        return;
    }
    m_attempted_hook_subnautica2_volumetric_fog_param_trace = true;
    if (!subnautica2_is_current_game()) {
        return;
    }
    if (!subnautica2_enable_volumetric_fog_param_trace()) {
        SPDLOG_INFO("[Subnautica2][VolFogParam] disabled (set UEVR_SUBNAUTICA2_VOLUMETRIC_FOG_PARAM_TRACE=1)");
        return;
    }

    const auto exe_base = (uintptr_t)utility::get_executable();
    if (exe_base == 0) {
        SPDLOG_WARN("[Subnautica2][VolFogParam] exe_base==0; cannot install");
        return;
    }

    struct Target {
        uintptr_t rva;
        safetyhook::MidHook FFakeStereoRenderingHook::* slot;
        void (*handler)(safetyhook::Context&);
        const char* tag;
    };
    const Target targets[] = {
        {SUBNAUTICA2_VOLFOG_INIT_VOLUME_ATTRS_RVA, &FFakeStereoRenderingHook::m_subnautica2_volfog_param_trace_FCFD20, &FFakeStereoRenderingHook::subnautica2_volfog_param_trace_FCFD20, "FCFD20_InitAttrs"},
        {SUBNAUTICA2_VOLFOG_VOXELIZE_PRIMS_RVA, &FFakeStereoRenderingHook::m_subnautica2_volfog_param_trace_FD0A50, &FFakeStereoRenderingHook::subnautica2_volfog_param_trace_FD0A50, "FD0A50_Voxelize"},
        {SUBNAUTICA2_VOLFOG_CLEAR_PASS_RVA, &FFakeStereoRenderingHook::m_subnautica2_volfog_param_trace_FB53B0, &FFakeStereoRenderingHook::subnautica2_volfog_param_trace_FB53B0, "FB53B0_Clear"},
        {SUBNAUTICA2_VOLFOG_LIGHT_SCATTER_RVA, &FFakeStereoRenderingHook::m_subnautica2_volfog_param_trace_FB5320, &FFakeStereoRenderingHook::subnautica2_volfog_param_trace_FB5320, "FB5320_LightScatter"},
        {SUBNAUTICA2_VOLFOG_FINAL_INTEGRATION_RVA, &FFakeStereoRenderingHook::m_subnautica2_volfog_param_trace_FB5270, &FFakeStereoRenderingHook::subnautica2_volfog_param_trace_FB5270, "FB5270_FinalIntegration"},
    };

    for (const auto& target_info : targets) {
        const auto target = exe_base + target_info.rva;
        if (!is_executable_process_range(target, 0x10)) {
            SPDLOG_WARN("[Subnautica2][VolFogParam] cannot hook {} at {:x} (not executable)", target_info.tag, target);
            continue;
        }
        auto hook_result = safetyhook::create_mid((void*)target, target_info.handler);
        if (!hook_result) {
            SPDLOG_WARN("[Subnautica2][VolFogParam] safetyhook::create_mid failed for {} at {:x}", target_info.tag, target);
            continue;
        }
        this->*(target_info.slot) = std::move(hook_result);
        SPDLOG_WARN("[Subnautica2][VolFogParam] installed {} at {:x}", target_info.tag, target);
    }
}

void FFakeStereoRenderingHook::attempt_hook_subnautica2_fog_alias_thunk() {
    if (m_attempted_hook_subnautica2_fog_alias_thunk) {
        return;
    }
    m_attempted_hook_subnautica2_fog_alias_thunk = true;
    if (!subnautica2_is_current_game()) {
        return;
    }

    const auto mode = subnautica2_fog_alias_thunk_mode();
    const auto diag_enabled = subnautica2_enable_fog_alias_thunk_diag();
    if (!diag_enabled && mode == 0) {
        SPDLOG_INFO("[Subnautica2][FogAliasThunk] disabled (set UEVR_SUBNAUTICA2_FOG_ALIAS_THUNK_DIAG=1)");
        return;
    }

    const auto exe_base = (uintptr_t)utility::get_executable();
    const auto target = exe_base + SUBNAUTICA2_FOG_ALIAS_THUNK_RVA;
    if (exe_base == 0 || !is_executable_process_range(target, 0x30)) {
        SPDLOG_WARN("[Subnautica2][FogAliasThunk] Bad target {:x}", target);
        return;
    }

    constexpr uint8_t expected_bytes[] = {
        0x48, 0x8B, 0x42, 0x10,                         // mov rax, [rdx+10h]
        0x4C, 0x8B, 0x90, 0x58, 0x26, 0x00, 0x00,       // mov r10, [rax+2658h]
        0x4D, 0x89, 0x90, 0x58, 0x26, 0x00, 0x00,       // mov [r8+2658h], r10
        0x4C, 0x8B, 0x90, 0x60, 0x26, 0x00, 0x00,       // mov r10, [rax+2660h]
        0x4D, 0x89, 0x90, 0x60, 0x26, 0x00, 0x00,       // mov [r8+2660h], r10
    };
    if (std::memcmp((void*)target, expected_bytes, sizeof(expected_bytes)) != 0) {
        SPDLOG_WARN("[Subnautica2][FogAliasThunk] Byte mismatch at {:x}; skipping hook", target);
        return;
    }

    m_subnautica2_fog_alias_thunk_hook = safetyhook::create_inline(
        (void*)target,
        &FFakeStereoRenderingHook::subnautica2_fog_alias_thunk_hook,
        safetyhook::InlineHook::StartDisabled);

    if (!m_subnautica2_fog_alias_thunk_hook) {
        SPDLOG_WARN("[Subnautica2][FogAliasThunk] Failed to create hook at {:x}", target);
        return;
    }

    if (auto enable_result = m_subnautica2_fog_alias_thunk_hook.enable(); !enable_result.has_value()) {
        SPDLOG_WARN("[Subnautica2][FogAliasThunk] Failed to enable hook at {:x}: {}", target, (int)enable_result.error().type);
        return;
    }

    SPDLOG_WARN("[Subnautica2][FogAliasThunk] Hooked thunk at {:x}; mode={} diag={}", target, mode, diag_enabled ? 1 : 0);
}

void FFakeStereoRenderingHook::subnautica2_fog_alias_thunk_hook(
    void* graph_builder,
    void* scene_renderer,
    void* view_info,
    void* arg4,
    uint32_t arg5,
    uint8_t arg6)
{
    auto* hook = g_hook;
    const auto mode = subnautica2_fog_alias_thunk_mode();
    const bool diag_enabled = subnautica2_enable_fog_alias_thunk_diag();
    const uintptr_t renderer = (uintptr_t)scene_renderer;
    const uintptr_t dst_view = (uintptr_t)view_info;
    const uintptr_t src_view = sn2_trace_read_ptr(renderer + 0x10);
    const int src_view_id = subnautica2_view_id_from_view(src_view);
    const int dst_view_id = subnautica2_view_id_from_view(dst_view);
    const uintptr_t src_2658 = sn2_trace_read_ptr(src_view + 0x2658);
    const uintptr_t src_2660 = sn2_trace_read_ptr(src_view + 0x2660);
    const uintptr_t dst_2658 = sn2_trace_read_ptr(dst_view + 0x2658);
    const uintptr_t dst_2660 = sn2_trace_read_ptr(dst_view + 0x2660);
    const uintptr_t src_visible_data = sn2_trace_read_ptr(src_view + 0x1F88);
    const uint32_t src_visible_num = sn2_trace_read_u32(src_view + 0x1F88 + 8);
    const uint32_t src_visible_max = sn2_trace_read_u32(src_view + 0x1F88 + 12);
    const uintptr_t dst_visible_data = sn2_trace_read_ptr(dst_view + 0x1F88);
    const uint32_t dst_visible_num = sn2_trace_read_u32(dst_view + 0x1F88 + 8);
    const uint32_t dst_visible_max = sn2_trace_read_u32(dst_view + 0x1F88 + 12);

    static std::atomic<uint64_t> call_n{0};
    const auto n = call_n.fetch_add(1, std::memory_order_relaxed);
    const bool secondary_alias =
        dst_view_id == 1 && src_view != 0 && src_view != dst_view && src_view_id == 0;
    const bool bypass =
        mode == 2 ||
        mode == 3 ||
        (mode == 1 && secondary_alias);
    const bool temp_copy_visible_lights =
        secondary_alias && mode == 3 &&
        src_visible_data != 0 &&
        src_visible_num != 0 &&
        src_visible_max >= src_visible_num &&
        src_visible_num < 0x10000 &&
        src_visible_max < 0x10000 &&
        is_readable_process_range(src_view + 0x1F88, 16) &&
        is_writable_process_range(dst_view + 0x1F88, 16);

    if (diag_enabled && (n < 96 || (secondary_alias && n < 512) || (bypass && (n % 600) == 0) || (n % 2400) == 0)) {
        SPDLOG_WARN(
            "[Subnautica2][FogAliasThunk] call={} mode={} bypass={} temp_visible={} renderer=0x{:x} src_view=0x{:x} src_id={} dst_view=0x{:x} dst_id={} src2658=0x{:x} src2660=0x{:x} dst2658_before=0x{:x} dst2660_before=0x{:x} srcVis=0x{:x}/{}/{} dstVis=0x{:x}/{}/{}",
            n + 1,
            mode,
            bypass ? 1 : 0,
            temp_copy_visible_lights ? 1 : 0,
            renderer,
            src_view,
            src_view_id,
            dst_view,
            dst_view_id,
            src_2658,
            src_2660,
            dst_2658,
            dst_2660,
            src_visible_data,
            src_visible_num,
            src_visible_max,
            dst_visible_data,
            dst_visible_num,
            dst_visible_max);
    }

    if (bypass) {
        const auto exe_base = (uintptr_t)utility::get_executable();
        const auto real_handler = exe_base + SUBNAUTICA2_REAL_RENDER_FOG_HANDLER_RVA;
        if (exe_base != 0 && is_executable_process_range(real_handler, 0x20)) {
            std::array<uint8_t, 16> original_visible_light_header{};
            bool restored_visible_light_header = false;
            if (temp_copy_visible_lights) {
                std::memcpy(original_visible_light_header.data(), (void*)(dst_view + 0x1F88), original_visible_light_header.size());
                std::memcpy((void*)(dst_view + 0x1F88), (void*)(src_view + 0x1F88), original_visible_light_header.size());
                restored_visible_light_header = true;
                if (diag_enabled && (n < 96 || (n % 600) == 0)) {
                    SPDLOG_WARN(
                        "[Subnautica2][FogAliasThunk][VisibleLightInfos] call={} copied src header to secondary for RealFogHandler only: src=0x{:x}/{}/{} dst_was=0x{:x}/{}/{}",
                        n + 1,
                        src_visible_data,
                        src_visible_num,
                        src_visible_max,
                        dst_visible_data,
                        dst_visible_num,
                        dst_visible_max);
                }
            }
            utility::ScopeGuard restore_visible_lights{[&]() {
                if (restored_visible_light_header && is_writable_process_range(dst_view + 0x1F88, original_visible_light_header.size())) {
                    std::memcpy((void*)(dst_view + 0x1F88), original_visible_light_header.data(), original_visible_light_header.size());
                }
            }};
            using RealFogHandler = void (*)(void*, void*, void*, void*, uint32_t, uint8_t);
            reinterpret_cast<RealFogHandler>(real_handler)(graph_builder, scene_renderer, view_info, arg4, arg5, arg6);
            return;
        }
        SPDLOG_WARN("[Subnautica2][FogAliasThunk] mode={} requested bypass, but real handler target is invalid", mode);
    }

    if (hook != nullptr && hook->m_subnautica2_fog_alias_thunk_hook) {
        hook->m_subnautica2_fog_alias_thunk_hook.unsafe_call<void>(
            graph_builder,
            scene_renderer,
            view_info,
            arg4,
            arg5,
            arg6);
    }
}

void FFakeStereoRenderingHook::attempt_hook_subnautica2_light_affects_view() {
    if (m_attempted_hook_subnautica2_light_affects_view) {
        return;
    }
    m_attempted_hook_subnautica2_light_affects_view = true;
    if (!subnautica2_is_current_game()) {
        return;
    }

    const auto mode = subnautica2_light_affects_view_mode();
    const auto diag_enabled = subnautica2_enable_light_affects_view_diag();
    if (!diag_enabled && mode == 0) {
        SPDLOG_INFO("[Subnautica2][LightAffectsView] disabled (set UEVR_SUBNAUTICA2_LIGHT_AFFECTS_VIEW_DIAG=1)");
        return;
    }

    const auto exe_base = (uintptr_t)utility::get_executable();
    const auto target = exe_base + SUBNAUTICA2_LIGHT_AFFECTS_VIEW_RVA;
    if (exe_base == 0 || !is_executable_process_range(target, 0x40)) {
        SPDLOG_WARN("[Subnautica2][LightAffectsView] Bad target {:x}", target);
        return;
    }

    constexpr uint8_t expected_bytes[] = {
        0xF6, 0x81, 0xE4, 0x00, 0x00, 0x00, 0x02, // test byte ptr [rcx+E4h], 2
        0x74, 0x79,                               // jz +79h
        0x48, 0x63, 0x41, 0x34,                   // movsxd rax, dword ptr [rcx+34h]
        0x4C, 0x6B, 0xC8, 0x68,                   // imul r9, rax, 68h
    };
    if (std::memcmp((void*)target, expected_bytes, sizeof(expected_bytes)) != 0) {
        SPDLOG_WARN("[Subnautica2][LightAffectsView] Byte mismatch at {:x}; skipping hook", target);
        return;
    }

    m_subnautica2_light_affects_view_hook = safetyhook::create_inline(
        (void*)target,
        &FFakeStereoRenderingHook::subnautica2_light_affects_view_hook,
        safetyhook::InlineHook::StartDisabled);

    if (!m_subnautica2_light_affects_view_hook) {
        SPDLOG_WARN("[Subnautica2][LightAffectsView] Failed to create hook at {:x}", target);
        return;
    }

    if (auto enable_result = m_subnautica2_light_affects_view_hook.enable(); !enable_result.has_value()) {
        SPDLOG_WARN("[Subnautica2][LightAffectsView] Failed to enable hook at {:x}: {}", target, (int)enable_result.error().type);
        return;
    }

    SPDLOG_WARN("[Subnautica2][LightAffectsView] Hooked predicate at {:x}; mode={} diag={}", target, mode, diag_enabled ? 1 : 0);
}

uint8_t FFakeStereoRenderingHook::subnautica2_light_affects_view_hook(
    void* light_scene_info,
    void* view_info,
    uint8_t flag)
{
    auto* hook = g_hook;
    if (hook == nullptr || !hook->m_subnautica2_light_affects_view_hook) {
        return 0;
    }

    const uint8_t native = hook->m_subnautica2_light_affects_view_hook.unsafe_call<uint8_t>(
        light_scene_info,
        view_info,
        flag);

    const auto mode = subnautica2_light_affects_view_mode();
    const bool diag_enabled = subnautica2_enable_light_affects_view_diag();
    const uintptr_t light = (uintptr_t)light_scene_info;
    const uintptr_t view = (uintptr_t)view_info;
    const int view_id = subnautica2_view_id_from_view(view);
    const uint32_t light_index = sn2_trace_read_u32(light + 0x34);
    const uintptr_t table = sn2_trace_read_ptr(view + 0x1F88);
    const uintptr_t entry = (table != 0 && light_index < 0x100000)
        ? table + (static_cast<uintptr_t>(light_index) * 0x68)
        : 0;
    const uint32_t table_bits = sn2_trace_read_u32(entry + 0x30);
    const uint32_t selected_bit = flag != 0 ? (table_bits >> 1) & 1u : table_bits & 1u;

    int primary_result = -1;
    uint32_t primary_bits = 0xffffffffu;
    uint32_t primary_selected_bit = 0xffffffffu;
    uint32_t primary_visible_num = 0;
    uint32_t primary_visible_max = 0;
    uintptr_t primary_view = 0;
    uintptr_t primary_table = 0;
    if (view_id == 1 && view >= SUBNAUTICA2_SCENEVIEW_STRIDE) {
        primary_view = view - SUBNAUTICA2_SCENEVIEW_STRIDE;
        if (subnautica2_view_id_from_view(primary_view) == 0) {
            primary_result = hook->m_subnautica2_light_affects_view_hook.unsafe_call<uint8_t>(
                light_scene_info,
                (void*)primary_view,
                flag);
            primary_table = sn2_trace_read_ptr(primary_view + 0x1F88);
            const uintptr_t primary_entry = (primary_table != 0 && light_index < 0x100000)
                ? primary_table + (static_cast<uintptr_t>(light_index) * 0x68)
                : 0;
            primary_bits = sn2_trace_read_u32(primary_entry + 0x30);
            primary_selected_bit = flag != 0 ? (primary_bits >> 1) & 1u : primary_bits & 1u;
            primary_visible_num = sn2_trace_read_u32(primary_view + 0x1F88 + 8);
            primary_visible_max = sn2_trace_read_u32(primary_view + 0x1F88 + 12);
        }
    }

    int visible_copy_result = -1;
    bool visible_copy_applied = false;
    if (view_id == 1 && (mode == 4 || mode == 5) &&
        primary_view != 0 &&
        primary_table != 0 &&
        primary_visible_num != 0 &&
        primary_visible_max >= primary_visible_num &&
        primary_visible_num < 0x10000 &&
        primary_visible_max < 0x10000 &&
        is_readable_process_range(primary_view + 0x1F88, 16) &&
        is_writable_process_range(view + 0x1F88, 16))
    {
        std::array<uint8_t, 16> original_visible_light_header{};
        std::memcpy(original_visible_light_header.data(), (void*)(view + 0x1F88), original_visible_light_header.size());
        std::memcpy((void*)(view + 0x1F88), (void*)(primary_view + 0x1F88), original_visible_light_header.size());
        visible_copy_applied = true;
        visible_copy_result = hook->m_subnautica2_light_affects_view_hook.unsafe_call<uint8_t>(
            light_scene_info,
            view_info,
            flag) ? 1 : 0;
        if (is_writable_process_range(view + 0x1F88, original_visible_light_header.size())) {
            std::memcpy((void*)(view + 0x1F88), original_visible_light_header.data(), original_visible_light_header.size());
        }
    }

    uint8_t final_result = native;
    if (view_id == 1) {
        if (mode == 1 && native == 0 && primary_result > 0) {
            final_result = 1;
        } else if (mode == 2 && primary_result >= 0) {
            final_result = static_cast<uint8_t>(primary_result != 0);
        } else if (mode == 3) {
            final_result = 1;
        } else if (mode == 4 && visible_copy_result >= 0) {
            final_result = static_cast<uint8_t>(visible_copy_result != 0);
        } else if (mode == 5 && visible_copy_result > 0) {
            final_result = 1;
        }
    }

    static std::atomic<uint64_t> call_n{0};
    const auto n = call_n.fetch_add(1, std::memory_order_relaxed);
    const bool mismatch = view_id == 1 && primary_result >= 0 && native != primary_result;
    const bool changed = final_result != native;
    if (diag_enabled && (n < 192 || mismatch || changed || (visible_copy_applied && (n % 4000) == 0) || (n % 16000) == 0)) {
        SPDLOG_WARN(
            "[Subnautica2][LightAffectsView] call={} mode={} light=0x{:x} light_idx={} flag={} view=0x{:x} view_id={} native={} primary={} viscopy={} final={} table=0x{:x} bits=0x{:08x} bit={} primary_view=0x{:x} primary_table=0x{:x} primary_bits=0x{:08x} primary_bit={} primary_vis={}/{} copy_applied={} v11e8={} v11eb={} v11f2={} v1bd0={} v1bd1={} v24c8={} v2941={} light_e4=0x{:02x} light1d8=0x{:02x} light1f1=0x{:02x} light1f2=0x{:02x}",
            n + 1,
            mode,
            light,
            light_index,
            flag,
            view,
            view_id,
            native ? 1 : 0,
            primary_result,
            visible_copy_result,
            final_result ? 1 : 0,
            table,
            table_bits,
            selected_bit,
            primary_view,
            primary_table,
            primary_bits,
            primary_selected_bit,
            primary_visible_num,
            primary_visible_max,
            visible_copy_applied ? 1 : 0,
            sn2_trace_read_u8(view + 0x11E8),
            sn2_trace_read_u8(view + 0x11EB),
            sn2_trace_read_u8(view + 0x11F2),
            sn2_trace_read_u8(view + 0x1BD0),
            sn2_trace_read_u8(view + 0x1BD1),
            sn2_trace_read_u8(view + 0x24C8),
            sn2_trace_read_u8(view + 0x2941),
            sn2_trace_read_u8(light + 0xE4),
            sn2_trace_read_u8(light + 0x1D8),
            sn2_trace_read_u8(light + 0x1F1),
            sn2_trace_read_u8(light + 0x1F2));
    }

    return final_result;
}

void FFakeStereoRenderingHook::attempt_hook_subnautica2_setup_volumetric_fog_ub() {
    if (m_attempted_hook_subnautica2_setup_volumetric_fog_ub) {
        return;
    }
    m_attempted_hook_subnautica2_setup_volumetric_fog_ub = true;
    if (!subnautica2_is_current_game()) {
        return;
    }
    if (subnautica2_disable_legacy_fog_uniform_mutations() || subnautica2_disable_setup_volumetric_fog_ub_hook()) {
        SPDLOG_INFO("[Subnautica2][FogIsolation] Skipping SetupVolumetricFogUniformBufferParameters hook");
        return;
    }

    const auto exe_base = (uintptr_t)utility::get_executable();
    const auto target = exe_base + SUBNAUTICA2_SETUP_VOLUMETRIC_FOG_UB_RVA;
    if (exe_base == 0 || !is_executable_process_range(target, 0x10)) {
        SPDLOG_WARN("[Subnautica2][SetupFogUB] Bad target {:x}", target);
        return;
    }

    // Prologue: mov [rsp+18h], rbx; push rbp; push rsi; push rdi; sub rsp, 70h
    constexpr uint8_t expected_prologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x70,
    };
    if (std::memcmp((void*)target, expected_prologue, sizeof(expected_prologue)) != 0) {
        SPDLOG_WARN("[Subnautica2][SetupFogUB] Prologue mismatch at {:x}; skipping", target);
        return;
    }

    m_subnautica2_setup_volumetric_fog_ub_hook = safetyhook::create_inline(
        (void*)target,
        &FFakeStereoRenderingHook::subnautica2_setup_volumetric_fog_ub_hook,
        safetyhook::InlineHook::StartDisabled);

    if (!m_subnautica2_setup_volumetric_fog_ub_hook) {
        SPDLOG_WARN("[Subnautica2][SetupFogUB] Failed to create hook at {:x}", target);
        return;
    }
    if (auto enable_result = m_subnautica2_setup_volumetric_fog_ub_hook.enable(); !enable_result.has_value()) {
        SPDLOG_WARN("[Subnautica2][SetupFogUB] Failed to enable hook at {:x}: {}", target, (int)enable_result.error().type);
        return;
    }
    SPDLOG_WARN("[Subnautica2][SetupFogUB] Hooked SetupVolumetricFogUniformBufferParameters at {:x}", target);
}

// REAL FIX: pair-detect view 0 / view 1 using view stride 0x29D0. For view 0
// of a stereo pair, save the UB's volumetric fog section (~128 bytes at
// offset 0xFC0) AFTER original returns. For view 1 of that pair, overwrite
// view 1's UB fog section with view 0's saved bytes. Basepass for view 1
// then reads view 0's IntegratedLightScattering binding -> visible teal in
// BOTH eyes.
void FFakeStereoRenderingHook::subnautica2_setup_volumetric_fog_ub_hook(
    void* view_info,
    void* view_uniform_shader_parameters)
{
    auto* hook = g_hook;
    if (hook == nullptr || !hook->m_subnautica2_setup_volumetric_fog_ub_hook) {
        return;
    }

    // Always call original first so the game continues to function normally.
    hook->m_subnautica2_setup_volumetric_fog_ub_hook.unsafe_call<void>(
        view_info, view_uniform_shader_parameters);

    if (view_info == nullptr || view_uniform_shader_parameters == nullptr) {
        return;
    }

    constexpr size_t FOG_UB_OFFSET = 0x0;       // FIX-V2: scan FULL UB
    constexpr size_t FOG_UB_SIZE = 10076;       // FViewUniformShaderParameters total size

    const uintptr_t this_view = (uintptr_t)view_info;
    const uintptr_t this_ub = (uintptr_t)view_uniform_shader_parameters;

    if (!is_writable_process_range(this_ub + FOG_UB_OFFSET, FOG_UB_SIZE)) {
        return;
    }

    // 2026-05-16 FIX-V2: pair-detect view 0/1 and SURGICALLY rewrite ONLY the
    // fog texture handle bytes (not the entire section) for view 1. This is
    // the real binding fix: replace the 8-byte handle in view 1's UB that
    // points to view 0's filtered fog texture with view 1's natural pointer
    // (captured in compute_volumetric_fog_hook before propagation).
    static thread_local uintptr_t prev_view = 0;

    static std::atomic<uint64_t> log_n{0};

    if (prev_view != 0 && this_view == prev_view + SUBNAUTICA2_SCENEVIEW_STRIDE) {
        // VIEW 1 of pair.
        const uintptr_t v1_natural = g_subnautica2_view1_natural_lightscat.load(std::memory_order_relaxed);
        const uintptr_t v0_handle = g_subnautica2_view0_lightscat.load(std::memory_order_relaxed);
        int32_t handle_offset = g_subnautica2_fog_ub_handle_offset.load(std::memory_order_relaxed);

        if (v1_natural != 0 && v0_handle != 0 && handle_offset >= 0 &&
            (size_t)handle_offset + 8 <= FOG_UB_SIZE) {
            // Scan view 1's UB at the discovered offset and replace.
            uintptr_t* slot = (uintptr_t*)(this_ub + FOG_UB_OFFSET + handle_offset);
            const uintptr_t before = *slot;
            *slot = v1_natural;
            static std::atomic<uint64_t> fixed_calls{0};
            const auto fc = fixed_calls.fetch_add(1, std::memory_order_relaxed);
            if (fc < 8 || (fc % 600) == 0) {
                SPDLOG_WARN(
                    "[Subnautica2][FixV2] view1 ub=0x{:x} fog[+0x{:x}+0x{:x}]: 0x{:x} -> 0x{:x} (write v1_natural)",
                    this_ub, FOG_UB_OFFSET, handle_offset, before, v1_natural);
            }
        } else {
            static std::atomic<uint64_t> skip_calls{0};
            const auto sc = skip_calls.fetch_add(1, std::memory_order_relaxed);
            if (sc < 4 || (sc % 600) == 0) {
                SPDLOG_WARN(
                    "[Subnautica2][FixV2] view1 SKIP: v1_natural=0x{:x} v0_handle=0x{:x} handle_offset={}",
                    v1_natural, v0_handle, handle_offset);
            }
        }
        prev_view = 0;
    } else {
        // VIEW 0 (or non-stereo). Discover the byte offset of the fog texture
        // handle within UB+FOG_UB_OFFSET by scanning for the 8-byte sequence
        // matching view 0's +0x2658 value (which IS the handle).
        prev_view = this_view;

        const uintptr_t v0_handle = g_subnautica2_view0_lightscat.load(std::memory_order_relaxed);
        if (v0_handle != 0 && g_subnautica2_fog_ub_handle_offset.load(std::memory_order_relaxed) < 0) {
            for (size_t off = 0; off + 8 <= FOG_UB_SIZE; off += 8) {
                const uintptr_t candidate = *(uintptr_t*)(this_ub + FOG_UB_OFFSET + off);
                if (candidate == v0_handle) {
                    g_subnautica2_fog_ub_handle_offset.store((int32_t)off, std::memory_order_relaxed);
                    SPDLOG_WARN(
                        "[Subnautica2][FixV2] DISCOVERED handle_offset=0x{:x} (UB+0x{:x}+0x{:x}=0x{:x} matches view0+0x2658)",
                        off, FOG_UB_OFFSET, off, v0_handle);
                    break;
                }
            }
            if (g_subnautica2_fog_ub_handle_offset.load(std::memory_order_relaxed) < 0) {
                // Not found — log dump for diagnostics
                static std::atomic<uint64_t> nf_calls{0};
                const auto nfc = nf_calls.fetch_add(1, std::memory_order_relaxed);
                if (nfc < 4) {
                    std::string hex;
                    for (size_t off = 0; off + 8 <= FOG_UB_SIZE; off += 8) {
                        const uintptr_t v = *(uintptr_t*)(this_ub + FOG_UB_OFFSET + off);
                        hex += fmt::format(" [0x{:x}]=0x{:x}", off, v);
                    }
                    SPDLOG_WARN(
                        "[Subnautica2][FixV2] handle NOT FOUND in UB scan (looking for 0x{:x}); UB dump:{}",
                        v0_handle, hex);
                }
            }
        }

        const auto cnt = log_n.fetch_add(1, std::memory_order_relaxed);
        if (cnt < 4) {
            uint64_t first = *(uint64_t*)(this_ub + FOG_UB_OFFSET);
            SPDLOG_WARN(
                "[Subnautica2][SetupFogUB] view0 view=0x{:x} ub=0x{:x} fog[0x{:x}] first=0x{:x}",
                this_view, this_ub, FOG_UB_OFFSET, first);
        }
    }
}

void FFakeStereoRenderingHook::attempt_hook_subnautica2_single_layer_water_scene_without_water() {
    if (m_attempted_hook_subnautica2_single_layer_water_scene_without_water) {
        return;
    }

    m_attempted_hook_subnautica2_single_layer_water_scene_without_water = true;

    if (!subnautica2_is_current_game()) {
        return;
    }

    const auto exe_base = (uintptr_t)utility::get_executable();
    const auto target = exe_base + SUBNAUTICA2_SINGLE_LAYER_WATER_SCENE_WITHOUT_WATER_READY_RVA;

    if (exe_base == 0 || !is_executable_process_range(target, 0x10)) {
        SPDLOG_WARN("[Subnautica2][SceneWithoutWater] Cannot hook RenderSingleLayerWater handoff; bad target {:x}", target);
        return;
    }

    constexpr uint8_t expected_bytes[] = {
        0x48, 0x85, 0xC9,             // test rcx, rcx
        0x74, 0x05,                   // jz short
        0xE8, 0xFF, 0xCB, 0x45, 0xFE, // call FMemory::Free
    };

    if (std::memcmp((void*)target, expected_bytes, sizeof(expected_bytes)) != 0) {
        SPDLOG_WARN("[Subnautica2][SceneWithoutWater] RenderSingleLayerWater handoff bytes mismatch at {:x}; skipping hook for this build", target);
        return;
    }

    auto hook_result = safetyhook::create_mid(
        (void*)target,
        &FFakeStereoRenderingHook::subnautica2_single_layer_water_scene_without_water_hook);

    if (!hook_result) {
        SPDLOG_WARN("[Subnautica2][SceneWithoutWater] Failed to hook RenderSingleLayerWater handoff at {:x}", target);
        return;
    }

    m_subnautica2_single_layer_water_scene_without_water_hook = std::move(hook_result);

    SPDLOG_WARN(
        "[Subnautica2][SceneWithoutWater] Hooked RenderSingleLayerWater handoff at {:x}; per-eye rect/UV fix {}",
        target,
        subnautica2_disable_underwater_fog_view_data_fix() ? "disabled" : "enabled");
}

void FFakeStereoRenderingHook::attempt_hook_subnautica2_single_layer_water() {
    if (m_attempted_hook_subnautica2_single_layer_water) {
        return;
    }

    m_attempted_hook_subnautica2_single_layer_water = true;

    if (!subnautica2_is_current_game()) {
        return;
    }

    const auto exe_base = (uintptr_t)utility::get_executable();
    const auto target = exe_base + SUBNAUTICA2_SINGLE_LAYER_WATER_RVA;

    if (exe_base == 0 || !is_executable_process_range(target, 0x20)) {
        SPDLOG_WARN("[Subnautica2][SingleLayerWater] Cannot hook RenderSingleLayerWater; bad target {:x}", target);
        return;
    }

    constexpr uint8_t expected_prologue[] = {
        0x40, 0x55,                         // push rbp
        0x53,                               // push rbx
        0x56,                               // push rsi
        0x57,                               // push rdi
        0x41, 0x54,                         // push r12
        0x41, 0x55,                         // push r13
        0x41, 0x56,                         // push r14
        0x41, 0x57,                         // push r15
        0x48, 0x8D, 0xAC, 0x24, 0x98, 0xFD, 0xFF, 0xFF, // lea rbp, [rsp-268h]
    };

    if (std::memcmp((void*)target, expected_prologue, sizeof(expected_prologue)) != 0) {
        SPDLOG_WARN("[Subnautica2][SingleLayerWater] RenderSingleLayerWater prologue mismatch at {:x}; skipping hook for this build", target);
        return;
    }

    m_subnautica2_single_layer_water_hook = safetyhook::create_inline(
        (void*)target,
        &FFakeStereoRenderingHook::subnautica2_single_layer_water_hook,
        safetyhook::InlineHook::StartDisabled);

    if (!m_subnautica2_single_layer_water_hook) {
        SPDLOG_WARN("[Subnautica2][SingleLayerWater] Failed to create RenderSingleLayerWater hook at {:x}", target);
        return;
    }

    if (auto enable_result = m_subnautica2_single_layer_water_hook.enable(); !enable_result.has_value()) {
        SPDLOG_WARN("[Subnautica2][SingleLayerWater] Failed to enable RenderSingleLayerWater hook at {:x}: {}", target, (int)enable_result.error().type);
        return;
    }

    SPDLOG_WARN(
        "[Subnautica2][SingleLayerWater] Hooked RenderSingleLayerWater at {:x}; runtime ViewRect fix {}",
        target,
        subnautica2_disable_single_layer_water_view_rect_fix() ? "disabled" : "enabled");
}

void FFakeStereoRenderingHook::attempt_hook_subnautica2_single_layer_water_inner() {
    if (m_attempted_hook_subnautica2_single_layer_water_inner) {
        return;
    }

    m_attempted_hook_subnautica2_single_layer_water_inner = true;

    if (!subnautica2_is_current_game()) {
        return;
    }

    const auto exe_base = (uintptr_t)utility::get_executable();
    const auto target = exe_base + SUBNAUTICA2_SINGLE_LAYER_WATER_INNER_RVA;

    if (exe_base == 0 || !is_executable_process_range(target, 0x20)) {
        SPDLOG_WARN("[Subnautica2][SingleLayerWaterInner] Cannot hook RenderSingleLayerWaterInner; bad target {:x}", target);
        return;
    }

    constexpr uint8_t expected_prologue[] = {
        0x4C, 0x8B, 0xDC,             // mov r11, rsp
        0x55,                         // push rbp
        0x41, 0x57,                   // push r15
        0x49, 0x8D, 0xAB, 0xD8, 0xFC, 0xFF, 0xFF, // lea rbp, [r11-328h]
    };

    if (std::memcmp((void*)target, expected_prologue, sizeof(expected_prologue)) != 0) {
        SPDLOG_WARN("[Subnautica2][SingleLayerWaterInner] RenderSingleLayerWaterInner prologue mismatch at {:x}; skipping hook for this build", target);
        return;
    }

    m_subnautica2_single_layer_water_inner_hook = safetyhook::create_inline(
        (void*)target,
        &FFakeStereoRenderingHook::subnautica2_single_layer_water_inner_hook,
        safetyhook::InlineHook::StartDisabled);

    if (!m_subnautica2_single_layer_water_inner_hook) {
        SPDLOG_WARN("[Subnautica2][SingleLayerWaterInner] Failed to create RenderSingleLayerWaterInner hook at {:x}", target);
        return;
    }

    if (auto enable_result = m_subnautica2_single_layer_water_inner_hook.enable(); !enable_result.has_value()) {
        SPDLOG_WARN("[Subnautica2][SingleLayerWaterInner] Failed to enable RenderSingleLayerWaterInner hook at {:x}: {}", target, (int)enable_result.error().type);
        return;
    }

    SPDLOG_WARN(
        "[Subnautica2][SingleLayerWaterInner] Hooked RenderSingleLayerWaterInner at {:x}; secondary water pass fix {}",
        target,
        subnautica2_disable_single_layer_water_pass_fix() ? "disabled" : "enabled");
}

// 2026-05-16 Phase 3: hook the per-view SLW dispatcher (sub_142EB72E0) located
// inside the SLW inner loop. Fires once per FViewInfo* (passed as R8). This is
// the per-view boundary where we can rewrite bindless descriptor heap slots
// (currently slots like 363073/363074 — heap-state-dependent) BETWEEN view 0's
// and view 1's SLW pixel-shader draws.
//
// Initial role: pure diagnostic — log r8/rcx/rdx/r9 + first 4 stack args + the
// FViewInfo*'s stereo_pass field (+0xDD0) to confirm per-view firing and
// distinguish view 0 from view 1. Future revision will add the descriptor swap.
void FFakeStereoRenderingHook::attempt_hook_subnautica2_slw_per_view() {
    if (m_attempted_hook_subnautica2_slw_per_view) {
        return;
    }
    m_attempted_hook_subnautica2_slw_per_view = true;

    if (!subnautica2_is_current_game()) {
        return;
    }

    if (!subnautica2_enable_slw_per_view_hook()) {
        SPDLOG_WARN("[Subnautica2][SLWPerView] Hook disabled (UEVR_SUBNAUTICA2_ENABLE_SLW_PER_VIEW_HOOK!=1)");
        return;
    }

    const auto exe_base = (uintptr_t)utility::get_executable();
    const auto target = exe_base + SUBNAUTICA2_SLW_PER_VIEW_RVA;

    if (exe_base == 0 || !is_executable_process_range(target, 0x20)) {
        SPDLOG_WARN("[Subnautica2][SLWPerView] Cannot hook sub_142EB72E0; bad target {:x}", target);
        return;
    }

    // Verified from binary dump 2026-05-16: standard MSVC x64 prologue.
    constexpr uint8_t expected_prologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x20,             // mov [rsp+20], rbx
        0x48, 0x89, 0x54, 0x24, 0x10,             // mov [rsp+10], rdx
        0x48, 0x89, 0x4C, 0x24, 0x08,             // mov [rsp+08], rcx
        0x55,                                       // push rbp
        0x56,                                       // push rsi
        0x57,                                       // push rdi
        0x41, 0x54,                                 // push r12
        0x41, 0x55,                                 // push r13
        0x41, 0x56,                                 // push r14
        0x41, 0x57,                                 // push r15
        0x48, 0x83, 0xEC, 0x70,                     // sub rsp, 70
    };

    if (std::memcmp((void*)target, expected_prologue, sizeof(expected_prologue)) != 0) {
        SPDLOG_WARN("[Subnautica2][SLWPerView] prologue mismatch at {:x}; skipping hook for this build", target);
        return;
    }

    m_subnautica2_slw_per_view_hook = safetyhook::create_inline(
        (void*)target,
        &FFakeStereoRenderingHook::subnautica2_slw_per_view_hook,
        safetyhook::InlineHook::StartDisabled);

    if (!m_subnautica2_slw_per_view_hook) {
        SPDLOG_WARN("[Subnautica2][SLWPerView] Failed to create hook at {:x}", target);
        return;
    }

    if (auto enable_result = m_subnautica2_slw_per_view_hook.enable(); !enable_result.has_value()) {
        SPDLOG_WARN("[Subnautica2][SLWPerView] Failed to enable hook at {:x}: {}", target, (int)enable_result.error().type);
        return;
    }

    SPDLOG_WARN(
        "[Subnautica2][SLWPerView] Hooked sub_142EB72E0 at {:x}; diagnostic mode (log per-view firing, no behavior change)",
        target);
}

void __cdecl FFakeStereoRenderingHook::subnautica2_slw_per_view_hook(
    void* scene_renderer,
    void* arg2,
    void* view_info,
    void* arg4,
    void* stack0,
    void* stack1,
    void* stack2,
    void* stack3)
{
    auto* hook = g_hook;

    auto call_original = [&]() {
        if (hook != nullptr && hook->m_subnautica2_slw_per_view_hook) {
            hook->m_subnautica2_slw_per_view_hook.unsafe_call<void>(
                scene_renderer, arg2, view_info, arg4, stack0, stack1, stack2, stack3);
        }
    };

    // Read stereo_pass from FViewInfo+0xDD0 (constant SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET).
    uint32_t stereo_pass = 0xFFFFFFFF;
    uint32_t stereo_index = 0xFFFFFFFF;
    if (view_info != nullptr) {
        if (is_readable_process_range((uintptr_t)view_info + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET, 4)) {
            stereo_pass = *(uint32_t*)((uintptr_t)view_info + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET);
        }
        if (is_readable_process_range((uintptr_t)view_info + SUBNAUTICA2_SCENEVIEW_STEREO_VIEW_INDEX_OFFSET, 4)) {
            stereo_index = *(uint32_t*)((uintptr_t)view_info + SUBNAUTICA2_SCENEVIEW_STEREO_VIEW_INDEX_OFFSET);
        }
    }

    static std::atomic<uint64_t> call_count{0};
    const auto cn = call_count.fetch_add(1, std::memory_order_relaxed);

    // 2026-05-17 WHOLE-VIEWINFO DIFF: scan entire FViewInfo struct in 256-byte
    // chunks. For each chunk, count bytes that differ between view 0 and view 1.
    // Chunks with most diffs are the view-specific regions (where stereo matrices
    // should live).
    if (view_info != nullptr) {
        static thread_local uintptr_t tl_seen_view0 = 0;
        static thread_local uint8_t tl_v0_full[0x2A00]{};
        constexpr uintptr_t SCAN_SIZE = 0x2A00; // ~ FViewInfo size 0x29D0
        if (is_readable_process_range((uintptr_t)view_info, SCAN_SIZE)) {
            if (stereo_pass == 1) {
                memcpy(tl_v0_full, (const void*)view_info, SCAN_SIZE);
                tl_seen_view0 = (uintptr_t)view_info;
            } else if (stereo_pass == 2 && tl_seen_view0 != 0) {
                static std::atomic<uint64_t> mc_count{0};
                const auto mc = mc_count.fetch_add(1, std::memory_order_relaxed);
                if (mc < 4 || (mc % 600) == 0) {
                    // For each 256-byte chunk, count diff bytes
                    char buf[2048] = {0};
                    int written = 0;
                    constexpr int CHUNK_SIZE = 256;
                    int total_diff = 0;
                    for (int chunk_off = 0; chunk_off < (int)SCAN_SIZE; chunk_off += CHUNK_SIZE) {
                        int chunk_diff = 0;
                        for (int i = 0; i < CHUNK_SIZE && chunk_off + i < (int)SCAN_SIZE; ++i) {
                            if (((uint8_t*)view_info)[chunk_off + i] != tl_v0_full[chunk_off + i]) {
                                ++chunk_diff;
                            }
                        }
                        if (chunk_diff > 0) {
                            int n = snprintf(buf + written, sizeof(buf) - written,
                                "[0x%x:%d]", chunk_off, chunk_diff);
                            if (n > 0) written += n;
                            total_diff += chunk_diff;
                        }
                    }
                    SPDLOG_WARN(
                        "[Subnautica2][ViewInfoDiff] mc={} v0@0x{:x} v1@0x{:x} total_diff={}/{} chunks: {}",
                        mc + 1, tl_seen_view0, (uintptr_t)view_info,
                        total_diff, SCAN_SIZE, buf);
                }
            }
        }
    }

    // 2026-05-17 Plan A.3: log SRV-map view-tag counts to validate Phase A.1
    // tagging is working. Runs only at log-gate ticks to avoid overhead.
    {
        static std::atomic<uint64_t> log_count{0};
        const auto lc = log_count.fetch_add(1, std::memory_order_relaxed);
        if (lc < 16 || (lc % 600) == 0) {
            SPDLOG_WARN(
                "[Subnautica2][SLWPerView][SRVMapStatus] call={} pass={} idx={} | total={} v0_count={} v1_count={}",
                lc + 1, stereo_pass, stereo_index,
                sn2_fog_srv_map::size(),
                sn2_fog_srv_map::count_by_view(0),
                sn2_fog_srv_map::count_by_view(1));
        }
    }

    // 2026-05-16 EXPERIMENT: per-view field copy. Save view 0's pointer on its
    // call (stereo_pass==1), then on view 1's call (stereo_pass==2), copy a
    // configurable byte range from view 0 to view 1 in place. After the original
    // function runs, restore view 1's original bytes.
    static thread_local void* tl_saved_view0 = nullptr;
    static thread_local std::array<uint8_t, 0x100> tl_view1_backup{};  // up to 256 bytes
    static thread_local uint32_t tl_view1_backup_size = 0;
    static thread_local uintptr_t tl_view1_backup_off = 0;
    bool field_copy_fired = false;

    if (subnautica2_enable_slw_per_view_field_copy() && view_info != nullptr && stereo_pass != 0xFFFFFFFF) {
        const uint32_t copy_off = subnautica2_slw_field_copy_start();
        const uint32_t copy_sz = subnautica2_slw_field_copy_size();
        if (stereo_pass == 1) {
            tl_saved_view0 = view_info;
        } else if (stereo_pass == 2 && tl_saved_view0 != nullptr && copy_sz > 0 && copy_sz <= tl_view1_backup.size()) {
            if (is_readable_process_range((uintptr_t)tl_saved_view0 + copy_off, copy_sz) &&
                is_writable_process_range((uintptr_t)view_info + copy_off, copy_sz))
            {
                memcpy(tl_view1_backup.data(), (const void*)((uintptr_t)view_info + copy_off), copy_sz);
                tl_view1_backup_size = copy_sz;
                tl_view1_backup_off = copy_off;
                memcpy((void*)((uintptr_t)view_info + copy_off),
                       (const void*)((uintptr_t)tl_saved_view0 + copy_off),
                       copy_sz);
                field_copy_fired = true;

                static std::atomic<uint64_t> fc_count{0};
                const auto fn = fc_count.fetch_add(1, std::memory_order_relaxed);
                if (fn < 8 || (fn % 600) == 0) {
                    const uintptr_t* v0p = (const uintptr_t*)((uintptr_t)tl_saved_view0 + copy_off);
                    const uintptr_t* v1p = (const uintptr_t*)tl_view1_backup.data();
                    SPDLOG_WARN(
                        "[Subnautica2][SLWPerView][FieldCopy] fc={} off=0x{:x} size={} v0=[0x{:x} 0x{:x} 0x{:x} 0x{:x}] v1_was=[0x{:x} 0x{:x} 0x{:x} 0x{:x}]",
                        fn + 1, copy_off, copy_sz,
                        v0p[0], v0p[1], v0p[2], v0p[3],
                        v1p[0], v1p[1], v1p[2], v1p[3]);
                }
            }
        }
    }

    // Log gate: catch BOTH parities — log calls 1..32 AND every 601st OR every 600th (pairs).
    const bool log_now = (cn < 32) || ((cn % 600) == 0) || ((cn % 600) == 1);

    // 2026-05-17 NEW DIAGNOSTIC: dump the contents at view+0x2618..+0x2648.
    // These pointers were confirmed per-view-distinct. Reading their target
    // memory is SAFE (we don't modify). The vtable at offset +0 tells us the
    // object type; if it's a known FRDGTexture3D vtable, we're golden.
    if (log_now && view_info != nullptr && stereo_pass != 0xFFFFFFFF) {
        constexpr uintptr_t wrapper_offsets[] = { 0x2630, 0x2638, 0x2640, 0x2648 };
        for (auto off : wrapper_offsets) {
            if (!is_readable_process_range((uintptr_t)view_info + off, 8)) continue;
            const uintptr_t wrapper_ptr = *(uintptr_t*)((uintptr_t)view_info + off);
            if (wrapper_ptr < 0x10000 || (wrapper_ptr & 0x7) != 0) continue;
            if (!is_readable_process_range(wrapper_ptr, 128)) continue;

            // Dump 16 uint64 (128 bytes) of the FRDGTexture-like object.
            uintptr_t qw[16]{};
            for (int i = 0; i < 16; ++i) {
                qw[i] = *(uintptr_t*)(wrapper_ptr + i * 8);
            }
            SPDLOG_WARN(
                "[Subnautica2][SLWPerView][DeepDump] call={} pass={} off=0x{:x} wrapper=0x{:x} [0..7]=0x{:x} 0x{:x} 0x{:x} 0x{:x} 0x{:x} 0x{:x} 0x{:x} 0x{:x}",
                cn + 1, stereo_pass, (unsigned long long)off, wrapper_ptr,
                qw[0], qw[1], qw[2], qw[3], qw[4], qw[5], qw[6], qw[7]);
            SPDLOG_WARN(
                "[Subnautica2][SLWPerView][DeepDump] call={} pass={} off=0x{:x} [8..15]=0x{:x} 0x{:x} 0x{:x} 0x{:x} 0x{:x} 0x{:x} 0x{:x} 0x{:x}",
                cn + 1, stereo_pass, (unsigned long long)off,
                qw[8], qw[9], qw[10], qw[11], qw[12], qw[13], qw[14], qw[15]);

            // 2026-05-17 PHASE 3 ACTIVE: try to look up this wrapper's fog volume
            // SRV in the global D3D12-side map. The shared object at +64 has a
            // GPU VA at its +0x70 (qw[14]). If the SRV for that resource was
            // captured by D3D12Hook::create_shader_resource_view, we can look it
            // up here.
            if (qw[8] >= 0x10000 && is_readable_process_range(qw[8], 128)) {
                const UINT64 candidate_gpu_va = *(UINT64*)(qw[8] + 0x70);
                if (candidate_gpu_va != 0) {
                    sn2_fog_srv_map::Entry srv{};
                    const bool found = sn2_fog_srv_map::lookup_by_gpu_va(candidate_gpu_va, srv);
                    if (found) {
                        SPDLOG_WARN(
                            "[Subnautica2][SLWPerView][SRVLookup] call={} pass={} off=0x{:x} gpuVA=0x{:x} -> resource={:p} cpuHandle=0x{:x} (map size={})",
                            cn + 1, stereo_pass, (unsigned long long)off,
                            candidate_gpu_va, (void*)srv.resource, srv.cpu_handle.ptr,
                            sn2_fog_srv_map::size());
                    } else if (cn < 32 || (cn % 600) == 0) {
                        SPDLOG_WARN(
                            "[Subnautica2][SLWPerView][SRVLookup] call={} pass={} off=0x{:x} gpuVA=0x{:x} NOT FOUND in map (size={})",
                            cn + 1, stereo_pass, (unsigned long long)off,
                            candidate_gpu_va, sn2_fog_srv_map::size());
                    }
                }
            }

            // qw[8] is the SHARED pointer-at-+64 within wrapper. Pair A wrappers
            // share one value, pair B wrappers share another. Likely FRHITexture*
            // or pooled-RT*. Read 128 bytes to find the ID3D12Resource* nested
            // deeper. (Read-only — safe.)
            const uintptr_t shared_ptr = qw[8];
            if (shared_ptr >= 0x10000 && (shared_ptr & 0x7) == 0 &&
                is_readable_process_range(shared_ptr, 128))
            {
                uintptr_t sub[16];
                for (int i = 0; i < 16; ++i) {
                    sub[i] = *(uintptr_t*)(shared_ptr + i * 8);
                }
                // Only log for pair A (off=0x2630) to keep log volume manageable.
                if (off == 0x2630) {
                    SPDLOG_WARN(
                        "[Subnautica2][SLWPerView][SharedDeref128] call={} pass={} qw[8]=0x{:x} [0..7]=0x{:x} 0x{:x} 0x{:x} 0x{:x} 0x{:x} 0x{:x} 0x{:x} 0x{:x}",
                        cn + 1, stereo_pass, shared_ptr,
                        sub[0], sub[1], sub[2], sub[3], sub[4], sub[5], sub[6], sub[7]);
                    SPDLOG_WARN(
                        "[Subnautica2][SLWPerView][SharedDeref128] call={} pass={} qw[8]=0x{:x} [8..15]=0x{:x} 0x{:x} 0x{:x} 0x{:x} 0x{:x} 0x{:x} 0x{:x} 0x{:x}",
                        cn + 1, stereo_pass, shared_ptr,
                        sub[8], sub[9], sub[10], sub[11], sub[12], sub[13], sub[14], sub[15]);
                }
            }
        }
    }

    if (log_now && view_info != nullptr) {
        // Scan a wide range of FViewInfo offsets to find ANY pointer-like value
        // that differs between view 0 and view 1. Target range 0x2400..0x2900
        // covers known fog-related fields. Log only non-null + non-zero entries.
        constexpr uintptr_t scan_start = 0x2400;
        constexpr uintptr_t scan_end = 0x2900;
        char buf[2048] = {0};
        int written = 0;
        for (uintptr_t off = scan_start; off < scan_end && written < (int)sizeof(buf) - 64; off += 8) {
            if (!is_readable_process_range((uintptr_t)view_info + off, 8)) break;
            const uintptr_t v = *(uintptr_t*)((uintptr_t)view_info + off);
            // Only log heap-ish pointers (>0x10000, looks like address)
            if (v >= 0x10000 && v < 0xFFFFFFFFFFFFFFFFull && (v & 0x7) == 0) {
                int n = snprintf(buf + written, sizeof(buf) - written,
                    "[+0x%llx=0x%llx]", (unsigned long long)off, (unsigned long long)v);
                if (n > 0) written += n;
            }
        }

        SPDLOG_WARN(
            "[Subnautica2][SLWPerView] call={} view_info={:p} pass={} idx={} non-zero-ptr-fields-in-0x2400..0x2900: {}",
            cn + 1, view_info, stereo_pass, stereo_index, buf);
    }

    // 2026-05-17 Plan Phase B: descriptor swap. On view 1's call, if user has
    // specified valid source + destination CPU descriptor handles via env vars,
    // CopyDescriptorsSimple from source (view 1's SRV) to destination (view 0's
    // slot the SLW PS reads). Performed BEFORE the original call so the swap
    // is in place when the actual draws happen.
    static thread_local std::array<uint64_t, 8> tl_dst_backup{};
    static thread_local bool tl_descriptor_swap_active = false;
    if (stereo_pass == 2) {
        // Phase B: support BOTH raw cpu_handle env vars AND index-based env vars.
        // Index variant is friendlier: user reads FogSRV log, picks N as source
        // (= view 1 candidate) and M as destination (= view 0 slot bound to
        // right-eye SLW PS).
        uint64_t src_h = subnautica2_fog_swap_src_handle();
        uint64_t dst_h = subnautica2_fog_swap_dst_handle();
        int32_t src_idx = subnautica2_fog_swap_src_index();
        const int32_t dst_idx = subnautica2_fog_swap_dst_index();

        // 2026-05-17 sweep mode: target either SRC or DST (env var controlled).
        // Target 0 = sweep src (find content). Target 1 = sweep dst (find slot).
        int32_t effective_dst_idx = dst_idx;
        if (subnautica2_fog_swap_sweep_enabled()) {
            static const uint64_t start_tick = GetTickCount64();
            const uint64_t now = GetTickCount64();
            const uint32_t interval = subnautica2_fog_swap_sweep_interval_ms();
            const uint32_t min_idx = subnautica2_fog_swap_sweep_min_index();
            const uint32_t max_idx = subnautica2_fog_swap_sweep_max_index();
            const uint32_t target = subnautica2_fog_swap_sweep_target();
            const uint32_t range = (max_idx > min_idx) ? (max_idx - min_idx) : 0;
            if (interval > 0 && range > 0) {
                const int32_t swept = (int32_t)(min_idx + (((now - start_tick) / interval) % range));
                if (target == 1) {
                    effective_dst_idx = swept;
                } else {
                    src_idx = swept;
                }
                static std::atomic<int32_t> last_logged_swept{-2};
                if (last_logged_swept.exchange(swept, std::memory_order_relaxed) != swept) {
                    SPDLOG_WARN(
                        "[Subnautica2][FogDescSwap][Sweep] target={} now src_idx={} dst_idx={} (range={}..{} interval_ms={}) at tick={} -- WATCH RIGHT EYE",
                        target == 1 ? "DST" : "SRC",
                        src_idx, effective_dst_idx, min_idx, max_idx, interval, now - start_tick);
                }
            }
        }

        if (src_idx >= 0 || effective_dst_idx >= 0) {
            sn2_fog_srv_map::Entry e;
            if (src_idx >= 0 && sn2_fog_srv_map::lookup_by_creation_index((uint64_t)src_idx, e)) {
                src_h = (uint64_t)e.cpu_handle.ptr;
            }
            if (effective_dst_idx >= 0 && sn2_fog_srv_map::lookup_by_creation_index((uint64_t)effective_dst_idx, e)) {
                dst_h = (uint64_t)e.cpu_handle.ptr;
            }
        }

        // 2026-05-17 Task #43+#46: principled-source-and-dest override using
        // the bindless slot map. View 0's bindless slot (the one view 1's
        // basepass mistakenly also reads from, per the bug) is found by
        // searching for a bindless slot whose tagged source is view 0's
        // fog UAV. The src is view 1's compute UAV's staging cpu_handle —
        // CopyDescriptorsSimple is cross-heap-safe (staging→bindless).
        if (subnautica2_fog_swap_use_uav_tag()) {
            D3D12_CPU_DESCRIPTOR_HANDLE v0_bindless{};
            ID3D12Resource* v0_res = nullptr;
            const bool dst_ok = sn2_bindless_slot_map::find_slot_for_view(0, v0_bindless, &v0_res);

            sn2_fog_uav_map::Entry v1_uav{};
            sn2_fog_srv_map::Entry v1_srv{};
            uint64_t resolved_src = 0;
            const bool v1_uav_ok = sn2_fog_uav_map::lookup_last_by_view_id(1, v1_uav);
            if (v1_uav_ok && v1_uav.resource != nullptr) {
                // Prefer SRV for the source descriptor — basepass reads view
                // 1's volume as an SRV. If the same resource was tagged as
                // both UAV (compute target) and SRV (basepass input), pick
                // the SRV entry.
                if (sn2_fog_srv_map::lookup_by_resource(v1_uav.resource, v1_srv)) {
                    resolved_src = (uint64_t)v1_srv.cpu_handle.ptr;
                } else {
                    resolved_src = (uint64_t)v1_uav.cpu_handle.ptr;
                }
            }

            if (dst_ok && resolved_src != 0) {
                dst_h = (uint64_t)v0_bindless.ptr;
                src_h = resolved_src;
                static std::atomic<uint64_t> log_count{0};
                const auto lc = log_count.fetch_add(1, std::memory_order_relaxed);
                if (lc < 8 || (lc % 600) == 0) {
                    SPDLOG_WARN(
                        "[Subnautica2][FogDescSwap][UAVTag] bindless_dst=0x{:x} v0_res={:p} staging_src=0x{:x} v1_res={:p} (uav_v1={} srv_v1={} bindless_v0={} bindless_v1={})",
                        dst_h, (void*)v0_res, src_h, (void*)v1_uav.resource,
                        sn2_fog_uav_map::count_by_view(1),
                        sn2_fog_srv_map::count_by_view(1),
                        sn2_bindless_slot_map::count_by_view(0),
                        sn2_bindless_slot_map::count_by_view(1));
                }
            } else {
                static std::atomic<uint64_t> miss_count{0};
                const auto mc = miss_count.fetch_add(1, std::memory_order_relaxed);
                if (mc < 8 || (mc % 600) == 0) {
                    SPDLOG_WARN(
                        "[Subnautica2][FogDescSwap][UAVTag] missing data: dst_ok={} v1_uav_ok={} (uav_v1={} srv_v1={} bindless_v0={} bindless_v1={})",
                        dst_ok, v1_uav_ok,
                        sn2_fog_uav_map::count_by_view(1),
                        sn2_fog_srv_map::count_by_view(1),
                        sn2_bindless_slot_map::count_by_view(0),
                        sn2_bindless_slot_map::count_by_view(1));
                }
            }
        }
        const uint32_t count = subnautica2_fog_swap_count();
        if (src_h != 0 && dst_h != 0 && count > 0 && count <= 4) {
            auto& d3d12 = g_framework->get_d3d12_hook();
            if (d3d12 != nullptr) {
                ID3D12Device* device = d3d12->get_device();
                if (device != nullptr) {
                    D3D12_CPU_DESCRIPTOR_HANDLE dst{(SIZE_T)dst_h};
                    D3D12_CPU_DESCRIPTOR_HANDLE src{(SIZE_T)src_h};
                    device->CopyDescriptorsSimple(count, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                    tl_descriptor_swap_active = true;

                    static std::atomic<uint64_t> swap_count{0};
                    const auto sc = swap_count.fetch_add(1, std::memory_order_relaxed);
                    if (sc < 8 || (sc % 600) == 0) {
                        SPDLOG_WARN(
                            "[Subnautica2][FogDescSwap] call={} stereo_pass={} CopyDescriptorsSimple({}, dst=0x{:x}, src=0x{:x}, CBV_SRV_UAV) src_idx={} dst_idx={}",
                            sc + 1, stereo_pass, count, dst_h, src_h, src_idx, dst_idx);
                    }
                }
            }
        }
    }

    call_original();

    // Restore view 1's original bytes so its other downstream passes still see
    // its native per-view state. SKIPPED in NO_RESTORE mode — see experiment
    // notes; theory is that not restoring avoids the double-ownership crash by
    // committing view 1 to view 0's wrappers for the rest of the frame.
    if (field_copy_fired && view_info != nullptr && tl_view1_backup_size > 0) {
        if (!subnautica2_slw_field_copy_no_restore() &&
            is_writable_process_range((uintptr_t)view_info + tl_view1_backup_off, tl_view1_backup_size))
        {
            memcpy((void*)((uintptr_t)view_info + tl_view1_backup_off),
                   tl_view1_backup.data(),
                   tl_view1_backup_size);
        }
        tl_view1_backup_size = 0;
    }
}

// 2026-05-17 Plan Phase A.1: per-view volumetric fog dispatcher hook.
// sub_142FD6170 is called twice per frame from compute_volumetric_fog, once per
// FViewInfo*. We set thread_local g_sn2_current_fog_view based on the view's
// stereo_pass. CreateShaderResourceView (in D3D12Hook.cpp) reads this to tag
// captured fog SRVs with their owning view.
void FFakeStereoRenderingHook::attempt_hook_subnautica2_volumetric_fog_per_view() {
    if (m_attempted_hook_subnautica2_volumetric_fog_per_view) {
        return;
    }
    m_attempted_hook_subnautica2_volumetric_fog_per_view = true;

    if (!subnautica2_is_current_game()) {
        return;
    }

    if (!subnautica2_enable_volumetric_fog_per_view_hook()) {
        SPDLOG_WARN("[Subnautica2][VolumetricFogPerView] Hook disabled (UEVR_SUBNAUTICA2_ENABLE_VOLUMETRIC_FOG_PER_VIEW_HOOK!=1)");
        return;
    }

    const auto exe_base = (uintptr_t)utility::get_executable();
    const auto target = exe_base + SUBNAUTICA2_VOLUMETRIC_FOG_PER_VIEW_RVA;

    if (exe_base == 0 || !is_executable_process_range(target, 0x20)) {
        SPDLOG_WARN("[Subnautica2][VolumetricFogPerView] Cannot hook sub_142FD6170; bad target {:x}", target);
        return;
    }

    // Verified prologue 2026-05-17:
    // 48 89 5C 24 18  mov [rsp+18], rbx
    // 55             push rbp
    // 56             push rsi
    // 57             push rdi
    // 48 8D AC 24 30 FE FF FF  lea rbp, [rsp-1d0h]
    // 48 81 EC D0 02 00 00     sub rsp, 2D0h
    constexpr uint8_t expected_prologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x18,
        0x55, 0x56, 0x57,
        0x48, 0x8D, 0xAC, 0x24, 0x30, 0xFE, 0xFF, 0xFF,
        0x48, 0x81, 0xEC, 0xD0, 0x02, 0x00, 0x00,
    };

    if (std::memcmp((void*)target, expected_prologue, sizeof(expected_prologue)) != 0) {
        SPDLOG_WARN("[Subnautica2][VolumetricFogPerView] prologue mismatch at {:x}; skipping hook for this build", target);
        return;
    }

    m_subnautica2_volumetric_fog_per_view_hook = safetyhook::create_inline(
        (void*)target,
        &FFakeStereoRenderingHook::subnautica2_volumetric_fog_per_view_hook,
        safetyhook::InlineHook::StartDisabled);

    if (!m_subnautica2_volumetric_fog_per_view_hook) {
        SPDLOG_WARN("[Subnautica2][VolumetricFogPerView] Failed to create hook at {:x}", target);
        return;
    }

    if (auto enable_result = m_subnautica2_volumetric_fog_per_view_hook.enable(); !enable_result.has_value()) {
        SPDLOG_WARN("[Subnautica2][VolumetricFogPerView] Failed to enable hook at {:x}: {}", target, (int)enable_result.error().type);
        return;
    }

    SPDLOG_WARN(
        "[Subnautica2][VolumetricFogPerView] Hooked sub_142FD6170 at {:x}; per-view view-tagging active",
        target);
}

void __cdecl FFakeStereoRenderingHook::subnautica2_volumetric_fog_per_view_hook(
    void* arg1,
    void* view_info,
    void* arg3,
    void* arg4)
{
    auto* hook = g_hook;

    auto call_original = [&]() {
        if (hook != nullptr && hook->m_subnautica2_volumetric_fog_per_view_hook) {
            hook->m_subnautica2_volumetric_fog_per_view_hook.unsafe_call<void>(
                arg1, view_info, arg3, arg4);
        }
    };

    // FViewInfo* is in RDX (arg2 in x64 calling convention) per prior IDA work.
    // Determine view 0 vs view 1 via stereo_pass at offset 0xDD0.
    int view_id = -1;
    if (view_info != nullptr &&
        is_readable_process_range((uintptr_t)view_info + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET, 4))
    {
        const uint32_t pass = *(uint32_t*)((uintptr_t)view_info + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET);
        if (pass == EStereoscopicPass::eSSP_PRIMARY) view_id = 0;
        else if (pass == EStereoscopicPass::eSSP_SECONDARY) view_id = 1;
    }

    // Set the atomic view tag globally. We do NOT restore it: UE5's RDG
    // recording happens on the calling (render) thread, but actual D3D12
    // command-list binding/dispatch is deferred to an RHI worker thread.
    // Restoring atomic=-1 immediately after call_original means the worker
    // thread sees -1 by the time it runs SetComputeRootDescriptorTable.
    // Leaving the atomic at view_id makes the worker thread see the
    // most-recently-recorded view tag, which is correct as long as setups
    // for view 0 and view 1 are not interleaved with executes (UE5's
    // sequential per-view fog loop guarantees this within a frame).
    g_sn2_current_fog_view.store(view_id, std::memory_order_relaxed);

    static std::atomic<uint64_t> call_count{0};
    const auto cn = call_count.fetch_add(1, std::memory_order_relaxed);
    if (cn < 16 || (cn % 600) == 0) {
        SPDLOG_WARN(
            "[Subnautica2][VolumetricFogPerView] call={} view_info={:p} view_id_set={} tid={}",
            cn + 1, view_info, view_id, (uint32_t)GetCurrentThreadId());
    }

    const uint32_t visible_copy_mode = subnautica2_visible_light_infos_copy_mode();
    std::array<uint8_t, 16> original_visible_header{};
    std::vector<uint8_t> original_visible_contents{};
    uintptr_t visible_primary_view = 0;
    uintptr_t visible_primary_data = 0;
    uintptr_t visible_dst_data = 0;
    uint32_t visible_primary_num = 0;
    uint32_t visible_primary_max = 0;
    uint32_t visible_dst_num = 0;
    uint32_t visible_dst_max = 0;
    size_t visible_content_bytes = 0;
    bool restore_visible_header = false;
    bool restore_visible_contents = false;
    bool visible_content_copied = false;

    if (view_id == 1 && visible_copy_mode != 0 && view_info != nullptr) {
        const uintptr_t view = (uintptr_t)view_info;
        if (view >= SUBNAUTICA2_SCENEVIEW_STRIDE) {
            visible_primary_view = view - SUBNAUTICA2_SCENEVIEW_STRIDE;
        }
        if (subnautica2_view_id_from_view(visible_primary_view) == 0 &&
            is_readable_process_range(visible_primary_view + 0x1F88, 16) &&
            is_readable_process_range(view + 0x1F88, 16))
        {
            visible_primary_data = sn2_trace_read_ptr(visible_primary_view + 0x1F88);
            visible_primary_num = sn2_trace_read_u32(visible_primary_view + 0x1F88 + 8);
            visible_primary_max = sn2_trace_read_u32(visible_primary_view + 0x1F88 + 12);
            visible_dst_data = sn2_trace_read_ptr(view + 0x1F88);
            visible_dst_num = sn2_trace_read_u32(view + 0x1F88 + 8);
            visible_dst_max = sn2_trace_read_u32(view + 0x1F88 + 12);

            if (visible_copy_mode == 1 &&
                visible_primary_data != 0 &&
                visible_primary_num != 0 &&
                visible_primary_max >= visible_primary_num &&
                visible_primary_num < 0x10000 &&
                visible_primary_max < 0x10000 &&
                is_writable_process_range(view + 0x1F88, 16))
            {
                std::memcpy(original_visible_header.data(), (void*)(view + 0x1F88), original_visible_header.size());
                std::memcpy((void*)(view + 0x1F88), (void*)(visible_primary_view + 0x1F88), original_visible_header.size());
                restore_visible_header = true;
            } else if ((visible_copy_mode == 2 || visible_copy_mode == 3) &&
                       visible_primary_data != 0 &&
                       visible_dst_data != 0 &&
                       visible_primary_num != 0 &&
                       visible_dst_num != 0 &&
                       visible_primary_num <= visible_primary_max &&
                       visible_dst_num <= visible_dst_max &&
                       visible_primary_num < 0x10000 &&
                       visible_dst_num < 0x10000)
            {
                const uint32_t copy_count = std::min(visible_primary_num, visible_dst_num);
                visible_content_bytes = static_cast<size_t>(copy_count) * 0x68u;
                if (visible_content_bytes > 0 &&
                    visible_content_bytes <= (0x4000u * 0x68u) &&
                    is_readable_process_range(visible_primary_data, visible_content_bytes) &&
                    is_readable_process_range(visible_dst_data, visible_content_bytes) &&
                    is_writable_process_range(visible_dst_data, visible_content_bytes))
                {
                    original_visible_contents.resize(visible_content_bytes);
                    std::memcpy(original_visible_contents.data(), (void*)visible_dst_data, visible_content_bytes);
                    std::memcpy((void*)visible_dst_data, (void*)visible_primary_data, visible_content_bytes);
                    visible_content_copied = true;
                    restore_visible_contents = visible_copy_mode == 2;
                }
            }
        }

        if (restore_visible_header || visible_content_copied || visible_copy_mode != 0) {
            const bool log_visible_copy = cn < 64 || (cn % 600) == 0;
            if (log_visible_copy) {
            SPDLOG_WARN(
                "[Subnautica2][VolumetricFogPerView][VisibleLightInfos] call={} mode={} header_copy={} content_copy={} restore_content={} primary_view=0x{:x} primary=0x{:x}/{}/{} dst=0x{:x}/{}/{} bytes={}",
                cn + 1,
                visible_copy_mode,
                restore_visible_header ? 1 : 0,
                visible_content_copied ? 1 : 0,
                restore_visible_contents ? 1 : 0,
                visible_primary_view,
                visible_primary_data,
                visible_primary_num,
                visible_primary_max,
                visible_dst_data,
                visible_dst_num,
                visible_dst_max,
                visible_content_bytes);
            }
        }
    }

    {
        utility::ScopeGuard restore_visible_lights{[&]() {
            const uintptr_t view = (uintptr_t)view_info;
            if (restore_visible_contents &&
                visible_dst_data != 0 &&
                original_visible_contents.size() == visible_content_bytes &&
                is_writable_process_range(visible_dst_data, visible_content_bytes))
            {
                std::memcpy((void*)visible_dst_data, original_visible_contents.data(), visible_content_bytes);
            }
            if (restore_visible_header &&
                view != 0 &&
                is_writable_process_range(view + 0x1F88, original_visible_header.size()))
            {
                std::memcpy((void*)(view + 0x1F88), original_visible_header.data(), original_visible_header.size());
            }
        }};

        call_original();
    }

    // 2026-05-17 Path A diagnostic: post-compute, dump view+0x2658 (and +0x2650
    // as a cross-check, since prior memory was split on which offset holds
    // IntegratedLightScattering's FRDGTexture pointer). Goal: confirm whether
    // view 1's wrapper pointer is populated by the time FixV5 runs for view 1.
    // If view 1's +0x2658 is non-null here AND FixV5 still reads null, then
    // the call order is reversed (SetupFog before compute) and we need to
    // stash this pointer for FixV5 to pick up.
    if (view_info != nullptr && view_id != -1) {
        const uintptr_t v = (uintptr_t)view_info;
        const uintptr_t p_2650 = is_readable_process_range(v + 0x2650, 8) ? *(uintptr_t*)(v + 0x2650) : 0;
        const uintptr_t p_2658 = is_readable_process_range(v + 0x2658, 8) ? *(uintptr_t*)(v + 0x2658) : 0;
        const uintptr_t p_2660 = is_readable_process_range(v + 0x2660, 8) ? *(uintptr_t*)(v + 0x2660) : 0;
        static std::atomic<uint64_t> post_n{0};
        const auto pn = post_n.fetch_add(1, std::memory_order_relaxed);
        if (pn < 16 || (pn % 600) == 0) {
            SPDLOG_WARN(
                "[Subnautica2][VolumetricFogPerView][POST] call={} view_id={} view=0x{:x} +2650=0x{:x} +2658=0x{:x} +2660=0x{:x}",
                pn + 1, view_id, v, p_2650, p_2658, p_2660);
        }

        // 2026-05-17 Path A Step 3: if this is view 1 and +0x2658 is now
        // populated (per-view compute has wired up view 1's RDG wrapper),
        // overwrite UEVR's buffer at offset 0x120 with view 1's pointer.
        // This fixes the case where FixV5 ran with view 1's +0x2658 == null
        // (because compute hadn't recorded yet) and fell back to v0_tex.
        if (view_id == 1 && p_2658 != 0 &&
            sn2_fog_path_a::enabled_env() && sn2_fog_path_a::buffer_ready())
        {
            // We can't write at an offset with the current API; emit a
            // diagnostic so we can decide whether to add an offset-write
            // helper. For Step 3 v1, the FixV5 path already writes the whole
            // buffer post-FixV5 — its target_tex falls back to v0_tex when
            // FixV5 sees v1_tex=null. If POST hook sees v1_2658!=0 but FixV5
            // saw v1_2658==0, that's evidence of call-ordering mismatch.
            static std::atomic<uint64_t> post_v1_hits{0};
            const auto h = post_v1_hits.fetch_add(1, std::memory_order_relaxed);
            if (h < 8 || (h % 600) == 0) {
                SPDLOG_WARN(
                    "[Subnautica2][VolumetricFogPerView][POST][PathA] view1 v1_2658 NOW=0x{:x} hits={} (compare FixV5 v1_2658 in earlier log)",
                    p_2658, h + 1);
            }
        }
    }

    // 2026-05-17 Task #47/#48 — wrapper walk via FViewInfo+0x2650 (the
    // IntegratedLightScattering FRDGTexture pointer, identified via IDA +
    // runtime FViewPtrDeref probe; the prior +0x2658 in memory was off by
    // 8). FRDGTexture object at this pointer is ~200 bytes and stores
    // FRHITexture* somewhere inside; that FRHITexture eventually owns the
    // underlying ID3D12Resource* that's also in our staging-heap maps.
    //
    // Strategy: from view+0x2650 we have FRDGTexture*. Walk every 8-byte
    // slot inside the first 512 bytes of that object (and one level of
    // indirection) and for each pointer, probe sn2_fog_uav_map/
    // sn2_fog_srv_map for a matching ID3D12Resource. When found, tag with
    // this view's view_id.
    // 2026-05-17 — wrapper-walk DISABLED. The 3-level pointer scan (512 * 256
    // * 128 / 8 = ~524k pointer lookups per fog hook call, × ~7 calls/sec)
    // stalled the render thread enough to break the OpenXR frame loop
    // ("Recovering focused stale frame loop"), causing the Meta XR Simulator
    // to show a black screen. Removing the walk because it found zero
    // matches anyway — the FRDGTexture → ID3D12Resource chain is deeper or
    // encoded; see project_sn2_wrapper_culled_light_grid_correction_2026-05-17
    // memory for next-step plan (decompile vfunc at IDA 0x143112100).
}

void FFakeStereoRenderingHook::subnautica2_single_layer_water_hook(
    void* scene_renderer,
    FRDGBuilder* graph_builder,
    void* views,
    void* scene_textures,
    void* single_layer_water_prepass_result,
    bool should_render_volumetric_cloud,
    void* scene_without_water_textures,
    void* lumen_frame_temporaries,
    bool camera_underwater)
{
    auto* hook = g_hook;

    if (hook == nullptr || !hook->m_subnautica2_single_layer_water_hook) {
        return;
    }

    std::array<Subnautica2RuntimeViewRectPatch, 2> view_rect_patches{};
    subnautica2_patch_runtime_view_rects("RenderSingleLayerWater", views, view_rect_patches);

    utility::ScopeGuard restore_view_rects{[&]() {
        subnautica2_restore_runtime_view_rects(view_rect_patches);
    }};

    Subnautica2PrimaryViewIndexPatch primary_index_patch{};
    subnautica2_patch_secondary_primary_view_index("RenderSingleLayerWater", views, primary_index_patch);

    utility::ScopeGuard restore_primary_index{[&]() {
        subnautica2_restore_primary_view_index(primary_index_patch);
    }};

    hook->m_subnautica2_single_layer_water_hook.unsafe_call<void>(
        scene_renderer,
        graph_builder,
        views,
        scene_textures,
        single_layer_water_prepass_result,
        should_render_volumetric_cloud,
        scene_without_water_textures,
        lumen_frame_temporaries,
        camera_underwater);

    auto& vr = VR::get();
    if (!subnautica2_is_current_game() ||
        !subnautica2_enable_single_layer_water_secondary_replay() ||
        vr == nullptr ||
        !vr->is_hmd_active() ||
        !vr->is_native_stereo_fix_enabled() ||
        vr->is_native_stereo_fix_same_pass_enabled() ||
        vr->is_using_afr())
    {
        return;
    }

    Subnautica2ArrayViewPatch secondary_view_patch{};
    if (!subnautica2_patch_array_view_to_secondary(views, secondary_view_patch)) {
        return;
    }

    utility::ScopeGuard restore_secondary_view{[&]() {
        subnautica2_restore_array_view_patch(secondary_view_patch);
    }};

    static std::atomic<uint64_t> replay_logged_calls{0};
    const auto replay_count = replay_logged_calls.fetch_add(1, std::memory_order_relaxed);
    if (replay_count < 32 || (replay_count % 600) == 0) {
        SPDLOG_INFO(
            "[Subnautica2][SingleLayerWater] Replaying water pass for secondary view only call={} original_count={} secondary_view={:x}",
            replay_count + 1,
            secondary_view_patch.original_count,
            secondary_view_patch.replay_data);
    }

    hook->m_subnautica2_single_layer_water_hook.unsafe_call<void>(
        scene_renderer,
        graph_builder,
        views,
        scene_textures,
        single_layer_water_prepass_result,
        should_render_volumetric_cloud,
        scene_without_water_textures,
        lumen_frame_temporaries,
        camera_underwater);
}

void FFakeStereoRenderingHook::subnautica2_single_layer_water_inner_hook(
    void* scene_renderer,
    FRDGBuilder* graph_builder,
    void* views,
    void* scene_textures,
    void* scene_without_water_textures,
    void* single_layer_water_prepass_result)
{
    auto* hook = g_hook;

    if (hook == nullptr || !hook->m_subnautica2_single_layer_water_inner_hook) {
        return;
    }

    auto call_original = [&]() {
        hook->m_subnautica2_single_layer_water_inner_hook.unsafe_call<void>(
            scene_renderer,
            graph_builder,
            views,
            scene_textures,
            scene_without_water_textures,
            single_layer_water_prepass_result);
    };

    auto& vr = VR::get();

    if (!subnautica2_is_current_game() ||
        subnautica2_disable_single_layer_water_pass_fix() ||
        vr == nullptr ||
        !vr->is_hmd_active() ||
        !vr->is_native_stereo_fix_enabled() ||
        vr->is_native_stereo_fix_same_pass_enabled() ||
        vr->is_using_afr() ||
        views == nullptr)
    {
        call_original();
        return;
    }

    const auto views_addr = (uintptr_t)views;

    if (!is_readable_process_range(views_addr, sizeof(uintptr_t) + sizeof(int32_t))) {
        call_original();
        return;
    }

    auto* const views_data = *(uint8_t**)views_addr;
    const auto views_count = *(int32_t*)(views_addr + sizeof(uintptr_t));

    if (views_data == nullptr ||
        views_count < 2 ||
        views_count > 8 ||
        !is_writable_process_range(
            (uintptr_t)views_data,
            SUBNAUTICA2_SCENEVIEW_STRIDE * 2))
    {
        call_original();
        return;
    }

    auto* const primary_view = views_data;
    auto* const secondary_view = views_data + SUBNAUTICA2_SCENEVIEW_STRIDE;

    auto& primary_pass = *(uintptr_t*)(primary_view + SUBNAUTICA2_SCENEVIEW_SINGLE_LAYER_WATER_PASS_OFFSET);
    auto& secondary_pass = *(uintptr_t*)(secondary_view + SUBNAUTICA2_SCENEVIEW_SINGLE_LAYER_WATER_PASS_OFFSET);
    auto& primary_flags = *(uint8_t*)(primary_view + SUBNAUTICA2_SCENEVIEW_VISIBILITY_FLAGS_OFFSET);
    auto& secondary_flags = *(uint8_t*)(secondary_view + SUBNAUTICA2_SCENEVIEW_VISIBILITY_FLAGS_OFFSET);

    const auto original_secondary_pass = secondary_pass;
    const auto original_primary_flags = primary_flags;
    const auto original_secondary_flags = secondary_flags;
    const auto primary_stereo_pass = *(uint32_t*)(primary_view + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET);
    const auto secondary_stereo_pass = *(uint32_t*)(secondary_view + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET);

    bool patched = false;

    if (primary_stereo_pass == EStereoscopicPass::eSSP_PRIMARY &&
        secondary_stereo_pass == EStereoscopicPass::eSSP_SECONDARY)
    {
        primary_flags &= (uint8_t)~SUBNAUTICA2_SCENEVIEW_HAS_NO_VISIBLE_PRIMITIVE_MASK;
        secondary_flags &= (uint8_t)~SUBNAUTICA2_SCENEVIEW_HAS_NO_VISIBLE_PRIMITIVE_MASK;

        // 2026-05-16: AGGRESSIVE FIX — empirical observation: secondary_pass IS
        // populated (different pointer from primary), but its CONTENTS produce
        // the wrong per-view binding (bug confirmed via RenderDoc Thread A:
        // right-eye basepass binds view 0's fog volume).
        // The user-confirmed working workaround is `ShowFlag.UWEWaterLighting 0`
        // (disables this pass entirely for both eyes). As a code fix we instead
        // FORCE secondary to use primary's FSingleLayerWaterPass — both eyes
        // will then sample the SAME pass data (view 0's correct setup) rather
        // than two different setups where view 1's is wrong.
        if (primary_pass != 0) {
            secondary_pass = primary_pass;
        }

        patched =
            primary_flags != original_primary_flags ||
            secondary_flags != original_secondary_flags ||
            secondary_pass != original_secondary_pass;
    }

    static std::atomic<uint64_t> logged_calls{0};
    const auto count = logged_calls.fetch_add(1, std::memory_order_relaxed);
    const auto log_max = sn2_single_layer_water_inner_log_max();

    if (count < log_max) {
        // Use SPDLOG_WARN so spdlog::flush_on(err) doesn't apply — but warn is
        // flushed more aggressively than info per the default sink behavior.
        // (Also matches the [warning] level we see firing on other diagnostic logs.)
        SPDLOG_WARN(
            "[Subnautica2][SingleLayerWaterInner] call={} count={} patched={} primary(pass={}, water_pass={:x}, flags=0x{:02x}->0x{:02x}, singlepass={}, rect={} {} {} {}) secondary(pass={}, water_pass={:x}->{:x}, flags=0x{:02x}->0x{:02x}, singlepass={}, rect={} {} {} {})",
            count + 1,
            views_count,
            patched,
            primary_stereo_pass,
            primary_pass,
            original_primary_flags,
            primary_flags,
            primary_view[SUBNAUTICA2_SCENEVIEW_SINGLE_PASS_STEREO_OFFSET],
            ((sdk::FSceneViewInitOptionsUE5*)(primary_view + SUBNAUTICA2_SCENEVIEW_INIT_OPTIONS_OFFSET))->view_rect[0],
            ((sdk::FSceneViewInitOptionsUE5*)(primary_view + SUBNAUTICA2_SCENEVIEW_INIT_OPTIONS_OFFSET))->view_rect[1],
            ((sdk::FSceneViewInitOptionsUE5*)(primary_view + SUBNAUTICA2_SCENEVIEW_INIT_OPTIONS_OFFSET))->view_rect[2],
            ((sdk::FSceneViewInitOptionsUE5*)(primary_view + SUBNAUTICA2_SCENEVIEW_INIT_OPTIONS_OFFSET))->view_rect[3],
            secondary_stereo_pass,
            original_secondary_pass,
            secondary_pass,
            original_secondary_flags,
            secondary_flags,
            secondary_view[SUBNAUTICA2_SCENEVIEW_SINGLE_PASS_STEREO_OFFSET],
            ((sdk::FSceneViewInitOptionsUE5*)(secondary_view + SUBNAUTICA2_SCENEVIEW_INIT_OPTIONS_OFFSET))->view_rect[0],
            ((sdk::FSceneViewInitOptionsUE5*)(secondary_view + SUBNAUTICA2_SCENEVIEW_INIT_OPTIONS_OFFSET))->view_rect[1],
            ((sdk::FSceneViewInitOptionsUE5*)(secondary_view + SUBNAUTICA2_SCENEVIEW_INIT_OPTIONS_OFFSET))->view_rect[2],
            ((sdk::FSceneViewInitOptionsUE5*)(secondary_view + SUBNAUTICA2_SCENEVIEW_INIT_OPTIONS_OFFSET))->view_rect[3]);
        if (patched && count < 24) {
            spdlog::default_logger()->flush();  // see patches in log immediately during early debugging
        }
    }

    utility::ScopeGuard restore_view_fields{[&]() {
        secondary_pass = original_secondary_pass;
        primary_flags = original_primary_flags;
        secondary_flags = original_secondary_flags;
    }};

    call_original();
}

void FFakeStereoRenderingHook::subnautica2_compute_volumetric_fog_hook(
    void* scene_renderer,
    FRDGBuilder* graph_builder,
    void* scene_textures)
{
    auto* hook = g_hook;

    if (hook == nullptr || !hook->m_subnautica2_compute_volumetric_fog_hook) {
        return;
    }

    // (RetAddr walk removed — caused render-thread timeout)

    auto call_original = [&]() {
        hook->m_subnautica2_compute_volumetric_fog_hook.unsafe_call<void>(scene_renderer, graph_builder, scene_textures);
    };

    thread_local bool inside_hook = false;
    if (inside_hook) {
        call_original();
        return;
    }

    inside_hook = true;
    utility::ScopeGuard inside_guard{[]() {
        inside_hook = false;
    }};

    bool called_original = false;

    auto fallback_original = [&]() {
        called_original = true;
        call_original();
    };

    auto& vr = VR::get();

    const bool state_copy_enabled = subnautica2_enable_volumetric_fog_state_copy();
    const bool double_dispatch_disabled = subnautica2_disable_volumetric_fog_view_loop_fix();
    const bool rect_widen_enabled = subnautica2_enable_fog_rect_widen();
    const bool matrix_swap_enabled_gate = subnautica2_enable_fog_view1_matrix_swap();

    if (!subnautica2_is_current_game() ||
        vr == nullptr ||
        !vr->is_hmd_active() ||
        !vr->is_native_stereo_fix_enabled() ||
        vr->is_native_stereo_fix_same_pass_enabled() ||
        vr->is_using_afr() ||
        (double_dispatch_disabled && !state_copy_enabled && !rect_widen_enabled && !matrix_swap_enabled_gate))
    {
        fallback_original();
        return;
    }

    const auto renderer = (uintptr_t)scene_renderer;
    const auto header_addr = renderer + SUBNAUTICA2_SCENERENDERER_VIEWS_DATA_OFFSET;

    if (scene_renderer == nullptr ||
        graph_builder == nullptr ||
        !is_writable_process_range(header_addr, sizeof(uintptr_t) + sizeof(int32_t) * 2))
    {
        fallback_original();
        return;
    }

    auto* views_data_ptr = (uint8_t**)(renderer + SUBNAUTICA2_SCENERENDERER_VIEWS_DATA_OFFSET);
    auto* views_count_ptr = (int32_t*)(renderer + SUBNAUTICA2_SCENERENDERER_VIEWS_COUNT_OFFSET);
    auto* views_max_ptr = (int32_t*)(renderer + SUBNAUTICA2_SCENERENDERER_VIEWS_MAX_OFFSET);

    auto* const original_views_data = *views_data_ptr;
    const auto original_views_count = *views_count_ptr;
    const auto original_views_max = *views_max_ptr;

    if (original_views_data == nullptr ||
        original_views_count < 2 ||
        original_views_count > 8 ||
        original_views_max < original_views_count ||
        !is_readable_process_range(
            (uintptr_t)original_views_data,
            SUBNAUTICA2_SCENEVIEW_STRIDE + SUBNAUTICA2_SCENEVIEW_STEREO_ASPECTS_FLAGS_OFFSET + sizeof(uint16_t)))
    {
        fallback_original();
        return;
    }

    auto* const primary_view = original_views_data;
    auto* const secondary_view = original_views_data + SUBNAUTICA2_SCENEVIEW_STRIDE;
    const auto primary_pass = *(uint32_t*)(primary_view + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET);
    const auto secondary_pass = *(uint32_t*)(secondary_view + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET);
    const auto primary_index = *(uint32_t*)(primary_view + SUBNAUTICA2_SCENEVIEW_STEREO_VIEW_INDEX_OFFSET);
    const auto secondary_index = *(uint32_t*)(secondary_view + SUBNAUTICA2_SCENEVIEW_STEREO_VIEW_INDEX_OFFSET);

    if (primary_pass != EStereoscopicPass::eSSP_PRIMARY ||
        secondary_pass != EStereoscopicPass::eSSP_SECONDARY)
    {
        SPDLOG_INFO_EVERY_N_SEC(
            5,
            "[Subnautica2][VolumetricFog] Skipping second fog build; unexpected stereo passes primary={} secondary={} indices={}/{} count={}",
            primary_pass,
            secondary_pass,
            primary_index,
            secondary_index,
            original_views_count);
        fallback_original();
        return;
    }

    // 2026-05-15 23:38 FIX-V3 (DEPRECATED, default-off as of 2026-05-16):
    // widening view 0's runtime_view_rect to cover view 1's screen-X range
    // before compute. Empirically caused left-eye flicker (view 0 cbuffer
    // pollution). Kept behind UEVR_SUBNAUTICA2_ENABLE_FOG_RECT_WIDEN=1 only.
    int32_t saved_v0_runtime_rect[4]{};
    bool widened_v0_rect = false;

    if (rect_widen_enabled) {
        auto* const v0_rt = (int32_t*)(primary_view + SUBNAUTICA2_SCENEVIEW_RUNTIME_VIEW_RECT_OFFSET);
        auto* const v1_rt = (int32_t*)(secondary_view + SUBNAUTICA2_SCENEVIEW_RUNTIME_VIEW_RECT_OFFSET);

        if (is_writable_process_range((uintptr_t)v0_rt, 16) &&
            is_readable_process_range((uintptr_t)v1_rt, 16))
        {
            const int32_t v0_x0 = v0_rt[0], v0_y0 = v0_rt[1], v0_x1 = v0_rt[2], v0_y1 = v0_rt[3];
            const int32_t v1_x0 = v1_rt[0], v1_y0 = v1_rt[1], v1_x1 = v1_rt[2], v1_y1 = v1_rt[3];

            if (v1_x0 > v0_x1 && v1_x1 > v0_x1) {
                memcpy(saved_v0_runtime_rect, v0_rt, sizeof(saved_v0_runtime_rect));
                v0_rt[2] = v1_x1;
                widened_v0_rect = true;

                static std::atomic<uint64_t> widen_count{0};
                const auto wn = widen_count.fetch_add(1, std::memory_order_relaxed);
                if (wn < 4 || (wn % 600) == 0) {
                    SPDLOG_WARN(
                        "[Subnautica2][VolumetricFog][RectWiden] call={} v0_rt=({},{},{},{}) -> ({},{},{},{}) | v1_rt=({},{},{},{}) (unchanged)",
                        wn + 1, v0_x0, v0_y0, v0_x1, v0_y1,
                        v0_rt[0], v0_rt[1], v0_rt[2], v0_rt[3],
                        v1_x0, v1_y0, v1_x1, v1_y1);
                }
            }
        }
    }

    // 2026-05-16 NEW STRATEGY: view-1 matrix swap.
    // Copy view 0's FViewMatrices block to view 1 BEFORE compute_volumetric_fog
    // executes. View 1's compute iteration then builds its fog volume using
    // view 0's projection/view matrices — so the volume is filled with view 0's
    // perspective for BOTH eyes' UAV writes. Restore view 1's original matrices
    // immediately after fallback_original so view 1's basepass uses its own
    // matrices to SAMPLE the volume.
    //
    // Offset / size are env-var-tunable; defaults (0x3B0 / 0x200) come from
    // the memory note that sub_142FD6170 reads `*((_OWORD*)a2 + 59)` and walks
    // forward through ~8 matrices.
    const bool matrix_swap_enabled = subnautica2_enable_fog_view1_matrix_swap();
    const uint32_t matrix_swap_offset = subnautica2_fog_view1_matrix_swap_offset();
    const uint32_t matrix_swap_size = subnautica2_fog_view1_matrix_swap_size();
    std::vector<uint8_t> saved_v1_matrix_block;
    bool swapped_v1_matrices = false;

    if (matrix_swap_enabled &&
        matrix_swap_size > 0 &&
        matrix_swap_size <= 0x1000 &&
        is_readable_process_range((uintptr_t)primary_view + matrix_swap_offset, matrix_swap_size) &&
        is_writable_process_range((uintptr_t)secondary_view + matrix_swap_offset, matrix_swap_size))
    {
        saved_v1_matrix_block.resize(matrix_swap_size);
        memcpy(saved_v1_matrix_block.data(),
               (const void*)(secondary_view + matrix_swap_offset),
               matrix_swap_size);
        memcpy((void*)(secondary_view + matrix_swap_offset),
               (const void*)(primary_view + matrix_swap_offset),
               matrix_swap_size);
        swapped_v1_matrices = true;

        static std::atomic<uint64_t> swap_count{0};
        const auto sn = swap_count.fetch_add(1, std::memory_order_relaxed);
        if (sn < 8 || (sn % 600) == 0) {
            const float* v0m = (const float*)(primary_view + matrix_swap_offset);
            const float* v1m_orig = (const float*)saved_v1_matrix_block.data();
            SPDLOG_WARN(
                "[Subnautica2][VolumetricFog][MatrixSwap] call={} off=0x{:x} size={} | v0[0..3]=({:.4f},{:.4f},{:.4f},{:.4f}) v1_was=({:.4f},{:.4f},{:.4f},{:.4f})",
                sn + 1, matrix_swap_offset, matrix_swap_size,
                v0m[0], v0m[1], v0m[2], v0m[3],
                v1m_orig[0], v1m_orig[1], v1m_orig[2], v1m_orig[3]);
        }
    }

    fallback_original();

    // Restore view 1's matrices IMMEDIATELY after compute so its basepass uses
    // its own (correct) matrices to sample the fog volume.
    if (swapped_v1_matrices) {
        memcpy((void*)(secondary_view + matrix_swap_offset),
               saved_v1_matrix_block.data(),
               matrix_swap_size);
    }

    // Restore view 0's runtime_view_rect IMMEDIATELY after compute so downstream
    // passes (basepass etc) see the original per-view rect.
    if (widened_v0_rect) {
        auto* const v0_rt = (int32_t*)(primary_view + SUBNAUTICA2_SCENEVIEW_RUNTIME_VIEW_RECT_OFFSET);
        memcpy(v0_rt, saved_v0_runtime_rect, sizeof(saved_v0_runtime_rect));
    }

    // 2026-05-15 22:50 TARGETED FIX (per user's deep trace):
    // view+0x2658 IS the IntegratedLightScattering 3D texture pointer.
    // sub_142FBED20 (compute_volumetric_fog) STORES view 0's texture there at
    // 0x142FBFAAB; for view 1, that field stays null. SetupFogUniformParameters
    // (at 0x1426FD275) reads view+0x2658 and falls back to a default BLACK
    // texture when null -> view 1's basepass samples a black texture -> no fog.
    // Fix: copy view 0's view+0x2658 to view 1's view+0x2658 after compute.
    if (state_copy_enabled) {
        const uintptr_t view0_addr2 = (uintptr_t)primary_view;
        const uintptr_t view1_addr2 = (uintptr_t)secondary_view;
        if (is_readable_process_range(view0_addr2 + 0x2658, 8) &&
            is_writable_process_range(view1_addr2 + 0x2658, 8))
        {
            const uintptr_t v0_lightscat = *(uintptr_t*)(view0_addr2 + 0x2658);
            const uintptr_t v1_lightscat_was = *(uintptr_t*)(view1_addr2 + 0x2658);

            // 2026-05-16 FIX-V2: capture view 1's NATURAL pointer (the right-eye
            // fog texture) BEFORE the propagation below overwrites it. Stored
            // in a global for setup_volumetric_fog_ub_hook to use later.
            if (v1_lightscat_was != 0 && v1_lightscat_was != v0_lightscat) {
                g_subnautica2_view1_natural_lightscat.store(v1_lightscat_was, std::memory_order_relaxed);
                g_subnautica2_view0_lightscat.store(v0_lightscat, std::memory_order_relaxed);
                static std::atomic<uint64_t> capture_calls{0};
                const auto cn = capture_calls.fetch_add(1, std::memory_order_relaxed);
                if (cn < 4 || (cn % 600) == 0) {
                    SPDLOG_WARN(
                        "[Subnautica2][FixV2] CAPTURED view1_natural_lightscat=0x{:x} view0_lightscat=0x{:x} (call={})",
                        v1_lightscat_was, v0_lightscat, cn + 1);
                }
            }

            if (v0_lightscat != 0 && v0_lightscat != v1_lightscat_was) {
                *(uintptr_t*)(view1_addr2 + 0x2658) = v0_lightscat;

                static std::atomic<uint64_t> ls_copy_calls{0};
                const auto cnt2 = ls_copy_calls.fetch_add(1, std::memory_order_relaxed);
                if (cnt2 < 8 || (cnt2 % 300) == 0) {
                    SPDLOG_WARN(
                        "[Subnautica2][VolumetricFog][LightScat2658] call={} view0+0x2658=0x{:x} (was view1=0x{:x}) -> copied",
                        cnt2 + 1, v0_lightscat, v1_lightscat_was);
                }
            }
        }
    }

    // 2026-05-15 NEW PATH: state-copy fix. After compute_volumetric_fog ran
    // for view 0 (filling view 0's FSceneViewState's IntegratedLightScattering
    // texture handle at state+0x1E00 per user's offline trace), copy that
    // region from view 0's state to view 1's state. View 1's basepass walker
    // will then find a valid UB pointer instead of garbage at params+0x2C.
    if (state_copy_enabled) {
        const uintptr_t view0_addr = (uintptr_t)primary_view;
        const uintptr_t view1_addr = (uintptr_t)secondary_view;
        if (is_readable_process_range(view0_addr + 0x1C10, 8) &&
            is_readable_process_range(view1_addr + 0x1C10, 8))
        {
            const uintptr_t state0 = *(uintptr_t*)(view0_addr + 0x1C10);
            const uintptr_t state1 = *(uintptr_t*)(view1_addr + 0x1C10);
            if (state0 != 0 && state1 != 0 && state0 != state1) {
                const uint32_t copy_size = subnautica2_volumetric_fog_state_copy_size();
                if (is_readable_process_range(state0 + 0x1E00, copy_size) &&
                    is_writable_process_range(state1 + 0x1E00, copy_size))
                {
                    static std::atomic<uint64_t> copy_calls{0};
                    const auto cnt = copy_calls.fetch_add(1, std::memory_order_relaxed);

                    uintptr_t before[4]{};
                    if (copy_size >= 8 && is_readable_process_range(state1 + 0x1E00, 32)) {
                        memcpy(before, (const void*)(state1 + 0x1E00), 32);
                    }

                    memcpy((void*)(state1 + 0x1E00), (const void*)(state0 + 0x1E00), copy_size);

                    if (cnt < 8 || (cnt % 300) == 0) {
                        uintptr_t v0_p = *(uintptr_t*)(state0 + 0x1E00);
                        uintptr_t v1_p_now = *(uintptr_t*)(state1 + 0x1E00);
                        SPDLOG_WARN(
                            "[Subnautica2][VolumetricFog][StateCopy] call={} copied {} bytes state0+0x1E00=0x{:x}->state1+0x1E00 v0_ptr=0x{:x} v1_was=0x{:x} v1_now=0x{:x}",
                            cnt + 1, copy_size, state0 + 0x1E00,
                            v0_p, before[0], v1_p_now);
                    }
                } else {
                    SPDLOG_INFO_EVERY_N_SEC(
                        5,
                        "[Subnautica2][VolumetricFog][StateCopy] Skipping; state ranges not r/w state0+0x1E00=0x{:x} state1+0x1E00=0x{:x} size={}",
                        state0 + 0x1E00, state1 + 0x1E00, copy_size);
                }
            }
        }
    }

    if (double_dispatch_disabled) {
        // State-copy path: don't double-dispatch (avoids slot-0 overwrite).
        (void)called_original;
        return;
    }

    if (*views_data_ptr != original_views_data ||
        *views_count_ptr != original_views_count ||
        *views_max_ptr != original_views_max)
    {
        SPDLOG_INFO_EVERY_N_SEC(
            5,
            "[Subnautica2][VolumetricFog] Original call changed renderer views header; skipping second fog build");
        return;
    }

    const bool force_view_index = (bool)hook->m_subnautica2_compute_volumetric_fog_view_index_hook;

    *views_data_ptr = secondary_view;
    *views_count_ptr = 1;
    *views_max_ptr = 1;

    utility::ScopeGuard restore_views{[&]() {
        *views_data_ptr = original_views_data;
        *views_count_ptr = original_views_count;
        *views_max_ptr = original_views_max;
    }};

    if (force_view_index) {
        g_subnautica2_force_volumetric_fog_view_index_one = true;
        utility::ScopeGuard force_view_index_guard{[]() {
            g_subnautica2_force_volumetric_fog_view_index_one = false;
        }};

        call_original();
    } else {
        call_original();
    }

    static std::atomic<uint64_t> logged_calls{0};
    const auto count = logged_calls.fetch_add(1, std::memory_order_relaxed);
    if (count < 8 || (count % 300) == 0) {
        SPDLOG_INFO(
            "[Subnautica2][VolumetricFog] Ran second fog build for secondary view pass={} index={} original_count={} view_index_forced={} call={}",
            secondary_pass,
            secondary_index,
            original_views_count,
            force_view_index ? 1 : 0,
            count + 1);
    }

    (void)called_original;
}

void FFakeStereoRenderingHook::subnautica2_init_volumetric_render_target_hook(
    FRDGBuilder* graph_builder,
    void* views,
    void* scene_textures)
{
    auto* hook = g_hook;

    if (hook == nullptr || !hook->m_subnautica2_init_volumetric_render_target_hook) {
        return;
    }

    auto call_original = [&]() {
        hook->m_subnautica2_init_volumetric_render_target_hook.unsafe_call<void>(
            graph_builder,
            views,
            scene_textures);
    };

    thread_local bool inside_hook = false;
    if (inside_hook) {
        call_original();
        return;
    }

    inside_hook = true;
    utility::ScopeGuard inside_guard{[]() {
        inside_hook = false;
    }};

    auto& vr = VR::get();
    const bool log_native_stereo =
        subnautica2_is_current_game() &&
        vr != nullptr &&
        vr->is_hmd_active() &&
        vr->is_native_stereo_fix_enabled() &&
        !vr->is_native_stereo_fix_same_pass_enabled() &&
        !vr->is_using_afr();

    if (log_native_stereo) {
        subnautica2_log_compose_volumetric_render_target("VRTInitBefore", views, false, nullptr);
    }

    call_original();

    if (log_native_stereo) {
        subnautica2_log_compose_volumetric_render_target("VRTInitAfter", views, false, nullptr);
    }

    // Patch L: double-dispatch init_volumetric_render_target for view 1 so
    // its VolumetricFogResources get allocated. Without this, view 1's RTs
    // are null when compute/reconstruct/compose try to write/read them.
    if (log_native_stereo &&
        views != nullptr &&
        !subnautica2_disable_volumetric_fog_view_loop_fix() &&
        !subnautica2_disable_volumetric_rt_replay())
    {
        const uintptr_t views_addr = (uintptr_t)views;
        if (is_readable_process_range(views_addr, sizeof(uintptr_t) + sizeof(int32_t))) {
            auto** views_data_ptr2 = (uint8_t**)views_addr;
            auto* views_count_ptr2 = (int32_t*)(views_addr + sizeof(uintptr_t));
            auto* const original_views_data2 = *views_data_ptr2;
            const auto original_views_count2 = *views_count_ptr2;

            if (original_views_data2 != nullptr && original_views_count2 >= 2 &&
                is_readable_process_range((uintptr_t)original_views_data2, SUBNAUTICA2_SCENEVIEW_STRIDE * 2))
            {
                auto* const secondary_view2 = original_views_data2 + SUBNAUTICA2_SCENEVIEW_STRIDE;
                *views_data_ptr2 = secondary_view2;
                *views_count_ptr2 = 1;

                utility::ScopeGuard restore_views2{[&]() {
                    *views_data_ptr2 = original_views_data2;
                    *views_count_ptr2 = original_views_count2;
                }};

                call_original();

                static std::atomic<uint64_t> init_second_calls{0};
                const auto cnt = init_second_calls.fetch_add(1, std::memory_order_relaxed);
                if (cnt < 8 || (cnt % 300) == 0) {
                    SPDLOG_INFO(
                        "[Subnautica2][InitVolumetric] Ran second init for view 1 call={}",
                        cnt + 1);
                }
            }
        }
    }
}

void FFakeStereoRenderingHook::subnautica2_reconstruct_volumetric_render_target_hook(
    FRDGBuilder* graph_builder,
    void* views,
    FRDGTexture* scene_depth,
    FRDGTexture* half_resolution_depth_checkerboard_minmax,
    bool wait_finish_fence)
{
    auto* hook = g_hook;

    if (hook == nullptr || !hook->m_subnautica2_reconstruct_volumetric_render_target_hook) {
        return;
    }

    auto call_original = [&]() {
        hook->m_subnautica2_reconstruct_volumetric_render_target_hook.unsafe_call<void>(
            graph_builder,
            views,
            scene_depth,
            half_resolution_depth_checkerboard_minmax,
            wait_finish_fence);
    };

    thread_local bool inside_hook = false;
    if (inside_hook) {
        call_original();
        return;
    }

    inside_hook = true;
    utility::ScopeGuard inside_guard{[]() {
        inside_hook = false;
    }};

    auto& vr = VR::get();
    const bool log_native_stereo =
        subnautica2_is_current_game() &&
        vr != nullptr &&
        vr->is_hmd_active() &&
        vr->is_native_stereo_fix_enabled() &&
        !vr->is_native_stereo_fix_same_pass_enabled() &&
        !vr->is_using_afr();

    if (log_native_stereo) {
        subnautica2_log_compose_volumetric_render_target("VRTReconstructBefore", views, false, nullptr);
    }

    call_original();

    if (log_native_stereo) {
        subnautica2_log_compose_volumetric_render_target("VRTReconstructAfter", views, false, nullptr);
    }

    // Patch K: double-dispatch reconstruct for view 1. Like compose, this
    // function reads views_data[0] only — without a second call, view 1's
    // ReconstructionRT stays empty and the subsequent compose pass for view 1
    // reads garbage. Swap views[0] -> &view[1] for a second run.
    if (log_native_stereo &&
        views != nullptr &&
        !subnautica2_disable_volumetric_fog_view_loop_fix() &&
        !subnautica2_disable_volumetric_rt_replay())
    {
        const uintptr_t views_addr = (uintptr_t)views;
        if (is_readable_process_range(views_addr, sizeof(uintptr_t) + sizeof(int32_t))) {
            auto** views_data_ptr2 = (uint8_t**)views_addr;
            auto* views_count_ptr2 = (int32_t*)(views_addr + sizeof(uintptr_t));
            auto* const original_views_data2 = *views_data_ptr2;
            const auto original_views_count2 = *views_count_ptr2;

            if (original_views_data2 != nullptr && original_views_count2 >= 2 &&
                is_readable_process_range((uintptr_t)original_views_data2, SUBNAUTICA2_SCENEVIEW_STRIDE * 2))
            {
                auto* const secondary_view2 = original_views_data2 + SUBNAUTICA2_SCENEVIEW_STRIDE;
                *views_data_ptr2 = secondary_view2;
                *views_count_ptr2 = 1;

                utility::ScopeGuard restore_views2{[&]() {
                    *views_data_ptr2 = original_views_data2;
                    *views_count_ptr2 = original_views_count2;
                }};

                call_original();

                static std::atomic<uint64_t> reconstruct_second_calls{0};
                const auto cnt = reconstruct_second_calls.fetch_add(1, std::memory_order_relaxed);
                if (cnt < 8 || (cnt % 300) == 0) {
                    SPDLOG_INFO(
                        "[Subnautica2][ReconstructVolumetric] Ran second reconstruct for view 1 call={}",
                        cnt + 1);
                }
            }
        }
    }
}

void FFakeStereoRenderingHook::subnautica2_compose_volumetric_render_target_hook(
    FRDGBuilder* graph_builder,
    void* views,
    FRDGTexture* scene_color,
    FRDGTexture* scene_depth,
    bool compose_with_water,
    void* water_pass_data,
    void* scene_textures)
{
    auto* hook = g_hook;

    if (hook == nullptr || !hook->m_subnautica2_compose_volumetric_render_target_hook) {
        return;
    }

    auto call_original = [&]() {
        hook->m_subnautica2_compose_volumetric_render_target_hook.unsafe_call<void>(
            graph_builder,
            views,
            scene_color,
            scene_depth,
            compose_with_water,
            water_pass_data,
            scene_textures);
    };

    thread_local bool inside_hook = false;
    if (inside_hook) {
        call_original();
        return;
    }

    inside_hook = true;
    utility::ScopeGuard inside_guard{[]() {
        inside_hook = false;
    }};

    auto& vr = VR::get();

    // Defaults: no patch
    uint8_t* patched_view1 = nullptr;
    int32_t saved_init_rect[4]{};
    int32_t saved_runtime_rect[4]{};
    bool patched_init_rect = false;
    bool patched_runtime_rect = false;

    // Patch M state: expanded view[0] rect to cover full SBS.
    uint8_t* expanded_view0 = nullptr;
    int32_t saved_v0_init_rect[4]{};
    int32_t saved_v0_runtime_rect[4]{};
    bool expanded_v0_init = false;
    bool expanded_v0_runtime = false;

    // Patch N state: shared view 0's volumetric ViewState with view 1.
    uint8_t* patch_n_view1 = nullptr;
    uintptr_t patch_n_saved_v1_view_state = 0;

    if (subnautica2_is_current_game() &&
        vr != nullptr &&
        vr->is_hmd_active() &&
        vr->is_native_stereo_fix_enabled() &&
        !vr->is_native_stereo_fix_same_pass_enabled() &&
        !vr->is_using_afr())
    {
        subnautica2_log_compose_volumetric_render_target(
            "VolCloudComposeOverScene",
            views,
            compose_with_water,
            water_pass_data);

        // Per-eye rect patch. SN2's ComposeVolumetricRenderTargetOverScene reads
        // both views' init_options.view_rect and runtime view_rect, but for
        // stereo with view 0 occupying x=[0,W) and view 1 occupying x=[W,2W),
        // SN2 leaves view 1's rect mirroring view 0's (both starting at x=0).
        // The composite then writes the fog overlay to the left half only,
        // producing the "fog only in left eye" symptom on the title screen
        // and underwater. We detect the mirror and shift view 1's rect right.
        if (!subnautica2_disable_compose_volumetric_view_rect_fix() && views != nullptr) {
            const uintptr_t views_addr = (uintptr_t)views;

            if (is_readable_process_range(views_addr, sizeof(uintptr_t) + sizeof(int32_t))) {
                auto* const views_data = *(uint8_t**)views_addr;
                const auto views_count = *(int32_t*)(views_addr + sizeof(uintptr_t));

                if (views_data != nullptr && views_count >= 2 &&
                    is_readable_process_range((uintptr_t)views_data, SUBNAUTICA2_SCENEVIEW_STRIDE * 2))
                {
                    auto* const view0 = views_data + 0;
                    auto* const view1 = views_data + SUBNAUTICA2_SCENEVIEW_STRIDE;

                    auto* const v0_init = (sdk::FSceneViewInitOptionsUE5*)(view0 + SUBNAUTICA2_SCENEVIEW_INIT_OPTIONS_OFFSET);
                    auto* const v1_init = (sdk::FSceneViewInitOptionsUE5*)(view1 + SUBNAUTICA2_SCENEVIEW_INIT_OPTIONS_OFFSET);
                    auto* const v0_rt = (int32_t*)(view0 + SUBNAUTICA2_SCENEVIEW_RUNTIME_VIEW_RECT_OFFSET);
                    auto* const v1_rt = (int32_t*)(view1 + SUBNAUTICA2_SCENEVIEW_RUNTIME_VIEW_RECT_OFFSET);

                    const int32_t v0_init_width = v0_init->view_rect[2] - v0_init->view_rect[0];
                    const int32_t v0_rt_width = v0_rt[2] - v0_rt[0];

                    const bool init_mirrored =
                        v0_init_width > 0 &&
                        memcmp(v0_init->view_rect, v1_init->view_rect, sizeof(int32_t) * 4) == 0;
                    const bool rt_mirrored =
                        v0_rt_width > 0 &&
                        memcmp(v0_rt, v1_rt, sizeof(int32_t) * 4) == 0;

                    if (init_mirrored) {
                        memcpy(saved_init_rect, v1_init->view_rect, sizeof(saved_init_rect));
                        v1_init->view_rect[0] += v0_init_width;
                        v1_init->view_rect[2] += v0_init_width;
                        patched_init_rect = true;
                    }
                    // Patch M disabled — was making things worse, not better.

                    // Patch N disabled — shared ViewState made compose write view 0's
                    // rect for both passes (output dimensions come from ViewState).
                    (void)patch_n_view1;
                    (void)patch_n_saved_v1_view_state;

                    if (rt_mirrored) {
                        memcpy(saved_runtime_rect, v1_rt, sizeof(saved_runtime_rect));
                        // Match DLSS's expected source rect for view 1 exactly:
                        // DLSS log shows view 1 at (1128, 0, 2249, 1174).
                        // 8-pixel aligned start (v0_rt[2] rounded up to 8).
                        // Use full height (init_view_rect[3] * scale = 1760 * 0.667 ≈ 1174).
                        const int32_t v1_x_start = (v0_rt[2] + 7) & ~7;     // align to 8
                        const int32_t v1_height = (v0_init->view_rect[3] * 2 + 2) / 3; // 1760 * 2/3
                        v1_rt[0] = v1_x_start;
                        v1_rt[1] = v0_rt[1];
                        v1_rt[2] = v1_x_start + v0_rt_width;
                        v1_rt[3] = v1_height;
                        patched_runtime_rect = true;
                    }
                    // Patch M removed — was sending compose's output to invalid coords
                    // (init_view_rect-based shift wrote past the half-res scene_color buffer edge).
                    expanded_v0_runtime = false;
                    expanded_v0_init = false;
                    expanded_view0 = nullptr;

                    if (patched_init_rect || patched_runtime_rect) {
                        patched_view1 = view1;

                        static uint32_t patch_log_count = 0;
                        if (patch_log_count < 16) {
                            SPDLOG_INFO(
                                "[Subnautica2][ComposeVolumetric] Patched view[1] rect: init=({} {} {} {}) -> ({} {} {} {}); runtime=({} {} {} {}) -> ({} {} {} {})",
                                saved_init_rect[0], saved_init_rect[1], saved_init_rect[2], saved_init_rect[3],
                                v1_init->view_rect[0], v1_init->view_rect[1], v1_init->view_rect[2], v1_init->view_rect[3],
                                saved_runtime_rect[0], saved_runtime_rect[1], saved_runtime_rect[2], saved_runtime_rect[3],
                                v1_rt[0], v1_rt[1], v1_rt[2], v1_rt[3]);
                            ++patch_log_count;
                        }
                    }
                }
            }
        }
    }

    // DIAGNOSTIC: log compose exit-check fields for BOTH views before call.
    // The compose function iterates views[v17] in a while loop but exits to
    // the next view if any of these fields don't have expected values for
    // view 1. Identify which exit fires.
    if (subnautica2_is_current_game() && views != nullptr) {
        const uintptr_t views_addr = (uintptr_t)views;
        if (is_readable_process_range(views_addr, sizeof(uintptr_t) + sizeof(int32_t))) {
            auto* const views_data = *(uint8_t**)views_addr;
            const auto views_count = *(int32_t*)(views_addr + sizeof(uintptr_t));
            if (views_data != nullptr && views_count >= 2 &&
                is_readable_process_range((uintptr_t)views_data, SUBNAUTICA2_SCENEVIEW_STRIDE * 2))
            {
                static std::atomic<int> diag_count{0};
                if (diag_count.fetch_add(1, std::memory_order_relaxed) < 32) {
                    for (int eye = 0; eye < 2; ++eye) {
                        auto* const view = views_data + (SUBNAUTICA2_SCENEVIEW_STRIDE * eye);
                        const uintptr_t view_addr = (uintptr_t)view;
                        uintptr_t view_state = 0;
                        uintptr_t view_family = 0;
                        uint32_t state_1F50 = 0xDEADBEEF;
                        uint32_t view_2348_ptr_value = 0xDEADBEEF;
                        uintptr_t view_2348_ptr = 0;

                        if (is_readable_process_range(view_addr + 0x1C10, 8)) view_state = *(uintptr_t*)(view_addr + 0x1C10);
                        if (is_readable_process_range(view_addr + 8, 8)) view_family = *(uintptr_t*)(view_addr + 8);
                        if (view_state != 0 && is_readable_process_range(view_state + 0x1F50, 4)) state_1F50 = *(uint32_t*)(view_state + 0x1F50);
                        if (is_readable_process_range(view_addr + 0x2348, 8)) view_2348_ptr = *(uintptr_t*)(view_addr + 0x2348);
                        if (view_2348_ptr != 0 && is_readable_process_range(view_2348_ptr, 4)) view_2348_ptr_value = *(uint32_t*)view_2348_ptr;

                        // Compute the same predicates as sub_142FD72A0 (v26) and the fog enable
                        // path to identify which view ends up with no-fog shader variant.
                        const bool state_1F50_passes = ((state_1F50 & 0xFFFFFFFDu) == 0);

                        // 2026-05-15: probe the remaining candidate inputs to compose shader
                        // permutation key v31 (from offline IDA synthesis). v28 (=*(view+0x2348))
                        // and v101 (=state+0x1F50) are 0 for both eyes per prior logs.
                        // Remaining candidates that diverge per-view: v102 (underwater depth at
                        // view+0x1200), v30 (family+0xA5 — should be shared, sanity-check).
                        float view_1200_f = 0.0f;
                        uint32_t view_1200_u = 0xDEADBEEF;
                        if (is_readable_process_range(view_addr + 0x1200, 4)) {
                            view_1200_u = *(uint32_t*)(view_addr + 0x1200);
                            view_1200_f = *(float*)(view_addr + 0x1200);
                        }
                        uint32_t view_1204_u = 0xDEADBEEF;
                        if (is_readable_process_range(view_addr + 0x1204, 4)) {
                            view_1204_u = *(uint32_t*)(view_addr + 0x1204);
                        }
                        uint8_t family_A5 = 0xFF;
                        if (view_family != 0 && is_readable_process_range(view_family + 0xA5, 1)) {
                            family_A5 = *(uint8_t*)(view_family + 0xA5);
                        }
                        // v102 predicate as decompile suggests: underwater_depth > 0.0f
                        const bool v102_predicate = (view_1200_f > 0.0f);

                        // 2026-05-15 followup: v28/v101/v102/v30 all match between eyes.
                        // Remaining v27/v93 depend on ShouldViewRenderVolumetricCloudRenderTarget(view)
                        // whose per-view inputs are: view+0x11EC (byte; must == 0) AND
                        // view+0x428 (double; must be < 1.0). These are our prime suspects.
                        uint8_t view_11EC = 0xFF;
                        if (is_readable_process_range(view_addr + 0x11EC, 1)) {
                            view_11EC = *(uint8_t*)(view_addr + 0x11EC);
                        }
                        double view_428_d = 0.0;
                        uint64_t view_428_u = 0xDEADBEEFDEADBEEFull;
                        if (is_readable_process_range(view_addr + 0x428, 8)) {
                            view_428_d = *(double*)(view_addr + 0x428);
                            view_428_u = *(uint64_t*)(view_addr + 0x428);
                        }
                        const bool check_11EC_zero = (view_11EC == 0);
                        const bool check_428_lt1 = (view_428_d < 1.0);
                        const bool should_render_vcrt =
                            check_11EC_zero &&
                            check_428_lt1 &&
                            (view_state != 0);

                        // 2026-05-15 third diag pass: compose's INNER per-view loop has
                        // exit checks on STATE-side fields beyond v31 inputs.
                        // From compose decompile (sub_142FFB190):
                        //   `(state+7840, !*(BYTE*)(state+7855))` - state+0x1EAF must be nonzero
                        //   `(v19 = state+7192, state+0xC88: *(float*)(v19+0xCCC)==0 && !(*(byte*)(v19+0x1410)&0x10))` - skip combo
                        // The state pointer here is view+0x1C10 (ViewState), so:
                        //   state+0x1EAF must be != 0
                        //   state+0xCCC must be != 0.0  OR  (state+0x1410 & 0x10) must be set
                        // These are the FVolumetricRenderTargetViewStateData fields.
                        // If view 1's state has 0x1EAF==0, compose skips view 1 entirely.
                        uint8_t state_1EAF = 0xFF;
                        float state_CCC_f = 0.0f;
                        uint32_t state_CCC_u = 0xDEADBEEF;
                        uint8_t state_1410 = 0xFF;
                        if (view_state != 0) {
                            if (is_readable_process_range(view_state + 0x1EAF, 1)) state_1EAF = *(uint8_t*)(view_state + 0x1EAF);
                            if (is_readable_process_range(view_state + 0xCCC, 4)) {
                                state_CCC_u = *(uint32_t*)(view_state + 0xCCC);
                                state_CCC_f = *(float*)(view_state + 0xCCC);
                            }
                            if (is_readable_process_range(view_state + 0x1410, 1)) state_1410 = *(uint8_t*)(view_state + 0x1410);
                        }
                        const bool vrtsd_present = (state_1EAF != 0);
                        const bool ccc_combo_passes = !((state_CCC_f == 0.0f) && ((state_1410 & 0x10) == 0));

                        // 2026-05-15 user hypothesis: view+0x1C18 = CachedViewUniform ptr.
                        // If shared/null for view 1, underwater fog shader for view 1 samples
                        // with view 0's matrices -> no teal in view 1 region.
                        // Also probe view+0x1C00 (RUNTIME_VIEW_RECT base, per user note this
                        // is where the draw destination viewport comes from).
                        uintptr_t view_1C18 = 0;
                        if (is_readable_process_range(view_addr + 0x1C18, 8)) {
                            view_1C18 = *(uintptr_t*)(view_addr + 0x1C18);
                        }
                        uintptr_t view_1C00 = 0;
                        if (is_readable_process_range(view_addr + 0x1C00, 8)) {
                            view_1C00 = *(uintptr_t*)(view_addr + 0x1C00);
                        }
                        uintptr_t view_1C08 = 0;
                        if (is_readable_process_range(view_addr + 0x1C08, 8)) {
                            view_1C08 = *(uintptr_t*)(view_addr + 0x1C08);
                        }

                        static std::atomic<uint64_t> compose_exit_log_count{0};
                        const auto compose_exit_count = compose_exit_log_count.fetch_add(1, std::memory_order_relaxed);
                        if (compose_exit_count < sn2_compose_exit_check_log_max()) {
                            SPDLOG_WARN(
                                "[ComposeExitCheck] eye={} view=0x{:x} ViewState=0x{:x} ViewFamily=0x{:x} ViewState+0x1F50=0x{:08x} state_1F50_passes(0or2)={} view+0x2348(ptr)=0x{:x} *(view+0x2348)=0x{:08x} | v102_view+0x1200=0x{:08x}({:.4f}) v102_predicate(>0)={} view+0x1204=0x{:08x} v30_family+0xA5=0x{:02x} | view+0x11EC=0x{:02x} (==0 passes={}) view+0x428=0x{:016x}({:.6f}) (<1.0 passes={}) approx_VCRT={} | state+0x1EAF=0x{:02x} VRTSD_present={} state+0xCCC=0x{:08x}({:.4f}) state+0x1410=0x{:02x} ccc_combo_passes={} | view+0x1C00=0x{:x} view+0x1C08=0x{:x} view+0x1C18(CachedViewUniform)=0x{:x}",
                                eye, view_addr, view_state, view_family,
                                state_1F50, state_1F50_passes,
                                view_2348_ptr, view_2348_ptr_value,
                                view_1200_u, view_1200_f, v102_predicate,
                                view_1204_u, family_A5,
                                view_11EC, check_11EC_zero,
                                view_428_u, view_428_d, check_428_lt1,
                                should_render_vcrt,
                                state_1EAF, vrtsd_present,
                                state_CCC_u, state_CCC_f,
                                state_1410, ccc_combo_passes,
                                view_1C00, view_1C08, view_1C18);
                        }
                    }
                }
            }
        }
    }

    call_original();

    // Patch J: double-dispatch compose for view 1. The function reads
    // views_data[0] only; without this, view 1's region of the scene-color
    // SBS buffer never receives the fog overlay. Unlike compute_volumetric_fog,
    // compose does NOT call FRDGBuilder::CreateTexture with literal names —
    // it only RegisterExternalTexture (idempotent) and AddPass (index-based).
    // So double-dispatching should not assert.
    if (subnautica2_is_current_game() &&
        vr != nullptr &&
        vr->is_hmd_active() &&
        vr->is_native_stereo_fix_enabled() &&
        !vr->is_native_stereo_fix_same_pass_enabled() &&
        !vr->is_using_afr() &&
        views != nullptr &&
        !subnautica2_disable_volumetric_fog_view_loop_fix() &&
        !subnautica2_disable_volumetric_rt_replay())
    {
        const uintptr_t views_addr = (uintptr_t)views;
        if (is_readable_process_range(views_addr, sizeof(uintptr_t) + sizeof(int32_t))) {
            auto** views_data_ptr2 = (uint8_t**)views_addr;
            auto* views_count_ptr2 = (int32_t*)(views_addr + sizeof(uintptr_t));
            auto* const original_views_data2 = *views_data_ptr2;
            const auto original_views_count2 = *views_count_ptr2;

            if (original_views_data2 != nullptr && original_views_count2 >= 2 &&
                is_readable_process_range((uintptr_t)original_views_data2, SUBNAUTICA2_SCENEVIEW_STRIDE * 2))
            {
                auto* const secondary_view2 = original_views_data2 + SUBNAUTICA2_SCENEVIEW_STRIDE;

                *views_data_ptr2 = secondary_view2;
                *views_count_ptr2 = 1;

                utility::ScopeGuard restore_views2{[&]() {
                    *views_data_ptr2 = original_views_data2;
                    *views_count_ptr2 = original_views_count2;
                }};

                call_original();

                static std::atomic<uint64_t> compose_second_calls{0};
                const auto cnt = compose_second_calls.fetch_add(1, std::memory_order_relaxed);
                if (cnt < 8 || (cnt % 300) == 0) {
                    SPDLOG_INFO(
                        "[Subnautica2][ComposeVolumetric] Ran second compose dispatch for view 1 call={}",
                        cnt + 1);
                }
            }
        }
    }

    // Restore view[1]'s rect AFTER the second dispatch so the second call
    // sees the shifted rect (write to view 1's region of scene color).
    if (patched_view1 != nullptr) {
        auto* const v1_init = (sdk::FSceneViewInitOptionsUE5*)(patched_view1 + SUBNAUTICA2_SCENEVIEW_INIT_OPTIONS_OFFSET);
        auto* const v1_rt = (int32_t*)(patched_view1 + SUBNAUTICA2_SCENEVIEW_RUNTIME_VIEW_RECT_OFFSET);
        if (patched_init_rect) {
            memcpy(v1_init->view_rect, saved_init_rect, sizeof(saved_init_rect));
        }
        if (patched_runtime_rect) {
            memcpy(v1_rt, saved_runtime_rect, sizeof(saved_runtime_rect));
        }
    }

    // Patch M: restore view[0]'s rect after expansion.
    if (expanded_view0 != nullptr) {
        auto* const v0_init = (sdk::FSceneViewInitOptionsUE5*)(expanded_view0 + SUBNAUTICA2_SCENEVIEW_INIT_OPTIONS_OFFSET);
        auto* const v0_rt = (int32_t*)(expanded_view0 + SUBNAUTICA2_SCENEVIEW_RUNTIME_VIEW_RECT_OFFSET);
        if (expanded_v0_init) {
            memcpy(v0_init->view_rect, saved_v0_init_rect, sizeof(saved_v0_init_rect));
        }
        if (expanded_v0_runtime) {
            memcpy(v0_rt, saved_v0_runtime_rect, sizeof(saved_v0_runtime_rect));
        }
    }

    // Patch N: restore view[1]'s volumetric ViewState pointer.
    if (patch_n_view1 != nullptr) {
        auto* const v1_view_state_addr = (uintptr_t*)(patch_n_view1 + 0x1C10);
        if (is_writable_process_range((uintptr_t)v1_view_state_addr, sizeof(uintptr_t))) {
            *v1_view_state_addr = patch_n_saved_v1_view_state;
        }
    }
}

// Read the override value from UEVR_SUBNAUTICA2_BASEPASS_PSO_FORCE_A3. Values 0..6
// force every BasePass PSO selection to that case. -1 (or unset) means observe-only.
static int subnautica2_basepass_pso_force_a3_value() {
    if (subnautica2_diag_clean_mode()) return -1;
    static const int result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_BASEPASS_PSO_FORCE_A3", value, (DWORD)std::size(value));

        if (len == 0 || len >= std::size(value)) {
            return -1;
        }

        const int parsed = _wtoi(value);
        if (parsed < 0 || parsed > 6) {
            return -1;
        }
        return parsed;
    }();
    return result;
}

// Gate function: read UEVR_SUBNAUTICA2_DISABLE_BASEPASS_MP_CTOR_FIX env var.
// Default ENABLED (the BasePass MP ctor cull-threshold sync IS the fix).
static bool subnautica2_disable_basepass_mp_ctor_fix() {
    if (subnautica2_diag_clean_mode()) return true;
    static const bool result = []() {
        wchar_t value[16]{};
        const auto len = GetEnvironmentVariableW(L"UEVR_SUBNAUTICA2_DISABLE_BASEPASS_MP_CTOR_FIX", value, (DWORD)std::size(value));

        if (len == 0 || len >= std::size(value)) {
            return false;
        }

        return value[0] != L'\0' && value[0] != L'0' && value[0] != L'f' && value[0] != L'F';
    }();

    return result;
}

__int64 FFakeStereoRenderingHook::subnautica2_basepass_mp_ctor_hook(
    __int64 a1, __int64 a2, __int64 a3, unsigned int a4,
    __int64 a5, void* a6, __int64 a7, char a8, int a9)
{
    auto* hook = g_hook;

    if (hook == nullptr || !hook->m_subnautica2_basepass_mp_ctor_hook) {
        return 0;
    }

    // Call the original ctor first; it sets up *(_DWORD*)(a1+140) = the
    // per-view cull threshold based on View+0x24CC (only if View+0x11D9 != 0
    // and other conditions). Without our fix, view 0 gets a tight threshold
    // (1.0f) and view 1 gets a loose one (~6e7), causing different per-mesh
    // r12b cull verdicts in TryAddMeshBatch -> different shader-type-getter
    // variant -> different DXBC -> different fog rendering per eye.
    auto result = hook->m_subnautica2_basepass_mp_ctor_hook.unsafe_call<__int64>(
        a1, a2, a3, a4, a5, a6, a7, a8, a9);

    if (subnautica2_disable_basepass_mp_ctor_fix()) {
        return result;
    }

    // The ctor wrote *(_DWORD*)(a1+140) (== +0x8C) based on the FSceneView
    // passed in a5. Detect which view this is via FSceneView+STEREO_PASS,
    // cache primary's value, and force secondary's to match.
    static std::atomic<uint32_t> s_primary_threshold_bits{0};
    static std::atomic<bool> s_have_primary{false};
    static uint32_t log_count = 0;

    if (a5 != 0 && !IsBadReadPtr((void*)(a5 + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET), sizeof(uint32_t))) {
        const auto stereo_pass = *(uint32_t*)(a5 + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET);
        const uint32_t current_bits = *(uint32_t*)(a1 + 0x8C);

        if (stereo_pass == (uint32_t)EStereoscopicPass::eSSP_PRIMARY) {
            // Cache primary's threshold for the next secondary call to use.
            s_primary_threshold_bits.store(current_bits, std::memory_order_relaxed);
            s_have_primary.store(true, std::memory_order_relaxed);

            if (log_count < 16) {
                SPDLOG_INFO(
                    "[Subnautica2][BasePassMPCtor] PRIMARY ctor processor={:x} view={:x} pass={} threshold=0x{:08x}",
                    (uintptr_t)a1, (uintptr_t)a5, stereo_pass, current_bits);
                ++log_count;
            }
        } else if (stereo_pass == (uint32_t)EStereoscopicPass::eSSP_SECONDARY &&
                   s_have_primary.load(std::memory_order_relaxed))
        {
            const uint32_t primary_bits = s_primary_threshold_bits.load(std::memory_order_relaxed);

            if (current_bits != primary_bits) {
                *(uint32_t*)(a1 + 0x8C) = primary_bits;

                if (log_count < 16) {
                    SPDLOG_INFO(
                        "[Subnautica2][BasePassMPCtor] SECONDARY ctor processor={:x} view={:x} pass={} overrode threshold 0x{:08x} -> 0x{:08x}",
                        (uintptr_t)a1, (uintptr_t)a5, stereo_pass, current_bits, primary_bits);
                    ++log_count;
                }
            }
        }
    }

    return result;
}

void FFakeStereoRenderingHook::subnautica2_basepass_cull_threshold_hook(safetyhook::Context& ctx) {
    // Fires immediately AFTER `movss xmm3,[rsi+8Ch]` in TryAddMeshBatch.
    // ctx.rsi holds FBasePassMeshProcessor*; *(rsi+0x28) is the FSceneView*.
    // Read the view's stereo pass; if SECONDARY, override xmm3 with the
    // primary view's cached threshold so per-view shader selection agrees.
    auto* hook = g_hook;
    if (hook == nullptr || !hook->m_subnautica2_basepass_cull_threshold_hook) {
        return;
    }

    auto& vr = VR::get();
    if (vr == nullptr || !vr->is_hmd_active() ||
        !vr->is_native_stereo_fix_enabled() ||
        vr->is_native_stereo_fix_same_pass_enabled() ||
        vr->is_using_afr())
    {
        return;
    }

    const auto processor = (uintptr_t)ctx.rsi;
    // Strict validity check: processor must be a 16-byte-aligned high-half
    // heap pointer (Windows x64 user-mode addresses < 0x7FFFFFFFFFFF).
    if (processor == 0 ||
        (processor & 0xF) != 0 ||
        processor < 0x10000 ||
        processor >= 0x7FFFFFFFFFFFull ||
        IsBadReadPtr((void*)(processor + 0x28), sizeof(uintptr_t)))
    {
        return;
    }

    const auto view_ptr = *(uintptr_t*)(processor + 0x28);
    if (view_ptr == 0 ||
        (view_ptr & 0xF) != 0 ||
        view_ptr < 0x10000 ||
        view_ptr >= 0x7FFFFFFFFFFFull ||
        IsBadReadPtr((void*)(view_ptr + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET), sizeof(uint32_t)))
    {
        return;
    }

    const auto stereo_pass = *(uint32_t*)(view_ptr + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET);

    // Must be one of the known EStereoscopicPass values (0=NONE, 1=PRIMARY,
    // 2=SECONDARY). Garbage stereo_pass means our pointer chase landed in
    // non-FSceneView memory; do nothing rather than corrupting state.
    if (stereo_pass > 2) {
        return;
    }

    static std::atomic<uint32_t> s_primary_bits{0};
    static std::atomic<bool> s_have_primary{false};
    static std::atomic<uint64_t> s_log_count{0};

    if (stereo_pass == (uint32_t)EStereoscopicPass::eSSP_PRIMARY) {
        // Cache primary's threshold for the matching secondary call.
        // xmm3 is the 4th XMM register; ctx.xmm3 is a 16-byte struct.
        const uint32_t bits = *(uint32_t*)&ctx.xmm3;
        s_primary_bits.store(bits, std::memory_order_relaxed);
        s_have_primary.store(true, std::memory_order_relaxed);

        const auto n = s_log_count.fetch_add(1, std::memory_order_relaxed);
        if (n < 32) {
            SPDLOG_INFO(
                "[Subnautica2][BasePassCullThreshold] PRIMARY view rsi={:x} view={:x} pass={} threshold_bits=0x{:08x}",
                processor, view_ptr, stereo_pass, bits);
        }
    } else if (stereo_pass == (uint32_t)EStereoscopicPass::eSSP_SECONDARY &&
               s_have_primary.load(std::memory_order_relaxed))
    {
        const uint32_t primary_bits = s_primary_bits.load(std::memory_order_relaxed);
        const uint32_t before_bits = *(uint32_t*)&ctx.xmm3;

        if (before_bits != primary_bits) {
            // Log-only mode for crash isolation. To re-enable actual override,
            // set UEVR_SUBNAUTICA2_BASEPASS_CULL_THRESHOLD_OVERRIDE=1.
            static const bool override_enabled = []() {
                wchar_t value[16]{};
                const auto len = GetEnvironmentVariableW(
                    L"UEVR_SUBNAUTICA2_BASEPASS_CULL_THRESHOLD_OVERRIDE",
                    value, (DWORD)std::size(value));
                return len > 0 && len < std::size(value) &&
                    value[0] != L'\0' && value[0] != L'0' &&
                    value[0] != L'f' && value[0] != L'F';
            }();

            if (override_enabled) {
                *(uint32_t*)&ctx.xmm3 = primary_bits;
            }

            const auto n = s_log_count.fetch_add(1, std::memory_order_relaxed);
            if (n < 32) {
                SPDLOG_INFO(
                    "[Subnautica2][BasePassCullThreshold] SECONDARY view rsi={:x} view={:x} pass={} threshold_before=0x{:08x} primary=0x{:08x} override_enabled={}",
                    processor, view_ptr, stereo_pass, before_bits, primary_bits, override_enabled);
            }
        }
    }
}

void FFakeStereoRenderingHook::subnautica2_basepass_pso_select_hook(safetyhook::Context& ctx) {
    // r8d is the 7-way switch input (a3 in IDA's view of sub_142631130).
    // We can optionally override it to force all calls to use the same case
    // branch, eliminating per-view PSO divergence.
    const int a3 = (int)(ctx.r8 & 0xFFFFFFFFu);
    int effective_a3 = a3;
    const int forced = subnautica2_basepass_pso_force_a3_value();

    if (subnautica2_is_current_game() && forced >= 0 && forced <= 6) {
        effective_a3 = forced;
        ctx.r8 = (uint64_t)(uint32_t)effective_a3;
    }

    static std::atomic<uint64_t> log_count{0};
    const auto current = log_count.fetch_add(1, std::memory_order_relaxed);
    if (current < 96) {
        SPDLOG_INFO(
            "[Subnautica2][BasePassPSO] sub_142631130 call#{} rcx={:x} rdx={:x} r8(a3)={} effective_a3={} r9={}",
            current + 1,
            (uintptr_t)ctx.rcx,
            (uintptr_t)ctx.rdx,
            a3,
            effective_a3,
            (uint32_t)(ctx.r9 & 0xFFFFFFFFu));
    }
}

bool FFakeStereoRenderingHook::subnautica2_render_fog_wrapper_hook(
    void* scene_renderer,
    FRDGBuilder* graph_builder,
    void* scene_textures,
    void* scene_color_or_data,
    bool should_render_volumetric)
{
    auto* hook = g_hook;

    if (hook == nullptr || !hook->m_subnautica2_render_fog_wrapper_hook) {
        return false;
    }

    auto call_original = [&]() {
        return hook->m_subnautica2_render_fog_wrapper_hook.unsafe_call<bool>(
            scene_renderer,
            graph_builder,
            scene_textures,
            scene_color_or_data,
            should_render_volumetric);
    };

    thread_local bool inside_hook = false;
    if (inside_hook) {
        return call_original();
    }

    inside_hook = true;
    utility::ScopeGuard inside_guard{[]() {
        inside_hook = false;
    }};

    std::array<Subnautica2RuntimeViewRectPatch, 2> patches{};
    const bool patched = subnautica2_patch_renderer_runtime_view_rects_for_fog(
        "RenderFogWrapper",
        scene_renderer,
        patches);

    utility::ScopeGuard restore_view_rects{[&]() {
        if (patched && !subnautica2_persist_render_fog_view_rect_fix()) {
            subnautica2_restore_runtime_view_rects(patches);
        }
    }};

    return call_original();
}

bool FFakeStereoRenderingHook::subnautica2_render_fog_pass_hook(
    void* scene_renderer,
    FRDGBuilder* graph_builder,
    void* scene_textures,
    void* scene_color_or_data,
    bool should_render_volumetric)
{
    auto* hook = g_hook;

    if (hook == nullptr || !hook->m_subnautica2_render_fog_pass_hook) {
        return false;
    }

    auto call_original = [&]() {
        return hook->m_subnautica2_render_fog_pass_hook.unsafe_call<bool>(
            scene_renderer,
            graph_builder,
            scene_textures,
            scene_color_or_data,
            should_render_volumetric);
    };

    thread_local bool inside_hook = false;
    if (inside_hook) {
        return call_original();
    }

    inside_hook = true;
    utility::ScopeGuard inside_guard{[]() {
        inside_hook = false;
    }};

    std::array<Subnautica2RuntimeViewRectPatch, 2> patches{};
    const bool patched = subnautica2_patch_renderer_runtime_view_rects_for_fog(
        "HeightFogPass",
        scene_renderer,
        patches);

    utility::ScopeGuard restore_view_rects{[&]() {
        if (patched && !subnautica2_persist_render_fog_view_rect_fix()) {
            subnautica2_restore_runtime_view_rects(patches);
        }
    }};

    return call_original();
}

void FFakeStereoRenderingHook::subnautica2_render_underwater_fog_hook(
    void* scene_renderer,
    FRDGBuilder* graph_builder,
    void* scene_without_water_textures,
    void* scene_textures)
{
    auto* hook = g_hook;

    if (hook == nullptr || !hook->m_subnautica2_render_underwater_fog_hook) {
        return;
    }

    auto call_original = [&]() {
        hook->m_subnautica2_render_underwater_fog_hook.unsafe_call<void>(
            scene_renderer,
            graph_builder,
            scene_without_water_textures,
            scene_textures);
    };

    thread_local bool inside_hook = false;
    if (inside_hook) {
        call_original();
        return;
    }

    inside_hook = true;
    utility::ScopeGuard inside_guard{[]() {
        inside_hook = false;
    }};

    auto& vr = VR::get();

    if (!subnautica2_is_current_game() ||
        vr == nullptr ||
        !vr->is_hmd_active() ||
        !vr->is_native_stereo_fix_enabled() ||
        vr->is_native_stereo_fix_same_pass_enabled() ||
        vr->is_using_afr() ||
        scene_renderer == nullptr ||
        scene_without_water_textures == nullptr)
    {
        call_original();
        return;
    }

    const auto renderer = (uintptr_t)scene_renderer;

    if (!is_readable_process_range(renderer + SUBNAUTICA2_SCENERENDERER_VIEWS_DATA_OFFSET, sizeof(uintptr_t) + sizeof(int32_t) * 2)) {
        call_original();
        return;
    }

    auto* const views_data = *(uint8_t**)(renderer + SUBNAUTICA2_SCENERENDERER_VIEWS_DATA_OFFSET);
    const auto views_count = *(int32_t*)(renderer + SUBNAUTICA2_SCENERENDERER_VIEWS_COUNT_OFFSET);
    const auto views_max = *(int32_t*)(renderer + SUBNAUTICA2_SCENERENDERER_VIEWS_MAX_OFFSET);

    const auto water = (uintptr_t)scene_without_water_textures;

    if (!is_readable_process_range(water + SUBNAUTICA2_SCENE_WITHOUT_WATER_COLOR_OFFSET, 0x30)) {
        call_original();
        return;
    }

    auto* const water_views = *(Subnautica2SceneWithoutWaterView**)(water + SUBNAUTICA2_SCENE_WITHOUT_WATER_VIEWS_DATA_OFFSET);
    const auto water_views_count = *(int32_t*)(water + SUBNAUTICA2_SCENE_WITHOUT_WATER_VIEWS_COUNT_OFFSET);
    const auto water_views_max = *(int32_t*)(water + SUBNAUTICA2_SCENE_WITHOUT_WATER_VIEWS_MAX_OFFSET);
    const auto color_texture = *(uintptr_t*)(water + SUBNAUTICA2_SCENE_WITHOUT_WATER_COLOR_OFFSET);
    const auto depth_texture = *(uintptr_t*)(water + SUBNAUTICA2_SCENE_WITHOUT_WATER_DEPTH_OFFSET);
    const auto refraction_factor_raw = *(float*)(water + SUBNAUTICA2_SCENE_WITHOUT_WATER_REFRACTION_FACTOR_OFFSET);

    if (views_data == nullptr ||
        views_count < 2 ||
        views_count > 8 ||
        views_max < views_count ||
        water_views == nullptr ||
        water_views_count < 2 ||
        water_views_count > 8 ||
        water_views_max < water_views_count ||
        !is_readable_process_range((uintptr_t)views_data, SUBNAUTICA2_SCENEVIEW_STRIDE * 2) ||
        !is_writable_process_range((uintptr_t)water_views, sizeof(Subnautica2SceneWithoutWaterView) * 2))
    {
        SPDLOG_INFO_EVERY_N_SEC(
            5,
            "[Subnautica2][UnderwaterFog] Skipping scene-without-water probe; renderer_views={} count={}/{} water_views={} count={}/{}",
            (uintptr_t)views_data,
            views_count,
            views_max,
            (uintptr_t)water_views,
            water_views_count,
            water_views_max);
        call_original();
        return;
    }

    int32_t full_rect[4]{0, 0, 0, 0};
    int32_t downsample_factor = 1;
    if (refraction_factor_raw >= 1.5f && refraction_factor_raw <= 8.5f) {
        downsample_factor = (int32_t)(refraction_factor_raw + 0.5f);
    }

    std::array<Subnautica2SceneWithoutWaterView, 2> expected{};
    bool expected_valid[2]{};

    for (int32_t eye = 0; eye < 2; ++eye) {
        auto* const view = views_data + (SUBNAUTICA2_SCENEVIEW_STRIDE * eye);

        if (!is_readable_process_range(
                (uintptr_t)view + SUBNAUTICA2_SCENEVIEW_INIT_OPTIONS_OFFSET,
                sizeof(sdk::FSceneViewInitOptionsUE5)))
        {
            continue;
        }

        int32_t rect[4]{};

        if (!subnautica2_get_effective_scene_view_rect(view, rect)) {
            continue;
        }

        full_rect[0] = std::min(full_rect[0], rect[0]);
        full_rect[1] = std::min(full_rect[1], rect[1]);
        full_rect[2] = std::max(full_rect[2], rect[2]);
        full_rect[3] = std::max(full_rect[3], rect[3]);
    }

    for (int32_t eye = 0; eye < 2; ++eye) {
        expected_valid[eye] = subnautica2_expected_scene_without_water_view(
            views_data + (SUBNAUTICA2_SCENEVIEW_STRIDE * eye),
            full_rect,
            downsample_factor,
            expected[eye]);
    }

    bool patched_any = false;
    bool mismatch_any = false;

    if (!subnautica2_disable_underwater_fog_view_data_fix()) {
        for (int32_t eye = 0; eye < 2; ++eye) {
            if (!expected_valid[eye]) {
                continue;
            }

            auto& current = water_views[eye];
            const bool rect_mismatch = !subnautica2_rect_equal(current.rect, expected[eye].rect);
            const bool uv_mismatch = !subnautica2_uv_close(current.minmax_uv, expected[eye].minmax_uv);

            mismatch_any = mismatch_any || rect_mismatch || uv_mismatch;

            if (rect_mismatch || uv_mismatch) {
                current = expected[eye];
                patched_any = true;
            }
        }
    } else {
        for (int32_t eye = 0; eye < 2; ++eye) {
            if (!expected_valid[eye]) {
                continue;
            }

            mismatch_any = mismatch_any ||
                !subnautica2_rect_equal(water_views[eye].rect, expected[eye].rect) ||
                !subnautica2_uv_close(water_views[eye].minmax_uv, expected[eye].minmax_uv);
        }
    }

    static std::atomic<uint64_t> logged_calls{0};
    const auto count = logged_calls.fetch_add(1, std::memory_order_relaxed);
    if (count < 24 || ((count % 600) == 0 && (patched_any || mismatch_any))) {
        SPDLOG_INFO(
            "[Subnautica2][UnderwaterFog] call={} renderer_views={} count={}/{} water_views={} count={}/{} color={:x} depth={:x} refraction={} downsample={} full_rect={} {} {} {} patched={} mismatch={}",
            count + 1,
            (uintptr_t)views_data,
            views_count,
            views_max,
            (uintptr_t)water_views,
            water_views_count,
            water_views_max,
            color_texture,
            depth_texture,
            refraction_factor_raw,
            downsample_factor,
            full_rect[0],
            full_rect[1],
            full_rect[2],
            full_rect[3],
            patched_any,
            mismatch_any);

        for (int32_t eye = 0; eye < 2; ++eye) {
            auto* const view = views_data + (SUBNAUTICA2_SCENEVIEW_STRIDE * eye);
            const auto* init_options = (const sdk::FSceneViewInitOptionsUE5*)(view + SUBNAUTICA2_SCENEVIEW_INIT_OPTIONS_OFFSET);
            const auto* runtime_view_rect = (const int32_t*)(view + SUBNAUTICA2_SCENEVIEW_RUNTIME_VIEW_RECT_OFFSET);
            const auto& current = water_views[eye];

            SPDLOG_INFO(
                "[Subnautica2][UnderwaterFog] eye={} view={:x} pass={} stereo_index={} primary_index={} fog_flag={} instanced={} singlepass={} multiviewport={} mobile_multiview={} bind_instanced_ub={} underwater_depth={} water_intersection={} init_rect={} {} {} {} runtime_rect={} {} {} {} current_rect={} {} {} {} current_uv={:.6f} {:.6f} {:.6f} {:.6f} expected_valid={} expected_rect={} {} {} {} expected_uv={:.6f} {:.6f} {:.6f} {:.6f}",
                eye,
                (uintptr_t)view,
                *(uint32_t*)(view + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET),
                *(int32_t*)(view + SUBNAUTICA2_SCENEVIEW_STEREO_VIEW_INDEX_OFFSET),
                *(int32_t*)(view + SUBNAUTICA2_SCENEVIEW_PRIMARY_VIEW_INDEX_OFFSET),
                view[SUBNAUTICA2_SCENEVIEW_FOG_RENDER_FLAG_OFFSET],
                view[SUBNAUTICA2_SCENEVIEW_INSTANCED_STEREO_ENABLED_OFFSET],
                view[SUBNAUTICA2_SCENEVIEW_SINGLE_PASS_STEREO_OFFSET],
                view[SUBNAUTICA2_SCENEVIEW_MULTI_VIEWPORT_ENABLED_OFFSET],
                view[SUBNAUTICA2_SCENEVIEW_MOBILE_MULTI_VIEW_ENABLED_OFFSET],
                view[SUBNAUTICA2_SCENEVIEW_SHOULD_BIND_INSTANCED_VIEW_UB_OFFSET],
                *(float*)(view + SUBNAUTICA2_SCENEVIEW_UNDERWATER_DEPTH_OFFSET),
                view[SUBNAUTICA2_SCENEVIEW_WATER_INTERSECTION_OFFSET],
                init_options->view_rect[0],
                init_options->view_rect[1],
                init_options->view_rect[2],
                init_options->view_rect[3],
                runtime_view_rect[0],
                runtime_view_rect[1],
                runtime_view_rect[2],
                runtime_view_rect[3],
                current.rect[0],
                current.rect[1],
                current.rect[2],
                current.rect[3],
                current.minmax_uv[0],
                current.minmax_uv[1],
                current.minmax_uv[2],
                current.minmax_uv[3],
                expected_valid[eye],
                expected[eye].rect[0],
                expected[eye].rect[1],
                expected[eye].rect[2],
                expected[eye].rect[3],
                expected[eye].minmax_uv[0],
                expected[eye].minmax_uv[1],
                expected[eye].minmax_uv[2],
                expected[eye].minmax_uv[3]);
        }
    }

    call_original();
}

void FFakeStereoRenderingHook::subnautica2_single_layer_water_scene_without_water_hook(safetyhook::Context& ctx) {
    subnautica2_patch_scene_without_water_views(
        "SingleLayerWater",
        (void*)ctx.r14,
        (void*)ctx.rdi);
}

void FFakeStereoRenderingHook::subnautica2_compute_volumetric_fog_force_view_index_hook(safetyhook::Context& ctx) {
    if (!g_subnautica2_force_volumetric_fog_view_index_one) {
        return;
    }

    ctx.r12 = 1;

    auto* const stack_view_index = (uint32_t*)(ctx.rbp - 0x1C);
    if (ctx.rbp != 0 && is_writable_process_range((uintptr_t)stack_view_index, sizeof(uint32_t))) {
        *stack_view_index = 1;
    }
}

// 2026-05-16: UWE trace helper. Logs entry args (rcx/rdx/r8/r9 + first stack
// args) and dereferences each pointer-shaped arg's first 32 bytes. Gated by
// the count of total calls so we don't flood the log (these functions fire
// 700+ times per frame for UWEWaterExtinctionView alone).
// Per-tag stats for ALL UWE-trace hooks. We just atomically count and store
// the most recent few argument values. A background thread periodically logs
// the stats — no per-call spdlog (which was causing 120s render-thread
// watchdog timeouts).
struct UweTraceStats {
    std::atomic<uint64_t> call_count{0};
    std::atomic<uint64_t> last_rcx{0};
    std::atomic<uint64_t> last_rdx{0};
    std::atomic<uint64_t> last_r8{0};
    std::atomic<uint64_t> last_r9{0};
    std::atomic<uint64_t> distinct_rdx_seen[8]{};
    std::atomic<uint32_t> distinct_rdx_count{0};
    // Track up to 4 distinct thread IDs that called this hook. Knowing
    // whether the call comes from the render thread (where the per-view bug
    // lives) vs the game thread vs a worker is highly diagnostic.
    std::atomic<uint32_t> distinct_tids[4]{};
    std::atomic<uint32_t> distinct_tid_count{0};
};

// Map: tag pointer (string literal so addrs are stable) -> stats slot.
// Use a small fixed table to avoid map lookups in the hot path.
static constexpr int kUweTraceMaxTags = 20;
static const char* g_uwe_trace_tag_table[kUweTraceMaxTags] = {};
static UweTraceStats g_uwe_trace_stats[kUweTraceMaxTags];
static std::atomic<int> g_uwe_trace_tag_count{0};

static UweTraceStats* uwe_trace_get_stats(const char* tag) {
    // First-call linear search; fast on subsequent calls since g_uwe_trace_tag_table
    // is populated in known order during hook install.
    const int n = g_uwe_trace_tag_count.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i) {
        if (g_uwe_trace_tag_table[i] == tag) return &g_uwe_trace_stats[i];
    }
    // Reserve a slot.
    const int idx = g_uwe_trace_tag_count.fetch_add(1, std::memory_order_acq_rel);
    if (idx >= kUweTraceMaxTags) return nullptr;
    g_uwe_trace_tag_table[idx] = tag;
    return &g_uwe_trace_stats[idx];
}

static void subnautica2_uwe_trace_dump(const char* tag, safetyhook::Context& ctx) {
    auto* s = uwe_trace_get_stats(tag);
    if (s == nullptr) return;

    // ALL we do in the hot path: count + save the registers. No spdlog, no
    // formatting, no string ops, no I/O. A background thread does the logging.
    s->call_count.fetch_add(1, std::memory_order_relaxed);
    s->last_rcx.store((uint64_t)ctx.rcx, std::memory_order_relaxed);
    s->last_rdx.store((uint64_t)ctx.rdx, std::memory_order_relaxed);
    s->last_r8.store((uint64_t)ctx.r8, std::memory_order_relaxed);
    s->last_r9.store((uint64_t)ctx.r9, std::memory_order_relaxed);

    // Track up to 8 distinct rdx values (which is usually the per-view ptr).
    const uint64_t rdx = (uint64_t)ctx.rdx;
    const uint32_t dcnt = s->distinct_rdx_count.load(std::memory_order_relaxed);
    bool found = false;
    for (uint32_t i = 0; i < dcnt && i < 8; ++i) {
        if (s->distinct_rdx_seen[i].load(std::memory_order_relaxed) == rdx) { found = true; break; }
    }
    if (!found && dcnt < 8) {
        // Atomic CAS the slot; if it succeeds, bump count.
        uint64_t expected = 0;
        if (s->distinct_rdx_seen[dcnt].compare_exchange_strong(expected, rdx, std::memory_order_acq_rel)) {
            s->distinct_rdx_count.fetch_add(1, std::memory_order_release);
        }
    }
}

static std::thread g_uwe_trace_reporter_thread;
static std::atomic<bool> g_uwe_trace_reporter_stop{false};

static void uwe_trace_reporter_loop() {
    using namespace std::chrono_literals;
    uint64_t last_counts[kUweTraceMaxTags]{};
    while (!g_uwe_trace_reporter_stop.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(2s);
        const int n = g_uwe_trace_tag_count.load(std::memory_order_acquire);
        for (int i = 0; i < n; ++i) {
            const auto& s = g_uwe_trace_stats[i];
            const auto cnt = s.call_count.load(std::memory_order_relaxed);
            const auto delta = cnt - last_counts[i];
            last_counts[i] = cnt;
            if (delta == 0 && cnt > 0) continue;  // quiet tag — skip
            const uint32_t dcnt = s.distinct_rdx_count.load(std::memory_order_relaxed);
            uint64_t rdx0 = dcnt > 0 ? s.distinct_rdx_seen[0].load(std::memory_order_relaxed) : 0;
            uint64_t rdx1 = dcnt > 1 ? s.distinct_rdx_seen[1].load(std::memory_order_relaxed) : 0;
            uint64_t rdx2 = dcnt > 2 ? s.distinct_rdx_seen[2].load(std::memory_order_relaxed) : 0;
            SPDLOG_WARN(
                "[UWE-trace][{}] total={} delta={}/s/2 distinct_rdx={} [{:x} {:x} {:x}] last rcx={:x} rdx={:x} r8={:x} r9={:x}",
                g_uwe_trace_tag_table[i], cnt, delta, dcnt, rdx0, rdx1, rdx2,
                s.last_rcx.load(std::memory_order_relaxed),
                s.last_rdx.load(std::memory_order_relaxed),
                s.last_r8.load(std::memory_order_relaxed),
                s.last_r9.load(std::memory_order_relaxed));
        }
    }
}

static void start_uwe_trace_reporter() {
    static std::once_flag once;
    std::call_once(once, [] {
        g_uwe_trace_reporter_thread = std::thread(uwe_trace_reporter_loop);
        g_uwe_trace_reporter_thread.detach();
    });
}

void FFakeStereoRenderingHook::subnautica2_uwe_trace_hook_45F890(safetyhook::Context& ctx) {
    subnautica2_uwe_trace_dump("45F890", ctx);
}
void FFakeStereoRenderingHook::subnautica2_uwe_trace_hook_4B2A10(safetyhook::Context& ctx) {
    subnautica2_uwe_trace_dump("4B2A10", ctx);
}
void FFakeStereoRenderingHook::subnautica2_uwe_trace_hook_5665F0(safetyhook::Context& ctx) {
    subnautica2_uwe_trace_dump("5665F0", ctx);
}
void FFakeStereoRenderingHook::subnautica2_uwe_trace_hook_576140(safetyhook::Context& ctx) {
    subnautica2_uwe_trace_dump("576140", ctx);
}
void FFakeStereoRenderingHook::subnautica2_uwe_trace_hook_FD6170(safetyhook::Context& ctx) {
    // Per-view fog integration setup. rdx = FViewInfo*. Dump heavily so we
    // see view 0 vs view 1 differences. This is INSIDE compute_volumetric_fog
    // so we know we're capturing per-view fog work.
    subnautica2_uwe_trace_dump("FD6170", ctx);
}
void FFakeStereoRenderingHook::subnautica2_uwe_trace_hook_FD0A50(safetyhook::Context& ctx) {
    // UWE Ray-Traced Volumetric Fog Pass dispatcher.
    // rcx=FRDGBuilder*, rdx=FViewInfo*, r8=FScene*, r9=FVolumetricFogIntegrationParameterData*,
    // sa5=FRDGTextureRef** (OUTPUT — per-view fog volume handle written here).
    subnautica2_uwe_trace_dump("FD0A50", ctx);
}

static std::atomic<uintptr_t> g_sn2_fd0a50_last_left_output{0};
static std::atomic<uintptr_t> g_sn2_fd0a50_last_left_view{0};

void FFakeStereoRenderingHook::subnautica2_uwe_fd0a50_detail_hook(
    void* graph_builder,
    void* view_info,
    void* scene,
    void* integration_data,
    void** out_texture_ref)
{
    auto* hook = g_hook;
    if (hook == nullptr || !hook->m_subnautica2_uwe_fd0a50_detail_hook) {
        return;
    }

    int view_id = -1;
    uint32_t stereo_pass = 0xffffffffu;
    const uintptr_t view = (uintptr_t)view_info;
    if (view != 0 &&
        is_readable_process_range(view + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET, sizeof(uint32_t)))
    {
        stereo_pass = *(uint32_t*)(view + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET);
        if (stereo_pass == EStereoscopicPass::eSSP_PRIMARY) view_id = 0;
        else if (stereo_pass == EStereoscopicPass::eSSP_SECONDARY) view_id = 1;
    }

    uintptr_t out_before = 0;
    const uintptr_t out_slot = (uintptr_t)out_texture_ref;
    const bool out_slot_writable = out_texture_ref != nullptr &&
        is_writable_process_range(out_slot, sizeof(uintptr_t));
    if (out_slot_writable) {
        out_before = (uintptr_t)(*out_texture_ref);
    }

    hook->m_subnautica2_uwe_fd0a50_detail_hook.unsafe_call<void>(
        graph_builder, view_info, scene, integration_data, out_texture_ref);

    uintptr_t out_after = 0;
    if (out_slot_writable) {
        out_after = (uintptr_t)(*out_texture_ref);
    }

    const uintptr_t v_2650 = view != 0 && is_readable_process_range(view + 0x2650, 8)
        ? *(uintptr_t*)(view + 0x2650) : 0;
    const uintptr_t v_2658 = view != 0 && is_readable_process_range(view + 0x2658, 8)
        ? *(uintptr_t*)(view + 0x2658) : 0;
    const uintptr_t v_2660 = view != 0 && is_readable_process_range(view + 0x2660, 8)
        ? *(uintptr_t*)(view + 0x2660) : 0;

    if (view_id == 0 && out_after != 0) {
        g_sn2_fd0a50_last_left_output.store(out_after, std::memory_order_relaxed);
        g_sn2_fd0a50_last_left_view.store(view, std::memory_order_relaxed);
    } else if (view_id == 1 && out_slot_writable && subnautica2_fd0a50_right_out_from_left()) {
        const uintptr_t left_output = g_sn2_fd0a50_last_left_output.load(std::memory_order_relaxed);
        const uintptr_t left_view = g_sn2_fd0a50_last_left_view.load(std::memory_order_relaxed);
        if (left_output != 0 && left_output != out_after) {
            *out_texture_ref = (void*)left_output;
            static std::atomic<uint64_t> rewrite_count{0};
            const auto rn = rewrite_count.fetch_add(1, std::memory_order_relaxed);
            if (rn < 32 || (rn % 600) == 0) {
                SPDLOG_WARN(
                    "[Subnautica2][FD0A50][RightOutFromLeft] call={} right_view=0x{:x} left_view=0x{:x} out 0x{:x}->0x{:x}",
                    rn + 1, view, left_view, out_after, left_output);
            }
            out_after = left_output;
        }
    }

    static std::atomic<uint64_t> detail_count{0};
    const auto n = detail_count.fetch_add(1, std::memory_order_relaxed);
    if (n < 64 || (n % 600) == 0) {
        SPDLOG_WARN(
            "[Subnautica2][FD0A50][Detail] call={} view_id={} pass={} view=0x{:x} out_slot=0x{:x} out=0x{:x}->0x{:x} view2650=0x{:x} view2658=0x{:x} view2660=0x{:x} graph=0x{:x} scene=0x{:x} integration=0x{:x}",
            n + 1,
            view_id,
            stereo_pass,
            view,
            out_slot,
            out_before,
            out_after,
            v_2650,
            v_2658,
            v_2660,
            (uintptr_t)graph_builder,
            (uintptr_t)scene,
            (uintptr_t)integration_data);
    }
}

// Hooks on the 9 functions that read FEngineShowFlags+0xDF (UWEWaterLighting).
// User-confirmed 2026-05-16: `ShowFlag.UWEWaterLighting 0` makes both eyes match
// (disables the broken-per-view teal effect in left eye). Hooking these lets us
// observe which function actually gates the visible-broken render pass.
void FFakeStereoRenderingHook::subnautica2_uwelit_hook_506DC20(safetyhook::Context& ctx) { subnautica2_uwe_trace_dump("506DC20", ctx); }
void FFakeStereoRenderingHook::subnautica2_uwelit_hook_5BF08A6(safetyhook::Context& ctx) { subnautica2_uwe_trace_dump("5BF08A6", ctx); }
void FFakeStereoRenderingHook::subnautica2_uwelit_hook_5D638D1(safetyhook::Context& ctx) { subnautica2_uwe_trace_dump("5D638D1", ctx); }
void FFakeStereoRenderingHook::subnautica2_uwelit_hook_644EDEC(safetyhook::Context& ctx) { subnautica2_uwe_trace_dump("644EDEC", ctx); }
void FFakeStereoRenderingHook::subnautica2_uwelit_hook_644FA04(safetyhook::Context& ctx) { subnautica2_uwe_trace_dump("644FA04", ctx); }
void FFakeStereoRenderingHook::subnautica2_uwelit_hook_6452806(safetyhook::Context& ctx) { subnautica2_uwe_trace_dump("6452806", ctx); }
void FFakeStereoRenderingHook::subnautica2_uwelit_hook_69D828D(safetyhook::Context& ctx) { subnautica2_uwe_trace_dump("69D828D", ctx); }
void FFakeStereoRenderingHook::subnautica2_uwelit_hook_7FA6BB5(safetyhook::Context& ctx) { subnautica2_uwe_trace_dump("7FA6BB5", ctx); }
void FFakeStereoRenderingHook::subnautica2_uwelit_hook_9131F2A(safetyhook::Context& ctx) { subnautica2_uwe_trace_dump("9131F2A", ctx); }

void FFakeStereoRenderingHook::attempt_hook_subnautica2_uwe_trace() {
    if (m_attempted_hook_subnautica2_uwe_trace) {
        return;
    }
    m_attempted_hook_subnautica2_uwe_trace = true;

    if (!subnautica2_is_current_game()) {
        return;
    }

    const bool broad_uwe_trace = subnautica2_enable_uwe_trace();
    const bool fd0a50_detail = subnautica2_enable_uwe_fd0a50_detail();
    if (!broad_uwe_trace && !fd0a50_detail) {
        SPDLOG_INFO("[Subnautica2][UWE-trace] disabled (set UEVR_SUBNAUTICA2_ENABLE_UWE_TRACE=1 or UEVR_SUBNAUTICA2_ENABLE_UWE_FD0A50_DETAIL=1 to enable)");
        return;
    }

    const auto exe_base = (uintptr_t)utility::get_executable();
    if (exe_base == 0) {
        SPDLOG_WARN("[Subnautica2][UWE-trace] exe_base==0; cannot install");
        return;
    }

    struct TraceTarget {
        uintptr_t rva;
        safetyhook::MidHook FFakeStereoRenderingHook::* slot;
        void (*handler)(safetyhook::Context&);
        const char* tag;
    };
    const TraceTarget targets[] = {
        {SUBNAUTICA2_UWE_TRACE_45F890_RVA, &FFakeStereoRenderingHook::m_subnautica2_uwe_trace_hook_45F890, &FFakeStereoRenderingHook::subnautica2_uwe_trace_hook_45F890, "sub_14045F890"},
        {SUBNAUTICA2_UWE_TRACE_4B2A10_RVA, &FFakeStereoRenderingHook::m_subnautica2_uwe_trace_hook_4B2A10, &FFakeStereoRenderingHook::subnautica2_uwe_trace_hook_4B2A10, "sub_1404B2A10"},
        {SUBNAUTICA2_UWE_TRACE_5665F0_RVA, &FFakeStereoRenderingHook::m_subnautica2_uwe_trace_hook_5665F0, &FFakeStereoRenderingHook::subnautica2_uwe_trace_hook_5665F0, "sub_1405665F0"},
        {SUBNAUTICA2_UWE_TRACE_576140_RVA, &FFakeStereoRenderingHook::m_subnautica2_uwe_trace_hook_576140, &FFakeStereoRenderingHook::subnautica2_uwe_trace_hook_576140, "sub_140576140"},
        {SUBNAUTICA2_UWE_TRACE_FD6170_RVA, &FFakeStereoRenderingHook::m_subnautica2_uwe_trace_hook_FD6170, &FFakeStereoRenderingHook::subnautica2_uwe_trace_hook_FD6170, "sub_142FD6170"},
        {SUBNAUTICA2_UWE_TRACE_FD0A50_RVA, &FFakeStereoRenderingHook::m_subnautica2_uwe_trace_hook_FD0A50, &FFakeStereoRenderingHook::subnautica2_uwe_trace_hook_FD0A50, "sub_142FD0A50"},
    };

    struct UweLitTarget {
        uint32_t bit;
        uintptr_t rva;
        safetyhook::MidHook FFakeStereoRenderingHook::* slot;
        void (*handler)(safetyhook::Context&);
        const char* tag;
    };
    // UWEWaterLighting reader hooks. Enable via UEVR_SUBNAUTICA2_UWELIT_HOOK_MASK.
    // 9-all-at-once hangs the game (cumulative per-frame overhead on hot material
    // draws). Default mask=1 enables only sub_14506DC20 (material permutation
    // builder — called per material instantiation, not per draw).
    const UweLitTarget uwelit_targets[] = {
        {0x001, 0x506DC20, &FFakeStereoRenderingHook::m_subnautica2_uwelit_hook_506DC20, &FFakeStereoRenderingHook::subnautica2_uwelit_hook_506DC20, "sub_14506DC20"},
        {0x002, 0x5BF08A6, &FFakeStereoRenderingHook::m_subnautica2_uwelit_hook_5BF08A6, &FFakeStereoRenderingHook::subnautica2_uwelit_hook_5BF08A6, "sub_145BF08A6"},
        {0x004, 0x5D638D1, &FFakeStereoRenderingHook::m_subnautica2_uwelit_hook_5D638D1, &FFakeStereoRenderingHook::subnautica2_uwelit_hook_5D638D1, "sub_145D638D1"},
        {0x008, 0x644EDEC, &FFakeStereoRenderingHook::m_subnautica2_uwelit_hook_644EDEC, &FFakeStereoRenderingHook::subnautica2_uwelit_hook_644EDEC, "sub_14644EDEC"},
        {0x010, 0x644FA04, &FFakeStereoRenderingHook::m_subnautica2_uwelit_hook_644FA04, &FFakeStereoRenderingHook::subnautica2_uwelit_hook_644FA04, "sub_14644FA04"},
        {0x020, 0x6452806, &FFakeStereoRenderingHook::m_subnautica2_uwelit_hook_6452806, &FFakeStereoRenderingHook::subnautica2_uwelit_hook_6452806, "sub_146452806"},
        {0x040, 0x69D828D, &FFakeStereoRenderingHook::m_subnautica2_uwelit_hook_69D828D, &FFakeStereoRenderingHook::subnautica2_uwelit_hook_69D828D, "sub_1469D828D"},
        {0x080, 0x7FA6BB5, &FFakeStereoRenderingHook::m_subnautica2_uwelit_hook_7FA6BB5, &FFakeStereoRenderingHook::subnautica2_uwelit_hook_7FA6BB5, "sub_147FA6BB5"},
        {0x100, 0x9131F2A, &FFakeStereoRenderingHook::m_subnautica2_uwelit_hook_9131F2A, &FFakeStereoRenderingHook::subnautica2_uwelit_hook_9131F2A, "sub_149131F2A"},
    };
    const uint32_t mask = subnautica2_uwelit_hook_mask();
    SPDLOG_WARN("[Subnautica2][UWE-trace] UWELit hook mask=0x{:x} (UEVR_SUBNAUTICA2_UWELIT_HOOK_MASK)", mask);

    // Spawn background reporter thread (logs per-tag stats every 2s).
    start_uwe_trace_reporter();

    if (broad_uwe_trace) {
        for (const auto& t : targets) {
            if (fd0a50_detail && t.rva == SUBNAUTICA2_UWE_TRACE_FD0A50_RVA) {
                SPDLOG_WARN("[Subnautica2][UWE-trace] skipping FD0A50 mid-hook because detailed inline hook is enabled");
                continue;
            }
            const auto target = exe_base + t.rva;
            if (!is_executable_process_range(target, 0x10)) {
                SPDLOG_WARN("[Subnautica2][UWE-trace] cannot hook {} at {:x} (not executable)", t.tag, target);
                continue;
            }
            auto h = safetyhook::create_mid((void*)target, t.handler);
            if (!h) {
                SPDLOG_WARN("[Subnautica2][UWE-trace] safetyhook::create_mid failed for {} at {:x}", t.tag, target);
                continue;
            }
            this->*(t.slot) = std::move(h);
            SPDLOG_WARN("[Subnautica2][UWE-trace] installed at {} -> {:x}", t.tag, target);
        }
    }
    if (fd0a50_detail) {
        const auto target = exe_base + SUBNAUTICA2_UWE_TRACE_FD0A50_RVA;
        if (!is_executable_process_range(target, 0x10)) {
            SPDLOG_WARN("[Subnautica2][FD0A50] cannot install detailed hook at {:x} (not executable)", target);
        } else {
            m_subnautica2_uwe_fd0a50_detail_hook = safetyhook::create_inline(
                (void*)target,
                &FFakeStereoRenderingHook::subnautica2_uwe_fd0a50_detail_hook,
                safetyhook::InlineHook::StartDisabled);
            if (!m_subnautica2_uwe_fd0a50_detail_hook) {
                SPDLOG_WARN("[Subnautica2][FD0A50] failed to create detailed hook at {:x}", target);
            } else if (auto enable_result = m_subnautica2_uwe_fd0a50_detail_hook.enable(); !enable_result.has_value()) {
                SPDLOG_WARN("[Subnautica2][FD0A50] failed to enable detailed hook at {:x}: {}", target, (int)enable_result.error().type);
            } else {
                SPDLOG_WARN(
                    "[Subnautica2][FD0A50] detailed hook installed at {:x}; right_out_from_left={}",
                    target,
                    subnautica2_fd0a50_right_out_from_left() ? 1 : 0);
            }
        }
    }
    if (broad_uwe_trace) {
        for (const auto& t : uwelit_targets) {
            if ((mask & t.bit) == 0) {
                SPDLOG_INFO("[Subnautica2][UWE-trace] SKIPPED UWELit hook {} (bit 0x{:x} not in mask 0x{:x})", t.tag, t.bit, mask);
                continue;
            }
            const auto target = exe_base + t.rva;
            if (!is_executable_process_range(target, 0x10)) {
                SPDLOG_WARN("[Subnautica2][UWE-trace] cannot hook {} at {:x} (not executable)", t.tag, target);
                continue;
            }
            auto h = safetyhook::create_mid((void*)target, t.handler);
            if (!h) {
                SPDLOG_WARN("[Subnautica2][UWE-trace] safetyhook::create_mid failed for {} at {:x}", t.tag, target);
                continue;
            }
            this->*(t.slot) = std::move(h);
            SPDLOG_WARN("[Subnautica2][UWE-trace] installed UWELit at {} -> {:x} (bit 0x{:x})", t.tag, target, t.bit);
        }
    }
}

bool FFakeStereoRenderingHook::hook_ue418_oculus_pixel_density_sink() {
    if (m_ue418_oculus_pixel_density_hook) {
        return true;
    }

    const auto engine_version = sdk::search_for_version(utility::get_executable()).value_or(L"");
    if (!engine_version.starts_with(L"4.18")) {
        return false;
    }

    // UE4.18 OculusHMD registers a CVar sink that can be called with stale
    // settings after UEVR redirects stereo rendering. Guard the exact old sink
    // before it writes through an invalid FSettings pointer.
    const auto update_pixel_density = utility::scan(
        utility::get_executable(),
        "40 53 48 83 EC 20 80 B9 44 02 00 00 00 48 8B D9 75 ? 65 48 8B 04 25 58 00 00 00");

    if (!update_pixel_density) {
        return false;
    }

    SPDLOG_INFO("[UE4.18 Oculus] Hooking FSettings::UpdatePixelDensityFromScreenPercentage at {:x}", *update_pixel_density);
    m_ue418_oculus_pixel_density_hook = safetyhook::create_inline((void*)*update_pixel_density, &ue418_oculus_update_pixel_density_hook);

    if (!m_ue418_oculus_pixel_density_hook) {
        SPDLOG_WARN("[UE4.18 Oculus] Failed to hook FSettings::UpdatePixelDensityFromScreenPercentage");
        return false;
    }

    return true;
}

bool FFakeStereoRenderingHook::ue418_oculus_update_pixel_density_hook(void* settings) {
    const auto settings_addr = (uintptr_t)settings;

    // The CTTS crash passed a UEVRBackend code/shadow-vtable address as
    // FSettings*. The original function writes floats at +0x238/+0x23c/+0x240.
    if (!is_writable_process_range(settings_addr + 0x238, 0x10)) {
        static std::atomic<uint32_t> suppressed_invalid_calls{};
        const auto count = ++suppressed_invalid_calls;

        if (count == 1 || (count % 120) == 0) {
            SPDLOG_WARN("[UE4.18 Oculus] Suppressed invalid pixel-density sink call settings={:x} count={}",
                        settings_addr, count);
        }

        return true;
    }

    return g_hook->m_ue418_oculus_pixel_density_hook.call<bool>(settings);
}

bool FFakeStereoRenderingHook::hook() {
    SPDLOG_INFO("Entering FFakeStereoRenderingHook::hook");

    m_tried_hooking = true;

    // Locking the hook monitor mutex stops our code from trying to re-hook DX11 and 12 after
    // Long pauses in code execution, due to us doing massive scans for code in this function.
    std::scoped_lock _{g_framework->get_hook_monitor_mutex()};

    hook_ue418_oculus_pixel_density_sink();
    attempt_hook_subnautica2_compute_volumetric_fog();
    attempt_hook_subnautica2_init_volumetric_render_target();
    attempt_hook_subnautica2_reconstruct_volumetric_render_target();
    attempt_hook_subnautica2_compose_volumetric_render_target();
    attempt_hook_subnautica2_render_fog_wrapper();
    attempt_hook_subnautica2_render_fog_pass();
    attempt_hook_subnautica2_render_underwater_fog();
    attempt_hook_subnautica2_setup_volumetric_fog_ub();  // 2026-05-16 FIX-V2 RE-ENABLED: targeted UB handle rewrite
    // 2026-05-17 evening: RE-ENABLED. Per IDA at 0x142fbfa14 there's a JZ that
    // skips the texture-creation block when [view_family+0x20]==0; on those
    // paths r12 stays uninitialized (often 0). The midhook callback already
    // bails on tex_ptr==0, so it only mirrors when r12 holds a valid texture
    // (the view-0 success path). Previously disabled "because r12=0" was a
    // wrong diagnosis: the bail handles that case correctly.
    attempt_hook_subnautica2_lightscat_store_midhook();
    attempt_hook_subnautica2_volumetric_fog_param_trace();
    attempt_hook_subnautica2_fog_alias_thunk();
    attempt_hook_subnautica2_light_affects_view();
    // 2026-05-16: disabled both — caused instability when running under RenderDoc
    attempt_hook_subnautica2_setup_fog_uniform_params();  // FIX-V4: diag cbuffer3
    // attempt_hook_subnautica2_try_add_mesh_batch_probe();
    attempt_hook_subnautica2_basepass_pso_select();
    attempt_hook_subnautica2_basepass_mp_ctor();
    attempt_hook_subnautica2_basepass_cull_threshold();
    attempt_hook_subnautica2_basepass_ps_force_nolm();
    attempt_hook_subnautica2_single_layer_water();
    attempt_hook_subnautica2_single_layer_water_inner();
    attempt_hook_subnautica2_slw_per_view();
    attempt_hook_subnautica2_volumetric_fog_per_view();
    attempt_hook_subnautica2_single_layer_water_scene_without_water();
    attempt_hook_subnautica2_uwe_trace();

    const auto vtable = locate_fake_stereo_rendering_vtable();

    // This happens if games have intentionally removed the stereo initialization functions and stereo emulation classes.
    // So we need to manually create the stereo device.
    if (!vtable) {
        SPDLOG_ERROR("Failed to locate Fake Stereo Rendering VTable, attempting to perform nonstandard hook");

        auto check_file_version = [](uint32_t ms, uint32_t ls) {
            try {
                const auto full_path = utility::get_module_pathw(utility::get_executable());

                if (!full_path) {
                    SPDLOG_ERROR("Failed to get executable path, falling back");
                    return false;
                }

                const auto file_version_size = GetFileVersionInfoSizeW(full_path->c_str(), nullptr);

                if (file_version_size == 0) {
                    SPDLOG_ERROR("Failed to get file version info size, falling back");
                    return false;
                }

                std::vector<uint8_t> file_version_data(file_version_size);
                GetFileVersionInfoW(full_path->c_str(), 0, file_version_size, file_version_data.data());

                UINT size{};
                VS_FIXEDFILEINFO* fixed_file_info{};

                if (VerQueryValueA(file_version_data.data(), "\\", (LPVOID*)&fixed_file_info, &size) && fixed_file_info != nullptr) {
                    SPDLOG_INFO("MS: {:x}, LS: {:x}", fixed_file_info->dwFileVersionMS, fixed_file_info->dwFileVersionLS);

                    if (fixed_file_info->dwFileVersionMS == ms && fixed_file_info->dwFileVersionLS == ls) {
                        SPDLOG_INFO("Found matching executable, attempting to perform nonstandard hook");
                        return true;
                    } else {
                        SPDLOG_INFO("File does not match requested version, falling back");
                    }
                } else {
                    SPDLOG_ERROR("Failed to get file version info, falling back");
                }
            } catch(...) {
                SPDLOG_ERROR("Failed to get file version info, falling back");
            }

            return false;
        };

        const auto found_version = sdk::search_for_version(utility::get_executable());

        if (!found_version) {
            SPDLOG_WARN("Failed to find version in executable");
        }

        // Check for version 4.27.2.0
        // 4.26 also works here
        if (check_file_version(0x4001B, 0x20000) || found_version.value_or(L"") == L"4.26") {
            return nonstandard_create_stereo_device_hook_4_27();
        }

        // Check for version 4.22.3.0
        if (check_file_version(0x40016, 0x30000)) {
            return nonstandard_create_stereo_device_hook_4_22();
        }

        // Check for version 4.18.3.0
        if (check_file_version(0x40012, 0x30000)) {
            return nonstandard_create_stereo_device_hook_4_18();
        }

        return nonstandard_create_stereo_device_hook();
    }

    return standard_fake_stereo_hook(*vtable);
}

bool FFakeStereoRenderingHook::standard_fake_stereo_hook(uintptr_t vtable) {
    ZoneScopedN(__FUNCTION__);
    SPDLOG_INFO("Performing standard fake stereo hook");

    const auto game = sdk::get_ue_module(L"Engine");
    std::array<uint8_t, 0x1000> og_vtable{};
    memcpy(og_vtable.data(), (void*)vtable, og_vtable.size()); // to perform tests on.

    const auto module_vtable_within = utility::get_module_within(vtable);

    // In 4.18 the destructor virtual doesn't exist or is at the very end of the vtable.
    const auto is_stereo_enabled_index = sdk::is_vfunc_pattern(*(uintptr_t*)vtable, "B0 01") ? 0 : 1;
    const auto is_stereo_enabled_func_ptr = &((uintptr_t*)vtable)[is_stereo_enabled_index];

    SPDLOG_INFO("IsStereoEnabled Index: {}", is_stereo_enabled_index);

    const auto stereo_view_offset_index = get_stereo_view_offset_index(vtable);

    if (!stereo_view_offset_index) {
        SPDLOG_ERROR("Failed to locate Stereo View Offset Index");
        return false;
    }

    // Some compiler optimizations cause 31 C0 (xor eax, eax) to be used.
    bool uses_33_c0 = false;

    for (size_t i = 0; i < 30; ++i) try {
        const auto fn = ((uintptr_t*)vtable)[i];

        if (fn == 0 || IsBadReadPtr((void*)fn, sizeof(void*))) {
            SPDLOG_WARN("Found null function pointer at index {}", i);
            break;
        }

        if (sdk::is_vfunc_pattern(fn, "33 C0")) {
            uses_33_c0 = true;
            SPDLOG_INFO("Found 33 C0 pattern at index {}", i);
            break;
        }
    } catch(...) {

    }

    const auto stereo_projection_matrix_index = *stereo_view_offset_index + 1;
    const auto is_4_18_or_lower = *stereo_view_offset_index <= 6;

    const auto& stereo_view_offset_func = ((uintptr_t*)vtable)[*stereo_view_offset_index];

    auto render_texture_render_thread_func = utility::find_virtual_function_from_string_ref(game, L"RenderTexture_RenderThread");

    // Seems more robust than simply just checking the vtable index.
    m_uses_old_rendertarget_manager = *stereo_view_offset_index <= 11 && !render_texture_render_thread_func;

    SPDLOG_INFO("Using old rendertarget manager: {}", m_uses_old_rendertarget_manager);

    if (!render_texture_render_thread_func) {
        // Fallback scan to checking for the first non-default virtual function (<= 4.18)
        SPDLOG_INFO("Failed to find RenderTexture_RenderThread, falling back to first non-default virtual function");

        for (auto i = 2; i < 10; ++i) {
            const auto func = ((uintptr_t*)vtable)[stereo_projection_matrix_index + i];

            // Some protectors can fool this check, so we also check for the vfunc pattern (emulates the code)
            if (!utility::is_stub_code((uint8_t*)func) && 
                !sdk::is_vfunc_pattern(func, "33 C0") &&
                !sdk::is_vfunc_pattern(func, "32 C0"))
            {
                render_texture_render_thread_func = func;
                break;
            }
        }

        if (!render_texture_render_thread_func) {
            SPDLOG_ERROR("Failed to find RenderTexture_RenderThread");
            return false;
        }
    }

    SPDLOG_INFO("RenderTexture_RenderThread: {:x}", (uintptr_t)*render_texture_render_thread_func);

    // Scan for the function pointer, it should be in the middle of the vtable.
    auto rendertexture_fn_vtable_middle = utility::scan_ptr(vtable + ((stereo_projection_matrix_index + 2) * sizeof(void*)), 50 * sizeof(void*), *render_texture_render_thread_func);

    if (!rendertexture_fn_vtable_middle) {
        SPDLOG_ERROR("Failed to find RenderTexture_RenderThread VTable Middle");
        return false;
    }

    auto rendertexture_fn_vtable_index = (*rendertexture_fn_vtable_middle - vtable) / sizeof(uintptr_t);
    SPDLOG_INFO("RenderTexture_RenderThread VTable Middle: {} {:x}", rendertexture_fn_vtable_index, (uintptr_t)*rendertexture_fn_vtable_middle);

    auto render_target_manager_vtable_index = rendertexture_fn_vtable_index + 1 + (2 * (size_t)is_4_18_or_lower);

    // verify first that the render target manager index is returning a null pointer
    // and if not, scan forward until we run into a vfunc that returns a null pointer
    auto get_render_target_manager_func_ptr = &((uintptr_t*)vtable)[render_target_manager_vtable_index];

    bool is_4_11 = false;

    //if (!sdk::is_vfunc_pattern(*(uintptr_t*)get_render_target_manager_func_ptr, "33 C0")) {
        //SPDLOG_INFO("Expected GetRenderTargetManager function at index {} does not return null, scanning forward for return nullptr.", render_target_manager_vtable_index);

        for (;;++render_target_manager_vtable_index) {
            get_render_target_manager_func_ptr = &((uintptr_t*)vtable)[render_target_manager_vtable_index];

            if (IsBadReadPtr(*(void**)get_render_target_manager_func_ptr, 1)) {
                SPDLOG_ERROR("Failed to find GetRenderTargetManager vtable index, a crash is imminent");
                return false;
            }

            if (sdk::is_vfunc_pattern(*(uintptr_t*)get_render_target_manager_func_ptr, "33 C0") || (!uses_33_c0 && sdk::is_vfunc_pattern(*(uintptr_t*)get_render_target_manager_func_ptr, "31 C0"))) {
                const auto distance_from_rendertexture_fn = render_target_manager_vtable_index - rendertexture_fn_vtable_index;

                // means it's 4.17 I think. 12 means 4.11.
                if (distance_from_rendertexture_fn == 10 || distance_from_rendertexture_fn == 11 || distance_from_rendertexture_fn == 12) {
                    is_4_11 = distance_from_rendertexture_fn == 12;
                    m_rendertarget_manager_embedded_in_stereo_device = true;
                    SPDLOG_INFO("Render target manager appears to be directly embedded in the stereo device vtable");
                } else {
                    // Now this may potentially be the correct index, but we're not quite done yet.
                    // On 4.19 (and possibly others), the index is 1 higher than it should be.
                    // We can tell by checking how many functions in front of this index return null.
                    // if there are two functions in front of this index that return null, we need to add 1 to the index.
                    SPDLOG_INFO("Found potential GetRenderTargetManager function at index {}", render_target_manager_vtable_index);
                    SPDLOG_INFO("Double checking GetRenderTargetManager index...");

                    int32_t count = 0;
                    for (auto i = render_target_manager_vtable_index + 1; i < render_target_manager_vtable_index + 5; ++i) {
                        const auto addr_of_func = (uintptr_t)&((uintptr_t*)vtable)[i];
                        const auto func = ((uintptr_t*)vtable)[i];

                        if (func == 0 || IsBadReadPtr((void*)func, 1)) {
                            break;
                        }

                        // Make sure we didn't cross over into another vtable's boundaries.
                        const auto module_within = utility::get_module_within(addr_of_func);

                        if (module_within && utility::scan_displacement_reference(*module_within, addr_of_func)) {
                            SPDLOG_INFO("Crossed over into another vtable's boundaries, aborting double check");
                            SPDLOG_INFO("Reached end of double check at index {}, {} appears to be the correct index.", i, render_target_manager_vtable_index);
                            break;
                        }

                        if (!sdk::is_vfunc_pattern(func, "33 C0") && !sdk::is_vfunc_pattern(func, "31 C0")) {
                            SPDLOG_INFO("Reached end of double check at index {}, {} appears to be the correct index.", i, render_target_manager_vtable_index);
                            break;
                        }

                        if (++count >= 2) {
                            ++render_target_manager_vtable_index;
                            get_render_target_manager_func_ptr = &((uintptr_t*)vtable)[render_target_manager_vtable_index];

                            SPDLOG_INFO("Adjusted GetRenderTargetManager index to {}", render_target_manager_vtable_index);
                            break;
                        }
                    }

                    SPDLOG_INFO("Distance: {}", distance_from_rendertexture_fn);
                }

                break;
            } else {
                try {
                    using GetRenderTargetManagerFn = IStereoRenderTargetManager* (*)(void*, void*, void*, void*, void*, void*, void*, void*);
                    const auto func = (GetRenderTargetManagerFn)(*get_render_target_manager_func_ptr);
    
                    // On UE5.5+ FFakeStereoRendering has a valid GetRenderTargetManager that doesn't return null.
                    if (!is_4_18_or_lower && func(og_vtable.data(), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) == (IStereoRenderTargetManager*)&og_vtable[sizeof(void*)]) {
                        m_uses_old_rendertarget_manager = false; // nope
                        SPDLOG_INFO("Found UE5.5+ variant of GetRenderTargetManager function at index {}", render_target_manager_vtable_index);
                        SPDLOG_INFO("GetRenderTargetManager function at index {} appears to be valid.", render_target_manager_vtable_index);
                        break;
                    }
                } catch(...) {
                    SPDLOG_WARN("Unknown exception while checking GetRenderTargetManager function at index {}", render_target_manager_vtable_index);
                }
            }
        }
    //} else {
        //SPDLOG_INFO("GetRenderTargetManager function at index {} appears to be valid.", render_target_manager_vtable_index);
    //}
    
    const auto get_stereo_layers_func_ptr = (uintptr_t)(get_render_target_manager_func_ptr + sizeof(void*));

    if (get_render_target_manager_func_ptr == 0) {
        SPDLOG_ERROR("Failed to find GetRenderTargetManager");
        return false;
    }

    if (get_stereo_layers_func_ptr == 0) {
        SPDLOG_ERROR("Failed to find GetStereoLayers");
        return false;
    }

    SPDLOG_INFO("GetRenderTargetManagerptr: {:x}", (uintptr_t)get_render_target_manager_func_ptr);
    SPDLOG_INFO("GetStereoLayersptr: {:x}", (uintptr_t)get_stereo_layers_func_ptr);

    const auto adjust_view_rect_distance = is_4_18_or_lower ? 2 : 3;
    const auto adjust_view_rect_index = *stereo_view_offset_index - adjust_view_rect_distance;
    const auto set_final_view_rect_index = adjust_view_rect_index + 1;

    SPDLOG_INFO("AdjustViewRect Index: {}", adjust_view_rect_index);
    SPDLOG_INFO("SetFinalViewRect Index: {}", set_final_view_rect_index);
    
    auto calculate_stereo_projection_matrix_index = *stereo_view_offset_index + 1;

    // While generally most of the time the stereo projection matrix func is the next one after the stereo view offset func,
    // it's not always the case. We can scan for a call to the tanf function in one of the virtual functions to find it.
    for (auto i = 0; i < 10; ++i) {
        const auto potential_func = ((uintptr_t*)vtable)[calculate_stereo_projection_matrix_index + i];
        if (potential_func == 0 || IsBadReadPtr((void*)potential_func, 1) || utility::is_stub_code((uint8_t*)potential_func)) {
            continue;
        }

        auto ip = (uint8_t*)potential_func;
        if (*(uint8_t*)ip == 0xE9) {
            ip = (uint8_t*)utility::calculate_absolute(potential_func + 1);
            SPDLOG_INFO("Found JMP at {:x}, jumping to {:x}", (uintptr_t)potential_func, (uintptr_t)ip);
        }

        bool found = false;

        SPDLOG_INFO("Scanning {:x}...", (uintptr_t)ip);

        for (auto j = 0; j < 50; ++j) {
            INSTRUX ix{};

            const auto status = NdDecodeEx(&ix, (ND_UINT8*)ip, 1000, ND_CODE_64, ND_DATA_64);

            if (!ND_SUCCESS(status)) {
                SPDLOG_INFO("Decoding failed with error {:x}!", (uint32_t)status);
                break;
            }

            if (ix.Category == ND_CAT_RET || ix.InstructionBytes[0] == 0xE9) {
                SPDLOG_INFO("Encountered RET or JMP at {:x}, aborting scan", (uintptr_t)ip);
                break;
            }

            if (ix.InstructionBytes[0] == 0xE8) {
                auto called_func = (uintptr_t)(ip + ix.Length + (int32_t)ix.RelativeOffset);
                auto inner_ins = utility::decode_one((uint8_t*)called_func);

                SPDLOG_INFO("called {:x}", (uintptr_t)called_func);
                uintptr_t final_func = 0;

                // Fully resolve the pointer jmps until we reach another module.
                while (inner_ins && inner_ins->InstructionBytes[0] == 0xFF && inner_ins->InstructionBytes[1] == 0x25) {
                    const auto called_func_ptr = (uintptr_t*)(called_func + inner_ins->Length + (int32_t)inner_ins->Displacement);
                    const auto called_func_ptr_val = *called_func_ptr;

                    SPDLOG_INFO("called ptr {:x}", (uintptr_t)called_func_ptr_val);

                    inner_ins = utility::decode_one((uint8_t*)called_func_ptr_val);
                    final_func = called_func_ptr_val;
                    called_func = called_func_ptr_val;
                }

                // Check if this function is jmping into the "tanf" export in ucrtbase.dll
                if (final_func != 0) {
                    const auto module_within = utility::get_module_within(final_func);

                    if (module_within &&
                        (final_func == (uintptr_t)GetProcAddress(*module_within, "tanf") ||
                        final_func == (uintptr_t)GetProcAddress(*module_within, "tan"))) 
                    {
                        SPDLOG_INFO("Found CalculateStereoProjectionMatrix: {} {:x}", calculate_stereo_projection_matrix_index + i, potential_func);
                        calculate_stereo_projection_matrix_index += i;
                        found = true;
                        break;
                    } else {
                        SPDLOG_INFO("Function did not call tanf, skipping");
                    }
                } else {
                    SPDLOG_INFO("Failed to resolve inner pointer");
                }
            }

            ip += ix.Length;
        }

        if (found) {
            break;
        }
    }

    const auto init_canvas_index = calculate_stereo_projection_matrix_index + 1;

    const auto adjust_view_rect_func = ((uintptr_t*)vtable)[adjust_view_rect_index];
    const auto set_final_view_rect_func = ((uintptr_t*)vtable)[set_final_view_rect_index];
    const auto calculate_stereo_projection_matrix_func = ((uintptr_t*)vtable)[calculate_stereo_projection_matrix_index];
    const auto init_canvas_func_ptr = &((uintptr_t*)vtable)[init_canvas_index];
    // const auto render_texture_render_thread_func = ((uintptr_t*)*vtable)[*stereo_view_offset_index + 3];
    

    SPDLOG_INFO("AdjustViewRect: {:x}", (uintptr_t)adjust_view_rect_func);
    SPDLOG_INFO("SetFinalViewRect: {:x}", (uintptr_t)set_final_view_rect_func);
    SPDLOG_INFO("CalculateStereoProjectionMatrix: {:x}", (uintptr_t)calculate_stereo_projection_matrix_func);
    SPDLOG_INFO("CalculateStereoViewOffset: {:x}", (uintptr_t)stereo_view_offset_func);
    SPDLOG_INFO("IsStereoEnabled: {:x}", (uintptr_t)*is_stereo_enabled_func_ptr);

    m_has_double_precision = is_using_double_precision(stereo_view_offset_func) || is_using_double_precision(calculate_stereo_projection_matrix_func);

    {
        m_adjust_view_rect_hook = safetyhook::create_inline((void*)adjust_view_rect_func, adjust_view_rect);
        m_set_final_view_rect_hook = safetyhook::create_inline((void*)set_final_view_rect_func, set_final_view_rect);
        m_calculate_stereo_view_offset_hook_inline = safetyhook::create_inline((void*)stereo_view_offset_func, calculate_stereo_view_offset);
        m_calculate_stereo_projection_matrix_hook = safetyhook::create_inline((void*)calculate_stereo_projection_matrix_func, calculate_stereo_projection_matrix);
    }
    
    if (!m_adjust_view_rect_hook) {
        SPDLOG_ERROR("Failed to create AdjustViewRect hook");
    }

    if (!m_set_final_view_rect_hook) {
        SPDLOG_ERROR("Failed to create SetFinalViewRect hook");
    }

    if (!m_calculate_stereo_view_offset_hook_inline) {
        SPDLOG_ERROR("Failed to create CalculateStereoViewOffset hook, falling back to pointer hook");
        m_calculate_stereo_view_offset_hook_ptr = std::make_unique<PointerHook>((void**)&stereo_view_offset_func, (void*)calculate_stereo_view_offset);
    }

    if (!m_calculate_stereo_projection_matrix_hook) {
        SPDLOG_ERROR("Failed to create CalculateStereoProjectionMatrix hook");
    }

    // This requires a pointer hook because the virtual just returns false
    // compiler optimization makes that function get re-used in a lot of places
    // so it's not feasible to just detour it, we need to replace the pointer in the vtable.
    if (!m_rendertarget_manager_embedded_in_stereo_device) {
        m_render_texture_render_thread_hook = safetyhook::create_inline((void*)*render_texture_render_thread_func, render_texture_render_thread);

        if (!m_render_texture_render_thread_hook) {
            SPDLOG_ERROR("Failed to create RenderTexture_RenderThread hook");
        }

        // Seems to exist in 4.18+
        m_get_render_target_manager_hook = std::make_unique<PointerHook>((void**)get_render_target_manager_func_ptr, (void*)&get_render_target_manager_hook);
    } else {
        // When the render target manager is embedded in the stereo device, it just means
        // that all of the virtuals are now part of FFakeStereoRendering
        // instead of being a part of IStereoRenderTargetManager and being returned via GetRenderTargetManager.
        // Only seen in 4.17 and below.
        SPDLOG_INFO("Performing hooks on embedded RenderTargetManager");

        // Scan forward from the alleged RenderTexture_RenderThread function to find the
        // real RenderTexture_RenderThread function, because it is different when the
        // render target manager is embedded in the stereo device.
        // When it's embedded, it seems like it's the first function right after
        // a set of functions that return false sequentially.
        bool prev_function_returned_false = false;

        for (auto i = rendertexture_fn_vtable_index + 1; i < 100; ++i) {
            const auto func = ((uintptr_t*)og_vtable.data())[i];

            if (func == 0 || IsBadReadPtr((void*)func, 3)) {
                SPDLOG_ERROR("Failed to find real RenderTexture_RenderThread");
                return false;
            }
            
            if (sdk::is_vfunc_pattern(func, "32 C0")) {
                prev_function_returned_false = true;
            } else {
                if (prev_function_returned_false) {
                    render_texture_render_thread_func = func;
                    rendertexture_fn_vtable_index = i;
                    m_render_texture_render_thread_hook = safetyhook::create_inline((void*)*render_texture_render_thread_func, render_texture_render_thread);
                    if (!m_render_texture_render_thread_hook) {
                        SPDLOG_ERROR("Failed to create RenderTexture_RenderThread hook");
                    }
                    SPDLOG_INFO("Real RenderTexture_RenderThread: {} {:x}", rendertexture_fn_vtable_index, (uintptr_t)*render_texture_render_thread_func);
                    break;
                }

                prev_function_returned_false = false;
            }
        }

        // Scan backwards from RenderTexture_RenderThread for the first virtual that just returns
        int32_t calculate_render_target_size_index = 0;

        for (auto i = rendertexture_fn_vtable_index - 1; i > 0; --i) {
            const auto func = ((uintptr_t*)og_vtable.data())[i];

            if (func == 0 || IsBadReadPtr((void*)func, 3)) {
                SPDLOG_ERROR("Failed to find calculate render target size index, falling back to hardcoded index");
                calculate_render_target_size_index = rendertexture_fn_vtable_index - 3;
                break;
            }

            if (sdk::is_vfunc_pattern(func, "C3") || sdk::is_vfunc_pattern(func, "C2 00 00")) {
                SPDLOG_INFO("Dynamically found CalculateRenderTargetSize index: {}", i);
                calculate_render_target_size_index = i;
                break;
            }
        }

        const auto calculate_render_target_size_func_ptr = &((uintptr_t*)vtable)[calculate_render_target_size_index];
        SPDLOG_INFO("CalculateRenderTargetSize index: {}", calculate_render_target_size_index);

        // To be seen if this one needs automated analysis
        const auto need_reallocate_viewport_render_target_index = calculate_render_target_size_index + 1;
        const auto need_reallocate_viewport_render_target_func_ptr = &((uintptr_t*)vtable)[need_reallocate_viewport_render_target_index];

        // To be seen if this one needs automated analysis
        const auto should_use_separate_render_target_index = calculate_render_target_size_index + 2;
        const auto should_use_separate_render_target_func_ptr = &((uintptr_t*)vtable)[should_use_separate_render_target_index];

        // Log a warning if NeedReallocateViewportRenderTarget or ShouldUseSeparateRenderTarget are not
        // functions that plainly return false, but do not fail entirely.
        bool need_reallocate_viewport_render_target_is_bad = false;
        bool should_use_separate_render_target_is_bad = false;

        if (!sdk::is_vfunc_pattern(*need_reallocate_viewport_render_target_func_ptr, "32 C0")) {
            SPDLOG_WARN("NeedReallocateViewportRenderTarget is not a function that returns false");
            need_reallocate_viewport_render_target_is_bad = true;
        }

        if (!sdk::is_vfunc_pattern(*should_use_separate_render_target_func_ptr, "32 C0")) {
            SPDLOG_WARN("ShouldUseSeparateRenderTarget is not a function that returns false");
            should_use_separate_render_target_is_bad = true;
        }

        SPDLOG_INFO("NeedReallocateViewportRenderTarget index: {}", need_reallocate_viewport_render_target_index);
        SPDLOG_INFO("ShouldUseSeparateRenderTarget index: {}", should_use_separate_render_target_index);

        // Scan forward from RenderTexture_RenderThread for the first virtual that returns false
        int32_t allocate_render_target_index = 0;

        for (auto i = rendertexture_fn_vtable_index + 1; i < 100; ++i) {
            const auto func = ((uintptr_t*)og_vtable.data())[i];

            if (func == 0 || IsBadReadPtr((void*)func, 3)) {
                SPDLOG_ERROR("Failed to find allocate render target index, falling back to hardcoded index");
                allocate_render_target_index = render_target_manager_vtable_index + 3;
                break;
            }

            if (sdk::is_vfunc_pattern(func, "32 C0")) {
                SPDLOG_INFO("Dynamically found AllocateRenderTarget index: {}", i);
                allocate_render_target_index = i;
                break;
            }
        }

        const auto allocate_render_target_func_ptr = &((uintptr_t*)vtable)[allocate_render_target_index];
        SPDLOG_INFO("AllocateRenderTarget index: {}", allocate_render_target_index);

        m_embedded_rtm.calculate_render_target_size_hook = 
            std::make_unique<PointerHook>((void**)calculate_render_target_size_func_ptr, +[](void* self, const sdk::FViewport& viewport, uint32_t& x, uint32_t& y) {
            #ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
                SPDLOG_INFO("CalculateRenderTargetSize (embedded)");
            #else
                SPDLOG_INFO_ONCE("CalculateRenderTargetSize (embedded)");
            #endif

                return g_hook->get_render_target_manager()->calculate_render_target_size(viewport, x, y);
            }
        );

        m_embedded_rtm.allocate_render_target_texture_hook = 
            std::make_unique<PointerHook>((void**)allocate_render_target_func_ptr, +[](void* self, 
                uint32_t index, uint32_t w, uint32_t h, uint8_t format, uint32_t num_mips,
                ETextureCreateFlags lags, ETextureCreateFlags targetable_texture_flags, FTexture2DRHIRef& out_texture,
                FTexture2DRHIRef& out_shader_resource, uint32_t num_samples) -> bool {
            #ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
                SPDLOG_INFO("AllocateRenderTargetTexture (embedded): {:x}", (uintptr_t)_ReturnAddress());
            #else
                SPDLOG_INFO_ONCE("AllocateRenderTargetTexture (embedded): {:x}", (uintptr_t)_ReturnAddress());
            #endif

                return g_hook->get_render_target_manager()->allocate_render_target_texture((uintptr_t)_ReturnAddress(), &out_texture, &out_shader_resource);
            }
        );
    
        m_embedded_rtm.should_use_separate_render_target_hook = 
            std::make_unique<PointerHook>((void**)should_use_separate_render_target_func_ptr, +[](void* self) -> bool {
            #ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
                SPDLOG_INFO("ShouldUseSeparateRenderTarget (embedded): {:x}", (uintptr_t)_ReturnAddress());
            #else
                SPDLOG_INFO_ONCE("ShouldUseSeparateRenderTarget (embedded): {:x}", (uintptr_t)_ReturnAddress());
            #endif
            
                auto vr = VR::get();

                if (vr->is_extreme_compatibility_mode_enabled()) {
                    return false;
                }

                if (vr->is_hmd_active() && !vr->is_stereo_emulation_enabled()) {
                    g_hook->get_embedded_rtm().should_use_separate_rt_called = true;
                    return true;
                }

                return false;
            }
        );

        if (!need_reallocate_viewport_render_target_is_bad) {
            m_embedded_rtm.need_reallocate_viewport_render_target_hook = 
                std::make_unique<PointerHook>((void**)need_reallocate_viewport_render_target_func_ptr, +[](void* self, sdk::FViewport* viewport) -> bool {
                #ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
                    SPDLOG_INFO("NeedReallocateViewportRenderTarget (embedded): {:x}", (uintptr_t)_ReturnAddress());
                #else
                    SPDLOG_INFO_ONCE("NeedReallocateViewportRenderTarget (embedded): {:x}", (uintptr_t)_ReturnAddress());
                #endif

                    if (g_hook->get_render_target_manager()->need_reallocate_view_target(*viewport)) {
                        g_hook->get_embedded_rtm().need_reallocate_viewport_render_target_called = true;
                        g_hook->get_embedded_rtm().last_time_needed_hmd_reallocate = std::chrono::steady_clock::now();
                        return true;
                    }

                    return false;
                }
            );
        }
    }
    
    m_is_stereo_enabled_hook = std::make_unique<PointerHook>((void**)is_stereo_enabled_func_ptr, (void*)&is_stereo_enabled);

    // scan for GetDesiredNumberOfViews function, we use this function to perform AFR if needed
    SPDLOG_INFO("Searching for GetDesiredNumberOfViews function...");
    std::optional<uint32_t> get_desired_number_of_views_index{};

    for (auto i = 1; i < 20; ++i) {
        auto func_ptr = &((uintptr_t*)vtable)[i];

        if (IsBadReadPtr((void*)*func_ptr, sizeof(void*))) {
            SPDLOG_INFO("Could not locate GetDesiredNumberOfViews function, this is okay, not really needed");
            break;
        }

        // pretty consistent patterns
        if (sdk::is_vfunc_pattern(*func_ptr, "0F B6 C2 FF C0 C3") ||
            sdk::is_vfunc_pattern(*func_ptr, "33 C0 84 D2 0F 95 C0 FF C0 C3") || 
            sdk::is_vfunc_pattern(*func_ptr, "84 D2 74 04 8B 41 ? C3 B8 01") ||
            sdk::is_vfunc_pattern(*func_ptr, "B8 01 00 00 00 84 D2 74 03 8B 41 ? C3"))
        {
            SPDLOG_INFO("Found GetDesiredNumberOfViews function at index: {}", i);
            get_desired_number_of_views_index = i;
            m_get_desired_number_of_views_hook = std::make_unique<PointerHook>((void**)func_ptr, (void*)&get_desired_number_of_views_hook);
            break;
        }
    }

    // If double precision detected, it means it's >= UE 5.0.3
    if (m_has_double_precision && get_desired_number_of_views_index) {
        SPDLOG_INFO("Searching for GetViewPassForIndex function...");

        // Pretty simple, it's at +1, to be seen if this needs automation
        const auto get_view_pass_for_index_index = *get_desired_number_of_views_index + 1;

        auto func_ptr = &((uintptr_t*)vtable)[get_view_pass_for_index_index];

        if (IsBadReadPtr((void*)*func_ptr, sizeof(void*))) {
            SPDLOG_INFO("Could not locate GetViewPassForIndex function. A crash may occur.");
        } else {
            SPDLOG_INFO("Found GetViewPassForIndex function at index: {}", get_view_pass_for_index_index);
            m_get_view_pass_for_index_hook = std::make_unique<PointerHook>((void**)func_ptr, (void*)&get_view_pass_for_index_hook);
        }
    } else if (m_has_double_precision) {
        SPDLOG_INFO("Could not locate GetViewPassForIndex function because GetDesiredNumberOfViews function was not found. A crash may occur.");
    }

    SPDLOG_INFO("Leaving FFakeStereoRenderingHook::hook");

    const auto renderer_module = sdk::get_ue_module(L"Renderer");
    const auto backbuffer_format_cvar = sdk::find_cvar_by_description(L"Defines the default back buffer pixel format.", L"r.DefaultBackBufferPixelFormat", 4, renderer_module);
    m_pixel_format_cvar_found = backbuffer_format_cvar.has_value();

    // In 4.18 this doesn't exist. Not much we can do about that.
    if (backbuffer_format_cvar) {
        SPDLOG_INFO("Backbuffer Format CVar: {:x}", (uintptr_t)*backbuffer_format_cvar);
        *(int32_t*)(*(uintptr_t*)*backbuffer_format_cvar + 0) = 0;   // 8bit RGBA, which is what VR headsets support
        *(int32_t*)(*(uintptr_t*)*backbuffer_format_cvar + 0x4) = 0; // 8bit RGBA, which is what VR headsets support
    } else {
        SPDLOG_ERROR("Failed to find backbuffer format cvar, continuing anyways...");
    }

    // make a shadow copy of FFakeStereoRendering's vtable to get past weird compiler optimizations
    // that cause the hook to not work, reason being that the compiler will optimize
    // if the vtable pointer is equal to the original vtable pointer, and it will
    // not call the hook function, so we make a shadow copy of the vtable
    auto active_stereo_device = locate_active_stereo_rendering_device();
    
    // We need to manually insert a stereo device at this point if it's not already.
    // This is what the "nonstandard" hooks did, but those did not have access to FFakeStereoRendering's vtable.
    // All we need to do in this instance is get the engine offset to the stereo device, create a fake pointer with our own vtable,
    // and just overwrite the engine's (null) stereo device pointer with our fake one.
    // It is very rare that this should need to be done.
    if (!active_stereo_device) {
        SPDLOG_INFO("Attempting to create a stereo device without InitializeHMDDevice...");
        const auto device_offset = sdk::UEngine::get_stereo_rendering_device_offset();

        if (device_offset) {
            auto engine = sdk::UGameEngine::get();

            if (engine != nullptr) {
                m_fallback_device.vtable = (void*)vtable;
                *(uintptr_t*)((uintptr_t)engine + *device_offset) = (uintptr_t)&m_fallback_device;

                active_stereo_device = (uintptr_t)&m_fallback_device;
                s_stereo_rendering_device_offset = *device_offset; // Set it up if it's not already
            }
        } else {
            SPDLOG_ERROR("Could not create a new stereo device, VR may not work!");
        }
    }

    if (active_stereo_device) {
        SPDLOG_INFO("Found active stereo device: {:x}", (uintptr_t)*active_stereo_device);
        SPDLOG_INFO("Overwriting vtable...");

        static std::vector<uintptr_t> shadow_vtable{};
        auto& vtable = *(uintptr_t**)*active_stereo_device;

        for (auto i = 0; i < 100; i++) {
            shadow_vtable.push_back(vtable[i]);
        }

        vtable = shadow_vtable.data();
    } else {
        SPDLOG_INFO("Current stereo device is null, cannot overwrite vtable");
        patch_vtable_checks(); // fallback to patching vtable checks
    }

    setup_view_extensions();
    hook_game_viewport_client();

    m_finished_hooking = true;

    SPDLOG_INFO("Finished hooking FFakeStereoRendering!");

    return true;
}

bool FFakeStereoRenderingHook::nonstandard_create_stereo_device_hook() {
    // This may only work on one game for now, but it should be a good placeholder
    // for creating a stereo device for games that don't have one.
    // We can figure out how to make it work for other games when we run into one
    // that needs this same functionality.

    // The reason why this function is needed is because in the one game that
    // the FFakeStereoRenderingHook doesn't work through the standard method,
    // is because the VR pipeline seems to have been heavily modified,
    // and so the -emulatestereo command line argument doesn't work, and
    // the FFakeStereoRendering vtable does not seem to exist
    // However the StereoRenderingDevice within GEngine seems to still exist
    // so we can take advantage of that and create our own stereo device
    // the downside is it will be much more difficult to figure out the 
    // proper vtable indices for the functions we need to hook
    // and we will need to actually implement some of the functions
    SPDLOG_INFO("Attempting to create a stereo device for the game using nonstandard method");
    m_fallback_vtable.resize(30);

    // Give all of the functions placeholders.
    for (auto i = 0; i < m_fallback_vtable.size(); ++i) {
        m_fallback_vtable[i] = +[](FFakeStereoRendering* stereo) -> void* {
            return nullptr;
        };
    }

    // Actually implement the ones we care about now.
    auto idx = 0;
    //m_fallback_vtable[idx++] = +[](FFakeStereoRendering* stereo) -> void { SPDLOG_INFO("Destructor called?");  }; // destructor.
    m_fallback_vtable[idx++] = +[](FFakeStereoRendering* stereo) -> bool { 
#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
        SPDLOG_INFO("IsStereoEnabled called: {:x}", (uintptr_t)_ReturnAddress());
#endif

        return g_hook->is_stereo_enabled(stereo); 
    }; // IsStereoEnabled
    m_fallback_vtable[idx++] = +[](FFakeStereoRendering* stereo) -> bool { return g_hook->is_stereo_enabled(stereo); }; // IsStereoEnabledOnNextFrame
    m_fallback_vtable[idx++] = +[](FFakeStereoRendering* stereo) -> bool { return g_hook->is_stereo_enabled(stereo); }; // EnableStereo

    m_fallback_vtable[idx++] = +[](FFakeStereoRendering* stereo, int32_t index, int* x, int* y, uint32_t* w, uint32_t* h) { 
        return g_hook->adjust_view_rect(stereo, index, x, y, w, h);
    }; // AdjustViewRect


    ++idx; // idk waht this is.

    // in this version the index is passed...?
    /*m_fallback_vtable[idx++] = +[](FFakeStereoRendering* stereo, uint32_t index, Vector2f* bounds) {
#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
        SPDLOG_INFO("GetTextSafeRegionBounds called");
#endif

        bounds->x = 0.75f;
        bounds->y = 0.75f;

        return bounds;
    };*/ // GetTextSafeRegionBounds

    m_fallback_vtable[idx++] = 
    +[](FFakeStereoRendering* stereo, const int32_t view_index, Rotator<float>* view_rotation, const float world_to_meters, Vector3f* view_location) {
        return g_hook->calculate_stereo_view_offset(stereo, view_index, view_rotation, world_to_meters, view_location);
    }; // CalculateStereoViewOffset

    
    idx++;

    m_fallback_vtable[idx++] = +[](FFakeStereoRendering* stereo, Matrix4x4f* out, const int32_t view_index) {
#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
        SPDLOG_INFO("CalculateStereoProjectionMatrix called: {:x} {} {:x}", (uintptr_t)_ReturnAddress(), view_index, (uintptr_t)out);
#endif

        if (!g_hook->m_has_double_precision) {
            (*out)[3][2] = 0.1f; // Need to pre-set the Z value to something, otherwise it will be 0.0f & probably break something.
        } else {
            auto dmat = (Matrix4x4d*)out;
            (*dmat)[3][2] = 0.1;
        }

        return g_hook->calculate_stereo_projection_matrix(stereo, out, view_index);
    }; // CalculateStereoProjectionMatrix

    m_fallback_vtable[idx++] = +[](FFakeStereoRendering* stereo, void* a2) {
        // do nothing
    }; // not sure what this one is. think it sets the FOV. Not present in newer UE4 versions.

    idx++; // just leave this one as a placeholder for now. Returns false.

    m_fallback_vtable[idx++] = 
    +[](FFakeStereoRendering* stereo, FRHICommandListImmediate* rhi_command_list, FRHITexture2D* backbuffer, FRHITexture2D* src_texture, double window_size) {
        return g_hook->render_texture_render_thread(stereo, rhi_command_list, backbuffer, src_texture, window_size);
    };

    idx++; // just leave this one as a placeholder for now. Probably SetClippingPlanes.

    m_fallback_vtable[13] = +[](FFakeStereoRendering* stereo) { return g_hook->get_render_target_manager_hook(stereo); }; // GetRenderTargetManager
    //m_fallback_vtable[13] = +[](FFakeStereoRendering* stereo) { return nullptr; }; // GetRenderTargetManager

    auto engine = sdk::UEngine::get();

    if (engine == nullptr) {
        SPDLOG_ERROR("Failed to get engine pointer! Cannot create stereo device!");
        return false;
    }

    //m_418_detected = true;
    m_special_detected = true;
    m_manually_constructed = true;
    m_fallback_device.vtable = m_fallback_vtable.data();

    auto stereo_rendering_device_offset = sdk::UEngine::get_stereo_rendering_device_offset();
    if (!stereo_rendering_device_offset) {
        if (avowed_is_current_game()) {
            SPDLOG_ERROR("[Avowed] StereoRenderingDevice offset discovery failed; refusing legacy 0xAC8 nonstandard fallback");
            return false;
        }

        stereo_rendering_device_offset = 0xAC8; // fallback for the engine this was originally made for.
    }

    *(uintptr_t*)((uintptr_t)engine + *stereo_rendering_device_offset) = (uintptr_t)&m_fallback_device; // TODO: Automatically find this offset.

    // So the view extension hook will work.
    s_stereo_rendering_device_offset = *stereo_rendering_device_offset;

    hook_game_viewport_client();
    setup_view_extensions();

    SPDLOG_INFO("Finished creating stereo device for the game using nonstandard method");

    m_finished_hooking = true;

    return true;
}

bool FFakeStereoRenderingHook::nonstandard_create_stereo_device_hook_4_27() {
    SPDLOG_INFO("Attempting to create a stereo device for the game using nonstandard method (4.27)");

    auto engine = sdk::UEngine::get();

    if (engine == nullptr) {
        SPDLOG_ERROR("Failed to get engine pointer! Cannot create stereo device!");
        return false;
    }

    m_fallback_vtable.resize(30);

    // Give all of the functions placeholders.
    for (auto i = 0; i < m_fallback_vtable.size(); ++i) {
        m_fallback_vtable[i] = +[](FFakeStereoRendering* stereo) -> void* {
            return nullptr;
        };
    }

    constexpr auto DESTRUCTOR_INDEX = 0;
    constexpr auto IS_STEREO_ENABLED_INDEX = 1;
    constexpr auto IS_STEREO_ENABLED_ON_NEXT_FRAME_INDEX = 2;
    constexpr auto ENABLE_STEREO_INDEX = 3;

    constexpr auto GET_DESIRED_NUMBER_OF_VIEWS_INDEX = 4;
    constexpr auto GET_VIEW_PASS_FOR_INDEX_INDEX = 5;
    constexpr auto GET_VIEW_INDEX_FOR_PASS_INDEX = 6;

    constexpr auto DEVICE_IS_STEREO_EYE_PASS_INDEX = 7;
    constexpr auto DEVICE_IS_STEREO_EYE_VIEW_INDEX = 8;
    constexpr auto DEVICE_IS_A_PRIMARY_PASS_INDEX = 9;
    constexpr auto DEVICE_IS_A_PRIMARY_VIEW_INDEX = 10;
    constexpr auto DEVICE_IS_A_SECONDARY_PASS_INDEX = 11;
    constexpr auto DEVICE_IS_A_SECONDARY_VIEW_INDEX = 12;
    constexpr auto DEVICE_IS_AN_ADDITIONAL_PASS_INDEX = 13; // not necessary...?
    constexpr auto DEVICE_IS_AN_ADDITIONAL_VIEW_INDEX = 14; // not necessary...?
    constexpr auto DEVICE_GET_LOD_VIEW_INDEX_INDEX = 15; // not necessary...?

    constexpr auto ADJUST_VIEW_RECT_INDEX = 16;
    constexpr auto CALCULATE_STEREO_VIEW_OFFSET_INDEX = 19;
    constexpr auto CALCULATE_STEREO_PROJECTION_MATRIX_INDEX = 20;
    constexpr auto RENDER_TEXTURE_RENDER_THREAD_INDEX = 22;
    constexpr auto GET_RENDER_TARGET_MANAGER_INDEX = 23;

    auto stereo_rendering_device_offset = sdk::UEngine::get_stereo_rendering_device_offset();
    if (!stereo_rendering_device_offset) {
        stereo_rendering_device_offset = 0xB18; // fallback for the engine this was originally made for.
    }

    static constexpr auto FSCENEVIEW_STEREO_PASS_OFFSET = 0xAF0;
    static auto get_stereo_pass = [](const sdk::FSceneView& view) -> EStereoscopicPass {
        return (EStereoscopicPass)*(uint8_t*)((uintptr_t)&view + FSCENEVIEW_STEREO_PASS_OFFSET);
    };

    // Actually implement the ones we care about now.
    m_fallback_vtable[DESTRUCTOR_INDEX] = +[](FFakeStereoRendering* stereo) -> void { SPDLOG_INFO("Destructor called?");  }; // destructor.
    m_fallback_vtable[IS_STEREO_ENABLED_INDEX] = +[](FFakeStereoRendering* stereo) -> bool { 
#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
        SPDLOG_INFO("IsStereoEnabled called: {:x}", (uintptr_t)_ReturnAddress());
#endif

        return g_hook->is_stereo_enabled(stereo); 
    }; // IsStereoEnabled

    m_fallback_vtable[IS_STEREO_ENABLED_ON_NEXT_FRAME_INDEX] = +[](FFakeStereoRendering* stereo) -> bool { return g_hook->is_stereo_enabled(stereo); }; // IsStereoEnabledOnNextFrame
    m_fallback_vtable[ENABLE_STEREO_INDEX] = +[](FFakeStereoRendering* stereo) -> bool { return g_hook->is_stereo_enabled(stereo); }; // EnableStereo

    m_fallback_vtable[ADJUST_VIEW_RECT_INDEX] = +[](FFakeStereoRendering* stereo, int32_t index, int* x, int* y, uint32_t* w, uint32_t* h) { 
        return g_hook->adjust_view_rect(stereo, index, x, y, w, h);
    }; // AdjustViewRect

    m_fallback_vtable[CALCULATE_STEREO_VIEW_OFFSET_INDEX] = 
    +[](FFakeStereoRendering* stereo, const int32_t view_index, Rotator<float>* view_rotation, const float world_to_meters, Vector3f* view_location) {
        return g_hook->calculate_stereo_view_offset(stereo, view_index, view_rotation, world_to_meters, view_location);
    }; // CalculateStereoViewOffset

    m_fallback_vtable[CALCULATE_STEREO_PROJECTION_MATRIX_INDEX] = +[](FFakeStereoRendering* stereo, Matrix4x4f* out, const int32_t view_index) {
#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
        SPDLOG_INFO("CalculateStereoProjectionMatrix called: {:x} {} {:x}", (uintptr_t)_ReturnAddress(), view_index, (uintptr_t)out);
#endif

        if (!g_hook->m_has_double_precision) {
            (*out)[3][2] = 0.1f; // Need to pre-set the Z value to something, otherwise it will be 0.0f & probably break something.
        } else {
            auto dmat = (Matrix4x4d*)out;
            (*dmat)[3][2] = 0.1;
        }

        return g_hook->calculate_stereo_projection_matrix(stereo, out, view_index);
    }; // CalculateStereoProjectionMatrix

    m_fallback_vtable[RENDER_TEXTURE_RENDER_THREAD_INDEX] = 
    +[](FFakeStereoRendering* stereo, FRHICommandListImmediate* rhi_command_list, FRHITexture2D* backbuffer, FRHITexture2D* src_texture, double window_size) {
        return g_hook->render_texture_render_thread(stereo, rhi_command_list, backbuffer, src_texture, window_size);
    };

    m_fallback_vtable[GET_RENDER_TARGET_MANAGER_INDEX] = +[](FFakeStereoRendering* stereo) { return g_hook->get_render_target_manager_hook(stereo); }; // GetRenderTargetManager

    m_fallback_vtable[GET_DESIRED_NUMBER_OF_VIEWS_INDEX] = +[](FFakeStereoRendering* stereo, bool stereo_enabled) -> int32_t { 
        return g_hook->get_desired_number_of_views_hook(stereo, stereo_enabled); 
    }; // GetDesiredNumberOfViews

    m_fallback_vtable[GET_VIEW_PASS_FOR_INDEX_INDEX] = +[](FFakeStereoRendering* stereo, bool stereo_requested, const uint32_t view_index) -> EStereoscopicPass {
    #ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
        SPDLOG_INFO("GetViewPassForIndex called: {:x} {} ", (uintptr_t)_ReturnAddress(), view_index);
    #endif

        return g_hook->get_view_pass_for_index_hook(stereo, stereo_requested, view_index);
    }; // GetViewPassForIndex

    m_fallback_vtable[GET_VIEW_INDEX_FOR_PASS_INDEX] = +[](FFakeStereoRendering* stereo, const EStereoscopicPass pass) -> int32_t {
    #ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
        SPDLOG_INFO("GetViewIndexForPass called: {:x} {} ", (uintptr_t)_ReturnAddress(), (uint32_t)pass);
    #endif

        switch (pass) {
            case EStereoscopicPass::eSSP_FULL:
            case EStereoscopicPass::eSSP_PRIMARY:
                return 0;
            
            case EStereoscopicPass::eSSP_SECONDARY:
                return 1;
            
            default:
                SPDLOG_ERROR("Unknown pass: {}", (uint32_t)pass);
                return -1;
        };
    };

    m_fallback_vtable[DEVICE_IS_STEREO_EYE_PASS_INDEX] = +[](FFakeStereoRendering* stereo, const EStereoscopicPass pass) -> bool {
    #ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
        SPDLOG_INFO("DeviceIsStereoEyePass called: {:x} {} ", (uintptr_t)_ReturnAddress(), (uint32_t)pass);
    #endif

        return pass != EStereoscopicPass::eSSP_FULL;
    }; // DeviceIsStereoEyePass

    m_fallback_vtable[DEVICE_IS_STEREO_EYE_VIEW_INDEX] = +[](FFakeStereoRendering* stereo, const sdk::FSceneView& view) -> bool {
    #ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
        SPDLOG_INFO("DeviceIsStereoEyeView called: {:x} {} ", (uintptr_t)_ReturnAddress(), (uint32_t)get_stereo_pass(view));
    #endif

        return get_stereo_pass(view) != EStereoscopicPass::eSSP_FULL;
    }; // DeviceIsStereoEyePass

    m_fallback_vtable[DEVICE_IS_A_PRIMARY_PASS_INDEX] = +[](FFakeStereoRendering* stereo, const EStereoscopicPass pass) -> bool {
    #ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
        SPDLOG_INFO("DeviceIsAPrimaryPass called: {:x} {} ", (uintptr_t)_ReturnAddress(), (uint32_t)pass);
    #endif

        return pass == EStereoscopicPass::eSSP_FULL || pass == EStereoscopicPass::eSSP_PRIMARY;
    }; // DeviceIsAPrimaryPass

    m_fallback_vtable[DEVICE_IS_A_PRIMARY_VIEW_INDEX] = +[](FFakeStereoRendering* stereo, const sdk::FSceneView& view) -> bool {
    #ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
        SPDLOG_INFO("DeviceIsAPrimaryView called: {:x} {} ", (uintptr_t)_ReturnAddress(), (uint32_t)get_stereo_pass(view));
    #endif

        return get_stereo_pass(view) == EStereoscopicPass::eSSP_FULL || get_stereo_pass(view) == EStereoscopicPass::eSSP_PRIMARY;
    }; // DeviceIsAPrimaryPass

    m_fallback_vtable[DEVICE_IS_A_SECONDARY_PASS_INDEX] = +[](FFakeStereoRendering* stereo, const EStereoscopicPass pass) -> bool {
    #ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
        SPDLOG_INFO("DeviceIsASecondaryPass called: {:x} {} ", (uintptr_t)_ReturnAddress(), (uint32_t)pass);
    #endif

        return !(pass == EStereoscopicPass::eSSP_FULL || pass == EStereoscopicPass::eSSP_PRIMARY);
    }; // DeviceIsASecondaryPass

    m_fallback_vtable[DEVICE_IS_A_SECONDARY_VIEW_INDEX] = +[](FFakeStereoRendering* stereo, const sdk::FSceneView& view) -> bool {
    #ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
        SPDLOG_INFO("DeviceIsASecondaryView called: {:x} {} ", (uintptr_t)_ReturnAddress(), (uint32_t)get_stereo_pass(view));
    #endif

        return get_stereo_pass(view) > EStereoscopicPass::eSSP_PRIMARY;
    }; // DeviceIsASecondaryView

    m_special_detected_4_27 = true;
    m_manually_constructed = true;
    m_fallback_device.vtable = m_fallback_vtable.data();

    auto& current_device = *(uintptr_t*)((uintptr_t)engine + *stereo_rendering_device_offset);
    SPDLOG_INFO("Current device: {:x}", current_device);
    current_device = (uintptr_t)&m_fallback_device; // TODO: Automatically find this offset.

    // So the view extension hook will work.
    s_stereo_rendering_device_offset = *stereo_rendering_device_offset;

    hook_game_viewport_client();
    setup_view_extensions();

    SPDLOG_INFO("Finished creating stereo device for the game using nonstandard method");

    m_finished_hooking = true;

    return true;
}

bool FFakeStereoRenderingHook::nonstandard_create_stereo_device_hook_4_22() {
    SPDLOG_INFO("Attempting to create a stereo device for the game using nonstandard method (4.22)");

    auto engine = sdk::UEngine::get();

    if (engine == nullptr) {
        SPDLOG_ERROR("Failed to get engine pointer! Cannot create stereo device!");
        return false;
    }

    m_fallback_vtable.resize(30);

    // Give all of the functions placeholders.
    for (auto i = 0; i < m_fallback_vtable.size(); ++i) {
        m_fallback_vtable[i] = +[](FFakeStereoRendering* stereo) -> void* {
            return nullptr;
        };
    }

    constexpr auto DESTRUCTOR_INDEX = 0;
    constexpr auto IS_STEREO_ENABLED_INDEX = 1;
    constexpr auto IS_STEREO_ENABLED_ON_NEXT_FRAME_INDEX = 2;
    constexpr auto ENABLE_STEREO_INDEX = 3;

    constexpr auto GET_DESIRED_NUMBER_OF_VIEWS_INDEX = 4;
    constexpr auto GET_VIEW_PASS_FOR_INDEX_INDEX = 5;
    constexpr auto GET_VIEW_INDEX_FOR_PASS_INDEX = 6;
    constexpr auto IS_STEREO_EYE_PASS_INDEX = 7;

    constexpr auto ADJUST_VIEW_RECT_INDEX = 8;
    constexpr auto CALCULATE_STEREO_VIEW_OFFSET_INDEX = ADJUST_VIEW_RECT_INDEX + 3;
    constexpr auto CALCULATE_STEREO_PROJECTION_MATRIX_INDEX = CALCULATE_STEREO_VIEW_OFFSET_INDEX + 1;
    constexpr auto RENDER_TEXTURE_RENDER_THREAD_INDEX = CALCULATE_STEREO_PROJECTION_MATRIX_INDEX + 2;
    constexpr auto GET_RENDER_TARGET_MANAGER_INDEX = RENDER_TEXTURE_RENDER_THREAD_INDEX + 1;

    auto stereo_rendering_device_offset = sdk::UEngine::get_stereo_rendering_device_offset();
    if (!stereo_rendering_device_offset) {
        stereo_rendering_device_offset = 0xAB8; // fallback for the engine this was originally made for.
    }

    // Actually implement the ones we care about now.
    m_fallback_vtable[DESTRUCTOR_INDEX] = +[](FFakeStereoRendering* stereo) -> void { SPDLOG_INFO("Destructor called?");  }; // destructor.
    m_fallback_vtable[IS_STEREO_ENABLED_INDEX] = +[](FFakeStereoRendering* stereo) -> bool { 
#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
        SPDLOG_INFO("IsStereoEnabled called: {:x}", (uintptr_t)_ReturnAddress());
#endif

        return g_hook->is_stereo_enabled(stereo); 
    }; // IsStereoEnabled

    m_fallback_vtable[IS_STEREO_ENABLED_ON_NEXT_FRAME_INDEX] = +[](FFakeStereoRendering* stereo) -> bool { return g_hook->is_stereo_enabled(stereo); }; // IsStereoEnabledOnNextFrame
    m_fallback_vtable[ENABLE_STEREO_INDEX] = +[](FFakeStereoRendering* stereo) -> bool { return g_hook->is_stereo_enabled(stereo); }; // EnableStereo

    m_fallback_vtable[ADJUST_VIEW_RECT_INDEX] = +[](FFakeStereoRendering* stereo, int32_t index, int* x, int* y, uint32_t* w, uint32_t* h) { 
        return g_hook->adjust_view_rect(stereo, index, x, y, w, h);
    }; // AdjustViewRect

    m_fallback_vtable[CALCULATE_STEREO_VIEW_OFFSET_INDEX] = 
    +[](FFakeStereoRendering* stereo, const int32_t view_index, Rotator<float>* view_rotation, const float world_to_meters, Vector3f* view_location) {
        return g_hook->calculate_stereo_view_offset(stereo, view_index, view_rotation, world_to_meters, view_location);
    }; // CalculateStereoViewOffset

    m_fallback_vtable[CALCULATE_STEREO_PROJECTION_MATRIX_INDEX] = +[](FFakeStereoRendering* stereo, Matrix4x4f* out, const int32_t view_index) {
#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
        SPDLOG_INFO("CalculateStereoProjectionMatrix called: {:x} {} {:x}", (uintptr_t)_ReturnAddress(), view_index, (uintptr_t)out);
#endif

        if (!g_hook->m_has_double_precision) {
            (*out)[3][2] = 0.1f; // Need to pre-set the Z value to something, otherwise it will be 0.0f & probably break something.
        } else {
            auto dmat = (Matrix4x4d*)out;
            (*dmat)[3][2] = 0.1;
        }

        return g_hook->calculate_stereo_projection_matrix(stereo, out, view_index);
    }; // CalculateStereoProjectionMatrix

    m_fallback_vtable[RENDER_TEXTURE_RENDER_THREAD_INDEX] = 
    +[](FFakeStereoRendering* stereo, FRHICommandListImmediate* rhi_command_list, FRHITexture2D* backbuffer, FRHITexture2D* src_texture, double window_size) {
        return g_hook->render_texture_render_thread(stereo, rhi_command_list, backbuffer, src_texture, window_size);
    };

    m_fallback_vtable[GET_RENDER_TARGET_MANAGER_INDEX] = +[](FFakeStereoRendering* stereo) { return g_hook->get_render_target_manager_hook(stereo); }; // GetRenderTargetManager

    m_fallback_vtable[GET_DESIRED_NUMBER_OF_VIEWS_INDEX] = +[](FFakeStereoRendering* stereo, bool stereo_enabled) -> int32_t { 
        return g_hook->get_desired_number_of_views_hook(stereo, stereo_enabled); 
    }; // GetDesiredNumberOfViews

    m_fallback_vtable[GET_VIEW_PASS_FOR_INDEX_INDEX] = +[](FFakeStereoRendering* stereo, bool stereo_requested, const uint32_t view_index) -> EStereoscopicPass {
    #ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
        SPDLOG_INFO("GetViewPassForIndex called: {:x} {} ", (uintptr_t)_ReturnAddress(), view_index);
    #endif

        return g_hook->get_view_pass_for_index_hook(stereo, stereo_requested, view_index);
    }; // GetViewPassForIndex

    m_fallback_vtable[GET_VIEW_INDEX_FOR_PASS_INDEX] = +[](FFakeStereoRendering* stereo, const EStereoscopicPass pass) -> int32_t {
    #ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
        SPDLOG_INFO("GetViewIndexForPass called: {:x} {} ", (uintptr_t)_ReturnAddress(), (uint32_t)pass);
    #endif

        switch (pass) {
            case EStereoscopicPass::eSSP_FULL:
            case EStereoscopicPass::eSSP_PRIMARY:
                return 0;
            
            case EStereoscopicPass::eSSP_SECONDARY:
                return 1;
            
            default:
                SPDLOG_ERROR("Unknown pass: {}", (uint32_t)pass);
                return -1;
        };
    };

    m_fallback_vtable[IS_STEREO_EYE_PASS_INDEX ] = +[](FFakeStereoRendering* stereo, const EStereoscopicPass pass) -> bool {
    #ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
        SPDLOG_INFO("IsStereoPass called: {:x} {} ", (uintptr_t)_ReturnAddress(), (uint32_t)pass);
    #endif

        return pass != EStereoscopicPass::eSSP_FULL;
    };

    m_special_detected_4_22 = true;
    m_manually_constructed = true;
    m_fallback_device.vtable = m_fallback_vtable.data();

    auto& current_device = *(uintptr_t*)((uintptr_t)engine + *stereo_rendering_device_offset);
    SPDLOG_INFO("Current device: {:x}", current_device);
    current_device = (uintptr_t)&m_fallback_device; // TODO: Automatically find this offset.

    // So the view extension hook will work.
    s_stereo_rendering_device_offset = *stereo_rendering_device_offset;

    hook_game_viewport_client();
    setup_view_extensions();

    SPDLOG_INFO("Finished creating stereo device for the game using nonstandard method");

    m_finished_hooking = true;

    return true;
}

bool FFakeStereoRenderingHook::nonstandard_create_stereo_device_hook_4_18() {
    SPDLOG_INFO("Attempting to create a stereo device for the game using nonstandard method (4.18)");

    auto engine = sdk::UEngine::get();

    if (engine == nullptr) {
        SPDLOG_ERROR("Failed to get engine pointer! Cannot create stereo device!");
        return false;
    }

    m_fallback_vtable.resize(30);

    // Give all of the functions placeholders.
    for (auto i = 0; i < m_fallback_vtable.size(); ++i) {
        m_fallback_vtable[i] = +[](FFakeStereoRendering* stereo) -> void* {
            return nullptr;
        };
    }

    constexpr auto IS_STEREO_ENABLED_INDEX = 0;
    constexpr auto IS_STEREO_ENABLED_ON_NEXT_FRAME_INDEX = 1;
    constexpr auto ENABLE_STEREO_INDEX = 2;

    constexpr auto ADJUST_VIEW_RECT_INDEX = 3;
    constexpr auto CALCULATE_STEREO_VIEW_OFFSET_INDEX = ADJUST_VIEW_RECT_INDEX + 2;
    constexpr auto CALCULATE_STEREO_PROJECTION_MATRIX_INDEX = CALCULATE_STEREO_VIEW_OFFSET_INDEX + 1;
    constexpr auto RENDER_TEXTURE_RENDER_THREAD_INDEX = CALCULATE_STEREO_PROJECTION_MATRIX_INDEX + 3;
    constexpr auto GET_RENDER_TARGET_MANAGER_INDEX = RENDER_TEXTURE_RENDER_THREAD_INDEX + 3;

    auto stereo_rendering_device_offset = sdk::UEngine::get_stereo_rendering_device_offset();
    if (!stereo_rendering_device_offset) {
        stereo_rendering_device_offset = 0xAE8; // fallback for the engine this was originally made for.
    }

    // Actually implement the ones we care about now.
    m_fallback_vtable[IS_STEREO_ENABLED_INDEX] = +[](FFakeStereoRendering* stereo) -> bool { 
#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
        SPDLOG_INFO("IsStereoEnabled called: {:x}", (uintptr_t)_ReturnAddress());
#endif

        return g_hook->is_stereo_enabled(stereo); 
    }; // IsStereoEnabled

    m_fallback_vtable[IS_STEREO_ENABLED_ON_NEXT_FRAME_INDEX] = +[](FFakeStereoRendering* stereo) -> bool { return g_hook->is_stereo_enabled(stereo); }; // IsStereoEnabledOnNextFrame
    m_fallback_vtable[ENABLE_STEREO_INDEX] = +[](FFakeStereoRendering* stereo) -> bool { return g_hook->is_stereo_enabled(stereo); }; // EnableStereo

    m_fallback_vtable[ADJUST_VIEW_RECT_INDEX] = +[](FFakeStereoRendering* stereo, int32_t index, int* x, int* y, uint32_t* w, uint32_t* h) { 
        return g_hook->adjust_view_rect(stereo, index, x, y, w, h);
    }; // AdjustViewRect

    m_fallback_vtable[CALCULATE_STEREO_VIEW_OFFSET_INDEX] = 
    +[](FFakeStereoRendering* stereo, const int32_t view_index, Rotator<float>* view_rotation, const float world_to_meters, Vector3f* view_location) {
        return g_hook->calculate_stereo_view_offset(stereo, view_index, view_rotation, world_to_meters, view_location);
    }; // CalculateStereoViewOffset

    m_fallback_vtable[CALCULATE_STEREO_PROJECTION_MATRIX_INDEX] = +[](FFakeStereoRendering* stereo, Matrix4x4f* out, const int32_t view_index) {
#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
        SPDLOG_INFO("CalculateStereoProjectionMatrix called: {:x} {} {:x}", (uintptr_t)_ReturnAddress(), view_index, (uintptr_t)out);
#endif

        if (!g_hook->m_has_double_precision) {
            (*out)[3][2] = 0.1f; // Need to pre-set the Z value to something, otherwise it will be 0.0f & probably break something.
        } else {
            auto dmat = (Matrix4x4d*)out;
            (*dmat)[3][2] = 0.1;
        }

        return g_hook->calculate_stereo_projection_matrix(stereo, out, view_index);
    }; // CalculateStereoProjectionMatrix

    m_fallback_vtable[RENDER_TEXTURE_RENDER_THREAD_INDEX] = 
    +[](FFakeStereoRendering* stereo, FRHICommandListImmediate* rhi_command_list, FRHITexture2D* backbuffer, FRHITexture2D* src_texture, double window_size) {
        return g_hook->render_texture_render_thread(stereo, rhi_command_list, backbuffer, src_texture, window_size);
    };

    m_fallback_vtable[GET_RENDER_TARGET_MANAGER_INDEX] = +[](FFakeStereoRendering* stereo) { return g_hook->get_render_target_manager_hook(stereo); }; // GetRenderTargetManager

    m_special_detected_4_18 = true;
    m_uses_old_rendertarget_manager = true; // this engine has a funny render target manager.
    m_manually_constructed = true;
    m_fallback_device.vtable = m_fallback_vtable.data();

    auto& current_device = *(uintptr_t*)((uintptr_t)engine + *stereo_rendering_device_offset);
    SPDLOG_INFO("Current device: {:x}", current_device);
    current_device = (uintptr_t)&m_fallback_device; // TODO: Automatically find this offset.

    // So the view extension hook will work.
    s_stereo_rendering_device_offset = *stereo_rendering_device_offset;

    hook_game_viewport_client();
    setup_view_extensions();

    SPDLOG_INFO("Finished creating stereo device for the game using nonstandard method");

    m_finished_hooking = true;

    return true;
}

bool FFakeStereoRenderingHook::hook_game_viewport_client() try {
    SPDLOG_INFO("Attempting to hook UGameViewportClient::Draw...");

    // We need to cache the canvas index before we hook the draw function or else this doesn't work.
    sdk::FViewport::get_debug_canvas_index();
    auto game_viewport_client_draw = sdk::UGameViewportClient::get_draw_function();

    if (!game_viewport_client_draw) {
        SPDLOG_ERROR("Failed to find UGameViewportClient::Draw!");
        m_has_game_viewport_client_draw_hook = false;
        return false;
    }

    m_gameviewportclient_draw_hook = safetyhook::create_inline((void*)*game_viewport_client_draw, &game_viewport_client_draw_hook, safetyhook::InlineHook::StartDisabled);
    m_has_game_viewport_client_draw_hook = true;

    if (!m_gameviewportclient_draw_hook) {
        SPDLOG_ERROR("Failed to hook UGameViewportClient::Draw!");
        return false;
    }

    if (auto enable_result = m_gameviewportclient_draw_hook.enable(); !enable_result.has_value()) {
        SPDLOG_ERROR("Failed to enable UGameViewportClient::Draw hook!");
        return false;
    }

    return true;
} catch(std::exception& e) {
    SPDLOG_ERROR("Failed to hook UGameViewportClient: {}", e.what());
    return false;
} catch(...) {
    SPDLOG_ERROR("Failed to hook UGameViewportClient!");
    return false;
}

void* FFakeStereoRenderingHook::viewport_destructor_hook(void* viewport, void* a2, void* a3, void* a4) {
    ZoneScopedN(__FUNCTION__);

    SPDLOG_INFO("FViewport::~FViewport called: {:x}", (uintptr_t)_ReturnAddress());

    // Call the original destructor.
    auto call_orig = [&]() -> void* {
        ZoneScopedN("FViewport::~FViewport");
        auto res = g_hook->m_viewport_destructor_hook->get_original<decltype(&viewport_destructor_hook)>()(viewport, a2, a3, a4);
        g_hook->m_last_destroyed_viewport = viewport;

        return res;
    };

    if (!g_framework->is_game_data_intialized()) {
        return call_orig();
    }

    auto vr = VR::get();

    if (!vr->is_hmd_active()) {
        return call_orig();
    }

    static bool once = true;

    if (once) {
        SPDLOG_INFO("FViewport::Destructor called for the first time.");
        once = false;
    }

    return call_orig();
}

void FFakeStereoRenderingHook::viewport_draw_hook(void* viewport, bool should_present) {
    ZoneScopedN(__FUNCTION__);

    g_hook->m_last_viewport_vtable = *(void***)viewport;

    auto call_orig = [&]() {
        ZoneScopedN("FViewport::Draw");
        g_hook->m_viewport_draw_hook.call(viewport, should_present);
    };

    if (!g_framework->is_game_data_intialized()) {
        call_orig();
        return;
    }

    if (g_hook->m_viewport_destructor_hook == nullptr) {
        static bool already_tried = false;

        if (!already_tried) {
            already_tried = true;
            auto& vtable = *(void***)viewport;

            if (vtable != nullptr && vtable[0] != nullptr) {
                // Destructors usually have some kind of test reg8, 01 instruction within them.
                if (utility::find_pattern_in_path((uint8_t*)vtable[0], 0x100, false, "F6 ? 01")) {
                    SPDLOG_INFO("Found TEST mnemonic for FViewport destructor at {:x}", (uintptr_t)vtable[0]);
                    SPDLOG_INFO("Hooking FViewport::~FViewport at {:x}", (uintptr_t)vtable[0]);
                    g_hook->m_viewport_destructor_hook = std::make_unique<PointerHook>(&vtable[0], &viewport_destructor_hook);
                } else {
                    SPDLOG_ERROR("Failed to find FViewport destructor pattern at {:x}", (uintptr_t)vtable[0]);
                }
            }
        }
    }

    if (g_hook->m_ignore_next_viewport_draw) {
        g_hook->m_ignore_next_viewport_draw = false;
        return;
    }

    auto vr = VR::get();

    if (!vr->is_hmd_active()) {
        call_orig();
        return;
    }

    static bool once = true;

    if (once) {
        SPDLOG_INFO("FViewport::Draw called for the first time.");
        once = false;
    }

    call_orig();
}

// This function needs some more work for more rigorous filtering
// However it does its job on the relevant titles
// This is only used for the UI compatibility mode.
FRHITexture2D** FFakeStereoRenderingHook::viewport_get_render_target_texture_hook(sdk::FViewport* viewport) {
    const auto retaddr = (uintptr_t)_ReturnAddress();

    SPDLOG_INFO_ONCE("FViewport::GetRenderTargetTexture called!");
    const auto og = g_hook->m_viewport_get_render_target_texture_hook->get_original<decltype(&viewport_get_render_target_texture_hook)>();
    const auto& vr = VR::get();

    if (!vr->is_ahud_compatibility_enabled() || !vr->is_hmd_active() || g_hook->m_slate_draw_window_thread_id == 0) {
        return og(viewport);
    }

    auto& data = g_hook->m_viewport_rt_hook_data;

    {
        std::scoped_lock _{data.retaddr_mutex};
        utility::ScopeGuard guard{[&](){ data.seen_retaddrs.insert(retaddr); }};

        if (data.call_original_retaddrs.contains(retaddr)) {
            return og(viewport);
        }

        std::optional<size_t> func_start{};

        // ALWAYS check the retaddr for ViewFamilyTexture first and never skip it
        // This will fix the case where we run into some other texture initially.
        if (!data.seen_retaddrs.contains(retaddr)) {
            SPDLOG_INFO("FViewport::GetRenderTargetTexture called from {:x}", retaddr);

            func_start = utility::find_function_start(retaddr);

            if (!func_start) {
                func_start = retaddr;
            }

            // The function that has this string reference should ALWAYS get passed
            // back to the original function, this is the actual scene render target.
            // Everything else we will redirect to the UI render target.
            if (utility::find_string_reference_in_path(*func_start, L"ViewFamilyTexture", false) || utility::find_string_reference_in_path(*func_start, L"ViewFamilyTarget", false)) {
                SPDLOG_INFO("Found view family texture reference @ {:x}", retaddr);
                data.call_original_retaddrs.insert(retaddr);
                data.has_view_family_tex = true;
                return og(viewport);
            }

            // We should always allow the viewport when used in a post processing context to go through.
            // There's two because this function stops itself at 200 instructions
            // doing a second one from the retaddr allows us to go further.
            if (utility::find_string_reference_in_path(*func_start, L"FinalPostProcessColor", false) || utility::find_string_reference_in_path(retaddr, L"FinalPostProcessColor", false)) {
                SPDLOG_INFO("Found FinalPostProcessColor reference @ {:x}", retaddr);
                data.call_original_retaddrs.insert(retaddr);
                return og(viewport);
            }

            const auto next_fn_call = utility::scan_disasm(retaddr, 0x30, "E8 ? ? ? ?");

            if (next_fn_call) {
                const auto fn = utility::calculate_absolute(*next_fn_call + 1);

                // I don't know of any other way to check this. I'm not sure what this function is.
                // It seems like deep within a threaded or function for enqueueing a render command.
                if (utility::scan(fn, 0x50, "01 01 01 01") && utility::scan(fn, 0x50, "22 00 00 00")) {
                    SPDLOG_INFO("Found unknown screen space rendering call @ {:x}", retaddr);
                    data.redirected_retaddrs.insert(retaddr);
                }
            }

            // There are multiple other HAL references we can use too.
            static const auto hal_clear_solid_rectangle_fn = utility::find_function_from_string_ref(utility::get_executable(), "HAL::ClearSolidRectangle");
            static std::unordered_set<uintptr_t> scaleform_hal_vtable_functions{};

            const auto is_scaleform = hal_clear_solid_rectangle_fn.has_value();

            if (hal_clear_solid_rectangle_fn.has_value() && scaleform_hal_vtable_functions.empty()) try {
                scaleform_hal_vtable_functions.insert(*hal_clear_solid_rectangle_fn);

                SPDLOG_INFO("Found HAL::ClearSolidRectangle function @ {:x}", *hal_clear_solid_rectangle_fn);
                std::vector<uintptr_t> scaleform_hal_vtable_refs{};
                const auto module_size = utility::get_module_size(utility::get_executable()).value_or(0);
                const auto start = (uintptr_t)utility::get_executable();
                const auto end = (uintptr_t)utility::get_executable() + module_size;
                const auto hal_module = utility::get_module_within(*hal_clear_solid_rectangle_fn).value_or(nullptr);

                // There are multiple HAL vtable, so just collect all of them.
                for (auto i = start; i < end - 0x1000; i += sizeof(uintptr_t)) {
                    const auto remaining = end - i;
                    const auto function_ptr = utility::scan_ptr(i, remaining - 0x1000, *hal_clear_solid_rectangle_fn);

                    if (!function_ptr.has_value()) {
                        break;
                    }

                    i = *function_ptr;

                    SPDLOG_INFO("Found HAL::ClearSolidRectangle function pointer @ {:x}", *function_ptr);
                    for (auto j = 0; j < 100; ++j) {
                        const auto entry = *(uintptr_t*)(*function_ptr + (j * sizeof(uintptr_t)));

                        if (entry == 0 || IsBadReadPtr((void*)entry, sizeof(uintptr_t))) {
                            break;
                        }

                        const auto is_same_module = utility::get_module_within(entry).value_or(nullptr) == hal_module;

                        if (!is_same_module) {
                            break;
                        }

                        scaleform_hal_vtable_functions.insert(entry);
                    }
                }
            } catch(...) {
                SPDLOG_ERROR("Failed to find Scaleform HAL vtable functions!");
            }

            if (is_scaleform && !scaleform_hal_vtable_functions.empty()) try {
                // Walk the stack, get function starts and check if any are in the vtable
                constexpr auto max_stack_depth = 100;
                uintptr_t stack[max_stack_depth]{};

                const auto depth = RtlCaptureStackBackTrace(0, max_stack_depth, (void**)&stack, nullptr);

                for (auto i = 0; i < depth; ++i) {
                    SPDLOG_INFO(" Stack[{}]: {:x}", i, stack[i]);
                }

                bool found = false;

                for (auto i = 1; i < std::min<uint16_t>(7, depth); ++i) {
                    const auto scaleform_func_start = utility::find_virtual_function_start(stack[i]);

                    if (!scaleform_func_start) {
                        continue;
                    }

                    if (scaleform_hal_vtable_functions.contains(*scaleform_func_start)) {
                        SPDLOG_INFO("Found Scaleform HAL vtable function reference @ {:x}", retaddr);
                        data.redirected_retaddrs.insert(retaddr);
                        found = true;
                        break;
                    }
                }
            } catch(...) {
                SPDLOG_ERROR("Failed to walk stack for scaleform vtable functions!");
            }
        }

        // Hacky way to allow the first texture to go through
        // For the games that are using something other than ViewFamilyTexture as the scene RT.
        if (!data.call_original_retaddrs.empty() && !data.redirected_retaddrs.contains(retaddr) && !data.has_view_family_tex) {
            return og(viewport);
        }

        if (!data.redirected_retaddrs.contains(retaddr) && !data.call_original_retaddrs.contains(retaddr)) {
            if (!func_start) {
                func_start = utility::find_function_start(retaddr);

                if (!func_start) {
                    func_start = retaddr;
                }
            }

            // Probably NOT...
            /*if (utility::find_string_reference_in_path(*func_start, L"r.RHICmdAsyncRHIThreadDispatch")) {
                SPDLOG_INFO("Found RHICmdAsyncRHIThreadDispatch reference @ {:x}", retaddr);
                call_original_retaddrs.insert(retaddr);
                return og(viewport);
            }*/

            // TODO? this needs some more rigorous filtering
            // some games are insane and have multiple "UnknownTexture" references...
            if (utility::find_string_reference_in_path(*func_start, L"UnknownTexture", false)) {
                SPDLOG_INFO("Found unknown texture reference @ {:x}", retaddr);
                data.call_original_retaddrs.insert(retaddr);
                return og(viewport);
            }

            SPDLOG_INFO("Redirecting FViewport::GetRenderTargetTexture call to UI render target @ {:x}", retaddr);
            data.redirected_retaddrs.insert(retaddr);
        }
    }

    // Finally redirect the call to the UI render target.
    auto& ui_target = g_hook->get_render_target_manager()->get_effective_ui_target_ref();

    if (ui_target != nullptr) {
        return &ui_target;
    }

    return og(viewport);
}

void FFakeStereoRenderingHook::try_adopt_scene_viewport_render_target(sdk::FViewport* viewport, const char* source) {
    constexpr bool allow_scene_viewport_rt_adoption = false;

    if (is_ue_5_7_or_newer() || !g_framework->is_dx12()) {
        return;
    }

    auto vr = VR::get();

    if (!vr->is_hmd_active() || viewport == nullptr || IsBadReadPtr(viewport, sizeof(void*))) {
        return;
    }

    auto rtm = get_render_target_manager();

    if (rtm == nullptr || rtm->get_render_target() != nullptr) {
        return;
    }

    auto candidate = viewport->get_scene_viewport_render_target_texture_direct();

    if (candidate == nullptr) {
        shf_probe_scene_viewport_memory(viewport, source, nullptr);
        SPDLOG_INFO_EVERY_N_SEC(2, "[SHf] FSceneViewport render target is not available yet from {}", source);
        return;
    }

    ID3D12Resource* native_resource = nullptr;
    D3D12_RESOURCE_DESC desc{};

    if (is_ue_5_6_dx12_backend()) {
        if (!ue56_dx12_try_get_native_resource(candidate, source, &native_resource, &desc)) {
            SPDLOG_WARNING_EVERY_N_SEC(2, "[UE5.6][RT] Failing closed for FSceneViewport render target from {}; waiting for D3D12 texture/backbuffer hooks", source);
            return;
        }
    } else {
        try {
            native_resource = (ID3D12Resource*)candidate->get_native_resource();
        } catch (const std::exception& e) {
            SPDLOG_WARNING_EVERY_N_SEC(2, "[SHf] Rejected FSceneViewport render target from {} because GetNativeResource failed: {}", source, e.what());
            return;
        } catch (...) {
            SPDLOG_WARNING_EVERY_N_SEC(2, "[SHf] Rejected FSceneViewport render target from {} because GetNativeResource threw", source);
            return;
        }

        if (native_resource != nullptr && !IsBadReadPtr(native_resource, sizeof(void*))) {
            desc = native_resource->GetDesc();
        }
    }

    if (native_resource == nullptr || IsBadReadPtr(native_resource, sizeof(void*))) {
        SPDLOG_INFO_EVERY_N_SEC(2, "[SHf] FSceneViewport render target from {} has no native D3D12 resource yet", source);
        return;
    }

    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.Width == 0 || desc.Height == 0) {
        SPDLOG_WARNING_EVERY_N_SEC(2,
            "[SHf] Rejected FSceneViewport render target from {} because desc is invalid: dim={} size={}x{} fmt={}",
            source, (uint32_t)desc.Dimension, desc.Width, desc.Height, (uint32_t)desc.Format);
        return;
    }

    if (!allow_scene_viewport_rt_adoption) {
        shf_probe_scene_viewport_memory(viewport, source, candidate);
        SPDLOG_WARNING_EVERY_N_SEC(2,
            "[SHf] Found FSceneViewport render target candidate from {} at {:x} [{}x{} fmt={}] but not adopting it yet",
            source, (uintptr_t)candidate, desc.Width, desc.Height, (uint32_t)desc.Format);
        return;
    }

    rtm->set_render_target(candidate);

    SPDLOG_WARN_ONCE("[SHf] Adopted real FSceneViewport render target from {} at {:x} [{}x{} fmt={}]",
        source, (uintptr_t)candidate, desc.Width, desc.Height, (uint32_t)desc.Format);
}

void FFakeStereoRenderingHook::game_viewport_client_draw_hook(sdk::UGameViewportClient* viewport_client, sdk::FViewport* viewport, sdk::FCanvas* canvas, void* a4) {
    ZoneScopedN(__FUNCTION__);

    // UI compatibility mode
    // Tries to redirect calls to GetRenderTargetTexture to point towards our UI
    // texture instead of the scene render target, if it's not the scene itself/the view family texture.
    // This usually isn't needed but sometimes there are bespoke changes to the rendering pipeline
    // or uses of the AHUD class that make it necessary.
    if (g_framework->is_game_data_intialized() && VR::get()->is_ahud_compatibility_enabled() && viewport != nullptr) {
        if (g_hook->m_viewport_get_render_target_texture_hook == nullptr) {
            SPDLOG_INFO("Hooking FViewport::GetRenderTargetTexture...");
            void** vp_vtable = *(void***)viewport;
            g_hook->m_viewport_get_render_target_texture_hook = std::make_unique<PointerHook>(&vp_vtable[1], &viewport_get_render_target_texture_hook);
            SPDLOG_INFO("Hooked FViewport::GetRenderTargetTexture!");
        }
    }

    auto call_orig = [=]() {
        ZoneScopedN("UGameViewportClient::Draw");
        g_hook->m_gameviewportclient_draw_hook.call(viewport_client, viewport, canvas, a4);
    };

    SPDLOG_INFO_ONCE("UGameViewportClient::Draw called for the first time.");

    if (!g_framework->is_game_data_intialized()) {
        call_orig();
        return;
    }

    g_hook->m_in_viewport_client_draw = true;
    g_hook->m_was_in_viewport_client_draw = false;
    g_hook->get_render_target_manager()->set_viewport(viewport);
    if (viewport != nullptr) {
        shf_force_scene_viewport_separate_rt(*viewport, "UGameViewportClient::Draw");
    }
    g_hook->try_adopt_scene_viewport_render_target(viewport, "UGameViewportClient::Draw viewport");

    utility::ScopeGuard _{ 
        []() { 
            g_hook->m_in_viewport_client_draw = false;
            g_hook->m_was_in_viewport_client_draw = false;
        } 
    };

    auto vr = VR::get();

    if (!vr->is_hmd_active()) {
        call_orig();
        return;
    }

    static uint32_t hook_attempts = 0;
    static bool run_anyways = false;

    if (hook_attempts < 100 && !g_hook->m_hooked_game_engine_tick && g_hook->m_attempted_hook_game_engine_tick) {
        ZoneScopedN("UGameViewportClient::Draw (hook UGameEngine::Tick)");
        SPDLOG_INFO("Performing alternative UGameEngine::Tick hook for synced AFR.");

        ++hook_attempts;

        // Go up the stack and find the viewport draw function.
        constexpr auto max_stack_depth = 100;
        uintptr_t stack[max_stack_depth]{};

        const auto depth = RtlCaptureStackBackTrace(0, max_stack_depth, (void**)&stack, nullptr);

        for (auto i = 0; i < depth; ++i) {
            SPDLOG_INFO("Stack[{}]: {:x}", i, stack[i]);
        }

        for (auto i = 3; i < depth; ++i) {
            const auto ret = stack[i];

            g_hook->attempt_hook_game_engine_tick(ret);

            if (g_hook->m_hooked_game_engine_tick) {
                SPDLOG_INFO("Successfully hooked UGameEngine::Tick for synced AFR.");
                break;
            }
        }
    } else {
        run_anyways = !g_hook->m_hooked_game_engine_tick;
    }

    const auto in_engine_tick = g_hook->m_in_engine_tick;

    if (run_anyways || in_engine_tick) {
        if (g_hook->m_has_view_extension_hook) {
            g_frame_count = vr->get_runtime()->internal_frame_count;
            vr->update_hmd_state(true, vr->get_runtime()->internal_frame_count + 1);
        } else {
            vr->update_hmd_state(false);
        }

        if (subnautica2_is_current_game() &&
            vr->is_native_stereo_fix_enabled() &&
            !vr->is_native_stereo_fix_same_pass_enabled() &&
            g_hook->m_tracking_system_hook != nullptr)
        {
            const auto controller_camera_guard_active = vr->is_controller_camera_conflict_guard_active();
            const auto direct_aim_compatibility_fallback =
                vr->is_hmd_active() &&
                !controller_camera_guard_active &&
                (is_deadzone_ue56_executable() || vr->is_direct_aim_compatibility_enabled()) &&
                (vr->is_headlocked_aim_enabled() ||
                    (vr->is_controller_aim_enabled() && vr->is_using_controllers()));

            if (vr->is_any_aim_method_active() &&
                !controller_camera_guard_active &&
                (vr->is_aim_modify_player_control_rotation_enabled() || direct_aim_compatibility_fallback))
            {
                SPDLOG_INFO_ONCE("[Subnautica2][NativeStereoFix] Updating control rotation before both native stereo views");
                g_hook->m_tracking_system_hook->manual_update_control_rotation();
            }
        }
    }

    const auto& mods = g_framework->get_mods()->get_mods();

    for (const auto& mod : mods) {
        mod->on_pre_viewport_client_draw(viewport_client, viewport, canvas);
    }

    call_orig();

    // Perform synced eye rendering (synced AFR)
    if (in_engine_tick && vr->is_using_synchronized_afr()) {
        static bool hooked_viewport_draw = false;

        // Hook for FViewport::Draw
        if (g_hook->m_hooked_game_engine_tick && !hooked_viewport_draw) {
            hooked_viewport_draw = true;

            // Go up the stack and find the viewport draw function.
            constexpr auto max_stack_depth = 100;
            uintptr_t stack[max_stack_depth]{};

            const auto depth = RtlCaptureStackBackTrace(0, max_stack_depth, (void**)&stack, nullptr);
            if (depth >= 2) {
                // Log the stack functions
                for (auto i = 0; i < depth; ++i) {
                    SPDLOG_INFO("(Stack[{}]: {:x}", i, stack[i]);
                }

                auto try_hook_index = [&](uint32_t index) -> bool {
                    SPDLOG_INFO("Attempting to locate FViewport::Draw function @ stack[{}]", index);

                    const auto viewport_draw_middle = stack[index];
                    const auto viewport_draw = utility::find_function_start_with_call(viewport_draw_middle);

                    if (!viewport_draw) {
                        SPDLOG_ERROR("Failed to find viewport draw function @ {}", index);
                        return false;
                    }

                    SPDLOG_INFO("Found FViewport::Draw function at {:x}", (uintptr_t)*viewport_draw); 

                    g_hook->m_viewport_draw_hook = safetyhook::create_inline((void*)*viewport_draw, &viewport_draw_hook);

                    if (!g_hook->m_viewport_draw_hook) {
                        SPDLOG_ERROR("Failed to hook FViewport::Draw function!");
                        return false;
                    }

                    return true;
                };

                if (!try_hook_index(1)) {
                    // Fallback to index 3, on some UE4 games the viewport draw function is called from a different stack index.
                    if (!try_hook_index(2)) {
                        SPDLOG_ERROR("Failed to find viewport draw function! Cannot perform synced AFR!");
                    }
                }
            }
        }
    }

    // This is how synchronized AFR works. it forces a world draw
    // on the start of the next engine tick, before the world ticks again.
    // that will allow both views and the world to be drawn in sync with no artifacts.
    if (in_engine_tick && vr->is_using_synchronized_afr() && g_frame_count % 2 == 0) {
        GameThreadWorker::get().enqueue([=]() {
            if (g_hook->m_viewport_draw_hook && viewport != g_hook->m_last_destroyed_viewport) {
                __try {
                    if (*(void***)viewport != g_hook->m_last_viewport_vtable) {
                        SPDLOG_ERROR("FViewport::Draw called on a viewport with a different vtable! This is not expected!");
                        return;
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    SPDLOG_ERROR("FViewport::Draw called with a bad viewport pointer! This is not expected!");
                    return;
                }

                const auto viewport_draw = (void (*)(void*, bool))g_hook->m_viewport_draw_hook.target();
                viewport_draw(viewport, true);

                auto& vr = VR::get();
                const auto method = vr->get_synced_sequential_method();
                
                if (method == VR::SyncedSequentialMethod::SKIP_TICK) {
                    g_hook->m_ignore_next_engine_tick = true;
                    //g_hook->m_ignore_next_viewport_draw = true;
                } else if (method == VR::SyncedSequentialMethod::SKIP_DRAW) {
                    g_hook->m_ignore_next_viewport_draw = true;
                }
            }
        });
    }

    for (const auto& mod : mods) {
        mod->on_post_viewport_client_draw(viewport_client, viewport, canvas);
    }
}

static std::array<uintptr_t, 50> g_view_extension_vtable{};
struct SceneViewExtensionAnalyzer;

// Analyzes all of the virtual functions for ISceneViewExtension
// We create the ISceneViewExtension ourselves and overwrite all of the virtual functions
// The class will count how many times each virtual is getting called
// and then when a threshold is reached, it finds the most called one
// the most called one is IsActiveThisFrame which we need to activate the ISceneViewExtension
struct SceneViewExtensionAnalyzer {
    template<int N>
    struct FillVtable {
        static void fill(std::array<uintptr_t, 50>& table);
        static void fill2(std::array<uintptr_t, 50>& table);
    };

    template<>
    struct FillVtable<-1> {
        static void fill(std::array<uintptr_t, 50>& table) {}
        static void fill2(std::array<uintptr_t, 50>& table) {}
    };

    struct AnalyzedFunction {
        uint32_t call_count{0};
        uint32_t frame_count_a2{0};
        uint32_t frame_count_a3{0};
        uint32_t frame_count_offset_a2{0};
        uint32_t frame_count_offset_a3{0};
        uint32_t times_frame_count_correct_a2{0};
        uint32_t times_frame_count_correct_a3{0};
        std::array<uint8_t, 0x100> a2_data{};
        std::array<uint8_t, 0x100> a3_data{};
    };

    static inline std::recursive_mutex dummy_mutex{};
    static inline uint32_t total_call_count{};
    static inline std::unordered_map<uint32_t, AnalyzedFunction> functions{};
    static inline bool has_found_is_active_this_frame_index{false};
    static inline bool has_found_begin_render_viewfamily{false};
    static inline bool index_0_called{false};
    
    static inline uint32_t is_active_this_frame_index{0};
    static inline uint32_t begin_render_viewfamily_index{0};
    static inline uint32_t pre_render_viewfamily_renderthread_index{0};
    static inline uint32_t frame_count_offset{0};

    static bool validate_cached_discovery(void** original_vtable, const nlohmann::json& cached);
    static bool try_apply_cached_discovery(void** original_vtable);
    static void save_cached_discovery();

    template<int N>
    static bool analysis_dummy_stage1(ISceneViewExtension* extension, uintptr_t a2, uintptr_t a3, uintptr_t a4) {
        if (N == 0) {
            index_0_called = true;
        }

        if (has_found_is_active_this_frame_index) {
            return false;
        }

        std::scoped_lock _{dummy_mutex};

        auto& func = functions[N];

        ++total_call_count;
        ++functions[N].call_count;

        if (total_call_count >= 50) {
            // Find the most called index, it's going to be IsActiveThisFrame
            uint32_t max_count = 0;
            uint32_t max_index = 0;

            for (const auto& func : functions) {
                const auto count = func.second.call_count;
                const auto index = func.first;

                if (count > max_count) {
                    max_count = count;
                    max_index = index;
                }
            }

            SPDLOG_INFO("[Stage 1] Found most called index to be {} with {} calls", max_index, max_count);

            functions.clear();
            FillVtable<g_view_extension_vtable.size() - 1>::fill2(g_view_extension_vtable);

            // Force the function to return true
            g_view_extension_vtable[max_index] = (uintptr_t)+[](ISceneViewExtension* ext) -> bool {
                return true;
            };

            has_found_is_active_this_frame_index = true;
            is_active_this_frame_index = max_index;
        } else {
            if (functions[N].call_count == 1) {
                SPDLOG_INFO("[Stage 1] ISceneViewExtension Index {} called for the first time!", N);
            }
        }

        return false;
    };

    template<int N>
    static bool analysis_dummy_stage2(ISceneViewExtension* extension, uintptr_t a2, uintptr_t a3, uintptr_t a4) {
        if (has_found_begin_render_viewfamily) {
            return false;
        }

        if (N == 0) {
            index_0_called = true;
        }

        std::scoped_lock _{dummy_mutex};

        if (functions.contains(N)) {
            auto& func = functions[N];

            if (func.call_count++ == 0) {
                SPDLOG_INFO("[Stage 2] SceneViewExtension Index {} called for the first time!", N);
            }

            const auto& last_view_family_data_a2 = func.a2_data;
            const auto view_family_a2 = (uintptr_t)a2;

            if (a2 != 0 && !IsBadReadPtr((void*)a2, 0x100)) {
                for (auto i = 0x10; i < last_view_family_data_a2.size(); i += sizeof(uint32_t)) {
                    const auto a = *(uint32_t*)&last_view_family_data_a2[i];
                    const auto b = *(uint32_t*)&((uint8_t*)view_family_a2)[i];

                    if (b == a + 1 && a >= 10) { // rule out really low frame counts (this could be something else)
                        if (func.frame_count_a2 + 1 == b) {
                            SPDLOG_DEBUG("[A2] Function index {} Found frame count offset at {:x}, ({})", N, i, b);

                            func.frame_count_offset_a2 = i;
                            ++func.times_frame_count_correct_a2;

                            // func_next is one of the functions ahead of N and has the frame count in a3
                            AnalyzedFunction* func_next = nullptr;
                            uint32_t next_index = 0;

                            for (auto j = N + 1; j < g_view_extension_vtable.size(); j++) {
                                if (functions.contains(j)) {
                                    const auto& next = functions[j];

                                    if (next.times_frame_count_correct_a3 >= 10) {
                                        func_next = &functions[j];
                                        next_index = j;
                                        break;
                                    }
                                }
                            }

                            if (func_next != nullptr) {
                                if (func.times_frame_count_correct_a2 >= 50 && 
                                    func_next->times_frame_count_correct_a3 >= 50 && 
                                    func.frame_count_offset_a2 == func_next->frame_count_offset_a3 &&
                                    std::abs((int32_t)func.frame_count_a2 - (int32_t)func_next->frame_count_a3) <= 3) // In some games, the frame delta is really high but the same offset (so, it's wrong)
                                {
                                    SPDLOG_INFO("Found final frame count offset at {:x}", i);
                                    SPDLOG_INFO("Found BeginRenderViewFamily at index {}", N);
                                    SPDLOG_INFO("Found PreRenderViewFamily_RenderThread at index {}", next_index);
                                    has_found_begin_render_viewfamily = true;
                                    begin_render_viewfamily_index = N;
                                    pre_render_viewfamily_renderthread_index = next_index;

                                    frame_count_offset = i;
                                    sdk::FSceneViewFamily::set_frame_count_offset(frame_count_offset);
                                    save_cached_discovery();

                                    setup_view_extension_hook();
                                    return false;
                                }   
                            }
                        }

                        func.frame_count_a2 = b;
                        break;
                    }
                }
            }

            const auto& last_view_family_data_a3 = func.a3_data;
            const auto view_family_a3 = (uintptr_t)a3;

            if (a3 != 0 && !IsBadReadPtr((void*)a3, 0x100)) {
                for (auto i = 0x10; i < last_view_family_data_a3.size(); i += sizeof(uint32_t)) {
                    const auto a = *(uint32_t*)&last_view_family_data_a3[i];
                    const auto b = *(uint32_t*)&((uint8_t*)view_family_a3)[i];

                    if (b == a + 1 && a >= 10) { // rule out really low frame counts (this could be something else)
                        if (func.frame_count_a3 + 1 == b) {
                            SPDLOG_DEBUG("[A3] Function index {} Found frame count offset at {:x} ({})", N, i, b);
                            ++func.times_frame_count_correct_a3;
                        }

                        func.frame_count_a3 = b;
                        func.frame_count_offset_a3 = i;
                        break;
                    }
                }
            }
        }

        if (a2 != 0 && !IsBadReadPtr((void*)a2, 0x100)) {
            memcpy(functions[N].a2_data.data(), (void*)a2, 0x100);
        }

        if (a3 != 0 && !IsBadReadPtr((void*)a3, 0x100)) {
            memcpy(functions[N].a3_data.data(), (void*)a3, 0x100);
        }

        return false;
    }

    static inline std::recursive_mutex vtable_mutex{};
    static inline std::unordered_map<sdk::FRHICommandBase_New*, void**> original_vtables{};
    static inline std::unordered_map<sdk::FRHICommandBase_New*, uint32_t> cmd_frame_counts{};

    // Meant to be called after analysis has been completed
    static void setup_view_extension_hook() {
        std::scoped_lock _{dummy_mutex};

        SPDLOG_INFO("Setting up BeginRenderViewFamily hook...");

        const auto setup_view_family_index = index_0_called ? 0 : 1;

        g_view_extension_vtable[setup_view_family_index] = (uintptr_t)&FFakeStereoRenderingHook::setup_view_family;
        g_view_extension_vtable[begin_render_viewfamily_index] = (uintptr_t)&FFakeStereoRenderingHook::begin_render_viewfamily;

        if (!index_0_called && (setup_view_family_index + 2) != begin_render_viewfamily_index) {
            g_view_extension_vtable[setup_view_family_index + 2] = (uintptr_t)&FFakeStereoRenderingHook::setup_viewpoint;
        }

        // PreRenderViewFamily_RenderThread
        g_view_extension_vtable[pre_render_viewfamily_renderthread_index] = (uintptr_t)&FFakeStereoRenderingHook::pre_render_viewfamily_renderthread;

        SPDLOG_INFO("Done setting up BeginRenderViewFamily hook!");
    }

    static inline std::unordered_set<int> tested_execute_indices{};
    static inline int correct_execute_index{0};
    static inline bool found_correct_execute{false};

    template<int N>
    static void* hooked_command_fn(sdk::FRHICommandBase_New* cmd, sdk::FRHICommandListBase* cmd_list, void* debug_context, void* r9, void* stack_1, void* stack_2, void* stack_3, void* stack_4, void* stack_5, void* stack_6, void* stack_7, void* stack_8) {
        std::scoped_lock _{vtable_mutex};
        //std::scoped_lock __{VR::get()->get_vr_mutex()};

        static bool once = true;

        if (once) {
            SPDLOG_INFO("[ISceneViewExtension] Successfully hijacked command list! {}", N);
        }

        if (g_hook != nullptr) {
            g_hook->note_successful_command_list_hijack();
        }

        const auto original_vtable = original_vtables[cmd];
        const auto original_func = original_vtable[N];

        const auto func = (decltype(hooked_command_fn<N>)*)original_func;
        const auto frame_count = cmd_frame_counts[cmd];

        if (once) {
            SPDLOG_INFO("[ISceneViewExtension] Command list frame count: {}", frame_count);
            SPDLOG_INFO("[ISceneViewExtension] Original vtable: {:x}", (uintptr_t)original_vtable);
            once = false;
        }

        if (!found_correct_execute && !tested_execute_indices.contains(N) && VR::get()->get_present_thread_id() != 0) {
            tested_execute_indices.insert(N);

            // N == 0 is a pretty safe heuristic
            // Otherwise if >= 1 gets called first, we can assume if the thread is the same
            // as the DXGI present thread, then it's the correct execute function
            if (N == 0 || GetCurrentThreadId() == VR::get()->get_present_thread_id()) {
                correct_execute_index = N;
                found_correct_execute = true;
                SPDLOG_INFO("[ISceneViewExtension] Found correct execute index: {}", N);
            }
        }

        auto& vr = VR::get();
        auto runtime = vr->get_runtime();

        auto call_orig = [=]() {
            const auto result = func(cmd, cmd_list, debug_context, r9, stack_1, stack_2, stack_3, stack_4, stack_5, stack_6, stack_7, stack_8);

            if (N == correct_execute_index) {
                runtime->enqueue_render_poses(frame_count);
            }

            return result;
        };

        if (N != correct_execute_index) {
            return call_orig();
        }

        // set the vtable back
        *(void**)cmd = original_vtable;
        original_vtables.erase(cmd);
        cmd_frame_counts.erase(cmd);

        RHIThreadWorker::get().execute();

        if (vr->get_synchronize_stage() == VR::SynchronizeStage::EARLY) {
            if (runtime->is_openxr()) {
                if (g_framework->get_renderer_type() == Framework::RendererType::D3D11) {
                    if (!runtime->got_first_sync || runtime->synchronize_frame(frame_count, VRRuntime::SyncFrameCallsite::VREarlyRHICommand) != VRRuntime::Error::SUCCESS) {
                        return call_orig();
                    }  
                } else if (runtime->synchronize_frame(frame_count, VRRuntime::SyncFrameCallsite::VREarlyRHICommand) != VRRuntime::Error::SUCCESS) {
                    return call_orig();
                }

                vr->get_openxr_runtime()->begin_frame();
            } else {
                if (runtime->synchronize_frame(frame_count, VRRuntime::SyncFrameCallsite::VREarlyRHICommand) != VRRuntime::Error::SUCCESS) {
                    return call_orig();
                }
            }
        }

        return call_orig();
    }

    static void hook_new_rhi_command(sdk::FRHICommandBase_New* last_command, uint32_t frame_count) {
        std::scoped_lock __{vtable_mutex};

        auto runtime = VR::get()->get_runtime();
        runtime->on_pre_render_render_thread(frame_count);

        if (last_command == nullptr || *(void**)last_command == nullptr) {
            SPDLOG_INFO("Cannot hook command with no vtable, falling back to passing current frame count to runtime");
            runtime->enqueue_render_poses(frame_count);
            return;
        }

        // Whichever one gets called first is the winner winner chicken dinner
        static std::array<uintptr_t, 7> new_vtable{
            (uintptr_t)&hooked_command_fn<0>,
            (uintptr_t)&hooked_command_fn<1>,
            (uintptr_t)&hooked_command_fn<2>,
            (uintptr_t)&hooked_command_fn<3>,
            (uintptr_t)&hooked_command_fn<4>,
            (uintptr_t)&hooked_command_fn<5>,
            (uintptr_t)&hooked_command_fn<6>
        };

        cmd_frame_counts[last_command] = frame_count;

        if (original_vtables.contains(last_command) || *(void**)last_command == new_vtable.data()) {
            static auto last_log_time = std::chrono::high_resolution_clock::time_point{};
            static uint64_t suppressed_count = 0;
            const auto now = std::chrono::high_resolution_clock::now();
            ++suppressed_count;
            
            if (last_log_time.time_since_epoch().count() == 0 || now - last_log_time > std::chrono::seconds(30)) {
                SPDLOG_WARN(
                    "Something strange is going on, the vtable is already hooked, maybe previous frame was not rendered? suppressed={}",
                    suppressed_count);
                suppressed_count = 0;
                last_log_time = now;
            }

            if (auto vr = VR::get(); vr != nullptr) {
                vr->note_stalker2_transition_stress("duplicate_rhi_command");
            }

            return;
        }

        original_vtables[last_command] = *(void***)last_command;
        *(void***)last_command = (void**)new_vtable.data();
    }

    static void hook_old_rhi_command(sdk::FRHICommandBase_Old* last_command, uint32_t frame_count) {
        static std::recursive_mutex func_mutex{};
        static std::unordered_map<sdk::FRHICommandBase_Old*, sdk::FRHICommandBase_Old::Func> original_funcs{};
        static std::unordered_map<sdk::FRHICommandBase_Old*, uint32_t> cmd_frame_counts{};

        std::scoped_lock __{func_mutex};

        auto runtime = VR::get()->get_runtime();
        runtime->on_pre_render_render_thread(frame_count);

        cmd_frame_counts[last_command] = frame_count;

        if (original_funcs.contains(last_command)) {
            static auto last_log_time = std::chrono::high_resolution_clock::time_point{};
            static uint64_t suppressed_count = 0;
            const auto now = std::chrono::high_resolution_clock::now();
            ++suppressed_count;
            
            if (last_log_time.time_since_epoch().count() == 0 || now - last_log_time > std::chrono::seconds(30)) {
                SPDLOG_WARN(
                    "Something strange is going on, the function is already hooked, maybe previous frame was not rendered? suppressed={}",
                    suppressed_count);
                suppressed_count = 0;
                last_log_time = now;
            }

            if (auto vr = VR::get(); vr != nullptr) {
                vr->note_stalker2_transition_stress("duplicate_old_rhi_command");
            }

            return;
        }

        static auto func_override = (sdk::FRHICommandBase_Old::Func)+[](sdk::FRHICommandListBase* cmd_list, sdk::FRHICommandBase_Old* cmd) {
            std::scoped_lock _{func_mutex};
            //std::scoped_lock __{VR::get()->get_vr_mutex()};

            static bool once = true;

            if (once) {
                SPDLOG_INFO("[ISceneViewExtension] Successfully hijacked command list!");
                once = false;
            }

            if (g_hook != nullptr) {
                g_hook->note_successful_command_list_hijack();
            }

            auto& vr = VR::get();
            auto runtime = vr->get_runtime();

            const auto func = original_funcs[cmd];
            const auto frame_count = cmd_frame_counts[cmd];

            runtime->enqueue_render_poses(frame_count);
            runtime->on_pre_render_rhi_thread(frame_count);

            auto call_orig = [&]() {
                func(*cmd_list, cmd);
            };

            cmd->func = func;
            original_funcs.erase(cmd);
            cmd_frame_counts.erase(cmd);

            RHIThreadWorker::get().execute();

            if (vr->get_synchronize_stage() == VR::SynchronizeStage::EARLY) {
                if (runtime->is_openxr()) {
                    if (g_framework->get_renderer_type() == Framework::RendererType::D3D11) {
                        if (!runtime->got_first_sync || runtime->synchronize_frame(std::nullopt, VRRuntime::SyncFrameCallsite::VREarlyRHICommand) != VRRuntime::Error::SUCCESS) {
                            return call_orig();
                        }  
                    } else if (runtime->synchronize_frame(std::nullopt, VRRuntime::SyncFrameCallsite::VREarlyRHICommand) != VRRuntime::Error::SUCCESS) {
                        return call_orig();
                    }

                    vr->get_openxr_runtime()->begin_frame();
                } else {
                    if (runtime->synchronize_frame(std::nullopt, VRRuntime::SyncFrameCallsite::VREarlyRHICommand) != VRRuntime::Error::SUCCESS) {
                        return call_orig();
                    }
                }
            }

            return call_orig();
        };

        original_funcs[last_command] = last_command->func;
        last_command->func = func_override;
    }
};

template<int N>
void SceneViewExtensionAnalyzer::FillVtable<N>::fill(std::array<uintptr_t, 50>& table) {
    table[N] = (uintptr_t)&SceneViewExtensionAnalyzer::analysis_dummy_stage1<N>;
    FillVtable<N - 1>::fill(table);
}

template<int N>
void SceneViewExtensionAnalyzer::FillVtable<N>::fill2(std::array<uintptr_t, 50>& table) {
    table[N] = (uintptr_t)&SceneViewExtensionAnalyzer::analysis_dummy_stage2<N>;
    FillVtable<N - 1>::fill2(table);
}

bool SceneViewExtensionAnalyzer::validate_cached_discovery(void** original_vtable, const nlohmann::json& cached) {
    if (!cached.is_object() || original_vtable == nullptr) {
        return false;
    }

    const auto cached_is_active = cached.value("is_active_this_frame_index", UINT32_MAX);
    const auto cached_begin = cached.value("begin_render_viewfamily_index", UINT32_MAX);
    const auto cached_pre_render = cached.value("pre_render_viewfamily_renderthread_index", UINT32_MAX);
    const auto cached_frame_count_offset = cached.value("frame_count_offset", UINT32_MAX);

    constexpr auto max_index = g_view_extension_vtable.size();

    if (cached_is_active >= max_index || cached_begin >= max_index || cached_pre_render >= max_index ||
        cached_begin == cached_pre_render || cached_frame_count_offset == 0 || cached_frame_count_offset > 0x4000)
    {
        return false;
    }

    const auto base_module = utility::get_module_within((void*)original_vtable[0]).value_or(nullptr);

    for (const auto index : {cached_is_active, cached_begin, cached_pre_render}) {
        const auto fn = original_vtable[index];

        if (fn == nullptr || IsBadReadPtr(fn, sizeof(void*))) {
            return false;
        }

        const auto fn_module = utility::get_module_within((void*)fn).value_or(nullptr);

        if (base_module != nullptr && fn_module != base_module) {
            return false;
        }
    }

    return true;
}

bool SceneViewExtensionAnalyzer::try_apply_cached_discovery(void** original_vtable) {
    if (!is_ue_5_7_or_newer()) {
        return false;
    }

    const auto cached = sdk::discovery_cache::load_entry(UE57_VIEW_EXTENSION_DISCOVERY_CACHE_KEY, utility::get_executable());

    if (!cached) {
        return false;
    }

    if (!validate_cached_discovery(original_vtable, *cached)) {
        sdk::discovery_cache::invalidate_entry(UE57_VIEW_EXTENSION_DISCOVERY_CACHE_KEY);
        return false;
    }

    functions.clear();
    total_call_count = 0;
    has_found_is_active_this_frame_index = true;
    has_found_begin_render_viewfamily = true;
    index_0_called = cached->value("index_0_called", false);
    is_active_this_frame_index = cached->value("is_active_this_frame_index", 0u);
    begin_render_viewfamily_index = cached->value("begin_render_viewfamily_index", 0u);
    pre_render_viewfamily_renderthread_index = cached->value("pre_render_viewfamily_renderthread_index", 0u);
    frame_count_offset = cached->value("frame_count_offset", 0u);
    sdk::FSceneViewFamily::set_frame_count_offset(frame_count_offset);

    FillVtable<g_view_extension_vtable.size() - 1>::fill2(g_view_extension_vtable);
    g_view_extension_vtable[is_active_this_frame_index] = (uintptr_t)+[](ISceneViewExtension* ext) -> bool {
        return true;
    };

    SPDLOG_INFO("[UE 5.7] Reused cached SceneViewExtension discovery");
    setup_view_extension_hook();
    return true;
}

void SceneViewExtensionAnalyzer::save_cached_discovery() {
    if (!is_ue_5_7_or_newer() || !has_found_is_active_this_frame_index || !has_found_begin_render_viewfamily || frame_count_offset == 0) {
        return;
    }

    sdk::discovery_cache::save_entry(UE57_VIEW_EXTENSION_DISCOVERY_CACHE_KEY, utility::get_executable(), {
        {"index_0_called", index_0_called},
        {"is_active_this_frame_index", is_active_this_frame_index},
        {"begin_render_viewfamily_index", begin_render_viewfamily_index},
        {"pre_render_viewfamily_renderthread_index", pre_render_viewfamily_renderthread_index},
        {"frame_count_offset", frame_count_offset}
    });
}

// 4.25something to 4.27
// TODO: Add support for all versions via PDB dumps
constexpr auto INIT_OPTIONS_OFFSET = 0x50;

bool FFakeStereoRenderingHook::is_in_viewport_client_draw() const {
    return m_in_viewport_client_draw && GameThreadWorker::get().is_same_thread();
}

// FSceneView constructor hook
sdk::FSceneView* FFakeStereoRenderingHook::sceneview_constructor(sdk::FSceneView* view, sdk::FSceneViewInitOptions* init_options, void* a3, void* a4) {
    SPDLOG_INFO_ONCE("Called FSceneView constructor for the first time");

    auto& vr = VR::get();

    if (!g_hook->is_in_viewport_client_draw() || !vr->is_hmd_active()) {
        return g_hook->m_sceneview_data.constructor_hook.unsafe_call<sdk::FSceneView*>(view, init_options, a3, a4);
    }

    if (g_hook->m_analyzing_view_extensions || !g_hook->m_has_view_extensions_installed) {
        SPDLOG_INFO_ONCE("FSceneView constructor was called before view extensions were installed, aborting");
        return g_hook->m_sceneview_data.constructor_hook.unsafe_call<sdk::FSceneView*>(view, init_options, a3, a4);
    }

    std::scoped_lock ___{g_hook->m_sceneview_data.mtx};

    const auto retaddr = (uintptr_t)_ReturnAddress();

    if (!g_hook->m_sceneview_data.seen_retaddrs.contains(retaddr)) {
        g_hook->m_sceneview_data.seen_retaddrs.insert(retaddr);
        SPDLOG_INFO("FSceneView constructor called from {:x}", retaddr);
    }

    sdk::FSceneViewInitOptionsBase::update_offsets(init_options);

    if (!is_ue_5_7_or_newer()) {
        if (auto view_family = init_options->get_view_family(); view_family != nullptr) {
            if (sdk::FSceneViewFamily::update_offsets(view_family, nullptr)) {
                g_hook->note_scene_view_family_offsets_ready();
            }
        }
    }

    const auto is_ue5 = g_hook->has_double_precision();
    auto init_options_ue5 = (sdk::FSceneViewInitOptionsUE5*)init_options;

    const auto init_options_scene_state = init_options->get_scene_state();

    if (init_options_scene_state != nullptr) {
        if (is_ue5) {
            auto& vio_entry = g_hook->m_sceneview_data.view_init_options_ue5[init_options_scene_state];
            memcpy(&vio_entry, init_options, sizeof(sdk::FSceneViewInitOptionsUE5));
        } else {
            auto& vio_entry = g_hook->m_sceneview_data.view_init_options_ue4[init_options_scene_state];
            memcpy(&vio_entry, init_options, sizeof(sdk::FSceneViewInitOptionsUE4));
        }
    }

    auto& known_scene_states = g_hook->m_sceneview_data.known_scene_states;
    auto& last_frame_count = g_hook->m_sceneview_data.last_frame_count;
    auto& last_index = g_hook->m_sceneview_data.last_index;

    if (last_frame_count != g_frame_count || last_index > 1) {
        last_index = 0;
    }

    last_frame_count = g_frame_count;

    const auto true_index = vr->is_using_afr() ? (g_frame_count + last_index) % 2 : last_index;

    if (subnautica2_is_current_game() &&
        vr->is_native_stereo_fix_enabled() &&
        !vr->is_native_stereo_fix_same_pass_enabled() &&
        subnautica2_force_primary_primary_views())
    {
        if (true_index == 0 && init_options_scene_state != nullptr) {
            g_hook->m_sceneview_data.native_stereo_primary_scene_state = init_options_scene_state;
            g_hook->m_sceneview_data.native_stereo_primary_scene_state_frame = g_frame_count;
        } else if (
            true_index == 1 &&
            g_hook->m_sceneview_data.native_stereo_primary_scene_state != nullptr &&
            g_hook->m_sceneview_data.native_stereo_primary_scene_state_frame == g_frame_count &&
            init_options->get_exposure_scene_state() == nullptr)
        {
            init_options->set_exposure_scene_state(g_hook->m_sceneview_data.native_stereo_primary_scene_state);
            SPDLOG_INFO_ONCE("[Subnautica2][NativeStereoFix] Sharing left-eye exposure scene state while using independent primary stereo passes");
        }

        // Debug fallback: keep each eye as a primary view while StereoViewIndex
        // and the view rect still distinguish left/right. This is not the default
        // because Subnautica 2's water/fog path expects a primary+secondary pair.
        init_options->set_stereo_pass(EStereoscopicPass::eSSP_PRIMARY);
    }

    if (vr->is_splitscreen_compatibility_enabled() || vr->is_sceneview_compatibility_enabled()) {
        int32_t w = vr->get_hmd_width();
        int32_t h = vr->get_hmd_height();

        int32_t x = 0;
        int32_t y = 0;

        if (!vr->is_using_afr() && true_index == 1 && !vr->is_native_stereo_fix_enabled()) {
            x += w;
        }

        FIntRect view_rect{x, y, x + w, y + h};

        vr->get_runtime()->update_matrices(0.1f, 10000.0f);

        const auto proj_mat = vr->get_projection_matrix((VRRuntime::Eye)(true_index));

        auto& init_options_view_origin = is_ue5 ? *(glm::vec3*)&init_options_ue5->view_origin : init_options->view_origin;
        auto& init_options_view_rect = is_ue5 ? init_options_ue5->view_rect : init_options->view_rect;
        auto& init_options_constrained_view_rect = is_ue5 ? init_options_ue5->constrained_view_rect : init_options->constrained_view_rect;
        auto& init_options_projection_matrix = init_options->projection_matrix;
        auto& init_options_projection_matrix_ue5 = init_options_ue5->projection_matrix;

        auto& init_options_view_rotation_matrix = init_options->view_rotation_matrix;
        auto& init_options_view_rotation_matrix_ue5 = init_options_ue5->view_rotation_matrix;

        const auto conversion_mat = glm::mat4 {
            0, 0, 1, 0,
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 0, 1
        };

        const auto conversion_mat_inverse = glm::inverse(conversion_mat);

        // We need to "undo" the operations done to create the rotation matrix so we can get the original angle
        // const auto view_rot_mat = conversion_mat * make_inverse_rot_matrix(euler); <-- this is the result of the conversion
        glm::vec3 euler{};

        if (is_ue5) {
            euler = utility::math::ue_euler_from_rotation_matrix(glm::inverse(conversion_mat_inverse * glm::mat4{init_options_view_rotation_matrix_ue5}));
        } else {
            euler = utility::math::ue_euler_from_rotation_matrix(glm::inverse(conversion_mat_inverse * init_options_view_rotation_matrix));
        }

        auto euler_d = glm::vec<3, double>{euler};
        auto euler_pointer = is_ue5 ? (Rotator<float>*)&euler_d : (Rotator<float>*)&euler;

        const auto native_stereo_explicit_view_index =
            subnautica2_is_current_game() &&
            vr->is_native_stereo_fix_enabled() &&
            !vr->is_native_stereo_fix_same_pass_enabled();
        g_hook->calculate_stereo_view_offset_(
            native_stereo_explicit_view_index ? true_index : true_index + 1,
            euler_pointer,
            100.0f,
            &init_options_view_origin);

        if (is_ue5) {
            euler = euler_d;
        }

        const auto view_rot_mat = conversion_mat * utility::math::ue_inverse_rotation_matrix(euler);

        *(FIntRect*)&init_options_view_rect = view_rect;
        *(FIntRect*)&init_options_constrained_view_rect = view_rect;

        if (is_ue5) {
            init_options_view_rotation_matrix_ue5 = view_rot_mat;

            if (!vr->is_using_2d_screen()) {
                init_options_projection_matrix_ue5 = proj_mat;
            }
        } else {
            init_options_view_rotation_matrix = view_rot_mat;

            if (!vr->is_using_2d_screen()) {
                init_options_projection_matrix = proj_mat;
            }
        }
    }

    const auto init_options_stereo_pass = init_options->get_stereo_pass();

    if (vr->is_native_stereo_fix_enabled() && !vr->is_native_stereo_fix_same_pass_enabled()) {
        auto init_options_view_rect = is_ue5 ? init_options_ue5->view_rect : init_options->view_rect;
        auto init_options_constrained_view_rect = is_ue5 ? init_options_ue5->constrained_view_rect : init_options->constrained_view_rect;

        const auto view_rect_valid =
            init_options_view_rect[2] > init_options_view_rect[0] &&
            init_options_view_rect[3] > init_options_view_rect[1];
        const auto constrained_rect_empty =
            init_options_constrained_view_rect[2] <= init_options_constrained_view_rect[0] ||
            init_options_constrained_view_rect[3] <= init_options_constrained_view_rect[1];

        if (view_rect_valid && constrained_rect_empty) {
            memcpy(init_options_constrained_view_rect, init_options_view_rect, sizeof(int32_t) * 4);
            SPDLOG_INFO_ONCE("[NativeStereoDebug] Filled empty constrained view rect from view rect for native stereo fix");
        }
    }

    {
        static uint32_t log_count = 0;

        if (vr->is_native_stereo_fix_enabled() && log_count < sn2_native_stereo_debug_log_max()) {
            const auto& init_options_view_rect = is_ue5 ? init_options_ue5->view_rect : init_options->view_rect;
            const auto& init_options_constrained_view_rect = is_ue5 ? init_options_ue5->constrained_view_rect : init_options->constrained_view_rect;
            SPDLOG_INFO("[NativeStereoDebug] FSceneView ctor last_index={} true_index={} stereo_pass={} view_rect={} {} {} {} constrained={} {} {} {} scene_state={:x}",
                last_index, true_index, init_options_stereo_pass,
                init_options_view_rect[0], init_options_view_rect[1], init_options_view_rect[2], init_options_view_rect[3],
                init_options_constrained_view_rect[0], init_options_constrained_view_rect[1], init_options_constrained_view_rect[2], init_options_constrained_view_rect[3],
                (uintptr_t)init_options_scene_state);
            ++log_count;
        }
    }

    std::optional<uint32_t> views_original_count{};

    if (vr->is_native_stereo_fix_enabled() && vr->is_native_stereo_fix_same_pass_enabled() && init_options_stereo_pass > EStereoscopicPass::eSSP_PRIMARY) {
        if (g_hook->get_render_target_manager()->get_scene_capture_render_target() != nullptr) {
            init_options->set_stereo_pass(EStereoscopicPass::eSSP_PRIMARY);

            auto view_family = init_options->get_view_family();
            auto views = view_family != nullptr ? view_family->get_views() : nullptr;

            if (views != nullptr) {
                // Hide the fact that we have multiple views from the FSceneView constructor.
                // At least 1 view causes special stereo logic to run in the constructor.
                // Notably I've seen more than 1 view causing crashes on UE5 with the native stereo fix without doing this.
                views_original_count = views->count;
                views->count = 0;
            }
        }
    }

    bool new_scene_state_inserted_this_frame = false;

    if (init_options_scene_state != nullptr && !g_hook->m_sceneview_data.known_scene_states.contains(init_options_scene_state)) {
        SPDLOG_INFO("Inserting new scene state {:x}", (uintptr_t)init_options_scene_state);
        known_scene_states.insert(init_options_scene_state);
        new_scene_state_inserted_this_frame = true;
    } else if (init_options_scene_state == nullptr) {
        SPDLOG_ERROR_ONCE("Scene state passed to FSceneView constructor is null");

        if ((int32_t)init_options_stereo_pass < 0) {
            SPDLOG_ERROR_ONCE("Stereo pass is negative");
        }
    }

    if (init_options_scene_state != nullptr && !new_scene_state_inserted_this_frame && vr->is_ghosting_fix_enabled() && !known_scene_states.empty() && vr->is_using_afr() && true_index == 1) {
        init_options->set_stereo_pass(EStereoscopicPass::eSSP_PRIMARY);

        // Set the scene state to the one that isn't the current one
        for (auto scene_state : known_scene_states) {
            if (scene_state != init_options_scene_state) {
                SPDLOG_INFO_ONCE("Setting scene state to {:x}", (uintptr_t)scene_state);
                init_options->set_scene_state(scene_state);
                break;
            }
        }
    }

    last_index++;

    auto result = g_hook->m_sceneview_data.constructor_hook.unsafe_call<sdk::FSceneView*>(view, init_options, a3, a4);

    if (result != nullptr && is_ue5 && vr->is_native_stereo_fix_enabled() && !vr->is_native_stereo_fix_same_pass_enabled()) {
        // UE derives these from platform stereo aspects. Explicit two-view stereo still
        // needs the secondary eye to keep UE's primary-view link for shared exposure,
        // LUTs, translucency lighting and other paired stereo resources.
        auto raw_view = (uint8_t*)result;
        const auto constructed_stereo_pass = *(uint32_t*)(raw_view + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET);

        if (constructed_stereo_pass == EStereoscopicPass::eSSP_PRIMARY || constructed_stereo_pass == EStereoscopicPass::eSSP_SECONDARY) {
            static uint32_t post_ctor_log_count = 0;

            if (post_ctor_log_count < sn2_native_stereo_debug_log_max()) {
                SPDLOG_INFO("[NativeStereoDebug] FSceneView post-ctor view={:x} pass={} stereo_view_index={} primary_view_index={} instanced={} singlepass={} multiviewport={} mobile_multiview={} bind_instanced_ub={} underwater_depth={} water_intersection={} aspects=0x{:02x}",
                    (uintptr_t)result,
                    constructed_stereo_pass,
                    *(int32_t*)(raw_view + SUBNAUTICA2_SCENEVIEW_STEREO_VIEW_INDEX_OFFSET),
                    *(int32_t*)(raw_view + SUBNAUTICA2_SCENEVIEW_PRIMARY_VIEW_INDEX_OFFSET),
                    raw_view[SUBNAUTICA2_SCENEVIEW_INSTANCED_STEREO_ENABLED_OFFSET],
                    raw_view[SUBNAUTICA2_SCENEVIEW_SINGLE_PASS_STEREO_OFFSET],
                    raw_view[SUBNAUTICA2_SCENEVIEW_MULTI_VIEWPORT_ENABLED_OFFSET],
                    raw_view[SUBNAUTICA2_SCENEVIEW_MOBILE_MULTI_VIEW_ENABLED_OFFSET],
                    raw_view[SUBNAUTICA2_SCENEVIEW_SHOULD_BIND_INSTANCED_VIEW_UB_OFFSET],
                    *(float*)(raw_view + SUBNAUTICA2_SCENEVIEW_UNDERWATER_DEPTH_OFFSET),
                    raw_view[SUBNAUTICA2_SCENEVIEW_WATER_INTERSECTION_OFFSET],
                    raw_view[SUBNAUTICA2_SCENEVIEW_STEREO_ASPECTS_FLAGS_OFFSET]);
                ++post_ctor_log_count;
            }

            raw_view[SUBNAUTICA2_SCENEVIEW_INSTANCED_STEREO_ENABLED_OFFSET] = 0;
            raw_view[SUBNAUTICA2_SCENEVIEW_MULTI_VIEWPORT_ENABLED_OFFSET] = 0;
            raw_view[SUBNAUTICA2_SCENEVIEW_MOBILE_MULTI_VIEW_ENABLED_OFFSET] = 0;
            raw_view[SUBNAUTICA2_SCENEVIEW_SHOULD_BIND_INSTANCED_VIEW_UB_OFFSET] = 0;
            raw_view[SUBNAUTICA2_SCENEVIEW_SINGLE_PASS_STEREO_OFFSET] = 0;

            if (subnautica2_is_current_game() &&
                subnautica2_force_primary_primary_views() &&
                constructed_stereo_pass == EStereoscopicPass::eSSP_SECONDARY)
            {
                auto& stereo_pass = *(uint32_t*)(raw_view + SUBNAUTICA2_SCENEVIEW_STEREO_PASS_OFFSET);
                auto& stereo_view_index = *(int32_t*)(raw_view + SUBNAUTICA2_SCENEVIEW_STEREO_VIEW_INDEX_OFFSET);
                auto& primary_view_index = *(int32_t*)(raw_view + SUBNAUTICA2_SCENEVIEW_PRIMARY_VIEW_INDEX_OFFSET);

                static uint32_t force_primary_log_count = 0;
                if (force_primary_log_count < 16) {
                    SPDLOG_WARN(
                        "[Subnautica2][NativeStereoFix] Post-ctor forcing secondary view to PRIMARY for independent right-eye render test; view_index={} primary_index={} -> {}",
                        stereo_view_index,
                        primary_view_index,
                        stereo_view_index);
                    ++force_primary_log_count;
                }

                stereo_pass = (uint32_t)EStereoscopicPass::eSSP_PRIMARY;

                if (stereo_view_index >= 0) {
                    primary_view_index = stereo_view_index;
                }
            }

            if (subnautica2_is_current_game() && subnautica2_clear_stereo_aspects()) {
                auto& aspects = raw_view[SUBNAUTICA2_SCENEVIEW_STEREO_ASPECTS_FLAGS_OFFSET];

                if (aspects != 0) {
                    static uint32_t aspects_patch_log_count = 0;
                    if (aspects_patch_log_count < 16) {
                        SPDLOG_WARN(
                            "[Subnautica2][NativeStereoFix] Clearing FSceneView::Aspects flags 0x{:02x} -> 0x00 for explicit two-view native stereo test",
                            aspects);
                        ++aspects_patch_log_count;
                    }

                    aspects = 0;
                }
            }

            if (subnautica2_is_current_game() &&
                subnautica2_force_secondary_primary_view_index() &&
                constructed_stereo_pass == EStereoscopicPass::eSSP_SECONDARY)
            {
                auto& stereo_view_index = *(int32_t*)(raw_view + SUBNAUTICA2_SCENEVIEW_STEREO_VIEW_INDEX_OFFSET);
                auto& primary_view_index = *(int32_t*)(raw_view + SUBNAUTICA2_SCENEVIEW_PRIMARY_VIEW_INDEX_OFFSET);

                if (stereo_view_index >= 0 && primary_view_index != stereo_view_index) {
                    static uint32_t primary_index_patch_log_count = 0;
                    if (primary_index_patch_log_count < 16) {
                        SPDLOG_WARN(
                            "[Subnautica2][NativeStereoFix] Patching secondary PrimaryViewIndex {} -> {} for custom water/fog test",
                            primary_view_index,
                            stereo_view_index);
                        ++primary_index_patch_log_count;
                    }

                    primary_view_index = stereo_view_index;
                }
            }

            static uint32_t post_patch_log_count = 0;

            if (post_patch_log_count < sn2_native_stereo_debug_log_max()) {
                SPDLOG_INFO("[NativeStereoDebug] FSceneView after native patch view={:x} pass={} stereo_view_index={} primary_view_index={} instanced={} singlepass={} multiviewport={} mobile_multiview={} bind_instanced_ub={} underwater_depth={} water_intersection={} aspects=0x{:02x}",
                    (uintptr_t)result,
                    constructed_stereo_pass,
                    *(int32_t*)(raw_view + SUBNAUTICA2_SCENEVIEW_STEREO_VIEW_INDEX_OFFSET),
                    *(int32_t*)(raw_view + SUBNAUTICA2_SCENEVIEW_PRIMARY_VIEW_INDEX_OFFSET),
                    raw_view[SUBNAUTICA2_SCENEVIEW_INSTANCED_STEREO_ENABLED_OFFSET],
                    raw_view[SUBNAUTICA2_SCENEVIEW_SINGLE_PASS_STEREO_OFFSET],
                    raw_view[SUBNAUTICA2_SCENEVIEW_MULTI_VIEWPORT_ENABLED_OFFSET],
                    raw_view[SUBNAUTICA2_SCENEVIEW_MOBILE_MULTI_VIEW_ENABLED_OFFSET],
                    raw_view[SUBNAUTICA2_SCENEVIEW_SHOULD_BIND_INSTANCED_VIEW_UB_OFFSET],
                    *(float*)(raw_view + SUBNAUTICA2_SCENEVIEW_UNDERWATER_DEPTH_OFFSET),
                    raw_view[SUBNAUTICA2_SCENEVIEW_WATER_INTERSECTION_OFFSET],
                    raw_view[SUBNAUTICA2_SCENEVIEW_STEREO_ASPECTS_FLAGS_OFFSET]);
                ++post_patch_log_count;
            }
        }
    }

    // Reset the view count back to what it was.
    if (views_original_count.has_value()) {
        auto view_family = init_options->get_view_family();
        auto views = view_family != nullptr ? view_family->get_views() : nullptr;

        if (views != nullptr) {
            views->count = views_original_count.value();
        }
    }

    return result;
}

void FFakeStereoRenderingHook::setup_view_family(ISceneViewExtension* extension, sdk::FSceneViewFamily& view_family) {
    ZoneScopedN("SetupViewFamily");

    static bool once = true;

    if (once) {
        SPDLOG_INFO("Called SetupViewFamily for the first time");
        once = false;
    }

    if (!g_framework->is_game_data_intialized()) {
        return;
    }

    auto& vr = VR::get();

    if (!vr->is_hmd_active()) {
        return;
    }

    disable_native_instanced_stereo_cvars();
    force_native_stereo_view_family_show_flag(view_family, "SetupViewFamily");

    //vr->update_hmd_state(true, vr->get_runtime()->internal_frame_count + 1);
}

void FFakeStereoRenderingHook::setup_viewpoint(ISceneViewExtension* extension, void* player_controller, void* view_info) {
    ZoneScopedN("SetupViewPoint");
    SPDLOG_INFO_ONCE("Called SetupViewPoint for the first time");

    if (!g_framework->is_game_data_intialized()) {
        return;
    }

    auto& vr = VR::get();

    if (!vr->is_ghosting_fix_enabled() || g_hook->m_fixed_localplayer_view_count) {
        return;
    }

    // Using this as a way to get to the localplayer
    static bool attempted_hook{false};

    // Fix localplayer view count
    if (!attempted_hook) {
        SPDLOG_INFO("Attempting to find caller of ISceneViewExtension::SetupViewPoint");

        attempted_hook = true;
        const auto return_address = (uintptr_t)_ReturnAddress();
        const auto caller = utility::find_virtual_function_start(return_address);

        if (!caller) {
            SPDLOG_ERROR("Failed to find caller of ISceneViewExtension::SetupViewPoint");
            return;
        }

        // No need to StartDisabled on this because we're on the same thread.
        g_hook->m_localplayer_get_viewpoint_hook = safetyhook::create_inline(*caller, (uintptr_t)&localplayer_setup_viewpoint);
        
        if (!g_hook->m_localplayer_get_viewpoint_hook) {
            SPDLOG_ERROR("Failed to hook ISceneViewExtension::SetupViewPoint");
            return;
        }

        SPDLOG_INFO("Hooked ISceneViewExtension::SetupViewPoint");
    }
}

void FFakeStereoRenderingHook::localplayer_setup_viewpoint(void* localplayer, void* view_info, void* pass) {
    ZoneScopedN("LocalPlayerSetupViewPoint");
    SPDLOG_INFO_ONCE("Called LocalPlayerSetupViewPoint for the first time");

    if (!g_hook->m_fixed_localplayer_view_count) {
        static bool attempted = false;

        if (!attempted) {
            attempted = true;

            if (localplayer != nullptr && !IsBadReadPtr(localplayer, sizeof(void*))) try {
                g_hook->post_init_properties((uintptr_t)localplayer);
            } catch(...) {
                SPDLOG_ERROR("[LocalPlayerSetupViewPoint] Failed to post init properties");
            }
        }
    }

    g_hook->m_localplayer_get_viewpoint_hook.call<void>(localplayer, view_info, pass);
}

void FFakeStereoRenderingHook::begin_render_viewfamily_real(void* render_module, sdk::FCanvas* canvas, sdk::FSceneViewFamily* view_family_candidate) {
    ZoneScopedN("BeginRenderViewFamilyReal");
    const auto begin_render_viewfamily_real_start = std::chrono::steady_clock::now();
    utility::ScopeGuard begin_render_viewfamily_real_timing_guard{[&]() {
        g_begin_render_viewfamily_real_timing.add(std::chrono::steady_clock::now() - begin_render_viewfamily_real_start);
        log_engine_render_timing_if_needed();
    }};

    SPDLOG_INFO_ONCE("Called BeginRenderViewFamilyReal for the first time");

    if (!g_framework->is_game_data_intialized()) {
        g_hook->m_render_module_begin_render_viewfamily_hook.unsafe_call<void>(render_module, canvas, view_family_candidate);
        return;
    }

    auto& vr = VR::get();
    auto rtm = g_hook->get_render_target_manager();

    if (!vr->is_hmd_active() || !vr->is_native_stereo_fix_enabled()) {
        avowed_native_fix_gate_reset("hmd inactive or native stereo fix disabled");
        rtm->destroy_scene_capture();

        g_hook->m_render_module_begin_render_viewfamily_hook.unsafe_call<void>(render_module, canvas, view_family_candidate);
        return;
    }

    struct TArrayViewViewFamily {
        sdk::FSceneViewFamily** data;
        uint32_t count;
    };

    const auto uses_tarrayview = sdk::FSceneViewFamily::has_vtable() && *(void**)view_family_candidate != sdk::FSceneViewFamily::get_vtable_ptr();
    const auto ue5_view_family_array = (TArrayViewViewFamily*)view_family_candidate;

    if (uses_tarrayview && ue5_view_family_array->data == nullptr) {
        g_hook->m_render_module_begin_render_viewfamily_hook.unsafe_call<void>(render_module, canvas, view_family_candidate);
        return;
    }

    // UE5 passes an TArrayView of ViewFamily pointers instead of a single ViewFamily
    sdk::FSceneViewFamily* view_family = uses_tarrayview ? ue5_view_family_array->data[0] : view_family_candidate;

    if (view_family != nullptr) {
        force_native_stereo_view_family_show_flag(*view_family, "BeginRenderViewFamilyReal");
    }

    auto views_ptr = view_family->get_views();
    if (views_ptr == nullptr) {
        g_hook->m_render_module_begin_render_viewfamily_hook.unsafe_call<void>(render_module, canvas, view_family_candidate);
        return;
    }

    auto& views = *views_ptr;
    const auto prev_count = views.count;

    if (!vr->is_native_stereo_fix_same_pass_enabled()) {
        avowed_native_fix_gate_reset("normal native stereo path");
        rtm->destroy_scene_capture();

        g_hook->m_render_module_begin_render_viewfamily_hook.unsafe_call<void>(render_module, canvas, view_family_candidate);
        return;
    }

    if (auto view_family_target = view_family->get_render_target(); view_family_target != nullptr) {
        g_hook->try_adopt_scene_viewport_render_target(
            reinterpret_cast<sdk::FViewport*>(view_family_target),
            "BeginRenderingViewFamily RenderTarget");
    }

    const auto rt = rtm->get_scene_capture_utexture();
    const auto rtrsrc = rt != nullptr ? (sdk::FTextureRenderTargetResource*)rt->get_resource() : nullptr;
    const auto rtfrt = rtrsrc != nullptr ? rtrsrc->as_render_target() : nullptr;
    const auto scene_capture_rhi = rtm->get_scene_capture_render_target();
    const auto scene_capture_native = avowed_try_get_native_resource(scene_capture_rhi);

    if (avowed_is_current_game()) {
        SPDLOG_INFO_EVERY_N_SEC(
            2,
            "[Avowed][NativeStereoFix] BeginRenderViewFamilyReal: uses_tarrayview={} views={} scene_utexture={} resource={} render_target={} capture_rhi={:x} capture_native={:x} fixed_localplayer={}",
            uses_tarrayview,
            views.count,
            rt != nullptr,
            rtrsrc != nullptr,
            rtfrt != nullptr,
            (uintptr_t)scene_capture_rhi,
            scene_capture_native,
            g_hook->m_fixed_localplayer_view_count);
    }

    if (rtfrt == nullptr) {
        avowed_native_fix_gate_update(
            (uintptr_t)view_family->get_scene_interface(),
            0,
            (uintptr_t)scene_capture_rhi,
            scene_capture_native,
            false);

        // This is fine to call constantly because we use an in-flight render target
        // that gets unset after the texture is fully created. This function exits early otherwise.
        rtm->create_scene_capture();
        views.count = 1;
        g_hook->m_render_module_begin_render_viewfamily_hook.unsafe_call<void>(render_module, canvas, view_family_candidate);
        views.count = prev_count;
        return;
    }

    auto view_family_target = view_family->get_render_target();
    const auto view_family_scene = view_family->get_scene_interface();

    if (view_family_target == nullptr) {
        avowed_native_fix_gate_update(
            (uintptr_t)view_family_scene,
            0,
            (uintptr_t)scene_capture_rhi,
            scene_capture_native,
            false);

        g_hook->m_render_module_begin_render_viewfamily_hook.unsafe_call<void>(render_module, canvas, view_family_candidate);
        return;
    }

    uint32_t avowed_gate_stable_frames = 0;
    uint32_t avowed_gate_required_frames = AVOWED_NATIVE_FIX_STABLE_FRAMES;
    const auto avowed_gate_ready = avowed_native_fix_gate_update(
        (uintptr_t)view_family_scene,
        (uintptr_t)view_family_target,
        (uintptr_t)scene_capture_rhi,
        scene_capture_native,
        rtfrt != nullptr && scene_capture_rhi != nullptr,
        &avowed_gate_stable_frames,
        &avowed_gate_required_frames);

    bool wants_swap = false;
    if (views.count > 1) {
        views.count = 1;
        wants_swap = !avowed_is_current_game() || avowed_gate_ready;

        if (avowed_is_current_game() && !avowed_gate_ready) {
            SPDLOG_INFO_EVERY_N_SEC(
                2,
                "[Avowed][NativeStereoFix] Suppressing right-eye pass during render transition stabilization stable={}/{}",
                avowed_gate_stable_frames,
                avowed_gate_required_frames);
        }

        if (wants_swap) {
            auto runtime = vr->get_runtime();
            const auto frame_count = runtime->internal_frame_count;

            // We need to clone the VR state from last frame to this frame
            if (runtime->is_openxr()) {
                auto openxr = (runtimes::OpenXR*)runtime;
                std::scoped_lock __{ openxr->sync_assignment_mtx };

                const auto last_frame = (frame_count) % runtimes::OpenXR::QUEUE_SIZE;
                const auto now_frame = (frame_count + 1) % runtimes::OpenXR::QUEUE_SIZE;
                openxr->pipeline_states[now_frame] = openxr->pipeline_states[last_frame];
                openxr->pipeline_states[now_frame].frame_count = now_frame;
            } else {
                auto openvr = (runtimes::OpenVR*)runtime;
                std::unique_lock __{ openvr->pose_mtx };

                const auto last_frame = (frame_count) % openvr->pose_queue.size();
                const auto now_frame = (frame_count + 1) % openvr->pose_queue.size();
                openvr->pose_queue[now_frame] = openvr->pose_queue[last_frame];
            }
        }

        /*auto init_options = (sdk::FSceneViewInitOptions*)((uintptr_t)view_family.views.data[0] + INIT_OPTIONS_OFFSET);
        init_options->stereo_pass = 0;

        auto init_options2 = (sdk::FSceneViewInitOptions*)((uintptr_t)view_family.views.data[1] + INIT_OPTIONS_OFFSET);
        init_options2->stereo_pass = 0;

        std::array<uint8_t, 0x500> init_options_copy{};
        std::array<uint8_t, 0x500> init_options_copy2{};

        memcpy(init_options_copy.data(), init_options, 0x500);
        view_family.views.data[0]->constructor((sdk::FSceneViewInitOptions*)init_options_copy.data()); // Triggers our hook as well

        memcpy(init_options_copy2.data(), init_options2, 0x500);
        view_family.views.data[1]->constructor((sdk::FSceneViewInitOptions*)init_options_copy2.data());*/
    }

    g_hook->m_render_module_begin_render_viewfamily_hook.unsafe_call<void>(render_module, canvas, view_family_candidate);

    if (wants_swap) {
        if (avowed_is_current_game()) {
            SPDLOG_INFO_EVERY_N_SEC(2, "[Avowed][NativeStereoFix] Executing right-eye second render pass into scene capture target");
        }

        // Swap out the existing render target for our custom one
        // Also, the entire point of swapping the render target
        // instead of "just" re-using the existing one is that doing that causes a 90% FPS drop
        // because the engine is still working on the old render target
        const auto original_target = view_family_target;

        view_family->set_render_target(rtfrt);

        auto scene = (sdk::FScene*)view_family->get_scene_interface();

        if (scene != nullptr) {
            // We decrement the frame count because it fixes motion vectors in the right eye.
            scene->decrement_frame_count();
        }
        
        std::swap(views[0], views[1]);

        // Call it again
        g_hook->m_render_module_begin_render_viewfamily_hook.unsafe_call<void>(render_module, canvas, view_family_candidate);

        std::swap(views[0], views[1]);

        view_family->set_render_target(original_target);
    }

    views.count = prev_count;
}

void FFakeStereoRenderingHook::begin_render_viewfamily(ISceneViewExtension* extension, sdk::FSceneViewFamily& view_family) {
    ZoneScopedN("BeginRenderViewFamily");
    const auto begin_render_viewfamily_start = std::chrono::steady_clock::now();
    utility::ScopeGuard begin_render_viewfamily_timing_guard{[&]() {
        g_begin_render_viewfamily_timing.add(std::chrono::steady_clock::now() - begin_render_viewfamily_start);
        log_engine_render_timing_if_needed();
    }};

    SPDLOG_INFO_ONCE("Called BeginRenderViewFamily for the first time");

    if (!g_framework->is_game_data_intialized()) {
        return;
    }

    if (!g_hook->has_scene_view_family_offsets_ready() &&
        sdk::FSceneViewFamily::update_offsets(&view_family, g_hook->get_render_target_manager()->get_viewport()))
    {
        g_hook->note_scene_view_family_offsets_ready();
    }

    if (auto view_family_target = view_family.get_render_target(); view_family_target != nullptr) {
        g_hook->try_adopt_scene_viewport_render_target(
            reinterpret_cast<sdk::FViewport*>(view_family_target),
            "FSceneViewFamily::RenderTarget");
    }

    auto si = view_family.get_scene_interface();

    if (si != nullptr) {
        sdk::FScene::update_offsets((sdk::FScene*)si);
    }

    if (!g_hook->has_engine_tick_hook()) {
        // Alternative place of running game thread work.
        GameThreadWorker::get().execute();
    }

    auto& vr = VR::get();

    if (!vr->is_hmd_active()) {
        return;
    }

    disable_native_instanced_stereo_cvars();
    force_native_stereo_view_family_show_flag(view_family, "BeginRenderViewFamily");

    const auto frame_count = *(uint32_t*)((uintptr_t)&view_family + SceneViewExtensionAnalyzer::frame_count_offset);
    auto views_ptr = view_family.get_views();

    if (vr->is_native_stereo_fix_enabled() && views_ptr != nullptr) {
        static uint32_t log_count = 0;

        if (log_count < sn2_native_stereo_debug_log_max() && views_ptr->count > 0 && views_ptr->count < 8) {
            SPDLOG_INFO("[NativeStereoDebug] BeginRenderViewFamily frame={} views_count={}", frame_count, views_ptr->count);

            for (uint32_t i = 0; i < (uint32_t)views_ptr->count; ++i) {
                auto view = views_ptr->data[i];

                if (view == nullptr) {
                    SPDLOG_INFO("[NativeStereoDebug] BeginRenderViewFamily view[{}]=null", i);
                    continue;
                }

                auto init_options = (sdk::FSceneViewInitOptionsUE5*)((uintptr_t)view + INIT_OPTIONS_OFFSET);
                const auto stereo_pass = init_options->get_stereo_pass();
                SPDLOG_INFO("[NativeStereoDebug] BeginRenderViewFamily view[{}]={:x} stereo_pass={} rect={} {} {} {} constrained={} {} {} {} scene_state={:x}",
                    i, (uintptr_t)view, stereo_pass,
                    init_options->view_rect[0], init_options->view_rect[1], init_options->view_rect[2], init_options->view_rect[3],
                    init_options->constrained_view_rect[0], init_options->constrained_view_rect[1], init_options->constrained_view_rect[2], init_options->constrained_view_rect[3],
                    (uintptr_t)init_options->get_scene_state());
            }

            ++log_count;
        }
    }

    subnautica2_sync_native_stereo_water_state(view_family, frame_count);

    //vr->update_hmd_state(true, frame_count);
    auto runtime = vr->get_runtime();
    runtime->internal_frame_count = frame_count;
    runtime->on_pre_render_game_thread(frame_count);

    // This is a HACKHACKHACK to get splitscreen working on around 4.20 to 4.27 something
    // This is completely borked on UE5
    // We can probably do it better inside the sceneview constructor hook, but that needs to be handled with care
    if (vr->is_splitscreen_compatibility_enabled() && views_ptr != nullptr) {
        auto& views = *views_ptr;
        
        // B = dst, A = src
        static auto copy_init_options_from = [](const sdk::FSceneView& a, sdk::FSceneView& b) {
            std::scoped_lock _{g_hook->m_sceneview_data.mtx};
            auto init_options_a = (sdk::FSceneViewInitOptions*)((uintptr_t)&a + INIT_OPTIONS_OFFSET);
            auto init_options_b = (sdk::FSceneViewInitOptions*)((uintptr_t)&b + INIT_OPTIONS_OFFSET);

            auto& cached_init_options = g_hook->m_sceneview_data.view_init_options_ue4;

            if (auto it = cached_init_options.find(init_options_a->scene_view_state); it != cached_init_options.end()) {
                const auto& vio_entry = it->second;
                //memcpy(init_options_b, &vio_entry, sizeof(sdk::FSceneViewInitOptionsUE4));
                init_options_b->view_origin = vio_entry.view_origin;
                init_options_b->view_rotation_matrix = vio_entry.view_rotation_matrix;
                *(FIntRect*)&init_options_b->view_rect = *(FIntRect*)&vio_entry.view_rect;
                *(FIntRect*)&init_options_b->constrained_view_rect = *(FIntRect*)&vio_entry.constrained_view_rect;
                init_options_b->projection_matrix = vio_entry.projection_matrix;
                return;
            }

            // Otherwise just do this crap
            init_options_b->view_origin = init_options_a->view_origin;
            init_options_b->view_rotation_matrix = init_options_a->view_rotation_matrix;
            *(FIntRect*)&init_options_b->view_rect = *(FIntRect*)&init_options_a->view_rect;
            *(FIntRect*)&init_options_b->constrained_view_rect = *(FIntRect*)&init_options_a->constrained_view_rect;
            init_options_b->projection_matrix = init_options_a->projection_matrix;
        };

        auto do_splitscreen = [&](int32_t view_index) {
            int32_t w = vr->get_hmd_width();
            int32_t h = vr->get_hmd_height();

            int32_t x = 0;
            int32_t y = 0;

            const auto true_index = vr->is_using_afr() ? (frame_count + 1) % 2 : view_index;

            if (!vr->is_using_afr() && true_index == 1) {
                x += w;
            }

            auto view = views.data[view_index % views.count];

            FIntRect view_rect{x, y, x + w, y + h};

            auto& vr = VR::get();

            VR::get()->get_runtime()->update_matrices(0.1f, 10000.0f);

            const auto proj_mat = VR::get()->get_projection_matrix((VRRuntime::Eye)(true_index));

            std::array<uint8_t, 0x500> init_options_copy{};

            auto init_options = (sdk::FSceneViewInitOptions*)((uintptr_t)view + INIT_OPTIONS_OFFSET);

            auto& init_options_view_origin = init_options->view_origin;
            auto& init_options_view_rotation_matrix = init_options->view_rotation_matrix;
            auto& init_options_view_rect = *(FIntRect*)&init_options->view_rect;
            auto& init_options_constrained_view_rect = *(FIntRect*)&init_options->constrained_view_rect;
            auto& init_options_projection_matrix = init_options->projection_matrix;
            auto& init_options_stereo_pass = init_options->stereo_pass;

            // ADDENDUM: The sceneview constructor hook handles the rotation logic now.
            /*const auto conversion_mat = glm::mat4 {
                0, 0, 1, 0,
                1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 0, 1
            };

            const auto conversion_mat_inverse = glm::inverse(conversion_mat);*/

            // We need to "undo" the operations done to create the rotation matrix so we can get the original angle
            // const auto view_rot_mat = conversion_mat * make_inverse_rot_matrix(euler); <-- this is the result of the conversion
            //auto euler = utility::math::ue_euler_from_rotation_matrix(glm::inverse(conversion_mat_inverse * init_options_view_rotation_matrix));
            //g_hook->calculate_stereo_view_offset_(true_index + 1, (Rotator<float>*)&euler, 100.0f, &init_options_view_origin);
            //const auto view_rot_mat = conversion_mat * utility::math::ue_inverse_rotation_matrix(euler);
            //init_options_view_rotation_matrix = view_rot_mat;

            init_options_view_rect = view_rect;
            init_options_constrained_view_rect = view_rect;
            init_options_projection_matrix = proj_mat;

            memcpy(init_options_copy.data(), init_options, 0x500);
            view->constructor((sdk::FSceneViewInitOptions*)init_options_copy.data()); // Triggers our hook as well
        };

        const auto requested_index = vr->get_requested_splitscreen_index();
        const auto final_index = std::min<uint32_t>(views.count - 1, requested_index);
        const auto other_index = final_index != 0 ? 0 : 1;

        if (final_index > 0) {
            if (views.count > 1) {
                copy_init_options_from(*views.data[final_index], *views.data[other_index]);
            }

            if (!vr->is_using_afr()) {
                if (views.count > 1) {
                    do_splitscreen(other_index);
                } else {
                    do_splitscreen(0);
                }
            } else {
                do_splitscreen(0);
            }
        }
    }

    // If we couldn't find GetDesiredNumberOfViews, we need to set the view count to 1 as a workaround
    // TODO: Check if this can cause a memory leak, I don't know who is resonsible
    // for destroying the views in the array
    // This check might seem kind of arbitrary, but sometimes (rarely) the offset
    // for the views can be wrong so if the count is some sane number
    // then we can assume that the offset is correct
    if (vr->is_using_afr() && views_ptr != nullptr && views_ptr->count >= 2 && views_ptr->count <= 4) {
        SPDLOG_INFO_ONCE("Setting view count to 1 (from {})", views_ptr->count);
        views_ptr->count = 1;
    }


    using BeginRenderViewFamilyRealFn = void(*)(void*, sdk::FCanvas*, sdk::FSceneViewFamily*);
    static BeginRenderViewFamilyRealFn begin_rendering_view_family_real_fn = nullptr;
    static bool already_tried = false;
    if (begin_rendering_view_family_real_fn == nullptr &&
        !already_tried &&
        vr->is_native_stereo_fix_enabled() &&
        vr->is_native_stereo_fix_same_pass_enabled())
    {
        already_tried = true;

        // Get callstack
        constexpr auto max_stack_depth = 100;
        uintptr_t stack[max_stack_depth]{};

        const auto depth = RtlCaptureStackBackTrace(0, max_stack_depth, (void**)&stack, nullptr);
        uintptr_t mid = 0;

        for (int i = 1; i < depth; i++) {
            SPDLOG_INFO(" {:x}", (uintptr_t)stack[i]);
            mid = stack[i];
            break;
        }

        if (mid != 0) {
            const auto candidate = utility::find_virtual_function_start(mid);

            if (candidate) {
                begin_rendering_view_family_real_fn = (BeginRenderViewFamilyRealFn)*candidate;

                if (begin_rendering_view_family_real_fn != nullptr) {
                    SPDLOG_INFO("Found BeginRenderingViewFamily real function at {:x}", (uintptr_t)begin_rendering_view_family_real_fn);

                    g_hook->m_render_module_begin_render_viewfamily_hook = safetyhook::create_inline((uintptr_t)begin_rendering_view_family_real_fn, (uintptr_t)&begin_render_viewfamily_real);

                    if (g_hook->m_render_module_begin_render_viewfamily_hook) {
                        SPDLOG_INFO("Hooked BeginRenderingViewFamily real function");
                    } else {
                        SPDLOG_ERROR("Failed to hook BeginRenderingViewFamily real function");
                    }
                } else {
                    SPDLOG_ERROR("Failed to find BeginRenderingViewFamily real function");
                }
            } else {
                SPDLOG_ERROR("Failed to find BeginRenderingViewFamily real function");
            }
        }
    }
}

void FFakeStereoRenderingHook::pre_render_viewfamily_renderthread(ISceneViewExtension* extension, sdk::FRHICommandListBase* cmd_list, sdk::FSceneViewFamily& view_family) {
    ZoneScopedN("PreRenderViewFamily_RenderThread");
    const auto prerender_viewfamily_rt_start = std::chrono::steady_clock::now();
    utility::ScopeGuard prerender_viewfamily_rt_timing_guard{[&]() {
        g_prerender_viewfamily_rt_timing.add(std::chrono::steady_clock::now() - prerender_viewfamily_rt_start);
        log_engine_render_timing_if_needed();
    }};

    utility::ScopeGuard _{[]() {
        RenderThreadWorker::get().execute();
    }};
    
    SPDLOG_INFO_ONCE("Called PreRenderViewFamily_RenderThread for the first time");
    
    if (!g_framework->is_game_data_intialized()) {
        return;
    }

    auto& vr = VR::get();

    if (!vr->is_hmd_active()) {
        return;
    }

    g_hook->note_prerender_viewfamily_seen();

    static size_t execution_count{0};

    // This should 100% only get executed if the headset is on, because
    // FFakeStereoRenderingHook::render_texture_render_thread is the first fallback for hooking
    // And we don't want to miss that unintentionally
    if (g_hook->m_attempted_hook_slate_thread && !g_hook->m_slate_thread_hook && !g_hook->m_attempted_hook_slate_thread_alternate && execution_count++ >= 50) {
        SPDLOG_INFO("DrawWindow_RenderThread was not hooked after {} render calls, trying alternative hook", execution_count);

        g_hook->attempt_hook_slate_thread(0, true);
    }

    if (vr->is_stereo_emulation_enabled()) {
        return;
    }

    const auto frame_count = *(uint32_t*)((uintptr_t)&view_family + SceneViewExtensionAnalyzer::frame_count_offset);
    static uint32_t last_frame = 0;

    // We only want to run this logic on the first "frame" (left eye) passed through here
    // When using Native Stereo Fix
    if (vr->is_native_stereo_fix_enabled() && frame_count == last_frame) {
        return;
    }

    last_frame = frame_count;

    static bool is_ue5_rdg_builder = false;
    static uint32_t ue5_command_offset = 0;
    static bool analyzed_root_already = false;
    static bool is_old_command_base = false;

    if (is_ue5_rdg_builder) {
        cmd_list = *(sdk::FRHICommandListBase**)((uintptr_t)cmd_list + ue5_command_offset);
    }

    const auto compensation = g_hook->get_frame_delay_compensation();

    // Using slate's draw window hook is the safest way to do this without
    // false positives on the command list in this function
    // otherwise we can attempt to use the command list here and hook it
    // in the slate hook, a guaranteed proper command list is passed to the function
    // so we can use that to hook the command list
    // The main inspiration for this is UE5.0.3 because it passes an FRDGBuilder
    // which *does* contain the command list in it, but for whatever reason I can't
    // seem to hook it properly, so I'm using the slate hook instead
    // ADDENDUM: For now, I'm only using the slate hook for UE5.0.3.
    // But I'll use it as a fallback as well for when the command list appears to be empty
    // Reason being the slate hook doesn't appear to run every frame, so it's not a perfect solution
    auto enqueue_poses_on_slate_thread = [&]() {
        g_hook->get_slate_thread_worker()->enqueue([=](FRHICommandListImmediate* command_list) {
            static bool once_slate = true;

            if (once_slate) {
                SPDLOG_INFO("Called enqueued function on the Slate thread for the first time! Frame count: {}", frame_count);
                once_slate = false;
            }

            static size_t actual_offset = 0;
            auto l = (sdk::FRHICommandListBase*)((uintptr_t)command_list + actual_offset);
            const auto is_ue5 = g_hook->has_double_precision();

            if (l != nullptr && l->root != nullptr && ((uintptr_t)l->root & (sizeof(void*) - 1)) == 0) {
                auto new_root = (sdk::FRHICommandBase_New*)l->root;
                if (!analyzed_root_already) try {
                    // so all of this might seem really overkill but
                    // it's a good way to detect whether we have an FMemStack at the top of the command list
                    // which we need to skip on UE5.5+
                    if (utility::get_module_within(*(void**)l->root).value_or(nullptr) == nullptr || 
                        IsBadReadPtr(*(void**)l->root, sizeof(void*)) || 
                        utility::get_module_within(**(void***)l->root).value_or(nullptr) == nullptr ||
                        (!IsBadReadPtr(new_root->next, sizeof(void*)) && (utility::get_module_within(*(void**)new_root->next).value_or(nullptr) == nullptr || utility::get_module_within(**(void***)new_root->next).value_or(nullptr) == nullptr))
                    )
                {
                        if (is_ue5) {
                            // UE5 is NOT an old command list, we need to bruteforce the offset
                            // Start at 0x10 because that's usually where the pointers in FMemStack end.
                            for (size_t i = 0x10; i < 0x50; i += sizeof(void*)) try {
                                const auto cur_l = (sdk::FRHICommandListBase*)((uintptr_t)command_list + i);
                                if (utility::get_module_within(*(void**)cur_l->root).value_or(nullptr) != nullptr) {
                                    actual_offset = i;
                                    l = cur_l;
                                    SPDLOG_INFO("Found UE5.5+ command list at offset 0x{:x}", i);
                                    break;
                                }
                            } catch(...) {

                            }
                        } else {
                            SPDLOG_INFO("Old FRHICommandBase detected");
                            is_old_command_base = true;
                        }
                    } else {
                        SPDLOG_INFO("New FRHICommandBase detected");
                    }

                    analyzed_root_already = true;
                } catch(...) {
                    SPDLOG_ERROR("Failed to analyze FRHICommandBase");
                    analyzed_root_already = true;
                }

                if (!is_old_command_base) {
                    SceneViewExtensionAnalyzer::hook_new_rhi_command((sdk::FRHICommandBase_New*)l->root, frame_count + compensation);
                } else {
                    SceneViewExtensionAnalyzer::hook_old_rhi_command((sdk::FRHICommandBase_Old*)l->root, frame_count + compensation);
                }
            } else {
                // welp
                vr->get_runtime()->enqueue_render_poses(frame_count + compensation);
            }
        });
    };

    auto fall_back_to_slate_thread = [&]() {
        if (is_ue_5_7_or_newer()) {
            g_hook->m_prefer_slate_thread_for_session = true;
            save_ue57_slate_thread_preference(true);
        }

        if (is_ue_5_7_or_newer()) {
            g_hook->get_render_target_manager()->try_schedule_dedicated_ui_creation();
        }

        if (g_hook->has_slate_hook()) {
            enqueue_poses_on_slate_thread();
        } else {
            vr->get_runtime()->enqueue_render_poses(frame_count + compensation);
        }
    };

    if (is_ue_5_7_or_newer() &&
        g_hook->has_slate_hook() &&
        g_hook->has_seen_stable_slate_draw() &&
        !g_hook->has_successful_command_list_hijack() &&
        !g_hook->m_prefer_slate_thread_for_session &&
        g_hook->m_first_stable_slate_draw_at.time_since_epoch().count() != 0 &&
        std::chrono::steady_clock::now() - g_hook->m_first_stable_slate_draw_at > std::chrono::seconds(2))
    {
        SPDLOG_WARN_ONCE("[UE 5.7] Command-list path did not stabilize after the first Slate draw; preferring Slate-thread startup");
        fall_back_to_slate_thread();
        return;
    }

    if (is_ue_5_7_or_newer() && g_hook->m_prefer_slate_thread_for_session && g_hook->has_slate_hook()) {
        enqueue_poses_on_slate_thread();
        return;
    }

    // okay well I think this evaluates to false all the time
    // but apparently it has been working for a LONG TIME so I'm not going to touch this until after release
    // (the else statement still handles everything... fine?)
    const auto has_good_root = 
        cmd_list != nullptr &&
        ((uintptr_t)cmd_list & 1 == 0) &&
        cmd_list->root != nullptr &&
        ((uintptr_t)cmd_list->root & 1 == 0);

    // Hijack the top command in the command list so we can enqueue the render poses on the RHI thread
    if (has_good_root) {
        SPDLOG_INFO_ONCE("Command list root is good");

        if (!analyzed_root_already) try {
            auto root = cmd_list->root;

            auto analyze_for_ue5 = [&]() {
                // Find the real command list.
                is_ue5_rdg_builder = true;
                const auto rdg_builder = (uintptr_t)cmd_list;

                for (auto i = 0x10; i <= 0x100; i += sizeof(void*)) try {
                    const auto value = *(uintptr_t*)(rdg_builder + i);

                    if (value == 0 || IsBadReadPtr((void*)value, sizeof(void*))) {
                        continue;
                    }

                    if (utility::get_module_within((void*)value).has_value()) {
                        continue;
                    }

                    const auto value_deref = *(uintptr_t*)value;

                    if (value_deref == 0 || IsBadReadPtr((void*)value_deref, sizeof(void*))) {
                        continue;
                    }

                    if (utility::get_module_within((void*)value_deref).has_value()) {
                        continue;
                    }

                    const auto root_vtable = *(uintptr_t*)value_deref;

                    if (root_vtable == 0 || IsBadReadPtr((void*)root_vtable, sizeof(void*))) {
                        continue;
                    }

                    if (!utility::get_module_within((void*)root_vtable).has_value()) {
                        continue;
                    }

                    // Check that there is a valid function in the vtable
                    const auto first_function = *(uintptr_t*)root_vtable;

                    if (first_function == 0 || IsBadReadPtr((void*)first_function, sizeof(void*))) {
                        continue;
                    }

                    if (!utility::get_module_within((void*)first_function).has_value()) {
                        continue;
                    }

                    SPDLOG_INFO("Possible UE5 command list found at offset 0x{:x}", i);
                    ue5_command_offset = i;
                    cmd_list = (sdk::FRHICommandListBase*)value;
                    break;
                } catch(...) {
                    spdlog::error("Exception occurred while analyzing UE5 command list");
                }
            };

            // If we read the pointer at the start of the root and it's not a module, then it's the old FRHICommandBase
            // this is because all vtables reside within a module
            if (utility::get_module_within(*(void**)root).value_or(nullptr) == nullptr) {
                // UE5
                if (g_hook->has_double_precision()) {
                    analyze_for_ue5();

                    if (ue5_command_offset == 0) {
                        SPDLOG_ERROR("Failed to find UE5 command list, trying again next frame");
                        return;
                    }
                } else {
                    SPDLOG_INFO("Old FRHICommandBase detected");
                    is_old_command_base = true;
                }
            } else {
                SPDLOG_INFO("New FRHICommandBase detected");
            }

            analyzed_root_already = true;
        } catch(...) {
            SPDLOG_ERROR("Failed to analyze root command");
            analyzed_root_already = true;
        }

        if (g_hook->get_render_target_manager()->is_ue_5_0_3() && g_hook->has_slate_hook()) {
            enqueue_poses_on_slate_thread();
        } else try {
            if (!is_old_command_base) {
                SceneViewExtensionAnalyzer::hook_new_rhi_command((sdk::FRHICommandBase_New*)cmd_list->root, frame_count + compensation);
            } else {
                SceneViewExtensionAnalyzer::hook_old_rhi_command((sdk::FRHICommandBase_Old*)cmd_list->root, frame_count + compensation);
            }
        } catch(...) {
            SPDLOG_INFO_ONCE("Failed to hook command list, falling back to Slate thread hook");
            fall_back_to_slate_thread();
        }
    } else {
        SPDLOG_INFO_ONCE("Bad root or command list, falling back to Slate thread hook");
        fall_back_to_slate_thread();
    }
}

bool FFakeStereoRenderingHook::setup_view_extensions() try {
    SPDLOG_INFO("Attempting to set up view extensions...");

    auto engine = sdk::UEngine::get();

    if (engine == nullptr) {
        SPDLOG_ERROR("Failed to get engine pointer! Cannot set up view extensions!");
        return false;
    }

    const auto active_stereo_device = locate_active_stereo_rendering_device();

    if (!active_stereo_device || !s_stereo_rendering_device_offset) {
        SPDLOG_ERROR("Failed to locate active stereo rendering device!");
        return false;
    }

    // This is a proof of concept at the moment for newer UE versions
    // older versions may not work or crash.
    // TODO: Figure out older versions.
    constexpr auto weak_ptr_size = sizeof(TWeakPtr<void*>);
    static const auto potential_hmd_device_offset = s_stereo_rendering_device_offset + weak_ptr_size;
    static const uintptr_t potential_hmd_device = (uintptr_t)engine + potential_hmd_device_offset;
    static const uintptr_t potential_view_extensions = (uintptr_t)engine + s_stereo_rendering_device_offset + (weak_ptr_size * 2); // 2 to skip over the XRSystem

    // This can happen if the game left a VR plugin in it
    // Usually this isn't an issue, but some games can leave a valid HMDDevice or XRSystem laying around for whatever reason
    // If this isn't cleaned up, the game will crash because it tries to gather view extensions from the existing device
    // and the view extensions it gathered will cause a crash when calling them. also the HMD device itself can cause a crash, it's not actually initialized.
    if (*(void**)potential_hmd_device != nullptr) {
        // Double check that we're actually replacing a pointer and not an integer or something
        if (!IsBadReadPtr(*(void**)potential_hmd_device, sizeof(void*))) {
            SPDLOG_INFO("Found an existing HMDDevice or XRSystem, nullifying it...");
            static std::vector<uintptr_t> replacement_vtable{};

            for (auto i = 0; i < 200; ++i) {
                replacement_vtable.push_back((uintptr_t)+[]() { return nullptr; });
            }

            //**(void***)potential_hmd_device = replacement_vtable.data();
            *(void**)potential_hmd_device = nullptr;
            m_fixed_localplayer_view_count = true; // If this is already allocated, then there's already a second view for us to use
        }

        if (!IsBadReadPtr(*(void**)(potential_hmd_device + sizeof(void*)), sizeof(void*))) {
            *(void**)(potential_hmd_device + sizeof(void*)) = nullptr;
        }
    }

    m_tracking_system_hook = std::make_unique<IXRTrackingSystemHook>(this, potential_hmd_device_offset);
    m_components.push_back(m_tracking_system_hook.get());

    // Add a vectored exception handler that catches attempted dereferences of a null XRSystem or HMDDevice
    // The exception handler will then patch out the instructions causing the crash and continue execution
    AddVectoredExceptionHandler(1, [](PEXCEPTION_POINTERS exception) -> LONG {
        static std::vector<Patch::Ptr> xrsystem_patches{};
        static std::unordered_set<uintptr_t> ignored_addresses{};

        if (exception->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
            const auto exception_address = exception->ContextRecord->Rip;

            if (subnautica2_is_current_game()) {
                constexpr uintptr_t subnautica2_vsm_null_projection_start_rva = 0x2FD10D0;
                constexpr uintptr_t subnautica2_vsm_null_projection_end_rva = 0x2FD1205;
                constexpr uintptr_t subnautica2_vsm_null_projection_epilogue_rva = 0x2FD1205;
                const auto exe_base = (uintptr_t)utility::get_executable();

                if (exe_base != 0 &&
                    exception_address >= exe_base + subnautica2_vsm_null_projection_start_rva &&
                    exception_address < exe_base + subnautica2_vsm_null_projection_end_rva)
                {
                    SPDLOG_WARNING_EVERY_N_SEC(
                        2,
                        "[Subnautica2][NativeStereoFix] Skipping virtual shadow map projection helper after access violation at {:x}",
                        exception_address);
                    exception->ContextRecord->Rip = exe_base + subnautica2_vsm_null_projection_epilogue_rva;
                    return EXCEPTION_CONTINUE_EXECUTION;
                }

                constexpr uintptr_t subnautica2_vsm_pass_builder_start_rva = 0x2FD1340;
                constexpr uintptr_t subnautica2_vsm_pass_builder_end_rva = 0x2FD1C45;
                constexpr uintptr_t subnautica2_vsm_pass_builder_epilogue_rva = 0x2FD1C29;

                if (exe_base != 0 &&
                    exception_address >= exe_base + subnautica2_vsm_pass_builder_start_rva &&
                    exception_address < exe_base + subnautica2_vsm_pass_builder_end_rva)
                {
                    SPDLOG_WARNING_EVERY_N_SEC(
                        2,
                        "[Subnautica2][NativeStereoFix] Skipping virtual shadow map pass builder after access violation at {:x}",
                        exception_address);
                    exception->ContextRecord->Rip = exe_base + subnautica2_vsm_pass_builder_epilogue_rva;
                    return EXCEPTION_CONTINUE_EXECUTION;
                }
            }

            if (ignored_addresses.contains(exception_address)) {
                return EXCEPTION_CONTINUE_SEARCH;
            }

            ignored_addresses.insert(exception_address);

            if (exception_address == 0) {
                SPDLOG_INFO("[Exception Handler] Exception address is null");
                return EXCEPTION_CONTINUE_SEARCH;
            }

            if (IsBadReadPtr((void*)exception_address, sizeof(void*))) {
                SPDLOG_INFO("[Exception Handler] Bad read pointer at {:x}", exception_address);
                return EXCEPTION_CONTINUE_SEARCH;
            }

            const auto decoded = utility::decode_one((uint8_t*)exception_address);

            if (!decoded) {
                SPDLOG_ERROR("[Exception Handler] Failed to decode instruction at {:x}", exception_address);
                return EXCEPTION_CONTINUE_SEARCH;
            }

            const auto& op2 = decoded->Operands[1];

            if (decoded->OperandsCount != 2 || 
                 op2.Type != ND_OP_MEM      || 
                !op2.Info.Memory.HasBase)
            {
                return EXCEPTION_CONTINUE_SEARCH;
            }

            SPDLOG_INFO("Encountered attempted dereference of null pointer at {:x}", exception_address);

            // Get the start of the previous instruction
            auto previous_instruction = utility::resolve_instruction(exception_address - 1);

            if (!previous_instruction) {
                SPDLOG_ERROR("Could not resolve previous instruction at {:x}", exception_address - 1);
                return EXCEPTION_CONTINUE_SEARCH;
            }

            if (previous_instruction->instrux.Operands[0].Type != ND_OP_REG ||
                previous_instruction->instrux.Operands[0].Info.Register.Reg != op2.Info.Memory.Base)
            {
                const auto can_use_stalker2_backscan = stalker2_is_current_game() && is_ue_5_1_dx12_backend();

                if (!can_use_stalker2_backscan) {
                    SPDLOG_ERROR("Previous instruction does not use the same register as the dereference");
                    return EXCEPTION_CONTINUE_SEARCH;
                }

                std::optional<utility::Resolved> xr_hmd_load{};
                const auto prior_instructions = utility::get_disassembly_behind(exception_address);

                for (auto it = prior_instructions.rbegin(); it != prior_instructions.rend(); ++it) {
                    const auto& candidate = *it;
                    const auto& candidate_ix = candidate.instrux;

                    if ((exception_address - candidate.addr) > 0x40) {
                        break;
                    }

                    if (candidate_ix.OperandsCount < 2 ||
                        candidate_ix.Operands[0].Type != ND_OP_REG ||
                        candidate_ix.Operands[0].Info.Register.Reg != op2.Info.Memory.Base)
                    {
                        continue;
                    }

                    const auto& candidate_op2 = candidate_ix.Operands[1];

                    if (candidate_op2.Type == ND_OP_MEM &&
                        candidate_op2.Info.Memory.HasBase &&
                        candidate_op2.Info.Memory.HasDisp &&
                        candidate_op2.Info.Memory.Disp == potential_hmd_device_offset)
                    {
                        xr_hmd_load = candidate;
                        break;
                    }
                }

                if (!xr_hmd_load) {
                    SPDLOG_ERROR("Previous instruction does not use the same register as the dereference");
                    return EXCEPTION_CONTINUE_SEARCH;
                }

                SPDLOG_INFO(
                    "[Stalker2][UE5.1] Matched non-adjacent XRSystem/HMDDevice load at {:x} for null dereference at {:x}",
                    xr_hmd_load->addr,
                    exception_address);

                previous_instruction = *xr_hmd_load;
            }

            const auto prev_op2 = previous_instruction->instrux.Operands[1];

            if (previous_instruction->instrux.OperandsCount < 2 ||
                prev_op2.Type != ND_OP_MEM ||
                !prev_op2.Info.Memory.HasBase)
            {
                SPDLOG_ERROR("Previous instruction is not a memory dereference");
                return EXCEPTION_CONTINUE_SEARCH;
            }

            if (!prev_op2.Info.Memory.HasDisp) {
                SPDLOG_ERROR("Previous instruction does not have a displacement");
                return EXCEPTION_CONTINUE_SEARCH;
            }

            if (prev_op2.Info.Memory.Disp != potential_hmd_device_offset) {
                SPDLOG_ERROR("Previous instruction is not the XRSystem or HMDDevice dereference");
                return EXCEPTION_CONTINUE_SEARCH;
            }

            SPDLOG_INFO("Found the dereference of the XRSystem or HMDDevice at {:x}", previous_instruction->addr);

            // Patch the initial instruction that caused the crash
            SPDLOG_INFO("Creating first patch...");

            std::vector<int16_t> first_patch{};

            for (auto i = 0; i < decoded->Length; ++i) {
                first_patch.push_back(0x90);
            }

            xrsystem_patches.push_back(Patch::create(exception_address, first_patch));

            const auto next_instruction_addr = exception_address + decoded->Length;
            const auto next_instruction = utility::decode_one((uint8_t*)next_instruction_addr);

            if (!next_instruction) {
                SPDLOG_ERROR("Could not decode next instruction at {:x}", exception_address + decoded->Length);
                return EXCEPTION_CONTINUE_EXECUTION;
            }

            if (!std::string_view{next_instruction->Mnemonic}.starts_with("CALL")) {
                SPDLOG_ERROR("Next instruction is not a call, continuing anyways since we patched the dereference");
                return EXCEPTION_CONTINUE_EXECUTION;
            }

            // Patch the next instruction if it's a call
            SPDLOG_INFO("Creating second patch...");

            std::vector<int16_t> second_patch{};

            for (auto i = 0; i < next_instruction->Length; ++i) {
                second_patch.push_back(0x90);
            }

            xrsystem_patches.push_back(Patch::create(next_instruction_addr, second_patch));

            SPDLOG_INFO("Finished creating patches, continuing execution. Hopefully we don't crash...");
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        return EXCEPTION_CONTINUE_SEARCH;
    });

    // The TWeakPtr version is for >= 4.11 UE versions
    TWeakPtr<FSceneViewExtensions>& view_extensions_tweakptr = 
        *(TWeakPtr<FSceneViewExtensions>*)potential_view_extensions;

    // This means it's an old version of UE
    // so the view extensions are a TArray and not a TWeakPtr<TArray>
    if (!m_rendertarget_manager_embedded_in_stereo_device) {
        if (view_extensions_tweakptr.reference == nullptr) {
            view_extensions_tweakptr.allocate_naive(m_use_fmalloc_scene_view_extensions->value());
        }
    }

    FSceneViewExtensions& view_extensions = m_rendertarget_manager_embedded_in_stereo_device ?  
                                            *(FSceneViewExtensions*)potential_view_extensions : *view_extensions_tweakptr.reference;

    SPDLOG_INFO("Current ext ptr: {:x}", (uintptr_t)view_extensions.extensions.data);
    SPDLOG_INFO("Current ext count: {}", view_extensions.extensions.count);
    SPDLOG_INFO("Current ext capacity: {}", view_extensions.extensions.capacity);

    // Verifications on the current memory of the FSceneViewExtensions, because pre-4.10 (?) the view extensions array did not actually exist
    if (m_rendertarget_manager_embedded_in_stereo_device) {
        SPDLOG_INFO("Performing verifications on the current memory of the FSceneViewExtensions...");

        const auto& current_view_extensions_ptr_value = view_extensions.extensions;

        // Check if current value is non zero and points to invalid memory
        if (current_view_extensions_ptr_value.data != nullptr && IsBadReadPtr((void*)current_view_extensions_ptr_value.data, sizeof(void*))) {
            SPDLOG_ERROR("Usual view extensions pointer is non-zero but points to invalid memory! Cannot set up view extensions!");
            SPDLOG_ERROR("This may mean that the UE version is very old and this method of hooking the view extensions is not supported.");
            return false;
        }

        // Check if count is greater than capacity, which is not possible
        if ((uint32_t)current_view_extensions_ptr_value.count > (uint32_t)current_view_extensions_ptr_value.capacity) {
            SPDLOG_ERROR("Usual view extensions count is greater than capacity! Cannot set up view extensions!");
            SPDLOG_ERROR("This may mean that the UE version is very old and this method of hooking the view extensions is not supported.");
            return false;
        }

        // Check if count or capacity is negative, which is not possible
        if ((int32_t)current_view_extensions_ptr_value.count < 0 || (int32_t)current_view_extensions_ptr_value.capacity < 0) {
            SPDLOG_ERROR("Usual view extensions count or capacity is negative! Cannot set up view extensions!");
            SPDLOG_ERROR("This may mean that the UE version is very old and this method of hooking the view extensions is not supported.");
            return false;
        }
        
        // Check if the memory at count treated as a pointer points to valid memory, which is not possible
        const auto count_as_ptr = *(void**)&current_view_extensions_ptr_value.count;
        if (count_as_ptr != nullptr && !IsBadReadPtr(count_as_ptr, sizeof(void*))) {
            SPDLOG_ERROR("Usual view extensions count is actually a pointer to valid memory! Cannot set up view extensions!");
            SPDLOG_ERROR("This may mean that the UE version is very old and this method of hooking the view extensions is not supported.");
            return false;
        }

        // Check if the data pointer is null but capacity is greater than 0, which is not possible
        if (current_view_extensions_ptr_value.data == nullptr && current_view_extensions_ptr_value.capacity > 0) {
            SPDLOG_INFO("Usual view extensions data pointer is null but capacity is greater than 0! Cannot set up view extensions!");
            SPDLOG_INFO("This may mean that the UE version is very old and this method of hooking the view extensions is not supported.");
        }

        // Check if the data pointer is non-null but the capacity is 0, which is not possible
        if (current_view_extensions_ptr_value.data != nullptr && current_view_extensions_ptr_value.capacity == 0) {
            SPDLOG_ERROR("Usual view extensions data pointer is non-null but capacity is 0! Cannot set up view extensions!");
            SPDLOG_ERROR("This may mean that the UE version is very old and this method of hooking the view extensions is not supported.");
            return false;
        }

        // Check if any current entries in the array within the count are invalid, which is not possible
        if (current_view_extensions_ptr_value.data != nullptr) {
            for (auto i = 0; i < current_view_extensions_ptr_value.count; ++i) {
                const auto ext = current_view_extensions_ptr_value.data[i].reference;

                if (IsBadReadPtr((void*)ext, sizeof(void*))) {
                    SPDLOG_ERROR("Usual view extensions array contains an invalid entry! Cannot set up view extensions!");
                    SPDLOG_ERROR("This may mean that the UE version is very old and this method of hooking the view extensions is not supported.");
                    return false;
                }

                const auto ext_vtable = *(void**)ext;

                if (IsBadReadPtr((void*)ext_vtable, sizeof(void*))) {
                    SPDLOG_ERROR("Usual view extensions array contains an entry with an invalid vtable! Cannot set up view extensions!");
                    SPDLOG_ERROR("This may mean that the UE version is very old and this method of hooking the view extensions is not supported.");
                    return false;
                }
            }
        }
    }

    // Allocate a completely new array if the current one is null or empty
    if (view_extensions.extensions.data == nullptr || view_extensions.extensions.data[0].reference == nullptr || view_extensions.extensions.count == 0) {
        SPDLOG_INFO("Allocating new view extensions array...");

        auto& exts = view_extensions.extensions;

        // Allocate a bunch more than necessary to prevent crashes when the engine tries to add new entries
        const auto new_capacity = 32;

        if (!m_use_fmalloc_scene_view_extensions->value()) {
            exts.data = new TWeakPtr<ISceneViewExtension>[new_capacity]{};
        } else {
            if (auto fmalloc = sdk::FMalloc::get(); fmalloc != nullptr) {
                exts.data = (TWeakPtr<ISceneViewExtension>*)fmalloc->malloc(new_capacity * sizeof(TWeakPtr<ISceneViewExtension>));
                for (auto i = 0; i < new_capacity; ++i) {
                    new (&exts.data[i]) TWeakPtr<ISceneViewExtension>();
                }
            } else {
                SPDLOG_ERROR("Failed to get FMalloc! Cannot allocate new view extensions array! Falling back to default allocation method...");
                exts.data = new TWeakPtr<ISceneViewExtension>[new_capacity]{};
            }
        }

        exts.count = 0;
        exts.capacity = new_capacity;

        ZeroMemory(exts.data, sizeof(TWeakPtr<ISceneViewExtension>) * new_capacity);
        exts.data[exts.count++].allocate_naive(m_use_fmalloc_scene_view_extensions->value());
    } else if (view_extensions.extensions.data != nullptr && view_extensions.extensions.count <= view_extensions.extensions.capacity) {
        auto& exts = view_extensions.extensions;

        // TODO: Use FMemory::Realloc (or whatever its called) instead of new/delete cuz game crashes when reallocating/closing the game
        if (exts.count == exts.capacity) {
            SPDLOG_INFO("Extending view extensions array...");

            const auto new_capacity = exts.capacity * 4;
            const auto old_capacity = exts.capacity;

            TWeakPtr<ISceneViewExtension>* new_exts = nullptr;

            if (!m_use_fmalloc_scene_view_extensions->value()) {
                new_exts = new TWeakPtr<ISceneViewExtension>[new_capacity];
            } else {
                if (auto fmalloc = sdk::FMalloc::get(); fmalloc != nullptr) {
                    new_exts = (TWeakPtr<ISceneViewExtension>*)fmalloc->malloc(new_capacity * sizeof(TWeakPtr<ISceneViewExtension>));
                    for (auto i = 0; i < new_capacity; ++i) {
                        new (&new_exts[i]) TWeakPtr<ISceneViewExtension>();
                    }
                } else {
                    SPDLOG_ERROR("Failed to get FMalloc! Cannot allocate new view extensions array! Falling back to default allocation method...");
                    new_exts = new TWeakPtr<ISceneViewExtension>[new_capacity];
                }
            }

            ZeroMemory(new_exts, sizeof(TWeakPtr<ISceneViewExtension>) * new_capacity);
            memcpy(new_exts, exts.data, sizeof(TWeakPtr<ISceneViewExtension>) * old_capacity);

            // dont delete it cuz its owned by the games allocator... for now
            //delete[] exts.data;

            exts.data = new_exts;
            exts.capacity = new_capacity;
        } else {
            SPDLOG_INFO("Allocating new view extension entry onto existing array...");
        }

        exts.data[exts.count++].allocate_naive(m_use_fmalloc_scene_view_extensions->value());
    } else {
        SPDLOG_INFO("None of the previous conditions were met, so we're not allocating a new view extensions array");
    }

    if (view_extensions.extensions.count > 0 && view_extensions.extensions.data != nullptr) {
        // Replace the vtable of the first entry
        auto& entry = view_extensions.extensions.data[view_extensions.extensions.count-1];

        if (entry.reference == nullptr) {
            SPDLOG_ERROR("Failed to get first view extension entry!");
            return false;
        }

        auto& vtable = *(uintptr_t**)entry.reference;
        const auto original_vtable = (void**)vtable;

        g_hook->m_analyze_view_extensions_start_time = std::chrono::high_resolution_clock::now();
        g_hook->m_analyzing_view_extensions = true;

        if (SceneViewExtensionAnalyzer::try_apply_cached_discovery(original_vtable)) {
            vtable = g_view_extension_vtable.data();
            g_hook->m_analyzing_view_extensions = false;
            g_hook->m_has_view_extensions_installed = true;
            m_has_view_extension_hook = true;
            return true;
        }

        if (!m_rendertarget_manager_embedded_in_stereo_device) {
            SceneViewExtensionAnalyzer::FillVtable<g_view_extension_vtable.size()-1>::fill(g_view_extension_vtable);
        } else {
            // Skip straight to stage 2.
            SPDLOG_INFO("Skipping view extension stage 1...");
            SceneViewExtensionAnalyzer::FillVtable<g_view_extension_vtable.size()-1>::fill2(g_view_extension_vtable);
        }

        // Will get called when the view extensions are finally hooked.
        RenderThreadWorker::get().enqueue([this]() {
            this->m_analyzing_view_extensions = false;
            this->m_has_view_extensions_installed = true;
        });

        // overwrite the vtable
        vtable = g_view_extension_vtable.data();
        m_has_view_extension_hook = true;
    } else {
        // TODO: Allocate a new one.
        m_has_view_extension_hook = false;

        SPDLOG_INFO("Failed to set up view extensions! (not yet implemented to allocate a new one)");
    }

    return true;
} catch(...) {
    SPDLOG_ERROR("Unknown exception while setting up view extensions!");
    return false;
}

std::optional<uintptr_t> FFakeStereoRenderingHook::locate_fake_stereo_rendering_constructor() {
    static std::optional<uintptr_t> cached_result{};

    if (cached_result) {
        return cached_result;
    }

    const auto engine_dll = sdk::get_ue_module(L"Engine");

    auto fake_stereo_rendering_constructor = utility::find_function_from_string_ref(engine_dll, L"r.StereoEmulationHeight");

    if (!fake_stereo_rendering_constructor) {
        fake_stereo_rendering_constructor = utility::find_function_from_string_ref(engine_dll, L"r.StereoEmulationFOV");

        if (!fake_stereo_rendering_constructor) {
            SPDLOG_ERROR("Failed to find FFakeStereoRendering constructor");
            return std::nullopt;
        }
    }

    if (!fake_stereo_rendering_constructor) {
        SPDLOG_ERROR("Failed to find FFakeStereoRendering constructor");
        return std::nullopt;
    }

    SPDLOG_INFO("FFakeStereoRendering constructor: {:x}", (uintptr_t)*fake_stereo_rendering_constructor);
    cached_result = *fake_stereo_rendering_constructor;

    return *fake_stereo_rendering_constructor;
}

std::optional<uintptr_t> FFakeStereoRenderingHook::locate_fake_stereo_rendering_vtable() {
    static std::optional<uintptr_t> cached_result{};

    if (cached_result) {
        return cached_result;
    }

    if (g_hook->m_manually_constructed) {
        cached_result = *(uintptr_t*)((uintptr_t)sdk::UGameEngine::get() + s_stereo_rendering_device_offset);
        return cached_result;
    }

    const auto fake_stereo_rendering_constructor = locate_fake_stereo_rendering_constructor();

    if (!fake_stereo_rendering_constructor) {
        // If this happened, then that's bad news, the UE version is probably extremely old
        // so we have to use this fallback method.
        SPDLOG_INFO("Failed to locate FFakeStereoRendering constructor, using fallback method");
        const auto initialize_hmd_device = sdk::UEngine::get_initialize_hmd_device_address();

        if (!initialize_hmd_device) {
            SPDLOG_ERROR("Failed to find FFakeStereoRendering VTable via fallback method");
            return std::nullopt;
        }

        // To be seen if this needs to be adjusted. At first glance it doesn't look very reliable.
        // maybe perform emulation or something in the future?
        const auto instruction = utility::scan_disasm(*initialize_hmd_device, 100, "48 8D 05 ? ? ? ?");

        if (!instruction) {
            SPDLOG_ERROR("Failed to find FFakeStereoRendering VTable via fallback method (2)");
            return std::nullopt;
        }

        const auto result = utility::calculate_absolute(*instruction + 3);

        if (!result) {
            SPDLOG_ERROR("Failed to find FFakeStereoRendering VTable via fallback method (3)");
            return std::nullopt;
        }

        SPDLOG_INFO("FFakeStereoRendering VTable: {:x}", (uintptr_t)result);
        cached_result = result;

        return result;
    }

    const auto vtable_ref = utility::scan(*fake_stereo_rendering_constructor, 100, "48 8D 05 ? ? ? ?");

    if (!vtable_ref) {
        SPDLOG_WARN("Failed to find FFakeStereoRendering VTable Reference through legacy pattern, trying constructor RIP-reference scan");

        if (const auto vtable_from_constructor = locate_vtable_from_constructor_rip_references(*fake_stereo_rendering_constructor)) {
            SPDLOG_INFO("FFakeStereoRendering VTable: {:x}", *vtable_from_constructor);
            cached_result = vtable_from_constructor;
            return vtable_from_constructor;
        }

        SPDLOG_ERROR("Failed to find FFakeStereoRendering VTable Reference");
        return std::nullopt;
    }

    const auto vtable = utility::calculate_absolute(*vtable_ref + 3);

    if (!vtable) {
        SPDLOG_ERROR("Failed to find FFakeStereoRendering VTable");
        return std::nullopt;
    }

    SPDLOG_INFO("FFakeStereoRendering VTable: {:x}", (uintptr_t)vtable);
    cached_result = vtable;

    return vtable;
}

std::optional<uintptr_t> FFakeStereoRenderingHook::locate_active_stereo_rendering_device() {
    auto engine = (uintptr_t)sdk::UEngine::get();

    if (engine == 0) {
        SPDLOG_ERROR("GEngine does not appear to be instantiated, cannot verify stereo rendering device is setup.");
        return std::nullopt;
    }

    SPDLOG_INFO("Checking engine pointers for StereoRenderingDevice...");
    auto fake_stereo_device_vtable = locate_fake_stereo_rendering_vtable();

    if (!fake_stereo_device_vtable) {
        SPDLOG_ERROR("Failed to locate fake stereo rendering device vtable, cannot verify stereo rendering device is setup.");
        return std::nullopt;
    }

    if (s_stereo_rendering_device_offset != 0) {
        const auto result = *(uintptr_t*)(engine + s_stereo_rendering_device_offset);

        if (result == 0) {
            return std::nullopt;
        }

        return result;
    }

    for (auto i = 0; i < 0x2000; i += sizeof(void*)) {
        const auto addr_of_ptr = engine + i;

        if (IsBadReadPtr((void*)addr_of_ptr, sizeof(void*))) {
            SPDLOG_INFO("Reached end of engine pointers at offset {:x}", i);
            break;
        }

        const auto ptr = *(uintptr_t*)addr_of_ptr;

        if (ptr == 0 || IsBadReadPtr((void*)ptr, sizeof(void*))) {
            continue;
        }

        auto potential_vtable = *(uintptr_t*)ptr;

        if (potential_vtable == *fake_stereo_device_vtable) {
            SPDLOG_INFO("Found fake stereo rendering device at offset {:x} -> {:x}", i, ptr);
            s_stereo_rendering_device_offset = i;
            return ptr;
        }
    }

    SPDLOG_ERROR("Failed to find stereo rendering device");
    return std::nullopt;
}

std::optional<uint32_t> FFakeStereoRenderingHook::get_stereo_view_offset_index(uintptr_t vtable) {
    for (auto i = 0; i < 30; ++i) {
        auto func = ((uintptr_t*)vtable)[i];

        if (func == 0 || IsBadReadPtr((void*)func, sizeof(void*))) {
            continue;
        }

        // Resolve jmps if needed.
        while (*(uint8_t*)func == 0xE9) {
            SPDLOG_INFO("VFunc at index {} contains a jmp, resolving...", i);
            func = utility::calculate_absolute(func + 1);
        }

        bool found = false;
        uint32_t xmm_register_usage_count = 0;

        // We do an exhaustive decode (disassemble all possible code paths) that correctly follows the control flow
        // because some games are obfuscated and do huge jumps across gaps of junk code.
        // so we can't just linearly scan forward as the disassembler will fail at some point.
        utility::exhaustive_decode((uint8_t*)func, 50, [&](INSTRUX& ix, uintptr_t ip) -> utility::ExhaustionResult {
            if (found) {
                return utility::ExhaustionResult::BREAK;
            }

            if (ix.BranchInfo.IsBranch && !ix.BranchInfo.IsConditional && std::string_view{ix.Mnemonic}.starts_with("CALL")) {
                return utility::ExhaustionResult::STEP_OVER;
            }

            char txt[ND_MIN_BUF_SIZE]{};
            NdToText(&ix, 0, sizeof(txt), txt);

            if (std::string_view{txt}.find("xmm") != std::string_view::npos && ++xmm_register_usage_count >= 10) {
                found = true;
                return utility::ExhaustionResult::BREAK;
            }

            return utility::ExhaustionResult::CONTINUE;
        });

        if (found) {
            SPDLOG_INFO("Found Stereo View Offset Index: {}", i);
            return i;
        }
    }

    return std::nullopt;
}

// DISCLAIMER: I've only seen this in one game so far...
// So, there's some kind of compiler optimization for inlined virtuals
// that checks whether the vtable pointer matches the base FFakeStereoRendering class.
// if it matches, it just calls an inlined version of the function.
// otherwise it actually calls the function within the vtable.
bool FFakeStereoRenderingHook::patch_vtable_checks() {
    SPDLOG_INFO("Attempting to patch inlined vtable checks...");

    const auto fake_stereo_rendering_constructor = locate_fake_stereo_rendering_constructor();
    const auto fake_stereo_rendering_vtable = locate_fake_stereo_rendering_vtable();

    if (!fake_stereo_rendering_constructor || !fake_stereo_rendering_vtable) {
        SPDLOG_ERROR("Cannot patch vtables, constructor or vtable not found!");
        return false;
    }

    const auto vtable_module_within = utility::get_module_within(*fake_stereo_rendering_vtable);
    const auto module_size = utility::get_module_size(*vtable_module_within);
    const auto module_end = (uintptr_t)*vtable_module_within + *module_size;

    SPDLOG_INFO("{:x} {:x} {:x}", *fake_stereo_rendering_vtable, (uintptr_t)*vtable_module_within, *module_size);

    for (auto ref = utility::scan_displacement_reference(*vtable_module_within, *fake_stereo_rendering_vtable); 
        ref.has_value();
        ref = utility::scan_displacement_reference((uintptr_t)*ref + 4, (module_end - *ref) - sizeof(void*), *fake_stereo_rendering_vtable)) 
    {
        const auto distance_from_constructor = *ref - *fake_stereo_rendering_constructor;

        // We don't want to mess with the one within the constructor.
        if (distance_from_constructor < 0x100) {
            SPDLOG_INFO("Skipping vtable reference within constructor");
            continue;
        }

        // Change the bytes to be some random number
        // this causes the vtable check to fail and will call the function within the vtable.
        DWORD old{};
        VirtualProtect((void*)*ref, 4, PAGE_EXECUTE_READWRITE, &old);
        *(uint32_t*)*ref = 0x12345678;
        VirtualProtect((void*)*ref, 4, old, &old);
        SPDLOG_INFO("Patched vtable check at {:x}", (uintptr_t)*ref);
    }

    SPDLOG_INFO("Finished patching inlined vtable checks.");
    return true;
}

bool FFakeStereoRenderingHook::attempt_runtime_inject_stereo() {
    // This attempts to create a new StereoRenderingDevice in the GEngine
    // if it doesn't already exist via using -emulatestereo.
    auto engine = sdk::UEngine::get();

    if (engine == nullptr) {
        SPDLOG_ERROR("Failed to locate GEngine, cannot inject stereo rendering device at runtime.");
        return false;
    }

    static auto enable_stereo_emulation_cvar = sdk::vr::get_enable_stereo_emulation_cvar();

    if (!locate_active_stereo_rendering_device()) {
        SPDLOG_INFO("Calling InitializeHMDDevice...");

        //utility::ThreadSuspender _{};

        engine->initialize_hmd_device();

        SPDLOG_INFO("Called InitializeHMDDevice.");

        if (!locate_active_stereo_rendering_device()) {
            SPDLOG_INFO("Previous call to InitializeHMDDevice did not setup the stereo rendering device, attempting to call again...");

            auto patch_emulate_stereo_flag = []() {
                //SPDLOG_ERROR("Failed to locate r.EnableStereoEmulation cvar, next call may fail.");
                SPDLOG_INFO("r.EnableStereoEmulation cvar not found, using fallback method of forcing -emulatestereo flag.");
                
                const auto emulate_stereo_string_ref = sdk::UGameEngine::get_emulatestereo_string_ref_address();

                if (emulate_stereo_string_ref) {
                    const auto resolved = utility::resolve_instruction(*emulate_stereo_string_ref);

                    if (resolved) {
                        // Scan forward for a call instruction, this call checks the command line for "emulatestereo".
                        const auto call = utility::scan_disasm(resolved->addr, 20, "E8 ? ? ? ?");

                        if (call) {
                            // Patch the instruction to mov al, 1
                            SPDLOG_INFO("Patching instruction at {:x} to mov al, 1", (uintptr_t)*call);
                            static auto patch = Patch::create(*call, { 0xB0, 0x01, 0x90, 0x90, 0x90 });
                        }
                    }
                }
            };

            // We don't call this before because the cvar will not be set up
            // until it's referenced once. after we set this we need to call the function again.
            if (enable_stereo_emulation_cvar) {
                try {
                    enable_stereo_emulation_cvar->set<int>(1);
                } catch(...) {
                    SPDLOG_ERROR("Access violation occurred when writing to r.EnableStereoEmulation, the address may be incorrect!");
                    patch_emulate_stereo_flag();
                }
            } else {
                //SPDLOG_ERROR("Failed to locate r.EnableStereoEmulation cvar, next call may fail.");
                patch_emulate_stereo_flag();
            }

            SPDLOG_INFO("Calling InitializeHMDDevice... AGAIN");

            engine->initialize_hmd_device();

            SPDLOG_INFO("Called InitializeHMDDevice again.");
        }

        if (locate_active_stereo_rendering_device()) {
            SPDLOG_INFO("Stereo rendering device setup successfully.");
        } else {
            SPDLOG_ERROR("Failed to setup stereo rendering device.");
            return false;
        }
    } else {
        SPDLOG_INFO("Not necessary to call InitializeHMDDevice, stereo rendering device is already setup.");
        m_fixed_localplayer_view_count = true; // Everything was set up beforehand, we don't need to do anything, so just set it to true.
    }

    return true;
}

bool FFakeStereoRenderingHook::is_stereo_enabled(FFakeStereoRendering* stereo) {
#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
    SPDLOG_INFO("is stereo enabled called!");
#else
    SPDLOG_INFO_ONCE("is stereo enabled called!");
#endif

    // wait!!!
    if (!g_framework->is_game_data_intialized()) {
        return false;
    }

    if (g_hook->m_sceneview_data.inside_post_init_properties) {
        g_hook->set_should_recreate_textures(true);
        return true;
    }
    
    /*if (g_hook->m_analyzing_view_extensions) {
        const auto now = std::chrono::high_resolution_clock::now();

        if (now - g_hook->m_analyze_view_extensions_start_time > std::chrono::seconds(15)) {
            SPDLOG_INFO("Timed out waiting for view extensions to be analyzed.");
            g_hook->m_analyzing_view_extensions = false;
        }

        return false;
    }*/

    static std::atomic<bool> last_state = false;
    auto hook = g_hook;

    // The best way to enable stereo rendering without causing crashes
    // while also allowing the desktop view to initially display
    // if the HMD is not on at the start. It only allows
    // stereo to be enabled if it starts from the first call to IsStereoEnabled inside UGameViewportClient::Draw.
    if (hook->m_has_game_viewport_client_draw_hook) {
        if (GameThreadWorker::get().is_same_thread()) {
            if (hook->m_in_viewport_client_draw && !hook->m_was_in_viewport_client_draw) {
                const auto is_hmd_active = VR::get()->is_hmd_active();

                if (!last_state && is_hmd_active) {
                    VR::get()->wait_for_present();
                    hook->set_should_recreate_textures(true);
                }

                last_state = is_hmd_active;
            }

            hook->m_was_in_viewport_client_draw = hook->m_in_viewport_client_draw;
        }

        return last_state;
    }

    static uint32_t count = 0;

    // Forcefully return true the first few times to let stuff initialize.
    if (count < 50) {
        if (count == 0) {
            hook->set_should_recreate_textures(true);
        }

        ++count;
        last_state = true;
        return true;
    }

    const auto result = !VR::get()->get_runtime()->got_first_sync || VR::get()->is_hmd_active();

    if (result && !last_state) {
        hook->set_should_recreate_textures(true);
    }

    last_state = result;

    return result;
}

void FFakeStereoRenderingHook::adjust_view_rect(FFakeStereoRendering* stereo, int32_t index, int* x, int* y, uint32_t* w, uint32_t* h) {
#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
    SPDLOG_INFO("adjust view rect called! {}", index);
    SPDLOG_INFO(" x: {}, y: {}, w: {}, h: {}", *x, *y, *w, *h);
#else
    SPDLOG_INFO_ONCE("adjust view rect called! {}", index);
    SPDLOG_INFO_ONCE(" x: {}, y: {}, w: {}, h: {}", *x, *y, *w, *h);
#endif

    if (!g_framework->is_game_data_intialized()) {
        return;
    }

    static bool index_starts_from_one = true;

    if (index == 2) {
        index_starts_from_one = true;
    } else if (index == 0) {
        index_starts_from_one = false;
    }

    // The purpose of this is to prevent the game from crashing in IDirect3D12CommandList::Close
    // Because the game will try to copy a texture region that is out of bounds.
    if (g_hook->m_skip_next_adjust_view_rect) {
        *x = 0;
        *y = 0;
        *w = std::min<uint32_t>(VR::get()->get_hmd_width(), *w);
        *h = std::min<uint32_t>(VR::get()->get_hmd_height(), *h);
        g_hook->m_skip_next_adjust_view_rect = false;
        g_hook->m_skip_next_adjust_view_rect_count = 1;
        return;
    }

    if (g_hook->m_skip_next_adjust_view_rect_count > 0) {
        *x = 0;
        *y = 0;
        *w = std::min<uint32_t>(VR::get()->get_hmd_width(), *w);
        *h = std::min<uint32_t>(VR::get()->get_hmd_height(), *h);
        --g_hook->m_skip_next_adjust_view_rect_count;
        return;
    }

    if (VR::get()->is_stereo_emulation_enabled()) {
        *w *= 2;
    } else {
        *w = VR::get()->get_hmd_width() * 2;
        *h = VR::get()->get_hmd_height();
    }


    *w = *w / 2;

    const auto true_index = index_starts_from_one ? ((index + 1) % 2) : (index % 2);

    auto& vr = VR::get();

    if (!vr->is_native_stereo_fix_enabled() || !vr->is_native_stereo_fix_same_pass_enabled()) {
        *x += *w * true_index;
    }

    if (index >= 0 && index < 4) {
        static bool logged_index[4]{};

        if (sn2_native_stereo_debug_log_max() != 0 && !logged_index[index]) {
            logged_index[index] = true;
            SPDLOG_INFO("[NativeStereoDebug] AdjustViewRect index={} true_index={} rect={} {} {} {} native_fix={} same_pass={}",
                index, true_index, *x, *y, *w, *h, vr->is_native_stereo_fix_enabled(), vr->is_native_stereo_fix_same_pass_enabled());
        }
    }
}

void FFakeStereoRenderingHook::set_final_view_rect(FFakeStereoRendering* stereo, sdk::FRHICommandListBase* cmd_list, int32_t view_index, const FIntRect* final_view_rect) {
    auto& vr = VR::get();
    FIntRect corrected_view_rect{};
    auto rect_to_submit = final_view_rect;

    if (final_view_rect != nullptr &&
        vr->is_native_stereo_fix_enabled() &&
        !vr->is_native_stereo_fix_same_pass_enabled() &&
        view_index == 1 &&
        final_view_rect->bounds[0] == 0)
    {
        corrected_view_rect = *final_view_rect;
        const auto width = corrected_view_rect.bounds[2] - corrected_view_rect.bounds[0];

        if (width > 0) {
            corrected_view_rect.bounds[0] += width;
            corrected_view_rect.bounds[2] += width;
            rect_to_submit = &corrected_view_rect;

            SPDLOG_INFO_ONCE("[NativeStereoDebug] Corrected right-eye SetFinalViewRect to {} {} {} {}",
                corrected_view_rect.bounds[0], corrected_view_rect.bounds[1],
                corrected_view_rect.bounds[2], corrected_view_rect.bounds[3]);
        }
    }

#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
    SPDLOG_INFO("set final view rect called! {}", view_index);
#else
    SPDLOG_INFO_ONCE("set final view rect called! {}", view_index);
#endif

    if (rect_to_submit != nullptr && vr->is_native_stereo_fix_enabled()) {
        static uint32_t log_count = 0;

        if (log_count < sn2_native_stereo_debug_log_max()) {
            SPDLOG_INFO("[NativeStereoDebug] SetFinalViewRect view_index={} rect={} {} {} {} native_fix={} same_pass={}",
                view_index,
                rect_to_submit->bounds[0],
                rect_to_submit->bounds[1],
                rect_to_submit->bounds[2],
                rect_to_submit->bounds[3],
                vr->is_native_stereo_fix_enabled(),
                vr->is_native_stereo_fix_same_pass_enabled());
            ++log_count;
        }
    }

    if (g_hook->m_set_final_view_rect_hook) {
        g_hook->m_set_final_view_rect_hook.call<void>(stereo, cmd_list, view_index, rect_to_submit);
    }
}

__forceinline void FFakeStereoRenderingHook::calculate_stereo_view_offset(
    FFakeStereoRendering* stereo, const int32_t view_index, Rotator<float>* view_rotation, 
    const float world_to_meters, Vector3f* view_location)
{
#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
    SPDLOG_INFO("calculate stereo view offset called! {}", view_index);
#else
    SPDLOG_INFO_ONCE("calculate stereo view offset called! {}", view_index);
#endif

    if (!g_framework->is_game_data_intialized()) {
        return;
    }

    auto vr = VR::get();
    //std::scoped_lock _{vr->get_vr_mutex()};

    const auto subnautica2_native_stereo_explicit_view_indices =
        subnautica2_is_current_game() &&
        vr->is_native_stereo_fix_enabled() &&
        !vr->is_native_stereo_fix_same_pass_enabled();
    const auto subnautica2_synced_sequential_explicit_eye =
        subnautica2_is_current_game() &&
        vr->is_using_synchronized_afr();
    const auto subnautica2_explicit_view_zero_is_eye =
        subnautica2_native_stereo_explicit_view_indices ||
        subnautica2_synced_sequential_explicit_eye;

    static bool index_starts_from_one = true;
    static bool index_was_ever_two = false;
    static bool index_was_ever_negative = false;

    if (view_index == -1) {
        index_was_ever_negative = true;
        SPDLOG_INFO_ONCE("calculate stereo view offset called with view index -1 (INDEX_NONE), ignoring.");
        return;
    }

    // This is eSSP_FULL, we don't care. It will cause the view to become monoscopic if we do anything.
    if (!subnautica2_explicit_view_zero_is_eye && index_was_ever_two && view_index == 0) {
        SPDLOG_INFO_ONCE("calculate stereo view offset called with view index 0 after 2, ignoring.");
        return;
    }

    vr->set_world_to_meters(world_to_meters);

    if (!subnautica2_explicit_view_zero_is_eye && view_index == 2) {
        index_starts_from_one = true;
        index_was_ever_two = true;
    } else if (!subnautica2_explicit_view_zero_is_eye && view_index == 0 && !index_was_ever_two) {
        index_starts_from_one = false;
    }

    const auto is_full_pass =
        !subnautica2_explicit_view_zero_is_eye &&
        view_index == 0 &&
        !index_was_ever_two &&
        !index_was_ever_negative;

    auto true_index = subnautica2_native_stereo_explicit_view_indices
        ? (view_index <= 0 ? 0 : 1)
        : (index_starts_from_one ? ((view_index + 1) % 2) : (view_index % 2));
    if (subnautica2_synced_sequential_explicit_eye) {
        true_index = g_frame_count % 2;
    }
    const auto has_double_precision = g_hook->m_has_double_precision;
    const auto rot_d = (Rotator<double>*)view_rotation;

    const auto log_native_stereo_view_offset_sample = [&](std::string_view phase) {
        if (!subnautica2_explicit_view_zero_is_eye) {
            return;
        }

        static uint32_t log_count = 0;
        if (log_count >= sn2_native_stereo_debug_log_max()) {
            return;
        }

        const auto hmd_rotation = glm::normalize(glm::quat{vr->get_rotation(0)});
        const auto hmd_position = vr->get_position(0);

        if (has_double_precision) {
            const auto view_d = (Vector3d*)view_location;
            SPDLOG_INFO("[Subnautica2][NativeStereoFix] CalcStereoOffset {} view_index={} true_index={} full={} rot={:.3f},{:.3f},{:.3f} loc={:.3f},{:.3f},{:.3f} hmd_pos={:.3f},{:.3f},{:.3f} hmd_rot={:.4f},{:.4f},{:.4f},{:.4f}",
                phase, view_index, true_index, is_full_pass,
                rot_d->pitch, rot_d->yaw, rot_d->roll,
                view_d->x, view_d->y, view_d->z,
                hmd_position.x, hmd_position.y, hmd_position.z,
                hmd_rotation.x, hmd_rotation.y, hmd_rotation.z, hmd_rotation.w);
        } else {
            SPDLOG_INFO("[Subnautica2][NativeStereoFix] CalcStereoOffset {} view_index={} true_index={} full={} rot={:.3f},{:.3f},{:.3f} loc={:.3f},{:.3f},{:.3f} hmd_pos={:.3f},{:.3f},{:.3f} hmd_rot={:.4f},{:.4f},{:.4f},{:.4f}",
                phase, view_index, true_index, is_full_pass,
                view_rotation->pitch, view_rotation->yaw, view_rotation->roll,
                view_location->x, view_location->y, view_location->z,
                hmd_position.x, hmd_position.y, hmd_position.z,
                hmd_rotation.x, hmd_rotation.y, hmd_rotation.z, hmd_rotation.w);
        }

        ++log_count;
    };

    log_native_stereo_view_offset_sample("pre");

    if (vr->is_using_afr() && !is_full_pass) {
        true_index = g_frame_count % 2;

        if (!vr->is_using_synchronized_afr()) {
            if (g_hook->m_has_double_precision) {
                if (true_index == 1) {
                    *rot_d = g_hook->m_last_afr_rotation_double;
                } else {
                    g_hook->m_last_afr_rotation_double = *rot_d;
                }
            } else {
                if (true_index == 1) {
                    *view_rotation = g_hook->m_last_afr_rotation;
                } else {
                    g_hook->m_last_afr_rotation = *view_rotation;
                }
            }
        }
    }

    if (true_index == 0 && !is_full_pass) {
        if (has_double_precision) {
            g_hook->m_last_pre_rotation_double = *rot_d;
        } else {
            g_hook->m_last_pre_rotation = *view_rotation;
        }

        //vr->wait_for_present();
        
        if (!g_hook->m_has_view_extension_hook && !g_hook->m_has_game_viewport_client_draw_hook) {
            vr->update_hmd_state();
        }
    }

    /*if (view_index % 2 == 1 && VR::get()->get_synchronize_stage() == VR::SynchronizeStage::EARLY) {
        std::scoped_lock _{ vr->get_runtime()->render_mtx };
        SPDLOG_INFO("SYNCING!!!");
        //vr->get_runtime()->synchronize_frame();
        vr->update_hmd_state();
    }*/

    // if we were unable to hook UGameEngine::Tick, we can run our game thread jobs here instead.
    if (!is_full_pass && !g_hook->m_has_view_extension_hook && g_hook->m_attempted_hook_game_engine_tick && !g_hook->m_hooked_game_engine_tick) {
        GameThreadWorker::get().execute();
    }

    if (vr->is_sceneview_compatibility_enabled() && !g_hook->m_inside_manual_view_offset) {
        return;
    }

    const auto& mods = g_framework->get_mods()->get_mods();

    if (!is_full_pass) {
        for (auto& mod : mods) {
            mod->on_early_calculate_stereo_view_offset(stereo, view_index, view_rotation, world_to_meters, view_location, g_hook->m_has_double_precision);
        }

        for (auto& mod : mods) {
            mod->on_pre_calculate_stereo_view_offset(stereo, view_index, view_rotation, world_to_meters, view_location, g_hook->m_has_double_precision);
        }
    }

    const auto view_d = (Vector3d*)view_location;

    const auto view_mat = !has_double_precision ? 
        glm::yawPitchRoll(
            glm::radians(view_rotation->yaw),
            glm::radians(view_rotation->pitch),
            glm::radians(view_rotation->roll)) : 
        glm::yawPitchRoll(
            glm::radians((float)rot_d->yaw),
            glm::radians((float)rot_d->pitch),
            glm::radians((float)rot_d->roll));

    const auto view_mat_inverse = !has_double_precision ? 
        glm::yawPitchRoll(
            glm::radians(-view_rotation->yaw),
            glm::radians(view_rotation->pitch),
            glm::radians(-view_rotation->roll)) : 
        glm::yawPitchRoll(
            glm::radians(-(float)rot_d->yaw),
            glm::radians((float)rot_d->pitch),
            glm::radians(-(float)rot_d->roll));

    const auto view_quat_inverse = glm::quat {
        view_mat_inverse
    };

    const auto view_quat = glm::quat {
        view_mat
    };

    const auto quat_converter = glm::quat{Matrix4x4f {
        0, 0, -1, 0,
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 0, 1
    }};

    auto vqi_norm = glm::normalize(view_quat_inverse);

    // Decoupled Pitch
    if (vr->is_decoupled_pitch_enabled()) {
        vr->set_pre_flattened_rotation(vqi_norm);
        vqi_norm = utility::math::flatten(vqi_norm);
    }

    const auto camera_forward_offset = vr->get_camera_forward_offset();
    const auto camera_right_offset = vr->get_camera_right_offset();
    const auto camera_up_offset = vr->get_camera_up_offset();
    const auto camera_forward = quat_converter * (vqi_norm * glm::vec3{0, 0, camera_forward_offset});
    const auto camera_right = quat_converter * (vqi_norm * glm::vec3{-camera_right_offset, 0, 0});
    const auto camera_up = quat_converter * (vqi_norm * glm::vec3{0, -camera_up_offset, 0});

    const auto world_scale = world_to_meters * vr->get_world_scale();

    if (has_double_precision) {
        *view_d += camera_forward;
        *view_d += camera_right;
        *view_d += camera_up;
    } else {
        *view_location += camera_forward;
        *view_location += camera_right;
        *view_location += camera_up;
    }

    // Don't apply any headset transformations
    // if we have stereo emulation mode enabled
    // it is only for debugging purposes
    if (!vr->is_stereo_emulation_enabled()) {
        const auto is_2d_screen = vr->is_using_2d_screen();

        const auto rotation_offset = vr->get_rotation_offset();
        const auto current_hmd_rotation = glm::normalize(rotation_offset * glm::quat{vr->get_rotation(0)});
        const auto current_eye_rotation_offset = glm::normalize(glm::quat{vr->get_eye_transform(true_index)});

        const auto new_rotation = glm::normalize(vqi_norm * current_hmd_rotation * current_eye_rotation_offset);
        const auto eye_offset = glm::vec3{vr->get_eye_offset((VRRuntime::Eye)(true_index))};


        const auto standing_delta = vr->get_position(0) - vr->get_standing_origin();
        const auto standing_delta_flat = glm::vec3{standing_delta.x, 0, standing_delta.z};

        const auto pos = glm::vec3{rotation_offset * standing_delta};
        const auto pos_flat = glm::vec3{rotation_offset * standing_delta_flat};

        const auto head_offset = quat_converter * (vqi_norm * (pos * world_scale));
        const auto head_offset_flat = quat_converter * (vqi_norm * (pos_flat * world_scale));
        const auto eye_separation = quat_converter * (glm::normalize(new_rotation) * (eye_offset * world_scale));

        if (!has_double_precision) {
            if (!is_2d_screen) {
                *view_location -= head_offset;
            }

            *view_location -= eye_separation;
        } else {
            if (!is_2d_screen) {
                *view_d -= head_offset;
            }

            *view_d -= eye_separation;
        }

        if (!is_2d_screen) {
            const auto euler = glm::degrees(utility::math::euler_angles_from_steamvr(new_rotation));

            if (!has_double_precision) {
                view_rotation->pitch = euler.x;
                view_rotation->yaw = euler.y;
                view_rotation->roll = euler.z;
            } else {
                rot_d->pitch = euler.x;
                rot_d->yaw = euler.y;
                rot_d->roll = euler.z;
            }
        }

        // Roomscale movement
        // only do it on the right eye pass
        // if we did it on the left, there would be eye desyncs when the right eye is rendered
        if (true_index == 1 && (vr->is_roomscale_enabled() || vr->is_aim_pawn_control_rotation_enabled())) {
            const auto world = sdk::UEngine::get()->get_world();

            if (const auto controller = sdk::UGameplayStatics::get()->get_player_controller(world, 0); controller != nullptr) {
                const auto pawn = controller->get_acknowledged_pawn();

                static bool was_pawn_rotation_enabled = false;

                if (pawn != nullptr && vr->is_aim_pawn_control_rotation_enabled()) {
                    auto camera_component = (sdk::UObject*)pawn->get_camera_component();

                    if (camera_component != nullptr && camera_component->get_class() != nullptr) {
                        static const auto boolprop = (sdk::FBoolProperty*)camera_component->get_class()->find_property(L"bUsePawnControlRotation");

                        if (boolprop != nullptr) {
                            boolprop->set_value_in_object(camera_component, true);
                            was_pawn_rotation_enabled = true;
                        }
                    }
                } else if (pawn != nullptr && was_pawn_rotation_enabled) {
                    auto camera_component = (sdk::UObject*)pawn->get_camera_component();

                    if (camera_component != nullptr && camera_component->get_class() != nullptr) {
                        static const auto boolprop = (sdk::FBoolProperty*)camera_component->get_class()->find_property(L"bUsePawnControlRotation");

                        if (boolprop != nullptr) {
                            boolprop->set_value_in_object(camera_component, false);
                            was_pawn_rotation_enabled = false;
                        }
                    }
                }

                if (pawn != nullptr && vr->is_roomscale_enabled()) {
                    const auto pawn_pos = pawn->get_actor_location();
                    const auto new_pos = pawn_pos - head_offset_flat;

                    // Roomscale sweep option allows the actor to affect the world
                    // like push doors open, and prevent them from clipping through walls
                    pawn->set_actor_location(new_pos, vr->is_roomscale_sweep_enabled(), false);

                    // Recenter the standing origin
                    auto current_standing_origin = vr->get_standing_origin();
                    const auto hmd_pos = vr->get_position(0);
                    // dont touch the Y axis
                    current_standing_origin.x = hmd_pos.x;
                    current_standing_origin.z = hmd_pos.z;
                    vr->set_standing_origin(current_standing_origin);
                }
            }
        }

        // Process snapturn    
        vr->process_snapturn();
    }

    log_native_stereo_view_offset_sample("post");

    if (!is_full_pass) {
        for (auto& mod : mods) {
            mod->on_post_calculate_stereo_view_offset(stereo, view_index, view_rotation, world_to_meters, view_location, g_hook->m_has_double_precision);
        }

        if (true_index == 0) {
            if (has_double_precision) {
                g_hook->m_last_rotation_double = *rot_d;
            } else {
                g_hook->m_last_rotation = *view_rotation;
            }
        }

        // Modify Player Control Rotation
        const auto controller_camera_guard_active = vr->is_controller_camera_conflict_guard_active();
        const auto direct_aim_compatibility_fallback =
            vr->is_hmd_active() &&
            !controller_camera_guard_active &&
            (is_deadzone_ue56_executable() || vr->is_direct_aim_compatibility_enabled()) &&
            (vr->is_headlocked_aim_enabled() ||
                (vr->is_controller_aim_enabled() && vr->is_using_controllers()));

        const auto subnautica2_native_stereo_updates_control_rotation_before_views =
            subnautica2_is_current_game() &&
            vr->is_native_stereo_fix_enabled() &&
            !vr->is_native_stereo_fix_same_pass_enabled();

        if (true_index == 1 &&
            !subnautica2_native_stereo_updates_control_rotation_before_views &&
            vr->is_any_aim_method_active() &&
            !controller_camera_guard_active &&
            (vr->is_aim_modify_player_control_rotation_enabled() || direct_aim_compatibility_fallback))
        {
            if (g_hook->m_tracking_system_hook != nullptr) {
                g_hook->m_tracking_system_hook->manual_update_control_rotation();
            }
        }
    }

#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
    SPDLOG_INFO("Finished calculating stereo view offset!");
#else
    SPDLOG_INFO_ONCE("Finished calculating stereo view offset!");
#endif
}

__forceinline Matrix4x4f* FFakeStereoRenderingHook::calculate_stereo_projection_matrix(FFakeStereoRendering* stereo, Matrix4x4f* out, const int32_t view_index) {
#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
    SPDLOG_INFO("calculate stereo projection matrix called! {} from {:x}", view_index, (uintptr_t)_ReturnAddress() - (uintptr_t)utility::get_module_within((uintptr_t)_ReturnAddress()).value_or(nullptr));
#else
    SPDLOG_INFO_ONCE("calculate stereo projection matrix called! {} from {:x}", view_index, (uintptr_t)_ReturnAddress() - (uintptr_t)utility::get_module_within((uintptr_t)_ReturnAddress()).value_or(nullptr));
#endif

    auto& vr = VR::get();

    // Only call PostInitProperties if ghosting fix enabled or native stereo is being used.
    // Also, if we don't have a hook on GetDesiredNumberOfViews, we need to call PostInitProperties
    //if (!vr->is_using_afr() || vr->is_ghosting_fix_enabled() || !g_hook->m_get_desired_number_of_views_hook) {
    if (!vr->should_skip_post_init_properties()) {
        if (!g_hook->m_fixed_localplayer_view_count) {
            if (!g_hook->m_calculate_stereo_projection_matrix_post_hook) {
                const auto return_address = (uintptr_t)_ReturnAddress();
                SPDLOG_INFO("Inserting midhook after CalculateStereoProjectionMatrix... @ {:x}", return_address);

                constexpr auto max_stack_depth = 100;
                uintptr_t stack[max_stack_depth]{};

                const auto depth = RtlCaptureStackBackTrace(0, max_stack_depth, (void**)&stack, nullptr);

                for (int i = 0; i < depth; i++) {
                    g_hook->m_projection_matrix_stack.push_back(stack[i]);
                    SPDLOG_INFO(" {:x}", (uintptr_t)stack[i]);
                }

                g_hook->m_calculate_stereo_projection_matrix_post_hook = safetyhook::create_mid((void*)return_address, &FFakeStereoRenderingHook::post_calculate_stereo_projection_matrix);

                if (!g_hook->m_calculate_stereo_projection_matrix_post_hook) {
                    SPDLOG_ERROR("Failed to insert midhook after CalculateStereoProjectionMatrix!");
                }
            }
        } else if (g_hook->m_calculate_stereo_projection_matrix_post_hook) {
            SPDLOG_INFO("Removing midhook after CalculateStereoProjectionMatrix, job is done...");
            g_hook->m_calculate_stereo_projection_matrix_post_hook = {};
            g_hook->m_get_projection_data_pre_hook = {};
        }   
    }

    if (!g_framework->is_game_data_intialized()) {
        if (g_hook->m_calculate_stereo_projection_matrix_hook) {
            return g_hook->m_calculate_stereo_projection_matrix_hook.call<Matrix4x4f*>(stereo, out, view_index);
        }

        return out;
    }

    static bool index_starts_from_one = true;
    static bool index_was_ever_two = false;

    // This is eSSP_FULL, we don't care. It will cause the view to become monoscopic if we do anything.
    // or maybe we should, this could be used for WorldToScreen.
    /*if (index_was_ever_two && view_index == 0) {
        SPDLOG_INFO_ONCE("Index was ever two, and now it's zero. This is eSSP_FULL, we don't care. It will cause the view to become monoscopic if we do anything.");
        return out;
    }*/

    if (view_index == 2) {
        index_starts_from_one = true;
        index_was_ever_two = true;
    } else if (view_index == 0) {
        index_starts_from_one = false;
    }

    // Can happen if we hooked this differently.
    if (g_hook->m_calculate_stereo_projection_matrix_hook) {
        g_hook->m_calculate_stereo_projection_matrix_hook.call<Matrix4x4f*>(stereo, out, view_index);
    } else {
        if (g_hook->m_has_double_precision) {
            (*out)[3][2] = sdk::globals::get_near_clipping_plane();
        } else {
            (*(Matrix4x4d*)out)[3][2] = (double)sdk::globals::get_near_clipping_plane();
        }
    }

    if (VR::get()->is_using_2d_screen()) {
        float fov = 90.0f; // todo, get from FMinimalViewInfo

        const float width = VR::get()->get_hmd_width();
        const float height = VR::get()->get_hmd_height();
        const float half_fov = glm::radians(fov) / 2.0f;
        const float xs = 1.0f / glm::tan(half_fov);
        const float ys = width / glm::tan(half_fov) / height;
        const float near_z = sdk::globals::get_near_clipping_plane();

        if (g_hook->m_has_double_precision) {
            (*(Matrix4x4d*)out) = Matrix4x4d {
                xs, 0.0, 0.0, 0.0,
                0.0, ys, 0.0, 0.0,
                0.0, 0.0, 0.0, 1.0,
                0.0, 0.0, near_z, 0.0
            };
        } else {
            *out = Matrix4x4f {
                xs, 0.0f, 0.0f, 0.0f,
                0.0f, ys, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f,
                0.0f, 0.0f, near_z, 0.0f
            };
        }

        return out;
    }

    // SPDLOG_INFO("NearZ: {}", old_znear);

    if (out != nullptr) {
        auto true_index = index_starts_from_one ? ((view_index + 1) % 2) : (view_index % 2);
    
        if (vr->is_using_afr()) {
            true_index = g_frame_count % 2;
        }

        auto& double_matrix = *(Matrix4x4d*)out;

        if (!g_hook->m_has_double_precision) {
            float old_znear = (*out)[3][2];
            VR::get()->m_nearz = old_znear;            
            VR::get()->get_runtime()->update_matrices(old_znear, 10000.0f);
        } else {
            double old_znear = (double_matrix)[3][2];
            VR::get()->m_nearz = (float)old_znear;
            VR::get()->get_runtime()->update_matrices((float)old_znear, 10000.0f);
        }

        if (!g_hook->m_has_double_precision) {
            *out = VR::get()->get_projection_matrix((VRRuntime::Eye)(true_index));
        } else {
            const auto fmat = VR::get()->get_projection_matrix((VRRuntime::Eye)(true_index));
            double_matrix = fmat;
        }
    } else {
        SPDLOG_ERROR("CalculateStereoProjectionMatrix returned nullptr!");
    }

#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
    SPDLOG_INFO("Finished calculating stereo projection matrix!");
#else
    SPDLOG_INFO_ONCE("Finished calculating stereo projection matrix!");
#endif
    
    return out;
}

__forceinline void FFakeStereoRenderingHook::render_texture_render_thread(FFakeStereoRendering* stereo, FRHICommandListImmediate* rhi_command_list,
    FRHITexture2D* backbuffer, FRHITexture2D* src_texture, double window_size) 
{
    if (!g_framework->is_game_data_intialized()) {
        return;
    }

#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
    SPDLOG_INFO("render texture render thread called!");
#else
    SPDLOG_INFO_ONCE("render texture render thread called!");
#endif


    if (!g_hook->is_slate_hooked() && g_hook->has_attempted_to_hook_slate()) {
        SPDLOG_INFO("Attempting to hook SlateRHIRenderer::DrawWindow_RenderThread using RenderTexture_RenderThread return address...");
        const auto return_address = (uintptr_t)_ReturnAddress();
        SPDLOG_INFO(" Return address: {:x}", return_address);
        g_hook->attempt_hook_slate_thread(return_address);
    }

    g_hook->get_slate_thread_worker()->execute(rhi_command_list);

    /*const auto return_address = (uintptr_t)_ReturnAddress();
    const auto slate_cvar_usage_location = sdk::vr::get_slate_draw_to_vr_render_target_usage_location();

    if (slate_cvar_usage_location) {
        const auto distance_from_usage = (intptr_t)(return_address - *slate_cvar_usage_location);

        if (distance_from_usage <= 0x200) {
            //SPDLOG_INFO("Ret: {:x} Distance: {:x}", return_address, distance_from_usage);

            auto& d3d11_vr = VR::get()->m_d3d11;
            auto& hook = g_framework->get_d3d11_hook();
            auto device = hook->get_device();
            ComPtr<ID3D11DeviceContext> context{};

            device->GetImmediateContext(&context);
            context->CopyResource(d3d11_vr.get_test_tex().Get(), (ID3D11Resource*)src_texture->get_native_resource());
            context->Flush();
        }
    }*/

    //g_hook->m_rtm.set_render_target(src_texture);

    /*if (g_hook->m_rtm.get_scene_target() != src_texture) {
        g_hook->m_rtm.set_render_target(src_texture);
    }*/

    // SPDLOG_INFO("{:x}", (uintptr_t)src_texture->GetNativeResource());

    // maybe the window size is actually a pointer we will find out later.
    /*if (g_hook->m_render_texture_render_thread_hook) {
        g_hook->m_render_texture_render_thread_hook->call<void*>(stereo, rhi_command_list, backbuffer, src_texture, window_size);
    }*/
}

void FFakeStereoRenderingHook::init_canvas(FFakeStereoRendering* stereo, sdk::FSceneView* view, UCanvas* canvas) {
#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
    SPDLOG_INFO("init canvas called!");
#else
    SPDLOG_INFO_ONCE("init canvas called!");
#endif

    if (!g_framework->is_game_data_intialized()) {
        return;
    }

    // Since the FSceneView and UCanvas structures will probably vary wildly
    // in terms of field offsets and size, we will need to dynamically scan
    // from the return address of this function to find the ViewProjectionMatrix offset.
    // in the FSceneView and also the UCanvas.
    // it happens in the else block of the conditional statement that calls this function
    static uint32_t fsceneview_viewproj_offset = 0;
    static uint32_t ucanvas_viewproj_offset = 0;

    if (fsceneview_viewproj_offset == 0 || ucanvas_viewproj_offset == 0) {
        SPDLOG_INFO("Searching for FSceneView and UCanvas offsets...");
        SPDLOG_INFO("Canvas: {:x}", (uintptr_t)canvas);

        const auto return_address = (uintptr_t)_ReturnAddress();
        const auto containing_function = utility::find_function_start(return_address);

        SPDLOG_INFO("Found containing function at {:x}", *containing_function);

        auto find_offsets = [](uintptr_t start, uintptr_t end) -> bool {
            for (auto ip = (uintptr_t)start; ip < end + 0x100;) {
                const auto ix = utility::decode_one((uint8_t*)ip);

                if (!ix) {
                    SPDLOG_ERROR("Failed to decode instruction at {:x}", ip);
                    break;
                }

                // The initial instructions look something like this
                /*
                0F 28 86 C0 03 00 00                          movaps  xmm0, xmmword ptr [rsi+3C0h]
                41 0F 11 87 80 02 00 00                       movups  xmmword ptr [r15+280h], xmm0
                */
                if (std::string_view{ix->Mnemonic} == "MOVAPS" && ix->Operands[1].Type == ND_OP_MEM) {
                    const auto next = utility::decode_one((uint8_t*)(ip + ix->Length));

                    if (next) {
                        if (std::string_view{next->Mnemonic} == "MOVUPS" && next->Operands[0].Type == ND_OP_MEM) {
                            fsceneview_viewproj_offset = ix->Operands[1].Info.Memory.Disp;
                            ucanvas_viewproj_offset = next->Operands[0].Info.Memory.Disp;
                            
                            SPDLOG_INFO("Found at {:x}", ip);
                            SPDLOG_INFO("Found FSceneView ViewProjectionMatrix offset: {:x}", fsceneview_viewproj_offset);
                            SPDLOG_INFO("Found UCanvas ViewProjectionMatrix offset: {:x}", ucanvas_viewproj_offset);
                            return true;
                            break;
                        }
                    }
                }

                ip += ix->Length;
            }

            return false;
        };

        if (!find_offsets(*containing_function, return_address)) {
            // If we still didn't find it at this stage, re-scan from the previous function from the previous function call instead.
            const auto potential_func = utility::calculate_absolute(return_address - 4);
            if (!find_offsets(potential_func, potential_func + 0x100)) {
                SPDLOG_ERROR("Failed to find offsets!");
                return;
            }
        }
    }

    //*(Matrix4x4f*)((uintptr_t)view + fsceneview_viewproj_offset) = VR::get()->get_projection_matrix(VRRuntime::Eye::LEFT);
    *(Matrix4x4f*)((uintptr_t)canvas + ucanvas_viewproj_offset) = *(Matrix4x4f*)((uintptr_t)view + fsceneview_viewproj_offset);
}

uint32_t FFakeStereoRenderingHook::get_desired_number_of_views_hook(FFakeStereoRendering* stereo, bool is_stereo_enabled) {
#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
    SPDLOG_INFO("get desired number of views hook called!");
#else
    SPDLOG_INFO_ONCE("get desired number of views hook called!");
#endif

    auto& vr = VR::get();

    if (g_hook->m_sceneview_data.inside_post_init_properties) {
        return 2;
    }

    if (!is_stereo_enabled || (vr->is_using_afr() && !vr->is_splitscreen_compatibility_enabled())) {
        // We need to know about the second scene state to fix ghosting, so set the view count to 2
        // after we know about it, we can continue returning 1.
        if (is_stereo_enabled && vr->is_ghosting_fix_enabled() && vr->is_using_afr() &&
            g_hook->m_sceneview_data.known_scene_states.size() < 2 && g_hook->m_fixed_localplayer_view_count &&
            !!g_hook->m_sceneview_data.constructor_hook && g_hook->m_has_view_extensions_installed)
        {
            // Only works correctly if view extensions are installed, so we can reset the view count to 1 without crashing
            return 2;
        }

        return 1;
    }

    if (vr->is_native_stereo_fix_enabled() && vr->is_native_stereo_fix_same_pass_enabled()) {
        auto rtm = g_hook->get_render_target_manager();
        const auto scene_capture_rt_ready = rtm->get_scene_capture_render_target() != nullptr;
        const auto scene_capture_utexture_ready = rtm->get_scene_capture_utexture() != nullptr;
        const auto fsceneview_hook_ready = !!g_hook->m_sceneview_data.constructor_hook;
        const auto begin_viewfamily_hook_ready = !!g_hook->m_render_module_begin_render_viewfamily_hook;

        if (avowed_is_current_game()) {
            SPDLOG_INFO_EVERY_N_SEC(
                2,
                "[Avowed][NativeStereoFix] GetDesiredNumberOfViews state: stereo_enabled={} scene_rt={} scene_utexture={} fsceneview_hook={} begin_viewfamily_hook={} fixed_localplayer={}",
                is_stereo_enabled,
                scene_capture_rt_ready,
                scene_capture_utexture_ready,
                fsceneview_hook_ready,
                begin_viewfamily_hook_ready,
                g_hook->m_fixed_localplayer_view_count);
        }

        if ((!scene_capture_rt_ready || !fsceneview_hook_ready || !begin_viewfamily_hook_ready)) {
            if (rtm->get_scene_capture_utexture() == nullptr) {
                rtm->create_scene_capture();
            }

            if (avowed_is_current_game()) {
                SPDLOG_INFO_EVERY_N_SEC(2, "[Avowed][NativeStereoFix] Returning one view while native-fix prerequisites initialize");
            }

            return 1; // wait for the scene capture render target to be set and FSceneView constructor to be hooked
        }

        if (avowed_is_current_game()) {
            uint32_t stable_frames = 0;
            uint32_t required_frames = AVOWED_NATIVE_FIX_STABLE_FRAMES;

            if (!avowed_native_fix_gate_ready(&stable_frames, &required_frames)) {
                SPDLOG_INFO_EVERY_N_SEC(
                    2,
                    "[Avowed][NativeStereoFix] Returning one view while render transition stabilizes stable={}/{}",
                    stable_frames,
                    required_frames);
                return 1;
            }
        }
    } else if (vr->is_native_stereo_fix_enabled()) {
        SPDLOG_INFO_ONCE("[NativeStereoDebug] GetDesiredNumberOfViews using explicit native stereo path; not waiting on same-pass scene capture");
    }

    if (avowed_is_current_game() && vr->is_native_stereo_fix_enabled()) {
        SPDLOG_INFO_EVERY_N_SEC(2, "[Avowed][NativeStereoFix] Returning two views for native stereo fix");
    }

    return 2;
}

// Only really necessary for 5.0.3 because for some reason negative view index gets passed into it
// but 5.0.3 doesn't account for this and thinks it's a secondary pass
// so the purpose of the hook (mostly) is to make those return eSSP_FULL to fix a crash
EStereoscopicPass FFakeStereoRenderingHook::get_view_pass_for_index_hook(FFakeStereoRendering* stereo, bool stereo_requested, int32_t view_index) {
#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
    SPDLOG_INFO("get view pass for index hook called! {} {}", stereo_requested, view_index);
#else
    SPDLOG_INFO_ONCE("get view pass for index hook called! {} {}", stereo_requested, view_index);
#endif

    // On 5.0.3 this check is not here, it was only added in 5.1
    // So we need to imitate it here to prevent a crash
    if (!stereo_requested || view_index < 0) {
        return EStereoscopicPass::eSSP_FULL;
    }

    auto& vr = VR::get();

    if (subnautica2_is_current_game() && vr->is_native_stereo_fix_enabled() &&
        !vr->is_native_stereo_fix_same_pass_enabled() &&
        subnautica2_force_primary_primary_views())
    {
        return EStereoscopicPass::eSSP_PRIMARY;
    }

    return view_index % 2 == 0 ? EStereoscopicPass::eSSP_PRIMARY : EStereoscopicPass::eSSP_SECONDARY;
}

IStereoRenderTargetManager* FFakeStereoRenderingHook::get_render_target_manager_hook(FFakeStereoRendering* stereo) {
#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
    SPDLOG_INFO("get render target manager hook called!");
#else
    SPDLOG_INFO_ONCE("get render target manager hook called!");
#endif

    if (!g_framework->is_game_data_intialized()) {
        return nullptr;
    }

    auto vr = VR::get();

    if (vr->is_stereo_emulation_enabled() || vr->is_extreme_compatibility_mode_enabled()) {
        return nullptr;
    }

    if (!vr->get_runtime()->got_first_poses || vr->is_hmd_active()) {
        if (g_hook->m_uses_old_rendertarget_manager) {
            return (IStereoRenderTargetManager*)&g_hook->m_rtm_418;
        }

        if (g_hook->m_special_detected) {
            return (IStereoRenderTargetManager*)&g_hook->m_rtm_special;
        }

        return &g_hook->m_rtm;
    }

    return nullptr;
}

IStereoLayers* FFakeStereoRenderingHook::get_stereo_layers_hook(FFakeStereoRendering* stereo) {
#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
    SPDLOG_INFO("get stereo layers hook called!");
#else
    SPDLOG_INFO_ONCE("get stereo layers hook called!");
#endif

    if (!g_framework->is_game_data_intialized()) {
        return nullptr;
    }

    if (!VR::get()->get_runtime()->got_first_poses || VR::get()->is_hmd_active()) {
        /*static uint8_t fake_data[0x100]{};

        if (*(uintptr_t*)&fake_data == 0) {
            *(uintptr_t*)&fake_data = (uintptr_t)utility::get_executable() + 0x3D13420; // test
        }

        //return &g_hook->m_sl;
        return (IStereoLayers*)&fake_data;*/
    }

    return nullptr;
}

void FFakeStereoRenderingHook::post_calculate_stereo_projection_matrix(safetyhook::Context& ctx) {
#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
    SPDLOG_INFO("post calculate stereo projection matrix called!");
#else
    SPDLOG_INFO_ONCE("post calculate stereo projection matrix called!");
#endif

    if (g_hook->m_fixed_localplayer_view_count || g_hook->m_hooked_alternative_localplayer_scan) {
        return;
    }

    auto vfunc = utility::find_virtual_function_start(g_hook->m_calculate_stereo_projection_matrix_post_hook.target_address());

    if (!vfunc) {
        // attempt to hook GetProjectionData instead to get the localplayer
        SPDLOG_INFO("Failed to find virtual function start for CalculateStereoProjectionMatrix, attempting to hook GetProjectionData instead...");

        if (!g_hook->m_projection_matrix_stack.empty() && g_hook->m_projection_matrix_stack.size() >= 3) {
            const auto post_get_projection_data = g_hook->m_projection_matrix_stack[2];

            const auto get_projection_data_candidate_1 = utility::find_function_start_with_call(post_get_projection_data);
            const auto get_projection_data_candidate_2 = utility::find_virtual_function_start(post_get_projection_data);

            // Select whichever one is closest to post_get_projection_data
            std::optional<uintptr_t> get_projection_data{};

            if (get_projection_data_candidate_1 && get_projection_data_candidate_2) {
                const auto candidate_1_distance = std::abs((int64_t)post_get_projection_data - (int64_t)*get_projection_data_candidate_1);
                const auto candidate_2_distance = std::abs((int64_t)post_get_projection_data - (int64_t)*get_projection_data_candidate_2);

                if (candidate_1_distance < candidate_2_distance) {
                    get_projection_data = get_projection_data_candidate_1;
                } else {
                    get_projection_data = get_projection_data_candidate_2;
                }
            } else if (get_projection_data_candidate_1) {
                get_projection_data = get_projection_data_candidate_1;
            } else if (get_projection_data_candidate_2) {
                get_projection_data = get_projection_data_candidate_2;
            } else {
                // emergency fallback
                SPDLOG_INFO("Failed to find GetProjectionData, falling back to emergency fallback (this may not work)");
                get_projection_data = utility::find_function_start(post_get_projection_data);
            }

            if (get_projection_data) {
                SPDLOG_INFO("Successfully found GetProjectionData at {:x}", *get_projection_data);

                g_hook->m_hooked_alternative_localplayer_scan = true;

                g_hook->m_get_projection_data_pre_hook = safetyhook::create_mid((void*)*get_projection_data, &FFakeStereoRenderingHook::pre_get_projection_data);
                g_hook->m_projection_matrix_stack.clear();

                if (g_hook->m_get_projection_data_pre_hook) {
                    SPDLOG_INFO("Successfully hooked GetProjectionData");
                    return;
                } else {
                    SPDLOG_ERROR("Failed to hook GetProjectionData");
                }
            } else {
                SPDLOG_ERROR("Failed to find GetProjectionData!");
            }
        }
    }

    if (!vfunc) {
        SPDLOG_INFO("Could not find function via normal means, scanning for int3s...");

        const auto ref = utility::scan_reverse(g_hook->m_calculate_stereo_projection_matrix_post_hook.target_address(), 0x2000, "CC CC CC");

        if (ref) {
            vfunc = *ref + 3;
        }

        if (!vfunc) {
            g_hook->m_fixed_localplayer_view_count = true;
            SPDLOG_ERROR("Failed to find virtual function start for post calculate_stereo_projection_matrix!");
            return;
        }
    }

    // Scan forward until we find an assignment of the RCX register into a storage register.
    std::unordered_map<uint32_t, uintptr_t*> register_to_context {
        { NDR_RBX, &ctx.rbx },
        { NDR_RCX, &ctx.rcx },
        { NDR_RDX, &ctx.rdx },
        { NDR_RSI, &ctx.rsi },
        { NDR_RDI, &ctx.rdi },
        { NDR_RBP, &ctx.rbp },
        { NDR_RSP, &ctx.rsp },
        { NDR_R8, &ctx.r8 },
        { NDR_R9, &ctx.r9 },
        { NDR_R10, &ctx.r10 },
        { NDR_R11, &ctx.r11 },
        { NDR_R12, &ctx.r12 },
        { NDR_R13, &ctx.r13 },
        { NDR_R14, &ctx.r14 },
        { NDR_R15, &ctx.r15 },
    };

    INSTRUX ix{};
    std::optional<uint32_t> found_register{};
    auto ip = (uint8_t*)vfunc.value_or(0);

    while (true) {
        const auto status = NdDecodeEx(&ix, (ND_UINT8*)ip, 1000, ND_CODE_64, ND_DATA_64);

        if (!ND_SUCCESS(status)) {
            SPDLOG_INFO("Decoding failed with error {:x}!", (uint32_t)status);
            break;
        }

        if (ix.Instruction == ND_INS_MOV && ix.Operands[0].Type == ND_OP_REG && ix.Operands[1].Type == ND_OP_REG && ix.Operands[1].Info.Register.Reg == NDR_RCX) {
            SPDLOG_INFO("Found assignment of RCX to storage register at {:x} ({})!", (uintptr_t)ip, ix.Operands[0].Info.Register.Reg);
            found_register = ix.Operands[0].Info.Register.Reg;
            break;
        }

        ip += ix.Length;
    }

    if (!found_register) {
        g_hook->m_fixed_localplayer_view_count = true;
        SPDLOG_ERROR("Failed to find assignment of RCX to storage register!");
        return;
    }

    const auto localplayer = *register_to_context[found_register.value_or(0)];
    SPDLOG_INFO("Local player: {:x}", localplayer);

    if (localplayer == 0) {
        g_hook->m_fixed_localplayer_view_count = true;
        SPDLOG_ERROR("Failed to find local player, cannot call PostInitProperties!");
        return;
    }

    if (avowed_is_current_game() && !avowed_is_live_uobject(localplayer)) {
        SPDLOG_WARNING_EVERY_N_SEC(
            2,
            "[Avowed][NativeStereoFix] CalculateStereoProjectionMatrix yielded non-UObject LocalPlayer candidate {:x}; switching to GetProjectionData scan",
            localplayer);

        auto try_hook_get_projection_data = [&]() -> bool {
            if (g_hook->m_hooked_alternative_localplayer_scan || g_hook->m_fixed_localplayer_view_count) {
                return false;
            }

            if (g_hook->m_projection_matrix_stack.size() < 3) {
                SPDLOG_WARNING_EVERY_N_SEC(
                    2,
                    "[Avowed][NativeStereoFix] Cannot install GetProjectionData LocalPlayer scan; projection stack has {} entries",
                    g_hook->m_projection_matrix_stack.size());
                return false;
            }

            const auto post_get_projection_data = g_hook->m_projection_matrix_stack[2];
            const auto get_projection_data_candidate_1 = utility::find_function_start_with_call(post_get_projection_data);
            const auto get_projection_data_candidate_2 = utility::find_virtual_function_start(post_get_projection_data);
            std::optional<uintptr_t> get_projection_data{};

            if (get_projection_data_candidate_1 && get_projection_data_candidate_2) {
                const auto candidate_1_distance = std::abs((int64_t)post_get_projection_data - (int64_t)*get_projection_data_candidate_1);
                const auto candidate_2_distance = std::abs((int64_t)post_get_projection_data - (int64_t)*get_projection_data_candidate_2);
                get_projection_data = candidate_1_distance < candidate_2_distance ? get_projection_data_candidate_1 : get_projection_data_candidate_2;
            } else if (get_projection_data_candidate_1) {
                get_projection_data = get_projection_data_candidate_1;
            } else if (get_projection_data_candidate_2) {
                get_projection_data = get_projection_data_candidate_2;
            } else {
                get_projection_data = utility::find_function_start(post_get_projection_data);
            }

            if (!get_projection_data) {
                SPDLOG_WARNING_EVERY_N_SEC(2, "[Avowed][NativeStereoFix] Failed to locate GetProjectionData; disabling LocalPlayer bootstrap spam");
                return false;
            }

            SPDLOG_INFO("[Avowed][NativeStereoFix] Hooking GetProjectionData at {:x}", *get_projection_data);

            auto hook = safetyhook::create_mid((void*)*get_projection_data, &FFakeStereoRenderingHook::pre_get_projection_data);
            if (!hook) {
                SPDLOG_ERROR("[Avowed][NativeStereoFix] Failed to hook GetProjectionData");
                return false;
            }

            g_hook->m_get_projection_data_pre_hook = std::move(hook);
            g_hook->m_hooked_alternative_localplayer_scan = true;
            g_hook->m_projection_matrix_stack.clear();
            SPDLOG_INFO("[Avowed][NativeStereoFix] Installed GetProjectionData LocalPlayer scan");
            return true;
        };

        if (!try_hook_get_projection_data()) {
            g_hook->m_fixed_localplayer_view_count = true;
        }

        return;
    }

    g_hook->post_init_properties(localplayer);
}

void FFakeStereoRenderingHook::pre_get_projection_data(safetyhook::Context& ctx) {
#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
    SPDLOG_INFO("pre get projection data called!");
#else
    SPDLOG_INFO_ONCE("pre get projection data called!");
#endif

    if (g_hook->m_fixed_localplayer_view_count) {
        return;
    }

    const auto localplayer = ctx.rcx;
    SPDLOG_INFO("Local player: {:x}", localplayer);

    if (localplayer == 0) {
        g_hook->m_fixed_localplayer_view_count = true;
        SPDLOG_ERROR("Failed to find local player, cannot call PostInitProperties!");
        return;
    }

    if (avowed_is_current_game() && !avowed_is_live_uobject(localplayer)) {
        SPDLOG_WARNING_EVERY_N_SEC(
            2,
            "[Avowed][NativeStereoFix] GetProjectionData yielded non-UObject LocalPlayer candidate {:x}; disabling LocalPlayer bootstrap",
            localplayer);
        g_hook->m_fixed_localplayer_view_count = true;
        return;
    }

    g_hook->post_init_properties(localplayer);
}

void FFakeStereoRenderingHook::post_init_properties(uintptr_t localplayer) {
    SPDLOG_INFO("Searching for PostInitProperties virtual function...");

    std::optional<uint32_t> idx{};
    const auto engine = sdk::UEngine::get_lvalue();

    if (engine == nullptr) {
        SPDLOG_ERROR("Cannot proceed without engine!");
        return;
    }

    uintptr_t vtable_address{};

    if (avowed_is_current_game()) {
        uintptr_t class_address{};
        if (!avowed_is_live_uobject(localplayer, &vtable_address, &class_address)) {
            SPDLOG_WARNING_EVERY_N_SEC(
                2,
                "[Avowed][NativeStereoFix] Refusing PostInitProperties on unsafe LocalPlayer candidate {:x}",
                localplayer);
            g_hook->m_fixed_localplayer_view_count = true;
            return;
        }

        SPDLOG_INFO_ONCE("[Avowed][NativeStereoFix] LocalPlayer candidate validated via GUObjectArray (object={:x}, class={:x}, vtable={:x})",
            localplayer,
            class_address,
            vtable_address);
    } else {
        vtable_address = *(uintptr_t*)localplayer;
    }

    const auto vtable = (uintptr_t*)vtable_address;

    if (vtable == nullptr || IsBadReadPtr((void*)vtable, sizeof(void*))) {
        SPDLOG_ERROR("Cannot proceed, vtable for so-called \"local player\" is invalid!");
        return;
    }

    const auto ue51_post_init = is_ue_5_1_dx_backend();
    const auto needs_source_informed_post_init = is_ue_5_7_or_newer() || is_ue_5_6_dx12_backend() || is_ue_5_5_dx_backend() || is_ue_5_4_dx_backend() || ue51_post_init;

    if (needs_source_informed_post_init) {
        idx = resolve_post_init_properties_index_from_uobject(localplayer);
    }

    if (ue51_post_init && !idx) {
        g_hook->m_sceneview_data.known_scene_states.clear();
        g_hook->m_fixed_localplayer_view_count = true;
        return;
    }

    for (auto i = 1; !idx && i < 25; ++i) {
        if (idx) {
            break;
        }

        SPDLOG_INFO("Analyzing index {}...", i);

        const auto vfunc = vtable[i];

        if (vfunc == 0 || IsBadReadPtr((void*)vfunc, 1)) {
            SPDLOG_ERROR("Encountered invalid vfunc at index {}!", i);
            break;
        }

        SPDLOG_INFO("Scanning vfunc at index {} ({:x})...", i, vfunc);

        utility::exhaustive_decode((uint8_t*)vfunc, 25, [&](INSTRUX& ix, uintptr_t ip) -> utility::ExhaustionResult {
            if (idx) {
                return utility::ExhaustionResult::BREAK;
            }

            if (const auto disp = utility::resolve_displacement(ip); disp) {
                // the second expression catches UE dynamic/debug builds
                if (*disp == (uintptr_t)engine || 
                    (!IsBadReadPtr((void*)*disp, sizeof(void*)) && *(uintptr_t*)*disp == (uintptr_t)*engine)) 
                {
                    SPDLOG_INFO("Found PostInitProperties through legacy body scan at {} {:x}!", i, (uintptr_t)vfunc);
                    idx = i;
                    return utility::ExhaustionResult::BREAK;
                }
            }

            return utility::ExhaustionResult::CONTINUE;
        });
    }

    if (!idx) {
        if (needs_source_informed_post_init) {
            SPDLOG_WARN("Failed to find PostInitProperties virtual function on UE 5.4+/modern path; skipping LocalPlayer bootstrap for safety");
            g_hook->m_sceneview_data.known_scene_states.clear();
            g_hook->m_fixed_localplayer_view_count = true;
            return;
        }

        SPDLOG_ERROR("Failed to find PostInitProperties virtual function! A crash may occur!");
    }

    // Now call PostInitProperties.
    // The purpose of this is setting up the view for the other eye.
    // Just creating the StereoRenderingDevice does not automatically do it, so we have to do it manually.
    // Usually the game just calls this function near startup after calling InitializeHMDDevice.
    if (idx) {
        SPDLOG_INFO("Calling PostInitProperties on local player!");

        // Get PEB and set debugger present
        auto peb = (PEB*)__readgsqword(0x60);

        const auto old = peb->BeingDebugged;
        peb->BeingDebugged = true;

        // If the exception count exceeds a certain amount, we need to un-nop the function call because it was supposed to return a pointer.
        static auto exception_count = 0;
        static std::vector<Patch::Ptr> patches{};
        static std::vector<uintptr_t> patch_locations{};

        static std::vector<Patch::Ptr> assert_patches{};
        const void (*post_init_properties)(uintptr_t) = (*(decltype(post_init_properties)**)localplayer)[*idx];

        // Scan through all of the branches of PostInitProperties to find any assertions
        // The assertion we're looking for is easily identified by a string that it loads in RCX, named "!Reference"
        // If we dont do this, there's a possibility that the game will crash at some point or cause some sort of corruption
        utility::exhaustive_decode((uint8_t*)post_init_properties, 100, [](INSTRUX& ix, uintptr_t ip) -> utility::ExhaustionResult {
            if (ix.Operands[1].Type == ND_OP_MEM) {
                const auto referenced_addr = utility::resolve_displacement(ip);

                if (referenced_addr) try {
                    if (std::string_view{(const char*)*referenced_addr}.starts_with("!Reference")) {
                        // Scan forward and patch out the first call or jmp we run into
                        utility::exhaustive_decode((uint8_t*)ip, 10, [](INSTRUX& ix, uintptr_t ip) -> utility::ExhaustionResult {
                            if (*(uint8_t*)ip == 0xE8) {
                                SPDLOG_INFO("Patching assertion at {:x}!", ip);
                                assert_patches.push_back(Patch::create(ip, { 0x90, 0x90, 0x90, 0x90, 0x90 }));
                                return utility::ExhaustionResult::BREAK;
                            }

                            if (*(uint8_t*)ip == 0xE9) {
                                SPDLOG_INFO("Patching assertion at {:x}!", ip);
                                assert_patches.push_back(Patch::create(ip, { 0xC3 }));
                                return utility::ExhaustionResult::BREAK;
                            }

                            if (std::string_view{ix.Mnemonic}.starts_with("CALL")) {
                                SPDLOG_INFO("Patching assertion at {:x}!", ip);
                                std::vector<int16_t> nop{};
                                for (auto i = 0; i < ix.Length; ++i) {
                                    nop.push_back(0x90);
                                }

                                assert_patches.push_back(Patch::create(ip, nop));
                                return utility::ExhaustionResult::BREAK;
                            }

                            return utility::ExhaustionResult::CONTINUE;
                        });
                    }
                } catch(...) {

                }
            }

            return utility::ExhaustionResult::CONTINUE;
        });

        // set up a handler to skip int3 assertions
        // we do this because debug builds assert when the views are already setup.
        const auto seh_handler = [](PEXCEPTION_POINTERS info) -> LONG {
            ++exception_count;

            if (info->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT) {
                SPDLOG_INFO("Skipping int3 breakpoint at {:x}!", info->ContextRecord->Rip);
                const auto insn = utility::decode_one((uint8_t*)info->ContextRecord->Rip);

                if (insn) {
                    spdlog::info("Skipping {} bytes!", insn->Length);
                    info->ContextRecord->Rip += insn->Length;

                    // Nop out the next function call.
                    // It logs and does some other stuff and causes a crash later on.
                    // To be seen if this will cause any issues, does not appear to (on 4.9 debug builds)
                    const auto call = utility::scan_disasm((uintptr_t)info->ContextRecord->Rip, 20, "E8 ? ? ? ?");

                    if (call) {
                        patch_locations.push_back(*call);
                        patches.emplace_back(Patch::create(*call, {0x90, 0x90, 0x90, 0x90, 0x90}));
                    }

                    return EXCEPTION_CONTINUE_EXECUTION;
                }
                
                return EXCEPTION_CONTINUE_EXECUTION;
            }

            SPDLOG_INFO("Encountered exception {:x} at {:x}!", info->ExceptionRecord->ExceptionCode, info->ContextRecord->Rip);

            // This happens if we removed a call that shouldn't have been removed.
            if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && !patches.empty()) {
                SPDLOG_WARN("Access violation at {:x}! Removing patch at {:x}!", info->ContextRecord->Rip, patch_locations.back());

                exception_count = 0;
                info->ContextRecord->Rip = patch_locations.back();
                patches.pop_back();
                patch_locations.pop_back();
            } else {
                const auto insn = utility::decode_one((uint8_t*)info->ContextRecord->Rip);

                if (insn) {
                    info->ContextRecord->Rip += insn->Length;
                } else {
                    info->ContextRecord->Rip += 1;
                }
            }

            // yolo? idk xd
            return EXCEPTION_CONTINUE_EXECUTION;
        };

        const auto exception_handler = AddVectoredExceptionHandler(1, seh_handler);

        m_sceneview_data.inside_post_init_properties = true;
        post_init_properties(localplayer);
        m_sceneview_data.inside_post_init_properties = false;

        SPDLOG_INFO("PostInitProperties called!");

        // remove the handler
        RemoveVectoredExceptionHandler(exception_handler);
        peb->BeingDebugged = old;
    }

    g_hook->m_sceneview_data.known_scene_states.clear();
    g_hook->m_fixed_localplayer_view_count = true;
}

void FFakeStereoRenderingHook::ue57_add_slate_draw_elements_pass_hook(safetyhook::Context& ctx) {
    if (g_hook == nullptr || !is_ue_5_7_or_newer() || !g_framework->is_dx12()) {
        return;
    }

    if (!g_hook->m_inside_slate_draw_window || GetCurrentThreadId() != g_hook->m_slate_draw_window_thread_id) {
        return;
    }

    auto* inputs = reinterpret_cast<UE57SlateDrawElementsPassInputsHead*>(ctx.r8);

    if (!looks_like_ue57_slate_draw_elements_inputs(inputs)) {
        return;
    }

    const auto scene_viewport_texture = inputs->scene_viewport_texture;
    const auto elements_texture = inputs->elements_texture;

    static bool logged_valid_path{false};

    if (elements_texture != scene_viewport_texture) {
        static bool logged_separate_path{false};

        if (!logged_valid_path) {
            logged_valid_path = true;
            SPDLOG_INFO("[UE 5.7 Slate] Validated AddSlateDrawElementsPass inputs");
        }

        if (!logged_separate_path) {
            logged_separate_path = true;
            SPDLOG_INFO("[UE 5.7 Slate] DrawElements pass already has a separate ElementsTexture");
        }
    } else {
        if (!logged_valid_path) {
            logged_valid_path = true;
            SPDLOG_INFO("[UE 5.7 Slate] Validated AddSlateDrawElementsPass inputs");
        }

        SPDLOG_INFO_EVERY_N_SEC(5, "[UE 5.7 Slate] DrawElements pass is still aliasing ElementsTexture to SceneViewportTexture");
    }
}

namespace {
struct UE55SlateDrawWindowPassInputsHead {
    void* renderer;
    void* window_element_list;
    void* window;
    sdk::FViewportInfo* viewport_info;
};

struct UE55SlatePostProcessArrayView {
    void* data;
    uint64_t count;
};

struct UE55FIntPoint {
    int32_t x;
    int32_t y;
};

struct UE55FIntRect {
    UE55FIntPoint min;
    UE55FIntPoint max;
};

struct UE55SlateDrawWindowPassInputs {
    void* renderer;
    void* window_element_list;
    void* window;
    sdk::FViewportInfo* viewport_info;
    UE55SlatePostProcessArrayView post_process_requests;
    UE55FIntPoint cursor_position;
    UE55FIntRect scene_view_rect;
    float viewport_scale_ui;
};

struct UE55SlateDrawWindowPassOutputs {
    void* viewport_rhi;
    FRHITexture2D* viewport_texture_rhi;
    FRHITexture2D* output_texture_rhi;
};

struct UE55SlateExtent {
    uint32_t width{};
    uint32_t height{};
};

bool try_read_ue55_slate_draw_inputs(void* candidate, void* renderer, UE55SlateDrawWindowPassInputsHead& out) {
    if (!is_readable_process_range((uintptr_t)candidate, sizeof(UE55SlateDrawWindowPassInputsHead))) {
        return false;
    }

    memcpy(&out, candidate, sizeof(out));

    if (out.renderer != renderer || out.window == nullptr) {
        return false;
    }

    return is_readable_process_range((uintptr_t)out.window, sizeof(void*));
}

bool try_read_ue55_slate_draw_inputs_full(void* candidate, void* renderer, UE55SlateDrawWindowPassInputs& out) {
    if (!is_readable_process_range((uintptr_t)candidate, sizeof(UE55SlateDrawWindowPassInputs))) {
        return false;
    }

    memcpy(&out, candidate, sizeof(out));

    if (out.renderer != renderer || out.window == nullptr) {
        return false;
    }

    return is_readable_process_range((uintptr_t)out.window, sizeof(void*));
}

std::optional<UE55SlateExtent> ue55_get_slate_expected_extent(const UE55SlateDrawWindowPassInputs& inputs) {
    const auto width = inputs.scene_view_rect.max.x - inputs.scene_view_rect.min.x;
    const auto height = inputs.scene_view_rect.max.y - inputs.scene_view_rect.min.y;

    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        return std::nullopt;
    }

    return UE55SlateExtent{(uint32_t)width, (uint32_t)height};
}

bool looks_like_vtable_object(void* object) {
    uintptr_t vtable{};
    return safe_read_value((uintptr_t)object, vtable) && looks_like_virtual_function_table(vtable);
}

bool try_call_slate_viewport_bool_slot(sdk::ISlateViewport* viewport, size_t slot, bool& out) {
    out = false;

    if (viewport == nullptr || !looks_like_vtable_object(viewport)) {
        return false;
    }

    uintptr_t vtable{};
    if (!safe_read_value((uintptr_t)viewport, vtable) ||
        !is_readable_process_range(vtable + (slot * sizeof(void*)), sizeof(void*)))
    {
        return false;
    }

    uintptr_t fn{};
    if (!safe_read_value(vtable + (slot * sizeof(void*)), fn) ||
        fn == 0 ||
        !is_executable_process_range(fn, 1))
    {
        return false;
    }

    using BoolVirtualFn = bool (*)(sdk::ISlateViewport*);

    __try {
        out = ((BoolVirtualFn)fn)(viewport);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

sdk::FSlateResource* try_get_slate_viewport_render_target_texture(sdk::ISlateViewport* viewport, bool& faulted) {
    faulted = false;

    __try {
        return viewport != nullptr ? viewport->GetViewportRenderTargetTexture() : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        faulted = true;
        return nullptr;
    }
}

bool ue55_is_valid_ui_texture_candidate(
    VRRenderTargetManager_Base* rtm,
    FRHITexture2D* texture,
    std::optional<UE55SlateExtent> expected_extent,
    const char* source)
{
    if (!supports_ue55_dedicated_ui_target_for_current_game() || rtm == nullptr || texture == nullptr || IsBadReadPtr(texture, sizeof(void*))) {
        return false;
    }

    if (texture == rtm->get_render_target()) {
        SPDLOG_INFO_EVERY_N_SEC(2, "[UE5.5][SlateUI] rejecting {} candidate because it matches the scene render target: tex={:x}",
            source != nullptr ? source : "<unknown>", (uintptr_t)texture);
        return false;
    }

    const auto desc = ue55_try_get_d3d12_desc(texture, source);
    if (!desc) {
        return false;
    }

    if (!expected_extent || expected_extent->width == 0 || expected_extent->height == 0) {
        SPDLOG_INFO_EVERY_N_SEC(2,
            "[UE5.5][SlateUI] observing {} candidate tex={:x} [{}x{} fmt={} flags=0x{:x}] but no trusted Slate extent is available yet",
            source != nullptr ? source : "<unknown>",
            (uintptr_t)texture,
            desc->Width,
            desc->Height,
            (uint32_t)desc->Format,
            (uint32_t)desc->Flags);
        return false;
    }

    if (desc->Width != expected_extent->width || desc->Height != expected_extent->height) {
        SPDLOG_INFO_EVERY_N_SEC(2,
            "[UE5.5][SlateUI] rejecting {} candidate tex={:x} because extent [{}x{}] != expected Slate [{}x{}]",
            source != nullptr ? source : "<unknown>",
            (uintptr_t)texture,
            desc->Width,
            desc->Height,
            expected_extent->width,
            expected_extent->height);
        return false;
    }

    SPDLOG_INFO_EVERY_N_SEC(2,
        "[UE5.5][SlateUI] accepted {} candidate tex={:x} [{}x{} fmt={} flags=0x{:x}]",
        source != nullptr ? source : "<unknown>",
        (uintptr_t)texture,
        desc->Width,
        desc->Height,
        (uint32_t)desc->Format,
        (uint32_t)desc->Flags);

    return true;
}

void ue55_promote_slate_outputs(
    VRRenderTargetManager_Base* rtm,
    void* outputs_ptr,
    std::optional<UE55SlateExtent> expected_extent)
{
    if (!supports_ue55_dedicated_ui_target_for_current_game() || rtm == nullptr || outputs_ptr == nullptr) {
        return;
    }

    if (!is_readable_process_range((uintptr_t)outputs_ptr, sizeof(UE55SlateDrawWindowPassOutputs))) {
        SPDLOG_INFO_EVERY_N_SEC(2, "[UE5.5][SlateUI] DrawWindow outputs are not readable yet: {:x}", (uintptr_t)outputs_ptr);
        return;
    }

    UE55SlateDrawWindowPassOutputs outputs{};
    memcpy(&outputs, outputs_ptr, sizeof(outputs));

    SPDLOG_INFO_EVERY_N_SEC(2,
        "[UE5.5][SlateUI] DrawWindow outputs viewport_rhi={:x} viewport_texture={:x} output_texture={:x} expected={}x{}",
        (uintptr_t)outputs.viewport_rhi,
        (uintptr_t)outputs.viewport_texture_rhi,
        (uintptr_t)outputs.output_texture_rhi,
        expected_extent ? expected_extent->width : 0,
        expected_extent ? expected_extent->height : 0);

    if (ue55_is_valid_ui_texture_candidate(rtm, outputs.viewport_texture_rhi, expected_extent, "DrawWindow viewport texture output")) {
        if (rtm->get_dedicated_ui_target() != outputs.viewport_texture_rhi) {
            rtm->set_dedicated_ui_target(outputs.viewport_texture_rhi, expected_extent->width, expected_extent->height);
            rtm->get_fallback_ui_target_ref() = nullptr;
            if (mechwarrior_clans_is_current_game()) {
                rtm->cancel_dedicated_ui_creation_preserving_target("MechWarrior DrawWindow viewport texture");
            }
            SPDLOG_WARN("[UE5.5][SlateUI] promoted DrawWindow viewport texture output as dedicated UI target");
        }
        return;
    }

    if (ue55_is_valid_ui_texture_candidate(rtm, outputs.output_texture_rhi, expected_extent, "DrawWindow output texture")) {
        SPDLOG_INFO_EVERY_N_SEC(2,
            "[UE5.5][SlateUI] DrawWindow output texture is window-sized but not promoted; explicit SlateOutputTexture routing remains preferred");
    }
}
}

void FFakeStereoRenderingHook::ue55_slate_output_texture_register_hook(safetyhook::Context& ctx) {
    if (g_hook == nullptr || !supports_ue55_dedicated_ui_target_for_current_game()) {
        return;
    }

    if (!g_hook->m_inside_slate_draw_window || GetCurrentThreadId() != g_hook->m_slate_draw_window_thread_id) {
        return;
    }

    if (ctx.r8 == 0 ||
        !is_readable_process_range(ctx.r8, sizeof(wchar_t) * 19) ||
        !std::wstring_view{(const wchar_t*)ctx.r8, 18}.starts_with(L"SlateOutputTexture"))
    {
        return;
    }

    auto* rtm = g_hook->get_render_target_manager();
    auto* ui_target = rtm != nullptr ? rtm->get_dedicated_ui_target() : nullptr;

    if (rtm == nullptr || ui_target == nullptr || ui_target == rtm->get_render_target()) {
        SPDLOG_INFO_EVERY_N_SEC(1, "[UE5.5][SlateUI] SlateOutputTexture call reached before a valid dedicated UI target exists");
        return;
    }

    const auto desc = ue55_try_get_d3d12_desc(ui_target, "dedicated UI target at SlateOutputTexture");
    if (!desc) {
        SPDLOG_INFO_EVERY_N_SEC(1, "[UE5.5][SlateUI] dedicated UI target is not a valid D3D12 texture yet: {:x}", (uintptr_t)ui_target);
        return;
    }

    const auto expected_width = rtm->get_dedicated_ui_width();
    const auto expected_height = rtm->get_dedicated_ui_height();

    if (expected_width != 0 && expected_height != 0 &&
        (desc->Width != expected_width || desc->Height != expected_height))
    {
        SPDLOG_INFO_EVERY_N_SEC(1,
            "[UE5.5][SlateUI] refusing SlateOutputTexture replacement because dedicated UI target extent [{}x{}] != requested [{}x{}]",
            desc->Width,
            desc->Height,
            expected_width,
            expected_height);
        return;
    }

    const auto original = (FRHITexture2D*)ctx.rdx;
    const auto original_desc = ue55_try_get_d3d12_desc(original, "original SlateOutputTexture");

    SPDLOG_INFO_EVERY_N_SEC(1,
        "[UE5.5][SlateUI] routing SlateOutputTexture original={:x} [{}x{}] -> dedicated={:x} [{}x{} fmt={}]",
        (uintptr_t)original,
        original_desc ? original_desc->Width : 0,
        original_desc ? original_desc->Height : 0,
        (uintptr_t)ui_target,
        desc->Width,
        desc->Height,
        (uint32_t)desc->Format);

    ctx.rdx = (uintptr_t)ui_target;
}

void* FFakeStereoRenderingHook::slate_draw_window_render_thread(void* renderer, void* a2, void* a3, 
                                                                void* a4, void* params, void* unk1, void* unk2) 
{
#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
    SPDLOG_INFO("SlateRHIRenderer::DrawWindow_RenderThread called!");
#else
    SPDLOG_INFO_ONCE("SlateRHIRenderer::DrawWindow_RenderThread called!");
#endif

    g_framework->notify_render_activity();

    if (!g_framework->is_game_data_intialized() || a2 == nullptr) {
        return g_hook->m_slate_thread_hook.call<void*>(renderer, a2, a3, a4, params, unk1, unk2);
    }

    auto viewport_info = (sdk::FViewportInfo*)a3;
    sdk::ISlateViewport* slate_viewport = nullptr; // UE5.5+
    UE55SlateDrawWindowPassInputsHead ue55_inputs{};
    UE55SlateDrawWindowPassInputs ue55_inputs_full{};
    const auto a4_is_ue_5_5_variant = try_read_ue55_slate_draw_inputs(a4, renderer, ue55_inputs);
    const auto a4_has_ue_5_5_full_inputs = a4_is_ue_5_5_variant && try_read_ue55_slate_draw_inputs_full(a4, renderer, ue55_inputs_full);

    if (a4_is_ue_5_5_variant) {
        SPDLOG_INFO_ONCE("[SlateRHIRenderer::DrawWindow_RenderThread] Using UE 5.5 FSlateDrawWindowPassInputs layout");
        viewport_info = ue55_inputs.viewport_info;
    }

    if (!a4_is_ue_5_5_variant) {
        // How are we going to fix this on UE5.5?
        g_hook->get_slate_thread_worker()->execute((FRHICommandListImmediate*)a2);
    } else if (!supports_ue55_dedicated_ui_target_for_current_game()) {
        const auto window = (uintptr_t)ue55_inputs.window;

        static std::optional<size_t> viewport_offset = [&]() -> std::optional<size_t> {
            std::optional<size_t> result{};
            const auto module_within = utility::get_module_within(g_hook->m_slate_thread_hook.target_address());

            // Temporarily unhook the DrawWindow_RenderThread hook because we need to emulate the function
            // We could use the trampoline but bdshemu is picky about whether RIP is
            // within the "shellcode" or not (e.g. within the module bounds)
            // and so, the hook must be temporarily unhooked
            if (!module_within) {
                SPDLOG_ERROR("[SlateRHIRenderer::DrawWindow_RenderThread] Failed to get module within for target address!");
                return result;
            }

            SPDLOG_DEBUG("[SlateRHIRenderer::DrawWindow_RenderThread] Module within: {:x}", (uintptr_t)*module_within);

            if (!g_hook->m_slate_thread_hook.disable().has_value()) {
                SPDLOG_ERROR("[SlateRHIRenderer::DrawWindow_RenderThread] Failed to disable slate thread hook!");
                return result;
            }

            utility::ScopeGuard guard{[&]() {
                SPDLOG_DEBUG("[SlateRHIRenderer::DrawWindow_RenderThread] Re-enabling slate thread hook");
                if (!g_hook->m_slate_thread_hook.enable().has_value()) {
                    SPDLOG_ERROR("[SlateRHIRenderer::DrawWindow_RenderThread] Failed to re-enable slate thread hook!");
                }
            }};

            utility::ShemuContext ctx{*module_within};
            ctx.ctx->Registers.RegRip = (ND_UINT64)g_hook->m_slate_thread_hook.target_address();
            ctx.ctx->Registers.RegRcx = (ND_UINT64)renderer;
            ctx.ctx->Registers.RegRdx = (ND_UINT64)a2;
            ctx.ctx->Registers.RegR8 = (ND_UINT64)a3;
            ctx.ctx->Registers.RegR9 = (ND_UINT64)a4;
            ctx.ctx->MemThreshold = 1000;

            uint32_t window_getter_callstack_level = 0;
            std::span<uint8_t> window_bounds{(uint8_t*)window, (uint8_t*)window + 0x1000};

            utility::emulate(*module_within, ctx.ctx->Registers.RegRip, 1000, ctx, [&](const utility::ShemuContextExtended& ctx) -> utility::ExhaustionResult {
                SPDLOG_DEBUG("[SlateRHIRenderer::DrawWindow_RenderThread] Emulating instruction: {:x} ({:X})", ctx.ctx->ctx->Registers.RegRip, ctx.ctx->ctx->Registers.RegRip - (uintptr_t)*module_within);

                // Allow writes to go through if we are inside the window getter.
                // The downside is this might unintentionally increase the reference count of the window
                // but it's necessary for the window getter to not give us a nullptr.
                if (ctx.next.writes_to_memory && window_getter_callstack_level == 0) {
                    return utility::ExhaustionResult::STEP_OVER;
                }

                if (std::string_view{ctx.next.ix.Mnemonic}.starts_with("CALL")) {
                    if (window_getter_callstack_level > 0) {
                        SPDLOG_INFO("[SlateRHIRenderer::DrawWindow_RenderThread] Allowing call inside window getter function, continuing!");
                        ++window_getter_callstack_level;
                        return utility::ExhaustionResult::CONTINUE;
                    }

                    // Check if RCX != window first. We don't want to skip over the call if it is set to it.
                    // There are inlined and non-inlined versions of this function which is why we need to check this.
                    if ((uint8_t*)ctx.ctx->ctx->Registers.RegRcx < window_bounds.data() || (uint8_t*)ctx.ctx->ctx->Registers.RegRcx > window_bounds.data() + window_bounds.size()) {
                        SPDLOG_INFO("[SlateRHIRenderer::DrawWindow_RenderThread] Skipping call!");
                        return utility::ExhaustionResult::STEP_OVER;
                    }

                    SPDLOG_INFO("[SlateRHIRenderer::DrawWindow_RenderThread] Allowing call, RCX matches window {:x}!", ctx.next.ix.Operands[0].Info.Register.Reg, window);
                    SPDLOG_INFO("[SlateRHIRenderer::DrawWindow_RenderThread] RCX: {:x}, RDX: {:x}", ctx.ctx->ctx->Registers.RegRcx, ctx.ctx->ctx->Registers.RegRdx);
                    ++window_getter_callstack_level;
                    return utility::ExhaustionResult::CONTINUE;
                }

                // Check if we hit a ret and are inside the window getter function.
                if (ctx.next.ix.Instruction == ND_INS_RETN) {
                    if (window_getter_callstack_level > 0) { 
                        SPDLOG_INFO("[SlateRHIRenderer::DrawWindow_RenderThread] Hit ret inside window getter function, continuing!");
                        --window_getter_callstack_level;
                        return utility::ExhaustionResult::CONTINUE;
                    }
                }

                // We're looking for a mov reg, [reg+offset] instruction
                // where reg contains the pointer to the window
                // and offset is the offset to the viewport.
                const auto& cctx = ctx.ctx->ctx;
                const auto& ix = cctx->Instruction;

                if (ix.Instruction == ND_INS_MOV && ix.Operands[0].Type == ND_OP_REG && ix.Operands[1].Type == ND_OP_MEM &&
                    ix.Operands[1].Info.Memory.HasBase && ix.Operands[1].Info.Memory.HasDisp)
                {
                    uintptr_t* reg = (uintptr_t*)&((uint64_t*)&cctx->Registers.RegRax)[ix.Operands[1].Info.Memory.Base];

                    // Instead of checking the window, we check if the register is within the bounds of the window's memory.
                    // This should allow us to catch all sorts of compiler optimizations.
                    if ((uint8_t*)*reg >= window_bounds.data() && (uint8_t*)*reg < window_bounds.data() + window_bounds.size()) try {
                        SPDLOG_INFO("[SlateRHIRenderer::DrawWindow_RenderThread] Found window pointer at {:x}!", (uintptr_t)reg);
                        auto offset = ix.Operands[1].Info.Memory.Disp;
                        const auto value = *(uintptr_t***)((uintptr_t)*reg + offset);

                        if (value == nullptr || IsBadReadPtr((void*)value, sizeof(void*))) {
                            SPDLOG_ERROR("[SlateRHIRenderer::DrawWindow_RenderThread] Skipping invalid offset at {:x}!", (uintptr_t)value);
                            return utility::ExhaustionResult::CONTINUE;
                        }

                        if (*value == nullptr || IsBadReadPtr((void*)*value, sizeof(void*))) {
                            SPDLOG_ERROR("[SlateRHIRenderer::DrawWindow_RenderThread] Skipping invalid vtable at {:x}!", (uintptr_t)*value);
                            return utility::ExhaustionResult::CONTINUE;
                        }

                        if (!utility::get_module_within(*value).has_value() || !utility::get_module_within((*value)[0]).has_value()) {
                            SPDLOG_ERROR("[SlateRHIRenderer::DrawWindow_RenderThread] Skipping invalid module at {:x}!", (uintptr_t)*value);
                            return utility::ExhaustionResult::CONTINUE;
                        }

                        const auto behind_value = *(uintptr_t***)((uintptr_t)*reg + offset - sizeof(void*));

                        if (behind_value != nullptr && !IsBadReadPtr((void*)behind_value, sizeof(void*)) &&
                            *behind_value != nullptr && !IsBadReadPtr((void*)*behind_value, sizeof(void*)) &&
                            utility::get_module_within(*behind_value).has_value() && utility::get_module_within((*behind_value)[0]).has_value())
                        {
                            SPDLOG_INFO("[SlateRHIRenderer::DrawWindow_RenderThread] Adjusting offset by sizeof(void*)!");
                            offset -= sizeof(void*);
                        }

                        result = (*reg + offset) - (uintptr_t)window;

                        SPDLOG_INFO("[SlateRHIRenderer::DrawWindow_RenderThread] Found viewport offset at {:x}!", *result);
                        return utility::ExhaustionResult::BREAK;
                    } catch (...) {
                        SPDLOG_ERROR("[SlateRHIRenderer::DrawWindow_RenderThread] Exception while checking offset!");
                    }
                }

                return utility::ExhaustionResult::CONTINUE;
            });

            if (!result) {
                SPDLOG_ERROR("[SlateRHIRenderer::DrawWindow_RenderThread] Failed to find viewport offset!");
            }

            return result;
        }();

        if (viewport_offset) {
            sdk::ISlateViewport* candidate{};

            if (safe_read_value((uintptr_t)window + *viewport_offset, candidate) && looks_like_vtable_object(candidate)) {
                slate_viewport = candidate;
            } else {
                SPDLOG_WARN_ONCE("[SlateRHIRenderer::DrawWindow_RenderThread] UE 5.5 Slate viewport candidate was not a valid vtable object");
            }
        }
    }

    const auto& mods = g_framework->get_mods()->get_mods();

    for (auto& mod : mods) {
        mod->on_pre_slate_draw_window(renderer, a2, viewport_info);
    }

    g_hook->m_inside_slate_draw_window = true;
    g_hook->m_slate_draw_window_thread_id = GetCurrentThreadId();

    auto call_orig = [&]() {
        auto ret = g_hook->m_slate_thread_hook.call<void*>(renderer, a2, a3, a4, params, unk1, unk2);

        for (auto& mod : mods) {
            mod->on_post_slate_draw_window(renderer, a2, viewport_info);
        }

        g_hook->m_inside_slate_draw_window = false;

        return ret;
    };


    auto vr = VR::get();

    if (!vr->is_hmd_active() || vr->is_stereo_emulation_enabled()) {
        return call_orig();
    }

    sdk::FSlateResource* slate_resource = nullptr;
    sdk::FSlateResource* provider_resource = nullptr;
    FRHITexture2D* provider_texture = nullptr;
    auto rtm = g_hook->get_render_target_manager();

    if (supports_ue55_dedicated_ui_target_for_current_game() && a4_is_ue_5_5_variant) {
        g_hook->note_stable_slate_draw();
        g_hook->attempt_hook_ue55_slate_output_texture_register();

        const auto expected_extent = a4_has_ue_5_5_full_inputs ? ue55_get_slate_expected_extent(ue55_inputs_full) : std::nullopt;

        if (expected_extent) {
            SPDLOG_INFO_EVERY_N_SEC(2,
                "[UE5.5][SlateUI] DrawWindow trusted Slate extent [{}x{}] scene_rect min=[{},{}] max=[{},{}] scale={:.3f}",
                expected_extent->width,
                expected_extent->height,
                ue55_inputs_full.scene_view_rect.min.x,
                ue55_inputs_full.scene_view_rect.min.y,
                ue55_inputs_full.scene_view_rect.max.x,
                ue55_inputs_full.scene_view_rect.max.y,
                ue55_inputs_full.viewport_scale_ui);

            rtm->request_dedicated_ui_target(expected_extent->width, expected_extent->height);
            rtm->ensure_dedicated_ui_target((uintptr_t)a2);
        } else {
            SPDLOG_INFO_EVERY_N_SEC(2, "[UE5.5][SlateUI] No trusted Slate extent yet; dedicated UI creation is deferred");
        }

        if (slate_viewport != nullptr) {
            bool use_separate{};
            bool stereo{};
            const auto have_use_separate = try_call_slate_viewport_bool_slot(slate_viewport, 7, use_separate);
            const auto have_stereo = try_call_slate_viewport_bool_slot(slate_viewport, 6, stereo);

            SPDLOG_INFO_EVERY_N_SEC(2,
                "[UE5.5][SlateUI] ISlateViewport={:x} UseSeparate={}{} IsStereoscopic3D={}{}",
                (uintptr_t)slate_viewport,
                have_use_separate ? "" : "unreadable/",
                have_use_separate ? use_separate : false,
                have_stereo ? "" : "unreadable/",
                have_stereo ? stereo : false);

            if (have_use_separate && use_separate) {
                bool slate_viewport_faulted = false;
                auto* direct_resource = try_get_slate_viewport_render_target_texture(slate_viewport, slate_viewport_faulted);
                auto* direct_texture = direct_resource != nullptr ? direct_resource->get_mutable_resource() : nullptr;

                if (slate_viewport_faulted) {
                    SPDLOG_WARN_ONCE("[UE5.5][SlateUI] GetViewportRenderTargetTexture faulted; relying on DrawWindow outputs and dedicated target");
                } else if (ue55_is_valid_ui_texture_candidate(rtm, direct_texture, expected_extent, "ISlateViewport direct texture")) {
                    rtm->set_dedicated_ui_target(direct_texture, expected_extent->width, expected_extent->height);
                    rtm->get_fallback_ui_target_ref() = nullptr;
                    if (mechwarrior_clans_is_current_game()) {
                        rtm->cancel_dedicated_ui_creation_preserving_target("MechWarrior ISlateViewport direct texture");
                    }
                    SPDLOG_WARN_ONCE("[UE5.5][SlateUI] promoted ISlateViewport direct texture as dedicated UI target");
                }
            }
        }

        const auto ret = call_orig();
        ue55_promote_slate_outputs(rtm, a2, expected_extent);
        return ret;
    }

    if (slate_viewport != nullptr) {
        bool slate_viewport_faulted = false;
        slate_resource = try_get_slate_viewport_render_target_texture(slate_viewport, slate_viewport_faulted);

        if (slate_viewport_faulted) {
            SPDLOG_WARN_ONCE("[SlateRHIRenderer::DrawWindow_RenderThread] UE 5.5 Slate viewport resource call faulted; deferring to fallback path");
        }
    }

    bool skip_ue56_viewport_provider = false;

    if (viewport_info != nullptr && !a4_is_ue_5_5_variant && !is_ue_5_7_or_newer()) {
        const auto known_texture = rtm->get_render_target() != nullptr ? rtm->get_render_target() : (slate_resource != nullptr ? slate_resource->get_mutable_resource() : nullptr);
        const auto known_texture_is_safe =
            known_texture == nullptr ||
            !is_ue_5_6_dx12_backend() ||
            ue56_dx12_try_get_native_resource(known_texture, "FViewportInfo known texture");

        if (!known_texture_is_safe) {
            skip_ue56_viewport_provider = true;
            SPDLOG_WARNING_EVERY_N_SEC(2,
                "[UE5.6][RT] Skipping FViewportInfo::GetRenderTargetProvider probing because the known texture cannot expose a stable native resource; deferring to D3D12 hooks");
        }

        const auto viewport_rt_provider = known_texture != nullptr && known_texture_is_safe ? viewport_info->get_rt_provider(known_texture) : nullptr;

        if (viewport_rt_provider != nullptr) {
            provider_resource = viewport_rt_provider->get_viewport_render_target_texture();

            if (provider_resource != nullptr) {
                provider_texture = provider_resource->get_mutable_resource();
            }
        } else if (slate_viewport == nullptr && rtm->get_render_target() == nullptr) {
            SPDLOG_INFO_EVERY_N_SEC(1, "No viewport RT provider, skipping!");
            return call_orig();
        }
    } else if (viewport_info != nullptr && a4_is_ue_5_5_variant) {
        SPDLOG_INFO_ONCE("[SlateRHIRenderer::DrawWindow_RenderThread] Skipping FViewportInfo RT provider probing on UE 5.5 direct Slate inputs");
    } else if (viewport_info != nullptr && is_ue_5_7_or_newer()) {
        SPDLOG_WARN_ONCE("[SlateRHIRenderer::DrawWindow_RenderThread] Skipping FViewportInfo RT provider probing on UE 5.7+ for stability");
    }

    if (slate_resource == nullptr) {
        slate_resource = provider_resource;
    }

    if (slate_resource == nullptr) {
        SPDLOG_INFO_EVERY_N_SEC(1, "No slate resource, skipping!");
        return call_orig();
    }

    const auto engine_texture = slate_resource->get_mutable_resource();
    bool engine_texture_native_ok = true;
    bool provider_texture_native_ok = true;
    const auto vr_state = VR::get();
    const bool ue56_d3d12_targets_ready =
        is_ue_5_6_dx12_backend() &&
        vr_state != nullptr &&
        vr_state->has_d3d12_game_ui_textures() &&
        rtm->get_render_target() != nullptr &&
        rtm->get_ui_target() != nullptr;

    if (engine_texture != nullptr && !IsBadReadPtr(engine_texture, sizeof(void*))) {
        engine_texture_native_ok =
            !is_ue_5_6_dx12_backend() ||
            ue56_dx12_try_get_native_resource(engine_texture, "Slate viewport texture");

        if (engine_texture_native_ok) {
            FRHITexture2D::set_vtable(*(void**)engine_texture);
            g_hook->note_stable_slate_draw();
        } else if (!ue56_d3d12_targets_ready) {
            SPDLOG_WARNING_EVERY_N_SEC(2,
                "[UE5.6][RT] Not adopting Slate viewport texture because native-resource discovery failed; waiting for D3D12 texture/backbuffer hooks");
        }

        if (engine_texture_native_ok && rtm->get_render_target() == nullptr) {
            SPDLOG_WARN_ONCE("[SlateRHIRenderer::DrawWindow_RenderThread] Adopting Slate viewport texture as render target fallback");
            rtm->set_render_target(engine_texture);
        }
    }

    if (provider_texture != nullptr && !IsBadReadPtr(provider_texture, sizeof(void*))) {
        provider_texture_native_ok =
            !is_ue_5_6_dx12_backend() ||
            ue56_dx12_try_get_native_resource(provider_texture, "Viewport RT provider texture");
    }

    if (is_ue_5_7_or_newer()) {
        rtm->ensure_dedicated_ui_target((uintptr_t)a2);

        if (!rtm->has_dedicated_ui_target() && !g_hook->m_hooked_ue57_slate_elements_pass) {
            const auto now = std::chrono::steady_clock::now();

            if (g_hook->m_ue57_dedicated_ui_missing_since.time_since_epoch().count() == 0) {
                g_hook->m_ue57_dedicated_ui_missing_since = now;
                g_hook->m_ue57_dedicated_ui_missing_frames = 0;
            }

            ++g_hook->m_ue57_dedicated_ui_missing_frames;

            if (g_hook->m_ue57_dedicated_ui_missing_frames > 180 &&
                now - g_hook->m_ue57_dedicated_ui_missing_since > std::chrono::seconds(3))
            {
                g_hook->attempt_hook_ue57_slate_elements_pass();
            }
        } else if (rtm->has_dedicated_ui_target()) {
            g_hook->m_ue57_dedicated_ui_missing_since = {};
            g_hook->m_ue57_dedicated_ui_missing_frames = 0;
        }
    }

    auto ui_target = rtm->get_ui_target();
    const auto render_target_fallback = rtm->get_render_target();

    if (is_ue_5_7_or_newer()) {
        if (rtm->has_dedicated_ui_target()) {
            SPDLOG_WARN_ONCE("[SlateRHIRenderer::DrawWindow_RenderThread] Using explicit dedicated UI target on UE 5.7+");
        }

        auto& fallback_ui_target = rtm->get_fallback_ui_target_ref();

        if (fallback_ui_target == render_target_fallback) {
            fallback_ui_target = nullptr;
        }

        ui_target = rtm->get_ui_target();
    } else {
        if (ui_target == nullptr && provider_texture_native_ok && provider_texture != nullptr && !IsBadReadPtr(provider_texture, sizeof(void*)) && provider_texture != render_target_fallback) {
            SPDLOG_WARN_ONCE("[SlateRHIRenderer::DrawWindow_RenderThread] Adopting viewport RT provider texture as dedicated UI target fallback");
            ui_target = provider_texture;
            rtm->get_fallback_ui_target_ref() = provider_texture;
        }

        if (ui_target == nullptr && engine_texture_native_ok && engine_texture != nullptr && !IsBadReadPtr(engine_texture, sizeof(void*)) && engine_texture != render_target_fallback) {
            SPDLOG_WARN_ONCE("[SlateRHIRenderer::DrawWindow_RenderThread] Adopting Slate viewport texture as dedicated UI target fallback");
            ui_target = engine_texture;
            rtm->get_fallback_ui_target_ref() = engine_texture;
        }

        if (ui_target == nullptr && render_target_fallback != nullptr && !skip_ue56_viewport_provider) {
            SPDLOG_WARN_ONCE("[SlateRHIRenderer::DrawWindow_RenderThread] Falling back to render target because no dedicated UI target was recovered");
            ui_target = render_target_fallback;
            rtm->get_fallback_ui_target_ref() = render_target_fallback;
        }
    }

    if (ui_target == nullptr) {
        if (is_ue_5_7_or_newer()) {
            SPDLOG_INFO_EVERY_N_SEC(1, "[SlateRHIRenderer::DrawWindow_RenderThread] No dedicated UI target yet");
            return call_orig();
        }

        SPDLOG_INFO_EVERY_N_SEC(1, "No UI target, skipping!");
        return call_orig();
    }

    // Replace the texture with one we have control over.
    // This isolates the UI to render on our own texture separate from the scene.
    const auto old_texture = slate_resource->get_mutable_resource();
    slate_resource->get_mutable_resource() = ui_target;

    // To be seen if we need to resort to a MidHook on this function if the parameters
    // are wildly different between UE versions.
    const auto ret = g_hook->m_slate_thread_hook.call<void*>(renderer, a2, a3, a4, params, unk1, unk2);

    // Restore the old texture.
    slate_resource->get_mutable_resource() = old_texture;

    for (auto& mod : mods) {
        mod->on_post_slate_draw_window(renderer, a2, viewport_info);
    }
    
    // After this we copy over the texture and clear it in the present hook. doing it here just seems to crash sometimes.
    SPDLOG_INFO_ONCE("SlateRHIRenderer::DrawWindow_RenderThread finished!");

    return ret;
}

// INTERNAL USE ONLY!!!!
__declspec(noinline) void VRRenderTargetManager::CalculateRenderTargetSize(const sdk::FViewport& Viewport, uint32_t& InOutSizeX, uint32_t& InOutSizeY) {
    SPDLOG_INFO_ONCE("VRRenderTargetManager::CalculateRenderTargetSize called!");

    m_last_calculate_render_size_return_address = (uintptr_t)_ReturnAddress();

    VRRenderTargetManager_Base::calculate_render_target_size(Viewport, InOutSizeX, InOutSizeY);
}

__declspec(noinline) bool VRRenderTargetManager::NeedReAllocateDepthTexture(const void* DepthTarget) {
    SPDLOG_INFO_ONCE("VRRenderTargetManager::NeedReAllocateDepthTexture called!");

    m_last_needs_reallocate_depth_texture_return_address = (uintptr_t)_ReturnAddress();

    if (this->depth_analysis_passed) {
        return VRRenderTargetManager_Base::need_reallocate_depth_texture(DepthTarget);
    }

    return false;
}

__declspec(noinline) bool VRRenderTargetManager::NeedReAllocateShadingRateTexture(const void* ShadingRateTarget) {
    SPDLOG_INFO_ONCE("VRRenderTargetManager::NeedReAllocateShadingRateTexture called!");

    const auto return_address = (uintptr_t)_ReturnAddress();
    const auto diff = return_address - m_last_calculate_render_size_return_address;

    if (diff <= 0x50) {
        // We need to switch the FFakeStereoRenderingHook's render target manager
        // to the old one NOW or we will crash. Reason being what was actually called
        // is the GetNumberOfBufferedFrames function, not NeedReAllocateShadingRateTexture.
        SPDLOG_INFO("Switching to old render target manager! Incorrect function called!");
        //g_hook->switch_to_old_rendertarget_manager();

        // Do a switcharoo on the vtable of this object to the old one because we will crash if we don't.
        // I've decided against actually switching the entire object over in favor of just vtable
        // swapping for now even though it's kind of a hack.
        const auto fake_object = std::make_unique<VRRenderTargetManager_418>();
        *(void**)this = *(void**)fake_object.get();

        return true; // The return value should actually be 1, so just return true.
    }

    return false;
}

void VRRenderTargetManager_Base::update_viewport(bool use_separate_rt, const sdk::FViewport& vp, class SViewport* vp_widget) {
    SPDLOG_INFO_ONCE("VRRenderTargetManager_Base::update_viewport called! {} {:x} {:x}", use_separate_rt, (uintptr_t)&vp, (uintptr_t)vp_widget);

    if (!g_framework->is_game_data_intialized()) {
        return;
    }

    shf_force_scene_viewport_separate_rt(vp, "RenderTargetManager::UpdateViewport");

    //SPDLOG_INFO("Widget: {:x}", (uintptr_t)ViewportWidget);
}

void VRRenderTargetManager_Base::calculate_render_target_size(const sdk::FViewport& viewport, uint32_t& x, uint32_t& y) {
    SPDLOG_INFO_ONCE("VRRenderTargetManager_Base::calculate_render_target_size called!");

#ifdef FFAKE_STEREO_RENDERING_LOG_ALL_CALLS
    SPDLOG_INFO("calculate render target size called!");
#endif

    if (!g_framework->is_game_data_intialized()) {
        return;
    }

    if (is_ue_5_7_or_newer() && x > 0 && y > 0) {
        // UE 5.7 still reports the pre-VR Slate/window size here before we overwrite it
        // with the stereo render target size. Keep it so the UI path can stay full-width.
        this->request_dedicated_ui_target(x, y);
    }

    x = VR::get()->get_hmd_width() * 2;
    y = VR::get()->get_hmd_height();

    SPDLOG_DEBUG("RenderTargetSize After: {}x{}", x, y);
}

bool VRRenderTargetManager_Base::need_reallocate_view_target(const sdk::FViewport& Viewport) {
    SPDLOG_INFO_ONCE("VRRenderTargetManager_Base::need_reallocate_view_target called!");

    if (!g_framework->is_game_data_intialized()) {
        return false;
    }

    if (!m_attempted_find_force_separate_rt) try {
        m_attempted_find_force_separate_rt = true;

        // Go up the stack until we find something that isn't in our module.
        const auto our_module = g_framework->get_framework_module();
        constexpr auto max_stack_depth = 100;
        uintptr_t stack[max_stack_depth]{};

        const auto depth = RtlCaptureStackBackTrace(0, max_stack_depth, (void**)&stack, nullptr);

        std::optional<uintptr_t> ret_addr{};
        std::optional<HMODULE> module_within{};

        for (auto i = 0; i < depth; ++i) {
            SPDLOG_INFO("Stack[{}]: {:x}", i, stack[i]);

            module_within = utility::get_module_within(stack[i]);

            if (!module_within) {
                continue;
            }

            if (*module_within != our_module) {
                ret_addr = stack[i];
                break;
            }
        }

        // Emulate from the return address and find a memory write
        // this should contain the offset to the force separate rt bool.
        if (ret_addr) {
            SPDLOG_INFO("Found return address: {:x}", *ret_addr);

            utility::ShemuContext ctx{*module_within};
            ctx.ctx->Registers.RegRip = *ret_addr;
            ctx.ctx->Registers.RegRax = 1; // As if we're returning true from this function.

            utility::emulate(*module_within, *ret_addr, 100, ctx, [this](const utility::ShemuContextExtended& ctx) -> utility::ExhaustionResult {
                SPDLOG_INFO("Emulating instruction: {:x}", ctx.ctx->ctx->Registers.RegRip);

                if (ctx.next.writes_to_memory) {
                    const auto& ix = ctx.next.ix;
                    if (ix.Instruction == ND_INS_MOV && ix.Operands[0].Type == ND_OP_MEM && ix.Operands[1].Type == ND_OP_REG) {
                        // We're looking for a mov [reg1+N], reg2
                        const auto& op0 = ix.Operands[0];

                        // Needs a register
                        if (!op0.Info.Memory.HasBase || op0.Info.Memory.IsRipRel) {
                            return utility::ExhaustionResult::STEP_OVER;
                        }

                        // Needs a displacement
                        if (!op0.Info.Memory.HasDisp) {
                            return utility::ExhaustionResult::STEP_OVER;
                        }

                        // We don't want a stack based register
                        if (op0.Info.Memory.Base == NDR_RSP || op0.Info.Memory.Base == NDR_RBP) {
                            return utility::ExhaustionResult::STEP_OVER;
                        }

                        if (op0.Info.Memory.Disp > 0 && op0.Info.Memory.Disp < 0x2000) {
                            m_viewport_force_separate_rt_offset = op0.Info.Memory.Disp;
                            SPDLOG_INFO("Found force separate rt offset: {:x}", *m_viewport_force_separate_rt_offset);
                            return utility::ExhaustionResult::BREAK;
                        }
                    }

                    SPDLOG_INFO("Stepping over...");

                    return utility::ExhaustionResult::STEP_OVER;
                }

                if (std::string_view{ctx.next.ix.Mnemonic}.starts_with("CALL")) {
                    // We need to break out of this, we should've found the offset before the call.
                    SPDLOG_ERROR("Failed to find force separate rt offset! Encountered call at {:x}", ctx.ctx->ctx->Registers.RegRip);
                    return utility::ExhaustionResult::BREAK;
                }

                return utility::ExhaustionResult::CONTINUE;
            });
        }
    } catch(...) { // if we dont find it, it's fine, not very many games require it.
        SPDLOG_ERROR("Failed to find force separate rt offset! (Exception)");
    }

    const auto w = VR::get()->get_hmd_width();
    const auto h = VR::get()->get_hmd_height();

    if (w != this->last_width || h != this->last_height || g_hook->should_recreate_textures()) {
        SPDLOG_INFO("Reallocating view target! {} {} -> {} {}", this->last_width, this->last_height, w, h);

        this->last_width = w;
        this->last_height = h;
        this->wants_depth_reallocate = true;
        this->destroy_scene_capture();
        g_hook->set_should_recreate_textures(false);
        return true;
    }

    return false;
}

bool VRRenderTargetManager_Base::need_reallocate_depth_texture(const void* DepthTarget) {
    SPDLOG_INFO_ONCE("VRRenderTargetManager_Base::need_reallocate_depth_texture called!");

    if (!g_framework->is_game_data_intialized()) {
        return false;
    }

    if (this->wants_depth_reallocate) {
        SPDLOG_INFO("Reallocating depth texture!");

        this->wants_depth_reallocate = false;
        return true;
    }

    return false;
}

void VRRenderTargetManager_Base::pre_texture_hook_callback(safetyhook::Context& ctx, bool from_second) {
    if (g_framework->is_dx12() && shf_is_current_game()) {
        SPDLOG_INFO_EVERY_N_SEC(2, "[SHf] PreTextureHook summary last_desc={:x}", ctx.r8);
    } else if (is_ue_5_1_dx12_backend()) {
        SPDLOG_INFO_EVERY_N_SEC(2, "[UE5.1][RTChurn] PreTextureHook summary last_desc={:x}", ctx.r8);
    } else {
        SPDLOG_INFO("PreTextureHook called! {}", ctx.r8);
    }

    auto rtm = g_hook->get_render_target_manager();

    if (g_framework->is_dx12() && is_ue_5_7_or_newer()) {
        if (rtm->texture_desc_prepare_func == 0 || rtm->texture_create_wrapper_func == 0 || rtm->texture_finalize_func == 0) {
            SPDLOG_WARN_ONCE("Skipping pre-texture duplication on UE 5.7+ DX12 because the real texture wrapper/finalize sequence was not resolved");
            return;
        }

        if (!rtm->allocate_texture_called) {
            SPDLOG_ERROR("AllocateTexture not called yet! (UE 5.7 PreTextureHook)");
            return;
        }

        if (ctx.r8 == 0 || IsBadReadPtr((void*)ctx.r8, sizeof(void*))) {
            SPDLOG_WARN_ONCE("Skipping UE 5.7 pre-texture duplication because the texture desc pointer is invalid");
            return;
        }

        using PrepareDescFn = void(*)(void*, const void*);
        using CreateTextureWrapperFn = void*(*)(void*, void*, const void*);
        using FinalizeTextureFn = void(*)(void*, FTexture2DRHIRef*);

        alignas(16) std::array<uint8_t, 0x80> copied_desc{};
        alignas(16) std::array<uint8_t, 0x90> texture_initializer{};
        static FTexture2DRHIRef duplicated_ui_texture{};

        ((PrepareDescFn)rtm->texture_desc_prepare_func)(copied_desc.data(), (const void*)ctx.r8);

        const auto scan_x = VR::get()->get_hmd_width() * 2;
        const auto scan_y = VR::get()->get_hmd_height();
        const auto requested_width = rtm->get_dedicated_ui_width() != 0 ? rtm->get_dedicated_ui_width() : (uint32_t)g_framework->get_d3d12_rt_size().x;
        const auto requested_height = rtm->get_dedicated_ui_height() != 0 ? rtm->get_dedicated_ui_height() : (uint32_t)g_framework->get_d3d12_rt_size().y;

        bool patched_desc = false;

        for (auto i = 0; i < 0x60; ++i) {
            auto& x = *(int32_t*)(copied_desc.data() + i);
            auto& y = *(int32_t*)(copied_desc.data() + i + 4);

            if (x == (int32_t)scan_x && y == (int32_t)scan_y) {
                SPDLOG_INFO("UE 5.7: Found scene render target extent at desc offset 0x{:x}; duplicating UI extent as [{}x{}]", i, requested_width, requested_height);
                x = (int32_t)requested_width;
                y = (int32_t)requested_height;

                auto* format = copied_desc.data() + i + 15;
                if (*format == 18) {
                    *format = 2;
                }

                patched_desc = true;
                break;
            }
        }

        if (!patched_desc) {
            SPDLOG_WARN_ONCE("Skipping UE 5.7 pre-texture duplication because the texture desc width/height pair could not be found");
            return;
        }

        duplicated_ui_texture.texture = nullptr;

        ((CreateTextureWrapperFn)rtm->texture_create_wrapper_func)((void*)ctx.rcx, texture_initializer.data(), copied_desc.data());
        ((FinalizeTextureFn)rtm->texture_finalize_func)(texture_initializer.data(), &duplicated_ui_texture);

        if (duplicated_ui_texture.texture == nullptr || IsBadReadPtr(duplicated_ui_texture.texture, sizeof(void*))) {
            SPDLOG_WARN_ONCE("UE 5.7 UI duplication ran but did not produce a texture");
            return;
        }

        FRHITexture2D::set_vtable(*(void**)duplicated_ui_texture.texture);
        rtm->set_dedicated_ui_target(duplicated_ui_texture.texture, requested_width, requested_height);
        rtm->get_fallback_ui_target_ref() = nullptr;

        SPDLOG_WARN_ONCE("UE 5.7 created a dedicated UI texture through the real texture wrapper path");
        SPDLOG_INFO("UE 5.7 dedicated UI texture: {:x} [{}x{}]", (uintptr_t)duplicated_ui_texture.texture, requested_width, requested_height);

        VR::get()->reinitialize_renderer();
        return;
    }

    if (is_ue_5_1_dx12_backend() && rtm->allocate_texture_called) {
        const auto size = g_framework->get_d3d12_rt_size();

        if (ue51_can_reuse_current_ui_target(rtm, (uint32_t)size.x, (uint32_t)size.y)) {
            // The engine allocation still continues after this pre-hook. Reusing the
            // existing duplicate UI target avoids rebuilding UEVR's UI texture every
            // frame when UE 5.1 repeatedly hits the same RT allocation path.
            if (rtm->is_using_texture_desc && rtm->is_version_greq_5_1) {
                if (!rtm->is_pre_texture_call_e8 &&
                    ctx.rdx != 0 && !IsBadReadPtr((void*)ctx.rdx, sizeof(FTexture2DRHIRef)))
                {
                    rtm->texture_hook_ref = (FTexture2DRHIRef*)ctx.rdx;
                } else if (rtm->is_pre_texture_call_e8 &&
                    ctx.rcx != 0 && !IsBadReadPtr((void*)ctx.rcx, sizeof(FTexture2DRHIRef)))
                {
                    rtm->texture_hook_ref = (FTexture2DRHIRef*)ctx.rcx;
                }
            }

            SPDLOG_INFO_EVERY_N_SEC(2, "[UE5.1][RTChurn] Reusing stable UI texture; skipping duplicate UI texture creation");
            return;
        }
    }

    if (g_framework->is_dx12() && shf_is_current_game() && rtm->allocate_texture_called) {
        const auto size = g_framework->get_d3d12_rt_size();

        if (shf_can_reuse_current_ui_target(rtm, (uint32_t)size.x, (uint32_t)size.y)) {
            if (rtm->is_using_texture_desc && rtm->is_version_greq_5_1 && !rtm->is_pre_texture_call_e8 &&
                ctx.rdx != 0 && !IsBadReadPtr((void*)ctx.rdx, sizeof(FTexture2DRHIRef)))
            {
                rtm->texture_hook_ref = (FTexture2DRHIRef*)ctx.rdx;
                SPDLOG_INFO_EVERY_N_SEC(2, "[SHf] Reusing stable UI texture; tracking current UE5 texture-desc output ref {:x}", ctx.rdx);
            }

            return;
        }
    }

    // maybe do some work later to bruteforce the registers/offsets for these
    // a la emulation or something more rudimentary
    // since it always seems to access a global right before, which
    // refers to the current pixel format, which we can overwrite (which may not be safe)
    // so we could just follow how the global is being written to registers or the stack
    // and then just overwrite the registers/stack with our own values
    if (!rtm->allocate_texture_called) {
        SPDLOG_ERROR("AllocateTexture not called yet! (PreTextureHook)");
        return;
    }

    if (!g_hook->has_pixel_format_cvar()) {
        if (g_hook->get_render_target_manager()->is_pre_texture_call_e8) {
            //ctx.r8 = 2; // PF_B8G8R8A8 // decided not to actually set it here, we need to double check when it's actually called
        } else if (!rtm->is_using_texture_desc) {
            *((uint8_t*)ctx.rsp + 0x28) = 2; // PF_B8G8R8A8
        }
    }

    // Now we are going to attempt to JIT a function that will call the original function
    // using the context we have. This will call it twice, but allow us to
    // have control over one of the textures it generates. We need
    // the other generated texture as a UI render target to be used in FFakeStereoRenderingHook::slate_draw_window_render_thread.
    // This will allow the original game UI to be rendered in world space without resorting to WidgetComponent.
    // One can argue that this may be an overengineered alternative to "just" calling FDynamicRHI::CreateTexture2D
    // but that function is very hard to pattern scan for, and we already have it here, so why not use it?
    using namespace asmjit;
    using namespace asmjit::x86;

    SPDLOG_INFO("Attempting to JIT a function to call the original function!");

    auto& insn_bytes = !from_second ? rtm->texture_create_insn_bytes : rtm->texture_create_insn_bytes2;

    const auto ix = utility::decode_one(insn_bytes.data(), insn_bytes.size());

    if (!ix) {
        SPDLOG_ERROR("Failed to decode instruction!");
        return;
    }
    
    // We can't do it to the normal E8 call because the code is not in the same area
    // so RIP relative calls are not possible through the emulator. will just have to
    // resolve those manually through disassembly.
    uintptr_t func_ptr = 0;

    if (!g_hook->get_render_target_manager()->is_pre_texture_call_e8) {
        // Set up the emulator. We will use it to emulate the function call.
        // All we need from it is where the function call lands, so we can call it for real.
        auto emu_ctx = utility::ShemuContext(
            (uintptr_t)insn_bytes.data(),
            insn_bytes.size());

        SPDLOG_INFO("Insn bytes size: {}", insn_bytes.size());
        for (size_t i = 0; i < insn_bytes.size(); ++i) {
            SPDLOG_INFO("Byte[{}]: {:x}", i, insn_bytes[i]);
        }

        emu_ctx.ctx->Registers.RegRcx = ctx.rcx;
        emu_ctx.ctx->Registers.RegRdx = ctx.rdx;
        emu_ctx.ctx->Registers.RegR8 = ctx.r8;
        emu_ctx.ctx->Registers.RegR9 = ctx.r9;
        emu_ctx.ctx->Registers.RegRbx = ctx.rbx;
        emu_ctx.ctx->Registers.RegRax = ctx.rax;
        emu_ctx.ctx->Registers.RegRdi = ctx.rdi;
        emu_ctx.ctx->Registers.RegRsi = ctx.rsi;
        emu_ctx.ctx->Registers.RegR10 = ctx.r10;
        emu_ctx.ctx->Registers.RegR11 = ctx.r11;
        emu_ctx.ctx->Registers.RegR12 = ctx.r12;
        emu_ctx.ctx->Registers.RegR13 = ctx.r13;
        emu_ctx.ctx->Registers.RegR14 = ctx.r14;
        emu_ctx.ctx->Registers.RegR15 = ctx.r15;

        // if disasm is call [rsp+N] we need to set RSP to the actual stack
        // otherwise emulation will fail.
        // conversely, if we set RSP when it's NOT using RSP in the register
        // it will also fail.
        if (ix->Operands[0].Type == ND_OP_MEM && ix->Operands[0].Info.Memory.HasBase &&
            ix->Operands[0].Info.Memory.Base == NDR_RSP)
        {
            emu_ctx.ctx->Registers.RegRsp = ctx.rsp;
            emu_ctx.ctx->Stack = (ND_UINT8*)ctx.rsp;
            emu_ctx.ctx->StackBase = ctx.rsp;
            SPDLOG_INFO("Setting RSP to {:x} for emulation!", ctx.rsp);
        } else {
            SPDLOG_INFO("Not setting RSP for emulation!");
        }

        emu_ctx.ctx->MemThreshold = 1;

        if (emu_ctx.emulate((uintptr_t)insn_bytes.data(), 1) != SHEMU_SUCCESS) {
            SPDLOG_ERROR("Failed to emulate instruction!: {} RIP: {:x}", emu_ctx.status, emu_ctx.ctx->Registers.RegRip);
            return;
        }
    
        SPDLOG_INFO("Emu landed at {:x}", emu_ctx.ctx->Registers.RegRip);
        func_ptr = emu_ctx.ctx->Registers.RegRip;

        if (func_ptr == 0) {
            SPDLOG_ERROR("Function pointer is null after emulation!");
            return;
        }
    } else {
        const auto target = g_hook->get_render_target_manager()->pre_texture_hook.target_address();
        func_ptr = target + 5 + *(int32_t*)&insn_bytes.data()[1];
    }

    SPDLOG_INFO("Function pointer: {:x}", func_ptr);

    /*CodeHolder code{};
    JitRuntime runtime{};
    code.init(runtime.environment());

    Assembler a{&code};
    
    static auto cloned_stack = std::make_unique<std::array<uint8_t, 0x3000>>();
    static auto cloned_registers = std::make_unique<std::array<uint8_t, 0x1000>>();

    auto aligned_stack = ((uintptr_t)&(*cloned_stack)[0x2000]);
    aligned_stack += (-(intptr_t)aligned_stack) & (40 - 1);

    memcpy((void*)aligned_stack, (void*)(ctx.rsp), 0x1000);

    static auto stack_ptr = std::make_unique<uintptr_t>();
    static auto post_register_storage = std::make_unique<uintptr_t>();

    // Store the original stack pointer.
    a.movabs(rax, (void*)stack_ptr.get());
    a.mov(ptr(rax), rsp);

    // Push all of the original registers onto the stack.
    a.movabs(rsp, (void*)&(*cloned_registers)[0x500]);
    //a.mov(rsp, rax);

    a.push(rcx);
    a.push(rdx);
    a.push(r8);
    a.push(r9);
    a.push(r10);
    a.push(r11);
    a.push(r12);
    a.push(r13);
    a.push(r14);
    a.push(r15);
    a.push(rbx);
    a.push(rbp);
    a.push(rsi);
    a.push(rdi);
    a.pushfq();

    a.mov(rax, (void*)post_register_storage.get());
    a.mov(ptr(rax), rsp);

    a.movabs(rsp, aligned_stack);


    a.mov(rdx, rcx); // func param
    a.movabs(rcx, ctx.rcx);
    //a.movabs(rdx, ctx.rdx);
    a.movabs(r8, ctx.r8);
    //a.movabs(r9, ctx.r9);
    const auto size = g_framework->is_dx11() ? g_framework->get_d3d11_rt_size() : g_framework->get_d3d12_rt_size();
    a.mov(r9, (uint32_t)size.x);
    // move w into first stack argument
    a.mov(dword_ptr(rsp, 0x20), (uint32_t)size.y);
    a.movabs(r10, ctx.r10);
    a.movabs(r11, ctx.r11);
    a.movabs(r12, ctx.r12);
    a.movabs(r13, ctx.r13);
    a.movabs(r14, ctx.r14);
    a.movabs(r15, ctx.r15);
    a.movabs(rax, ctx.rax);
    a.movabs(rbx, ctx.rbx);
    a.movabs(rbp, ctx.rbp);
    a.movabs(rsi, ctx.rsi);
    a.movabs(rdi, ctx.rdi);

    // Correct the stack pointers inside the stack we cloned
    // to point to areas within the cloned stack if they were
    // pointing to the original stack.
    for (auto stack_var = 0; stack_var < 0x1000; stack_var += sizeof(void*)) {
        auto stack_var_ptr = (uintptr_t*)(aligned_stack + stack_var);

        if (*stack_var_ptr >= ctx.rsp && *stack_var_ptr < ctx.rsp + 0x1000) {
            SPDLOG_INFO("Correcting stack var at 0x{:x}", stack_var);
            *stack_var_ptr = aligned_stack + (*stack_var_ptr - ctx.rsp);
        }
    }

    auto correct_register = [&](auto& reg) {
        if (reg >= ctx.rsp && reg < ctx.rsp + 0x1000) {
            SPDLOG_INFO("Correcting Register");
            reg = aligned_stack + (reg - ctx.rsp);
        }

    };
    for (auto insn_byte : g_hook->get_render_target_manager()->texture_create_insn_bytes) {
        a.db(insn_byte);
    }

    a.mov(rsp, post_register_storage.get());
    a.mov(rsp, ptr(rsp));
    //a.mov(rsp, rcx);

    // Pop all of the original registers off of the stack.
    a.popfq();
    a.pop(rdi);
    a.pop(rsi);
    a.pop(rbp);
    a.pop(rbx);
    a.pop(r15);
    a.pop(r14);
    a.pop(r13);
    a.pop(r12);
    a.pop(r11);
    a.pop(r10);
    a.pop(r9);
    a.pop(r8);
    a.pop(rdx);
    a.pop(rcx);

    //a.pop(rsp); // Restore the original stack pointer.
    a.movabs(rsp, (void*)stack_ptr.get());
    a.mov(rsp, ptr(rsp));

    a.ret();

    uintptr_t code_addr{};
    runtime.add(&code_addr, &code);

    SPDLOG_INFO("JITed address: {:x}", code_addr);

    //MessageBox(0, "debug now", "debug", 0);

    void (*func)(void* rdx) = (decltype(func))code_addr;

    static FTexture2DRHIRef out{};
    out.texture = nullptr;
    func(&out);*/

    auto call_with_context = [&](uintptr_t func, FTexture2DRHIRef& out) {
        CodeHolder code{};
        JitRuntime runtime{};
        code.init(runtime.environment());

        Assembler a{&code};

        auto post_align_label = a.newLabel();

        a.push(rbx);

        a.mov(rcx, ctx.rcx);
        
        if (!g_hook->get_render_target_manager()->is_pre_texture_call_e8) {
            a.movabs(rdx, (uintptr_t)&out);
        } else {
            a.mov(rdx, ctx.rdx);
        }

        a.mov(r8, ctx.r8);

        const auto size = g_framework->is_dx11() ? g_framework->get_d3d11_rt_size() : g_framework->get_d3d12_rt_size();
        a.mov(r9, (uint32_t)size.x);

        a.sub(rsp, 0x100);
        a.mov(rbx, 0x100);
        a.test(rsp, sizeof(void*));
        a.jz(post_align_label);

        a.sub(rsp, 8);
        a.mov(rbx, 0x108);
        a.bind(post_align_label);

        a.mov(ptr(rsp, 0x20), (uint32_t)size.y);

        for (auto i = 0x28; i < 0x90; i += sizeof(void*)) {
            a.mov(rax, *(uintptr_t*)(ctx.rsp + i));
            a.mov(ptr(rsp, i), rax);
        }

        a.mov(rax, (void*)func);
        a.call(rax);

        a.add(rsp, rbx);
        a.pop(rbx);

        a.ret();

        uintptr_t code_addr{};
        runtime.add(&code_addr, &code);
        void (*jitted_func)() = (decltype(jitted_func))code_addr;

        jitted_func();
    };

    static FTexture2DRHIRef out{};
    static FTexture2DRHIRef shader_out{};

    const auto size = g_framework->is_dx11() ? g_framework->get_d3d11_rt_size() : g_framework->get_d3d12_rt_size();
    const auto stack_args = (uintptr_t*)(ctx.rsp + 0x20);

    SPDLOG_INFO("About to call the original!");
    
    if (!rtm->is_pre_texture_call_e8) {
        SPDLOG_INFO("Calling register version of texture create");

        if (rtm->is_using_texture_desc && rtm->is_version_greq_5_1) {
            if (ctx.r9 == 0 || IsBadReadPtr((void*)ctx.r9, sizeof(void*))) {
                SPDLOG_INFO("Possible UE 5.0.3 detected, not 5.1 or above");
                rtm->is_using_texture_desc = false;
                rtm->is_version_5_0_3 = true;
                rtm->is_version_greq_5_1;
            }
        }

        if (rtm->is_using_texture_desc && rtm->is_version_greq_5_1) {
            SPDLOG_INFO("Calling UE5 texture desc version of texture create");

            void (*func)(
                uintptr_t rhi,
                FTexture2DRHIRef* out,
                uintptr_t command_list,
                uintptr_t desc,
                uintptr_t stack_0, // Stack dummies in-case this is the wrong function
                uintptr_t stack_1,
                uintptr_t stack_2,
                uintptr_t stack_3,
                uintptr_t stack_4,
                uintptr_t stack_5,
                uintptr_t stack_6,
                uintptr_t stack_7,
                uintptr_t stack_8) = (decltype(func))func_ptr;

            // Scan for the render target width and height in the desc
            // and replace it with the desktop resolution (This is for the UI texture)
            const auto scan_x = VR::get()->get_hmd_width() * 2;
            const auto scan_y = VR::get()->get_hmd_height();

            std::optional<int32_t> width_offset{};
            std::optional<int32_t> height_offset{};

            int32_t old_width{};
            int32_t old_height{};

            for (auto i = 0; i < 0x100; ++i) {
                auto& x = *(int32_t*)(ctx.r9 + i);
                auto& y = *(int32_t*)(ctx.r9 + i + 4);

                if (x == scan_x && y == scan_y) {
                    SPDLOG_INFO("UE5: Found render target width and height at offset: {:x}", i);

                    width_offset = i;
                    height_offset = i + 4;

                    old_width = x;
                    old_height = y;

                    x = size.x;
                    y = size.y;

                    uint8_t* format = (uint8_t*)(ctx.r9 + width_offset.value() + 15);

                    // some games have 10 bit format
                    if (*format == 18) {
                        *format = 2; // PF_B8G8R8A8
                    }

                    break;
                }
            }

            func(ctx.rcx, &out, ctx.r8, ctx.r9,
                stack_args[0], stack_args[1], 
                stack_args[2], stack_args[3],
                stack_args[4],
                stack_args[5], stack_args[6],
                stack_args[7], stack_args[8]
            );

            if (width_offset && height_offset) {
                auto& x = *(int32_t*)(ctx.r9 + *width_offset);
                auto& y = *(int32_t*)(ctx.r9 + *height_offset);

                x = old_width;
                y = old_height;
            }

            if (rtm->texture_hook_ref == nullptr || rtm->texture_hook_ref->texture == nullptr) {
                SPDLOG_INFO("Had to set texture hook ref in pre texture hook!");
                rtm->texture_hook_ref = (FTexture2DRHIRef*)ctx.rdx;
            }
        } else if (rtm->is_using_texture_desc) { // extremely rare.
            SPDLOG_INFO("Calling UE4 texture desc version of texture create");

            void (*func)(
                uintptr_t rhi,
                uintptr_t desc,
                TRefCountPtr<IPooledRenderTarget>* out,
                uintptr_t name // wchar_t*
            ) = (decltype(func))func_ptr;

            // Scan for the render target width and height in the desc
            // and replace it with the desktop resolution (This is for the UI texture)
            const auto scan_x = VR::get()->get_hmd_width() * 2;
            const auto scan_y = VR::get()->get_hmd_height();

            std::optional<int32_t> width_offset{};
            std::optional<int32_t> height_offset{};

            int32_t old_width{};
            int32_t old_height{};

            for (auto i = 0; i < 0x100; ++i) {
                auto& x = *(int32_t*)(ctx.rdx + i);
                auto& y = *(int32_t*)(ctx.rdx + i + 4);

                if (x == scan_x && y == scan_y) {
                    SPDLOG_INFO("UE4: Found render target width and height at offset: {:x}", i);

                    width_offset = i;
                    height_offset = i + 4;

                    old_width = x;
                    old_height = y;

                    x = size.x;
                    y = size.y;
                    break;
                }
            }

            static TRefCountPtr<IPooledRenderTarget> real_out{};

            func(ctx.rcx, ctx.rdx, &real_out, ctx.r9);

            if (real_out.reference != nullptr) {
                const auto& tex = real_out.reference->item.texture;
                const auto& shader = real_out.reference->item.srt;
                out.texture = tex.texture;
                shader_out.texture = shader.texture;
            }

            if (width_offset && height_offset) {
                auto& x = *(int32_t*)(ctx.rdx + *width_offset);
                auto& y = *(int32_t*)(ctx.rdx + *height_offset);

                x = old_width;
                y = old_height;
            }

            if (rtm->texture_hook_ref == nullptr || rtm->texture_hook_ref->texture == nullptr) {
                SPDLOG_INFO("Had to set texture hook ref in pre texture hook!");
                rtm->texture_hook_ref = (FTexture2DRHIRef*)ctx.r8;
            }
        } else { // most common version.
            SPDLOG_INFO("Calling common version of texture create (several arguments)");

            void (*func)(
                uintptr_t rhi,
                FTexture2DRHIRef* out,
                uintptr_t command_list,
                uintptr_t w,
                uintptr_t h,
                uintptr_t format,
                uintptr_t mips,
                uintptr_t samples,
                uintptr_t flags,
                uintptr_t create_info,
                uintptr_t additional,
                uintptr_t additional2) = (decltype(func))func_ptr;

            func(ctx.rcx, &out, ctx.r8, size.x, size.y, 2, 
                stack_args[2], stack_args[3], stack_args[4], 
                stack_args[5], stack_args[6], stack_args[7]);

            if (rtm->texture_hook_ref == nullptr || rtm->texture_hook_ref->texture == nullptr) {
                SPDLOG_INFO("Had to set texture hook ref in pre texture hook!");
                rtm->texture_hook_ref = (FTexture2DRHIRef*)ctx.rdx;
            }
        }

        rtm->ui_target = out.texture;
    } else {
        SPDLOG_INFO("Calling E8 version of texture create");

        if (is_ue57_dx11_backend() && rtm->is_using_texture_desc && rtm->is_version_greq_5_1) {
            if (is_probable_ue57_dx11_texture_desc_prepare_function(func_ptr)) {
                SPDLOG_WARN_ONCE("Skipping UE 5.7 D3D11 texture-desc prepare helper");
                return;
            }

            SPDLOG_WARN_ONCE("Skipping UE 5.7 D3D11 texture-create replay; RHICmdList texture initializers are not safe to duplicate here");
            return;
        }

        // check if RCX is near the stack pointer
        // if it is then it's a different form of E8 call that takes the texture in the first parameter.
        if (ctx.rcx != 0 && std::abs((int64_t)ctx.rcx - (int64_t)ctx.rsp) <= 0x300) {
            SPDLOG_INFO("Weird form of E8 call detected...");

            // RDX check is to make sure RDX is a pointer and not something like the width which would be a relatively small integer
            if (rtm->is_using_texture_desc && rtm->is_version_greq_5_1 && ctx.rdx >= 65535) {
                SPDLOG_INFO("Calling UE5 texture desc version of texture create");

                void (*func)(
                    FTexture2DRHIRef* out,
                    uintptr_t desc,
                    uintptr_t r8,
                    uintptr_t r9
                ) = (decltype(func))func_ptr;

                // Scan for the render target width and height in the desc
                // and replace it with the desktop resolution (This is for the UI texture)
                const auto scan_x = VR::get()->get_hmd_width() * 2;
                const auto scan_y = VR::get()->get_hmd_height();

                std::optional<int32_t> width_offset{};
                std::optional<int32_t> height_offset{};

                int32_t old_width{};
                int32_t old_height{};

                for (auto i = 0; i < 0x100; ++i) {
                    auto& x = *(int32_t*)(ctx.rdx + i);
                    auto& y = *(int32_t*)(ctx.rdx + i + 4);

                    if (x == scan_x && y == scan_y) {
                        SPDLOG_INFO("UE5: Found render target width and height at offset: {:x}", i);

                        width_offset = i;
                        height_offset = i + 4;

                        old_width = x;
                        old_height = y;

                        x = size.x;
                        y = size.y;
                        break;
                    }
                }

                func(&out, ctx.rdx, ctx.r8, ctx.r9);

                if (width_offset && height_offset) {
                    auto& x = *(int32_t*)(ctx.rdx + *width_offset);
                    auto& y = *(int32_t*)(ctx.rdx + *height_offset);

                    x = old_width;
                    y = old_height;
                }

                if (rtm->texture_hook_ref == nullptr || rtm->texture_hook_ref->texture == nullptr) {
                    SPDLOG_INFO("Had to set texture hook ref in pre texture hook!");
                    rtm->texture_hook_ref = (FTexture2DRHIRef*)ctx.rcx;
                }
            } else {
                // Format
                ctx.r9 = 2; // PF_B8G8R8A8

                void (*func)(
                    FTexture2DRHIRef* out,
                    uint32_t w,
                    uint32_t h,
                    uint8_t format,
                    uintptr_t mips,
                    uintptr_t samples,
                    uintptr_t flags,
                    uintptr_t a7,
                    uintptr_t a8,
                    uintptr_t a9,
                    uintptr_t additional,
                    uintptr_t additional2) = (decltype(func))func_ptr;

                func(&out, (uint32_t)size.x, (uint32_t)size.y, 2,
                    stack_args[0], stack_args[1], 
                    stack_args[2], stack_args[3],
                    stack_args[4],
                    stack_args[7], stack_args[8], stack_args[9]);
            }
        } else {
            ctx.r8 = 2; // PF_B8G8R8A8

            std::optional<int> previous_stack_found_index{};
            std::optional<int> previous_stack_repeating_index{};

            std::optional<int> texture_argument_index{};
            std::optional<int> shader_argument_index{};

            for (auto i = 0; i < 10; ++i) {
                const auto stack_ptr = stack_args[i];

                if (std::abs((int64_t)stack_ptr - (int64_t)ctx.rsp) <= 0x300) {
                    if (previous_stack_found_index && *previous_stack_found_index == i - 1) {
                        previous_stack_repeating_index = i;
                    }

                    previous_stack_found_index = i;
                    SPDLOG_INFO("Stack pointer found at arg index {} ({} stack)", i + 4, i);
                } else if (previous_stack_repeating_index && *previous_stack_repeating_index == i - 1) {
                    texture_argument_index = i - 2;
                    shader_argument_index = i - 1;
                    SPDLOG_INFO("Texture argument may be at index {} ({} stack)", *texture_argument_index + 4, *texture_argument_index);
                    SPDLOG_INFO("Shader argument may be at index {} ({} stack)", *shader_argument_index + 4, *shader_argument_index);
                    break;
                }
            }

            if (!texture_argument_index && !shader_argument_index) {
                // operate on a wild guess (hardcoded function signature)
                SPDLOG_INFO("Calling E8 version of texture create with hardcoded function signature");

                void (*func)(
                    uint32_t w,
                    uint32_t h,
                    uint8_t format,
                    uintptr_t mips,
                    uintptr_t samples,
                    uintptr_t flags,
                    uintptr_t a7,
                    uintptr_t a8,
                    uintptr_t a9,
                    FTexture2DRHIRef* out,
                    FTexture2DRHIRef* shader_out,
                    uintptr_t additional,
                    uintptr_t additional2) = (decltype(func))func_ptr;

                func((uint32_t)size.x, (uint32_t)size.y, 2, ctx.r9,
                    stack_args[0], stack_args[1], 
                    stack_args[2], stack_args[3],
                    stack_args[4],
                    &out, &shader_out,
                    stack_args[7], stack_args[8]);
            } else {
                // dynamically generate the function call
                SPDLOG_INFO("Calling E8 version of texture create with dynamically generated function signature");

                void (*func)(
                    uint32_t w,
                    uint32_t h,
                    uint8_t format,
                    uintptr_t mips,
                    uintptr_t stack_0,
                    uintptr_t stack_1,
                    uintptr_t stack_2,
                    uintptr_t stack_3,
                    uintptr_t stack_4,
                    uintptr_t stack_5,
                    uintptr_t stack_6,
                    uintptr_t stack_7,
                    uintptr_t stack_8) = (decltype(func))func_ptr;

                std::array<uintptr_t, 9> cloned_stack{};
                for (auto i = 0; i < 9; ++i) {
                    cloned_stack[i] = stack_args[i];
                }

                cloned_stack[*texture_argument_index] = (uintptr_t)&out;
                cloned_stack[*shader_argument_index] = (uintptr_t)&shader_out;

                func((uint32_t)size.x, (uint32_t)size.y, 2, ctx.r9,
                    cloned_stack[0], cloned_stack[1], 
                    cloned_stack[2], cloned_stack[3],
                    cloned_stack[4],
                    cloned_stack[5], cloned_stack[6],
                    cloned_stack[7], cloned_stack[8]);

                if (rtm->texture_hook_ref == nullptr || rtm->texture_hook_ref->texture == nullptr) {
                    SPDLOG_INFO("Had to set texture hook ref in pre texture hook!");
                    rtm->texture_hook_ref = (FTexture2DRHIRef*)stack_args[*texture_argument_index];
                }
            }
        }

        rtm->ui_target = out.texture;
    }

    if (out.texture == nullptr) {
        SPDLOG_ERROR("Failed to create UI texture!");
    } else {
        SPDLOG_INFO("Created UI texture at {:x}", (uintptr_t)out.texture);
        ue51_note_ui_created(out.texture, (uint32_t)size.x, (uint32_t)size.y);
    }

    //call_with_context((uintptr_t)func, out);

    SPDLOG_INFO("Called the original function!");

    // Cause stuff like the VR ui texture to get recreated.
    VR::get()->reinitialize_renderer();
}

void VRRenderTargetManager_Base::texture_hook_callback(safetyhook::Context& ctx, bool from_second) {
    auto rtm = g_hook->get_render_target_manager();

    if (g_framework->is_dx12() && shf_is_current_game()) {
        SPDLOG_INFO_EVERY_N_SEC(2, "[SHf] PostTextureHook summary last_ref={:x}", (uintptr_t)rtm->texture_hook_ref);
    } else if (is_ue_5_1_dx12_backend()) {
        SPDLOG_INFO_EVERY_N_SEC(2, "[UE5.1][RTChurn] PostTextureHook summary last_ref={:x}", (uintptr_t)rtm->texture_hook_ref);
    } else {
        SPDLOG_INFO("Post texture hook called!");
        SPDLOG_INFO(" Ref: {:x}", (uintptr_t)rtm->texture_hook_ref);
    }

    if (!rtm->allocate_texture_called) {
        g_hook->set_should_recreate_textures(true);
        rtm->texture_hook_ref = nullptr;
        rtm->shader_resource_hook_ref = nullptr;

        SPDLOG_INFO("[Post texture hook] Allocate texture was not called, skipping...");
        return;
    }

    rtm->allocate_texture_called = false;

    // very rare...
    if (rtm->is_using_texture_desc && !rtm->is_version_greq_5_1) {
        const auto pooled_rt_container = (TRefCountPtr<IPooledRenderTarget>*)rtm->texture_hook_ref;

        if (pooled_rt_container != nullptr && pooled_rt_container->reference != nullptr) {
            rtm->texture_hook_ref = &pooled_rt_container->reference->item.texture;
        }
    }

    auto is_valid_texture_candidate = [&](FRHITexture2D* candidate, const char* source) -> bool {
        if (candidate == nullptr || IsBadReadPtr(candidate, sizeof(void*))) {
            return false;
        }

        void* vtable{};

        try {
            vtable = *(void**)candidate;
        } catch (...) {
            return false;
        }

        if (vtable == nullptr || IsBadReadPtr(vtable, sizeof(void*))) {
            SPDLOG_INFO_EVERY_N_SEC(1, " Rejected texture candidate from {} because the vtable is invalid", source);
            return false;
        }

        if (!utility::get_module_within(vtable).has_value()) {
            SPDLOG_INFO_EVERY_N_SEC(1, " Rejected texture candidate from {} because its vtable {:x} is not inside a module", source, (uintptr_t)vtable);
            return false;
        }

        FRHITexture2D::set_vtable(vtable);
        return true;
    };

    auto recover_texture_from_ref = [&](uintptr_t ref_ptr, const char* source) -> FRHITexture2D* {
        if (ref_ptr == 0 || IsBadReadPtr((void*)ref_ptr, sizeof(FTexture2DRHIRef))) {
            return nullptr;
        }

        const auto ref = (FTexture2DRHIRef*)ref_ptr;

        if (!is_valid_texture_candidate(ref->texture, source)) {
            return nullptr;
        }

        SPDLOG_INFO(" Recovered texture from {}: {:x}", source, (uintptr_t)ref->texture);
        return ref->texture;
    };

    auto try_promote_dedicated_ui_candidate = [&](FRHITexture2D* candidate, const char* source) -> bool {
        if (!is_ue_5_7_or_newer() || !g_framework->is_dx12()) {
            return false;
        }

        if (!is_valid_texture_candidate(candidate, source) || candidate == rtm->get_render_target()) {
            return false;
        }

        const auto native = (ID3D12Resource*)candidate->get_native_resource();

        if (native == nullptr || IsBadReadPtr(native, sizeof(void*))) {
            return false;
        }

        const auto desc = native->GetDesc();

        if (desc.Width == 0 || desc.Height == 0) {
            return false;
        }

        const auto requested_width = rtm->get_dedicated_ui_width();
        const auto requested_height = rtm->get_dedicated_ui_height();

        if (requested_width == 0 || requested_height == 0) {
            return false;
        }

        if (desc.Width != requested_width || desc.Height != requested_height) {
            SPDLOG_INFO_EVERY_N_SEC(1,
                "[VRRenderTargetManager] Rejected UE 5.7 UI candidate from {} because size [{}x{}] != requested [{}x{}]",
                source, desc.Width, desc.Height, requested_width, requested_height);
            return false;
        }

        rtm->set_dedicated_ui_target(candidate, desc.Width, desc.Height);
        rtm->get_fallback_ui_target_ref() = nullptr;

        SPDLOG_WARN_ONCE("[VRRenderTargetManager] Promoted an engine-created texture to the dedicated UI target");
        SPDLOG_INFO("[VRRenderTargetManager] dedicated UI target from {} [{:x}] [{}x{}]",
            source, (uintptr_t)candidate, desc.Width, desc.Height);

        return true;
    };

    FRHITexture2D* texture = nullptr;
    const char* texture_source = "unknown";

    if (rtm->texture_hook_ref != nullptr) {
        texture = rtm->texture_hook_ref->texture;
        texture_source = "texture_hook_ref";

        // happens?
        if (texture == nullptr) {
            if (g_framework->is_dx12() && is_ue_5_7_or_newer() && rtm->texture_finalize_func != 0) {
                SPDLOG_INFO_EVERY_N_SEC(1, " Texture is null after UE 5.7 finalize hook; skipping unreliable RAX fallback");
            } else {
                SPDLOG_INFO(" Texture is null, trying to get it from RAX...");

                const auto ref = (FTexture2DRHIRef*)ctx.rax;

                if (!IsBadReadPtr(ref, sizeof(void*)) && is_valid_texture_candidate(ref->texture, "RAX")) {
                    texture = ref->texture;
                    texture_source = "RAX";
                } else {
                    SPDLOG_ERROR(" RAX is bad! Can't get texture!");
                }
            }
        }

        if (is_valid_texture_candidate(texture, "texture_hook_ref")) {
            if (shf_is_current_game() && g_framework->is_dx12()) {
                shf_log_rtm_candidate(rtm, texture, texture_source);
            } else if (is_ue_5_1_dx12_backend()) {
                SPDLOG_INFO_EVERY_N_SEC(2, "[UE5.1][RTChurn] Resulting texture summary source={} tex={:x}",
                    texture_source, (uintptr_t)texture);
            } else {
                SPDLOG_INFO(" Resulting texture: {:x}", (uintptr_t)texture);
                try {
                    SPDLOG_INFO(" Real resource: {:x}", (uintptr_t)texture->get_native_resource());
                } catch (const std::exception& e) {
                    SPDLOG_INFO(" Real resource: unavailable ({})", e.what());
                } catch (...) {
                    SPDLOG_INFO(" Real resource: unavailable (unknown exception)");
                }
            }
        } else {
            texture = nullptr;
            SPDLOG_INFO(" Texture is still null!");
        }
    }

    if ((!shf_is_current_game() || !g_framework->is_dx12()) && !is_ue_5_1_dx12_backend()) {
        SPDLOG_INFO(" last texture index: {}", rtm->last_texture_index);
    }

    if (texture != nullptr) {
        rtm->render_target = texture;
    }

    bool dedicated_ui_promoted = false;

    if (rtm->shader_resource_hook_ref != nullptr && rtm->shader_resource_hook_ref->texture != nullptr) {
        dedicated_ui_promoted = try_promote_dedicated_ui_candidate(rtm->shader_resource_hook_ref->texture, "shader_resource_hook_ref");
    }

    if (!dedicated_ui_promoted && !is_ue_5_7_or_newer() && rtm->get_fallback_ui_target_ref() == nullptr && rtm->shader_resource_hook_ref != nullptr) {
        const auto shader_texture = rtm->shader_resource_hook_ref->texture;

        if (shader_texture != nullptr) {
            SPDLOG_INFO(" Falling back to original shader resource texture as UI target: {:x}", (uintptr_t)shader_texture);
            FRHITexture2D::set_vtable(*(void**)shader_texture);
            rtm->get_fallback_ui_target_ref() = shader_texture;
        }
    }

    if (!dedicated_ui_promoted && rtm->get_fallback_ui_target_ref() == nullptr) {
        for (const auto& candidate : {
                std::pair{(uintptr_t)rtm->shader_resource_hook_ref, "shader_resource_hook_ref"},
                std::pair{ctx.rdx, "RDX"},
                std::pair{ctx.rcx, "RCX"},
                std::pair{ctx.r8, "R8"},
                std::pair{ctx.r9, "R9"},
                std::pair{ctx.rax, "RAX"},
            })
        {
            const auto recovered = recover_texture_from_ref(candidate.first, candidate.second);

            if (recovered != nullptr) {
                if (try_promote_dedicated_ui_candidate(recovered, candidate.second)) {
                    dedicated_ui_promoted = true;
                    break;
                }

                if (!is_ue_5_7_or_newer()) {
                    rtm->get_fallback_ui_target_ref() = recovered;
                }
                break;
            }
        }
    }

    if (!is_ue_5_7_or_newer() && rtm->get_fallback_ui_target_ref() == nullptr && texture != nullptr) {
        SPDLOG_WARN(" Falling back to render target texture as UI target: {:x}", (uintptr_t)texture);
        rtm->get_fallback_ui_target_ref() = texture;
    } else if (is_ue_5_7_or_newer() && rtm->get_fallback_ui_target_ref() == nullptr && texture != nullptr) {
        SPDLOG_WARN_ONCE("Skipping render-target-as-UI fallback on UE 5.7+; waiting for a dedicated UI target");
    }

    rtm->texture_hook_ref = nullptr;
    rtm->shader_resource_hook_ref = nullptr;
    ++rtm->last_texture_index;
}

void VRRenderTargetManager_Base::destroy_scene_capture() try {
    if (this->scene_capture_actor != nullptr && this->in_flight_target == nullptr) {
        SPDLOG_INFO("Destroying scene capture!");

        if (this->scene_capture_actor.valid()) {
            this->scene_capture_actor->destroy_actor();
        }
    }

    if (this->in_flight_target == nullptr) {
        this->scene_capture_actor = nullptr;
        this->scene_capture_component = nullptr;
        this->scene_capture_target = nullptr;

        RHIThreadWorker::get().enqueue([this]() -> void {
            this->scene_capture_target_rhi_thread = nullptr;
        });
    }
} catch (const std::exception& e) {
    SPDLOG_ERROR("[VRRenderTargetManager] Exception in destroy_scene_capture: {}", e.what());
    this->scene_capture_target = nullptr;
    this->scene_capture_actor = nullptr;
    this->scene_capture_component = nullptr;
    
    RHIThreadWorker::get().enqueue([this]() -> void {
        this->scene_capture_target_rhi_thread = nullptr;
    });
} catch (...) {
    SPDLOG_ERROR("[VRRenderTargetManager] Unknown exception in destroy_scene_capture!");
}

void VRRenderTargetManager_Base::destroy_dedicated_ui_target() {
    if (dedicated_ui_texture != nullptr && dedicated_ui_texture.valid()) {
        auto rooted_texture = dedicated_ui_texture;

        GameThreadWorker::get().enqueue([rooted_texture]() mutable {
            if (rooted_texture != nullptr && rooted_texture.valid()) {
                rooted_texture->remove_from_root();
            }
        });
    }

    if (in_flight_dedicated_ui_texture != nullptr) {
        auto* rooted_texture = in_flight_dedicated_ui_texture;

        GameThreadWorker::get().enqueue([rooted_texture]() {
            if (rooted_texture != nullptr && !IsBadReadPtr(rooted_texture, sizeof(void*))) {
                try {
                    rooted_texture->remove_from_root();
                } catch (...) {
                }
            }
        });
    }

    owned_dedicated_ui_target.reset();
    dedicated_ui_target = nullptr;
    dedicated_ui_texture = nullptr;
    in_flight_dedicated_ui_texture = nullptr;
    reset_dedicated_ui_creation_state();
}

void VRRenderTargetManager_Base::cancel_dedicated_ui_creation_preserving_target(const char* reason) {
    const bool had_pending_creation =
        dedicated_ui_creation_pending ||
        dedicated_ui_object_created ||
        in_flight_dedicated_ui_generation != 0 ||
        in_flight_dedicated_ui_texture != nullptr ||
        dedicated_ui_texture != nullptr;

    if (!had_pending_creation) {
        return;
    }

    auto rooted_texture = dedicated_ui_texture;
    auto* in_flight_texture = in_flight_dedicated_ui_texture;
    auto* rooted_raw = rooted_texture.get();

    if (rooted_texture != nullptr && rooted_texture.valid()) {
        GameThreadWorker::get().enqueue([rooted_texture]() mutable {
            if (rooted_texture != nullptr && rooted_texture.valid()) {
                rooted_texture->remove_from_root();
            }
        });
    }

    if (in_flight_texture != nullptr && in_flight_texture != rooted_raw) {
        GameThreadWorker::get().enqueue([in_flight_texture]() {
            if (in_flight_texture != nullptr && !IsBadReadPtr(in_flight_texture, sizeof(void*))) {
                try {
                    in_flight_texture->remove_from_root();
                } catch (...) {
                }
            }
        });
    }

    dedicated_ui_texture = nullptr;
    in_flight_dedicated_ui_texture = nullptr;
    reset_dedicated_ui_creation_state();

    SPDLOG_INFO(
        "[VRRenderTargetManager] Cancelled in-flight dedicated UI UObject creation after {} promotion; preserving current FRHITexture target",
        reason != nullptr ? reason : "external UI target");
}

void VRRenderTargetManager_Base::invalidate_resolution_dependent_targets() {
    texture_hook_ref = nullptr;
    shader_resource_hook_ref = nullptr;
    allocate_texture_called = false;
    last_texture_index = 0;
    last_width = 0;
    last_height = 0;
    wants_depth_reallocate = true;

    ui_target = nullptr;
    destroy_dedicated_ui_target();
    dedicated_ui_width = 0;
    dedicated_ui_height = 0;
    dedicated_ui_last_attempt = {};
}

void VRRenderTargetManager_Base::reset_dedicated_ui_creation_state() {
    dedicated_ui_creation_pending = false;
    dedicated_ui_object_created = false;
    in_flight_dedicated_ui_generation = 0;
    dedicated_ui_pending_since = {};
    dedicated_ui_resource_pending_since = {};
}

void VRRenderTargetManager_Base::set_dedicated_ui_target(FRHITexture2D* rt, uint32_t width, uint32_t height) {
    if (rt != nullptr) {
        FRHITexture2D::set_vtable(*(void**)rt);
        owned_dedicated_ui_target = std::make_unique<FTexture2DRHIRef>(*rt);
    } else {
        owned_dedicated_ui_target.reset();
    }

    dedicated_ui_target = rt;
    dedicated_ui_width = width;
    dedicated_ui_height = height;
    dedicated_ui_creation_pending = false;
}

void VRRenderTargetManager_Base::request_dedicated_ui_target(uint32_t width, uint32_t height) {
    if (!supports_dedicated_ui_target_for_current_game() || width == 0 || height == 0) {
        return;
    }

    const bool extent_changed = dedicated_ui_width != width || dedicated_ui_height != height;
    dedicated_ui_width = width;
    dedicated_ui_height = height;

    if (extent_changed) {
        if (get_dedicated_ui_target() != nullptr || dedicated_ui_texture != nullptr || in_flight_dedicated_ui_texture != nullptr) {
            SPDLOG_INFO("[VRRenderTargetManager] Dedicated UI extent changed to [{}x{}], recreating", width, height);
            destroy_dedicated_ui_target();
        }

        dedicated_ui_last_attempt = {};
        reset_dedicated_ui_creation_state();
    }

    try_schedule_dedicated_ui_creation();
}

bool VRRenderTargetManager_Base::can_attempt_dedicated_ui_creation() const {
    if (!supports_dedicated_ui_target_for_current_game() || dedicated_ui_width == 0 || dedicated_ui_height == 0) {
        return false;
    }

    if (!g_framework->is_game_data_intialized()) {
        return false;
    }

    auto* engine = sdk::UGameEngine::get();

    if (engine == nullptr) {
        return false;
    }

    if (g_hook == nullptr || !g_hook->has_slate_hook() || !g_hook->has_seen_stable_slate_draw()) {
        return false;
    }

    if (supports_ue55_dedicated_ui_target_for_current_game()) {
        return true;
    }

    if (!g_hook->prefers_slate_thread_for_session()) {
        return false;
    }

    if (!g_hook->has_seen_prerender_viewfamily()) {
        return false;
    }

    if (!g_hook->has_scene_view_family_offsets_ready()) {
        return false;
    }

    return true;
}

bool VRRenderTargetManager_Base::try_schedule_dedicated_ui_creation() {
    if (!can_attempt_dedicated_ui_creation()) {
        return false;
    }

    if (get_dedicated_ui_target() != nullptr || dedicated_ui_texture != nullptr || in_flight_dedicated_ui_texture != nullptr ||
        dedicated_ui_creation_pending || in_flight_dedicated_ui_generation != 0)
    {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    if (dedicated_ui_last_attempt.time_since_epoch().count() != 0 &&
        now - dedicated_ui_last_attempt < std::chrono::seconds(2))
    {
        return false;
    }

    return create_dedicated_ui_texture();
}

bool VRRenderTargetManager_Base::create_dedicated_ui_texture() {
    if (!supports_dedicated_ui_target_for_current_game()) {
        return false;
    }

    if (dedicated_ui_width == 0 || dedicated_ui_height == 0) {
        return false;
    }

    const auto width = dedicated_ui_width;
    const auto height = dedicated_ui_height;
    const auto generation = ++dedicated_ui_generation;
    dedicated_ui_last_attempt = std::chrono::steady_clock::now();
    dedicated_ui_creation_pending = true;
    dedicated_ui_object_created = false;
    in_flight_dedicated_ui_generation = generation;
    dedicated_ui_pending_since = {};
    dedicated_ui_resource_pending_since = {};

    SPDLOG_INFO("[VRRenderTargetManager] Scheduling dedicated UI target creation for generation {} [{}x{}]", generation, width, height);

    GameThreadWorker::get().enqueue([this, width, height, generation]() -> void {
        try {
            if (!this->is_dedicated_ui_generation_current(generation)) {
                return;
            }

            if (this->in_flight_dedicated_ui_texture != nullptr || this->get_dedicated_ui_target() != nullptr || this->dedicated_ui_texture != nullptr) {
                this->reset_dedicated_ui_creation_state();
                return;
            }

            auto* engine = sdk::UGameEngine::get();

            if (engine == nullptr) {
                SPDLOG_INFO_EVERY_N_SEC(2, "[VRRenderTargetManager] Delaying dedicated UI texture creation because UGameEngine is not ready");
                this->reset_dedicated_ui_creation_state();
                return;
            }

            auto* world = engine->get_world();

            if (world == nullptr) {
                SPDLOG_INFO_EVERY_N_SEC(2, "[VRRenderTargetManager] Delaying dedicated UI texture creation because the world is not ready");
                this->reset_dedicated_ui_creation_state();
                return;
            }

            auto* kismet_rendering = sdk::UKismetRenderingLibrary::get();

            if (kismet_rendering == nullptr) {
                SPDLOG_INFO_EVERY_N_SEC(2, "[VRRenderTargetManager] Delaying dedicated UI texture creation because KismetRenderingLibrary is not ready");
                this->reset_dedicated_ui_creation_state();
                return;
            }

            const float clear_color[4]{0.0f, 0.0f, 0.0f, 0.0f};
            auto* tgt_raw = kismet_rendering->create_render_target_2d(world, width, height, 2, clear_color, false);

            if (tgt_raw == nullptr) {
                SPDLOG_WARNING_EVERY_N_SEC(2, "[VRRenderTargetManager] Failed to create dedicated UI texture [{}x{}] on the game thread", width, height);
                this->reset_dedicated_ui_creation_state();
                return;
            }

            tgt_raw->add_to_root();

            if (!this->is_dedicated_ui_generation_current(generation)) {
                try {
                    tgt_raw->remove_from_root();
                } catch (...) {
                }
                return;
            }

            sdk::UObjectReference<sdk::UTexture> tgt{tgt_raw};
            this->in_flight_dedicated_ui_texture = tgt_raw;
            this->dedicated_ui_object_created = true;
            this->dedicated_ui_pending_since = std::chrono::steady_clock::now();
            this->dedicated_ui_resource_pending_since = this->dedicated_ui_pending_since;

            SPDLOG_INFO("[VRRenderTargetManager] Created dedicated UI UObject for generation {}", generation);

            RenderThreadWorker::get().enqueue_conditional(
                [this, tgt, width, height, generation]() -> bool {
                    try {
                        if (!this->is_dedicated_ui_generation_current(generation)) {
                            return true;
                        }

                        if (!tgt.valid()) {
                            SPDLOG_ERROR("[VRRenderTargetManager] dedicated UI texture was destroyed before its render resource became valid");
                            GameThreadWorker::get().enqueue([this, generation]() -> void {
                                if (!this->is_dedicated_ui_generation_current(generation)) {
                                    return;
                                }

                                this->in_flight_dedicated_ui_texture = nullptr;
                                this->destroy_dedicated_ui_target();
                            });
                            return true;
                        }

                        if (!sdk::UTexture::update_render_resource_offset_texture2d(tgt)) {
                            return false;
                        }

                        auto* rsrc = (sdk::FTextureRenderTargetResource*)tgt->get_resource();
                        if (rsrc == nullptr) {
                            return false;
                        }

                        const bool updated_vtable_offset = sdk::FTextureRenderTargetResource::update_render_target_vtable_offset(rsrc);
                        auto* frt = updated_vtable_offset ? rsrc->as_render_target() : nullptr;

                        if (frt == nullptr) {
                            return false;
                        }

                        sdk::FRenderTarget::update_offsets(frt);
                        auto** frt_texture = frt->get_render_target_texture();

                        if (frt_texture == nullptr || *frt_texture == nullptr || IsBadReadPtr(*frt_texture, sizeof(void*))) {
                            return false;
                        }

                        if (!this->is_dedicated_ui_generation_current(generation)) {
                            return true;
                        }

                        FRHITexture2D::set_vtable(*(void**)*frt_texture);
                        this->set_dedicated_ui_target(*frt_texture, width, height);
                        this->get_fallback_ui_target_ref() = nullptr;

                        GameThreadWorker::get().enqueue([this, tgt, generation]() -> void {
                            if (!this->is_dedicated_ui_generation_current(generation)) {
                                return;
                            }

                            if (!tgt.valid()) {
                                this->dedicated_ui_texture = nullptr;
                                this->in_flight_dedicated_ui_texture = nullptr;
                                this->destroy_dedicated_ui_target();
                                return;
                            }

                            this->dedicated_ui_texture = tgt;
                            this->in_flight_dedicated_ui_texture = nullptr;
                            this->reset_dedicated_ui_creation_state();
                        });

                        SPDLOG_INFO("[VRRenderTargetManager] dedicated UI target ready for generation {} [{}x{}]", generation, width, height);
                        return true;
                    } catch (...) {
                        SPDLOG_ERROR("[VRRenderTargetManager] Exception while waiting for the dedicated UI texture render resource");
                        GameThreadWorker::get().enqueue([this, generation]() -> void {
                            if (!this->is_dedicated_ui_generation_current(generation)) {
                                return;
                            }

                            this->dedicated_ui_texture = nullptr;
                            this->in_flight_dedicated_ui_texture = nullptr;
                            this->destroy_dedicated_ui_target();
                        });
                        return true;
                    }
                },
                [this, generation]() {
                    if (!this->is_dedicated_ui_generation_current(generation)) {
                        return;
                    }

                    SPDLOG_ERROR("[VRRenderTargetManager] dedicated UI target generation {} timed out waiting for the render resource", generation);
                    GameThreadWorker::get().enqueue([this, generation]() -> void {
                        if (!this->is_dedicated_ui_generation_current(generation)) {
                            return;
                        }

                        this->dedicated_ui_texture = nullptr;
                        this->in_flight_dedicated_ui_texture = nullptr;
                        this->destroy_dedicated_ui_target();
                    });
                },
                std::chrono::seconds(2));
        } catch (...) {
            SPDLOG_ERROR("[VRRenderTargetManager] Exception while scheduling dedicated UI texture creation");
            this->in_flight_dedicated_ui_texture = nullptr;
            this->reset_dedicated_ui_creation_state();
        }
    });

    return true;
}

void VRRenderTargetManager_Base::ensure_dedicated_ui_target(uintptr_t command_list) {
    (void)command_list;

    if (!supports_dedicated_ui_target_for_current_game()) {
        return;
    }

    if (dedicated_ui_width == 0 || dedicated_ui_height == 0) {
        return;
    }

    auto existing_target = get_dedicated_ui_target();

    if (existing_target != nullptr && !IsBadReadPtr(existing_target, sizeof(void*))) {
        if (g_framework->is_dx11()) {
            auto* native_resource = (ID3D11Texture2D*)existing_target->get_native_resource();

            if (native_resource != nullptr && !IsBadReadPtr(native_resource, sizeof(void*))) {
                D3D11_TEXTURE2D_DESC desc{};
                native_resource->GetDesc(&desc);

                if (desc.Width == dedicated_ui_width && desc.Height == dedicated_ui_height) {
                    return;
                }
            }
        } else {
            auto* native_resource = (ID3D12Resource*)existing_target->get_native_resource();

            if (native_resource != nullptr) {
                const auto desc = native_resource->GetDesc();

                if (desc.Width == dedicated_ui_width && desc.Height == dedicated_ui_height) {
                    return;
                }
            }
        }

        SPDLOG_INFO("[VRRenderTargetManager] Recreating dedicated UI target [{}x{}]", dedicated_ui_width, dedicated_ui_height);
        destroy_dedicated_ui_target();
    }

    if (existing_target == nullptr && owned_dedicated_ui_target != nullptr && owned_dedicated_ui_target->texture != nullptr &&
        !IsBadReadPtr(owned_dedicated_ui_target->texture, sizeof(void*)))
    {
        FRHITexture2D::set_vtable(*(void**)owned_dedicated_ui_target->texture);
        dedicated_ui_target = owned_dedicated_ui_target->texture;
        existing_target = get_dedicated_ui_target();
    }

    if (dedicated_ui_texture != nullptr && dedicated_ui_texture.valid()) {
        if (sdk::UTexture::update_render_resource_offset_texture2d(dedicated_ui_texture)) {
            if (auto* rsrc = (sdk::FTextureRenderTargetResource*)dedicated_ui_texture->get_resource(); rsrc != nullptr) {
                const bool updated_vtable_offset = sdk::FTextureRenderTargetResource::update_render_target_vtable_offset(rsrc);
                auto* frt = updated_vtable_offset ? rsrc->as_render_target() : nullptr;

                if (frt != nullptr) {
                    sdk::FRenderTarget::update_offsets(frt);
                    auto** frt_texture = frt->get_render_target_texture();

                    if (frt_texture != nullptr && *frt_texture != nullptr && !IsBadReadPtr(*frt_texture, sizeof(void*))) {
                        FRHITexture2D::set_vtable(*(void**)*frt_texture);
                        set_dedicated_ui_target(*frt_texture, dedicated_ui_width, dedicated_ui_height);
                        get_fallback_ui_target_ref() = nullptr;
                        return;
                    }
                }
            }
        }

        SPDLOG_INFO_EVERY_N_SEC(5, "[VRRenderTargetManager] dedicated UI UObject is alive but its render resource is not ready");
    }

    if (dedicated_ui_object_created &&
        in_flight_dedicated_ui_texture != nullptr &&
        in_flight_dedicated_ui_generation != 0 &&
        dedicated_ui_resource_pending_since.time_since_epoch().count() != 0 &&
        std::chrono::steady_clock::now() - dedicated_ui_resource_pending_since > std::chrono::seconds(5))
    {
        SPDLOG_WARN("[VRRenderTargetManager] dedicated UI target creation appears stuck; resetting and retrying");
        destroy_dedicated_ui_target();
    }

    if (in_flight_dedicated_ui_texture != nullptr || dedicated_ui_creation_pending || in_flight_dedicated_ui_generation != 0) {
        return;
    }

    try_schedule_dedicated_ui_creation();
}

FRHITexture2D* VRRenderTargetManager_Base::get_scene_capture_render_target() {
    if (this->in_flight_target != nullptr) {
        return nullptr;
    }

    const auto is_same_as_rhi_thread = RHIThreadWorker::get().is_same_thread();
    const auto& sct = is_same_as_rhi_thread ? this->scene_capture_target_rhi_thread : this->scene_capture_target;

    if (sct != nullptr) try {
        // I REALLY don't want to lock a mutex in a hot path so let's hope that our exception handler catches everything.
        if (!sct.valid()) {
            SPDLOG_WARN("[VRRenderTargetManager] Scene capture target is not a UTexture! Texture probably deleted on level change!");
            
            if (is_same_as_rhi_thread) {
                this->scene_capture_target_rhi_thread = nullptr;
            }

            return nullptr;
        }

        auto rsrc = (sdk::FTextureRenderTargetResource*)sct->get_resource();
        auto rsrc_frt = rsrc != nullptr ? rsrc->as_render_target() : nullptr;

        if (rsrc_frt != nullptr) {  
            auto tex_ref = rsrc_frt->get_render_target_texture();
            if (tex_ref != nullptr) {
                return *tex_ref;
            }
        }
    } catch (...) {
        SPDLOG_ERROR("[VRRenderTargetManager] Exception in get_scene_capture_render_target! Texture probably deleted on level change!");

        if (is_same_as_rhi_thread) {
            this->scene_capture_target_rhi_thread = nullptr;
        }
    }

    return nullptr;
}

sdk::UTexture* VRRenderTargetManager_Base::get_scene_capture_utexture() {
    if (this->in_flight_target != nullptr) {
        return nullptr;
    }

    const auto& utex = this->scene_capture_target;

    if (utex != nullptr) try {
        if (utex.valid()) {
            return (sdk::UTexture*)utex;
        }

        SPDLOG_WARN("[VRRenderTargetManager] Scene capture target is not a UTexture! Texture probably deleted on level change!");

        GameThreadWorker::get().enqueue([this]() -> void {
            this->in_flight_target = nullptr;
            this->destroy_scene_capture();
        });
    } catch (...) {
        SPDLOG_ERROR("[VRRenderTargetManager] Exception in get_scene_capture_utexture! Texture probably deleted on level change!");

        GameThreadWorker::get().enqueue([this]() -> void {
            this->in_flight_target = nullptr;
            this->destroy_scene_capture();
        });
    }

    return nullptr;
}

bool VRRenderTargetManager_Base::create_scene_capture() try {
    if (this->in_flight_target != nullptr) {
        return false;
    }

    // This is necessary for offset calculations to succeed.
    if (FRHITexture2D::get_vtable() == nullptr) {
        SPDLOG_WARN("[VRRenderTargetManager] FRHITexture2D vtable is null, waiting for it to be set!");
        return false;
    }

    destroy_scene_capture();

    SPDLOG_INFO("Creating scene capture!");

    auto kismet_rendering = sdk::UKismetRenderingLibrary::get();

    if (kismet_rendering == nullptr) {
        SPDLOG_ERROR("[VRRenderTargetManager] Failed to get UKismetRenderingLibrary!");
        return false;
    }

    static auto scene_capture_c = sdk::USceneCaptureComponent2D::static_class();

    if (scene_capture_c == nullptr) {
        SPDLOG_ERROR("[VRRenderTargetManager] Failed to get USceneCaptureComponent2D class!");
        return false;
    }

    auto ugs = sdk::UGameplayStatics::get();

    if (ugs == nullptr) {
        SPDLOG_ERROR("[VRRenderTargetManager] Failed to get UGameplayStatics!");
        return false;
    }

    auto engine = sdk::UGameEngine::get();

    if (engine == nullptr) {
        SPDLOG_ERROR("[VRRenderTargetManager] Failed to get UGameEngine!");
        return false;
    }

    auto world = engine->get_world();

    if (world == nullptr) {
        SPDLOG_ERROR("[VRRenderTargetManager] Failed to get UWorld!");
        return false;
    }

    static auto actor_c = sdk::AActor::static_class();

    if (actor_c == nullptr) {
        SPDLOG_ERROR("[VRRenderTargetManager] Failed to get AActor class!");
        return false;
    }

    this->scene_capture_actor = ugs->spawn_actor(world, actor_c, glm::vec3{0, 0, 0});

    if (this->scene_capture_actor == nullptr) {
        SPDLOG_ERROR("[VRRenderTargetManager] Failed to spawn actor!");
        return false;
    }

    this->scene_capture_component = (sdk::USceneCaptureComponent2D*)this->scene_capture_actor->add_component_by_class(scene_capture_c, false);

    if (this->scene_capture_component == nullptr) {
        SPDLOG_ERROR("[VRRenderTargetManager] Failed to add scene capture component!");
        return false;
    }

    const float clear_color[4] {0.0f, 0.0f, 0.0f, 1.0f};
    auto tgt_raw = kismet_rendering->create_render_target_2d(world, VR::get()->get_hmd_width(), VR::get()->get_hmd_height(), 2, clear_color, false);

    if (tgt_raw == nullptr) {
        SPDLOG_ERROR("[VRRenderTargetManager] Failed to create texture!");
        return false;
    }

    sdk::UObjectReference tgt{tgt_raw};

    SPDLOG_INFO("[VRRenderTargetManager] Created texture target: {:x}", (uintptr_t)tgt.get());
    this->scene_capture_actor->finish_add_component(this->scene_capture_component);

    this->scene_capture_component->set_texture_target(tgt);

    // We don't actually want this to tick.
    // We are just using the property as a convenient way to keep the texture alive without crashing.
    this->scene_capture_component->set_visibility(false);
    if (auto capture_every_frame = scene_capture_c->find_property(L"bCaptureEveryFrame"); capture_every_frame != nullptr) {
        *capture_every_frame->get_data<bool>(this->scene_capture_component) = false;
    }

    static bool already_updated{false};
    static std::array<uintptr_t, 100> original_frender_target_vtable{};
    static auto gamma_increase_fn = +[](const sdk::FRenderTarget* frt) -> float {
        auto rtm = g_hook->get_render_target_manager();
        auto viewport = rtm != nullptr ? rtm->get_viewport() : nullptr;

        if (viewport != nullptr) {
            return viewport->get_display_gamma();
        }

        return 2.2f;
    };

    static auto hook_frt = [](sdk::FRenderTarget* frt) {
        if (frt == nullptr) {
            SPDLOG_WARN("[FRenderTarget] FRenderTarget is null! Can't hook!");
            return;
        }

        SPDLOG_INFO("[FRenderTarget] Hooking FRenderTarget!");

        auto& vtable = *(void**)frt;
        memcpy(original_frender_target_vtable.data(), vtable, original_frender_target_vtable.size() * sizeof(uintptr_t));

        if (auto display_gamma_index = sdk::FRenderTarget::get_display_gamma_index(); display_gamma_index != 0) {
            original_frender_target_vtable[*display_gamma_index] = (uintptr_t)gamma_increase_fn;
            vtable = original_frender_target_vtable.data();
            SPDLOG_INFO("[FRenderTarget] Hooked FRenderTarget!");
        } else {
            SPDLOG_WARN("[FRenderTarget] Gamma index not found, can't hook!");
        }
    };

    static const auto utex_c = sdk::UTexture::static_class();

    // Enqueue offset lookup on the render thread because that's when the resource is actually created.
    if (!already_updated) {
        this->in_flight_target = tgt;

        // Repeats every render loop for 5 seconds, times out if the texture is not created.
        RenderThreadWorker::ConditionalJobFunc render_thread_conditional_task = [this, tgt]() -> bool {
            try {
                if (!tgt.valid()) {
                    SPDLOG_ERROR("Scene capture target was destroyed between threads!");
                    GameThreadWorker::get().enqueue([this]() -> void {
                        this->in_flight_target = nullptr;
                        destroy_scene_capture();
                    });
                    return true;
                }
    
                if (sdk::UTexture::update_render_resource_offset_texture2d(tgt)) {
                    SPDLOG_INFO("Successfully updated render resource offset for scene capture target!");
    
                    if (auto rsrc = (sdk::FTextureRenderTargetResource*)tgt->get_resource(); rsrc != nullptr) {
                        const bool success = sdk::FTextureRenderTargetResource::update_render_target_vtable_offset(rsrc);
                        const auto frt = success ? rsrc->as_render_target() : nullptr;
    
                        if (frt != nullptr) {
                            sdk::FRenderTarget::update_offsets(frt);

                            if (frt->get_render_target_texture() == nullptr || *frt->get_render_target_texture() == nullptr) {
                                SPDLOG_WARN("Waiting for render target texture to be valid...");
                                return false;
                            }
    
                            hook_frt(frt);
    
                            RHIThreadWorker::get().enqueue([this, tgt]() -> void {
                                if (!tgt.valid()) {
                                    SPDLOG_ERROR("Scene capture target was destroyed between threads!");
                                    this->scene_capture_target_rhi_thread = nullptr;
                                    return;
                                }

                                this->scene_capture_target_rhi_thread = tgt;
                            });
                            
                            GameThreadWorker::get().enqueue([this, tgt]() -> void {
                                if (!tgt.valid()) {
                                    SPDLOG_ERROR("Scene capture target was destroyed between threads!");
                                    this->in_flight_target = nullptr;
                                    destroy_scene_capture();
                                    return;
                                }
                                
                                this->scene_capture_target = tgt;
                                this->in_flight_target = nullptr;
    
                                SPDLOG_INFO("Scene capture texture created!");
                            });
    
                            already_updated = true;
        
                            return true;
                        }
    
                        SPDLOG_WARN("Waiting for render target to be valid...");
    
                        return false; // Keep waiting until it works.
                    }
                } else {
                    SPDLOG_ERROR("Failed to update render resource offset for scene capture target!");
                }
            } catch (const std::exception& e) {
                SPDLOG_ERROR("[VRRenderTargetManager] Exception in create_scene_capture (offset lookup): {}", e.what());
                GameThreadWorker::get().enqueue([this]() -> void {
                    this->in_flight_target = nullptr;
                    destroy_scene_capture();
                });
                return true;
            } catch (...) {
                SPDLOG_ERROR("[VRRenderTargetManager] Unknown exception in create_scene_capture (offset lookup)!");
                GameThreadWorker::get().enqueue([this]() -> void {
                    this->in_flight_target = nullptr;
                    destroy_scene_capture();
                });
                return true;
            }

            return false;
        };

        RenderThreadWorker::ConditionalJobTimeoutFunc render_thread_on_timeout = [this]() {
            SPDLOG_ERROR("Timed out waiting for scene capture texture to be created!");
            GameThreadWorker::get().enqueue([this]() -> void {
                this->in_flight_target = nullptr;
                destroy_scene_capture();
            });
        };

        RenderThreadWorker::get().enqueue_conditional(render_thread_conditional_task, render_thread_on_timeout, std::chrono::seconds(2));
    
        SPDLOG_INFO("Waiting for scene capture texture to be created...");
    } else {
        this->in_flight_target = tgt;

        RenderThreadWorker::ConditionalJobFunc render_thread_conditional_task = [this, tgt]() -> bool {
            try {
                if (!tgt.valid()) {
                    SPDLOG_ERROR("Scene capture target was destroyed between threads!");
                    GameThreadWorker::get().enqueue([this]() -> void {
                        this->in_flight_target = nullptr;
                        destroy_scene_capture();
                    });
    
                    return true;
                }
    
                auto rsrc = (sdk::FTextureRenderTargetResource*)tgt->get_resource();
                auto frt = rsrc != nullptr ? rsrc->as_render_target() : nullptr;
                auto frttex = frt != nullptr ? frt->get_render_target_texture() : nullptr;
    
                // Wait until FRenderTarget is not null.
                if (frt == nullptr || frttex == nullptr || *frttex == nullptr) {
                    SPDLOG_WARN("Waiting for render target to be valid...");
                    return false;
                }
    
                hook_frt(frt);
    
                RHIThreadWorker::get().enqueue([this, tgt]() -> void {
                    if (!tgt.valid()) {
                        SPDLOG_ERROR("Scene capture target was destroyed between threads!");
                        this->scene_capture_target_rhi_thread = nullptr;
                        return;
                    }

                    this->scene_capture_target_rhi_thread = tgt;
                });
    
                GameThreadWorker::get().enqueue([this, tgt]() -> void {
                    if (!tgt.valid()) {
                        SPDLOG_ERROR("Scene capture target was destroyed between threads!");
                        this->in_flight_target = nullptr;
                        destroy_scene_capture();
                        return;
                    }
    
                    this->in_flight_target = nullptr;
                    this->scene_capture_target = tgt;
    
                    SPDLOG_INFO("Scene capture texture fully created!");
                });
    
                return true;
            } catch (const std::exception& e) {
                SPDLOG_ERROR("[VRRenderTargetManager] Exception in create_scene_capture: {}", e.what());
                GameThreadWorker::get().enqueue([this]() -> void {
                    this->in_flight_target = nullptr;
                    destroy_scene_capture();
                });
                return true;
            } catch (...) {
                SPDLOG_ERROR("[VRRenderTargetManager] Unknown exception in create_scene_capture!");
                GameThreadWorker::get().enqueue([this]() -> void {
                    this->in_flight_target = nullptr;
                    destroy_scene_capture();
                });
                return true;
            }
        };

        RenderThreadWorker::ConditionalJobTimeoutFunc render_thread_on_timeout = [this]() {
            SPDLOG_ERROR("Timed out waiting for scene capture texture to be created!");
            GameThreadWorker::get().enqueue([this]() -> void {
                this->in_flight_target = nullptr;
                destroy_scene_capture();
            });
        };

        RenderThreadWorker::get().enqueue_conditional(render_thread_conditional_task, render_thread_on_timeout, std::chrono::seconds(2));

        SPDLOG_INFO("Waiting for scene capture texture to be created...");
    }

    return true;
} catch (const std::exception& e) {
    SPDLOG_ERROR("[VRRenderTargetManager] Exception in create_scene_capture: {}", e.what());
    return false;
} catch (...) {
    SPDLOG_ERROR("[VRRenderTargetManager] Unknown exception in create_scene_capture!");
    return false;
}

// This is a very special fix for cases where engine modifications
// can add a second call to UpdateViewportRHI right before the place we expect it to get called
// The fact that they get called back-to-back over and over causes huge performance problems
// because the viewport texture keeps getting recreated over and over.
// This hook attempts to only allow the last call to UpdateViewportRHI inside of EnqueueBeginRenderFrame to do anything
// Usually there's only one call to UpdateViewportRHI inside of EnqueueBeginRenderFrame, but (very rarely) there can be two.
__declspec(noinline) void FFakeStereoRenderingHook::update_viewport_rhi_hook(void* viewport, size_t destroyed, size_t new_size_x, size_t new_size_y, size_t new_window_mode, size_t preferred_pixel_format) {
    auto call_orig = [&]() {
        g_hook->m_update_viewport_rhi_hook->get_original<void(*)(void*, size_t, size_t, size_t, size_t, size_t)>()(viewport, destroyed, new_size_x, new_size_y, new_window_mode, preferred_pixel_format);
    };

    SPDLOG_INFO_ONCE("UpdateViewportRHI (embedded): {:x}", (uintptr_t)_ReturnAddress());

    const auto hmd_active = VR::get()->is_hmd_active();
    static bool modified_use_separate_rt = false;

    if (!hmd_active) {
        if (modified_use_separate_rt) {
            modified_use_separate_rt = false;

            const auto rtm = g_hook->get_render_target_manager();

            if (rtm != nullptr) {
                if (const auto offset = rtm->get_viewport_force_separate_rt_offset()) {
                    SPDLOG_INFO_ONCE("Resetting bUseSeparateRenderTarget to false!");
                    auto& use_separate_rt = *(bool*)((uintptr_t)viewport + (*offset - 1));
                    use_separate_rt = false;
                }
            }
        }

        call_orig();
        return;
    }

    struct FunctionInfo {
        std::vector<uintptr_t> return_addrs{}; // in order of call
        size_t count{0};
    };

    static std::mutex mtx{};
    static std::unordered_map<uintptr_t, uintptr_t> functions_within{};
    static std::unordered_map<uintptr_t, FunctionInfo> function_infos{};

    {
        std::scoped_lock _{mtx};

        const auto return_addr = (uintptr_t)_ReturnAddress();
        auto function_within = functions_within.find(return_addr);

        if (function_within == functions_within.end()) {
            const auto result = utility::find_virtual_function_start(return_addr);

            if (result) {
                functions_within[return_addr] = *result;
            } else {
                functions_within[return_addr] = 0;
            }

            function_within = functions_within.find(return_addr);

            if (function_within->second != 0) {
                ++function_infos[function_within->second].count;
            }

            function_infos[function_within->second].return_addrs.push_back(return_addr);

            SPDLOG_INFO("Added new call of UpdateViewportRHI to function {:x} (count: {})", function_within->second, function_infos[function_within->second].count);
        }

        if (function_within->second == 0) {
            SPDLOG_INFO_ONCE("Could not find vfunc start for call of UpdateViewportRHI, calling original.");
            call_orig();
            return;
        }

        const auto& function_info = function_infos[function_within->second]; 

        // We only care about corrections where UpdateViewportRHI is called more than once in the same function.
        if (function_info.count <= 1 || function_info.return_addrs.empty()) {
            call_orig();
            return;
        }

        if (!g_hook->m_rendertarget_manager_embedded_in_stereo_device) {
            const auto rtm = g_hook->get_render_target_manager();

            if (rtm != nullptr) {
                if (const auto offset = rtm->get_viewport_force_separate_rt_offset()) {
                    auto& should_force_separate_rt = *(bool*)((uintptr_t)viewport + *offset);
                    auto& use_separate_rt = *(bool*)((uintptr_t)viewport + (*offset - 1));

                    if (!should_force_separate_rt) {
                        SPDLOG_INFO_ONCE("UpdateViewportRHI was called without should_force_separate_rt being set to true, skipping.");
                        should_force_separate_rt = true;
                        use_separate_rt = true;
                        modified_use_separate_rt = true;
                        return; // NO!!!!!!!!!!!!!!!!!!!
                    }
                }
            }
        } else {      
            // We only want the last function to be called.
            // We don't need to call the original here because it will get called by the last function.
            // if we call the original here, it will cause performance issues.
            if (function_info.return_addrs.back() != return_addr) {
                return;
            }
        }
    }

    
    if (!g_hook->m_rendertarget_manager_embedded_in_stereo_device) {
        call_orig();
        return;
    }

    auto& rtm = g_hook->get_embedded_rtm();

    static std::chrono::steady_clock::time_point last_time_hmd_active{};
    static bool hmd_was_active = false;
    bool should_call_orig = false;

    if (hmd_active && !hmd_was_active) {
        last_time_hmd_active = std::chrono::steady_clock::now();
        hmd_was_active = true;
    } else if (!hmd_active) {
        hmd_was_active = false;
        should_call_orig = true;
    }

    if (hmd_active) {
        should_call_orig = std::chrono::steady_clock::now() - last_time_hmd_active <= std::chrono::milliseconds(2000);
        //should_call_orig = should_call_orig || (std::chrono::steady_clock::now() - rtm.last_time_needed_hmd_reallocate <= std::chrono::milliseconds(2000));
    }

    if (should_call_orig) {
        rtm.should_use_separate_rt_called = false;
        rtm.need_reallocate_viewport_render_target_called = false;
        call_orig();
        return;
    }

    if (!rtm.should_use_separate_rt_called) {
        SPDLOG_INFO_ONCE("Skipping UpdateViewportRHI (embedded) because ShouldUseSeparateRenderTarget() was not called!");
        return; // Do not call at all.
    }

    if (!rtm.need_reallocate_viewport_render_target_called) {
        const auto need_reallocate = g_hook->get_render_target_manager()->need_reallocate_view_target(*(sdk::FViewport*)viewport);

        if (!need_reallocate) {
            SPDLOG_INFO_ONCE("Skipping UpdateViewportRHI (embedded) because NeedReallocateViewportRenderTarget() was not called and we don't need to reallocate anyway!");
            rtm.should_use_separate_rt_called = false;
            return; // Do not call at all.
        }

        SPDLOG_INFO_ONCE("We need to reallocate the viewport render target even though NeedReallocateViewportRenderTarget() was not called!");
        //rtm.last_time_needed_hmd_reallocate = std::chrono::steady_clock::now();
    }

    call_orig();
    rtm.should_use_separate_rt_called = false;
    rtm.need_reallocate_viewport_render_target_called = false;
}

void FFakeStereoRenderingHook::attempt_hook_update_viewport_rhi(uintptr_t return_address) {
    if (/*!m_rendertarget_manager_embedded_in_stereo_device ||*/ m_special_detected || m_attempted_hook_update_viewport_rhi) {
        return;
    }

    m_attempted_hook_update_viewport_rhi = true;

    if (m_update_viewport_rhi_hook == nullptr) {
        SPDLOG_INFO("Attempting to hook UpdateViewportRHI...");

        const auto init_dynamic_rhi = utility::find_virtual_function_start(return_address);

        if (init_dynamic_rhi) {
            SPDLOG_INFO("Found InitDynamicRHI: {:x}", *init_dynamic_rhi);

            const auto init_dynamic_rhi_ptr = utility::scan_ptr(*utility::get_module_within(*init_dynamic_rhi), *init_dynamic_rhi);
            if (!init_dynamic_rhi_ptr) {
                SPDLOG_ERROR("Failed to find InitDynamicRHI pointer!");
                return;
            }

            const auto update_viewport_rhi_ptr = *init_dynamic_rhi_ptr - (sizeof(void*) * 2);

            if (*(void**)update_viewport_rhi_ptr == nullptr || IsBadReadPtr(*(void**)update_viewport_rhi_ptr, sizeof(void*))) {
                SPDLOG_ERROR("Failed to find UpdateViewportRHI!");
                return;
            }

            // Make sure this is no displacement reference to this. This can mean we accidentally found the vtable for IViewportRenderTargetProvider
            // The vfunc pointer should be in the middle of the vtable, not the start.
            if (utility::scan_displacement_reference(*utility::get_module_within(*init_dynamic_rhi), update_viewport_rhi_ptr)) {
                SPDLOG_ERROR("Found displacement reference to UpdateViewportRHI, this is probably the vtable for IViewportRenderTargetProvider, aborting!");
                return;
            }

            m_update_viewport_rhi_hook = std::make_unique<PointerHook>((void**)update_viewport_rhi_ptr, &update_viewport_rhi_hook);
        } else {
            SPDLOG_ERROR("Failed to find InitDynamicRHI, cannot hook UpdateViewportRHI!");
        }
    }
}

bool VRRenderTargetManager_Base::allocate_render_target_texture(uintptr_t return_address, FTexture2DRHIRef* tex, FTexture2DRHIRef* shader_resource) {
    this->texture_hook_ref = tex;
    this->shader_resource_hook_ref = shader_resource;
    this->allocate_texture_called = true;

    if (!this->set_up_texture_hook) {
        ZoneScopedN("VRRenderTargetManager_Base::allocate_render_target_texture initialization");
        SPDLOG_INFO("AllocateRenderTargetTexture retaddr: {:x}", return_address);

        g_hook->attempt_hook_update_viewport_rhi(return_address);

        SPDLOG_INFO("Scanning for call instr...");

        bool next_call_is_not_the_right_one = false;

        auto is_string_nearby = [](uintptr_t addr, std::wstring_view str) {
            const auto addr_module = utility::get_module_within(addr);
            if (!addr_module) {
                return false;
            }

            const auto module_size = utility::get_module_size(*addr_module);
            const auto module_end = (uintptr_t)*addr_module + *module_size - 0x1000;

            // Find all possible strings, not just the first one
            for (auto str_addr = utility::scan_string(*addr_module, str.data(), true); 
                str_addr.has_value(); 
                str_addr = utility::scan_string(*str_addr + 1, (module_end - (*str_addr + 1)), str.data(), true)) 
            {
                // Scan for ALL references to this string
                for (auto string_ref = utility::scan_displacement_reference(*addr_module, (uintptr_t)*str_addr);
                    string_ref.has_value();
                    string_ref = utility::scan_displacement_reference(*string_ref + 1, (module_end - (*string_ref + 1)), (uintptr_t)*str_addr))
                {
                    const auto string_ref_func_start = utility::find_function_start((uintptr_t)*string_ref);
                    const auto return_addr_func_start = utility::find_function_start(addr);

                    SPDLOG_INFO("String ref func start: {:x}", (uintptr_t)*string_ref_func_start);
                    SPDLOG_INFO("Return addr func start: {:x}", (uintptr_t)*return_addr_func_start);

                    if (string_ref_func_start && return_addr_func_start && *string_ref_func_start == *return_addr_func_start) {
                        return true;
                    }
                }
            }

            return false;
        };

        // This string is present in UE5 (>= 5.1) and used when using texture descriptors to create textures.
        // that means this is UE5 and the function will take a texture descriptor instead of a bunch of arguments.
        if (is_string_nearby(return_address, L"BufferedRT")) {
            SPDLOG_INFO("Found string ref for BufferedRT, this is UE5!");
            this->is_using_texture_desc = true;
            this->is_version_greq_5_1 = true;

            if (is_ue57_dx11_backend()) {
                SPDLOG_WARN_ONCE("Skipping UE 5.7 D3D11 BufferedRT texture-create replay hook; using the engine allocation path");
                this->set_up_texture_hook = true;
                return false;
            }
        }

        // Present in a specific game or game(s), somewhere around 4.8-4.12 (?)
        // indicates that texture descriptors are being used.
        if (is_string_nearby(return_address, L"SceneViewBuffer")) {
            SPDLOG_INFO("Found string ref for SceneViewBuffer, texture descriptors are being used!");
            this->is_using_texture_desc = true;
            this->is_version_greq_5_1 = false;

            next_call_is_not_the_right_one = true; // not seen a case where this isn't true (yet)
        }

        // Now, we need to emulate from where AllocateRenderTargetTexture returns from
        // we will set RAX to false, to get the control flow correct
        // and then keep emulating until we hit the call we want
        // Previously, we were using just straight linear disassembly to do this, and it mostly worked
        // but in one game, there was an unconditional branch after the call instead of flowing
        // directly into the next instruction.
        auto emu = utility::ShemuContext{*utility::get_module_within(return_address)};

        emu.ctx->Registers.RegRax = 0;
        emu.ctx->Registers.RegRip = (ND_UINT64)return_address;
        emu.ctx->MemThreshold = 100;

        const std::vector<std::string> bad_patterns_before_call = {
            "B2 32", // mov dl, 32h, (seen in UE5 debug/dev builds)
            "B2 2A", // mov dl, 2Ah, (seen in UE4.23 debug/dev builds)
            "B2 2B", // mov dl, 2Bh, (seen in UE4.25 debug/dev builds)
            "BA 2F 00 00 00", // mov edx, 2Fh (seen in UE5 debug/dev builds)
            "F6 85 ? ? ? ? 05", // test byte ptr [rbp+?], 5 (seen in UE5 debug/dev builds)
        };

        auto is_probable_ue57_texture_desc_prepare = [](uintptr_t fn) {
            return utility::scan(fn, 0x80, "0F B6 42 32").has_value()
                && utility::scan(fn, 0x80, "48 8B 42 24").has_value()
                && utility::scan(fn, 0x80, "48 83 C2 38").has_value();
        };

        struct DirectCallInfo {
            uintptr_t callsite{};
            uintptr_t target{};
            uint8_t length{};
        };

        auto find_next_direct_calls = [](uintptr_t start, size_t span, size_t max_calls) {
            std::vector<DirectCallInfo> results{};

            utility::exhaustive_decode((uint8_t*)start, span, [&](const utility::ExhaustionContext& decode_ctx) -> utility::ExhaustionResult {
                if (std::string_view{decode_ctx.instrux.Mnemonic}.starts_with("CALL") && *(uint8_t*)decode_ctx.addr == 0xE8) {
                    results.emplace_back(DirectCallInfo{
                        .callsite = decode_ctx.addr,
                        .target = utility::calculate_absolute(decode_ctx.addr + 1),
                        .length = (uint8_t)decode_ctx.instrux.Length
                    });

                    if (results.size() >= max_calls) {
                        return utility::ExhaustionResult::BREAK;
                    }
                }

                return utility::ExhaustionResult::CONTINUE;
            });

            return results;
        };

        while(true) {
            if (emu.ctx->InstructionsCount > 200) {
                SPDLOG_WARN("Emulated too many instructions without finding the call, aborting!");
                break;
            }

            const auto ip = emu.ctx->Registers.RegRip;
            const auto bytes = (uint8_t*)ip;
            const auto decoded = utility::decode_one((uint8_t*)ip);

            if (ip != 0) {
                for (const auto& pattern : bad_patterns_before_call) {
                    if (utility::scan(ip, 100, pattern).value_or(0) == ip) {
                        SPDLOG_INFO("Found bad pattern before call, skipping next call: {:x} ({})", ip, pattern);
                        next_call_is_not_the_right_one = true;
                        break;
                    }
                }
            }
            
            if (!next_call_is_not_the_right_one) try {
                const auto addr = utility::resolve_displacement(ip);

                if (addr && !IsBadReadPtr((void*)*addr, 12)) {
                    if (std::wstring_view{(const wchar_t*)*addr}.starts_with(L"BufferedRT")) {
                        this->is_using_texture_desc = true;
                        this->is_version_greq_5_1 = true;

                        SPDLOG_INFO("Found usage of string \"BufferedRT\" while analyzing AllocateRenderTargetTexture!");
                    } else if (std::string_view{(const char*)*addr}.starts_with("IsInRenderingThread") && std::string_view{decoded->Mnemonic}.starts_with("LEA") && decoded->Operands[0].Type == ND_OP_REG && decoded->Operands[0].Info.Register.Reg == NDR_RCX) {
                        SPDLOG_INFO("Found usage of string \"IsInRenderingThread\" while analyzing AllocateRenderTargetTexture, skipping next call!");
                        next_call_is_not_the_right_one = true;
                    }
                }
            } catch(...) {

            }

            // make sure we are not emulating any instructions that write to memory
            // so we can just set the IP to the next instruction
            if (decoded) {
                const auto is_call = std::string_view{decoded->Mnemonic}.starts_with("CALL");

                if (decoded->MemoryAccess & ND_ACCESS_ANY_WRITE || is_call) {
                    // We are looking for the call instruction
                    // This instruction calls RHICreateTargetableShaderResource2D(TexSizeX, TexSizeY, SceneTargetFormat, 1, TexCreate_None,
                    // TexCreate_RenderTargetable, false, CreateInfo, BufferedRTRHI, BufferedSRVRHI); Which sets up the BufferedRTRHI and
                    // BufferedSRVRHI variables.
                    if (is_call && !next_call_is_not_the_right_one && bytes[0] == 0xE8) try {
                        // Analyze some of the instructions inside the call first
                        // If it has a mov eax, 0x800, then returns, we can skip this function
                        const auto fn = utility::calculate_absolute(ip + 1);
                        SPDLOG_INFO("Analyzing call at {:x} to {:x}", ip, fn);

                        if (is_ue57_dx11_backend() && this->is_version_greq_5_1 &&
                            is_probable_ue57_dx11_texture_desc_prepare_function(fn))
                        {
                            SPDLOG_INFO("Skipping UE 5.7 D3D11 texture-desc prepare helper at {:x}; continuing to the real texture-create wrapper", fn);
                            next_call_is_not_the_right_one = true;
                        } else if (g_framework->is_dx12() && is_ue_5_7_or_newer()) {
                            const auto next_calls = find_next_direct_calls((uintptr_t)ip + decoded->Length, 0x80, 2);

                            if (next_calls.size() >= 2) {
                                this->texture_desc_prepare_func = fn;
                                this->texture_create_wrapper_func = next_calls[0].target;
                                this->texture_finalize_func = next_calls[1].target;
                                this->texture_release_func = 0;
                                this->texture_extract_func = 0;
                                this->texture_finalize_callsite = 0;
                                this->texture_extract_callsite = 0;

                                SPDLOG_INFO("Resolved UE 5.7 texture-desc helper {:x}, wrapper {:x}, and finalize helper {:x}",
                                    this->texture_desc_prepare_func,
                                    this->texture_create_wrapper_func,
                                    this->texture_finalize_func);

                                const auto wrapper_call_ip = next_calls[0].callsite;
                                const auto finalize_post_call = next_calls[1].callsite + next_calls[1].length;

                                this->texture_create_insn_bytes.resize(next_calls[0].length);
                                memcpy(this->texture_create_insn_bytes.data(), (void*)wrapper_call_ip, next_calls[0].length);

                                auto texture_hook_result = safetyhook::MidHook::create((void*)finalize_post_call, +[](safetyhook::Context& ctx) -> void {
                                    VRRenderTargetManager::texture_hook_callback(ctx, false);
                                });

                                if (!texture_hook_result.has_value()) {
                                    const auto e = texture_hook_result.error();

                                    if (e.type == safetyhook::MidHook::Error::BAD_ALLOCATION) {
                                        SPDLOG_ERROR("Failed to create UE 5.7 post texture hook: BAD_ALLOCATION: {}", (uint8_t)e.allocator_error);
                                    } else {
                                        SPDLOG_ERROR("Failed to create UE 5.7 post texture hook: BAD_INLINE_HOOK: {}", (uint8_t)e.inline_hook_error.type);
                                    }
                                } else {
                                    this->texture_hook = std::move(texture_hook_result.value());
                                }

                                auto pre_texture_hook_result = safetyhook::MidHook::create((void*)wrapper_call_ip, +[](safetyhook::Context& ctx) -> void {
                                    VRRenderTargetManager::pre_texture_hook_callback(ctx, false);
                                });

                                if (!pre_texture_hook_result.has_value()) {
                                    const auto e = pre_texture_hook_result.error();

                                    if (e.type == safetyhook::MidHook::Error::BAD_ALLOCATION) {
                                        SPDLOG_ERROR("Failed to create UE 5.7 pre texture hook: BAD_ALLOCATION: {}", (uint8_t)e.allocator_error);
                                    } else {
                                        SPDLOG_ERROR("Failed to create UE 5.7 pre texture hook: BAD_INLINE_HOOK: {}", (uint8_t)e.inline_hook_error.type);
                                    }
                                } else {
                                    this->pre_texture_hook = std::move(pre_texture_hook_result.value());
                                }

                                this->is_pre_texture_call_e8 = true;
                                this->set_up_texture_hook = true;
                                return false;
                            }

                            if (is_probable_ue57_texture_desc_prepare(fn)) {
                                SPDLOG_INFO("Detected UE 5.7 texture-desc prepare helper at {:x}, but failed to resolve the wrapper/finalize sequence", fn);
                            }
                        } else if (auto result = utility::scan(fn, 10, "41 B8 30 00 00 00"); result.has_value() && *result == fn) {
                            SPDLOG_INFO("First instruction is a mov r8d, 30h, skipping this call!");
                            next_call_is_not_the_right_one = true;
                        } else if (auto result = utility::scan(fn, 50, "B8 00 08 00 00 C3"); result.has_value()) {
                            SPDLOG_INFO("First few instructions are a mov eax, 800h, ret, skipping this call!");
                            next_call_is_not_the_right_one = true;
                        } else if (this->is_version_greq_5_1) { // Limiting the scope of this to newer UE5 versions so we don't potentially break older versions
                            const auto module_fn_within = utility::get_module_within(fn);
                            const auto next_insn = (uint8_t*)(ip + decoded->Length);

                            // Seen on UE5.3.2 development builds
                            if (auto result = utility::scan_disasm(fn, 15, "BD 01 00 00 00"); result.has_value()) {
                                // This string is not unicode
                                if (utility::find_string_reference_in_path(fn, "InGPUMask != 0", false).has_value()) {
                                    SPDLOG_INFO("Found InGPUMask != 0 string within the function and mov ebp, 1, skipping this call!");
                                    next_call_is_not_the_right_one = true;
                                }
                            } else if (next_insn[0] == 0x84 && next_insn[1] == 0xC0) { // test al, al
                                if (auto ref = utility::find_string_reference_in_path((uintptr_t)next_insn, "IsInRenderingThread()", false); ref.has_value()) {
                                    if (ref->addr > (uintptr_t)next_insn && ref->addr - (uintptr_t)next_insn < 30) {
                                        SPDLOG_INFO("Found IsInRenderingThread() instead of the function we want, skipping this call!");
                                        next_call_is_not_the_right_one = true;
                                    }
                                }
                            } else if (utility::find_pattern_in_path((uint8_t*)fn, 30, true, "66 41 C7 40 34 00 FF")) {
                                SPDLOG_INFO("Found 66 41 C7 40 34 00 FF pattern within the function, skipping this call!");
                                next_call_is_not_the_right_one = true;
                            } else {
                                // Check how many instructions are in the call. If there's <= 30 AND there's no call/jmp in it, this is not the right one
                                size_t insn_count = 0;
                                bool encountered_branch = false;
                                utility::exhaustive_decode((uint8_t*)fn, 200, [&](const utility::ExhaustionContext& ctx) -> utility::ExhaustionResult {
                                    if (std::string_view{ctx.instrux.Mnemonic}.starts_with("CALL") || std::string_view{ctx.instrux.Mnemonic}.starts_with("JMP")) {
                                        encountered_branch = true;
                                        return utility::ExhaustionResult::BREAK;
                                    }

                                    return utility::ExhaustionResult::CONTINUE;
                                });

                                if (insn_count <= 30 && !encountered_branch) {
                                    SPDLOG_INFO("Function at {:x} only has {} instructions and no calls/branches, skipping this call!", fn, insn_count);
                                    next_call_is_not_the_right_one = true;
                                }
                            }
                        }
                    } catch(...) {
                        SPDLOG_INFO("Failed to analyze call at {:x}", ip);
                    }

                    if (is_call && !next_call_is_not_the_right_one && bytes[0] == 0xFF && bytes[1] == 0x15) {
                        // well this definitely is not the right one, indirect calls have never called the function we wanted (I think)
                        SPDLOG_INFO("Found indirect call @ {:x}, skipping", ip);
                        next_call_is_not_the_right_one = true;
                    }

                    if (is_call && !next_call_is_not_the_right_one) {
                        const auto post_call = (uintptr_t)ip + decoded->Length;
                        SPDLOG_INFO("AllocateRenderTargetTexture post_call: {:x}, rel {:x}", post_call, post_call - (uintptr_t)*utility::get_module_within((void*)post_call));

                        if (*(uint8_t*)ip == 0xE8) {
                            SPDLOG_INFO("E8 call found!");
                            this->is_pre_texture_call_e8 = true;
                        } else {
                            SPDLOG_INFO("E8 call not found, assuming register call!");
                        }

                        // So we can call the original texture create function again.
                        this->texture_create_insn_bytes.resize(decoded->Length);
                        memcpy(this->texture_create_insn_bytes.data(), (void*)ip, decoded->Length);

                        if (this->is_version_greq_5_1 && !this->is_pre_texture_call_e8 && bytes[-7] == 0x48 && bytes[-6] == 0x8B && bytes[-5] == 0x0D && bytes[0] == 0xFF && bytes[1] == 0x94) {
                            // Scan forward for a similar one and also hook that
                            auto second_call = utility::scan((uintptr_t)ip + decoded->Length, 0x60, "48 8B 0D ? ? ? ? FF 94 ? ? ? ? ?");

                            if (second_call) {
                                // So we can call the original texture create function again.
                                this->texture_create_insn_bytes2.resize(decoded->Length);
                                memcpy(this->texture_create_insn_bytes2.data(), (void*)(*second_call + 7), decoded->Length);

                                SPDLOG_INFO("Found second call at {:x}", *second_call);
                                auto post_second_call = *second_call + 7 + decoded->Length;
                                //auto texture_hook_result = safetyhook::MidHook::create((void*)post_second_call, &VRRenderTargetManager::texture_hook_callback);
                                auto texture_hook_result = safetyhook::MidHook::create((void*)post_second_call, +[](safetyhook::Context& ctx) -> void {
                                    VRRenderTargetManager::texture_hook_callback(ctx, true);
                                });

                                if (!texture_hook_result.has_value()) {
                                    const auto e = texture_hook_result.error();

                                    if (e.type == safetyhook::MidHook::Error::BAD_ALLOCATION) {
                                        SPDLOG_ERROR("Failed to create post second texture hook: BAD_ALLOCATION: {}", (uint8_t)e.allocator_error);
                                    } else {
                                        SPDLOG_ERROR("Failed to create post second texture hook: BAD_INLINE_HOOK: {}", (uint8_t)e.inline_hook_error.type);
                                    }
                                } else {
                                    this->texture_hook2 = std::move(texture_hook_result.value());
                                    SPDLOG_INFO("Successfully created second texture hook!");
                                }

                                auto pre_second_call = *second_call + 7;
                                //auto pre_texure_hook_result = safetyhook::MidHook::create((void*)pre_second_call, &VRRenderTargetManager::pre_texture_hook_callback);
                                auto pre_texure_hook_result = safetyhook::MidHook::create((void*)pre_second_call, +[](safetyhook::Context& ctx) -> void {
                                    VRRenderTargetManager::pre_texture_hook_callback(ctx, true);
                                });

                                if (!pre_texure_hook_result.has_value()) {
                                    const auto e = pre_texure_hook_result.error();

                                    if (e.type == safetyhook::MidHook::Error::BAD_ALLOCATION) {
                                        SPDLOG_ERROR("Failed to create pre second texture hook: BAD_ALLOCATION: {}", (uint8_t)e.allocator_error);
                                    } else {
                                        SPDLOG_ERROR("Failed to create pre second texture hook: BAD_INLINE_HOOK: {}", (uint8_t)e.inline_hook_error.type);
                                    }
                                } else {
                                    this->pre_texture_hook2 = std::move(pre_texure_hook_result.value());
                                    SPDLOG_INFO("Successfully created second pre texture hook!");
                                }
                            } else {
                                SPDLOG_INFO("Second call not detected! Continuing...");
                            }
                        }

                        //auto texture_hook_result = safetyhook::MidHook::create((void*)post_call, &VRRenderTargetManager::texture_hook_callback);
                        auto texture_hook_result = safetyhook::MidHook::create((void*)post_call, +[](safetyhook::Context& ctx) -> void {
                            VRRenderTargetManager::texture_hook_callback(ctx, false);
                        });

                        if (!texture_hook_result.has_value()) {
                            const auto e = texture_hook_result.error();

                            if (e.type == safetyhook::MidHook::Error::BAD_ALLOCATION) {
                                SPDLOG_ERROR("Failed to create post texture hook: BAD_ALLOCATION: {}", (uint8_t)e.allocator_error);
                            } else {
                                SPDLOG_ERROR("Failed to create post texture hook: BAD_INLINE_HOOK: {}", (uint8_t)e.inline_hook_error.type);
                            }
                        } else {
                            this->texture_hook = std::move(texture_hook_result.value());
                        }

                        //auto pre_texure_hook_result = safetyhook::MidHook::create((void*)ip, &VRRenderTargetManager::pre_texture_hook_callback);
                        auto pre_texure_hook_result = safetyhook::MidHook::create((void*)ip, +[](safetyhook::Context& ctx) -> void {
                            VRRenderTargetManager::pre_texture_hook_callback(ctx, false);
                        });

                        if (!pre_texure_hook_result.has_value()) {
                            const auto e = pre_texure_hook_result.error();

                            if (e.type == safetyhook::MidHook::Error::BAD_ALLOCATION) {
                                SPDLOG_ERROR("Failed to create pre texture hook: BAD_ALLOCATION: {}", (uint8_t)e.allocator_error);
                            } else {
                                SPDLOG_ERROR("Failed to create pre texture hook: BAD_INLINE_HOOK: {}", (uint8_t)e.inline_hook_error.type);
                            }
                        } else {
                            this->pre_texture_hook = std::move(pre_texure_hook_result.value());
                        }
                        this->set_up_texture_hook = true;

                        return false;
                    }

                    SPDLOG_INFO("Skipping write to memory instruction at {:x} ({:x} bytes, landing at {:x})", ip, decoded->Length, ip + decoded->Length);
                    emu.ctx->Registers.RegRip += decoded->Length;
                    emu.ctx->Instruction = *decoded; // pseudo-emulate the instruction
                    ++emu.ctx->InstructionsCount;

                    if (is_call) {
                        next_call_is_not_the_right_one = false;
                    }
                } else if (emu.emulate() != SHEMU_SUCCESS) { // only emulate the non-memory write instructions
                    SPDLOG_INFO("Emulation failed at {:x} ({:x} bytes, landing at {:x})", ip, decoded->Length, ip + decoded->Length);
                    // instead of just adding it onto the RegRip, we need to use the ip we had previously from the decode
                    // because the emulator can move the instruction pointer after emulate() is called
                    emu.ctx->Registers.RegRip = ip + decoded->Length;
                    continue;
                }
            } else {
                break;
            }
        }

        SPDLOG_ERROR("Failed to find call instruction!");
    }

    return false;
}

bool VRRenderTargetManager::AllocateRenderTargetTexture(uint32_t Index, uint32_t SizeX, uint32_t SizeY, uint8_t Format, uint32_t NumMips,
    ETextureCreateFlags Flags, ETextureCreateFlags TargetableTextureFlags, FTexture2DRHIRef& OutTargetableTexture,
    FTexture2DRHIRef& OutShaderResourceTexture, uint32_t NumSamples) {
    // So, what's happening here is instead of using this method
    // to actually create our textures, we are going to
    // get the return address, scan forward for the next call instruction
    // and insert a midhook after the next call instruction.
    // The purpose of this is to get the texture that is being created
    // by the engine itself after we return false from this function.
    // When we return false from this function, it indicates
    // to the engine that we are letting the engine itself
    // create the texture, rather than us creating it ourselves.
    // This should allow maximum compatibility across engine versions.
    /*const auto dynamic_rhi = *(uintptr_t*)((uintptr_t)sdk::get_ue_module(L"Engine") + 0x3309C50);
    const auto command_list = (uintptr_t)sdk::get_ue_module(L"Engine") + 0x330AE70;
    struct {
        void* bulk_data{nullptr};
        void* rsrc_array{nullptr};

        struct {
            uint32_t color_binding{1};
            float color[4]{};
        } clear_value_binding;

        uint32_t gpu_mask{1};
        bool without_native_rsrc{false};
        const TCHAR* debug_name{"BufferedRT"};
        uint32_t extended_data{};
    } create_info;

    const void (*RHICreateTexture2D_RenderThread)(
        uintptr_t rhi,
        FTexture2DRHIRef* out,
        uintptr_t command_list,
        uint32_t w,
        uint32_t h,
        uint8_t format,
        uint32_t mips,
        uint32_t samples,
        ETextureCreateFlags flags,
        void* create_info) = (*(decltype(RHICreateTexture2D_RenderThread)**)dynamic_rhi)[178];

    *(uint64_t*)&TargetableTextureFlags |= (uint64_t)ETextureCreateFlags::ShaderResource | (uint64_t)Flags;
    RHICreateTexture2D_RenderThread(dynamic_rhi, &OutTargetableTexture, command_list, SizeX, SizeY, 2, NumMips, NumSamples, TargetableTextureFlags, &create_info);

    const auto size = g_framework->is_dx11() ? g_framework->get_d3d11_rt_size() : g_framework->get_d3d12_rt_size();
    RHICreateTexture2D_RenderThread(dynamic_rhi, &OutShaderResourceTexture, command_list, (uint32_t)size.x, (uint32_t)size.y, 2, NumMips, NumSamples, TargetableTextureFlags, &create_info);

    this->render_target = OutTargetableTexture.texture;
    this->ui_target = OutShaderResourceTexture.texture;

    OutShaderResourceTexture.texture = OutTargetableTexture.texture;*/

    m_last_allocate_render_target_return_address = (uintptr_t)_ReturnAddress();
    const auto relative_allocate_render_target_return_address =
        m_last_allocate_render_target_return_address - (uintptr_t)*utility::get_module_within((void*)m_last_allocate_render_target_return_address);

    if (g_framework->is_dx12() && shf_is_current_game()) {
        SPDLOG_INFO_EVERY_N_SEC(2, "[SHf] AllocateRenderTargetTexture summary last_caller={:x}", relative_allocate_render_target_return_address);
    } else if (is_ue_5_1_dx12_backend()) {
        ue51_note_rt_allocation(relative_allocate_render_target_return_address);
        SPDLOG_INFO_EVERY_N_SEC(2, "[UE5.1][RTChurn] AllocateRenderTargetTexture summary last_caller={:x}", relative_allocate_render_target_return_address);
    } else {
        SPDLOG_INFO("AllocateRenderTargetTexture called from: {:x}", relative_allocate_render_target_return_address);
    }

    // So, if CalculateRenderTargetSize was *never* called before this function
    // that means we have the virtual index of this function wrong, and we must swap the vtable out.
    // also, if this function was called very close to NeedReallocateDepthTexture, that also means
    // the virtual index is wrong, and we must swap the vtable out.
    const auto is_incorrect_vtable = 
        m_last_calculate_render_size_return_address == 0 ||
        m_last_allocate_render_target_return_address - m_last_needs_reallocate_depth_texture_return_address <= 0x200;

    if (is_incorrect_vtable) {
        // oh no this is the wrong vtable!!!! we need to fix it  nOW!!!
        SPDLOG_INFO("AllocateRenderTargetTexture called instead of AllocateDepthTexture! Fixing...");
        SPDLOG_INFO("Switching to old render target manager! Incorrect function called!");
        //g_hook->switch_to_old_rendertarget_manager();

        // Do a switcharoo on the vtable of this object to the old one because we will crash if we don't.
        // I've decided against actually switching the entire object over in favor of just vtable
        // swapping for now even though it's kind of a hack.
        const auto fake_object = std::make_unique<VRRenderTargetManager_418>();
        *(void**)this = *(void**)fake_object.get();

        return false;
    }

    this->depth_analysis_passed = true;

    return this->allocate_render_target_texture((uintptr_t)_ReturnAddress(), &OutTargetableTexture, &OutShaderResourceTexture);

    //return true;
}

bool VRRenderTargetManager::AllocateRenderTargetTextures(uint32_t SizeX, uint32_t SizeY, uint8_t Format, uint32_t NumLayers,
    ETextureCreateFlags Flags, ETextureCreateFlags TargetableTextureFlags, TArray<FTexture2DRHIRef>& OutTargetableTextures,
    TArray<FTexture2DRHIRef>& OutShaderResourceTextures, uint32_t NumSamples)
{
    SPDLOG_INFO_ONCE("VRRenderTargetManager::AllocateRenderTargetTextures called!");

    // Keep the engine on the deprecated single-texture allocation path for now.
    // UEVR's 5.7 UI separation still depends on analyzing and midhooking the real
    // texture creation sequence that happens after this returns false.
    return false;
}

bool VRRenderTargetManager_418::AllocateRenderTargetTexture(uint32_t Index, uint32_t SizeX, uint32_t SizeY, uint8_t Format, uint32_t NumMips, uint32_t Flags,
        uint32_t TargetableTextureFlags, FTexture2DRHIRef& OutTargetableTexture, FTexture2DRHIRef& OutShaderResourceTexture,
        uint32_t NumSamples) 
{
    return this->allocate_render_target_texture((uintptr_t)_ReturnAddress(), &OutTargetableTexture, &OutShaderResourceTexture);
}

bool VRRenderTargetManager_Special::AllocateRenderTargetTexture(uint32_t Index, uint32_t SizeX, uint32_t SizeY, uint8_t Format, uint32_t NumMips,
    ETextureCreateFlags Flags, ETextureCreateFlags TargetableTextureFlags, FTexture2DRHIRef& OutTargetableTexture,
    FTexture2DRHIRef& OutShaderResourceTexture, uint32_t NumSamples) 
{
    return this->allocate_render_target_texture((uintptr_t)_ReturnAddress(), &OutTargetableTexture, &OutShaderResourceTexture);
}
