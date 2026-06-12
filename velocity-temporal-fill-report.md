# Adding Velocity-Buffer Capture to the DIBR Temporal Fill — Implementation Report

*2026-06-12. Design report only — no code changes. All file/line references verified against the working tree and `E:\UnrealEngine\Source` (UE 5.5 layout; SN2 is UWE's UE 5.6 with a +2 pass-enum shift).*

## 1. What velocity buys us

The temporal path today is **camera-delta only**, and the code says so in two places:

- `D3D12Component.cpp:5465` — "Engine-side motion (locomotion, animated cameras) is invisible to this matrix; the fill kernel's depth validation rejects history it can't explain, so it degrades to the scanline fill."
- `dibr_yoro.hlsl:2329-2337` (the AFW whole-eye compose blend) — the **color-agreement gate** exists *precisely because* object motion is unmodeled: "Object-space animation (swaying plants, fish) moves under a camera-only reprojection while its depth still validates — blending that paints a displaced double image. Large color deltas therefore reject history instead."

So for animated content, history is currently *detected and discarded*, which under AFW means animated objects get no real-render consistency blend → they carry the full warp-vs-real alternation (half-rate shimmer). Per-pixel motion vectors let history stay **registered** on animated content instead of being rejected:

1. **Primary win — the yoro whole-eye blend** (`dibr_yoro.hlsl:2300-2343`): advect the history fetch by per-pixel motion so plants/fish/creatures land where they actually were last frame; the color gate then passes and they get the same consistency treatment as static geometry.
2. **Secondary win — hole-fill validation** (`dibr_scatter_fill.hlsl:427-510`): velocity's z channel carries the surface's *own* previous device depth, replacing the current `0.15 * estDepth` tolerance guess with an exact per-surface key.
3. **Tertiary — better history rejection**: a stashed history-velocity lets the fill recognize "this reveal is behind a moving object" and prefer scanline fill over a doubled image.

## 2. Verified UE velocity-buffer facts (from E:\UnrealEngine\Source)

| Fact | Value | Source |
|---|---|---|
| RDG texture name | `SceneVelocity` | `Renderer/Private/SceneTextures.cpp:692` |
| Format | `PF_G16R16` (R16G16_UNORM), or `PF_A16B16G16R16` (RGBA16_UNORM) when Lumen distance fields or raytracing are supported — SN2 has Lumen, so expect **RGBA16** | `VelocityRendering.cpp:358-374` |
| Extent | Render resolution (same family as SceneDepthZ; pre-upscale when DLSS/TSR runs) | `FVelocityRendering::GetRenderTargetDesc`, `VelocityRendering.cpp:382` |
| Content | `float3(ScreenPos - PrevScreenPos, DeviceZ - PrevDeviceZ)` — NDC-space delta, **camera + object motion combined**, **TAA jitter already removed** on both ends | `VelocityCommon.ush:9-22` |
| Encoding (xy) | `EncodedV.xy = V.xy * (0.499*0.5) + 32767/65535`, **after** gamma pre-encode `sign(V)*sqrt(abs(V))*(2/sqrt(2))` — `VELOCITY_ENCODE_GAMMA=1` on all SM5+ (every PC title) | `Common.ush:237-241, 1915-1924` |
| Encoding (zw) | `VELOCITY_ENCODE_DEPTH=1` default: `V.z = DeviceZ - PrevDeviceZ` packed as float bits split across z/w 16-bit channels (w's LSB = HasPixelAnimation flag) | `Common.ush:233-234, 1926-1930` |
| Decode | `V.xy = Enc.xy * InvDiv - (32767/65535)*InvDiv` with `InvDiv = 1/(0.499*0.5)`, then gamma undo `V.xy = V.xy*abs(V.xy)*0.5`; `V.z = asfloat((z16<<16)\|(w16&0xFFFE))` | `Common.ush:1948-1968` |
| "Not written" sentinel | Clear color is `(0,0)` (Transparent); encode deliberately avoids 0 (`0.499` margin, `+0.1/65535` bias). **`Enc.x == 0` ⇒ no velocity written** (static object that didn't get a velocity-pass draw) — fall back to camera matrix | `Common.ush:1921`, `VelocityCommon.ush:20-21` |
| SN2 pass indices | `Velocity` = pass 13, `TranslucentVelocity` = 14 (UWE +2 shift) | `Sn2HooksInstall.cpp:1046-1067` |

### The AFW alignment gift

Under AFW single-view alternation the engine's camera *is* one eye this frame and *was the other eye* last frame. The engine computes velocity against its own previous frame's view matrices — so the buffer natively encodes:

```
current source-eye (eye B, frame N) NDC  →  previous rendered frame (eye A, frame N-1) NDC
```

…and frame N-1's render of eye A **is exactly the stashed history** the fill consumes (`dibr_afw_stash.hlsl:4-9`). The IPD baseline, head motion, locomotion, *and* per-object animation are all inside one per-pixel vector, jitter-free. No matrix composition needed at the point of use: `histNdc = srcNdc - V.xy`, `histDeviceZ = srcDeviceZ - V.z`. The same holds in non-AFW modes (prev camera = same eye = that history's space), so one code path serves both.

Caveat to verify empirically: this assumes the engine's `PrevViewMatrices` genuinely carry last frame's (other-eye) camera under UEVR's hooks. The existing comment at `FFakeStereoRenderingHook.cpp:17729` ("we decrement the frame count because it fixes motion vectors in the right eye") shows prev-frame view bookkeeping is already actively managed — Phase 0 below measures it rather than trusting it.

## 3. Current temporal-fill data flow (what we're extending)

- **CPU** (`D3D12Component.cpp`):
  - Depth acquired at present time: AFW snapshot ring (opt-in, currently disabled — `:5034-5063`) → pool hook `get_texture(L"SceneDepthZ")` (`:5066-5070`) → **DSV-discovery fallback** `dibr_depth_tracker::select_scene_depth` (`:5072-5080`; this is the live path on SN2, whose UE5 build defeats the pool-hook signature scan) → AFW sequence-paired identity fix (`:5082+`).
  - `reproj_target_to_prev` built from per-frame eye-camera records (AFW, `:5468-5550`) or HMD pose delta (`:5551-5580`); `temporal_enabled` = 2.0 (AFW) / 1.0 (plain).
  - `synthesize(device, cmd_list, mode, source, src_state, depth, depth_state, params)` (`:5619-5622`).
- **GPU** (`DIBRSynthesis.hpp:499-501`): descriptor layout t0 color, t1 depth, t2 prepared depth, t3 history color SRV; u0 output, u1/u2 scatter key+color, u3/u4 history color+key, u5 prep UAV — `kDescriptorsPerSlot = 10`, ring of 8.
  - `dibr_afw_stash.hlsl` stashes this frame's raw render (color + device-depth key) into the history pair.
  - `dibr_scatter_fill.hlsl:427-510` — holes: reproject `(ndc, estDepth)` through `reproj_target_to_prev`, depth-validate vs `g_historyKey` (3×3 rescue probe), AFW replaces fill with real history outright.
  - `dibr_yoro.hlsl:2300-2343` — whole-eye blend: same matrix, nearest-tap key validation with `0.15*estDepth` tolerance, bilinear color fetch, color-agreement gate, EMA via `temporal_blend`.

## 4. Implementation plan

### Phase 0 — Discovery and ground-truth (no shader changes, ~half a day)

1. **Find the texture per title — the exact acquisition path.** Half of this already exists:
   - **Already running:** the always-installed `CreateRenderTargetView` device hook (`D3D12Hook.cpp:6434`) feeds `dibr_depth_tracker::record_rtv()`, which **already contains a velocity discovery probe** (`DIBRDepthTracker.cpp:236-256`): it filters for velocity-shaped RTVs (`R16G16B16A16_UNORM`, `R16G16_UNORM`, `R16G16_FLOAT`, `R32G32_FLOAT`; ≥256², non-MSAA) and logs each once as `[DIBR] velocity-shaped RTV observed: <ptr> WxH fmt N`. Its comment block already documents the format/encoding facts and the intent ("PureDark gates his temporal blend on |MV| ~0.005; ours currently has no MV input at all"). It is gated on `view_tracking_enabled()` (`DIBRDepthTracker.cpp:126-129`), which is true whenever AFW is env-requested, the census, the probe, or AFW depth snapshots are on — so AFW sessions already log the candidates. **First action: run SN2 under AFW and read these log lines** — expect SceneVelocity (eye-render-res, fmt 11 = RGBA16_UNORM on SN2/Lumen) plus possible decoys (other RG16/RGBA16 GBuffer-adjacent targets).
   - **To build (mirrors the depth side):** promote the log-once probe to a candidate tracker, copying the DSV `Candidate` pattern (`DIBRDepthTracker.cpp:193-227` — ComPtr-held, recency-sequenced, bounded) into a `select_scene_velocity(...)` mirroring `select_scene_depth` (`DIBRDepthTracker.cpp`, decl `hpp:72`). Selection keys, in order: (a) **extent exactly equals the selected scene depth's extent** (they share render resolution by construction — `SceneTextures.cpp:692` creates velocity at `Config.Extent`); (b) format in the velocity set; (c) **live this present window** — liveness comes from the always-installed bind hooks, which already pass the full RTV array of every `OMSetRenderTargets` (`D3D12Hook.cpp:32114`) and `BeginRenderPass` (`D3D12Hook.cpp:32195`, the path UE5's RHI actually uses) into `record_census_bind`; add a lightweight `record_rtv_bind`-style liveness probe next to the existing `record_dsv_bind` (`D3D12Hook.cpp:32099/32183`); (d) recency. The extent+format+liveness triple eliminates the decoys: nothing else velocity-shaped is bound as an RTV at scene-depth extent every frame.
   - **Pool-hook titles (not SN2):** one line next to the SceneDepthZ lookup (`D3D12Component.cpp:5068`): `rt_pool->get_texture<ID3D12Resource>(L"SceneVelocity")`. Confirm the name once via `snapshot_render_target_names()` (`RenderTargetPoolHook.hpp:35`).
   - **Bind-signature ground truth:** the census (`UEVR_DIBR_BIND_CENSUS=1`, `DIBRDepthTracker.hpp:40-48`) dumps one present-window's ordered (RTV0, DSV) binds with shapes/formats — run it once on SN2 to see exactly where the Velocity pass (SN2 pass 13) binds sit in the frame, which both verifies the candidate pick and gives the qualifying-bind signature Phase 1 v2 needs.
   - `FrameResourceInspector`'s `Velocity?` tagging (`FrameResourceInspector.cpp:165`) and `uevr_render_resources` remain available as cross-checks.
2. **Ground-truth the decode and the AFW alignment before any kernel work.** Static scene, head moving (use the existing motion trigger, `D3D12Component.cpp:5524-5533`): dump SceneVelocity alongside the frame pair (extend the consecutive-frame dumper / `uevr_render_export_frame_pair_diff`), decode on CPU, and compare against the displacement predicted by `reproj_target_to_prev` per pixel. They must agree to sub-pixel on static geometry. This single experiment validates: the resource pick, the gamma decode, the zw depth packing, the jitter claim, AND the PrevView-eye-alternation assumption. If it disagrees, stop and diagnose before touching shaders.

### Phase 1 — Capture plumbing (CPU)

3. **Snapshot, don't reference.** This is the trap PureDark already hit, quoted verbatim in our own source (`D3D12Component.cpp:5040-5042`): "his depth/MV backups being overwritten by the next frame's DLSS pass before the warp consumed them." DIBR executes at present time; by then the pooled velocity texture can already be re-targeted by frame N+1's recording. Two options, in order of preference:
   - **v1 (cheap, measure first):** read the discovered resource directly at present time, exactly like depth's pool path does today. Depth measured clean on SN2 via pool selection (`:5043-5049`), so velocity *may* be equally clean — measure with the Phase-0 dump (a stale velocity shows up as one-frame-lagged motion on the diff). Note v1's risk is higher than depth's: velocity is consumed by DLSS/TSR early in the next frame, depth survives longer.
   - **v2 (correct under load):** every mechanism needed already exists in `dibr_depth_tracker`, written for depth:
     - *In-stream copy:* mirror `record_afw_depth_bind` (`DIBRDepthTracker.hpp:87-107`) — at a qualifying bind, record a `CopyResource` of the velocity texture into a small per-frame ring **directly inside the game's command list** (call before forwarding the bind; render passes must not be open around the copy — the existing call sites at `D3D12Hook.cpp:32118/32199` show the exact placement, via `record_probe_bind` which does the same in-stream copy trick for the translucency probe). The qualifying bind: the SceneColor + read-only-DSV signature the depth snapshot already keys on fires *after* the opaque Velocity pass (SN2 pass 13) and before translucency — correct for opaque velocity; it misses TranslucentVelocity (pass 14), which only present-time capture (v1) sees. Use the Phase-0 census dump to confirm the ordering on SN2.
     - *Frame identity:* reuse the **sequence-paired identity** machinery verbatim (`DIBRDepthTracker.hpp:109-135`, consumer at `D3D12Component.cpp:5082+`): the velocity resource RDG-ping-pongs between pooled textures frame-to-frame exactly like SceneDepthZ, so the same resource-pointer-change frame delimiting works; present k consumes seq k+offset with the same majority-vote anchor. Do not invent a second pairing mechanism, and do not key on the game-thread frame tag — it races recording by +1 (the documented snapshot lesson, `:5043-5049` and `hpp:118-122`).
4. **Thread it into synthesis.** Add `velocity, velocity_state` to `DIBRSynthesis::synthesize(...)` mirroring the depth pair (`:5619-5622`). Null ⇒ feature off, zero behavioral change.

### Phase 2 — Shader consumption (the yoro whole-eye blend first)

5. **Descriptor + root signature:** new SRV **t4** (`Texture2D<float4> g_velocityTex`) in `dibr_yoro.hlsl`, `dibr_scatter_fill.hlsl`, `dibr_afw_stash.hlsl`; `kDescriptorsPerSlot` 10 → 11 (`DIBRSynthesis.hpp:501`); extend the SRV range in the root signature/descriptor table; bind a 1×1 zero dummy when velocity is absent (sentinel reads as "not written" everywhere — kernels need no null-checking).
6. **cbuffer additions** (append to `DIBRStereoParams` and to **every** kernel's cbuffer block — it's auto-patterned and byte-identity-guarded; see costs in §7):
   - `float velocity_enabled` (0 = off, kernels keep current behavior exactly)
   - `float velocity_has_depth` (resource has 4 channels ⇒ zw depth delta present — derive from the captured resource's desc, don't assume)
   - `float4x4 reproj_target_to_source` (inverse of the source→target warp matrix `m`, built on CPU next to it at `:5454` — needed to find, for an output pixel, the source pixel whose velocity applies)
   - Velocity UVs reuse `TransformDepthUv` — SceneVelocity and SceneDepthZ share extent semantics, and the existing `depth_uv_*` auto-mapping (`:5590-5598`) already solves render-res vs display-res and double-wide.
7. **HLSL decode helper** (shared via the auto-patterning, same as `TransformDepthUv`):
   ```hlsl
   // Common.ush:1948-1968 inverse; sentinel first, gamma undo (SM5+ always), optional zw depth.
   bool DecodeVelocity(float4 enc, out float3 v) {
       v = 0;
       if (enc.x <= 0.0f) return false;                    // clear color = not written
       const float invDiv = 1.0f / (0.499f * 0.5f);
       v.xy = enc.xy * invDiv - (32767.0f / 65535.0f) * invDiv;
       v.xy = (v.xy * abs(v.xy)) * 0.5f;                   // VELOCITY_ENCODE_GAMMA undo
       if (velocity_has_depth > 0.5f) {
           v.z = asfloat((uint(round(enc.z * 65535.0f)) << 16)
                       | (uint(round(enc.w * 65535.0f)) & 0xFFFEu));
       }
       return true;
   }
   ```
8. **`dibr_yoro.hlsl` blend rework** (~2304-2341): before the existing matrix path, compute `srcClip = mul(reproj_target_to_source, float4(ndc, estDepth, 1))`, sample `g_velocityTex` at the source UV (point sampler — velocity must never be bilinearly mixed across object silhouettes). If decode succeeds:
   - `histNdc = srcNdc - v.xy`; `histUv` from that (replaces the `reproj_target_to_prev` result).
   - Validation key: if `velocity_has_depth`, validate the stashed `g_historyKey` against `srcDeviceZ - v.z` (exact, per-surface) instead of `estDepth ± 0.15*estDepth`.
   - **Relax the color-agreement gate when velocity advected the fetch** — e.g. widen `lumDiff * 8` to `* 2`, or trust the depth-exact validation outright. The gate's documented purpose (`:2329-2337`) is to catch unmodeled object motion; with motion modeled, keeping it at full strength would silently discard the entire win. Keep it intact on the sentinel/fallback path.
   - Decode fails (static pixel, no velocity write) ⇒ current matrix path unchanged — bitwise-identical behavior on a fully static scene is the regression invariant.
9. **`dibr_scatter_fill.hlsl` holes:** leave the camera-matrix path as primary (a disoccluded surface has *no* velocity sample of its own in the source view — the source pixel there belongs to the occluder). Two safe uses only: (a) when `velocity_has_depth`, sharpen the history-key tolerance the same way as in the blend; (b) Phase 3's occluder check.

### Phase 3 — Optional: history-velocity stash for hole rejection

10. Stash decoded velocity (RG16F or RGBA16F, eye-sized) alongside color+key in `dibr_afw_stash.hlsl` (new UAV u6, history pair + SRV in the fill — descriptors 11 → 13). The fill's hole branch then samples *last* frame's velocity at the history location: large magnitude ⇒ the candidate history pixel was itself a moving object ⇒ reject toward scanline fill (avoids stamping a displaced copy of a fish into a reveal). Defer until Phases 1–2 are measured; it may prove unnecessary if the depth-exact validation already rejects these.

## 5. Cross-version discovery design (UE4 → UE5)

Goal: one discovery mechanism that finds the velocity buffer on any UE4/UE5 D3D12 title, not just SN2. (DIBR synthesis is D3D12-only — `src/mods/vr/d3d12/` — so D3D11-only UE4 titles are out of scope until that changes; the same layered concept would port.)

### Verified per-version facts (from installed engine sources on this machine)

| | UE 4.26 / 4.27 (`E:\Epic Games\UE_4.2x`) | UE 5.5 / 5.6 (`E:\UnrealEngine\Source`, `E:\Epic Games\UE_5.6`) |
|---|---|---|
| Texture name | `Velocity` (separate velocity pass, RDG — `VelocityRendering.cpp:240`) **or** `GBufferVelocity` (base-pass velocity — `SceneRenderTargets.cpp:1143,1751`) | `SceneVelocity` (`SceneTextures.cpp:692`) |
| Allocation path | RDG name goes through `FindFreeElementForRDG → FindFreeElementInternal` (`RenderGraphBuilder.cpp:2032`, `RenderTargetPool.cpp:315-322`) — **not** the public `FindFreeElement` (`RenderTargetPool.cpp:618`) that UEVR's pool hook targets, so the common separate-pass case may be invisible to the hook even where it installs. `GBufferVelocity` uses the hooked legacy path but base-pass velocity is off by default in UE4. | UE5 RDG pool; hook visibility title-dependent, and on SN2 the hook doesn't install at all (`D3D12Component.cpp:5072-5074`) |
| Format | `PF_A16B16G16R16` if the shader platform supports ray tracing, else `PF_G16R16` (`VelocityRendering.cpp:334-337`) | Same two formats, gate broadened to Lumen-GI-or-RT (`VelocityRendering.cpp:358-374`) |
| xy encode | **Linear only**: `V.xy*(0.499*0.5) + 32767/65535` (`Common.ush:1527-1551`; `VELOCITY_ENCODE_GAMMA` confirmed absent in both 4.26 and 4.27) | Linear **plus gamma pre-encode** `sign(V)*sqrt(abs(V))*(2/√2)` on all SM5+ (`Common.ush:237-241, 1915-1924`) |
| zw encode | Prev-depth delta, full low word (`& 0xFFFF`), written unconditionally but dropped by RG16 formats | Same packing, low bit repurposed as HasPixelAnimation (`& 0xFFFE`) |
| Unwritten sentinel | Clear color (0,0) — identical | Identical |
| Extent | Scene buffer size == SceneDepthZ extent | Identical |

UE 5.0–5.4 sit between the verified brackets (gamma encode appeared somewhere in there; no source on this machine), and licensee forks (UWE added custom passes to SN2) can deviate arbitrarily — which is why the design below does not trust a version table for anything that can be measured instead.

### Three layers + a behavioral arbiter

The invariants that held unchanged from 4.26 through 5.6 are *behavioral*, not nominal: format ∈ {R16G16_UNORM, R16G16B16A16_UNORM}, extent exactly equal to scene depth, RTV-bound every rendering frame, cleared to zero, non-MSAA. Names and allocation paths churned across the same span. So:

1. **Layer 1 — device-level shape tracker (primary; fully version-agnostic).** The Phase-0 candidate tracker (`record_rtv` probe promoted to a `select_scene_velocity()`, §4). Keys only on the invariant row above; works identically on a 4.26 title and SN2 because `CreateRenderTargetView`/`OMSetRenderTargets`/`BeginRenderPass` hooks sit below the engine entirely. UE5's multiview path can create the texture as a 2-slice array (`VelocityRendering.cpp:382-387` / 5.5 `GetRenderTargetDesc`) — accept `DepthOrArraySize ∈ {1,2}` and require it to match the selected depth's arrayness.
2. **Layer 2 — pool-name lookup (confirmatory vote, never required).** Where the pool hook installs, probe all three names — `SceneVelocity`, `Velocity`, `GBufferVelocity` — via `rt_pool->get_texture()`. A hit that pointer-matches Layer 1's candidate confirms it; a hit that contradicts it wins (names don't lie when present). A miss means nothing (see the 4.26 `FindFreeElementForRDG` gap above).
3. **Layer 3 — behavioral calibration (the arbiter; also resolves the decode flavor).** A runtime self-test that needs no version knowledge: during head motion (reuse the existing motion trigger, `D3D12Component.cpp:5524-5533`), read back a sparse grid of candidate texels (~16, the existing eye-dump/readback plumbing suffices), decode each under **both** flavors (UE4-linear and UE5-gamma), and compare against the displacement `reproj_target_to_prev` predicts at those pixels from depth. Score = median pixel error; the (candidate, flavor) pair that agrees sub-pixel wins. Below-threshold agreement demotes the candidate and tries the next. This one mechanism simultaneously: (a) confirms buffer identity on any engine version, (b) picks linear-vs-gamma empirically — covering the unbracketed 5.0–5.4 range and licensee forks with custom encodes, (c) detects staleness (a one-frame-lagged buffer shows a systematic error proportional to frame-to-frame motion). `velocity_has_depth` continues to come from the resource format, which both engine generations agree on.
4. **Version prior, not version truth:** UEVR already knows the engine version (patternsleuth / game info); use it only to order Layer 3's trials (try the likely flavor first) and to log discrepancies, never to skip calibration.

Persist the calibrated result (resource shape + flavor + pool name if any) in the per-title profile so calibration runs once per game, re-validating only when the cached shape stops appearing.

## 6. Risks and traps

| Risk | Mitigation |
|---|---|
| **Stale velocity** (overwritten by next frame's DLSS/velocity pass before DIBR executes) — *the* known killer; bit PureDark's warp (`D3D12Component.cpp:5040-5042`) | Phase-0 dump measures it directly; v2 snapshot ring inside the recording stream is the fix. Under AFW a one-frame-stale velocity carries the *wrong eye's* baseline → IPD-scale misregistration, same failure class as the depth phase bug (`:5082-5088`) |
| **SN2 has no pool hook** (signature scan defeated, `:5072-5074`) | RTV format+extent discovery in `dibr_depth_tracker`; SN2 pass table (Velocity = 13) available as a per-pass hook point if heuristics misfire |
| Velocity pass absent or partial (`r.VelocityOutputPass`, no-TAA titles, `r.Velocity.ForceOutput=0` means static meshes never write) | Sentinel decode makes every unwritten texel fall back to today's behavior; `velocity_enabled=0` when no resource is found |
| Wrong decode flavor (gamma/depth packing) | Both pinned to UE source for SM5+ (`Common.ush:233-241`); `velocity_has_depth` derived from the actual resource format; Phase-0 CPU decode validates end-to-end before any kernel consumes it |
| PrevView under UEVR hooks isn't the clean other-eye camera (cf. `FFakeStereoRenderingHook.cpp:17729` frame-count workaround) | Phase-0 static-scene agreement test is the go/no-go gate |
| Color gate left too strict ⇒ feature silently no-ops | Make gate strength on the velocity path inspectable (debug_view mode showing velocity-advected vs camera-path vs rejected pixels — the existing `debug_view_mode` plumbing fits) |
| TranslucentVelocity (SN2 pass 14) writes after opaque velocity | Capture point must be after pass 14 (or at present time, which is naturally after) |

## 7. Costs

- **cbuffer edits recompile all 15 kernels** (layout is byte-identity-guarded across kernels; yoro ~13 s is the current slowest — see memory `dibr-kernel-compile-cost`). Batch all cbuffer/struct changes into one edit pass; never relaunch mid-compile.
- Runtime: one extra point-sample + ~10 ALU in the yoro blend per output pixel; one eye-res RGBA16 copy per frame for the v2 snapshot (~2 MB @1080p-eye). Both well under the per-pass GPU-timing noise floor; the existing `TsPass` brackets (`DIBRSynthesis.hpp:547`) will show it regardless.
- New resources: v2 snapshot ring (2–3 × eye-res RGBA16) + Phase-3 history-velocity pair if pursued.

## 8. Validation plan (existing instrumentation only)

1. **Phase-0 agreement dump** — static scene + head motion: decoded velocity vs `reproj_target_to_prev` prediction; sub-pixel agreement required (motion-triggered consecutive-frame dumper already exists, `:5524-5533`).
2. **Static-scene invariance** — velocity path enabled, fully static camera+scene: output must be pixel-identical to baseline (`uevr_render_ab_pixel_diff`).
3. **Animated content** — SN2 swaying plants/fish (the exact cases named in `dibr_yoro.hlsl:2331`): A/B `UEVR_DIBR_VELOCITY=0/1` via the `sn2-fog-experiment` harness; success = reduced half-rate shimmer on animated geometry with no new ghosting; measure with the per-frame flicker methodology (memory: `afw-diagnostic-methodology`) and eye dumps.
4. **Gate telemetry** — count velocity-advected vs sentinel-fallback vs gate-rejected pixels per frame (log like the GPU timings) to confirm the feature actually engages.

## 9. Suggested order of work

1. Phase 0 discovery + agreement dump (half a day; produces the go/no-go evidence).
2. Phase 1 v1 capture + plumb-through behind `UEVR_DIBR_VELOCITY` env gate, default off.
3. Phase 2 yoro blend + fill key sharpening, one batched cbuffer edit.
4. Measure (validation 2–4). Only then decide on v2 snapshot ring and Phase 3.
