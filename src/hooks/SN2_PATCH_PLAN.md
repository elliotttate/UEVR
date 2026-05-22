# SN2 right-eye fix — patch plan beyond VSM clamp

Status as of 2026-05-21. VSM-UB clamp patch shipped (`Sn2VsmUbClampPatch.hpp` at RVA `0x2B50544`). This document maps the remaining shared-UAV bugs to fix families and identifies which are tractable for the same 3-byte-patch approach versus which need a different toolchain.

## The shared-UAV inventory (from `dead_dispatch_shaders.json` + colleague analysis)

| # | Subsystem | Shared UAV(s) | Producer | Visual symptom |
|---|---|---|---|---|
| 1 | VSM projection | 30086 | VirtualShadowMapProjection CS, view-0 only | No shadows on right eye | **FIXED via clamp** |
| 2 | HZB | 29816 | HZBBuildCS at event 20997 (left-only mip chain) | Occlusion-wrong reflections on right eye |
| 3 | Volumetric Fog froxels | 28115, 30081, 30083 | MaterialSetupCS (FVolumetricFogMaterialSetupCS), view-0 only | Blown-white fog / wrong fog density on right eye |
| 4 | UWE custom fog | 28153, 30092, 30095 | UWEFogDenoiseCS + UWEFogImportanceDilateCS, view-0 only | Wrong underwater fog on right eye |
| 5 | Lumen reflection tile classification | 27768, 27769, 28177, 28182 | ReflectionTileClassificationBuildListsCS / Mark, view-0 only | No/wrong reflections on right eye |
| 6 | Lumen reflection denoise | 30112, 30114, 28357 | LumenReflectionDenoiserClear/Spatial CS, view-0 only | Stale/wrong reflection denoising |

## Why VSM was the easy case

VSM had a *consumer-side* clamp function (`FVirtualShadowMapArray::GetUniformBuffer`) whose entire body was 13 instructions and whose key behaviour lived in a single `cmovl eax, edx`. Patching to `xor eax, eax` forces all consumers to read view 0's UB, regardless of which view they think they want. That works because the bug surface (sampling) is per-view but the data (UB) is per-view-stored. **The clamp gave us a single point of failure to patch.**

The other 5 subsystems don't have an equivalent clamp. The bug is on the *producer* side: a per-view loop somewhere in C++ that decides "view 0 produces this UAV, view 1 does not." The consumer-side bindings look at the same UAV regardless.

## Fix families per subsystem

### Family A — Same as VSM clamp (consumer-side per-view UB lookup)

**Only candidate**: VSM (done).

None of HZB / fog / Lumen tiles have an analogous indexed-by-ViewIndex UB lookup. The HZB texture, the fog froxel grid, and the Lumen tile lists are each a single resource shared across all consumers regardless of view.

### Family B — Producer-side: force per-view re-dispatch

Hook the per-view dispatch loop and force it to iterate view 1 alongside view 0. UE5 source dive identified the loops:

| Subsystem | Per-view loop location | C++ function |
|---|---|---|
| Lumen reflections | `IndirectLightRendering.cpp:930, 1008, 2047` | `FDeferredShadingSceneRenderer::RenderDiffuseIndirectAndAmbientOcclusion`, `RenderLumenReflections` (per-view inside) |
| VSM mask bits | `ShadowSceneRenderer.cpp:838` | `FShadowSceneRenderer::RenderVirtualShadowMapProjectionMaskBits` |
| Volumetric fog | `?ComputeVolumetricFog@FSceneRenderer@@` (lambda at 0x142FBB770) | `FSceneRenderer::ComputeVolumetricFog` |

For each: find the loop's induction-variable compare instruction (`cmp r/m32, [Views.Num()]`) and patch the compare to be unconditional — forcing it to always iterate 2 views.

**Catch**: the loop may exit early for view 1 *inside* the body (e.g. an `if (!ViewState[i]) continue;` check). The loop-compare patch wouldn't fix that. We'd need to find and patch the inner-body early-out too.

**Effort**: 1-3 hours per subsystem to identify the right instruction, plus careful testing for crashes (forcing extra dispatch work on a non-initialised view-1 state can crash).

### Family C — RDG-pass-hook re-issue

We already have `Sn2RDGPassHook` scaffolded (intercepts `FRDGBuilder::SetupParameterPass` and captures pass names). Extend it to: when a pass's name matches one of `MaterialSetupCS`, `HZBBuildCS`, `ReflectionTileClassificationBuildListsCS`, `UWEFogDenoiseCS`, etc., enqueue a duplicate pass with view-1 parameter bindings. Then on `FRDGBuilder::Execute`, the duplicate gets dispatched.

**Catches**:
- Parameter bindings are baked into the original `FRDGPass` — we'd need to copy + mutate them in-flight, which means understanding the `FRDGPass` parameter struct layout for each consumer-shader (we have it for VSM via the colleague's dump, but not all).
- Duplicating compute passes may crash if the consumer expects single-write semantics on the UAV.

**Effort**: medium-high. 1-2 days of careful engineering, with regressions likely.

### Family D — DXIL rewrite of consumer shaders

Patch the basepass MainPS + 4 overlay shaders to sample shared UAVs with view-0-relative UVs even when running for view 1. We already have the DXIL substitution toolchain (`inject_sky_atmos.exe`).

**Catches**:
- 5+ shaders to patch (basepass MainPS + each overlay).
- The "view-0-relative UV" computation needs the view-0 vs view-1 viewport transform — UEVR has this but the shader patch needs to read it from a CB.
- Will need re-patching every time the game's shaders change (each patch).

**Effort**: 2-3 days for a clean implementation.

### Family E — Engine source patch (cleanest)

Build SN2 from source with UE5.6.1 + UWE plugin source, fix `FShadowSceneRenderer::ApplyVirtualShadowMapProjectionForLight` etc. to genuinely run per-view. Requires UWE plugin source access from Unknown Worlds.

**Effort**: depends on source availability — but the cleanest outcome.

## Recommendation

For the next push (1-2 days), prioritise:

1. **Family B for Lumen reflections** — find the per-view loop compare in `RenderLumenReflections`, patch it to always iterate both views, see if right-eye reflections come back. The single-eye loops in IndirectLightRendering.cpp:930 are well-localised.
2. **Family B for VSM mask-bits** — same approach, complements the clamp patch by ensuring view 1 actually gets shadow-mask data for direct/spot lights, not just reading view 0's UB.
3. **Family D for the volumetric fog**: bind a "stereo-fix" CB to view-1's basepass that contains view-0's viewport transform, and DXIL-rewrite the fog SRV sample to use that CB's UV instead of the view-1 SVPosition-derived UV. Tested against pso3069 first (the largest visible effect), then OverlayA-D.

Family E is the long-term answer; everything else is a workaround. Without UE5 source rebuild it's all binary patches with diminishing returns.

## Concrete next IDA + UEVR work

If we want to do Family B for Lumen reflections:

1. **In IDA**: find the function whose mangled prefix is `?RenderLumenReflections@FDeferredShadingSceneRenderer@@` (not in BinFold yet; BinFold only got the HWRT one). Find its callers (it should be called inside a per-view loop in `RenderDiffuseIndirectAndAmbientOcclusion`).
2. **Identify the loop's `cmp` instruction** comparing the loop counter against `Views.Num()`. Get the byte offset.
3. **Add a Sn2LumenReflectionsForceBothViews patch** in UEVR analogous to the VSM clamp — patch the loop counter or the compare to always iterate.
4. **Test**: capture before/after, run Tool 8 (`sn2_nsight_regression_bundle`) for verdict.

If we want to do Family D for fog: dust off `inject_sky_atmos.exe` + the dxil-patch toolchain; add a CBV at root 4 or 5 that carries the view-0-to-view-1 viewport remap; modify pso3069's DXIL to sample t8/t9 with the remapped UV. (Refer to the existing `ngfx_cpp2025_make_uevr_magenta_probe.py` for the pipeline.)

I have not built either of these yet — they need explicit go-ahead because the per-view-loop patch can crash the game if a view-1 state is unexpectedly unbound. The VSM clamp was safe because both eyes read the same already-allocated UB; the new patches mutate state that the engine may have explicitly de-initialised.
