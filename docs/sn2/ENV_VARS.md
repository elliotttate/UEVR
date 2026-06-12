# Environment Variable Reference

Every UEVR_SN2_* env var, what it does, where it's read.

## Required for any SN2 work

| Env Var | Type | Default | Purpose |
|---|---|---|---|
| `UEVR_ENABLE_D3D12_DIAGNOSTIC_COMMAND_LIST_HOOKS` | bool | `0` | Required for draw/dispatch/viewport/barrier tracing. Auto-enabled when `UEVR_STEREO_FORENSICS=1` unless bind-only opt-out is set; also auto-enabled by `UEVR_SN2_FIX_RIGHT_EYE_UNDERWATER=1` |
| `UEVR_SN2_FIX_RIGHT_EYE_UNDERWATER` | bool | `0` | One-switch current candidate fix. Replays the missing left-only underwater draw (`0x13b00f0c`, shape `DrawIndexedInstanced` 76608/1) to the right viewport using a captured real right-eye View CB, defaulting the swap to root 4 |
| `UEVR_SN2_DUP_CONFIG_FILE` | path | (none) | Path to dup_cfg JSON |
| `UEVR_SN2_DUPLICATE_SLW_BASEPASS_RIGHT` | bool | `0` | Enable the dup function |

## Current right-eye underwater draw replay

| Env Var | Type | Default | Purpose |
|---|---|---|---|
| `UEVR_SN2_FIX_RIGHT_EYE_UNDERWATER` | bool | `0` | Preferred current test flag. Expands to the narrow `0x13b00f0c` replay path, captured-right View CB discovery, command-list hooks, root bookkeeping, and upload-buffer tracking |
| `UEVR_SN2_DUP_USE_CAPTURED_RIGHT_VIEW` | bool | `0` | Explicitly enables captured-right View CB mode. The wrapper enables this behavior internally |
| `UEVR_SN2_DUP_VIEW_SWAP_ROOTS` | csv | wrapper: `4`; legacy: all detected View-UB roots | Restricts which graphics root CBVs are swapped during replay. Root 4 is the validated candidate for `0x13b00f0c`; root 8 alone failed in prior tests |
| `UEVR_SN2_DUP_ANY_MRT_PS_CRCS` | csv | empty; wrapper adds `0x13b00f0c` | PS CRCs to duplicate regardless of RTV count. Used for the single-RTV teal-source geometry draw |
| `UEVR_SN2_WATER_BASEPASS_PS_CRCS` | csv | legacy default list | Legacy water-basepass CRC list. When the wrapper is enabled and this env is unset, the legacy list is suppressed to avoid broad replay confounds |
| `UEVR_SN2_DUP_DETAIL_LOG` | bool | `0` | Logs swapped View roots, RTVs, and sampled fixed SRVs for each duplicate. Use if visual validation fails and you need to verify what the replay actually samples/writes |
| `UEVR_SN2_DUP_DEBUG_COLOR` | bool | `0` | Replaces the duplicated draw's PS with a debug color. Use only as a coverage/depth/stencil isolation probe |
| `UEVR_SN2_UNDERWATER_DEFER_REPLAY` | bool | `0` | Captures the left-only `0x13b00f0c` draw state and replays it later instead of immediately in the duplicator |
| `UEVR_SN2_UNDERWATER_DEFER_AFTER_DRAW` | bool | `0` | Replays after the triggering right-eye draw instead of before it |
| `UEVR_SN2_UNDERWATER_DEFER_TRIGGER_N` | int | `1` | Old draw-count trigger: replay on the Nth right-eye draw after capture |
| `UEVR_SN2_UNDERWATER_DEFER_REPEAT` | bool | `0` | Keep the pending replay armed. Throttled to once per present frame |
| `UEVR_SN2_UNDERWATER_DEFER_CLEAR_AFTER` | bool | `0` | After replay, clear the replay viewport magenta. Diagnostic only |
| `UEVR_SN2_UNDERWATER_DEFER_DEBUG_COLOR` | bool | `0` | Try to replay with the debug-color PSO clone. May no-op for early-created PSOs whose original desc was not cached |
| `UEVR_SN2_UNDERWATER_DEFER_ANCHOR_PS` | hex CRC | `0` | Late-anchor mode: replay only when a right-eye draw with this pixel-shader CRC is reached. Works from `DrawInstanced` and `DrawIndexedInstanced` anchors |
| `UEVR_SN2_UNDERWATER_DEFER_ANCHOR_TRACE` | bool | `0` | Bounded log of right-eye PS CRCs/RTV handles after a pending deferred replay is captured |
| `UEVR_SN2_UNDERWATER_DEFER_ANCHOR_TRACE_MAX` | int | `96` | Initial row cap for anchor trace logging |
| `UEVR_SN2_UNDERWATER_DEFER_REQUIRE_SAME_RTV` | bool | draw-count: `1`, anchor: `0` | Require current RTV to match captured draw RTV before replaying. Defaults off in anchor mode because later visible passes may use a different target |

Wrapper fallback behavior:

- Preferred targeting is by PS CRC `0x13b00f0c`.
- Inject-after-run sessions can miss PSO creation, leaving `ps_crc=0`.
- When `UEVR_SN2_FIX_RIGHT_EYE_UNDERWATER=1`, the code falls back to the known draw shape:
  left viewport, one RTV, `index_count=76608`, `instance_count=1`, and a reasonable viewport size.
- The latest log-validated run is `captures\wrapper_fix_shape_fallback_20260526_171728`.

## Diagnostic / observation

| Env Var | Type | Purpose |
|---|---|---|
| `UEVR_SN2_BINDING_ANALYZER_JSON` | path | Output path for binding analyzer |
| `UEVR_SN2_CB_DUMP_DIR` | path | Output dir for CB dumps |
| `UEVR_SN2_CB_DUMP_PSOS` | csv | PS CRCs to dump |
| `UEVR_SN2_CB_DUMP_ROOTS` | csv | Root params to dump |
| `UEVR_SN2_CB_DUMP_MAX` | int | Max dumps per tuple (default 8) |
| `UEVR_SN2_PSO_BYTECODE_DIR` | path | Output dir for DXBC dumps |
| `UEVR_SN2_RT_SNAPSHOT` | bool | Enable RT snapshot infrastructure |
| `UEVR_SN2_RT_DIFF_DIR` | path | RT diff output dir |
| `UEVR_SN2_RT_DIFF_EVERY_N_FRAMES` | int | RT diff capture frequency (default 60) |
| `UEVR_SN2_STATE_INSPECTOR_DIR` | path | State inspector output dir |
| `UEVR_SN2_STATE_INSPECTOR_PS_CRCS` | csv | Target PSOs |
| `UEVR_SN2_STATE_INSPECTOR_EYE` | left/right/both | Eye filter |
| `UEVR_SN2_STATE_INSPECTOR_MAX` | int | Max dumps per CRC (default 32) |

## Stereo Forensics Layer

| Env Var | Type | Purpose |
|---|---|---|
| `UEVR_STEREO_FORENSICS` | bool | Enable the unified D3D12 frame/event database |
| `UEVR_STEREO_FORENSICS_DIR` | path | Output root for `session_*` bundles (default `C:\tmp\uevr_forensics`) |
| `UEVR_STEREO_FORENSICS_FRAME_STRIDE` | int | Capture every Nth frame (default 30) |
| `UEVR_STEREO_FORENSICS_START_FRAME` | int | First frame eligible for capture (default 1) |
| `UEVR_STEREO_FORENSICS_MAX_CAPTURED_FRAMES` | int | Captured-frame burst cap, `0` for unlimited (default 4) |
| `UEVR_STEREO_FORENSICS_MAX_EVENTS_PER_FRAME` | int | Per-captured-frame event cap before truncation, `0` for unlimited (default 12000) |
| `UEVR_STEREO_FORENSICS_MAX_EVENTS_PER_KIND_PER_FRAME` | int | Per-kind event cap, `0` for unlimited (default 8000) |
| `UEVR_STEREO_FORENSICS_MAX_SET_PSO_PER_FRAME` | int | Per-frame `set_pso` bind cap, `0` for unlimited (default 768) |
| `UEVR_STEREO_FORENSICS_MAX_ROOT_BINDS_PER_FRAME` | int | Per-frame root-bind cap, `0` for unlimited (default 4096) |
| `UEVR_STEREO_FORENSICS_MAX_TOTAL_EVENTS` | int | Burst event cap, `0` for unlimited (default 120000) |
| `UEVR_STEREO_FORENSICS_MAX_TOTAL_BYTES` | int | Approximate per-burst `events.jsonl` byte cap, `0` for unlimited (default 268435456) |
| `UEVR_STEREO_FORENSICS_FLUSH_EVERY_CAPTURED_FRAMES` | int | Flush JSONL after N captured frames (default 1) |
| `UEVR_STEREO_FORENSICS_ARM_FILE` | path | Sentinel file that rearms a new capture burst when created (default `C:\tmp\uevr_forensics_arm.txt`) |
| `UEVR_STEREO_FORENSICS_KEEP_HOOK_DETAIL_ON_SKIPPED_FRAMES` | bool | Keep expensive descriptor-read/draw detail collection on skipped/stopped frames; normally off |
| `UEVR_STEREO_FORENSICS_RECORD_D3D12DIAG_ON_SKIPPED_FRAMES` | bool | Alias for keeping legacy D3D12Diagnostics draw/root records on skipped/stopped frames |
| `UEVR_STEREO_FORENSICS_TRACK_SIDE_TABLES_AFTER_STOP` | bool | Keep descriptor/resource/heap side tables updated after a burst stops so later re-armed gameplay captures have current metadata (default 1) |
| `UEVR_STEREO_FORENSICS_TRACK_PRODUCERS_ON_SKIPPED_FRAMES` | bool | Keep lightweight RTV/copy/clear producer history on skipped/stopped frames without recording full draw events (default 1) |
| `UEVR_STEREO_FORENSICS_BIND_ONLY` | bool | Emergency opt-out that prevents forensics from forcing command-list hooks |
| `UEVR_STEREO_FORENSICS_DISABLE_COMMAND_LIST_HOOKS` | bool | Alias emergency opt-out for command-list hooks |
| `UEVR_STEREO_FORENSICS_BIND_ONLY_RECORD_PSO` | bool | In bind-only mode, explicitly keep `set_pso` events; off by default to avoid useless floods |
| `UEVR_STEREO_FORENSICS_MAX_DESCRIPTORS` | int | Descriptor side-table cap (default 262144) |
| `UEVR_STEREO_FORENSICS_MAX_WRITER_HISTORY` | int | View/resource writer history cap for lineage DAG output (default 100000) |
| `UEVR_STEREO_EXPERIMENTS` | bool | Enable live experiment rules inside Stereo Forensics |
| `UEVR_STEREO_EXPERIMENTS_FILE` | path | JSON v2 rule file; runtime executes skip probes, color override probes, and supported mutation actions: CBV swap, descriptor swap, forced SRV array slice |
| `UEVR_STEREO_FORENSICS_DB` | path | Default SQLite knowledge DB for Python tools (default `C:\tmp\uevr_forensics\forensics.db`) |

## Magic Ink (live PS skip)

| Env Var | Type | Purpose |
|---|---|---|
| `UEVR_SN2_MAGIC_INK_SKIP_PS_CRCS` | csv | Static skip list |
| `UEVR_SN2_MAGIC_INK_SKIP_FILE` | path | **Live-reload skip file (preferred)** |
| `UEVR_SN2_MAGIC_INK_EYE` | left/right/both | Eye filter for skip |

## Eye Screenshot

| Env Var | Type | Purpose |
|---|---|---|
| `UEVR_SN2_EYE_SCREENSHOT_TRIGGER_FILE` | path | File-trigger path |
| `UEVR_SN2_EYE_SCREENSHOT_OUTPUT_DIR` | path | PPM output dir |

## Right CB Synth (donor snapshot)

| Env Var | Type | Default | Purpose |
|---|---|---|---|
| `UEVR_SN2_RIGHT_CB_SYNTH` | bool | `0` | Enable |
| `UEVR_SN2_RIGHT_CB_SYNTH_DONOR` | hex CRC | `0x4d44ce74` | Donor PS CRC |
| `UEVR_SN2_RIGHT_CB_SYNTH_ROOT` | int | `3` | Donor's View CB root |
| `UEVR_SN2_RIGHT_CB_SYNTH_SIZE` | int | `4096` | Snapshot buffer size |

## Mirror infrastructure

| Env Var | Type | Purpose |
|---|---|---|
| `UEVR_SN2_UWE_FOG_MIRROR` | bool | Enable mirror allocation |
| `UEVR_SN2_UWE_FOG_MIRROR_LOG` | bool | Verbose mirror logging |
| `UEVR_SN2_ALIAS_SRV_REDIRECT` | bool | Old SRV redirect at consumer (compute path) |
| `UEVR_SN2_ALIAS_SRV_REDIRECT_LOG` | bool | Verbose alias-redirect logging |
| `UEVR_SN2_DUPLICATE_UWE_FOG_COMPUTE_RIGHT` | bool | Dup fog compute dispatches |

## Sky atmosphere / pre-built skip lists

These are diagnostic-only visual masks. They are ignored unless
`UEVR_SN2_ALLOW_SKYATMOS_DIAGNOSTICS=1` is also set. Keep the allow flag off for
real right-eye fog validation.

| Env Var | Type | Purpose |
|---|---|---|
| `UEVR_SN2_ALLOW_SKYATMOS_DIAGNOSTICS` | bool | Required second gate for any sky/atmos mutation below |
| `UEVR_SN2_SKYATMOS_SKIP_RIGHT` | bool | Skip 5 hardcoded sky-atmos CRCs on right eye, only when allow gate is set |
| `UEVR_SN2_SKYATMOS_CB_REDIRECT` | bool | Sky atmos CB redirect (older approach), only when allow gate is set |
| `UEVR_SN2_RIGHT_SKIP_CRCS` | csv | Additional right-eye CRC skip list, only when allow gate is set |
| `UEVR_SN2_SKY_ATMOS_INLINE_FIX` | bool | Old inline sky-atmos CB fix, only when allow gate is set |
| `UEVR_SN2_SKYATMOS_RIGHT_TABLE_SWAP` | bool | Old sky-atmos descriptor table swap/copy, only when allow gate is set |
| `UEVR_SN2_FOG_SRV_REDIRECT` | int | Older fog SRV redirect mode (1=enable) |
| `UEVR_SN2_FOG_SRV_REDIRECT_ROOT` | int | Root param to redirect |
| `UEVR_SN2_FOG_SRV_REDIRECT_SLOTS` | csv | Slot indices |

## Material name hook

| Env Var | Type | Purpose |
|---|---|---|
| `UEVR_SN2_MATERIAL_HOOK` | bool | Install `FBasePassMeshProcessor::AddMeshBatch` hook at RVA 0x2644A70 |
| `UEVR_SN2_ADDMESH_TRACE` | bool | Install/use the `FBasePassMeshProcessor::AddMeshBatch` hook as a targeted trace for the `0x13B00F0C` mesh shape before `TryAddMeshBatch` |
| `UEVR_SN2_ADDMESH_TRACE_MAX` | int | Maximum `SN2-AddMesh` rows when not target-shape-only (default 512) |
| `UEVR_SN2_ADDMESH_TRACE_TARGET_SHAPE_ONLY` | bool | Only log AddMesh rows whose `FMeshBatch` exposes 76608 indices or 25536 primitives; defaults to `1` |
| `UEVR_SN2_ADDMESH_TRACE_STACK` | bool | For target-shape `SN2-AddMesh` rows, also log a capped CPU stack with game-exe RVAs so the upstream mesh submission path can be resolved offline |
| `UEVR_SN2_ADDMESH_TRACE_STACK_MAX` | int | Maximum `SN2-AddMeshStack` rows to emit (default 32) |
| `UEVR_SN2_ADDMESH_FORCE_RIGHT_13B` | bool | Diagnostic only: when the left view submits the `0x13B00F0C` mesh shape, log the recent secondary BasePass processor that would be used for a re-add. Direct execution is disabled unless `UEVR_SN2_ADDMESH_FORCE_RIGHT_EXECUTE_UNSAFE=1` because same-thread and cross-thread re-adds both crashed in OpenXR tests on 2026-05-28 |
| `UEVR_SN2_ADDMESH_FORCE_RIGHT_EXECUTE_UNSAFE` | bool | Actually execute the direct `FBasePassMeshProcessor::AddMeshBatch` re-call. Unsafe by design; the 2026-05-28 same-thread run crashed at SN2 RVA `0x44d9a7b`, and the cross-thread/global run crashed at RVA `0x262c95f`. Keep off outside crash-repro diagnostics |
| `UEVR_SN2_ADDMESH_FORCE_RIGHT_USE_GLOBAL` | bool | Diagnostic companion for `UEVR_SN2_ADDMESH_FORCE_RIGHT_13B`: allow the candidate to use the most recent secondary BasePass processor observed on any worker thread when the same-thread cache is empty. Off by default because processor lifetime is more fragile across threads |
| `UEVR_SN2_ADDMESH_FORCE_RIGHT_MAX_AGE` | int | Maximum AddMesh call age for the cached secondary processor used by `UEVR_SN2_ADDMESH_FORCE_RIGHT_13B` (default 8192) |
| `UEVR_SN2_ADDMESH_FORCE_RIGHT_DELAY_MS` | int | Delay before the experimental re-add force can fire; avoids UEVR's D3D12 init race (default 25000) |
| `UEVR_SN2_ADDMESH_FORCE_RIGHT_MAX_TOTAL` | int | Total number of experimental re-add calls allowed before auto-suppressing (default 16, 0 = unlimited) |
| `UEVR_SN2_GENDYN_TRACE` | bool | Install `GenerateDynamicMeshDrawCommands` hook at RVA 0x2A9E010 and log per-view dynamic mesh command-generation inputs; used to prove whether the target `0x13B00F0C` mesh is absent before BasePass `AddMeshBatch` or present but masked out by `FMeshPassMask` |
| `UEVR_SN2_GENDYN_TRACE_PASS` | int | Mesh pass filter for `SN2-GenDyn` rows (default `2`, BasePass; `-1` logs all passes) |
| `UEVR_SN2_GENDYN_TRACE_MAX` | int | Maximum non-target `SN2-GenDyn` rows to log (default 256); target-shape hits are always logged |
| `UEVR_SN2_GENDYN_TRACE_MAX_ELEMS` | int | Maximum dynamic mesh array elements/windows to scan per command-generation call (default 512) |
| `UEVR_SN2_GENDYN_TRACE_SCAN_BYTES_PER_ELEM` | int | `FMeshBatchAndRelevance` element stride used by the dynamic mesh scanner (default 24 / `0x18`: mesh pointer, primitive pointer, relevance flags). Keep narrow; broad scans can read neighboring eye arrays and create false positives |
| `UEVR_SN2_TARGET_MESH_PASS` | int | Mesh pass used by legacy GenerateDynamicMeshDrawCommands duplicate diagnostics. Default `2` (BasePass). SN2's shifted pass table has `SingleLayerWaterPass=6` and `TranslucencyAfterDOF=17` |
| `UEVR_SN2_VIEWCOMMANDS_TRACE` | bool | Install a midhook in `FVisibilityTaskData::FinishGatherDynamicMeshElements` immediately before mesh-pass setup is launched, and dump the off-`FViewInfo` `ViewCommandsPerView` payload for passes `0/2/5/6/17` |
| `UEVR_SN2_VIEWCOMMANDS_TRACE_MAX` | int | Maximum `FinishGatherDynamicMeshElements` callbacks to dump (default 48); each callback emits primary/secondary rows for passes `0/2/5/6/17` |
| `UEVR_SN2_MESHCMD_BRUTE_SCAN` | bool | Install/use the same pre-setup `ViewCommands` midhook and brute-scan the configured target pass' `ViewCommands.MeshCommands` entries for pointers leading to the `0x13B00F0C` target mesh shape. Used to prove the left BasePass command array contains the target mesh while the paired right BasePass array does not, even when the exact `FVisibleMeshDrawCommand` layout is still unknown |
| `UEVR_SN2_MESHCMD_BRUTE_SCAN_MAX` | int | Maximum `SN2-MeshCmdBrute` rows to emit (default `24`) |
| `UEVR_SN2_MESHCMD_BRUTE_SCAN_MAX_ELEMS` | int | Maximum elements per `MeshCommands` array to brute-scan (default `128`). Keep low; this intentionally probes unknown command layouts and is for short identity captures only |
| `UEVR_SN2_MESHCMD_COPY_RIGHT_13B` | bool | Experimental diagnostic fix path: before mesh-pass setup, find the cached `0x13B00F0C` target mesh row in the primary configured pass' `ViewCommands.MeshCommands` and replace the last secondary row with that primary row when the secondary does not already contain it. Default pass is controlled by `UEVR_SN2_UNDERWATER_TARGET_MESH_PASS` (`2`, BasePass). This preserves secondary view matrices/render targets but is still a row-replacement diagnostic, not the final allocator-aware command insertion |
| `UEVR_SN2_VIEWCOMMANDS_COPY_RIGHT_13B` | bool | Experimental fix path: when the configured underwater target pass primary `ViewCommands` contains the `0x13B00F0C` static-mesh request and secondary does not, replace one existing secondary build-request entry with that target request before mesh-pass setup. Default target pass is `2` because current AddMesh/D3D12 traces point at Translucent BasePass; use pass `6` for the shifted SN2 `SingleLayerWaterPass` if follow-up identity work proves SLW owns the final draw. Pass `17` is `TranslucencyAfterDOF` in SN2 and should only be used to reproduce older wrong-bucket diagnostics |
| `UEVR_SN2_UNDERWATER_TARGET_MESH_PASS` | int | Mesh-pass bucket used by the `0x13B00F0C` FViewInfo/ViewCommands copy experiments. Default `2` (BasePass). SN2's shifted enum has `SingleLayerWaterPass=6` and `TranslucencyAfterDOF=17` |
| `UEVR_SN2_ALIAS_SLW_PASS_PTR` | bool | Legacy one-line pass-pointer diagnostic. In the pre-mesh-pass setup hook, alias `ParallelMeshDrawCommandPasses[slot]` from primary to secondary. Defaults to slot `6` for SN2's shifted `SingleLayerWaterPass`; override with `UEVR_SN2_ALIAS_PMDCP_SLOT`. This is not a final parallax-safe fix |
| `UEVR_SN2_ALIAS_PMDCP_SLOT` | int | Slot used by `UEVR_SN2_ALIAS_SLW_PASS_PTR`. Default `6`; set explicitly for old slot-5 or pass-17 diagnostics |
| `UEVR_SN2_VIEWCOMMANDS_COPY_DELAY_MS` | int | Startup guard for `UEVR_SN2_VIEWCOMMANDS_COPY_RIGHT_13B` / `UEVR_SN2_FVIEWINFO_BUNDLE_COPY_RIGHT_13B`; copy hooks only begin after this many milliseconds from process start (default `25000`) so they do not fire during UEVR/framework initialization |
| `UEVR_SN2_FVIEWINFO_BUNDLE_COPY_RIGHT_13B` | bool | Experimental diagnostic companion to `UEVR_SN2_VIEWCOMMANDS_COPY_RIGHT_13B`: only inside a pre-setup `ViewCommands` task whose primary configured target-pass build requests still contain the cached `0x13B00F0C` static-mesh request, set the target primitive/static mesh's secondary visibility bits, OR the target `PrimitiveViewRelevanceMap` / LOD entries, and OR the `bHasSingleLayerWaterMaterial` bit from primary to secondary. It validates the primitive index against the active primary view before writing, never clears secondary bits, deliberately does not raw-copy owned `TArray` / bit-array headers, and leaves view matrices/ViewState/View UB/PrevViewInfo untouched |
| `UEVR_SN2_TARGET_PRIMITIVE_INDEX_OVERRIDE` | int | Diagnostic override for the `0x13B00F0C` target primitive index used by `UEVR_SN2_FVIEWINFO_BUNDLE_COPY_RIGHT_13B`. Useful while IDA is still pinning `FPrimitiveSceneInfo::PackedIndex`; current plausible runtime values include `497`, while the old heuristic could choose low incidental bit indices like `62` |
| `UEVR_SN2_TARGET_IDENTITY_SCENE_SCAN_BYTES` | int/hex | Target-identity scan breadth for locating `FScene::Primitives` from the captured `FPrimitiveSceneInfo` candidate. Defaults to `0x8000`; only active when `UEVR_SN2_TARGET_IDENTITY_SCAN=1` |
| `UEVR_SN2_TARGET_IDENTITY_SCENE_SCAN_MAX_ELEMS` | int | Max elements to scan in candidate scene pointer arrays while resolving the real `PackedIndex`. Defaults to `200000`; lower this only if target identity logging becomes too expensive |
| `UEVR_SN2_STABILITY_STREAK` | int | Scene-assembly guard for `UEVR_SN2_VIEWCOMMANDS_COPY_RIGHT_13B` and `UEVR_SN2_FVIEWINFO_BUNDLE_COPY_RIGHT_13B`. The hooks re-resolve the target through live `FScene::Primitives` and wait for the same `(target_psi, PackedIndex)` for this many callbacks before mutating secondary state. Default `60`; set `0` only for controlled startup-race diagnostics |
| `UEVR_SN2_STABILITY_QUARANTINE` | int | Optional extra guard requiring this many callbacks since the last `PackedIndex`/scene-count instability before copies can fire. Default `0` |
| `UEVR_SN2_STABILITY_REQUIRE_SCENE_COUNT` | bool | Include `FScene::Primitives.Num` in the stability key. Defaults off because the menu scene streams small primitive-count changes while the target `PackedIndex` stays stable |
| `UEVR_SN2_FVIEWINFO_INDEXED_ARRAY_SCAN` | bool | Diagnostic scanner emitted from the FViewInfo bundle-copy hook. For the stable target index, scans `FViewInfo` TArray headers in `0x1B80..0x2110` and logs nonzero primary/secondary entries so missing relevance/LOD offsets can be found without another IDA pass |
| `UEVR_SN2_FVIEWINFO_INDEXED_ARRAY_SCAN_MAX` | int | Maximum `SN2-FViewIndexedArrayScan` rows (default `64`) |
| `UEVR_SN2_NUMVISIBLE_OFFSET_OVERRIDE` | int/hex | Diagnostic override for `FViewInfo::NumVisibleDynamicMeshElements[37]` base offset used by `UEVR_SN2_VIEWCOMMANDS_COPY_RIGHT_13B`. Leave unset by default; the RE-pinned base is `0x1D88` |
| `UEVR_SN2_FVIEWINFO_DYNAMIC_RANGE_COPY_RIGHT_13B` | bool | Extra experimental diagnostic layer: also copy the RE-mapped `FViewInfo` dynamic mesh bookkeeping band `0x1D70..0x2070` from primary to secondary before mesh-pass setup, while deliberately stopping before `ParallelMeshDrawCommandPasses` at `0x2070`. Keep this off by default: the 2026-05-28 OpenXR run with this broad range enabled paired the draw but crashed during startup, so future work should copy only pinned scalar fields instead of the whole band |
| `UEVR_SN2_TRYADD_TRACE` | bool | Install `FBasePassMeshProcessor::TryAddMeshBatch` entry hook at RVA 0x2687030 and log per-view primitive/material decisions |
| `UEVR_SN2_TRYADD_TRACE_MAX` | int | Maximum `SN2-TryAdd` rows to log before suppressing non-target-shape rows (default 512) |
| `UEVR_SN2_TRYADD_TRACE_TARGET_SHAPE_ONLY` | bool | Only log TryAdd rows whose `FMeshBatch`/first elements contain 76608 indices or 25536 primitives, matching the `0x13B00F0C` teal draw shape |

## Overlay UI

| Env Var | Type | Purpose |
|---|---|---|
| `UEVR_SN2_OVERLAY_UI` | bool | Enable ImGui debug overlay |

## VSM patch

| Env Var | Type | Purpose |
|---|---|---|
| `UEVR_SN2_VSM_UB_CLAMP_PATCH` | bool | Apply 3-byte VSM UB clamp patch at RVA 0x2B50544 |

## UWE ImGui diagnostics unlock

| Env Var | Type | Purpose |
|---|---|---|
| `UEVR_SN2_FORCE_DEV_BUILD` | bool | Patch the shipping `IsDevelopmentBuild()` stub at RVA 0x12924A0 (`32 C0`→`B0 01`, mov al,1=true) so the UWE ImGui diagnostics open-by-name gate passes and diagnostic windows (Rendering/Lighting/RenderResource) can be checked open in-game. Off by default; byte-verified before patching. |

## Live-reload behavior summary

These env vars support file-based live-reload (no game restart):
- `UEVR_SN2_MAGIC_INK_SKIP_FILE` — polled every 60 frames

These env vars are loaded ONCE at startup (require restart to change):
- `UEVR_SN2_DUP_CONFIG_FILE`
- All other env vars

## Diagnostic env var "warn_enabled" list

These env vars print a warning log line at startup confirming they're enabled (look for `[WARN]` in log.txt):
- `UEVR_SHADER_HUNTER_KILL_LEFT_EYE`
- `UEVR_SHADER_HUNTER_KILL_RIGHT_EYE`
- `UEVR_SN2_FORCE_LEFT_CB0`
- `UEVR_SN2_FOG_SRV_REDIRECT`
- `UEVR_SN2_PSO3069_CB_REDIRECT`
- `UEVR_SN2_WATER_CHAIN_REDIRECT`
- `UEVR_SN2_WATER_CHAIN_CB_REDIRECT`
- `UEVR_SN2_RIGHT_ARG_SUBSTITUTE`
- `UEVR_SN2_SKYATMOS_SKIP_RIGHT`
- `UEVR_SN2_SKYATMOS_CB_REDIRECT`
- `UEVR_SN2_SKYATMOS_CB_REDIRECT_ROOTS`
- `UEVR_SN2_SKYATMOS_RIGHT_TABLE_SWAP`
- `UEVR_SN2_UPSTREAM_SKIP_CRCS`
- `UEVR_SN2_UPSTREAM_SKIP_LEFT_CRCS`
- `UEVR_SN2_UPSTREAM_SKIP_RIGHT_CRCS`
- `UEVR_SN2_UPSTREAM_SKIP_UNKNOWN_CRCS`
- `UEVR_SUBNAUTICA2_MIRROR_LEFT_TO_RIGHT_EYE`

(Defined in `D3D12Hook.cpp` around line 2537.)
