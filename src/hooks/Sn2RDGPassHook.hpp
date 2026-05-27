// Sn2RDGPassHook.hpp
//
// UE5 FRDGPass hook to map RDG pass names → PSO IDs → bound resources.
//
// HIGH-VALUE HOOK: once installed, every CS dispatch in every future capture
// gets a UE5-source-level name like "ComputeVolumetricFog.VBufferA.CS" instead
// of "PipelineState 4".
//
// HOOK TARGET CHOICE (3 candidates, decreasing complexity):
//
// 1. FRDGPass::FRDGPass(FRDGEventName&&, FRDGParameterStruct, ERDGPassFlags, ERDGPassTaskMode)
//    RENDERCORE_API constructor. UE5 5.6.1 header:
//      Engine/Source/Runtime/RenderCore/Public/RenderGraphPass.h:219
//    Pros: called exactly once per pass; args directly contain name + flags.
//    Cons: virtual; multiple derived constructors call into base.
//
// 2. FRDGBuilder::SetupParameterPass(FRDGPass* Pass)  ← RECOMMENDED
//    RENDERCORE_API non-virtual. UE5 5.6.1 header:
//      Engine/Source/Runtime/RenderCore/Public/RenderGraphBuilder.h:523
//    Pros: single canonical post-construction hook point.
//    The FRDGPass* arg has Name field accessible via GetName() — which is
//    inlined to `Name.GetTCHAR()`. Name field is at a small offset within
//    FRDGPass (after vptr; see RenderGraphPass.h:216).
//
// 3. FRDGBuilder::ExecutePass(FRHIComputeCommandList&, FRDGPass*)
//    Static. Called every execute. Higher volume but most reliable.
//
// HOW TO FIND THE RVA IN IDA (for option 2, the recommended path):
//   1. Open Subnautica2.exe.i64
//   2. String search: "RDG" or "RenderGraphBuilder" — UE5 ships some strings
//   3. Alternative: search for the unique pattern of SetupPassInternals → which
//      is called only by AddPassInternal/SetupParameterPass.
//   4. Inspect Engine/Source/Runtime/RenderCore/Private/RenderGraphBuilder.cpp
//      for the SetupParameterPass body — it's distinctive (calls
//      SetupPassInternals, SetupPassResources, SetupPassDependencies)
//   5. Record VA; RVA = VA - 0x140000000
//
// WHAT TO LOG:
//   On each hook fire:
//   - pass_name: from Pass->Name.GetTCHAR()  (FRDGEventName field offset TBD)
//   - pass_flags: from Pass->Flags          (ERDGPassFlags)
//   - pipeline: from Pass->Pipeline         (ERHIPipeline)
//   - parameter_struct: from Pass->ParameterStruct (FRDGParameterStruct)
//
//   The PSO mapping happens later: when ExecutePass fires for this Pass,
//   the cmdlist binding the CS PSO will set state.current_pso. We correlate
//   by Pass pointer (kept in TLS through dispatch).
//
// FIELD OFFSETS IN FRDGPass (5.6.1):
//   The exact byte offset for `Name` within FRDGPass requires IDA inspection.
//   Stock UE5 layout:
//     0x00: vptr
//     0x08+: FRDGEventName Name  (which contains a FString or FName + ref)
//   Let IDA's struct view fill this in.
//
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <spdlog/spdlog.h>

namespace sn2_rdg_pass_hook {

// RVA in game binary (VA - 0x140000000). Confirmed via IDA round 11c on the
// Subnautica2-Win64-Shipping.exe.i64 (build md5 f6a779d80f093adaaada103973e9a4ec).
// Symbol: ?SetupParameterPass@FRDGBuilder@@AEAAPEAVFRDGPass@@PEAV2@@Z
inline constexpr uint64_t SUBNAUTICA2_FRDG_SETUP_PARAMETER_PASS_RVA = 0x32A2C20;

// Byte offset of the FRDGEventName field within FRDGPass. Confirmed via IDA
// constructor decompile: at offset 0x10 the ctor writes a3[0] (first
// FRDGEventName field, which UE5 stores as `const TCHAR* Name`). At +0x18 is
// the RefCounted backing pointer; +0x20 is the third FRDGEventName field.
// Read a wchar_t* from (FRDGPass*)+0x10 to get the pass name string.
inline constexpr uint64_t FRDGPASS_NAME_FIELD_OFFSET = 0x10;

// Alternate hook targets if SetupParameterPass causes issues:
inline constexpr uint64_t SUBNAUTICA2_FRDG_SETUP_PASS_INTERNALS_RVA = 0x32A3280;
inline constexpr uint64_t SUBNAUTICA2_FRDG_SETUP_PASS_RESOURCES_RVA = 0x32A3380;
inline constexpr uint64_t SUBNAUTICA2_FRDG_PASS_CTOR_RVA            = 0x32827B0;

inline bool env_enabled() {
    static const bool e = [](){
        const char* v = std::getenv("UEVR_SN2_RDG_PASS_HOOK");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

inline uint32_t setup_log_max() {
    static const uint32_t v = []() {
        const char* raw = std::getenv("UEVR_SN2_RDG_PASS_LOG_MAX");
        if (raw == nullptr || raw[0] == '\0') return 512u;
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(raw, &end, 0);
        return end != raw ? static_cast<uint32_t>(parsed) : 512u;
    }();
    return v;
}

// ExecutePass hook target resolved from the binfold symbol bridge:
//   ?ExecutePass@FRDGBuilder@@CAXAEAVFRHIComputeCommandList@@PEAVFRDGPass@@@Z
// It brackets the real pass execution, which is the point where RHI work is
// enqueued on the translate thread.
inline constexpr uint64_t SUBNAUTICA2_FRDG_EXECUTE_PASS_RVA = 0x32974B0;

// Pass -> name cache: populated by SetupParameterPass hook. ExecutePass
// brackets actual RDG execution so D3D12 work can inherit the pass name.
//
// CURRENT WIRING STATUS (2026-05-21):
//   - Producer (SetupParameterPass)        : INSTALLED   — Sn2HooksInstall.cpp
//   - Producer (ExecutePass begin/end)     : INSTALLED   — Sn2HooksInstall.cpp
//   - Consumer (D3D12 Dispatch hook)       : INSTALLED   — D3D12Hook::dispatch
//   - Consumer (D3D12 ExecuteIndirect hook): INSTALLED   — D3D12Hook::execute_indirect
//   - Consumer (log emit lookup)           : PARTIAL     — pso map is logged and queryable
inline std::mutex& pass_name_mu() { static std::mutex m; return m; }
inline std::unordered_map<uintptr_t, std::string>& pass_name_table() {
    static std::unordered_map<uintptr_t, std::string> m; return m;
}

// Current executing pass on this thread. This must be set only while the engine
// is actually executing a pass, not during SetupParameterPass.
inline thread_local uintptr_t s_tls_current_rdg_pass = 0;
inline thread_local std::string s_tls_current_rdg_pass_name;

// Called from the SetupParameterPass hook body when fully installed.
inline void on_setup_parameter_pass(uintptr_t pass_ptr, const wchar_t* name_tchar) {
    if (!env_enabled() || pass_ptr == 0) return;
    // Convert wchar_t* to std::string (UTF-8 best-effort)
    std::string name;
    if (name_tchar) {
        for (const wchar_t* p = name_tchar; *p; ++p) {
            if (*p < 0x80) name.push_back(static_cast<char>(*p));
            else name.push_back('?');
        }
    }
    bool first_sighting = false;
    {
        std::scoped_lock _{pass_name_mu()};
        auto [it, inserted] = pass_name_table().insert_or_assign(pass_ptr, name);
        (void)it;
        first_sighting = inserted;
    }
    if (first_sighting) {
        static std::atomic<uint32_t> log_count{0};
        const uint32_t n = log_count.fetch_add(1, std::memory_order_relaxed);
        const uint32_t max_logs = setup_log_max();
        if (n == max_logs) {
            SPDLOG_WARN("[SN2-RDGPass] setup log cap reached ({}); set UEVR_SN2_RDG_PASS_LOG_MAX to raise it", max_logs);
            return;
        }
        if (n > max_logs) {
            return;
        }
        SPDLOG_INFO("[SN2-RDGPass] setup pass=0x{:x} name=\"{}\"", pass_ptr, name);
    }
}

inline void on_execute_pass_begin(uintptr_t pass_ptr) {
    if (!env_enabled() || pass_ptr == 0) return;
    std::scoped_lock _{pass_name_mu()};
    auto it = pass_name_table().find(pass_ptr);
    if (it == pass_name_table().end()) {
        s_tls_current_rdg_pass = 0;
        s_tls_current_rdg_pass_name.clear();
        return;
    }
    s_tls_current_rdg_pass = pass_ptr;
    s_tls_current_rdg_pass_name = it->second;
}

inline std::string current_pass_name() {
    if (!env_enabled() || s_tls_current_rdg_pass == 0) return {};
    return s_tls_current_rdg_pass_name;
}

inline void on_execute_pass_end(uintptr_t pass_ptr) {
    if (s_tls_current_rdg_pass == pass_ptr) {
        s_tls_current_rdg_pass = 0;
        s_tls_current_rdg_pass_name.clear();
    }
}

// Called by the D3D12 Dispatch hook while an ExecutePass hook has bracketed the
// current TLS pass. The pair forms the "pass_name -> PSO" mapping we want.
inline std::mutex& dispatch_map_mu() { static std::mutex m; return m; }
inline std::unordered_map<uintptr_t, std::string>& pso_to_pass_name() {
    static std::unordered_map<uintptr_t, std::string> m; return m;
}

inline void on_dispatch(uintptr_t pso_ptr) {
    if (!env_enabled() || pso_ptr == 0 || s_tls_current_rdg_pass == 0) return;
    std::scoped_lock _{dispatch_map_mu()};
    auto& m = pso_to_pass_name();
    auto it = m.find(pso_ptr);
    if (it == m.end()) {
        m.emplace(pso_ptr, s_tls_current_rdg_pass_name);
        SPDLOG_WARN(
            "[SN2-RDGPSO] first_sighting pso=0x{:x} pass_name=\"{}\"",
            pso_ptr, s_tls_current_rdg_pass_name);
    }
}

// Lookup at log emit time.
inline std::string lookup_pso_pass_name(uintptr_t pso_ptr) {
    if (!env_enabled()) return {};
    std::scoped_lock _{dispatch_map_mu()};
    auto& m = pso_to_pass_name();
    auto it = m.find(pso_ptr);
    if (it == m.end()) return {};
    return it->second;
}

inline bool try_install() {
    if (!env_enabled()) {
        SPDLOG_INFO("[SN2-RDGPassHook] disabled (UEVR_SN2_RDG_PASS_HOOK not set)");
        return false;
    }
    if (SUBNAUTICA2_FRDG_SETUP_PARAMETER_PASS_RVA == 0 || FRDGPASS_NAME_FIELD_OFFSET == 0) {
        SPDLOG_WARN("[SN2-RDGPassHook] RVA or field-offset not yet located; hook not installed. See header for instructions.");
        return false;
    }
    // TODO: install midhook at image_base + SUBNAUTICA2_FRDG_SETUP_PARAMETER_PASS_RVA
    // On entry: pass = rcx (first arg of __fastcall; FRDGPass* this)
    //           name_tchar = read(pass + FRDGPASS_NAME_FIELD_OFFSET)
    //           call on_setup_parameter_pass(pass, name_tchar)
    return false;
}

} // namespace sn2_rdg_pass_hook
