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

## Overlay UI

| Env Var | Type | Purpose |
|---|---|---|
| `UEVR_SN2_OVERLAY_UI` | bool | Enable ImGui debug overlay |

## VSM patch

| Env Var | Type | Purpose |
|---|---|---|
| `UEVR_SN2_VSM_UB_CLAMP_PATCH` | bool | Apply 3-byte VSM UB clamp patch at RVA 0x2B50544 |

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
