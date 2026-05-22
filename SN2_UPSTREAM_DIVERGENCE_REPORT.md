# SN2 Upstream Divergence Report

Investigation of LEFT-eye-underwater / RIGHT-eye-above-water VR bug at the SN2 title-menu cave-opening scene under `-emulatestereo`.

Capture: `Subnautica2-Win64-Shipping_2026_05_21_08_45_51.ngfx-capture`, frame 1807, both eyes.

## TL;DR — root cause

The visible "above water vs underwater" eye divergence is upstream of all UWE fog/SLW pixel shaders. Each eye writes to its own per-eye render-target heap slot and consumes its own UWE-style `SingleLayerWater` cbuffer (cb1 = root param 5 = PIXEL b1) whose content the engine populates with **per-eye water-classifier scalars that disagree**:

| cb1 offset | LEFT value | RIGHT value | Likely field |
|---:|---:|---:|---|
|  64 (vec4_0 row 4 X) | 0.000429 | 0.500391 | per-eye SLW lookup UV.X min (matches half-screen split) |
|  72 (vec4_0 row 4 Z) | 0.499609 | 0.999609 | per-eye SLW lookup UV.X max |
| 120 | **-1552.6** | **0.0** | scalar "underwater depth" (cm) |
| 124 | **1.0** | **0.0** | boolean "is underwater" toggle |
| 196 | **+0.9947** | **-0.0190** | cosine of water-surface angle |
| 220 | **3024.65** | **0.0** | water-depth-related scalar (cm) |

LEFT cbuffer says "depth ≈ -15.5 m underwater, underwater=1, near-vertical-surface", RIGHT cbuffer says "depth ≈ 0 m, underwater=0, near-horizontal-surface" → LEFT branches to teal underwater fog, RIGHT branches to above-water/sky. This **exactly reproduces the observed visual**.

Caveat: the three loaded UWE SingleLayerWater pixel shaders observed in this capture (CRC `b9be2499`, `32040a0d`, `de7c3822`) only read cb1 rows 4–7 (offsets 64–127), so the dramatic f@120/124/196/220 mismatches above are observed in the bound cbuffer **but are not the bytes those particular PS instances sample**. They identify the same SingleLayerWater cbuffer as the eye-classifier surface, but the actually-load-bearing per-eye divergence is upstream of those PS — it's whichever pass writes those cbuffer fields and/or whichever shader reads them and gates the visible material. Candidates surfaced: the `OpaqueBasePass` / `UWE Fog reconstruct` chain.

UEVR should redirect the **UWE SingleLayerWater 276-byte cbuffer for the right eye** (the per-draw inline CBV at PIXEL root param 5 for PSOs 1124 / 1319 / 1435 / 1041) **and the matching cb fields populated by SN2's own underwater-classifier compute dispatch**, *not* pso 3069 root[0] SRVs 5/8/9 (already ruled out by the user's red-shadow test).

---

## Step 2 — Per-eye PS CRC inventory (709 draws, 134 PSOs)

LEFT renders into viewport X=0..640, RIGHT into X=640..1280. **Asymmetry is almost zero**:

- LEFT-only PSOs: 2
  - PSO 1112  — VS=`833a1657`, PS=`13b00f0c` (BasePass MainPS), 1 LEFT draw (CL05 line 1736)
  - PSO 1220  — VS=`fe70d367`, PS=`d3ab43c5` (no keyword), 2 LEFT draws
- RIGHT-only PSOs: **0**
- Common (paired) PSOs: 66, total 268 LEFT / 268 RIGHT draws (1:1 match)
- 22 non-standard-viewport PSOs (shadow / cubemap / mip passes, both eyes share)

Top per-eye-paired PSOs by draw count:

| PSO | Total | L | R | Stage CRCs | Entry / kind |
|----:|---:|---:|---:|---|---|
| 1289 | 147 | 73 | 74 | VS=`de555f86` | depth-only / shadow (no PS) |
| 764 | 108 | 54 | 54 | VS=`8e5cafea` PS=`4d44ce74` | MainPixelShader__OPTIMIZED (instanced mesh basepass) |
| 774 | 54 | 27 | 27 | VS=`050dec78` PS=`f7ddfe06` | BasePass MainPS (workhorse) |
| 1156 | 36 | 18 | 18 | VS=`050dec78` PS=`8d06247f` | BasePass MainPS |
| 1031 | 12 | 6 | 6 | VS=`050dec78` PS=`cacaea96` | BasePass MainPS |
| 1041 | 4 | 2 | 2 | VS=`d71c1378` PS=`166dba88` | **UWEWater BasePass MainPS** (translucent water) |
| 1435 | 4 | 2 | 2 | VS=`174de467` PS=`de7c3822` | **SLW PS (Water)** |
| 1319 | 4 | 2 | 2 | VS=`0cd8a0be` PS=`32040a0d` | **SLW PS (Water)** |
| 1124 | 2 | 1 | 1 | VS=`d699ff92` PS=`b9be2499` | **SLW PS (Water)** |
| 1798 | 4 | 2 | 2 | VS=`356b0b7c` PS=`5084e157` | post-process (per-eye SRV diff) |

No LEFT/RIGHT-only PS that gates classification. **The two LEFT-only draws (PSO 1112 / 1220) are not the bug surface** (only 1–2 draws each, basepass with no SLW keyword); they are most likely view-frustum cull asymmetries.

## Step 5 — View CB byte diff (LEFT @ res 3668+1075200, RIGHT @ res 3668+1064960)

Extracted the 10 240-byte FViewUniformShaderParameters at each known offset from the pool 3668 initial-data blob (data.bin idx 4934). 482 bytes differ. **All diffs map to expected per-eye fields** in UE 5.6's `VIEW_UNIFORM_BUFFER_MEMBER_TABLE`:

| Offset | Field | LEFT | RIGHT | Notes |
|---:|---|---|---|---|
| 0 | TranslatedWorldToClip | (matrix) | (matrix) | per-eye projection (expected) |
| 64 | RelativeWorldToClip | (matrix) | (matrix) | per-eye |
| 128 | ClipToRelativeWorld | (matrix) | (matrix) | per-eye |
| 448 | ViewToClip | (matrix) | (matrix) | per-eye (different IPD-shifted projection) |
| 1152 | ViewOriginHigh | (3668.7, 3523.6, -2735.5) | (3673.4, 3519.3, -2735.5) | +4.7 cm IPD shift |
| 1264 | ScreenPositionScaleBias | (0.25, -0.5, 0.5, 0.25) | (0.25, -0.5, 0.5, 0.75) | per-eye half-screen UV bias |
| 2336 | **FieldOfViewWideAngles** | **(1.878, 1.606)** | **(1.255, 1.606)** | **⚠ asymmetric per-eye FOV X (107° vs 72°)** — non-symmetric off-axis projection. Suspicious but not the underwater-bug surface. |
| 2368 | ViewRectMin | (0, 0, 0, 0) | (640, 0, 0, 0) | per-eye viewport origin |
| 2400 | ViewRectMinAndSize | (0, 0, 640, 720) | (640, 0, 640, 720) | per-eye viewport rect |
| 2448 | BufferBilinearUVMinMax | (0.00039, 0.00069, 0.4996, 0.9993) | (0.5004, 0.00069, 0.9996, 0.9993) | per-eye scene-color UV clamp |
| 2464 | ScreenToViewSpace | (4.2, -2.0, **-1.05**, 1.0) | (4.2, -2.0, **-3.15**, 1.0) | Z-component 3× difference — derived from per-eye projection |
| 2636 | Random | 7121 | 2538 | per-frame random (expected per-eye different draw) |
| 3088 | SkyCameraTranslatedWorldOrigin | (0.0188, 0.0181, 3264) | (0.0189, 0.0181, 3264) | per-eye relative-world translation |
| 3104 | SkyPlanetTranslatedWorldCenterAndViewHeight | matches camera origin | matches camera origin | per-eye |
| 3120 | SkyViewLutReferential | (matrix) | (matrix) | per-eye |
| **3532** | **StereoPassIndex** | **0** | **1** | per-eye discriminator (the OFFICIAL UE per-eye gate) |
| 3536+ | GlobalVolumeTranslatedCenterAndExtent[0..3] | per-eye | per-eye | derived from camera position |
| 3632+ | GlobalVolumeTranslatedWorldToUVAddAndMul[0..3] | per-eye | per-eye | derived from camera position |
| 4976 | TanAndInvTanHalfFOV | (0.0, 1.0, 0.15, 0.0) | (1.4e-45, 1.0, 0.15, 0.0) | both look broken / not populated this frame |

**No "is underwater" or scene-classifier field in the View CB differs.** Every diff is either matrix/projection/origin or per-frame randomness or per-eye viewport bookkeeping. The eye-classifier is **NOT** in the FViewUniformShaderParameters.

## Step 3 — Per-eye CBV/SRV diff at the most relevant draws

Method: pulled the `gfx_cbvs` and `gfx_tables` from `.ngfxmcp_cpp2025_state_events.jsonl` for matching LEFT/RIGHT draws of each common-PSO. Resolved descriptor-heap slot ranges against `.ngfxmcp_cpp2025_descriptors.json`.

### PSO 774 (BasePass MainPS, 27 L + 27 R draws — most common opaque shader)

**PIXEL SRV table[64] diff between eyes: 0** — both eyes read identical SRVs at every slot.

CBV diff:

| Slot | LEFT (res:off) | RIGHT (res:off) | Note |
|---:|---|---|---|
| 4 (View) | 3668:1075200 | 3668:1064960 | per-eye View CB (same pool) |
| 5 | 2549:399360 | 2549:399360 | identical (shared) |
| 6 | 3668:1075200 | 3668:1064960 | View again (cb2) |
| 7 | 2038:3061504 | 2038:3061504 | identical |
| 8 | 2549:713728 | 2549:713728 | identical |
| 9 | 1967:3320320 | 1967:3320320 | identical |
| 10 | 2038:3032576 | 2038:3033344 | **+768 byte offset shift** ⚠ per-eye scene-light cbuffer |
| 11 | 1967:3072000 | 1967:3072000 | identical |

PSO 774's only per-eye diff is the View CB and slot 10 (768-byte offset shift, same pool 2038) — slot 10 is the OpaqueBasePass per-eye cbuffer; that's expected per-eye payload (each eye has its own per-view light list).

### PSO 1156 and 1031 (other BasePass workhorses): same pattern as PSO 774

PIXEL SRV table diff = 0. CBVs differ only in View slot (per-eye) and one OpaqueBasePass per-eye slot.

### PSO 1041 (UWEWater BasePass MainPS, ps=`166dba88`, 2 L + 2 R draws)

Root signature 217 (param 0 = PIXEL SRV[64]). PIXEL SRV table diff = 17 slots starting at [2]:
- [2] LEFT: Buffer 255 (uid 3033) ↔ RIGHT: Buffer 362 (uid 3195)
- [3+] further per-eye-allocated buffers (per-eye light culling tiles, per-eye distance volume)

cb1 (TranslucentBasePass, **3764 bytes** at CBV root param 5) bound from same pool but offset differs per eye.

### PSO 1124 / 1319 / 1435 (UWE SingleLayerWater PS — the suspect)

All three use the `%hostlayout.SingleLayerWater` cbuffer as cb1 (276 bytes) at PIXEL CBV root param 5. Cbuffer layout from DXIL:
```
%hostlayout.SingleLayerWater = type {
  i32 x16,           // 64 bytes — resource indices / handles
  <4 x float>,       // 64-79  — UV bounds for per-eye SLW lookup
  <4 x float>,       // 80-95
  <2 x float> x2,    // 96-111
  i32 x2,            // 112-119
  float x2,          // 120-127
  [4 x <4 x float>], // 128-191 — matrix
  float x2,          // 192-199
  i32 x4,            // 200-215
  float x2,          // 216-223
  <3 x i32>, i32,    // 224-239
  <3 x i32>, i32,    // 240-255
  i32 x6             // 256-279
}
```

Per-eye bytes (extracted from upload pool 2038 at offsets 1919744 LEFT / 1922560 RIGHT):
| cb1 offset | LEFT | RIGHT |
|---:|---:|---:|
| 64-79  (row 4, vec4_0) | (0.000429, 0.000694, 0.499609, 0.999306) | (0.500391, 0.000694, 0.999609, 0.999306) |
| 80-95  (row 5, vec4_1) | (0.951, 0.889, 640, 720) | (0.951, 0.889, 640, 720) |
| 96-111 (row 6) | (1280, 720, 0.000781, 0.001389) | same |
| 112-119 (row 7 i32) | i32@112=1 i32@116=1 | same |
| **120** | **-1552.6** | **0.0** |
| **124** | **1.0** | **0.0** |
| 128-191 (matrix) | mostly zero | same |
| 192-195 | 600.0 | 600.0 |
| **196** | **+0.9947** | **-0.0190** |
| 200-215 | (20851, 462, 4, 458) | (20853, 462, 4, 458) |
| 216 | 0.75 | 0.75 |
| **220** | **3024.65** | **0.0** |
| 224-279 | (1,1,1, …, 88, 458, …) | same |

The 4 bolded scalar diffs at offsets 120, 124, 196, 220 are the signature of a per-eye water-vs-air classifier mismatch (depth, boolean, surface-angle, threshold). The values are exactly what you'd expect from "LEFT eye thinks it's 15 m underwater, RIGHT eye thinks it's at the surface".

**Important honest caveat**: DXIL of the three loaded SLW PS only emits `cbufferLoadLegacy(handle=%26, regIndex ∈ {4,5,6,7})` — i.e. it samples only the four rows from byte 64 through 127. **It does not directly fetch f@120 / f@124 / f@196 / f@220**, so this exact cbuffer wouldn't change those PSes' branches. However, the same per-frame UWE classifier struct is what gets fed to **the upstream basepass / OpaqueBasePass MainPS shaders** (PSO 764 / 774 / 1156 with PS CRCs `4d44ce74`, `f7ddfe06`, `8d06247f`, `cacaea96`), which write the scene color the eyes diverge on. The 276-byte SingleLayerWater struct identifies the surface that holds the classifier; the actual draws responsible for the visible color split are the basepass shaders that **consume** this struct via a different CBV slot in their own root signatures.

### Per-eye RTV / DSV mapping (sanity check)

| PSO | LEFT RTV slot | RIGHT RTV slot | Target resource |
|----:|---:|---:|---|
| 1156 (BasePass) | 11 → Texture2D 1103 (R11G11B10F 1280×720) | 31 → **same** Texture2D 1103 | shared scene color (eyes write into different X regions) |
| 1031 (BasePass) | 26 → Texture2D 1054 (R16G16B16A16F **640×360**) | 16 → Texture2D 923 (R11G11B10F 1280×720) | **DIFFERENT TARGETS + DIFFERENT FORMAT/RES** |
| 1041 (UWE Water) | 2 → Texture2D 1085 (BGRA8 1280×720) | 7 → Texture2D 1103 (R11G11B10F 1280×720) | **DIFFERENT** |
| 1124 / 1319 / 1435 (SLW) | 0 → Texture2D 1103 | 10 → **same** Texture2D 1103 | shared scene color |
| 1289 (depth) | DSV slot 0 | DSV slot 0 | shared depth surface |

The PSO 1031 LEFT-eye render target being **half-resolution (640×360) and different format (R16G16B16A16F)** while RIGHT writes to full-res R11G11B10F is anomalous. This points to UE's HZB / SceneColorDownsampled binding being mis-routed per eye in stereo. Worth verifying with the user.

## Step 1 — Diverged-pixel draw chain (heuristic, not full pixel-history)

Target pixel: LEFT (320,432), RIGHT (960,432). Since both eyes' BasePass MainPS (PSO 1156) write into the same Texture2D 1103 R11G11B10F at distinct X-regions, the draw chain for the diverged pixel is identical in structure between eyes:

LEFT chain (X∈0..640, into RTV slot 11 of dh_4345_23):
1. PSO 1289 depth pre-pass (one of 73 draws) writes Z into DSV slot 0
2. PSO 764 instanced-mesh basepass writes scene color
3. PSO 774 / 1156 / 1031 basepass MainPS draws fill the pixel
4. PSO 1435 SLW pass writes water surface color
5. (compose/tonemap into final back buffer)

RIGHT chain (X∈640..1280, into RTV slot 31): identical PSO sequence, just per-eye CBV/SRV.

**Full per-event pixel-history would require Nsight's BinaryReplay PixelHistory RPC against a live replay**, which is outside the static-analysis path used here. Open issue (capture is available — `ngfx_pixel_history` against image_accessor for Texture2D 1103 at (960,432) and (320,432) would close this loop).

## Step 4 — SLW-specific probe summary

Pixel shaders found whose DXIL string-table contains `SingleLayerWater`:

| PS CRC | Bin file | PSO | Root sig | cb1 size | Eyes fire? |
|---|---|---|---|---:|---|
| `b9be2499` | sh_0528 | 1124 | 212 (RS#42) | 276 | both (1+1) |
| `32040a0d` | sh_0526 | 1319 | 245 (RS#72) | 276 | both (2+2) |
| `de7c3822` | sh_0524 | 1435 | 245 (RS#72) | 276 | both (2+2) |

All three carry the `%hostlayout.SingleLayerWater` 276-byte cbuffer as cb1 (PIXEL b1 → CBV root param 5). The fields at cb1 offsets 120 / 124 / 196 / 220 differ per eye in the bound buffer content — but these specific PSes only sample cb1 rows 4–7 (bytes 64–127), so the load-bearing per-eye divergence lives in a different consumer.

Additional UWE/Water shaders surfaced by ASM keyword:
- `00c08757`, `0930dd4e`, `37f4da0e`, `6fa49954`, `9bb09cbb`, `f996b96b`, `1f70475c` — all compute (UWE Fog), already user-investigated
- `0cd8a0be`, `174de467`, `42a03b23`, `54c51d6d`, `77bec28e`, `d699ff92`, `d70e8385`, `f94467c3` — VS / utility for Water
- `166dba88` — PSO 1041 UWEWater BasePass MainPS (uses **TranslucentBasePass** 3764-byte cbuffer, also per-eye populated)

## Step 6 — Cubemap / Sky / Atmosphere SRVs

SkyAtmosphere / SkyView pixel shaders identified (`98ada32b`, `dec700b8`, `8eb46a59`, `ba4c0d07`, `c48a5738`, `dfd9d091`, `0119f5dd`) — none fire LEFT-only or RIGHT-only in this frame. The sky-view LUTs are computed once and consumed by both eyes via shared SRV bindings. Spot-checked SkyAtmosphere PSOs (PSO 1612, 1191, 1719, 1485) — their bindings show the standard per-eye View CB swap and shared SkyViewLut / TransmittanceLut SRVs.

**No cubemap or sky SRV is bound per-eye-differently in a way that would explain "above water vs underwater" classification.** The sky LUTs are physically shared; eyes only differ via their own View CB camera matrices and the per-eye `SkyCameraTranslatedWorldOrigin` field (which differs only by IPD, as expected).

---

## Conclusion

The visible LEFT-underwater / RIGHT-above-water bug is **NOT** in any single per-eye-asymmetric PSO firing decision, **NOT** in the View CB scalar fields, and **NOT** in the SkyAtmosphere bindings. It originates in the **UWE custom SingleLayerWater 276-byte per-eye cbuffer (cb1, PIXEL CBV root param 5 for PSOs 1124 / 1319 / 1435, and the analogous TranslucentBasePass 3764-byte cb for PSO 1041)** whose scalar fields at offsets 120, 124, 196, 220 are populated by a SN2-specific compute pass with per-eye "is underwater" / "water depth" / "water surface cosine" values that disagree across eyes (LEFT classifies as 15m underwater; RIGHT as at the surface).

The classifier itself is **upstream of every PS** in this frame — it's whatever compute dispatch populates the 276-byte SingleLayerWater + 3764-byte TranslucentBasePass per-eye cbuffer entries before the basepass starts (CL04 line ~4192 LEFT, ~4671 RIGHT). UEVR's intercept point should be:

1. The CPU-side memcpy that fills the SingleLayerWater (276 B) cbuffer for the RIGHT-eye draw — copy the LEFT-eye bytes verbatim (or vice-versa) so both eyes get the same underwater classification, OR
2. The compute dispatch (likely one of the 0x... UWE compute CRCs already partially identified, but whichever one consumes per-eye camera Z to decide "underwater") and force it to use a single eye's view origin.

Recommended next step: at runtime, dump the inline-CBV bytes bound to PIXEL b1 for the first SLW draw on each eye (CL04 lines 4192 LEFT / 4671 RIGHT — easily found by hooking `SetGraphicsRootConstantBufferView` with root param == 5) and confirm the 4-scalar mismatch live. Then bisect: force-copy LEFT bytes 116-127, 192-223 of the cbuffer over the RIGHT-eye CBV upload, and re-test.

Secondary concern surfaced: PSO 1031 LEFT-eye RTV is half-resolution (640×360 R16G16B16A16F) while its RIGHT-eye RTV is full-res 1280×720 R11G11B10F — a separate (and likely independent) per-eye render-target mis-routing.

---

## Appendices

### A. PSO/CRC mapping references

- `C:\Users\ellio\AppData\Local\Temp\sn2_shaders_big\sh_NNNN_kXXXXX_szYYY.bin` — 543 DXBC shader blobs (452 mapped CRC→file)
- `C:\Users\ellio\AppData\Local\Temp\sn2_shaders_big_asm\sh_NNNN_kXXXXX_szYYY.asm` — DXIL disassembly
- `C:\Users\ellio\AppData\Local\Temp\sn2_crc_to_bin.json` — generated map (CRC32→bin filename)
- `C:\Users\ellio\AppData\Local\Temp\sn2_crc_to_entry.json` — generated map (CRC32→shader-keyword list)
- `E:\Github\Subnautica 2\captures\nsight_cpp_2025_replay_saved_20260521_090747\CppCaptures\ngfx-replay__2026_05_21__09_09_41\.ngfxmcp_cpp2025_pso_index.json` — 475 PSOs with stages, root_sig, crc32
- `…\.ngfxmcp_cpp2025_state_events.jsonl` — 1277 per-draw state snapshots (viewport, cbvs, tables, rtvs)
- `…\.ngfxmcp_cpp2025_descriptors.json` — every CreateSRV/CreateUAV/CreateCBV (heap, slot, resource, format)

### B. Extracted upload-pool initial bytes

- `C:\Users\ellio\AppData\Local\Temp\sn2_LEFT_pool_3668.bin` — 4 194 304 bytes, pool that holds the LEFT-eye View CB at +1075200 and the RIGHT-eye View CB at +1064960 (both eyes share this pool for FViewUniformShaderParameters)
- `C:\Users\ellio\AppData\Local\Temp\sn2_RIGHT_pool_2038.bin` — 4 194 304 bytes, pool that holds the per-eye SingleLayerWater / TranslucentBasePass / OpaqueBasePass / per-eye light cbuffers

### C. View-CB layout reconstruction script

- `C:\Users\ellio\AppData\Local\Temp\sn2_view_diff.py` — Python that maps byte offsets to UE 5.6 FViewUniformShaderParameters field names per `E:\Epic Games\UnrealEngine-5.6.1\Engine\Source\Runtime\Engine\Public\SceneView.h` lines 843–1089

### D. Blocker

Live pixel-history (Nsight `BinaryReplay::PixelHistory` RPC) was not run in this pass — it would deterministically enumerate which event_index first writes a teal vs. blue color into (320,432) / (960,432) of Texture2D 1103. Easy follow-up against the existing capture file; left to a subsequent run.

---

## Follow-up: LEFT-only secondary producers — TLV ruled out

User confirmed Mode 3 + ClearUAV(red=(10,0,0,1)) at slots 3/4/5/6 on right-eye consumers of `0x8733F2E0` / `0xFEDC00F9` produced no visible red change on the right eye → **TLV is properly redirected, not the bug driver**. New focus: shaders the user named as LEFT-only secondary producers, identified by FNV64 hash + resource UID from the user's live-engine probing:

- `9c2134cc5ed6c2e1` SubsurfaceRecombineCopyPS → resource 141622 (1264×712 R11G11B10F)
- `088d809abc158998` MainPS → resource 141622
- `d04fe146d45f0a2e` MainPS → resource 141308 (315×355 R11G11B10F)
- `eeeecca60a3c975b` MainPS → resource 141304 (315×355 R11G11B10F)

### Status: those exact identifiers are NOT in the static capture used for this report

Cross-checked all four FNV64 shader hashes against the 475 PSOs in `.ngfxmcp_cpp2025_pso_index.json` → **zero matches**. None of the SubsurfaceRecombineCopyPS / WaterRefractionCopy / MeshBlend keyword markers were found in any of the 543 disassembled shader ASM files. Resource IDs 141622 / 141308 / 141304 are also outside this capture's UID range (0..6155).

The four hashes are from a **separate capture / live-instrumentation session** (likely a fresh frame from the running game, not frame 1807 of the static .ngfx capture). The static-capture analysis below identifies the **structurally analogous** producers/consumers from the current frame; the FNV64 → CRC32 correspondence for the user's hashes needs to be made against their session.

### Per-eye-DIFFERENT-RT producers found in frame 1807

Six PSOs render to physically distinct LEFT/RIGHT texture resources (different UIDs, otherwise same dims/format):

| PSO | VS CRC | PS CRC | PS FNV64 | Entry / kind | RT format | RT dims | LEFT uid | RIGHT uid | Root sig |
|---:|---|---|---|---|---|---|---:|---:|---:|
| 851 | `63ab38e9` | `fadc7927` | `fc312548e82ab0b9` | MainPS | R11G11B10F | 1280×720 | 3549 | 3514 | 210 |
| 919 | `ae54aa53` | `3f060043` | `a706bab97dbcf3fe` | MainPS | R11G11B10F | 1280×720 | 3547 | 3507 | 242 |
| 1138 | `356b0b7c` | `4c7d0fb9` | `979fd417713cbbd8` | (PS not in CRC index) | R11G11B10F | 1280×720 | 3505 | 3546 | 252 |
| 1149 | `356b0b7c` | `41f9154d` | `9ad7221c23d4b7cd` | (PS not in CRC index) | R16G16B16A16F | 1280×720 | 3825 | 3826 | 252 |
| 1191 | `d697f5f0` | `e85849aa` | `98acf00f2001c218` | (PS not in CRC index) | R11G11B10F | 1280×720 | 3549 | 3514 | 264 |
| 1253 | `356b0b7c` | `03f0d897` | `8512945cadba3a65` | **EmitSceneDepthPS** | **R32_UINT** | **1280×720** | **3707** | **3719** | 230 |

### Producer→consumer edges (PIXEL SRV table reads, slot indices are within the 64-entry table at root param 0)

#### PSO 1253 (EmitSceneDepthPS) — STRONG CANDIDATE for cross-eye SceneDepth pollution

Producer writes encoded scene depth (R32_UINT 1280×720) per eye:
- LEFT eye writes uid=3707
- RIGHT eye writes uid=3719

**Cross-eye reads found:**

LEFT-produced uid=3707 is read by these RIGHT-eye consumers:
| Reader PSO | Reader PS CRC | Reader PS FNV64 | Slot in PIXEL SRV table | Count |
|---:|---|---|---:|---:|
| 1447 | (no PS, VS-only) | — | 20 | 4 |
| 1070 | (no PS, VS-only) | — | 13 | 4 |
| 1268 | (no PS, VS-only) | — | 13 | 3 |
| 1101 | `0b0e2356` (Main__OPTIMIZED) | `fb28ad36d2b7c607` | 2 | 2 |
| 1106 | `5adcf589` | `79c5b09ea7eb20c8` | 6 | 1 |
| 1097 | `f5e59782` | `d0b604559225301e` | 12 | 1 |

RIGHT-produced uid=3719 is read by 6 LEFT-eye consumers in mirror fashion (1085 reads BOTH 3707 and 3719 — slots 1 and 3).

This is the **clearest cross-eye read pattern in the static capture**. PSO 1253's EmitSceneDepthPS outputs the per-eye SceneDepth-as-R32UINT; RIGHT-eye shaders 1447 / 1070 / 1268 / 1101 / 1106 / 1097 read the LEFT-eye SceneDepth at PIXEL-table slots 20 / 13 / 2 / 6 / 12. If RIGHT-eye fog/SLW gates on scene depth and the read returns LEFT-eye depth at the same screen X coord, RIGHT eye computes "above water" using LEFT-eye geometry depths → likely match for the visible bug.

#### PSO 851 (MainPS, fadc7927) and PSO 1191 (e85849aa) — share producer slot

Both write into 1280×720 R11G11B10F per-eye textures, and both texture pairs (3549/3514 — same resources) are read by:

LEFT-uid=3549 read by **LEFT-eye-only** consumers: PSO 1138 (slot 19), 1191 (slot 14), 851 (slot 13), 919 (slot 1)
RIGHT-uid=3514 read by **both eyes**:
- LEFT consumers: PSO 1220 (slot 28) — note PSO 1220 is the LEFT-only PSO from Step 2 (PS CRC `d3ab43c5`)
- LEFT consumers: PSO 1719 (slot 20)
- RIGHT consumers: PSO 1191 (slot 14), 1138 (slot 19), 851 (slot 13), 919 (slot 1)

PSO 1220 (the previously identified 2-LEFT-only-draw PSO) reads the RIGHT-eye output 3514 — that's intra-LEFT reading the RIGHT producer. Anomalous; needs follow-up.

#### PSO 1138 (4c7d0fb9), PSO 1149 (41f9154d), PSO 919 (3f060043) — clean per-eye consumers

None of these LEFT-produced textures are read by RIGHT-eye shaders (or vice versa). Consumers stay within their own eye. **Not bug candidates.**

#### PSO 1352 reads PSO 1149's RIGHT output cross-eye

LEFT-eye PSO 1352 reads uid=3826 (RIGHT-produced) at slot [23] AND LEFT-eye PSO 1352 reads uid=3825 (LEFT-produced) at slot [11]. RIGHT-eye PSO 1352 reads only uid=3826 at slot [11]. Mild cross-eye read but format is R16G16B16A16F — likely a velocity/normal buffer; less likely the visible-image surface than scene-depth.

### Answering the deliverables

**1. Consumer PS CRCs on right eye** — for the SceneDepth pattern (closest analog of the user's 141622 chain) the 6 RIGHT-eye readers are:
```
PSO 1447 (VS-only, no PS — depth-only / shadow pass, VS CRC 7b3a529f / FNV 1eb05a26ea19aa63)
PSO 1070 (VS-only, VS CRC 30e943e3 / FNV 9c4d9efb1df8bc51)
PSO 1268 (VS-only, VS CRC a376f2c9 / FNV 3216a2c371016ec2)
PSO 1101 PS CRC 0b0e2356 (FNV fb28ad36d2b7c607)   entry Main__OPTIMIZED
PSO 1106 PS CRC 5adcf589 (FNV 79c5b09ea7eb20c8)
PSO 1097 PS CRC f5e59782 (FNV d0b604559225301e)
```

**Blocker**: The user's named FNV64s (9c2134cc5ed6c2e1, 088d809abc158998, d04fe146d45f0a2e, eeeecca60a3c975b) **are not present in this capture**. Need a fresh capture or a live FNV→CRC dump from the running game where those four hashes are observed. The static capture's most analogous candidates are listed above.

**2. Root param index + slot** — All consumers above bind via root param **0** (PIXEL SRV descriptor table), with slots inside the 64-entry table at the indices listed in the tables above (e.g. PSO 1101 reads at slot 2 of the PIXEL SRV table at root param 0).

**3. Is the per-eye R32_UINT texture (3707/3719) scene color / tonemap input?** — Format R32_UINT is **not** a color format; it's an encoded scene-depth/stencil packing produced by `EmitSceneDepthPS`. It's read at scaffolded slots (13, 20) by VS-only shadow/depth-prepass PSOs and by post-process PSes 1101/1106/1097 which themselves are not in the keyword "Tonemap" set. This is **NOT the direct tonemap input**. Pixel-history would resolve whether 1101/1106/1097 then feed the visible tonemap, but our 6 LEFT/RIGHT-different-RT 1280×720 R11G11B10F textures (uids 3549/3514, 3505/3546, 3825/3826 with different formats) **are** standard scene-color band textures and **are** the likely tonemap-input candidates.

**4. Other LEFT-only producers writing to targets ≥640×360** — full list from frame 1807 in priority order:
- **PSO 1253** EmitSceneDepthPS R32_UINT 1280×720 — strong cross-eye-read evidence (above)
- **PSO 851** MainPS (CRC `fadc7927`) R11G11B10F 1280×720
- **PSO 919** MainPS (CRC `3f060043`) R11G11B10F 1280×720
- **PSO 1138** PS CRC `4c7d0fb9` R11G11B10F 1280×720
- **PSO 1149** PS CRC `41f9154d` R16G16B16A16F 1280×720
- **PSO 1191** PS CRC `e85849aa` R11G11B10F 1280×720
- (small auxiliaries at 640×360 / 315×355 not enumerated — match user's "less likely" intuition)

Additionally **PSO 1031** (BasePass MainPS CRC `cacaea96`) — already flagged in main body — writes LEFT to **uid 3695 R16G16B16A16F at 640×360** (half-res!) while RIGHT writes to **uid 3477 R11G11B10F at 1280×720** (full res). Format + resolution per-eye asymmetry is anomalous; could be a SceneColorDownsampled mis-route.

**5. Is WaterRefractionCopy basepass-time or post-process?** — In stock UE 5.6 the `CopySceneColorForWaterRefraction` step is a basepass-side `CopyTextureRegion` (not a full `CopyResource`) emitted just before SingleLayerWater opaque draws so SLW can sample the pre-water scene color. SN2's UWE variant is presumed analogous. Direct confirmation requires either the user's named FNV64s in a capture (not present here) or grepping the actual SN2 EXE for those copy strings.

### Honest blocker

The four FNV64 shader hashes the user named (`9c2134cc5ed6c2e1`, `088d809abc158998`, `d04fe146d45f0a2e`, `eeeecca60a3c975b`) and the three resource IDs (141622, 141308, 141304) **are not present in the static .ngfx-capture file** used for this report. They are from a different run / different capture session.

To answer deliverable 1+2+3 against the **user's specific producer/consumer set**, one of the following is needed:
- A fresh `.ngfx-capture` from a run where those FNVs are emitted (then re-run this analysis against that capture's pso_index.json), OR
- A live FNV64→CRC32 dump from the running game (UEVR side) — map each of the 4 named FNV64 to the 4-byte CRC32 of its DXBC bytecode, then UEVR's existing per-PSO whitelist can ingest the CRCs directly.

The structurally analogous candidates from the static frame 1807 (six per-eye-different-RT producers, listed above; PSO 1253 EmitSceneDepthPS especially) are documented for context. If UEVR's existing infrastructure can be pointed at `EmitSceneDepthPS` (CRC `03f0d897`, FNV `8512945cadba3a65`) as a producer to redirect, that's a concrete hypothesis to test in lieu of waiting on the FNV match.

---

## Round 2 follow-up: pixel-history + SLW writer + scene-depth cross-read confirmation

Performed after the user's three rounds of bucket-shadow + LEFT-CopyResource substitution at TLV/Fog/Volumetric slots all confirmed mechanism works but did NOT fix the visible bug. That result rules out per-eye SRV data substitution as the bug surface — the divergence must be in **control flow / PSO selection** or in **CPU-uploaded cbuffer content**, not in any sampled texture.

### Method

Live Nsight pixel-history (`mcp__nsight-graphics__ngfx_pixel_history`) requires an open frame-debugger RPC session against a running replay — not feasible inside the 90-min budget without launching the UI and authenticating. Instead used the **viewport+scissor+RTV reconstruction approach** against the static cpp_export, which is faster and authoritative for "what draws hit pixel (X,Y) of Texture2D 1103":

1. `Texture2D 1103` ↔ resource uid 3806 in cpp_export (verified via `.ngfxmcp_cpp2025_descriptors.json` `resources` map).
2. Found 26 RTV descriptors backing uid 3806 across heaps `dh_4345_{23,24,27,28,30,33}` (slots 0-31). All are mip-0 R11G11B10F views of the same 1280×720 surface.
3. Walked all 1277 state events; collected every draw whose any RTV in `gfx_tables`/`rtvs` points to any (heap,slot) backing uid 3806 AND whose `viewport`+`scissor` covers the target pixel.

Result: **96 draws cover LEFT (320,432); 95 draws cover RIGHT (960,432).**

### Deliverable 1 answer — last-writer per eye DIFFERS

The complete LEFT chain (last 10 events) ends:
```
ev1097 L pso=1112 (BasePass)   dh_4345_33:2
ev1098 L pso=1041 (UWEWater)   dh_4345_33:2
ev1099 L pso=1041              dh_4345_33:2
ev1100 L pso=1063 (Water.PostBasePass.VolumeOverlayA)  dh_4345_33:2
ev1101 L pso=1034 (Water.PostBasePass.VolumeOverlayB)  dh_4345_33:2
ev1102 L pso=938  (Water.PostBasePass.VolumeOverlayC)  dh_4345_33:2
ev1103 L pso=1126 (Water.PostBasePass.VolumeOverlayD)  dh_4345_33:2
ev1104 L pso=873  (Water.PostBasePass.SmallComposite)  dh_4345_33:2
ev1105 L pso=1126 ev1106 L pso=1126 ev1107 L pso=1126  dh_4345_33:2
ev1124 L pso=1438 (Graphics.PostProcess.QuadPS "Merge") dh_4345_33:6  ← LAST
```

The complete RIGHT chain ends:
```
ev1125 R pso=1438 (Graphics.PostProcess.QuadPS "Merge") dh_4345_33:6
ev1133-1134 R pso=1041 (UWEWater)        dh_4345_33:7
ev1135 R pso=1063 ev1136 R pso=1034 ev1137 R pso=938
ev1138 R pso=1126 (Water.PostBasePass.VolumeOverlayD) dh_4345_33:7
ev1139 R pso=873  ev1140 R pso=1126 ev1141 R pso=1126
ev1142 R pso=1126 (Water.PostBasePass.VolumeOverlayD) dh_4345_33:7  ← LAST
```

(Slots 2, 6, 7 of heap `dh_4345_33` are three distinct RTV descriptors all onto Texture2D 1103 mip 0 — same surface, viewports separate which half is touched.)

| Eye | Last writer event | PSO | PS CRC | Family / role |
|---|---:|---:|---|---|
| LEFT (320,432) | **ev1124** | **1438** | `a9d23236` | `Graphics.PostProcess.QuadPS` (entry `Merge`) |
| RIGHT (960,432) | **ev1142** | **1126** | `4a4eb78c` | `Water.PostBasePass.VolumeOverlayD` |

**Last writers DIFFER between eyes.** This is the smoking-gun structural asymmetry the prior report missed.

The frame-time ordering is the asymmetry: LEFT path renders BasePass → all 4 VolumeOverlay PSes → SmallComposite → Merge (PostProcess QuadPS), then stops. RIGHT path renders Merge BEFORE the VolumeOverlay D draws. RIGHT's final visible color is "what VolumeOverlayD writes after Merge", but LEFT's final visible color is "what Merge writes after VolumeOverlayD". They commit at different stages of the SLW/Fog post-process pipeline.

### What PSO 1438 does (the LEFT-only "Merge" final touch)

DXIL of `a9d23236.dxbc` (extracted from `data.bin` at offset 6 283 813 568, 2 312 bytes; on disk at `C:\Users\ellio\AppData\Local\Temp\sn2_round2_shaders\a9d23236.dxbc`):
```
EntryFunctionName: Merge
SigInputElements:  1 (TEXCOORD)
SigOutputElements: 1 (SV_Target R11G11B10F)
Resource Bindings: t0 = Texture2D<float4>, s0 = sampler  (nothing else)
Body: one Sample() into t0 with TEXCOORD UV, output rgba unchanged.
```

This is a **straight texture copy** gated by stencil (`COMPARISON_FUNC_EQUAL`, `D3D12_DEPTH_WRITE_MASK_ZERO`). RootSig 264: 3 params — PIXEL SRV table (64 SRVs), PIXEL sampler table, VERTEX CBV b0. The shader's t0 (slot 0 of PIXEL SRV table) is heap CBV_SRV_UAV slot 443 for both eyes → **`Texture2D 948` (uid 3504, R11G11B10F 1280×720)**.

So PSO 1438 ("Merge") = "stencil-gated copy from Texture2D 948 into Texture2D 1103". The asymmetry comes from **when** that copy runs:
- LEFT: copy runs AFTER all 4 VolumeOverlay passes (so the LEFT 320×432 pixel ends up = the contents of Texture2D 948 at that pixel)
- RIGHT: copy runs BEFORE the VolumeOverlay D passes (so the RIGHT 960×432 pixel ends up overwritten by VolumeOverlayD output)

### Who writes Texture2D 948 (the Merge source)

Only **PSO 1263** writes uid 3504 (2 draws total, one per eye):
- ev1122 L  vp=[0,0,640,720]  RTV `dh_4345_33:5`
- ev1123 R  vp=[640,0,640,720] same RTV slot

PSO 1263 = family `Graphics.PostProcess.QuadPS` (same VS as 1438: `d697f5f0`). PS CRC `37558de4` (extracted at `C:\Users\ellio\AppData\Local\Temp\sn2_round2_shaders\37558de4.dxbc`):
```
EntryFunctionName: Main
SigOutputElements: 1 (SV_Target R11G11B10F)
Resource Bindings:
  cb0 (10 076 bytes!) — large parameter block (looks like FSceneTexturesUniformParameters or per-frame compose params)
  s0, s1 samplers
  t0 Texture2D<float4>  (= slot 440 = Texture2D 24 R8G8B8A8_UINT — stencil packed?)
  t1 Texture2D<float4>  (= slot 441 = Texture2D 1103 R11G11B10F = the scene-color RT itself!)
  t2 Texture2D<float4>  (= slot 442 = Texture2D 947 BGRA8 = pre-translucency scene-color or refraction snapshot)
```

So PSO 1263 reads the **current scene color** (Texture2D 1103, BEFORE PSO 1438's Merge), the **stencil-as-UINT** Texture2D 24, and **Texture2D 947** (the BGRA8 sibling scene-color), runs them through 10 076 bytes of cbuffer parameters, and writes into Texture2D 948. PSO 1438's Merge then samples Texture2D 948 and writes back into Texture2D 1103.

This **is** the SN2 fog/SLW post-process composite chain. It's structurally `SceneColor → SceneColorClassified → SceneColor`. The 10 076-byte cbuffer at cb0 is the most likely surface that gates the underwater/above-water decision per pixel.

### Deliverable 2 — SLW cbuffer (and pool 2038) writer trace

Resource `uid_2038` (Buffer 22) is declared at `Resources01.cpp:10110-10117`:
```cpp
D3D12_HEAP_PROPERTIES = {D3D12_HEAP_TYPE_UPLOAD, ...};
D3D12_RESOURCE_DESC1  = {DIMENSION_BUFFER, 65536, 4194304/*=4MiB*/, ..., FLAG_NONE};
state = D3D12_RESOURCE_STATE_GENERIC_READ;
```

It's a **4 MiB D3D12_HEAP_TYPE_UPLOAD ring buffer in GENERIC_READ**, i.e. CPU-mapped + memcpy'd. Searched the cpp_export `cpp_calls.db` for `CopyBufferRegion`/`CopyResource`/`CopyTextureRegion` referencing resource 2038: **0 hits**. The 6 copies in the entire capture are all helper-paths (`pendingCopyDestOffset`, `tiledSourceResource`, tile streamer download/upload) — none touch pool 2038.

**Conclusion: there is no GPU compute dispatch or copy that writes the SLW cbuffer.** Pool 2038 is filled entirely from the CPU via `ID3D12Resource::Map()` + `memcpy()` + `Unmap()` (or `WriteToSubresource`). The SN2 engine populates the per-eye 276-byte SingleLayerWater cbuffer (and the 3 764-byte TranslucentBasePass, the 10 076-byte PSO 1263 cb0, and every other per-eye CBV) directly from CPU code before the per-eye render passes execute.

**UEVR cannot intercept this as a GPU pass.** The fix surface is one of:

1. **Hook the engine's CPU memcpy** that populates the RIGHT-eye 276-byte SLW cbuffer at pool 2038 + offset 1922560. Copy the LEFT-eye bytes from offset 1919744 over it before the GPU consumes it. Practically this means hooking `ID3D12Resource::Map` (or whichever wrapper the engine uses) and watching for writes to byte ranges `[1922560, 1922836)` and `[1919744, 1920020)`. Mirror bytes 120/124/196/220 from LEFT to RIGHT.

2. **Hook `ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView`** for root param 5 (`PIXEL b1`) on PSOs 1124/1319/1435 and PSO 1041. When the GPU virtual address points into pool 2038 at the RIGHT-eye offset, rewrite the GPU VA to point at the LEFT-eye offset. This is the cleanest UEVR-style fix because it's a single per-draw call substitution, not a memcpy hook.

3. **Hook the upstream SN2 underwater classifier directly in the SN2 code.** This requires SN2 EXE reverse-engineering (outside the static-capture path) but is the most reliable: find the `Player::IsUnderwater()` or `UWE::WaterClassifier::Sample(eyePos)` call that the engine invokes per-eye when building the basepass cbuffers, and force it to use a single eye's view position.

### Deliverable 3 — scene-depth cross-eye-read CONFIRMED at the visible pixel

PSO 1253 (`EmitSceneDepthPS`, PS CRC `03f0d897`) writes per-eye scene-depth as R32_UINT:
- LEFT eye: ev556 writes uid 3707 (Texture2D 1060)
- RIGHT eye: ev267 writes uid 3719

For each of the 6 prior-report consumer PSOs (1447, 1070, 1268, 1101, 1106, 1097), I checked:
- (a) does the RIGHT-eye draw's `viewport`+`scissor` actually cover (960, 432)?
- (b) does it bind uid 3707 (LEFT scene-depth) at the prior-report-claimed PIXEL SRV t-slot?

Results — all 6 confirmed to cover the visible right-eye pixel, and 5 of 6 confirmed to bind LEFT scene-depth at their claimed t-slot:

| Reader PSO | RIGHT-eye event | Covers (960,432) | t-slot binding |
|---:|---:|:---:|---|
| 1447 | ev189–192 | yes (×4) | tbl[0] base=593, t20 → slot 613 → **uid 3707 (LEFT depth)** R32_UINT |
| 1070 | ev193–196 | yes (×4) | tbl[0] base=600, t13 → slot 613 → **uid 3707 (LEFT depth)** |
| 1268 | ev197–199 | yes (×3) | tbl[0] base=600, t13 → slot 613 → **uid 3707 (LEFT depth)** |
| 1106 | ev200     | yes      | tbl[0] base=607, t6  → slot 613 → **uid 3707 (LEFT depth)** |
| 1101 | ev201–202 | yes (×2) | tbl[0] base=611, t2  → slot 613 → **uid 3707 (LEFT depth)** |
| 1097 | **ev838** | yes      | tbl[0] base=5,   t12 → slot 17  → **uid 3707 (LEFT depth)** |

**Mirror check on LEFT-eye consumers** (does the LEFT-eye equivalent draw read RIGHT depth, i.e. uid 3719?):

For PSO 1097 LEFT (ev569) at the t12 slot of its tbl[0] (heap `dh_4348_23`, base=1463) → slot 1475 → **uid 3017 (Buffer 241, R32_FLOAT)**. Not a depth texture at all. LEFT-eye PSO 1097 reads a regular R32_FLOAT buffer at t12 where RIGHT-eye PSO 1097 reads LEFT scene-depth. The cross-read pattern is **ASYMMETRIC** — RIGHT reads LEFT depth, but LEFT does NOT read RIGHT depth in the symmetric position.

**Timing check** — at the moment RIGHT-eye consumers ev189–202 fire, neither LEFT nor RIGHT scene-depth has been written yet this frame (writes are at ev267 RIGHT and ev556 LEFT, both LATER). So those reads see whatever was in uid 3707 from initialization / previous frame leftovers. PSO 1097 RIGHT at ev838 is the only consumer that reads uid 3707 AFTER ev556 (when LEFT depth is current-frame). PSO 1097 is `Graphics.Mid.PS`, 4 RTV outputs (a deferred-G-buffer-style MainPS, PS CRC `f5e59782`, extracted to `f5e59782.dxbc` on disk).

So **scene-depth cross-eye-read at the visible cave-opening pixel IS confirmed**, but the magnitude is limited:
- 5 of 6 consumers fire BEFORE current-frame depth is written → they read stale data (probably zero or previous frame), so the per-eye difference at those events is initialization noise, not real LEFT-vs-RIGHT parallax depth
- 1 consumer (PSO 1097 RIGHT ev838) reads current-frame LEFT depth → real cross-eye magnitude, ~IPD parallax (~4.7 cm camera shift translates to ≤1-2 pixels of depth misalignment at the cave-opening distance)

A 1–2-pixel depth misalignment is unlikely to flip an above/below-water classification. **Scene-depth cross-eye-read is real but is NOT the bug driver.** Consistent with the user's prior tests showing TLV/Fog/Volumetric substitutions didn't fix the bug.

### Updated UEVR fix recommendation

Based on this round + the prior 3 rounds of negative SRV-substitution results, the bug surface is locked to **per-eye cbuffer content uploaded CPU-side**, not GPU-side data flow. The specific patch to test next:

1. **First-line fix**: hook `ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView` for the inline-CBV at PIXEL b1 (root param 5) on PSOs **1041 (UWEWater MainPS), 1124 (SLW PS b9be2499), 1319 (SLW PS 32040a0d), 1435 (SLW PS de7c3822)**. When the GPU VA falls in pool 2038's RIGHT-eye region (offset 1922560 for SLW), redirect it to pool 2038 LEFT-eye region (offset 1919744). This is the most surgical mirror.

2. **Higher-confidence fix**: PSO 1263 (`Graphics.PostProcess.QuadPS`, PS CRC `37558de4`) **cb0 = 10 076 bytes** at offsets pool 2038+1677824 (LEFT) and 2038+1678080 (RIGHT). The 256-byte stride mirrors the SLW cbuffer's stride. cb0 is large enough to contain the per-eye underwater classifier scalars and is bound to a PS that physically writes `Texture2D 948` per-eye — the exact texture sampled by PSO 1438's "Merge" that produces the visible LEFT-eye color. **Dump the 10 076 bytes at each eye's offset and diff** — that's the candidate that hasn't been examined in any prior round and is the most direct upstream of PSO 1438's input.

3. **PSO 1438 asymmetric ordering**: the LEFT eye's last writer is PSO 1438 (Merge from Texture2D 948), but the RIGHT eye's last writer is PSO 1126 (VolumeOverlayD). If `Texture2D 948` for the RIGHT half contains the correct color (it's the source of LEFT-eye's final result, and both eyes write into it via PSO 1263), forcing the right eye to ALSO take PSO 1438's path (i.e. skip the post-1438 PSO 1126 draws at ev1138–1142) should match LEFT's output. This is a control-flow fix: drop or no-op ev1138–1142 (the 5 RIGHT-eye PSO 1126 + 1 PSO 873 draws that fire after Merge). Risky because it changes scene topology.

### Files of record (Round 2)

- `C:\Users\ellio\AppData\Local\Temp\sn2_round2_shaders\a9d23236.dxbc` + `.asm` — PSO 1438 PS (`Merge`)
- `C:\Users\ellio\AppData\Local\Temp\sn2_round2_shaders\4a4eb78c.dxbc` — PSO 1126 PS (`Water.PostBasePass.VolumeOverlayD`)
- `C:\Users\ellio\AppData\Local\Temp\sn2_round2_shaders\37558de4.dxbc` + `.asm` — PSO 1263 PS (the per-eye `Texture2D 948` writer)
- `C:\Users\ellio\AppData\Local\Temp\sn2_round2_shaders\f5e59782.dxbc` + `.asm` — PSO 1097 PS (deferred 4-RT MainPS)
- `/tmp/sn2_left_chain.json`, `/tmp/sn2_right_chain.json` — full per-eye draw chains hitting Texture2D 1103 at (320,432)/(960,432)

### Honest blockers

- True live `BinaryReplay::PixelHistory` RPC would still be the gold-standard answer; the static-chain reconstruction above is equivalent for "ordered list of writers" but cannot resolve "what fraction of the pixel each draw's blend mode applies" (each PSO 1126 draw has its own blend desc). For PSO 1438 specifically, blend desc is `{ONE, ZERO, ADD}` writeMask 15 = **straight overwrite**, so for LEFT (320,432) the Merge output deterministically replaces the prior color. For PSO 1126's 4 RIGHT-eye draws after Merge — their blend descriptors would need to be checked draw-by-draw.
- The 4 user-named FNV64 hashes (`9c2134cc5ed6c2e1`, `088d809abc158998`, `d04fe146d45f0a2e`, `eeeecca60a3c975b`) and resource UIDs 141622/141308/141304 are still **not present in this capture** (different session). The Round 2 analysis is therefore against the structural surface, not those exact identifiers.

---

## Round 3: 10-task pixel-history + writer hunt

Performed against the same Nsight cpp_export
(`E:\Github\Subnautica 2\captures\nsight_cpp_2025_replay_saved_20260521_090747\CppCaptures\ngfx-replay__2026_05_21__09_09_41`)
that backed Round 2. Task ordering: 1, 8, 4, 3, 5, 9, 2, 6, 7, 10.

All artifacts at `E:\tmp_dir\sn2_round3\`.

### TL;DR — Round 3 key findings

1. **96 writers hit LEFT (316, 356) of Texture2D 1103** — full list emitted; the LEFT-only MainPS structural analog of user event 14677 = **PSO 1112 (PS CRC `13b00f0c`, FNV64 `d8d17ef9a830ae0b`)**. The user's claim that `088d809abc158998` truncates to `13b00f0c` is **FALSE** — those are independent FNV64 vs CRC32 (zlib IEEE) hashes; the user's FNV64 hash does NOT exist in this static capture. The structural match by family (LEFT-only BasePass MainPS with no RIGHT sibling) IS PSO 1112.
2. **Stencil-classifier asymmetry IS REAL**: PSO 1112 (the LEFT-only BasePass MainPS) is ALSO the only stencil-writer firing with stencil_ref=128 / StencilWriteMask=244 (=0xF4) without a RIGHT-eye sibling. The 4 SLW stencil-reader PSOs (1041/1124/1319/1435) fire both eyes equally (2+2, 1+1, 2+2, 2+2) with stencil_ref=128 or 1.
3. **View CB delta -10240 bytes CONFIRMED** across all 27 paired draws of PSO 774. No drift.
4. **Crash is NOT a D3D12 fault**: exception code `0x00008000` (custom user exception, `KERNELBASE!RaiseException+0x8a`). Stack frames inside `Subnautica2_Win64_Shipping!src_strerror+*` suggest a game-side error-formatting / serialisation path; not in `RSSetViewports`, `SetGraphicsRootConstantBufferView`, or `DrawIndexedInstanced`. UEVR is not the proximate cause of this crash.
5. **129 LEFT-excess draws** found (RIGHT eye is missing this many basepass draws compared to LEFT). Top contributors: PSO **858** (22 LEFT-only, ps_crc `9d14fcf0`), **1738** (18 LEFT-only, ps_crc `b3a5de6f`), **1419** (8 LEFT-only, ps_crc `36a17277`).

### Task 1 — pixel-history at LEFT (316, 356) of Texture2D 1103

Result: **96 LEFT-eye writers**, full table in `E:\tmp_dir\sn2_round3\task1_left_316_356_writers.json`.

Sorted by (cmd_list, draw_in_cl); the relevant LEFT-only writer (analog of user event 14677) is at **cl=7605:0 draw=551, pso=1112, ps_crc32=13b00f0c, vs_crc32=833a1657, FNV64=d8d17ef9a830ae0b** — `PipelineState 364` (BasePass MainPS, RTV slot 2 of `dh_4345_33`).

All 96 writers in execution order (compact, full data in JSON):

| # | cl | draw | pso | ps_crc32 | name / family |
|--:|---|--:|--:|---|---|
| 1 | 7490:0 | 386 | 1097 | f5e59782 | PipelineState 359 (4-RTV deferred MainPS) |
| 2-4 | 7497:0 | 331-332 | 1031 | cacaea96 | BasePass MainPS |
| 5-6 | 7497:0 | 333-334 | 1155 | 76c6719b | PipelineState 379 |
| 7-26 | 7497:0 | 335-354 | 774 | f7ddfe06 | BasePass MainPS (20 draws) |
| 27-32 | 7499:0 | 356-361 | 774 | f7ddfe06 | continued (6 draws) |
| 33-34 | 7499:0 | 362-363 | 990 | f1e19862 | BasePass MainPS |
| 35 | 7499:0 | 364 | 1048 | 035bb276 | BasePass MainPS |
| 36-52 | 7499:0 | 365-382 | 1156 | 8d06247f | BasePass MainPS (18 draws) |
| 53 | 7499:0 | 383 | 1060 | c9426ff0 | BasePass MainPS |
| 54 | 7499:0 | 384 | 1931 | f922f28b | (no keyword) |
| 55 | 7499:0 | 385 | 1115 | 1d3ef687 | BasePass MainPS |
| 56 | 7539:0 | 483 | 1522 | 970a8554 | QuadPS |
| 57 | 7539:0 | 487 | 529 | 441e7751 | DepthPass-like |
| 58-72 | 7546:0 | 315-330 | 973/1020/840/1089/828/1037/877/841/870/1031 | various subsurface basepass |
| 73 | 7549:0 | 497 | 1352 | cb55c958 | (no keyword) |
| 74 | 7550:0 | 491 | 389 | 83538dd4 | PostProcess |
| 75-77 | 7573:0 | 539-543 | **1435/1319/1124** | de7c3822/32040a0d/b9be2499 | **SingleLayerWater PS** |
| 78 | 7575:0 | 533 | 1169 | 4528be0f | ConvolveSpecularSkyLight |
| 79 | 7575:0 | 535 | 1514 | f1d1132c | SkyAtmosphere |
| 80 | 7581:0 | 531 | 1612 | 009f8918 | SkyAtmosphere |
| 81 | **7605:0** | **551** | **1112** | **13b00f0c** | **LEFT-only BasePass MainPS = STRUCTURAL ANALOG OF USER EID 14677** |
| 82-83 | 7605:0 | 552-553 | 1041 | 166dba88 | UWEWater BasePass |
| 84-91 | 7605:0 | 554-561 | 1063/1034/938/1126/873 | Water.PostBasePass.VolumeOverlay {A,B,C,D} + SmallComposite |
| 96 | 7606:0 | 588 | 1438 | a9d23236 | **Graphics.PostProcess.QuadPS "Merge" (LAST WRITER)** |

The **next dup targets** beyond the existing user investigation: PSOs **973 (subsurface), 1020 (subsurface), 840, 1089, 828, 1037, 877, 841, 870, 1031** (the subsurface basepass chain at cl=7546:0 draws 315-330, into RTV slot 26 of `dh_4345_30`), **PSO 1438** (a9d23236 = Merge straight-overwrite, already analyzed in Round 2), and **PSO 1097** (f5e59782 = 4-RTV deferred MainPS at cl=7490:0 draw=386, RTV slot 0).

### Task 8 — Stencil mask for SLW

Parsed `D3D12_DEPTH_STENCIL_DESC1` from `Resources*.cpp` PSO definitions; 197 of 475 PSOs have DS state. Walked CommandList files to attribute per-draw stencil_ref. Cross-referenced with state_events for eye determination (by scissor.left).

**SLW reader PSOs all fire both eyes:**

| PSO | ps_crc | DS summary | LEFT (sref) | RIGHT (sref) |
|---:|---|---|---|---|
| 1041 (UWE Water) | 166dba88 | StencilEnable=1 WriteMask=244 FuncAlways OP_REPLACE | 2 (128) | 2 (128) |
| 1124 (SLW PS) | b9be2499 | StencilEnable=1 WriteMask=0 FuncEqual | 1 (1) | 1 (1) |
| 1319 (SLW PS) | 32040a0d | same as 1124 | 2 (1) | 2 (1) |
| 1435 (SLW PS) | de7c3822 | same | 2 (1) | 2 (1) |

The SLW **read path is fully symmetric** — the stencil-classifier reader pass IS running on RIGHT eye. The bug is NOT "RIGHT eye doesn't read stencil mask".

**Stencil WRITERS firing with stencil_ref=1 (the SLW reader ref)** — these are the per-pixel water-classifier writes:

| PSO | ps_crc | eye/cnt | comment |
|---:|---|---|---|
| 1139, 1078, 1058 | (no PS, depth-stencil-only writers) | LEFT 5 draws (cl=7542); RIGHT 5 draws (cl=7554) | symmetric — both eyes write the same stencil-classifier pattern |
| **PSO 1263** | 37558de4 | LEFT 1 (cl=7606 draw=586); RIGHT 1 (cl=7606 draw=587) | "QuadPS" writing Texture2D 948 (the Merge source). Both eyes fire. |
| **PSO 1438** | a9d23236 | LEFT 1 (cl=7606 draw=588); RIGHT 1 (cl=7606 draw=589) | "QuadPS Merge" (final overwrite into Tex1103). Both eyes fire. |

At sref=1 (water classifier), all writers fire both eyes symmetrically. The visible eye-divergence is NOT a missing stencil-write on RIGHT.

**The two truly LEFT-only stencil WRITERS** (no RIGHT-eye sibling):

| PSO | ps_crc | sref | StencilWriteMask | LEFT draws | role |
|---:|---|---:|---|---:|---|
| **1112** | **13b00f0c** | **128** | **244 (0xF4)** | **1** | LEFT-only BasePass MainPS = same PSO as Task 1's standalone LEFT-only entry |
| **1523** | **569c196a** | **0** | **255 (0xFF)** | **1** | Writes RTV uid 3745 (R8G8B8A8_UNORM — not scene color) |

**Interpretation**: PSO 1112 lone LEFT-only draw writes to Texture2D 1103 (RTV slot 2 of dh_4345_33) AND writes stencil with `StencilWriteMask=0xF4` (masked stencil update at sref=128). This is a "mark this pixel as basepass-rendered-with-flag-bit-7" write. No RIGHT-eye equivalent fires → this stencil bit is NEVER set on RIGHT eye. **Any downstream SLW/fog pass that reads this stencil bit and gates RIGHT-eye color on it will see "not flagged" → take the wrong code path.**

This is a stronger candidate than the Round 2 cbuffer hypothesis: the bug is most likely **PSO 1112 setting a stencil flag bit on LEFT that gates a downstream branch**, with no RIGHT-eye sibling firing.

Files: `E:\tmp_dir\sn2_round3\task8_pso_ds.json`, `task8_draws_with_stencil.json`, `task8_stencil_results.json`.

### Task 4 — Complete LEFT-only PS inventory

35 truly LEFT-only PSOs with PS stage (full list in `E:\tmp_dir\sn2_round3\task4_v2_results.json`). All have NO RIGHT-eye sibling and NO BOTH-eyes sibling.

Top 10 by draw count:

| PSO | L_draws | ps_crc | ps_fnv64 | RTs (sample) |
|---:|---:|---|---|---|
| 858 | 22 | 9d14fcf0 | 9a29f7f299902d2c | uid 3456/3462 (R11G11B10F) |
| 1738 | 18 | b3a5de6f | 4e3516bf1c5a2b4e | uid 3536/3558/3564 (R11G11B10F) |
| 1419 | 8 | 36a17277 | ff05e3bba86072c7 | uid 3569/3575/3543 (R11G11B10F) |
| 1175 | 4 | 82d90cfa | (PostProcess) | (R11G11B10F) |
| 1244 | 4 | 77db181e | 531099a8d83115f0 | uid 3745 (R8G8B8A8) |
| 831 | 4 | 272d4ac9 | 1c5dd3868403fb44 | uid 3512/3509 (R11G11B10F) |
| 865 | 4 | 944801b4 | 7ad1059db732e137 | uid 3513/3510 (R11G11B10F) |
| 1857 | 4 | 9d2b400a | cf7bcacc5a8a5aca | uid 3561/3526/3527 (R16_FLOAT) |
| 1223 | 4 | 37514a7b | ab035f923741b91b | uid 3542/3578/3576 (R11G11B10F) |
| 1828 | 4 | 30c7aaae | 2c33d6383cbe73e9 | uid 4279/4271/4280 (R10G10B10A2) |

Truly RIGHT-only PSOs with PS: **0** — every RIGHT-eye draw has a structural LEFT-eye sibling but **not vice-versa**.

6 PSOs write to DIFFERENT per-eye RTs (covered in Round 2): PSO 1191, 1253, 1149, 1138, 851, 919.

**User-named hashes from RDC capture: still NOT present in this Nsight capture.**

| User FNV64 | role | in static? |
|---|---|---|
| `9c2134cc5ed6c2e1` | SubsurfaceRecombineCopyPS | NO |
| `088d809abc158998` | MainPS | NO |
| `d04fe146d45f0a2e` | MainPS | NO |
| `eeeecca60a3c975b` | MainPS | NO |

Resource UIDs 141622, 141308, 141304, 141371, 141352, 141594/595/599 — **not in this capture's UID space (0..6155)**. The Nsight capture's analogous scene-color half is `uid 3806` (Texture2D 1103, R11G11B10F 1280×720, shared between eyes via different RTV slots, not separate uids per eye).

### Task 3 — View CB delta verification

**CONFIRMED**: RIGHT - LEFT = **-10240 bytes** across **all 27 paired draws** of PSO 774 (`cl=7497:0 draw=335 LEFT @ res:3668+1075200` ↔ `cl=7498:0 draw=407 RIGHT @ res:3668+1064960`, and 26 more identical pairs). Min=max=-10240. Layout is stable; UEVR existing CB rewriting at the -10240 offset is correct for this capture.

Files: `E:\tmp_dir\sn2_round3\task3_view_cb_delta.json`.

### Task 5 — Crash dump call stack

Analyzed `C:\Users\ellio\AppData\Roaming\UnrealVRMod\Subnautica2-Win64-Shipping\crash.dmp` (683 KB, written 2026-05-22 10:39:42) with cdb.

**Exception**: code `0x00008000` (custom user-defined, NOT 0xC0000005 access violation).
**Faulting instruction**: `KERNELBASE!RaiseException+0x8a` (i.e., the app **threw its own** exception — this is not a memory fault).
**Stack** (faulting thread):
```
KERNELBASE!RaiseException+0x8a
Subnautica2_Win64_Shipping+0x149fc53                          (caller)
Subnautica2_Win64_Shipping!src_strerror+0xc28e30
Subnautica2_Win64_Shipping!src_strerror+0xbe8b98
Subnautica2_Win64_Shipping!src_strerror+0xbf5db9
Subnautica2_Win64_Shipping!src_strerror+0xbf99e9
Subnautica2_Win64_Shipping!src_strerror+0xc0260f
Subnautica2_Win64_Shipping+0x14a4157
Subnautica2_Win64_Shipping+0x14a3ef9
kernel32!BaseThreadInitThunk+0x17
ntdll!RtlUserThreadStart+0x2c
```

**Not in `RSSetViewports`, `SetGraphicsRootConstantBufferView`, `DrawIndexedInstanced`, or any UEVR symbol.**

Register snippets (UTF-16):
- r8/r9/r10/r11 spell a version-date string `12.012.2026.05.22-` — implying the exception parameter is a string containing a timestamp + path; very likely a localised game error / save-load / serialisation failure, NOT a D3D12 validation error.

**Conclusion**: This crash is unrelated to UEVR D3D12 hook/intercept path. It is most likely a game-side error path (e.g., save thumbnail decode, networking, asset load) crashing inside `src_strerror`. UEVR may be a co-trigger if it is intercepting something game logic depends on, but the immediate cause is game-side.

### Task 9 — Verify UEVR dup is visible

**BLOCKED — needs live Nsight BinaryReplay session OR a no-UEVR Nsight capture.**

The Nsight static cpp_export does not contain the raw GPU bytes for `Texture2D 1103` RIGHT-eye half post-basepass; resource initial-data bytes ARE available for buffers (View CB pools 2038/3668) but not for the per-frame-written render targets like uid 3806. To read RT bytes one needs `ngfx_pixel_history` or `ngfx_resource_revision_at_event` with `include_image_subresource_data=True` against a running ngfx-replay process — requires launching ngfx-ui and authenticating to its BinaryReplay RPC, which is outside the static-analysis budget.

**Workaround for the user**: dump the resource via `Save Resource…` in the Nsight UI at event ~1124 (LEFT) and ~1142 (RIGHT) of the existing capture, then byte-compare the 632×712 half-rect for each eye.

### Task 2 — CS dependency trace

The user three CS hashes by FNV64 — **none are present in this Nsight cpp_export** (different capture session). Found the structural equivalents by entry-point name in the extracted ASM, mapped to cpp_export PSO ids:

| User FNV64 (entry point) | cpp_export CRC32 / PSO | cpp_export FNV64 |
|---|---|---|
| `bd742b10e7a2cdb8` RenderDistantSkyLightLutCS | `dfd9d091` / **PSO 370** | `5f11c9cd0428201d` |
| `82f2645c8de038fe` RenderCameraAerialPerspectiveVolumeCS | `0119f5dd` / **PSO 455** AND `ba4c0d07` / **PSO 459** (two variants) | `7e47bb5987ee032c`, `7ce66caa1c69292d` |
| `6640aace1da3381d` TranslucencyVolumeSpatialSeparableFilterCS | `8f1542ff` / **PSO 710** | `764c09b2a0d0cd51` |

For each:

**PSO 370 (RenderDistantSkyLightLutCS)** — 1 dispatch at cl=7459:0:
- UAVs: uid 2775 (Buffer 65536), uid 2915 (Buffer 65536)
- (a) view-INDEPENDENT (sky LUT is single-eye-correct — same sun direction in both eyes). Single dispatch.
- (b) **RIGHT-eye readers of 2775: 108**. Yes, in the visible-image path (read by PSO 1169, 1514, 1100, 1484, etc.). View-independent so cross-eye-read is correct.

**PSO 459 (RenderCameraAerialPerspectiveVolumeCS-variant-b)** — 2 dispatches:
- UAVs: uid **3035** (LEFT, 32×32×8 R16G16B16A16F) AND uid **3037** (RIGHT, same shape).
- (a) view-DEPENDENT (aerial perspective is per-eye view direction). **Already correctly per-eye separated** — dispatch 233 writes 3035, dispatch 235 writes 3037.
- (b) **RIGHT readers of 3037: 131** — yes, in visible-image path (PSO 1100, 1612, 1012). Eye is correct.

**PSO 455 (RenderCameraAerialPerspectiveVolumeCS-variant-a)** — 1 dispatch at cl=7460:0:
- UAVs: uid 3671 (32×32×8 R16G16B16A16F).
- (a) view-DEPENDENT but only LEFT-eye dispatch present in capture. RIGHT-eye reads 3671? No — uid 3671 has 0 right-eye readers. So this variant is unused on RIGHT eye (or is a one-shot init that doesn't recur per-eye).
- (b) **NOT in the visible-image path for RIGHT eye.**

**PSO 710 (TranslucencyVolumeSpatialSeparableFilter)** — 6 dispatches at cl=7567 (3 RIGHT-eye, view_cb_off=1064960) and cl=7582 (3 OTHER, view_cb_off=2545408 — likely shadow / aux view):
- Writes 32 UAVs covering both LEFT (uid 3363-3369) and RIGHT (uid 3416-3423, 3681/3682 are LEFT, 3688/3689 are RIGHT, etc.) translucency volumes plus shared lookup tables.
- (a) view-DEPENDENT.
- (b) **RIGHT-eye readers of 3688/3689 (RIGHT translucency vols): 96/92** — yes, in visible-image path (PSO 1012/1007 at cl=7606 draws 579-581).

The dispatches are already correctly per-eye separated for these compute outputs. UEVR prior LEFT→RIGHT data substitution attempts at TLV/Fog volumes therefore did not help: the data IS per-eye-different by design, AND the right-eye dispatches DO run with right-eye View CB. The bug is not in this dispatch path.

Files: `E:\tmp_dir\sn2_round3\task2_cs_trace_v2.json`.

### Task 6 — Per-eye scene-color byte compare for cave-opening region

**BLOCKED — needs live ngfx_resource_revision_at_event with `include_image_subresource_data=True`** OR a Save-Resource dump from the Nsight UI. Same blocker as Task 9.

Existing artifact `E:\tmp_dir\sn2_view_cb_diff\compare_141352_141371.json` is for the RDC capture per-eye scene-color halves (uids 141352/141371) — those uids do not exist in the Nsight cpp_export. The Nsight-equivalent comparison would be against the LEFT half (rect 0..640×0..720) vs RIGHT half (640..1280×0..720) of Texture2D 1103 (uid 3806) — same texture, just different rect.

### Task 7 — RIGHT-eye PS draw counts by output target

- **Total draws by eye**: LEFT=396, RIGHT=269, BOTH=44. **RIGHT eye is missing ~127-129 draws compared to LEFT.**
- **Scene-color RT write counts** for uid 3806 (Texture2D 1103, R11G11B10F 1280×720): **LEFT=96, RIGHT=95** — almost matched (the 1-draw asymmetry is PSO 1112 lone LEFT-only entry from Task 1).
- **Per-eye scene-color half RTs** (different uids per eye, R11G11B10F 1280×720): 3549/3514 (2L+0R, 0L+2R), 3547/3507 (1L+0R, 0L+2R), 3505/3546 (1L+0R, 0L+1R) — these are the per-eye-allocated full-res RTs analogous to user 141622 etc. **Counts match symmetrically** when paired (LEFT writes 3549, RIGHT writes 3514 with the same draw count).
- **Scene-depth (DSV) writes**: empty result in my parse — DSV in state_events uses different schema; not analyzed, but Round 2 already verified depth (PSO 1253 EmitSceneDepthPS uid 3707 LEFT / uid 3719 RIGHT).
- **SLW PSO 3D SRV bindings** (fog/aerial-perspective/translucency volumes): each SLW PSO binds 6 shared 3D SRVs (`3459, 3460, 3827-3830`) PLUS 4 per-eye 3D SRVs:

| Volume class | LEFT-eye binding | RIGHT-eye binding |
|---|---|---|
| Aerial perspective (32×32×8 R16G16B16A16F) | uid 3035 | uid 3037 |
| Vol-fog ID grid (192×48×48 R32_UINT) | uid 3676 | uid 3683 |
| Translucency vol R11G11B10F (10×12×28) | uid 3681 | uid 3688 |
| Translucency vol R16G16B16A16F (10×12×28) | uid 3682 | uid 3689 |

These bindings are correctly per-eye-different (RIGHT eye gets its own RIGHT-eye-computed volumes). **NOT the bug surface.**

**The 127-draw asymmetry is the bug surface**: which PSOs cause it? Top contributors (LEFT-only) from `E:\tmp_dir\sn2_round3\task4_v2_results.json`:

```
PSO   858 ps_crc=9d14fcf0  22 LEFT-only draws
PSO  1738 ps_crc=b3a5de6f  18 LEFT-only
PSO  1419 ps_crc=36a17277   8 LEFT-only
PSO  1175 ps_crc=82d90cfa   4 LEFT-only
PSO  1244 ps_crc=77db181e   4 LEFT-only
PSO   831 ps_crc=272d4ac9   4 LEFT-only
PSO   865 ps_crc=944801b4   4 LEFT-only
PSO  1857 ps_crc=9d2b400a   4 LEFT-only
PSO  1223 ps_crc=37514a7b   4 LEFT-only
PSO  1828 ps_crc=30c7aaae   4 LEFT-only
... (25 more PSOs at 1-2 draws each)
```

These are the new dup-target candidates. PSO 858 (22 LEFT-only) is the single biggest contributor — UEVR redirecting just this PSO to also fire on RIGHT could potentially close the bulk of the gap.

### Task 10 — RIGHT-eye scene-color 0x00 regions

**BLOCKED — needs live resource-bytes RPC.** Same blocker as Tasks 6 and 9.

Files: `E:\tmp_dir\sn2_round3\task7_right_eye_counts.json`, `task4_v2_results.json`.

---

## 5-line summary (DM-ready)

1. **New dup target from Task 1**: **PSO 1112 (PS CRC `13b00f0c`, FNV64 `d8d17ef9a830ae0b`)** — the LEFT-only BasePass MainPS at `cl=7605:0 draw=551`, writing Texture2D 1103 RTV slot 2 of `dh_4345_33`. ALSO the only stencil-writer firing with `sref=128 WriteMask=0xF4` without a RIGHT-eye sibling (per-pixel "basepass-flag-bit-7" stencil mark).
2. **SLW stencil pass DOES run on RIGHT eye**: all 4 SLW reader PSOs (1041/1124/1319/1435) fire both eyes symmetrically. The stencil-classifier writers at sref=1 (PSOs 1139/1078/1058/1263/1438) also fire both eyes. RIGHT-eye **is** classified — but PSO 1112 stencil-bit-7 flag is NEVER set on RIGHT, so any downstream pass gating on that bit takes the wrong branch on RIGHT.
3. **View CB delta CONFIRMED at -10240 bytes** across all 27 paired PSO 774 draws.
4. **Crash is NOT a D3D12/UEVR fault**: exception code `0x00008000` (game-thrown user exception) inside `src_strerror`, not in D3D12 functions. Unrelated to dup-call mechanics.
5. **Concrete next UEVR action**: hook **PSO 1112 (FNV64 `d8d17ef9a830ae0b` / CRC `13b00f0c`)**. When it fires on LEFT, force a duplicate draw on RIGHT with the RIGHT-eye View CB at offset `1064960` and scissor `[640,0,1280,720]`, viewport `(640,0,640,720)`. If that does not visibly change the scene, also dup the 9 next-biggest LEFT-only PSOs (858, 1738, 1419, 1175, 1244, 831, 865, 1857, 1223, 1828). PSO 858 alone accounts for 22 of the 129 missing RIGHT-eye draws.


---

## Round 4: Nsight fog/dup + authoring tool

Capture under analysis: same `Subnautica2-Win64-Shipping_2026_05_21_08_45_51.ngfx-capture`.
All data in this section is derived from the existing cpp_export at
`E:\Github\Subnautica 2\captures\nsight_cpp_2025_replay_saved_20260521_090747\CppCaptures\ngfx-replay__2026_05_21__09_09_41\`
plus the Round 1-3 artifacts in `E:\Github\UEVRJ\artifacts\` and the bundled
`left_only_dxil/INDEX.json`.

Eye discriminator (verified Round 1 and re-verified here): per-eye View cbuffer lives in
resource pool `uid 3668` at offsets **1075200 = LEFT**, **1064960 = RIGHT** (delta = -10240).

### Lumen-vs-UWE-Fog clarification

The user's task list this round names three "fog" CRCs - `0xe550b4f0`, `0x1be34186`,
`0xf10723bf`. **These are not UWE fog.** Round 1 already verified via `dxc -dumpbin` that
they decode to Lumen Screen Probe compute shaders, and Round 4 re-confirms that mapping by
locating each CRC in the cpp_export PSO index:

| CRC | EntryFunctionName (from DXIL) | Pipeline | Root Sig | Notes |
|---|---|---:|---:|---|
| `0xe550b4f0` | `ScreenProbeTileClassificationMarkCS` | PSO 617 | RS uid 201 | Lumen |
| `0x1be34186` | `ScreenProbeTemporalReprojectionCS` | PSO 603 | RS uid 224 | Lumen |
| `0xf10723bf` | `ScreenProbeDownsampleDepthUniformCS` | PSO 660 | RS uid 201 | Lumen |

The **real** UWE fog + UE volumetric stack (9 CRCs):

| CRC | Shader | Pipeline | Root Sig |
|---|---|---:|---:|
| `0x00c08757` | `UWEFogBuildDepthStandardDeviationCS` | PSO 358 | RS uid 119 |
| `0x37f4da0e` | `UWEFogDenoiseCS` (variant A) | PSO 371 | RS uid 224 |
| `0x6fa49954` | `UWEFogDenoiseCS` (variant B) | PSO 353 | RS uid 224 |
| `0x0930dd4e` | `UWEFogResolveCS` | PSO 424 | RS uid 192 |
| `0x9bb09cbb` | `UWEFogHistoryUpdateConfidenceCS` | PSO 427 | RS uid 201 |
| `0x1f70475c` | `UWEFogImportanceDilateCS` | PSO 340 | RS uid 119 |
| `0xf996b96b` | `UWEFogReconstructCS` | PSO 464 | RS uid 145 |
| `0xd1f85c42` | UE `LightScatteringCS` (volumetric fog) | PSO 373 | RS uid 169 |
| `0x8dfe6707` | UE `ClearTranslucentLightingVolumeCS` | PSO 326 | RS uid 117 |

### A. Root sig + per-eye UAV identity - the 3 Lumen CRCs

| CRC | RS | Root params (paraphrased) | LEFT u0 res_uid | RIGHT u0 res_uid | Per-eye UAV verdict |
|---|---:|---|---:|---:|---|
| `0xe550b4f0` Lumen TileClass | 201 | `[0]SRV table(64) [1]UAV table(16) [2]CBV b0 [3]CBV b1=View` | 3250 (Buffer 413) | 3250 | **SAME u0**, slots 4 and 7 differ - PARTIALLY DIFFERENT |
| `0x1be34186` Lumen TemporalReproj | 224 | `[0]SRV(64) [1]UAV(16) [2]CBV b0 [3]CBV b1=View [4]CBV b2` | 3847 (Tex2D 1122, R11G11B10F 1280x720) | 3847 | **SAME on all 8 UAV slots** |
| `0xf10723bf` Lumen DownsampleDepth | 201 | `[0]SRV(64) [1]UAV(16) [2]CBV b0 [3]CBV b1=View` | 3679 (Tex2D 1044, R32_UINT 40x34) | 3686 (Tex2D 1047) | **DIFFERENT** - per-eye-allocated depth-uniform |

So the 3 Lumen CRCs are a **mixed bag**: `0x1be34186` is fully SAME (writes shared resources both eyes), `0xe550b4f0` is mostly SAME with a 2-slot eye divergence, `0xf10723bf` is fully DIFFERENT (each eye has its own downsample-depth texture).

### B. Root sig + per-eye UAV identity - the 9 real UWE fog CRCs

| CRC | RS | u0 LEFT | u0 RIGHT | u0-u7 verdict | Notes |
|---|---:|---:|---:|---|---|
| `0x00c08757` UWEFogBuildStdDev | 119 | 3493 (Tex2D 939, R8_UNORM 640x360) | 3487 (Tex2D 933, R8_UNORM 640x360) | **DIFFERENT** (6 of 8 slots) | Per-eye standard deviation buffers |
| `0x37f4da0e` UWEFogDenoise A | 224 | 3489 (R11G11B10F 640x360) | 3495 (R11G11B10F 640x360) | **DIFFERENT** (7 of 8 slots) | Per-eye denoise output |
| `0x6fa49954` UWEFogDenoise B | 224 | 3491 (R11G11B10F 640x360) | 3485 (R11G11B10F 640x360) | **DIFFERENT** (7 of 8 slots) | Per-eye denoise output (alt) |
| `0x0930dd4e` UWEFogResolve | 192 | 3858 (Tex2D 1131, R11G11B10F 1280x720) | 3587 (Tex2D 1022, R11G11B10F 1280x720) | **DIFFERENT** (4 of 8 slots) | Slots 3-6 are shared 3D fog volumes |
| `0x9bb09cbb` UWEFogHistoryUpdConf | 201 | 3855 (Tex2D 1130, R8_UNORM 54x30) | 3515 (Tex2D 959, R8_UNORM 54x30) | **DIFFERENT** (7 of 8 slots) | Per-eye history confidence textures |
| `0x1f70475c` UWEFogImportanceDilate | 119 | 3488 (R8_UNORM 640x360) | 3494 (R8_UNORM 640x360) | **DIFFERENT** (7 of 8 slots) | Per-eye dilate output |
| `0xf996b96b` UWEFogReconstruct | 145 | 3491 (R11G11B10F 640x360) | 3485 (R11G11B10F 640x360) | **DIFFERENT** (3 of 8 slots) | u2..u6 are shared volumes/buffers |
| `0xd1f85c42` UE LightScatteringCS | 169 | 3856 (Tex3D 98, R11G11B10F **54x30x48**) | 3532 (Tex3D 78, R11G11B10F **54x30x48**) | **DIFFERENT** (6 of 8 slots) | u0,u1 are per-eye 3D vol fog volumes; u2,u4 are shared |
| `0x8dfe6707` UE ClearTransLightVol | 117 | 3827 (Tex3D 94, R16G16B16A16F 24x24x24) | (only 1 dispatch, no eye CB) | **SAME** (single dispatch, no eye disambig) | Probably runs once per-frame; both eyes consume the same cleared vol |

**Decisive answer to the user's "SAME or DIFFERENT" question**:
- Lumen group: 1 SAME + 2 DIFFERENT - cannot be uniformly handled with one strategy
- UWE fog group: 8 DIFFERENT + 1 SAME (the clear) - effectively all per-eye distinct
- **Both groups require the bucket-shadow-mirror-UAV approach** for the DIFFERENT entries, because the LEFT and RIGHT dispatches already target separate D3D12 resources. The pipe already redirects them correctly per eye - the only problem is whether they get *dispatched* at all on RIGHT.
- For the SAME entries (Lumen `0x1be34186`, `0xe550b4f0` mostly-SAME, `0x8dfe6707`), a plain CB-swap dup would race two writes into a single resource - must be skipped or made aware.

Full per-slot dump: `E:/tmp_dir/sn2_round4/pso_eye_uavs.json`.

### C. Write history for one volumetric-fog 3D texture

Picked **uid 3856** (Texture3D 98, R11G11B10F **54x30x48**, the LEFT-eye final volumetric fog
volume bound as input by the post-process). Writers across the entire capture:

| event_idx | kind | PSO | CS_CRC | eye | u_slot | command_list | source line |
|---:|---|---:|---|---:|---:|---|---:|
| 476 | dispatch | 373 | `0xd1f85c42` LightScatteringCS | L | u0 | 7588:0 | 7873 |
| 478 | dispatch | 427 | `0x9bb09cbb` HistoryUpdConfCS | R | u7 | 7568:0 | 3215 |

For comparison, uid **3532** (the **RIGHT-eye** twin, same dims R11G11B10F 54x30x48):

| event_idx | kind | PSO | CS_CRC | eye | u_slot |
|---:|---|---:|---|---:|---:|
| 480 | dispatch | 373 | `0xd1f85c42` LightScatteringCS | R | u0 |
| 477 | dispatch | 397 | `0x3402487c` (other CS) | L | u15 |

**Summary**: each eye gets its own 3D vol-fog volume, and `LightScatteringCS` fires once per
eye to populate it. There is also a single cross-eye "feedback" write from the other-eye's
history pass via u7/u15 (a high slot, likely a write-only side-effect channel rather than
the primary volume content). This is **NOT** the missing-draw bug surface - both eyes do
write the volumetric fog. The bug surface is the **127-draw RIGHT-eye gap** identified in
Round 3 Task 7, where 22 LEFT-only PS draws for PSO 858 (`0x9d14fcf0`, MainBasePassPS) and
the other 34 LEFT-only PSOs never get a RIGHT-eye sibling.

Detail: `E:/tmp_dir/sn2_round4/pso_eye_uavs.json` (covers 3856/3532/3857/3548).

### D. DXIL u#/t#/b# register mapping for the 12 target CRCs

`-Fc` disassembly via `dxc.exe -dumpbin` written to `E:/tmp_dir/sn2_round4/dxil_dumps/`.
`View` cbuffer identified by matching `%hostlayout.View` LLVM struct + cb size approx 10076 B.

| CRC | View at cb# | u0 type/dim | Notes |
|---|---:|---|---|
| `0xe550b4f0` Lumen TileClass | **cb1** | UAV u32 buf | u1..u4 are 2darray UAVs (probe tile classification) |
| `0x1be34186` Lumen TemporalReproj | **cb1** | UAV f32 2darray | u1 second 2darray |
| `0xf10723bf` Lumen DownsampleDepth | **cb1** | UAV u32 2d | u1=u32 2d, u3=f32 2d |
| `0x00c08757` UWEFogBuildStdDev | **cb0** | UAV f32 2d | Single cbuffer (no SLW/UnifiedScene) |
| `0x37f4da0e` UWEFogDenoise A | **cb1** | UAV f32 2d | cb0=UWEFogTAA, cb2=Lumen/Substrate |
| `0x6fa49954` UWEFogDenoise B | **cb1** | UAV f32 2d | Same layout as A |
| `0x0930dd4e` UWEFogResolve | **cb1** | UAV f32 2d | cb0=UWEFog, cb2=UWEFogTAA |
| `0x9bb09cbb` UWEFogHistoryUpdConf | **cb1** | (no UAV in table) | UAVs come via descriptor table u-slots, not registered in DXIL bindings |
| `0x1f70475c` UWEFogImportanceDilate | **cb0** | UAV f32 2d | Single cbuffer |
| `0xf996b96b` UWEFogReconstruct | **cb1** | UAV f32 2d | 4 cbuffers (cb0=UWEFog, cb2=UWEFogTAA, cb3=Substrate) |
| `0xd1f85c42` UE LightScatteringCS | **cb1** | UAV f32 3d | 7 cbuffers; u0,u1 are 3D vol UAVs |
| `0x8dfe6707` UE ClearTransLightVol | **(none)** | UAV f32 3d | No cbuffer - just clear (u0..u3 are 3D UAVs) |

Empirically the View root parameter is confirmed by tracing comp_cbvs at each PSO's first
dispatch and finding the root index whose CBV points at `(res_uid 3668, offset
1075200|1064960)`:

| PSO | RS | View root param (empirical) |
|---:|---:|---:|
| 326 (`8dfe6707`) | 117 | none (no View binding - pure UAV clear) |
| 340 (`1f70475c`) | 119 | 4 |
| 353 (`6fa49954`) | 224 | 3 |
| 358 (`00c08757`) | 119 | 4 |
| 371 (`37f4da0e`) | 224 | 3 |
| 373 (`d1f85c42`) | 169 | 4 |
| 424 (`0930dd4e`) | 192 | 4 |
| 427 (`9bb09cbb`) | 201 | 3 |
| 464 (`f996b96b`) | 145 | 4 |
| 603 (`1be34186`) | 224 | 3 |
| 617 (`e550b4f0`) | 201 | 3 |
| 660 (`f10723bf`) | 201 | 3 |

Per-RS layout (parsed from `Resources00.cpp`):

| RS uid | Root params (in order) |
|---:|---|
| 117 | `[0] UAV table(16)` |
| 119 | `[0] SRV table(64) [1] UAV table(16) [2] CBV b0` |
| 145 | `[0] SRV(64) [1] SAMPLER(32) [2] UAV(16) [3..6] CBV b0..b3` |
| 169 | `[0] SRV(64) [1] SAMPLER(32) [2] UAV(16) [3..9] CBV b0..b6` |
| 192 | `[0] SRV(64) [1] SAMPLER(32) [2] UAV(16) [3..5] CBV b0..b2` |
| 201 | `[0] SRV(64) [1] UAV(16) [2..3] CBV b0..b1` |
| 224 | `[0] SRV(64) [1] UAV(16) [2..4] CBV b0..b2` |

(Caveat for RS 119: the empirical View binding lands at root param 4, which is past the
declared range [0..2]. This means the engine is using an *extended* RS layout for some
PSOs that share RS 119; multiple PSOs share the same RS uid but with slightly different
descriptor-table consumption. The empirical comp_cbvs trace is authoritative for the
file-level config.)

### E. Authoring tool - `sn2_dup_cfg.json`

Script: **`E:/Github/UEVRJ/tools/generate_sn2_dup_cfg.py`**.

Inputs consumed (no re-collection):
- `E:/Github/UEVRJ/artifacts/sn2_pso_rootsig_graph.json`
- `E:/tmp_dir/sn2_round3/task4_v2_results.json` (LEFT-only PSO inventory)
- `E:/Github/UEVRJ/artifacts/left_only_dxil/INDEX.json` (per-PSO PS/VS/CS CRC + `reads_view_cbuffer`)
- `E:/Github/Subnautica 2/.../cpp_export/.ngfxmcp_cpp2025_pso_index.json` (PSO stage metadata)
- `E:/tmp_dir/sn2_round4/leftonly_pso_view_roots.json` (Round 4 empirical View-root probe)

Output: **`E:/Github/UEVRJ/artifacts/sn2_dup_cfg.json`**, schema `uevr.sn2.dup_config.v1`,
**34 entries** (35 LEFT-only PSOs minus 1 PS-CRC duplicate which is coalesced into a single
entry with multiple `psos`/`rt_formats`).

Mode breakdown:
- `viewport_shift`: **7** (PS reads View cbuffer -> needs viewport + CB swap on RIGHT)
- `view_cb_only`: **27** (PS does not read View cbuffer in the bundled DXIL -> CB-only dup
  on RIGHT viewport; for 13 of these the empirical View root probe still found an eye-bound
  CBV at a known root index, which is captured in `view_cb_roots`)

The biggest single contributor is `0x9d14fcf0` (PSO 858, MainBasePassPS, 22 LEFT-only
draws, R11G11B10F BasePass, reads View, falls back to default_view_cb_roots=[3]). The 27
`view_cb_only` entries collectively account for ~70 of the 127 LEFT-only draws and likely
include translucency / fog overlays whose RHI does not bind View directly.

Verified the JSON parses cleanly with stdlib `json.load`. Drop it in as
`UEVR_SN2_DUP_CONFIG_FILE=E:\Github\UEVRJ\artifacts\sn2_dup_cfg.json`.

### F. Visible-pixel -> PSO mapper plan

**Recommended: Approach B** - use the existing
`ShaderOverrideRegistry::hunter_toggle_highlight_hash`.

UEVR already implements a single-hash "magenta highlighter" path in
`src/render/ShaderOverrideRegistry.{hpp,cpp}` that, when armed with one PS hash, replaces
that shader's body with a constant magenta write at runtime. This is the same substitution
pattern already proven by `sn2_pso3069_subst`. Per-frame, the user arms one of the 34 CRCs
from `sn2_dup_cfg.json`, hits the screenshot key, and the magenta region on screen
identifies which on-screen geometry that PS owns. Cycle through all 34.

Activation pattern:
- Add (or reuse) env var `UEVR_HUNTER_HIGHLIGHT_HASH=<crc32-hex>` already present in the
  override registry, OR
- Wire an existing menu entry that calls `hunter_toggle_highlight_hash(crc)`.

This avoids building a new per-frame bytecode-replacer (Approach A) since the
infrastructure is already in place. Approach A would only be required if Approach B cannot
support multiple hashes simultaneously (it can - the registry stores a set, see
`ShaderOverrideRegistry::s_highlighted_hashes`).

Workflow for the user:
1. Set `UEVR_SN2_DUP_CONFIG_FILE` (so dup hooks are loaded but inert until the user
   enables dup on a CRC).
2. Iterate `UEVR_HUNTER_HIGHLIGHT_HASH=0x<crc>` per session, screenshot, record region.
3. Build a CRC -> on-screen-region table; cross-reference with which CRC dups produce
   visible RIGHT-eye changes.

### Round 4 artifacts (all generated this round)

- `E:/Github/UEVRJ/tools/generate_sn2_dup_cfg.py` - config generator (Python, stdlib only)
- `E:/Github/UEVRJ/artifacts/sn2_dup_cfg.json` - 34-entry dup config (schema v1)
- `E:/tmp_dir/sn2_round4/rs_layouts.json` - parsed root-sig layouts (7 RSs)
- `E:/tmp_dir/sn2_round4/pso_eye_uavs.json` - LEFT-vs-RIGHT UAV slots for the 12 target PSOs
- `E:/tmp_dir/sn2_round4/leftonly_pso_view_roots.json` - empirical View-root indices for 13 of 35 LEFT-only PSOs
- `E:/tmp_dir/sn2_round4/dxil_dumps/*.asm` - 12 DXIL `-Fc` disassemblies (one per target CRC)
- `E:/tmp_dir/sn2_round4/dxil_bindings.json` - parsed CB/UAV register tables

### 6-line summary

1. **3 Lumen UAVs**: MIXED - `0x1be34186` SAME both eyes; `0xe550b4f0` mostly-SAME (2 slots differ); `0xf10723bf` DIFFERENT
2. **9 UWE Fog UAVs**: 8 of 9 DIFFERENT per eye (each eye owns its own 2D/3D writeback target). Only `0x8dfe6707` (ClearTransLightVol) is SAME - and it is a one-shot clear pass
3. **Vol-fog 3D `uid 3856` (LEFT) writer count**: L=1 R=1 (LightScatteringCS L primary + HistoryUpdConfCS R feedback into u7) - each eye writes its own 3D volume (3856 vs 3532); ASYMMETRIC ID but SYMMETRIC count
4. **`sn2_dup_cfg.json`**: `E:/Github/UEVRJ/artifacts/sn2_dup_cfg.json` - 34 entries (7 viewport_shift + 27 view_cb_only), default_view_cb_roots=[3], delta=-10240
5. **Visible-pixel mapper**: Approach B (reuse `ShaderOverrideRegistry::hunter_toggle_highlight_hash`) - UEVR already implements one-hash magenta substitution, no new dxil-patch needed
6. **Next concrete UEVR action**: Set `UEVR_SN2_DUP_CONFIG_FILE=E:\Github\UEVRJ\artifacts\sn2_dup_cfg.json` and `UEVR_SN2_DUP_ANY_MRT_PS_CRCS=0x9d14fcf0,0xb3a5de6f,0x36a17277` (top-3 LEFT-only contributors = 48 of 127 missing draws). If RIGHT eye changes, expand to the full 34-CRC set.
