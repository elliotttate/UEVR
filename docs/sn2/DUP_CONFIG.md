# Dup Config (`UEVR_SN2_DUP_CONFIG_FILE`)

The dup_cfg JSON file controls per-PSO right-eye draw duplication behavior.

## Loading

- Env: `UEVR_SN2_DUP_CONFIG_FILE=<absolute_path>`
- Loaded ONCE at game startup via `Sn2DupConfigFile::load`
- **NOT live-reloaded** (yet) — changing the JSON requires game restart
- Parsed via nlohmann::json

## File format

```json
{
  "description": "Optional description of this config",
  "entries": {
    "0x<ps_crc>": {
      "mode": "<DupMode>",
      "view_cb_roots": [<root1>, <root2>, ...],
      "delta": <int64>,
      "note": "Optional notes"
    },
    ...
  }
}
```

### Entry fields

- `mode` (required): One of `"viewport_shift"`, `"view_cb_only"`, `"skip"`, `"synthesize_right_cb"`
- `view_cb_roots`: Array of root param indices holding the View CB. Used for CBV swap during dup.
- `delta`: For `view_cb_only` mode. The pool-relative byte offset from LEFT View CB to RIGHT. Computed by `Sn2BindingAnalyzer` per PSO.
- `note`: Free-form annotation.

## DupMode reference

### `viewport_shift` (default for unconfigured PSOs)

The dup re-issues the draw with:
- Viewport shifted from `(0, 0, W/2, H)` to `(W/2, 0, W/2, H)` (RIGHT half of RT)
- View CB swapped to right-eye via pool delta
- Scissor rect updated similarly

**Use case**: PSes that write to a split-screen RT (the engine's default UE5 behavior when -emulatestereo). Most basepass / overlay PSes.

**Costs**: One extra DrawIndexedInstanced per matching PSO per frame.

### `view_cb_only`

The dup re-issues the draw with:
- View CB swapped to right-eye via pool delta
- **No viewport shift** (full-screen RT or per-eye RT model)

**Use case**: PSes that write to a per-eye-owned RT (not split). E.g., 50 PSes in the restricted dup_cfg with delta=-10240.

### `skip`

The dup is a no-op for this PSO. The original LEFT draw still executes; we don't re-issue.

**Use case**: 
- PSOs that don't need stereo handling (HUD, post-process)
- PSOs that crash when duplicated (View CB in different pool)

### `synthesize_right_cb` (NEW this session)

The dup re-issues the draw with:
- View CB swapped to the **synth donor's right-eye CB GPU_VA** (not a pool delta)
- **No viewport shift**
- RTV slots redirected to **mirror RTVs** (if the resource has a mirror)

**Use case**: LEFT-only producers (like VoxelizePS) that don't have a native right-eye View CB. We synthesize one via donor snapshot (`Sn2RightCbSynth`).

**Requires**: 
- `UEVR_SN2_RIGHT_CB_SYNTH=1` + `_DONOR=0x...` + `_ROOT=N` env vars
- `UEVR_SN2_UWE_FOG_MIRROR=1` env var (for RTV redirect)

## Bundled configs

### `sn2_dup_cfg_measured.json`
- 74 view_cb_only entries with measured deltas
- 144 skip entries
- Generated from binding analyzer data via `generate_dup_cfg_from_analyzer.py`
- Includes some PSes that cause crashes — DO NOT USE blindly

### `sn2_dup_cfg_restricted.json` ⭐ STABLE BASELINE
- 50 view_cb_only entries (only -10240 delta — validated safe)
- 143 skip entries
- Used by `launch_restricted_cfg.ps1`
- LEFT eye correct, RIGHT eye original bug (washed-out sky)

### `sn2_dup_cfg_voxelize_synth.json`
- Restricted cfg + VoxelizePS (`0x9D14FCF0`) configured
- VoxelizePS toggles between `skip` and `synthesize_right_cb` mode for testing
- Used by `launch_phase_u.ps1`

### `sn2_dup_cfg_synth_minimal.json`
- Just `0x13b00f0c` with `synthesize_right_cb` mode at roots [4, 6]
- Earlier test; superseded by voxelize_synth

## How to generate a new config

### From binding analyzer JSON

```bash
# 1. Run game with binding analyzer enabled
$env:UEVR_SN2_BINDING_ANALYZER_JSON = 'C:\tmp\sn2_analyzer.json'
powershell -File launch_restricted_cfg.ps1
# ...play for ~60s...

# 2. Generate dup cfg with safe filters
python generate_dup_cfg_from_analyzer.py \
    --in C:\tmp\sn2_analyzer.json \
    --out C:\tmp\new_dup_cfg.json \
    --max-delta-abs 16384 \
    --require-paired

# 3. Inspect, edit, point launch script at it
```

### Manually for a single PSO test

```python
import json
cfg = {
    "description": "test synth on VoxelizePS",
    "entries": {
        "0x9d14fcf0": {
            "mode": "synthesize_right_cb",
            "view_cb_roots": [3],
            "note": "test only"
        }
    }
}
with open('test_cfg.json', 'w') as f:
    json.dump(cfg, f, indent=2)
```

## Future: live-reload

The `Sn2MagicInk` pattern (poll file mtime every 60 frames) is the model. To extend to dup_cfg:

1. Move `Sn2DupConfigFile::load` into a function called every 60 frames
2. Compare file mtime; on change, atomically replace the entries map
3. Lock around the map for the read path

Estimated ~50 LOC. Massive iteration speedup (no more restart per config edit).
