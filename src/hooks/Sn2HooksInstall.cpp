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

// Original signature (from IDA): FRDGPass* SetupParameterPass(FRDGBuilder* this, FRDGPass* pass)
static void* __fastcall setup_parameter_pass_trampoline(void* this_ptr, void* pass_ptr) {
    // Call the original first so the pass is fully set up.
    auto result = g_setup_parameter_pass_hook.call<void*>(this_ptr, pass_ptr);

    // Read the pass name field at +0x10. The pass was just constructed by
    // FRDGPass::FRDGPass (round 11c) which always wrote to +0x10 before
    // SetupParameterPass runs, so the read is safe.
    if (pass_ptr != nullptr) {
        const wchar_t* name = *reinterpret_cast<const wchar_t* const*>(
            reinterpret_cast<const uint8_t*>(pass_ptr) + sn2_rdg_pass_hook::FRDGPASS_NAME_FIELD_OFFSET);
        sn2_rdg_pass_hook::on_setup_parameter_pass(reinterpret_cast<uintptr_t>(pass_ptr), name);
    }

    return result;
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
