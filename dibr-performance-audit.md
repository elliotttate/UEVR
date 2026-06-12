# DIBR Performance / Efficiency Audit

**Scope:** read-only review of the entire DIBR synthetic-stereo pipeline — the
compute kernels (`src/mods/vr/d3d12/shaders/dibr_*.hlsl`), the orchestration
(`DIBRSynthesis.cpp/.hpp`), and the caller / integration site
(`D3D12Component.cpp::run_dibr_synthesis`).

**No code was changed.** This is an analysis of opportunities, with rationale,
risk, and `file:line` references so any item can be picked up independently.

**Date:** 2026-06-12 · **Branch:** `ue57performance`

---

## Why this matters (the critical-section framing)

`run_dibr_synthesis` records onto its **own** command list (`m_dibr_commands`),
**blocks on a fence** before recording (`D3D12Component.cpp:5307`), and must
finish before the per-eye OpenXR copies consume the backbuffer. The code already
carries a `>=5ms` stall guard (`D3D12Component.cpp:3251`) because overrunning
here wedges the OpenXR submit and can black-screen / freeze MetaXR.

So this whole pass sits on the **present critical path**. Every GPU pass, copy,
and barrier removed shortens a stall that directly threatens headset stability.
That's the lens for the priority ranking below.

The hottest configuration in practice is **AFW (mode 6 = `YoroScatter` +
per-frame eye flip)**, which is pinned to full resolution
(`DIBRSynthesis.cpp:782`) and runs the full scatter chain **plus** an extra
stash pass every frame. Items that touch the scatter chain and the fill pass pay
off most there.

---

## Priority ranking

| # | Opportunity | Where | Type | Effort | Risk |
|---|-------------|-------|------|--------|------|
| 1 | Fuse `scatter_depth` + `scatter_color` into one packed-atomic pass | scatter kernels + cpp | GPU pass count | Med | Med |
| 2 | Compose directly into the destination RT, skip the double-wide copy-back | cpp caller | GPU copy + alloc | Med | Med (conditional) |
| 3 | Stop recreating persistent-resource descriptors every frame | `DIBRSynthesis.cpp` | CPU/driver | Low | Low |
| 4 | Disocclusion fill: key-based coverage → drop color clear + per-tap color reads | `dibr_scatter_fill.hlsl` + cpp | GPU bandwidth | Low | Low–Med |
| 5 | Shrink intermediate formats (RGBA16F → RGBA8 / RG16F) | cpp resource descs | GPU bandwidth | Low | Low (HDR caveat) |
| 6 | YORO: share/hoist the gradient used by the two depth-gradient guards | `dibr_yoro.hlsl` + prep | GPU taps | Low | Low |
| 7 | Batch resource barriers | cpp (both files) | CPU/driver | Low | Low |
| 8 | Bake `TransformDepthUv` / `depth_sample_mode` CPU-side | shaders + cpp | GPU ALU | Low | Low |
| 9 | Cache `output_format` derivation per-frame | `D3D12Component.cpp:5310` | CPU | Trivial | Low |
| 10 | Groupshared the fill scanline scan (forward-looking) | `dibr_scatter_fill.hlsl` | GPU bandwidth | High | Med |

---

## A. Scatter pipeline (the AFW/scatter hot path)

### A1. Fuse `scatter_depth` + `scatter_color` via a packed atomic — **headline GPU win**

`dibr_scatter_depth.hlsl` and `dibr_scatter_color.hlsl` are **byte-for-byte
identical through line 332** — both sample device depth, run `ReprojectSourceUv`
(a full 4×4 `mul` + perspective divide), compute `tx`/`ty`, and do the 2-wide
splat. The depth pass does `InterlockedMax(key)`; the color pass then
**re-derives the same depth + reprojection** only to test
`g_scatterKey == key` before writing color
(`dibr_scatter_color.hlsl:330-344`).

Cost of the duplication, per frame: a redundant depth sample + full reprojection
for *every source pixel*, an entire extra dispatch (`DIBRSynthesis.cpp:969-971`),
a second full-screen clear, and a UAV barrier.

**Fix — pack the winning color into the atomic so the resolve is free:**
- **Primary (NVIDIA target):** 64-bit typed atomic —
  `InterlockedMax((depthKey << 32) | packedRGBA8)`. Reversed-Z key in the high
  32 bits keeps nearest-wins exact; the color rides along. Requires SM6.6 +
  `AtomicInt64OnTypedResourceSupported` — gate on the cap.
- **Fallback:** keep the existing `R32_UINT` and pack `depth(high16) |
  RGB565(low16)`. Ordering degrades to 16-bit (fine for nearest-wins), color to
  565.

Either path deletes: the color dispatch, the color-buffer clear, one UAV
barrier, and ~half the scatter ALU/bandwidth — straight off the critical
section.

### A2. Fill pass: make the **key** buffer the single source of coverage truth

`dibr_scatter_fill.hlsl` is the most divergent kernel — each hole pixel scans up
to `kMaxSearch = 96` taps left *and* right (`:381-394`). Each tap's coverage
test reads **both** buffers:
`k != 0u && (k & 0x80000000u) == 0u && g_scatterColor[...].a > 0.5f`.

A non-marker, non-zero key already **implies** the color pass wrote that pixel
with `a == 1` (the color pass only writes where the key matches the winner —
`dibr_scatter_color.hlsl:343`). So the `g_scatterColor[...].a > 0.5f` term is
redundant with the key check. Dropping it removes a full color-buffer load from
every tap of the 96-wide search. Same redundancy in `SampleBackgroundRun`
(`:349`) and the top-of-kernel self-skip (`:363`).

If coverage is keyed purely off the key buffer, you can also **drop the
color-buffer clear** entirely (`DIBRSynthesis.cpp:961`): the color buffer is
fully regenerated each frame (covered pixels by scatter, holes by fill), and the
key buffer is already cleared for the atomics. **Verify** the ping-pong
stale-data interaction first (buffers reuse 2-frames-ago contents), but the
logic holds: every synth pixel ends up either covered (key set) or filled.

This composes with A1 — under the packed-atomic merge the separate color buffer
goes away and coverage is *only* the key.

### A3. Note: scatter passes process the overscan border that gets cropped

Scatter depth/color iterate **source** pixels (`gx_src × gy_src`,
`DIBRSynthesis.cpp:966,970`) while fill/compose run in synth/out space. With
overscan active, source > out, so the scatter passes do work on the
overscan border that the compose later crops out. This is inherent to forward
scatter (you must scatter every source sample), so it's a *note*, not an action
item — but it's real extra work proportional to `overscan_x²`.

---

## B. Gather kernels & depth prepass

### B1. YORO: the two depth-gradient guards recompute the same gradient twice

`ConvergenceBoundaryScale` (`dibr_yoro.hlsl:1926`) and `DepthArtifactGuardScale`
(`:1945`) each take **4 `SamplePreparedDepth` taps** of the same neighbors and
compute the **same** gradient. In the YORO compose path both are multiplied per
output pixel (`:2418`), so with both guards enabled that's **8 prep taps to
compute one gradient twice**.

The raymarch kernel already avoids this: its prep (PREP_MODE 1) writes the
gradient into `g_prepDepth.y` and reads it back as a single tap
(`ConvergenceBoundaryScaleFromGradient`, `dibr_raymarch.hlsl:1706`). YORO's prep
(PREP_MODE 2, `YoroSearchDepthChain`) leaves `gradient = 0`
(`dibr_depth_prep.hlsl:894-896`), so YORO never got the treatment.

**Fix:** either (a) compute the 4-tap gradient once in YORO and feed both
scales, or better (b) have PREP_MODE 2 write the gradient into `.y` like
PREP_MODE 1, and read it as a single tap — mirroring the raymarch path exactly.
Both default to off (`convergence_boundary_strength` / `depth_artifact_guard_strength`
= 0 → early-out), so this only bites when those sliders are enabled, but it's a
clean parallel to an optimization already shipped for raymarch.

### B2. Prep chain depth is correctly hoisted (verified clean)

The per-pixel conditioning chain runs **once** per source pixel in
`dibr_depth_prep.hlsl` and is read back as single taps inside the search loops
(`dibr_raymarch.hlsl:1675`, `dibr_yoro.hlsl:1972`). The expensive multi-tap
stages (`AutoBalanceDepth` 9-tap, `ReconstructDepth` 8-tap) all early-out at
their defaults, so the prep is ~5-tap in the common case. This refactor is solid;
no action needed beyond B1.

### B3. Mask-stack guards are hoisted to the output pixel (verified clean)

`YoroSynthGuard` / `DepthShiftGuard` fold the slowly-varying mask stack into a
single per-output-pixel value reused across every probe (comments at
`dibr_yoro.hlsl:1977-1984`, `dibr_raymarch.hlsl:1732-1736`). Good.

---

## C. Intermediate buffer formats / memory bandwidth

### C1. Prep texture is 2× wider than needed

`m_prep` is `R16G16B16A16_FLOAT` (`DIBRSynthesis.cpp:599`) but only `.xy` carry
data (depth, gradient). The comment notes RGBA16F was chosen because RG16F
typed-UAV-store is an *optional* cap — but it's supported on the NVIDIA target.
The march taps this texture dozens of times per output pixel; halving its
footprint (→ `R16G16_FLOAT`) helps the cache it's pounding. Gate on the cap with
RGBA16F as the fallback.

### C2. Scatter color + history color are 2× wider than the output precision

`m_scatter_color` and `m_afw_history_color` are `R16G16B16A16_FLOAT` (8 B/px,
`DIBRSynthesis.cpp:518,567`) storing LDR `[0,1]` color + a 0/1 coverage flag. The
default DIBR output is `R8G8B8A8_UNORM` (`DIBRSynthesis.hpp:509`), so RGBA8
intermediates lose nothing relative to the final image and halve the bandwidth
the 96-tap fill search hammers.

**Caveat:** if the engine source color is genuinely HDR and you want to preserve
it pre-tonemap, RGBA8 clips — gate on the actual source/output format
(`uav_store_format_for` already classifies it).

---

## D. CPU-side / D3D12 orchestration

### D1. Persistent-resource descriptors are recreated every frame

`synthesize()` issues ~12 `CreateShaderResourceView` / `CreateUnorderedAccessView`
calls **per frame** (`DIBRSynthesis.cpp:821-884`, plus the two clear-heap views
at `:954-955`). Of those, only the **color and depth SRVs** view external
resources that can change frame to frame. The other ~8 (output UAV, prep
SRV+UAV, scatter key/color UAVs, history color/key UAVs, history SRV) point at
*internal persistent textures* that only change on resize.

Recreating identical views into the rotating ring every frame is pure CPU
render-thread cost at 90–120 Hz. Move those creations into the
`ensure_output` / `ensure_scatter` / `ensure_prep` / `ensure_afw_history`
paths (create once per resource lifetime); keep only color/depth SRVs rotating
through the ring for in-flight safety. Net: ~12 driver calls/frame → ~2.

### D2. Barriers are issued one at a time

The scatter path issues many single-barrier `ResourceBarrier(1, …)` calls —
clear→barrier ×2 (`DIBRSynthesis.cpp:962-963`), each dispatch→barrier, the AFW
stash's 2+2 (`:1012-1017`), and the three top-level transitions (`:892-898`).
D3D12 takes an array; batching the independent ones into single calls cuts
driver overhead and lets the GPU collapse the stalls. The caller side has the
same pattern around the copies (`D3D12Component.cpp:4767-4787`, `:5624-5643`).
Small but free.

### D3. Constant-buffer note (low value)

The 1.2 KB `StereoParams` cbuffer lives in an UPLOAD-heap ring
(`DIBRSynthesis.cpp:389-412`) and is memcpy'd each frame. It's read by every
thread of up to ~7 dispatches from system memory; the GPU constant cache
mitigates this. A DEFAULT-heap copy would be marginally faster to read but adds
an upload copy — **not worth it**. Listed for completeness.

---

## E. Whole-process / caller (`run_dibr_synthesis`)

### E1. Two full-resolution copies wrap the compute every frame

Per frame in steady state:
- **Stage-in** (`D3D12Component.cpp:4766-4788`): `CopyTextureRegion` of one
  eye-half (eye_w × eye_h) backbuffer → `m_dibr_source`, + 4 barriers.
- Compute (`synthesize`).
- **Copy-back** (`:5624-5643`): `CopyTextureRegion` of the **full double-wide**
  (2·eye_w × eye_h) `m_output` → backbuffer, + 4 barriers.

≈3 eye-frames of copy bandwidth + 8 barriers on the critical path, on top of
synthesis.

**The copy-back (the larger one) is removable when the destination is
UAV-capable.** Point the compose's `u0` directly at the destination and drop
both the copy-back *and* the `m_output` texture. The format side is already
solved — `uav_store_format_for(device, bb_desc.Format)` is computed and gates
DIBR (`:5291`).

**Caveat (why it's conditional):** `backbuffer` here is sometimes the real
swapchain buffer (`:2512`, no UAV flag) and sometimes an engine/UEVR texture.
The SHf / mono / `m_game_tex` textures *are* created with
`ALLOW_UNORDERED_ACCESS` (`:405,:667,:963`). So: when
`backbuffer->GetDesc().Flags & ALLOW_UNORDERED_ACCESS`, compose in place and
skip the double-wide copy; otherwise keep the copy-back.

#### E1 verification (2026-06-12)

Static trace of the SN2 path through `on_frame`
(`D3D12Component.cpp:2498-2542`):

- SN2 is UE5 and is neither SHf nor Stalker2, so `use_stable_external_backbuffer_copy`
  is false and `scene_source_state = D3D12_RESOURCE_STATE_RENDER_TARGET`.
- `ue4_texture = get_render_target_manager()->get_render_target()` is non-null for
  SN2, so **`backbuffer` = the engine scene render target's native resource**
  (`:2500-2503`).
- The `m_game_tex` re-assignment paths (`:2604`, `:2705`, `:2790`, `:2810`) are all
  gated on `backbuffer == real_backbuffer`, which is **false** for SN2 (engine RT ≠
  swapchain). So `backbuffer` stays the engine scene RT throughout — **not** a
  UEVR-created copy whose flags we control.

Therefore the UAV-capability of SN2's target is set by **UE5**, not UEVR, and
cannot be proven from UEVR source alone. A live read was attempted via
`uevr_render_d3d12` but returned `available:false` / `device 0x0` (no SN2 instance
running at audit time).

**Conclusion — make E1 self-gating; the static answer is not a blocker.** The
compose already reads its source from `m_dibr_source` (the staged copy), never
from the destination, so in-place compose has **no read/write aliasing** and is
safe on any destination. Implement E1 as a runtime branch:

```
bool can_compose_in_place = (backbuffer->GetDesc().Flags
                             & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0;
// fast path: create a UAV over `backbuffer` (cast to output_format), point the
//   compose's u0 at it, transition scene_source_state <-> UNORDERED_ACCESS around
//   the compose, and skip both `m_output` and the double-wide copy-back.
// fallback: today's path (compose -> m_output -> CopyTextureRegion).
```

This lights up automatically wherever the flag is present (and it likely *is* for
UE5 SceneColor, which the tonemap/TSR compute passes commonly write via UAV — but
that's the runtime branch's job to confirm, not ours to assume). To measure how
often the fast path fires for SN2 specifically, add a one-time
`SPDLOG_INFO_ONCE` of `backbuffer->GetDesc().Flags` in `run_dibr_synthesis`, or
re-run `uevr_render_d3d12` / `uevr_render_resources` while SN2 is live.

The descriptor for the backbuffer UAV is cheap and cacheable per
backbuffer-resource pointer (same idea as **D1**).

### E2. The stage-in copy is the *unavoidable* one — don't chase it

You can't also read the source straight from the backbuffer while writing the
packed output into it: the warp reads disparity-shifted **neighbor** source
pixels, so read/write aliasing on the same resource races. The staging copy
exists precisely to give the kernel a read-only source distinct from the write
target. (You *could* remove stage-in instead by adding a half-remap uniform to
every `g_colorTex` tap, but many taps bypass `SourceRemapUv` —
`ApplyImageFilter`, `LetterboxAutoMask`, `SampleOutputSource` — so it's invasive
for the smaller of the two copies. Lower priority than E1.)

### E3. The fence wait serializes synthesis into present (architectural)

`m_dibr_commands.wait(INFINITE)` (`:5307`) blocks the present thread on the
previous DIBR submission before recording, and the in-place backbuffer rewrite
prevents overlap with the next engine frame. This is structural (it's why the
5 ms guard exists). You won't remove the serialization without double-buffering
the target — but it reframes priority: **all the GPU-side wins land directly on
this stall.** Rough order of stall-time removed for the AFW/scatter path:
1. A1 (atomic merge) — deletes a dispatch + clear + barrier.
2. E1 (compose-in-place) — deletes the double-wide copy + 4 barriers.
3. A2/C1/C2 (fill format + redundant reads) — shrinks the longest dispatch.

### E4. `output_format` re-derived every frame

`:5310` calls `set_output_format` after re-deriving via `uav_store_format_for`
(which may probe device caps). Both early-out when unchanged, so it's cheap — but
the format only changes on a swapchain event. Cache the derived value keyed by
`bb_desc.Format`; recompute only on change. Trivial.

### E5. Verified clean (caller side)

- All staging/output/scatter/prep/history textures cache by size+format and only
  reallocate on change (`ensure_*`). No per-frame allocations in steady state.
- The AFW parity self-calibration readback (`:5661-5787`) is
  **one-time at engagement**; the `UEVR_DIBR_AFW_DUMP` capture (`:5793+`) is
  env-gated. Neither costs anything in steady state. Forensics `SPDLOG` traces
  are bounded counters that stop after startup.
- The AFW reprojection-matrix math (`:5370-5585`) is per-*frame*, not
  per-pixel — a handful of 4×4 multiplies; negligible.
- Divergence is resolution-scaled CPU-side (`:5331`) so angular disparity stays
  constant — no per-pixel cost.

---

## F. Minor / micro-opts

- **F1.** `SampleDepthTexture` branches on `floor(depth_sample_mode + 0.5)` to
  pick point vs linear sampler **per tap** (`dibr_*.hlsl`, e.g.
  `dibr_yoro.hlsl:377-383`). `TransformDepthUv` likewise recomputes an affine +
  `floor(anchor+0.5)` branch per tap. Both are dispatch-uniform; baking them
  into CPU-resolved constants (like `pre_effective_convergence` etc. already are,
  `DIBRSynthesis.cpp:736-748`) removes per-tap ALU — meaningful in the matrix
  search (32 steps + 4-step bisect, each with a tap, `dibr_yoro.hlsl:2100-2131`).
- **F2.** Many `floor(mode + 0.5)` uniform mode-decodes execute per pixel across
  the output stages. The compiler hoists uniform control flow, but the decodes
  themselves could be CPU-resolved. Very low value.
- **F3.** `ApplyDepthDither` uses `pow(2.0f, bits)` (`dibr_yoro.hlsl:1793`) where
  `dibr_depth_prep.hlsl:386` uses `exp2(bits)`. `exp2` is cheaper. Gated (dither
  default 0), so cosmetic.
- **F4.** `frac(sin(dot(...)))` hashes in `DepthDitherNoise` / `ImageFilterNoise`
  are expensive transcendental hashes, but both are behind strength gates that
  are off by default. Fine as-is.

---

## G. Threadgroup / occupancy notes

All kernels use `[numthreads(16,16,1)]` (256 threads) — a reasonable image-compute
default; no change recommended. The fill kernel (`dibr_scatter_fill.hlsl`) is the
one place where a **groupshared** redesign could pay off (item 10): adjacent
threads in a 16-wide group re-scan nearly the same horizontal span through global
UAV memory. Cooperatively loading each row's key/coverage into LDS once and
searching the LDS copy would cut global traffic for wide holes dramatically.
It's a real rewrite that only pays off where holes are wide, so it's a "later"
item — but it's the structural ceiling on fill cost, which the AFW full-res pin
(`DIBRSynthesis.cpp:782`) makes unavoidable.

---

## H. Things checked and found already-optimal

- Depth conditioning hoisted into a one-shot prepass (B2).
- Mask-stack and gradient guards hoisted to the output pixel for the **raymarch**
  path (B3); YORO is the gap (B1).
- The matrix search hoists scanline-invariant matrix terms into `coefX`/`coefW`
  (`dibr_yoro.hlsl:2063-2077`).
- `params_allow_lean` + the `DIBR_LEAN` PSO variants compile out optional output
  stages on the common path (`DIBRSynthesis.cpp:615-655`).
- The inverse kernel is a direct-shift warp with no search loop — cheapest gather
  mode already.
- Per-frame resource caching, one-time calibration, bounded forensics (E5).
- **No bugs found.** Two `\`-looking comment lines reported by `grep`
  (`dibr_yoro.hlsl:1967` and `:1994`) are grep-rendering artifacts — the file has
  `//` (verified by direct read).

---

## Suggested order of attack

1. **A1 (atomic merge)** + **A2 (key-based coverage)** together — biggest GPU
   saving, both in the scatter path, naturally combined.
2. **E1 (compose-in-place)** — confirm the SN2 destination has the UAV flag
   first; if so, removes the largest per-frame copy.
3. **D1 (descriptor caching)** — pure CPU win, lowest risk.
4. **C1/C2 (formats)** + **B1 (YORO gradient)** — bandwidth + tap reductions.
5. Measure with `uevr_render_gpu_timings` between steps to confirm where the
   critical-section time actually concentrates before the higher-effort items
   (E1, item 10).

> Recommendation: pull GPU timings **first** to confirm the scatter chain vs. the
> copies vs. the fill is where the AFW frame budget actually goes, then commit to
> A1/E1 in that order.
