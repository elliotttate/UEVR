// Sn2MaterialNameHook.hpp
//
// Scaffolding for logging UE5 material proxies at draw setup time.
//
// Goal: emit material proxy sightings, then eventually pair them with PSOs
// once a safe engine execution hook provides both values at the same time.
//
// IMPLEMENTATION STATUS: SCAFFOLD ONLY.
//   The RVA for UMaterialInterface::GetRenderProxy in Subnautica2-Win64-Shipping.exe
//   needs to be located via IDA before this hook can fire. Drop the resolved
//   RVA into SUBNAUTICA2_UMATERIAL_GETRENDERPROXY_RVA below.
//
// HOW TO FIND THE RVA (run in IDA, then update this file):
//   1. Open Subnautica2.exe.i64
//   2. Search strings for "GetRenderProxy" or look at vtable of UMaterialInterface
//   3. The function signature is `FMaterialRenderProxy* (UMaterialInterface* this)`
//   4. UE5 stock implementation typically returns `MaterialRenderProxy.Get()` or
//      similar; should be a small function (<100 bytes)
//   5. Record the RVA (VA - 0x140000000) and update the constant below
//
// ALTERNATIVE LOOKUP STRATEGY (if RVA can't be found):
//   Hook FMaterial::GetFriendlyName instead, which is called less hot but
//   gives the same data. Its signature is `FString (FMaterial* this)`.
//
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <spdlog/spdlog.h>

namespace sn2_material_hook {

// RVA in the game binary (VA - image_base 0x140000000).
//
// LOCATED via IDA round 11c (2026-05-21): UMaterialInterface::GetRenderProxy
// is at RVA 0x41ABCD0 (VA 0x1441ABCD0). BUT — this 42-byte function is the
// PURE_VIRTUAL trampoline: it just calls UE5's PURE_VIRTUAL error reporter
// with file/line/class strings and returns NULL. It is NEVER called at
// runtime because the vtable slot is always overridden by UMaterial or
// UMaterialInstance subclasses.
//
// Hooking the PURE_VIRTUAL stub is a no-op for our goal.
//
// CORRECT HOOK TARGETS (require separate IDA lookups):
//   - UMaterial::GetRenderProxy           — for UMaterial instances
//   - UMaterialInstance::GetRenderProxy   — for UMaterialInstanceConstant/Dynamic
//   - Or hook UMaterialInterface::UpdateMaterialRenderProxy (binfold-confirmed
//     symbol exists at sub_???_UpdateMaterialRenderProxy)
//   - Or hook FBasePassMeshProcessor::AddMeshBatch which takes (material, mesh)
//     and is called per-draw — gives material context with the PSO.
//
// Until one of these is located, this hook stays disabled. The recommended
// path is `UMaterialInterface::UpdateMaterialRenderProxy`:
//   binfold name: ?UpdateMaterialRenderProxy@UMaterialInterface@@... — search
//   for it in moddingkit/runs/binfold_scratch/symbols.json.
inline constexpr uint64_t SUBNAUTICA2_UMATERIAL_GETRENDERPROXY_RVA = 0; // intentionally 0 — PURE_VIRTUAL stub doesn't help
inline constexpr uint64_t SUBNAUTICA2_UMATERIAL_GETRENDERPROXY_PUREVIRT_STUB_RVA = 0x41ABCD0; // documented for reference; do NOT hook

// CORRECT HOOK TARGETS (confirmed via binfold + IDA round 11):
//
// FBasePassMeshProcessor::AddMeshBatch — called once per mesh batch with the
// material proxy in the FMeshBatch arg. Hot but bounded (~once per visible
// primitive per pass). At entry: rcx=this, rdx=FMeshBatch* (which contains
// MaterialRenderProxy*).
inline constexpr uint64_t SUBNAUTICA2_FBASEPASS_ADDMESHBATCH_RVA = 0x2644A70;

// FMaterialResource::GetFriendlyName(FString*) — returns the material name.
// Call this on the material the proxy resolves to (proxy->GetMaterial(level)
// → FMaterialResource* → GetFriendlyName).
inline constexpr uint64_t SUBNAUTICA2_FMATERIALRESOURCE_GETFRIENDLYNAME_RVA = 0x41CDB30;

// FMaterialInstanceResource::GetFriendlyName — instance variant.
inline constexpr uint64_t SUBNAUTICA2_FMATERIALINSTANCERESOURCE_GETFRIENDLYNAME_RVA = 0x41A99B0;

// RECOMMENDED IMPLEMENTATION PATH:
//   1. Midhook FBasePassMeshProcessor::AddMeshBatch entry.
//   2. Read FMeshBatch.MaterialRenderProxy (offset ~+0x18 in FMeshBatch).
//   3. Call (*MaterialRenderProxy->vtable->GetMaterial)(proxy, featureLevel)
//      → returns FMaterial* → cast to FMaterialResource* → call
//      FMaterialResource::GetFriendlyName to get FString name.
//   4. Cache (material_proxy_ptr → name_string) and emit [SN2-Material] on
//      first sighting per proxy.
//   5. Add a separate execution/draw association hook before emitting per-PSO
//      material rows. The AddMeshBatch hook alone does not identify the later
//      D3D12 PSO bind.

// Env-gated enable. The hook is no-op until both:
//   (a) UEVR_SN2_MATERIAL_HOOK=1
//   (b) SUBNAUTICA2_UMATERIAL_GETRENDERPROXY_RVA is non-zero
inline bool env_enabled() {
    static const bool e = [](){
        const char* v = std::getenv("UEVR_SN2_MATERIAL_HOOK");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

// Mapping: FMaterialRenderProxy* -> material_name (UE5 FString → std::string)
//
// CURRENT WIRING STATUS (2026-05-21):
//   - AddMeshBatch hook (proxy first-sighting log)  : INSTALLED — Sn2HooksInstall.cpp
//   - PSO ↔ material association                    : NOT WIRED
//   - log_known_materials_for_pso() callers         : NONE
//   - associate_pso_with_material() callers         : NONE
//
// The proxy_cache below is populated by the AddMeshBatch trampoline. To answer
// "which material caused this PSO?", we'd need a producer that sets a TLS
// "current material proxy" at a point where the consumer (D3D12 SetPipelineState
// or Draw) can read it. AddMeshBatch itself is too early: it builds an
// FMeshDrawCommand that is dispatched much later, so a TLS written there is
// cleared by the time the draw hits the GPU command list.
//
// A real implementation needs to hook the FMeshDrawCommand submission path
// (or the FRDGPass execution wrapper that runs the basepass) and bracket the
// current material proxy across the PSO-bind/Draw window. Until that lands,
// the *_for_pso functions stay defined but unused.
inline std::mutex& proxy_cache_mu() { static std::mutex m; return m; }
inline std::unordered_map<uintptr_t, std::string>& proxy_cache() {
    static std::unordered_map<uintptr_t, std::string> m; return m;
}

// Mapping: ID3D12PipelineState* -> set of material_names known to have used this PSO.
// Stored as comma-separated for cheap log emission.
inline std::mutex& pso_material_mu() { static std::mutex m; return m; }
inline std::unordered_map<uintptr_t, std::string>& pso_material() {
    static std::unordered_map<uintptr_t, std::string> m; return m;
}

// Called from the D3D12 draw hook when a pso3069-class PSO is bound.
// Logs the materials known to have created/used this PSO (if any).
inline void log_known_materials_for_pso(uint64_t pso_ptr, uint64_t seq, uint32_t ps_crc) {
    if (!env_enabled()) return;
    std::scoped_lock _{pso_material_mu()};
    auto it = pso_material().find(pso_ptr);
    if (it == pso_material().end()) {
        SPDLOG_WARN(
            "[SN2-Material] seq={} pso=0x{:x} ps_crc=0x{:08x} material=<unknown — no GetRenderProxy hook entry>",
            seq, pso_ptr, ps_crc);
        return;
    }
    SPDLOG_WARN(
        "[SN2-Material] seq={} pso=0x{:x} ps_crc=0x{:08x} materials=\"{}\"",
        seq, pso_ptr, ps_crc, it->second);
}

// Called by the GetRenderProxy hook body. Caches proxy → name mapping.
// material_name should be the UE5 FString result of UMaterial::GetName() or
// FMaterial::GetFriendlyName().
inline void on_get_render_proxy(uintptr_t material_interface_ptr,
                                uintptr_t returned_proxy_ptr,
                                const char* material_name) {
    if (!env_enabled() || returned_proxy_ptr == 0) return;
    std::scoped_lock _{proxy_cache_mu()};
    auto& cache = proxy_cache();
    auto it = cache.find(returned_proxy_ptr);
    if (it == cache.end()) {
        cache.emplace(returned_proxy_ptr, std::string(material_name ? material_name : "?"));
        // First-sighting log (low volume — once per unique proxy)
        SPDLOG_WARN(
            "[SN2-MaterialSeen] proxy=0x{:x} interface=0x{:x} material=\"{}\"",
            returned_proxy_ptr, material_interface_ptr, material_name ? material_name : "?");
    }
}

// Called when a PSO is bound during a draw and we know the FMaterialRenderProxy*
// (e.g., from a separate basepass/translucent processor hook). Records the
// pso<->material association.
inline void associate_pso_with_material(uintptr_t pso_ptr, uintptr_t proxy_ptr) {
    if (!env_enabled() || pso_ptr == 0 || proxy_ptr == 0) return;
    std::string name;
    {
        std::scoped_lock _{proxy_cache_mu()};
        auto it = proxy_cache().find(proxy_ptr);
        if (it == proxy_cache().end()) return;
        name = it->second;
    }
    std::scoped_lock _{pso_material_mu()};
    auto& map = pso_material();
    auto it = map.find(pso_ptr);
    if (it == map.end()) {
        map.emplace(pso_ptr, name);
    } else if (it->second.find(name) == std::string::npos) {
        it->second.append(",");
        it->second.append(name);
    }
}

// Installation entry point. Returns true if hooks were installed successfully.
// Stub for now: requires RVA. The actual hook body needs to be implemented
// as a midhook on UMaterialInterface::GetRenderProxy that, on return, calls
// on_get_render_proxy(this_ptr, returned_value, this->GetName()).
inline bool try_install() {
    if (!env_enabled()) {
        SPDLOG_INFO("[SN2-MaterialHook] disabled (UEVR_SN2_MATERIAL_HOOK not set)");
        return false;
    }
    if (SUBNAUTICA2_UMATERIAL_GETRENDERPROXY_RVA == 0) {
        SPDLOG_WARN("[SN2-MaterialHook] RVA not yet located; hook not installed. See header for instructions.");
        return false;
    }
    // TODO: install midhook at image_base + RVA
    return false;
}

} // namespace sn2_material_hook
