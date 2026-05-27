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
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <unordered_map>

#include <Windows.h>
#include <safetyhook.hpp>
#include <utility/Module.hpp>
#include <spdlog/spdlog.h>

#include "Sn2RDGPassHook.hpp"
#include "Sn2MaterialNameHook.hpp"
#include "Sn2VsmUbClampPatch.hpp"

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

// Original signature: void AddMeshBatch(FBasePassMeshProcessor* this,
//                                       const FMeshBatch& mesh,
//                                       uint64_t batch_element_mask,
//                                       const FPrimitiveSceneProxy* primitive,
//                                       int32 stencil_value)
static void __fastcall add_mesh_batch_trampoline(
    void* this_ptr,
    const void* mesh_ptr,
    uint64_t batch_element_mask,
    const void* primitive_proxy,
    int32_t stencil_value)
{
    // Pull the material proxy out of the mesh batch BEFORE the original
    // (the field is immutable for the duration of the call).
    uintptr_t material_proxy = 0;
    if (mesh_ptr != nullptr) {
        material_proxy = *reinterpret_cast<const uintptr_t*>(
            reinterpret_cast<const uint8_t*>(mesh_ptr) + k_fmeshbatch_material_render_proxy_offset);
    }

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
                "[SN2-MaterialSeen] proxy=0x{:x} mesh=0x{:x} primitive=0x{:x} stencil={} batch_mask=0x{:x}",
                material_proxy,
                reinterpret_cast<uintptr_t>(mesh_ptr),
                reinterpret_cast<uintptr_t>(primitive_proxy),
                stencil_value,
                batch_element_mask);
        }
    }

    // Call original.
    g_add_mesh_batch_hook.call<void>(this_ptr, mesh_ptr, batch_element_mask, primitive_proxy, stencil_value);
}

static bool install_material_name_hook() {
    if (!sn2_material_hook::env_enabled()) {
        SPDLOG_INFO("[SN2-MaterialHook] disabled (UEVR_SN2_MATERIAL_HOOK not set)");
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
// Public entry point: call once during UEVR startup, after the executable's
// renderer modules are loaded (i.e. after D3D12CreateDevice).
// ============================================================================
void install_all() {
    static std::atomic<bool> s_installed{false};
    bool expected = false;
    if (!s_installed.compare_exchange_strong(expected, true)) return;
    install_rdg_pass_hook();
    install_material_name_hook();
    install_vsm_ub_clamp_patch();
    install_uwe_underwater_pass_probe();
    install_fog_slot_probe();
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
