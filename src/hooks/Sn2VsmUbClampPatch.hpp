// Sn2VsmUbClampPatch.hpp
//
// Forces FVirtualShadowMapArray::GetUniformBuffer() to always return
// CachedUniformBuffers[0], regardless of the ViewIndex argument. The right
// eye (view 1) under -emulatestereo otherwise reads CachedUniformBuffers[1]
// which was built from a default-constructed FVirtualShadowMapArrayPerViewParameters
// entry (NumLocalLights + DirectionalLightIds.Num() == 0 early-out in
// VirtualShadowMapArray.cpp:2064) — i.e. an all-zero VSM uniform buffer.
//
// The right-eye VirtualShadowMapProjection compute pass then samples a zero
// VSM and produces no output, leading to the missing-shadow contribution to
// the right-eye basepass blown-white symptoms.
//
// Patch coordinate
// ----------------
//
// IDA round 14 (BinFold-assisted) located the function:
//
//   ?GetUniformBuffer@FVirtualShadowMapArray@@QEBAPEAV?$TRDGUniformBuffer@VFVirtualShadowMapUniformParameters@@@@H@Z
//
// at RVA 0x2B50530 (VA 0x142B50530), 13 instructions, body:
//
//   142b50530  mov     eax, [rcx+240h]    ; Num()
//   142b50536  mov     r8, rcx
//   142b50539  test    eax, eax
//   142b5053b  jnz     short loc_142B50540
//   142b5053d  xor     eax, eax
//   142b5053f  retn
//   142b50540  dec     eax
//   142b50542  cmp     edx, eax
//   142b50544  cmovl   eax, edx           ; <-- patch target (3 bytes: 0F 4C C2)
//   142b50547  movsxd  rcx, eax
//   142b5054a  mov     rax, [r8+238h]     ; CachedUniformBuffers.GetData()
//   142b50551  mov     rax, [rax+rcx*8]
//   142b50555  retn
//
// Replacing `cmovl eax, edx` (0F 4C C2) at +0x14 with `xor eax, eax; nop`
// (33 C0 90) forces `eax = 0` before the array indexing, so the function
// always returns CachedUniformBuffers[0].
//
// Gating
// ------
//
// UEVR_SN2_VSM_UB_CLAMP_PATCH=1 — enables the patch.
//
// Reverts on UEVR shutdown via the saved original-bytes copy.

#pragma once

#include <cstdint>
#include <cstdlib>
#include <string_view>

namespace sn2_vsm_ub_clamp_patch {

// RVAs derived from BinFold symbols (moddingkit/runs/binfold_scratch/symbols.json)
// for the specific SN2 shipping binary as of 2026-05-21. If the binary changes,
// these must be re-verified before the patch is applied.

inline constexpr uintptr_t SUBNAUTICA2_GETUNIFORMBUFFER_RVA = 0x2B50530;
inline constexpr uintptr_t SUBNAUTICA2_GETUNIFORMBUFFER_CMOVL_OFFSET = 0x14;
inline constexpr uintptr_t SUBNAUTICA2_GETUNIFORMBUFFER_CMOVL_RVA =
    SUBNAUTICA2_GETUNIFORMBUFFER_RVA + SUBNAUTICA2_GETUNIFORMBUFFER_CMOVL_OFFSET;

// Pre-patch bytes (cmovl eax, edx)
inline constexpr uint8_t k_original_bytes[3] = {0x0F, 0x4C, 0xC2};
// Post-patch bytes (xor eax, eax; nop)
inline constexpr uint8_t k_patched_bytes[3] = {0x33, 0xC0, 0x90};

// Sanity prologue at the function entry. If this mismatches we abort the
// patch — the binary has likely been updated and the offsets are stale.
inline constexpr uint8_t k_expected_function_prologue[10] = {
    0x8B, 0x81, 0x40, 0x02, 0x00, 0x00, // mov eax, [rcx+240h]
    0x4C, 0x8B, 0xC1,                   // mov r8, rcx (REX.WR + 8B + C1)
    0x85,                               // test eax,eax (first byte of next instr)
};

inline bool env_enabled() {
    const char* v = std::getenv("UEVR_SN2_VSM_UB_CLAMP_PATCH");
    if (v == nullptr) return false;
    const std::string_view sv{v};
    if (sv.empty()) return false;
    if (sv == "0") return false;
    return true;
}

}  // namespace sn2_vsm_ub_clamp_patch
