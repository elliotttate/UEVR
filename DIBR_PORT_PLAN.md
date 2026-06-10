# DIBR Port Plan: vrmod-private → UEVR (UEVRJ fork)

Goal: port the complete DIBR stereo-synthesis system (with all uncommitted improvements) from
`E:\Github\vrmod-private\crates\vrmod-stereo` into this UEVR fork as a first-class
**Synthetic Stereo (DIBR)** rendering feature.

---

## 1. What is being ported (source inventory)

All from the vrmod-private **working tree** (uncommitted state — do not use git HEAD, the
improvements are uncommitted):

| Source (vrmod-private) | Content | Port target |
|---|---|---|
| `crates/vrmod-stereo/src/dibr_inverse.hlsl` (~1.9k lines) | Symmetric inverse-warp DIBR, single pass | `src/shaders/dibr/dibr_inverse.hlsl` (near-verbatim) |
| `crates/vrmod-stereo/src/dibr_yoro.hlsl` (~1.9k lines) | YORO asymmetric: reference eye pristine passthrough, other eye carries full disparity | `src/shaders/dibr/dibr_yoro.hlsl` |
| `crates/vrmod-stereo/src/dibr_raymarch.hlsl` (~2.1k lines, **new/untracked**) | Steep-parallax depth-field raymarch with foveated step reduction, gradient-based guards | `src/shaders/dibr/dibr_raymarch.hlsl` |
| `crates/vrmod-stereo/src/lib.rs` (dispatch layer) | Root sig, 3 PSOs, 258-field `StereoParams` CB, SRV typeless-depth conversion, packed-SBS output + per-eye split | New C++ class `src/mods/vr/d3d12/DIBRSynthesis.cpp/.hpp` |
| `crates/vrmod-inject/.../depth_select.rs` | Depth-candidate ranking heuristic (format tiers, aspect rejection) | Optional fallback only — UEVR already has `SceneDepthZ` via rt_pool |
| `crates/vrmod-inject/.../compute_depth.rs` | Cross-API depth copy bridge | **Not needed** (no D3D11 cross-API hop in UEVR's D3D12 path) |
| `crates/vrmod-stereo/src/deghoster_d3d12/` | NVOF optical-flow AFR deghoster | Separate optional phase (orthogonal to DIBR) |

Shader facts (identical across all three variants):
- Entry `CSMain`, `[numthreads(16,16,1)]`, dispatch `((w+15)/16, (h+15)/16, 1)`
- Bindings: `t0` color SRV, `t1` depth SRV, `u0` output UAV, `s0` linear clamp, `s1` point clamp, `b0` StereoParams
- Compiled with FXC `cs_5_0`, `D3DCOMPILE_OPTIMIZATION_LEVEL3`
- Single dispatch per frame; **all** depth conditioning (expand/reconstruct/linearize/dither),
  guards, masks, overlays, and composition happen shader-side. No pre-passes.
- Output: packed texture, BGRA8. Layout mode 0 = side-by-side double-wide — which is exactly
  UEVR's native double-wide format.

The 258-parameter suite groups as: core stereo geometry, depth conditioning (~40), disocclusion
+ edge guards (~16), artifact guards (8), depth expand/reconstruct (12), letterbox manual+auto
(13), region/weapon masks (~28), focus reduction (5), output matte (8), cursor overlay (10),
UI alpha/auto masks (11), shape mask (11), comfort nose (9), image filter sharpen/AA/deband (11),
output geometry/distortion/per-eye alignment (~31), composition + anaglyph (12 modes) +
interlace + frame markers, debug views, raymarch foveation. Defaults live in
`impl Default for StereoParams` in `lib.rs` — port the table 1:1.

---

## 2. Why DIBR in UEVR (the three payoff modes)

1. **Synthesized right eye (YORO, left reference)** — game renders ONLY the left eye; the right
   eye is synthesized from left color + depth. This sidesteps *the entire class* of right-eye
   rendering bugs this fork has been fighting in SN2 (fog mirroring, right-eye cbuffer synth,
   missing dispatches) and cuts scene GPU cost nearly in half. The left eye stays pristine
   (no resampling), so the dominant-eye image is perfect.
2. **Mono → stereo with parallax** — the fork's SHF mono-cinematic expansion
   (`D3D12Component.cpp:2812-2823`) currently duplicates a mono scene flat into double-wide and
   drops depth (`:3207`). DIBR (inverse or raymarch) upgrades that to true parallax when depth
   is available.
3. **Compatibility fallback** — games where native stereo hooking fails outright can still get
   stereo from one rendered view + depth.

---

## 3. Verified integration points in this fork

| What | Where | Status |
|---|---|---|
| Scene depth access | `src/mods/vr/D3D12Component.cpp:3178-3202` — `rt_pool->get_texture<ID3D12Resource>(L"SceneDepthZ")` | verified |
| Frame submission / per-eye copies | `D3D12Component.cpp:3217-3470` (left-eye copy ~3254, right ~3363, depth layer copies 3256/3324/3365) | verified |
| Mono expansion hook point | `D3D12Component.cpp:2812-2823` (`shf_using_mono_expansion`) | verified |
| Compute-pass precedent (root sig, PSO, dispatch) | `src/hooks/D3D12Hook.cpp` ~18554-18644 (SN2 fog reproject compute) | verified pattern |
| Runtime shader compiler (FXC + DXC) | `src/render/ShaderCompiler.cpp` | verified exists |
| Command list + fence wrapper | `src/mods/vr/d3d12/CommandContext.cpp/.hpp` | verified exists |
| Settings/persistence framework | `src/Mod.hpp` `ModValue<T>` + `utility::Config`; UI in `src/mods/VR.cpp` `on_draw_ui()` | per Explore report |
| Build inclusion | `cmake.toml` ue4template globs `src/**.cpp` / `src/**.hpp` — new files picked up automatically | verified pattern |

---

## 4. Architecture decisions

1. **New component, not an Sn2 hook.** `src/mods/vr/d3d12/DIBRSynthesis.{hpp,cpp}` — this is a
   general feature; keep it out of the `Sn2*` namespace. A thin mode enum + params owner lives
   in `VR.cpp` mod settings.
2. **Consolidate the cbuffer into one include.** The three vrmod shaders each duplicate the
   258-field cbuffer block and ~80% of helper functions. Port as
   `src/shaders/dibr/dibr_common.hlsli` (cbuffer + shared helpers: depth conditioning, guards,
   masks, overlays, output geometry, composition/`WriteStereoPair`) + three thin variant files
   containing only their warp strategy (`InverseWarpUv` / YORO branch logic / `RaymarchUv`).
   This kills the triple-maintenance problem and makes the C++ `StereoParams` struct match a
   single source of truth. Add `static_assert(sizeof(StereoParams) == <N>)` and a unit check
   that field count/order matches.
3. **Compile at runtime from embedded source** (same as vrmod and the fog-reproject precedent):
   embed the `.hlsl`/`.hlsli` as raw string literals in a generated/checked-in header
   (`dibr_shaders_embedded.hpp`). Mind MSVC's 64KB-per-literal limit — split into concatenated
   chunks or embed per-section. FXC `cs_5_0` keeps us off any DXC redistribution concerns;
   the sources are already cs_5_0-clean.
4. **Output to packed SBS double-wide, owned by DIBRSynthesis.** The game backbuffer lacks
   UAV flags, so the shader writes to our own `DEFAULT`-heap BGRA8 double-wide UAV texture;
   the existing per-eye copy code then consumes *that* texture instead of the backbuffer.
   This means zero changes to swapchain/submission logic. (vrmod's per-eye split path —
   `CopyTextureRegion` into two per-eye textures — is an optional later optimization.)
5. **Record into the existing command flow; no synchronous fence waits.** vrmod's Rust layer
   does a blocking `WaitForSingleObject` per dispatch — replace with recording the dispatch +
   barriers into the same `CommandContext` UEVR already uses for submission copies, so it
   serializes naturally on the queue.
6. **Keep ALL parameter groups**, including the "game-injection-specific" ones (weapon masks,
   focus reduction, region masks, cursor overlay). They are useful for UE FPS titles too.
   Defaults keep everything off (matching vrmod defaults), so the baseline behavior is the
   plain algorithm.

---

## 5. UEVR-specific upgrades to make during the port

These are places where UEVR has *better information* than vrmod (a blind screen-space injector)
ever had — wire them in rather than porting the guesswork:

- **Reversed-Z**: UE uses reversed-Z. Default `reverse_depth = 1.0` (vrmod had to detect this;
  we know it).
- **Real projection**: auto-fill `depth_linearize_near/far` and `depth_linearize_mode` from
  UEVR's actual projection matrices instead of user guessing. Expose manual override.
- **Real IPD/world scale**: derive a physically-grounded default `divergence`/`convergence`
  from headset IPD × world scale × FOV. The vrmod values (divergence 30 px) were tuned for
  flat-screen content; UEVR can compute the geometrically correct disparity and let the
  parameter act as a multiplier.
- **UI is already separated**: UEVR composites slate/UMG UI as its own layer, so game UI never
  passes through DIBR. The `ui_alpha_mask`/`ui_auto_mask`/`letterbox` groups stay ported but
  matter far less here — document them as "for games where UI is baked into the scene".
- **Depth/color dimension mismatch**: SceneDepthZ can differ from backbuffer size (dynamic res,
  TAAU). Auto-derive `depth_uv_scale_x/y`, `depth_uv_offset_x/y` from the two resource descs
  each frame instead of manual sliders (keep sliders as trim).
- **Depth-layer interplay**: when DIBR is active we still have real SceneDepthZ — keep
  submitting it for `XR_KHR_composition_layer_depth` reprojection (per-eye-approximate), with
  a toggle to disable if runtimes misbehave.

---

## 6. Implementation phases

### Phase 1 — Foundation: compute pass + shaders (no behavior change)
1. `DIBRSynthesis` class: device init, root signature (descriptor table: SRV t0-t1 + UAV u0;
   root CBV b0; static samplers s0 linear / s1 point — port exactly from `lib.rs:1251-1338`),
   three PSOs, persistent-mapped upload CB, 3-slot shader-visible descriptor heap, output
   texture lifecycle (resize on dimension/layout change).
2. Port the **typeless-depth SRV conversion table** from `lib.rs` verbatim:
   `R32_TYPELESS→R32_FLOAT`, `R24G8_TYPELESS→R24_UNORM_X8_TYPELESS`,
   `R32G8X24_TYPELESS→R32_FLOAT_X8X24_TYPELESS`, `R16_TYPELESS→R16_UNORM`, passthrough typed.
3. Port shaders into `dibr_common.hlsli` + 3 variants; embed; compile via FXC at init;
   log + hard-disable on compile failure (never crash the game).
4. Port `StereoParams` struct + the full defaults table.
   **Exit criteria**: shaders compile in-game; dispatch on a test pattern produces a valid SBS
   image (use the built-in `debug_view_mode` depth heatmap to validate the depth binding).

### Phase 2 — Pipeline integration
5. Insert into `D3D12Component` after `scene_depth_tex` acquisition (~line 3182) and before
   the per-eye copies (~3225): when enabled and depth present, dispatch DIBR
   (color = backbuffer or its left half, depth = SceneDepthZ) → redirect the per-eye copy
   source to the DIBR output texture.
6. Mode plumbing (enum): `Off | InverseWarp | YORO (synth right) | YORO (synth left) | Raymarch`.
   - **YORO synth-right**: source = left half of native-stereo double-wide; reference eye
     passthrough = left; engine right-eye rendering can later be skipped entirely (follow-up:
     force mono view family / cull right-eye pass for the perf win — start by just overwriting
     the right half so it's purely additive and safe).
   - **Mono modes**: hook the `shf_using_mono_expansion` path (2812-2823) — feed the mono
     scene texture as DIBR source instead of flat duplication; stop dropping
     `scene_depth_tex` at :3207 when DIBR is on.
7. Barriers: depth `ENGINE_SRC_DEPTH → NON_PIXEL_SHADER_RESOURCE` and back; color source →
   `NON_PIXEL_SHADER_RESOURCE`; output `COMMON ↔ UNORDERED_ACCESS`. Record into the existing
   command context (decision #5).
   **Exit criteria**: SN2 (or any UE game) showing synthesized stereo in-headset in each mode;
   verified via openxr-simulator anaglyph/eye-screenshot tooling.

### Phase 3 — UI, config, presets
8. "Synthetic Stereo (DIBR)" collapsing header in `VR::on_draw_ui()`: master enable + mode
   combo + grouped sub-sections mirroring §1's parameter groups (Depth, Comfort/Guards,
   Disocclusion, Masks, Output Geometry, Composition/Preview, Debug). Back every param with
   `ModValue<float>` so persistence and per-game config come free.
9. Presets: `Comfort / Balanced / Pop-out / Cinema` mapping onto vrmod's tuned defaults; a
   "reset group" button per section. (258 raw sliders are for power users; presets are the UX.)
10. Expose `debug_view_mode` prominently (depth heatmap / disparity / mask view) — it is the
    primary diagnostic when a game's depth binding is wrong.

### Phase 4 — Validation & performance
11. Test matrix:
    - **SN2** (motivating title): YORO synth-right vs native stereo — fog/right-eye artifact
      classes should vanish in DIBR mode; compare with existing RenderDoc/PIX tooling.
    - One known-good native-stereo UE title (regression: DIBR off must be byte-identical path).
    - Mono cinematic case (SHF expansion upgrade).
12. Perf: GPU-time the dispatch with the fork's existing timing tooling. Budget ≤1.5 ms at
    headset res for inverse/YORO; raymarch uses `raymarch_foveation_*` (default radius 0.35,
    min 12 steps) to stay in budget.
13. Edge-case hardening: depth missing (auto-bypass + one-time log), resolution change
    (recreate output/SRVs), swapchain format ≠ BGRA8 (SRV cast table), device loss.

### Phase 1 implementation notes (as built) — **STATUS: COMPLETE (2026-06-09)**

Files: `src/mods/vr/d3d12/DIBRSynthesis.{hpp,cpp}`, `src/mods/vr/d3d12/DIBRShadersEmbedded.hpp`
(generated), `src/mods/vr/d3d12/shaders/dibr_{inverse,yoro,raymarch}.hlsl`,
`tools/embed_dibr_shaders.ps1`. Builds clean into `build/bin/uevr/UEVRBackend.dll`.
All three kernels verified compiling: inverse + yoro via FXC cs_5_0, raymarch via DXC cs_6_0.

- **Async compile is mandatory, not a nicety:** measured kernel compile times are ~1-2 min
  (DXC cs_6_0, raymarch) and 10+ min (FXC cs_5_0 -O3, raymarch — abandoned; DXC is the
  primary backend with FXC fallback). `ensure()` is non-blocking: a worker thread compiles
  and builds all device objects; `synthesize()` no-ops until ready. Compiled bytecode is
  disk-cached (`%LOCALAPPDATA%\UEVR\dibr_shader_cache\`, FNV-1a source hash) and the cache
  has been pre-seeded with the kernels compiled during this phase.
- **Verbatim shader port instead of `dibr_common.hlsli` consolidation.** The three
  shaders were copied unmodified to guarantee identical behavior; the layout-drift risk
  the consolidation was meant to address is covered instead by a runtime reflection check
  in `DIBRSynthesis::create_psos` (compiled cbuffer size + field count must match
  `sizeof(DIBRStereoParams)`). Consolidation remains a future cleanup.
- All three cbuffers were verified field-identical (243 four-byte scalars, same order),
  so one `DIBRStereoParams` struct serves all kernels. The struct was machine-generated
  from the HLSL cbuffer (order) + vrmod's Rust `Default` impl (values).
- **Upstream shader bug found & fixed:** `OutputDistortionGridColor()` used `line` as a
  local variable name — an HLSL reserved keyword; FXC rejects it, so the uncommitted
  vrmod copies cannot currently compile either. Renamed to `gridLine` in all three
  UEVRJ copies (report back to vrmod-private).
- Shader sources are embedded via `tools/embed_dibr_shaders.ps1` →
  `DIBRShadersEmbedded.hpp` (chunked raw-string literals under MSVC's limits), with an
  `UEVR_DIBR_SHADER_DIR` env override for on-disk iteration, and compiled DXBC is cached
  under `%LOCALAPPDATA%\UEVR\dibr_shader_cache\` keyed by source hash (FXC -O3 on these
  kernels takes seconds-to-minutes; the cache removes the first-enable hitch).

### Phase 2 implementation notes (as built) — **STATUS: COMPLETE (2026-06-09)**

Integration in `D3D12Component`: `run_dibr_synthesis()` (declared in the hpp, implemented
above `on_reset` in the cpp) is called once per presented frame right after
`sn2_color_transfer::run` and before the per-eye copies.

- **In-place backbuffer rewrite.** The flow is: stage the LEFT HALF of the SBS backbuffer
  into `m_dibr_source` (single-eye staging texture) → dispatch the selected kernel
  (color = staging, depth = SceneDepthZ) → copy the packed SBS output back OVER the
  backbuffer. Every downstream consumer (OpenXR AFR/native-split/array copies, OpenVR
  submits, desktop mirror) sees synthesized stereo with **zero changes** to submission code.
- **Own `CommandContext`** (`m_dibr_commands`) executed before the eye-copy contexts —
  queue ordering guarantees the rewrite lands first; no CPU stalls.
- **Depth is re-resolved independently** of UEVR's depth-submission setting (direct
  `SceneDepthZ` pool lookup fallback), since DIBR needs depth even when depth-layer
  submission is off. Suppressed-depth paths (mono expansion, debug toggles) fall through
  to the lookup; if no depth exists, the frame passes through untouched (logged at 5 s
  cadence).
- **Auto depth-UV alignment**: if SceneDepthZ is double-wide relative to the single-eye
  source (width ratio > 1.5), `depth_uv_scale_x=2, depth_uv_anchor=1` maps output UVs to
  the depth texture's left half (TransformDepthUv divides by scale).
- **Format safety**: output format = backbuffer's copy-family UNORM/FLOAT equivalent,
  verified for `D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE`; sRGB backbuffers are viewed as
  raw UNORM through the whole pass (no double-decode). Unsupported family → DIBR
  disables with a one-time log.
- **Env-gated (Phase 3 replaces this with the ImGui panel):**
  `UEVR_DIBR` = `yoro`/`synth_right` (default when `1`) | `yoro_left`/`synth_left` |
  `inverse` | `raymarch`; plus `UEVR_DIBR_DIVERGENCE`, `UEVR_DIBR_CONVERGENCE`,
  `UEVR_DIBR_REVERSE_DEPTH` (default 1 — UE reversed-Z), `UEVR_DIBR_DEBUG_VIEW`
  (1 = depth heatmap — first validation tool), `UEVR_DIBR_RAYMARCH_STEPS`,
  `UEVR_DIBR_DEPTH_UV_AUTO`.
- Skipped paths: extreme-compatibility mode (full-buffer-per-eye semantics don't fit the
  in-place SBS rewrite; one-time log). AFR works: the left half stays pristine source,
  the right half is synthesized, and each AFR frame's copies stay coherent.
- Known deferred items: deriving divergence/convergence from real IPD × projection
  (Phase 3, alongside UI), per-eye split submit without the copy-back (Phase 5 item 14),
  skipping the engine's right-eye render in YORO mode (Phase 5 item 15).

### Phase 3 implementation notes (as built) — **STATUS: COMPLETE (2026-06-09)**

- **UI**: "Synthetic Stereo (DIBR)" tree node in `VR::on_draw_ui` (VR.cpp, after Native
  Stereo Fix): mode combo with live pipeline status (`idle / compiling kernels... /
  ready / FAILED` + Retry button), Comfort/Balanced/Pop-out preset buttons, core sliders
  (divergence / convergence / auto-convergence balance), "Depth Input" and
  "Quality / Disocclusion" sub-trees, and a session-only Debug View combo (deliberately
  not persisted — a saved debug view would boot the next session into a diagnostic image).
- **Persistence**: 16 curated `ModValue`s (`DIBR_*` keys) registered in `VR::m_options` —
  saved/loaded through UEVR's standard per-game config like every other setting. The
  remaining ~225 cbuffer params keep their vrmod defaults; surfacing more is incremental.
- **Env interplay**: `UEVR_DIBR*` env vars now act as *overrides* over the UI (scripted
  testing keeps working); with no env set, the UI drives everything.
- **Review-pass fixes made while here**:
  - `DIBRSynthesis::reset()` is now **non-blocking during kernel compilation**: builds are
    generation-stamped, build into local objects, and commit under a mutex only if their
    generation is still current — a device reset (resolution change, fullscreen toggle)
    mid-compile abandons the build instead of freezing the render thread for minutes.
    `ensure()` reaps an abandoned worker after it drains; only the destructor joins.
  - Output-format/staging-texture recreation in `run_dibr_synthesis` was moved **after**
    the command-context fence wait (previously could release GPU-in-flight resources on a
    resolution/format change).
  - True-AFR (Alternating) now skips synthesis with a one-time log — the alternating
    submission only consumes the rendered eye's half, so the synthesized half was wasted
    GPU work.

### Triple-check audit (2026-06-09, post-Phase 3)

Verified with hard evidence:
- **Cbuffer layout byte-exact**: FXC reflection of the compiled kernels shows tight
  sequential packing (`divergence`@0 ... `stereo_axis_mode`@964, `frame_index`@968; 243
  fields, 972 bytes) — identical to `DIBRStereoParams`.
- **Encoding chain sound**: `/utf-8` is set project-wide (cmake.toml), so the embedded
  shader strings are byte-identical to the .hlsl files and the runtime source hash matches
  the pre-seeded bytecode cache keys. Cross-check: regenerating the embedded header after
  the yoro CRLF normalization shrank it by exactly 31 bytes (the 31 removed `\r`s).
- **Cache hygiene**: removed garbage-keyed entries left by the first (overflowing)
  PowerShell hash attempt + the stale pre-normalization yoro key; cache now holds exactly
  one valid entry per kernel.
- **`mode_param0` (YORO reference eye) is live in the yoro kernel** (not in its unused
  list) — the synth-left/right switch genuinely works.

Fixed during the audit:
- Runtime cbuffer guard was FXC-specific (expected the 16-byte-padded size); DXC's DXIL
  reflection may report unpadded and would have refused to enable the raymarch kernel.
  Now keys on field count + `frame_index` offset with a sanity size range.
- `on_reset` ordering: the command-context reset (which waits for in-flight GPU work) now
  runs before the DIBR textures it references are released.
- Removed redundant per-frame depth/color barriers when the incoming state already
  includes `NON_PIXEL_SHADER_RESOURCE` (always true for `ENGINE_SRC_DEPTH/COLOR`).
- Skip synthesis in 2D Screen Mode (flat virtual screen — parallax never visible).
- `UEVR_DIBR_*` value overrides now parse independently of the `UEVR_DIBR` master switch,
  so scripted runs can pin individual values while the UI drives the mode.

**Per-kernel parameter usage (from shader reflection — important for UI and Phase 6):**
- Used by ALL kernels: `divergence`, `convergence`, `zpd_balance`, `reverse_depth`,
  `edge_fill`/`edge_fill_mode`, `edge_guard_*`, depth-UV transform, masks/overlays/output
  geometry/composition, `debug_view_*`.
- **Raymarch-only** (25 params dead in inverse AND yoro): `depth_floor/ceiling/gain/curve`,
  `popout_limit`, `range_smoothing`, `foreground_protect`, `raymarch_steps`,
  `near_field_*`, `auto_depth_*`, `disocclusion_*`, `raymarch_foveation_*`.
  The UI groups these under "Raymarch Quality" accordingly.
- **Phase 6 candidate**: port vrmod's advanced depth conditioning (gain/curve/floor/
  ceiling/near-field/auto-depth) from the raymarch kernel into the shared depth sampling
  of inverse/yoro — currently those modes only get the raw conditioned depth.

### Phase 4.5 — Single-view rendering (Phase 5 item 15) — **STATUS: IMPLEMENTED (2026-06-10)**

The engine now renders ONLY ONE view when DIBR is active, using the same machinery AFR
already uses (GetDesiredNumberOfViews → 1), but pinned to the reference eye instead of
alternating. ~2× scene GPU saving; this is the original "render left, synthesize right"
behavior from §2 payoff 1.

- **Policy** (`VR::is_dibr_single_view_active`, VR.cpp): any DIBR mode enabled (env or UI)
  AND rendering method == plain Native Stereo AND D3D12 AND not splitscreen/sceneview
  compat AND the stereo hook has view-count control (GetDesiredNumberOfViews hook or the
  view-extension BeginRenderViewFamily count fallback) AND the pipeline is **proven**:
  kernels Ready + at least one frame actually synthesized (`m_dibr_synthesis_proven`,
  armed by `run_dibr_synthesis` on first success). Until proven, behavior is the old
  additive overwrite mode — both eyes rendered, synthesized eye overwritten — so a
  cold/failed pipeline can never leave an eye unrendered.
- **Hook wiring** (FFakeStereoRenderingHook.cpp): `get_desired_number_of_views_hook`
  returns 1; `begin_render_viewfamily` count=1 fallback extended; `true_index` pinned to
  `VR::get_dibr_reference_eye()` (0 = left for synth-right/inverse/raymarch, 1 = right
  for synth-left) in `calculate_stereo_view_offset`, `calculate_stereo_projection_matrix`
  and the FSceneView-constructor path. The lone view counts as BOTH first and last pass
  of the frame: the first-pass bookkeeping (pre-rotation store, `update_hmd_state`) and
  last-pass work (roomscale movement, aim control-rotation update) both fire on it.
- **The single view always lands in the LEFT half** of the still-double-wide RT
  (AdjustViewRect index 0), regardless of which eye it represents. `run_dibr_synthesis`
  stages accordingly: left half in single-view mode; in two-view mode the YORO reference
  eye's own half (THIS ALSO FIXED A LATENT BUG — synth-left used to warp the left eye's
  image; it now stages the right half with the depth-UV auto transform anchored right,
  TransformDepthUv anchor=2).
- **Never submit a stale eye**: every bail-out in `run_dibr_synthesis` (mode off,
  pipeline failed/compiling, no depth, bad format, synthesize()==null) calls
  `fill_right_half_mono()` — mirrors the rendered half over the right half via the
  staging texture (same-subresource CopyTextureRegion is illegal, hence the bounce).
  A 3-frame cooldown keeps the fill armed after the policy flips off, covering
  backbuffers still in flight that were rendered with one view.
- **Depth layer**: the runtime depth-layer submit (`SwapchainIndex::DEPTH`) is suppressed
  while single-view is active — the unrendered half of SceneDepthZ is stale. DIBR's own
  depth sampling is unaffected (it anchors to the rendered half).
- **Degradation**: titles where neither GetDesiredNumberOfViews nor view extensions
  hooked simply stay in overwrite mode (policy returns false) — correct image, no perf win.
- **Known caveat**: the synthesized eye is the reference eye's asymmetric-frustum image
  warped by screen-space disparity and submitted with the other eye's FOV — standard
  DIBR practice (vrmod does the same); `edge_guard`/`edge_fill` cover the frustum edges.
- Also fixed while here: Inverse/Raymarch eye-offset zeroing
  (`is_dibr_mono_view_active`) no longer activates when DIBR cannot run (it used to
  flatten the image to mono in Alternating/AFR, extreme-compat and 2D-screen modes
  where `run_dibr_synthesis` refuses to synthesize).

### Phase 5 — Optional follow-ons (separate efforts, do not block)
14. Per-eye split output path (`CopyTextureRegion` into per-eye OpenXR swapchains directly,
    skipping the double-wide hop).
15. ~~Skip engine right-eye rendering in YORO mode for the ~2× scene perf win (view-family
    surgery — bigger lift, big payoff).~~ **DONE — see Phase 4.5 above.**
16. **Deghoster port** (NVOF optical-flow temporal warp) for AFR ghosting — independent module
    (`deghoster_d3d12/` → `src/mods/vr/d3d12/Deghoster*`).
17. D3D11Component support — cs_5_0 compute works fine on D3D11; it's plumbing, not research.
18. Lua API exposure (`lua-api/`) so per-game scripts can drive DIBR params.

---

## 6b. Phase 6 — Depth3D / SuperDepth3D-derived enhancements

`E:\Github\Depth3D` (BlueSkyDefender's SuperDepth3D — the methodology the inverse-warp
shader originally derived from; clone is clean, no local changes) offers techniques the
vrmod port does not have. Ranked by value, all cbuffer-friendly:

1. **View Modes VM0–VM5** (SuperDepth3D.fx ~792–810): six disocclusion-infill strategies
   (stretch / depth-aware alpha separation / reiteration / stamped transparency / mixed
   foliage / adaptive) — selectable per game. Maps onto our `edge_fill_mode` slot as an
   extended enum.
2. **Post-search binary refinement** (~6394–6437): after the coarse parallax search, a
   3-iteration binary interpolation (0.5/0.25/0.125) plus a sharp-gap linear-seek path for
   large discontinuities. Direct upgrade for the raymarch kernel's termination accuracy.
3. **Distance-field adaptive step skipping** (~6316–6346): scales raymarch steps 1.0–2.5×
   by distance-to-surface, refining only near depth edges (~30–40% raymarch speedup).
4. **Smart convergence / ZPD overshoot** (~605–613, `Conv()` ~4714): scene-adaptive
   convergence from temporally averaged scene depth — auto-ZPD instead of manual. Needs a
   small persistent UAV for the running average (frame-to-frame state).
5. **ZPD boundary modes BD0–BD7** (~671–792): region-aware convergence constraint system
   (full / narrow / wide / FPS-center variants) with 3 parallel level scalers — richer
   than our single convergence-boundary guard.
6. **Per-game profile database** (Overwatch.fxh, 818 game profiles): each profile sets
   ~140 parameters via one game hash. Long-term: ship a profile table mapping onto
   `DIBRStereoParams` defaults per process name — synergizes with UEVR's per-game config.
7. Lower priority: weapon Z-fighting correction (~6368), 3×3 depth dilation helpers,
   performance-level tiers, AXAA, temporal frame averaging (TAA-style ZPD smoothing).

Items 1–3 are shader-local edits to the ported kernels; 4–5 add a handful of cbuffer
params (+ one persistent texture for 4); 6 is pure C++ data.

## 7. Risks & mitigations

| Risk | Mitigation |
|---|---|
| Depth/color frame mismatch (depth from a different point in the frame than color → edge shimmer) | Acquire both at the same site (already true at :3178-3217); validate with debug depth view |
| `SceneDepthZ` absent or wrong-sized in some titles | Auto-derive depth UV transform from descs; port `depth_select.rs` ranking as a fallback finder; auto-bypass if nothing usable |
| CB layout drift between C++ struct and HLSL (258 fields!) | Single `dibr_common.hlsli`; `static_assert` on struct size; debug-mode field-order checksum |
| MSVC 64KB string-literal limit for embedded shaders | Chunked literal concatenation in the generated header |
| Backbuffer lacks UAV flag | Never write game resources; own UAV output texture + copy (decision #4) |
| YORO synth-right reads left half while engine still renders right half (wasted work initially) | Acceptable for Phase 2 (correctness first); Phase 5 item 15 reclaims the perf |
| Anaglyph/interlace/frame-alternate modes are desktop-display features | Port them (they're in the shared composition code anyway) but only surface in UI under "Desktop Preview"; VR path always uses SBS layout 0 |

---

## 8. Suggested first PR slice

Smallest end-to-end vertical: Phase 1 + Phase 2 step 5/7 with **InverseWarp mode only**, master
toggle + divergence/convergence/edge_fill sliders, debug depth view. Everything else
(YORO/raymarch modes, full param UI, mono-path hookup) lands as follow-up commits on a working
skeleton.
