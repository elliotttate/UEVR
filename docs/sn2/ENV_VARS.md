# Environment Variable Reference

Every UEVR_SN2_* env var, what it does, where it's read.

## Required for any SN2 work

| Env Var | Type | Default | Purpose |
|---|---|---|---|
| `UEVR_ENABLE_D3D12_DIAGNOSTIC_COMMAND_LIST_HOOKS` | bool | `0` | Required for most Sn2 modules (state tracking) |
| `UEVR_SN2_DUP_CONFIG_FILE` | path | (none) | Path to dup_cfg JSON |
| `UEVR_SN2_DUPLICATE_SLW_BASEPASS_RIGHT` | bool | `0` | Enable the dup function |

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

| Env Var | Type | Purpose |
|---|---|---|
| `UEVR_SN2_SKYATMOS_SKIP_RIGHT` | bool | Skip 5 hardcoded sky-atmos CRCs on right eye |
| `UEVR_SN2_SKYATMOS_CB_REDIRECT` | bool | Sky atmos CB redirect (older approach) |
| `UEVR_SN2_RIGHT_SKIP_CRCS` | csv | Additional right-eye CRC skip list |
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
