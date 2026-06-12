# Hybrid Near-Geometry / Far-DIBR Rendering — Feasibility Deep Dive

*UEVR (`ue57performance` branch) — assessing a hybrid mode that renders near-field content as real stereo geometry and synthesizes the far field via DIBR, inspired by Oculus's Hybrid Mono Rendering (gearmono / UE 4.15 Monoscopic Far Field Rendering).*

## TL;DR

**This is not only possible — the codebase is unusually well prepared for it.** The verdict up front:

- Oculus's Hybrid Mono Rendering (gearmono → UE 4.15's "Monoscopic Far Field Rendering") renders near content in true stereo and far content once, compositing the mono far field flat into both eyes. Its core weakness was that the far field had **zero parallax** and was composited with a literal 2-pixel-shift hack — which is why they couldn't push the split plane closer than ~7.5–10 m.
- The DIBR pipeline is **the missing half of that technique**: it can reproject the far field with true per-eye matrices instead of compositing it flat. Conversely, hybrid is the missing half of DIBR: real near-field geometry eliminates exactly the artifacts DIBR suffers most (near-field disocclusion holes, weapon/cockpit warping, near translucency).
- The cost math is identical to Oculus's scheme **without needing a third engine view**: render one full reference view + one far-clipped near view in the existing two-view topology. Total work = 2× near field + 1× far field, same as gearmono's three cameras.
- The repo already has every required hook lever, verified at the line level: view-count control, per-view projection replacement (overscan already does asymmetric per-view projection edits), an FSceneView-ctor hook that patches `FSceneViewInitOptions` per view, the "additive overwrite" topology where the engine renders both halves and DIBR overwrites one, and the gearmono-style MONO baseline mode.
- The hard problems are not reprojection — they're **translucency rendered over void in the near-clipped eye, per-eye auto-exposure mismatch, and screen-space effects (Lumen/SSR) seeing only near content**. These are tractable but need honest mitigation, detailed below.

A phased plan exists where **Phase 1 is a pure-correctness composite mode with zero engine changes** that can be A/B'd against native stereo before any performance work lands.

---

## 1. What Oculus actually built (blog + gearmono + UE 4.15)

Source notes: the [blog post](https://developers.meta.com/horizon/blog/hybrid-mono-rendering-in-ue4-and-unity/) fetched fine. The [`4.12-gearmono` branch](https://github.com/Oculus-VR/UnrealEngine/tree/4.12-gearmono) itself returns 404 — `Oculus-VR/UnrealEngine` is private behind Epic's GitHub org, so the branch source isn't directly inspectable without an Epic-linked account. The technique was mainlined into stock UE 4.15 as "Monoscopic Far Field Rendering" for the mobile forward renderer ([UE docs](https://docs.unrealengine.com/en-us/Platforms/VR/MonoFarFieldRendering), [Meta docs](https://developers.meta.com/horizon/documentation/unreal/1.13/concepts/unreal-hybrid-monoscopic/)), then dropped from the engine years before UE5 — so nothing of it exists in the UE 5.6/5.7 titles this branch targets. Everything must come from UEVR's side.

### The pipeline (UE4 mobile forward renderer)

Three cameras on one plane: left eye, right eye, and a centered mono camera. On GearVR (symmetric frusta) the mono camera shares the stereo projection; on Rift the mono frustum is the **union of both asymmetric eye frusta** — exactly the union-frustum math UEVR's MONO mode already implements.

Render order per frame:

1. **Opaque in stereo** (both eyes), with the stereo depth buffers **far-clipped at the split plane**.
2. **Shift-and-combine the two stereo depth buffers** to build a mono occlusion mask, pre-populating the mono camera's depth buffer — pixels visible in *both* eyes' near fields don't get shaded again in mono.
3. **Opaque in mono**, near plane initialized at the split distance.
4. **Composite mono into both stereo buffers** with `(1, 1-dst_alpha)` blending and a **two-pixel lateral offset** to paper over the projection mismatch at the split plane.
5. **All translucency and post-processing in stereo**, after the composite — so translucents always blend against a complete background.

### Knobs and numbers

| Item | Value |
|---|---|
| Split plane | `World Settings → VR → Mono Culling Distance`, default 750 UU (7.5 m), adjustable per frame |
| Debug cvar | `vr.MonoscopicFarFieldMode 0–4` (0 off, 1 on, 2 stereo-only with culling, 3 stereo-only without culling — diffing 2 vs 3 reveals redundantly-stereo-rendered objects, 4 mono only) |
| Per-object opt-out | "Force Mono" flag, to stop big-bounds far objects from being submitted to the stereo passes |
| Performance | Epic SunTemple: 45 ms → 34 ms (~25%); Unity synthetic test: 10,846 → 8,583 draw calls, 48.7 → 97.7 FPS |

### Their stated limitations (all relevant here)

- Far field is **flat** — no parallax beyond the split plane, which forces the split to be far out.
- Translucency and post always stereo (cost floor); deferred/screen-space-heavy renderers benefit much less.
- Objects straddling the split render in both passes (wasted draws).
- Scenes dominated by near content can get *slower* — the third view has fixed costs.

---

## 2. Why DIBR changes the math

The key structural insight: **no third camera is needed.** Oculus's cost was 2×(near field) + 1×(far field) across three views. In UEVR's two-view topology the same total comes from rendering:

- **View A (reference eye): the full scene**, exactly as DIBR's source view today — this *is* the "mono far field" camera, it just also contains near content (which a normal z-prepass already keeps cheap).
- **View B (other eye): only the near field**, far-clipped at the split distance.

Then for the synthesized eye: composite **real near pixels** (from view B) over **DIBR-reprojected far pixels** (scattered from view A). The reference eye is 100% real, as it is in YORO mode today.

| | Gearmono (2016) | DIBR hybrid (proposed) |
|---|---|---|
| Far-field parallax | None (flat composite) | **True reprojection** via the existing R1 clip-to-clip matrices |
| Split-plane seam | 2-pixel lateral offset hack | Depth-aware reprojected composite + feather |
| Minimum practical split | ~7.5–10 m | Plausibly **2–4 m** (far synth is parallax-correct, so only disocclusion magnitude matters) |
| Disocclusion behind near objects | N/A (mono behind everything) | Existing scatter-fill + temporal/AFW history machinery |
| Third engine view needed | Yes | No |

The closer split matters doubly: the near pass gets cheaper *and* the residual DIBR workload loses its worst-case content. Disparity at 1920-wide/~90° is roughly `960 × IPD / depth` — ~30 px at 2 m but ~6 px at 10 m, and disocclusion hole width scales with the disparity *gradient*, which is overwhelmingly concentrated in the first few meters (weapons, cockpits, hands — exactly the content users notice warping today).

What can't be replicated from outside the engine: Oculus's step 2 (cross-view occlusion-mask pre-population of the mono depth buffer) and per-object "Force Mono" flags. Neither is load-bearing — the ref view's own z-buffer/prepass already limits far overdraw behind near geometry within that view, and the only true waste is the sliver of far pixels occluded in one eye but not the other.

---

## 3. Where it plugs into the existing code (verified)

Each of these was verified directly; this is the complete lever set the feature needs.

**Mode plumbing already anticipates this.** `RenderingMethod` at `src/mods/VR.hpp:44-56` already has `SYNTHETIC_DIBR = 3` and `MONO = 4` — the latter explicitly documented as "the Oculus gearmono-style baseline (union frustum via the symmetric projection overrides)". `is_mono_rendering_active()` (`src/mods/VR.cpp:2233`), `is_dibr_single_view_active()` (`VR.cpp:2252`), and `get_single_view_reference_eye()` (`VR.hpp:512`) define the activation pattern a new `is_dibr_hybrid_active()` would mirror — including the crucial "synthesis proven" arming gate at `VR.cpp:2279` that keeps the fallback safe.

**View count is controllable.** The `GetDesiredNumberOfViews` vtable hook (`FFakeStereoRenderingHook.cpp:20037`, installed at `15138-15179` alongside `GetViewPassForIndex`) currently returns 1 for single-view DIBR (`:20069`). Hybrid simply *doesn't* return 1 — it keeps the stock two views, so it works on any title where native stereo works, a strictly weaker requirement than today's single-view mode.

**Per-view projection replacement is already asymmetric-capable.** `calculate_stereo_projection_matrix` (`FFakeStereoRenderingHook.cpp:19836-19887`) fully replaces the engine's per-eye projection and already applies a per-view-index custom edit (the DIBR overscan scaling of `M[0][0]`/`M[2][0]` at `:19878-19887`, with double-precision handling). A far-clip clamp for the near-pass eye is the same shape of change in the same place. Concretely, the lone matrix edit is swapping the infinite-far reversed-Z coefficients for the finite pair (UE row-vector convention, matching the comment at `:19876`):

```
// infinite reversed-Z (current):  M[2][2] = 0,                    M[3][2] = near
// finite reversed-Z (near pass):  M[2][2] = -near/(split - near), M[3][2] = near*split/(split - near)
```

Geometry beyond the split then fails the `z_clip ≥ 0` test and is **hardware-clipped** — fill-rate savings on every title, no game-specific work. Engine-side depth reconstruction stays correct automatically because UE derives `InvDeviceZToWorldZTransform` per view from whatever projection it's given; only UEVR's own depth linearization needs to know the near half uses finite-far parameters.

**Draw-call culling has a per-game path.** UEVR already hooks the FSceneView constructor (`attempt_hook_fsceneview_constructor`, `FFakeStereoRenderingHook.cpp:5778`, `:6628`) and already reads/patches `FSceneViewInitOptionsUE5` fields per view at known offsets (the SN2 fog-flag block, `:1709`, `:1850`). Writing `OverrideFarClippingPlaneDistance` into the near view's init options the same way should engage UE's own far-plane culling in the visibility pass — i.e., actual draw-call elimination, the part that made gearmono fast on CPU-bound scenes. This needs per-game offset verification (same workflow as the existing SN2 init-options patches) and an in-game check that visibility culling honors it; treat it as the per-title optimization tier, not the baseline.

**The frame topology already exists.** Before synthesis is proven, DIBR runs in "additive overwrite" mode: the engine renders both eyes into the double-wide backbuffer and the compute pass overwrites the synthesized half (`VR.cpp:2276-2281`, staging at `D3D12Component.cpp:4754-4775`, `:5047-5053`). **Hybrid is exactly this topology** — the only changes are (a) the near eye's projection is far-clamped and (b) the overwrite becomes a depth-gated composite instead of a full replace. The scene depth is captured double-wide (the `UEVR_DIBR_DEPTH_UV_AUTO` half-addressing logic exists because of this), so the near eye's depth is already accessible to the kernels.

**Synthesis-side machinery reused as-is:** the R1 true-matrix reprojection (`reproj_source_to_left/right`, built in `D3D12Component.cpp` ~5098-5135), the scatter pipeline (clear → depth-key scatter with `InterlockedMax` occlusion → color match-fill → scanline+temporal fill), AFW history stash, the `StereoParams` cbuffer with its compile-time offset validation (`DIBRSynthesis.hpp:28-314`), the shader embed/caching flow (`tools/embed_dibr_shaders.ps1`, `UEVR_DIBR_SHADER_DIR` live-edit override), and instanced-stereo control (`disable_native_instanced_stereo_cvars`, `FFakeStereoRenderingHook.cpp:216-284` — multi-pass stereo, which hybrid wants, is already the default).

---

## 4. Proposed design

### Core mode: asymmetric two-view hybrid

Per frame:

1. Engine renders view 0 = reference eye, full scene (optionally with the existing overscan widening); view 1 = other eye, far-clamped at `split`.
2. Scatter passes reproject the reference view to the synthesized eye **with a near cutoff**: source pixels with linearized depth `< split` are skipped (one guard in `dibr_scatter_depth`/`dibr_scatter_color` reading a new `hybrid_near_cutoff` param). This produces a *far-only* synth layer and prevents the weapon being drawn twice (once real, once scattered) with sub-pixel disagreement shimmering at its edges. Temporal/AFW fill operates on this far layer unchanged.
3. New `dibr_hybrid_composite` kernel for the synthesized eye: linearize the near-eye depth; where `depth_world ≤ split` take the real near pixel, else take the scatter-filled far pixel; smoothstep-blend across a feather band around the split. Because the rule keys off *depth*, it is correct whether or not far geometry was actually culled — culling only determines the savings, never correctness. Reference eye remains passthrough.
4. Output packing, OpenXR submit, UI layer: unchanged.

Object straddling the split: hardware clipping cuts it at the plane in the near view; its far portion comes from the (parallax-correct) synth layer, and since both derive from the same geometry under a true reprojection they coincide to within reprojection accuracy — the feather hides the residual. This is strictly better than Oculus's flat composite at the same boundary.

### New parameters / UI

- `DIBR_HybridSplit` (UU, default ~300; displayed in meters via `world_to_meters`), `DIBR_HybridFeather`, plus `hybrid_near_cutoff`/`hybrid_split_depth` appended to `StereoParams` (respecting the offset-layout validation).
- Debug views mirroring `vr.MonoscopicFarFieldMode`: near-only, far-synth-only, boundary heatmap, composite-source visualization. The 2-vs-3 diff trick has an analog: "near pass without far clamp" vs "with" exposes what the clamp is actually culling.

### Phased rollout (each phase independently shippable and testable)

- **Phase 0 — measure.** Use the existing GPU timing instrumentation to record, per title: full-stereo cost, single-view DIBR cost, and (via a temporary far-clamped projection toggle) the near-pass cost at candidate split distances. This decides whether Phase 2+ is worth it per game.
- **Phase 1 — composite correctness mode, zero engine changes.** Both views render the *full* scene (plain native stereo); the hybrid composite replaces far pixels of one eye with DIBR synth. No perf win — but a perfect ground-truth A/B: toggling the composite on/off isolates far-synth quality and boundary behavior with the real render available for diffing (the eye-dump/pixel-diff forensics tooling applies directly).
- **Phase 2 — projection far clamp.** Universal fill-rate/pixel-shading savings, no per-game work. This is the default shipping tier.
- **Phase 3 — `OverrideFarClippingPlaneDistance` via the FSceneView-ctor hook.** Draw-call/Nanite/visibility savings, per-game offsets, per-game validation.

### Extensions (in rough value order)

- **AFW-hybrid:** alternate which eye is the full view per frame using the existing AFW alternation + history machinery. Each eye gets a real far field at half rate and hybrid composite on off-frames — symmetric quality, and the far-synth layer gains real same-eye history.
- **Half-rate far field:** with AFW-hybrid in place, the far field is effectively temporally upsampled already; an explicit half-rate far view is the same idea expressed differently and probably not needed separately.
- **Symmetric three-view gearmono** (centered union-frustum far view + two near views): the only variant needing `GetDesiredNumberOfViews` to return 3 and a triple-wide RT. The hooks could express it, but games' assumptions about view counts make this high-risk for **zero cost advantage** over the asymmetric design (both are 2×near + 1×far). File under "research, only if asymmetric artifacts prove objectionable."

---

## 5. Hard problems — the honest list

1. **Translucency over void in the near-clipped eye (the big one).** Oculus rendered all translucency *after* compositing mono into the stereo buffers; a shipped game's passes cannot be reordered. A near translucent surface with no opaque backing inside the split (glass against a vista, smoke against sky) writes color but no depth in the near view → the composite's depth test selects the far synth → the effect vanishes from that eye. Mitigations, in order of practicality: (a) default the split far enough that common near translucency has opaque backing within it — even 3–4 m covers weapons/cockpits; (b) a recover heuristic in the composite: where near depth is empty but near color deviates from the clear color, additively re-blend the near color over the far synth — approximately correct for additive FX (muzzle flashes, sparks), plausible for alpha blends; (c) per-game split tuning. Note this failure mode is *narrower* than what DIBR has today (where all translucency fights the opaque depth buffer in the synthesized eye); the existing translucency-forensics tooling (bind census, pre-translucency probes) is exactly the instrument for quantifying it per title.
2. **Per-eye auto-exposure.** The near view's eye adaptation sees only near content → exposure mismatch both between eyes and *within* the composited eye (near band vs far band). Mitigation: force fixed exposure via cvars when hybrid is active (the cvar-forcing infrastructure at `FFakeStereoRenderingHook.cpp:216` is the template), or measure and correct the gain in the composite kernel.
3. **Screen-space effects see only near content in the near view.** SSR on a car hood won't reflect the vista; Lumen screen traces/probes differ between eyes. This is the modern restatement of Oculus's "deferred renderers benefit less." Per-game assessment; worst case the answer is "hybrid quality tier requires turning down per-view screen-space effects," and UE5 titles are exactly where the perf win is biggest, so this is a genuine tension to measure, not hand-wave.
4. **HUD/UI.** Screen-space UI drawn into the near view over void would be erased by the composite. Where UEVR's UI-layer separation (quad layer) is active this is a non-issue; otherwise the translucency-recover heuristic doubles as the HUD fallback.
5. **Shadow cascades.** Per-view CSM over [near, split] in the near view gives different cascade distribution than the ref view — slight per-eye shadow LOD mismatch in the near field. Likely invisible; verify in Phase 1.
6. **Instanced stereo.** Must stay multi-pass (it's already UEVR's default). Under ISR, per-eye projections still clip correctly but culling is shared — Phase 2 savings only, no Phase 3. The SN2 keep-ISR experiment (`:251-266`) would be incompatible with the culling tier.
7. **Does a finite projection far plane actually cull draws (not just clip pixels)?** UE's visibility frustum construction does not always include the far plane unless `OverrideFarClippingPlaneDistance` is set — which is precisely why Phase 3 exists and why Phase 2's claim is limited to fill-rate. Verify per engine version in-game (draw-call counts via the existing render-stats tooling).

---

## 6. Expected performance, honestly framed

Let `f` = the far field's share of a full view's GPU cost (open-world UE5 titles: plausibly 0.5–0.8 including Lumen/Nanite/shadow work attributable to distance). Per-frame engine cost, relative to native stereo = 2.0 views:

| Mode | Engine cost | Near-field quality | Far-field quality |
|---|---|---|---|
| Native stereo | 2.0 | real | real |
| DIBR single-view today | ~1.0 (+~overscan) + compute | synthesized (worst artifacts) | synthesized (good) |
| **Hybrid (Phase 2/3)** | **~1.0 + (1 − f) ≈ 1.2–1.5** + compute | **real** | synthesized (good, small disparities only) |
| Gearmono reference | — | Oculus measured ~25% total frame savings on SunTemple with a 7.5–10 m split | flat |

So hybrid sits deliberately between DIBR and native stereo: it gives back roughly half of DIBR's savings to buy back the near field — which is where essentially all of DIBR's perceptual failures live. Two framings for users: a *quality tier above DIBR* for those who can afford ~1.3 views, and a *performance tier below native stereo* with near-perfect near-field fidelity. The added compute (one extra composite dispatch + a guard in two scatter kernels) is noise next to the existing pipeline. Phase 0 measurement should replace these estimates with per-title numbers before tuning the default split.

---

## 7. Bottom line

The Oculus technique and the DIBR pipeline are complementary halves: they had real near-stereo with a flat far field; UEVR has a reprojected far field with a synthesized near field. The merge needs **no third engine view, no engine pass reordering, and no new hook classes** — it's a far-clip clamp in a hook that already edits projections per view (`FFakeStereoRenderingHook.cpp:19878`), a near-cutoff guard in two existing scatter kernels, one new composite kernel, and an activation predicate alongside `is_dibr_single_view_active()`. The genuinely open risks are translucency-over-void and per-eye exposure/screen-space coherence — all measurable with already-built instrumentation, and Phase 1 provides a ground-truth A/B harness before any performance behavior changes.

## Sources

- [Hybrid Mono Rendering in UE4 and Unity (Meta blog)](https://developers.meta.com/horizon/blog/hybrid-mono-rendering-in-ue4-and-unity/)
- [Hybrid Monoscopic Rendering (Meta UE docs)](https://developers.meta.com/horizon/documentation/unreal/1.13/concepts/unreal-hybrid-monoscopic/)
- [UE4 Monoscopic Far Field Rendering docs](https://docs.unrealengine.com/en-us/Platforms/VR/MonoFarFieldRendering)
- [Oculus-VR/UnrealEngine `4.12-gearmono`](https://github.com/Oculus-VR/UnrealEngine/tree/4.12-gearmono) (private — 404 without an Epic-linked GitHub account)
- [UE 4.15 release notes](https://www.unrealengine.com/blog/unreal-engine-4-15-released)
- [ARM: Monoscopic far field rendering for mobile VR](https://developer.arm.com/documentation/100959/0100/virtual-reality-optimization-techniques/monoscopic-far-field-rendering-for-mobile-vr)
