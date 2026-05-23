# Python Analysis Tools

All tools live in `E:\Github\Subnautica 2\moddingkit\tools\`. They process outputs from UEVR's C++ modules and emit analysis artifacts.

## Index

### Core
- [analyze_60s_diag.py](#analyze_60s_diagpy)
- [generate_dup_cfg_from_analyzer.py](#generate_dup_cfg_from_analyzerpy)
- [diff_cb_dumps.py](#diff_cb_dumpspy)
- [diff_rt_dumps.py](#diff_rt_dumpspy)
- [decode_view_cb.py](#decode_view_cbpy)
- [sn2_auto_bisect.py](#sn2_auto_bisectpy)

### Phase X — RenderDoc-parity
- `ppm_to_png.py` — bulk PPM→PNG converter (so screenshots are inline-viewable)
- `dxil_disasm.py` — bulk DXBC disassembly via dxc
- `diff_rt_regions.py` — region-aware visual diff with semantic classification + anomaly highlighting
- `sn2_pixel_history_query.py` — Sn2PixelHistory query driver
- `sn2_gpu_counters_top.py` — sort GPU counter dump and display top N
- `mesh_dump_inspect.py` — list mesh dumps + detail

### Phase Y — Final Phase W completion
- `eye_pair_correlator.py` — pair LEFT and RIGHT eye PSO observations, identify View CB candidates + LEFT-only producers
- `sn2_fix_db.py` — SQLite-backed persistent fix database (init/seed/list/show/add/update/emit-launch)
- `frame_capture_inspect.py` — analyze events.json from Sn2FrameCppExport

### Phase Z — RenderDoc-gap closers
- `cb_reflect_decode.py` — shader-reflection-based CB decoder (works for ANY shader, not just hardcoded UE5 ViewUniform)
- `uevr_rd_bridge.py` — UEVR↔RD bridge: read capture sidecar, emit RD override plan

### Phase BB-GG — capture & fix CLIs
- `sn2_capture_now.py` — one-command UEVR frame capture trigger + wait + summary
- `sn2_capture_inspect.py` — unified view of a capture bundle (cross-referenced)
- `sn2_fix_rules.py` — CLI manager for hot-reloaded fix rules

### Stereo Forensics
- `E:\Github\UEVRJ\tools\stereo_forensics_query.py` — inspect `UEVR_STEREO_FORENSICS` session bundles by issue, event, shader, lineage slot, or alias group
- `E:\Github\UEVRJ\tools\stereo_forensics_db.py` — persistent SQLite knowledge DB for captures, event pairs, lineage paths, resource lifetimes, suspects, findings, evidence, experiments, shader roles, and fix rules
- `E:\Github\UEVRJ\tools\stereo_forensics_experiment.py` — emit v2 experiment rules and score left/right PPM or C-API sample JSON ROI deltas
- `E:\Github\UEVRJ\tools\stereo_forensics_run_experiments.py` — generate ranked probe/mutation rule candidates from DB suspects
- `E:\Github\UEVRJ\tools\stereo_forensics_ab_loop.py` — run closed-loop baseline/trial screenshot experiments against hot-reloaded rule files
- `E:\Github\UEVRJ\tools\stereo_forensics_compile_rule.py` — compile a captured event into a durable v2 rule skeleton, including descriptor-slot mutations such as `neutralize_texture`
- `E:\Github\UEVRJ\tools\stereo_forensics_shader_semantics.py` — build lightweight DXBC/DXIL semantic summaries, including DXBC RDEF/signature/token facts and richer DXIL disassembly when `dxil-patch` is available, then import shader roles into the DB

---

## analyze_60s_diag.py

**Purpose**: Parse the UEVR log file for a 60-second diagnostic run and emit a structured report of missing-passes, root-sig families, and dup events.

### Inputs
- UEVR log: `C:\Users\ellio\AppData\Roaming\UnrealVRMod\Subnautica2-Win64-Shipping\log.txt`

### Outputs
- Missing-pass CSV (which CSes fire LEFT-only)
- Root signature family histogram
- Dup events count per PSO

### Usage
```bash
python analyze_60s_diag.py --log <path> --out <dir>
```

### Used during
- Phase H: 60s live diagnostics
- Initial scene exploration to identify LEFT-only producers

---

## generate_dup_cfg_from_analyzer.py

**Purpose**: Convert binding analyzer JSON into a safe `dup_cfg` with measured per-PSO View CB deltas.

### Inputs
- `<analyzer.json>` from `UEVR_SN2_BINDING_ANALYZER_JSON`

### Outputs
- `sn2_dup_cfg_measured.json` — view_cb_only entries for every PSO with paired L/R samples

### Filters
- `--max-delta-abs 16384` — exclude PSOs with delta exceeding this magnitude
- `--require-paired` — only include PSOs with both LEFT and RIGHT samples seen

### Usage
```bash
python generate_dup_cfg_from_analyzer.py \
    --in sn2_analyzer.json \
    --out sn2_dup_cfg_measured.json \
    --max-delta-abs 16384 \
    --require-paired
```

### Outputs format
```json
{
  "entries": {
    "0x4d44ce74": {
      "mode": "view_cb_only",
      "view_cb_roots": [3],
      "delta": -10240,
      "samples_l": 14901,
      "samples_r": 4963
    }
  }
}
```

---

## diff_cb_dumps.py

**Purpose**: Byte-diff CBV dumps to find per-eye divergent slots.

### Inputs
- Multiple `G_0x<crc>_root<N>_eye<0|1>_seq<NNNN>.bin` files from `Sn2CbDumper`

### Outputs
- `cb_diff.json` — byte-offset-keyed divergent-slot analysis

### Output shape
```json
{
  "pso_0x4d44ce74_root3": {
    "total_slots": 256,
    "divergent_slots": 80,
    "divergent_offsets": [0, 16, 32, ..., 944, 960, 1264, ...],
    "samples_per_eye": {"L": 8, "R": 8}
  }
}
```

### Usage
```bash
python diff_cb_dumps.py --dir C:\tmp\cb_dumps --out cb_diff.json
```

### Used during
- Phase L: collect analyzer data
- Identifying the 80/256 divergent slots (used for CB synthesis design)

---

## diff_rt_dumps.py

**Purpose**: Pair LEFT and RIGHT scene-color PPM dumps and emit per-pixel diff heatmaps.

### Inputs
- `RTDIFF_L_<seq>.ppm` and `RTDIFF_R_<seq>.ppm` from `Sn2RtDiff`

### Outputs
- `diff_seq<NNNN>.ppm` — red-heatmap PPM (red intensity = |L-R| absolute difference)
- `diff_summary.json` — per-sequence stats (mean_diff, max_diff, differing_pixels, 8×8 region grid)

### Usage
```bash
python diff_rt_dumps.py \
    --in "C:\Users\ellio\AppData\Roaming\UnrealVRMod\Subnautica2-Win64-Shipping\rt_snapshots"
```

### Output PPM format
- Same dimensions as input L/R
- Red channel: `min(255, 2 * (|R_diff| + |G_diff| + |B_diff|))`
- Green and Blue: zero

### Region analysis
Splits image into 8×8 grid, reports mean diff per region. Helps spot localized stereo issues vs uniform tint differences.

---

## decode_view_cb.py

**Purpose**: Decode UE5 `ViewUniformShaderParameters` byte dumps into a symbolic field-by-field view with per-eye diff.

### Inputs
- Two `.bin` files (LEFT and RIGHT CBV dumps from `Sn2CbDumper`)

### Outputs
- Stdout table with Field name | Offset | LEFT value | RIGHT value | Δ

### Known field offsets (UE5 5.6)
- `WorldToClip[0..3]` @ 0..48
- `ClipToWorld[0..3]` @ 64..112
- `TranslatedWorldToClip[0..3]` @ 128..176
- `ViewToClip[0..3]` @ 192..240
- `WorldCameraOrigin` @ 944
- `TranslatedWorldCameraOrigin` @ 960
- `StereoViewIndex_or_EyeOffset` @ 1264 (the famous 0.25/0.75 eye marker)
- `ViewSizeAndInvSize` @ 1216
- `FieldOfViewWideAngles` @ 1280

### Usage
```bash
python decode_view_cb.py \
    G_0x9d14fcf0_root3_eye0_seq0001.bin \
    G_0x4d44ce74_root3_eye0_seq0001.bin
```

### `--diff-only` flag
Only prints fields where LEFT != RIGHT. Useful for spotting per-eye divergences in observed data.

### Used during
- Identifying what fields donor must populate for VoxelizePS to render correctly
- Validating that synth CB has correct matrices

---

## sn2_auto_bisect.py

**Purpose**: Automated PSO bisection driver. Given a list of candidate CRCs, iteratively halves the skip set, captures eye PPMs after each test, computes LEFT-eye diff vs baseline, converges on the artifact-causing PSO(s).

### Inputs
- `--crcs <csv>` — candidate CRC list
- `--eye <left|right>` — which eye to monitor for change (default: left)
- `--output-dir <path>` — output dir for baseline/diffs/result.json

### Prerequisites
1. Game must be running with Magic Ink + Eye Screenshot env vars enabled
2. Use `launch_phase_u.ps1` or similar — must include:
   - `UEVR_SN2_MAGIC_INK_SKIP_FILE=C:\tmp\ink_skip.txt`
   - `UEVR_SN2_EYE_SCREENSHOT_TRIGGER_FILE=C:\tmp\uevr_shot_req.txt`
   - `UEVR_SN2_EYE_SCREENSHOT_OUTPUT_DIR=C:\tmp\uevr_screenshots`
   - `UEVR_ENABLE_D3D12_DIAGNOSTIC_COMMAND_LIST_HOOKS=1`

### Algorithm
```
1. Capture baseline (no skips) eye PPMs
2. Confirm full skip set causes measurable change (L2 > threshold)
3. Halve set; test each half
4. Recurse on whichever half causes change
5. Stop when |candidates| == 1, or when neither half alone causes change
```

### Usage
```bash
python sn2_auto_bisect.py \
    --crcs 0x009f8918,0x03f0d897,...,0x9d14fcf0 \
    --eye left \
    --output-dir C:\tmp\bisect_run
```

### Output
- `<output-dir>/baseline_left.ppm`, `baseline_right.ppm`
- `<output-dir>/result.json`:
  ```json
  {"converged": ["0x37558de4"], "input": [...]}
  ```

### Limitations
- Threshold (5.0 L2) is heuristic — too low → noise causes false converge; too high → real bugs missed
- Doesn't handle cases where multiple PSOs together (but not individually) cause the artifact
- Requires the game state to be stable across iterations (no progress / time-of-day changes)

### What it replaces
Manual ink_skip iteration: write CRC list, wait, screenshot, eyeball diff, halve, repeat. Auto-bisect does this in 1-2 minutes vs 30+ for a 50-PSO set.

---

## eye_pair_correlator.py

**Purpose**: Read binding analyzer JSON, pair LEFT-eye observations with RIGHT-eye observations per (PSO, root). Identifies View CB candidates and LEFT-only producers.

### Inputs
- `--analyzer <path>` — binding analyzer JSON (from `UEVR_SN2_BINDING_ANALYZER_JSON`)
- `--out <path>` — output JSON (default `eye_pairs.json`)
- `--min-samples <N>` — min samples per side to pair (default 10)

### Output
- `eye_pairs.json` with `pairs[]`, `unpaired_left[]`, `unpaired_right[]`
- Stdout: top 20 View CB candidates + LEFT-only producers

### Used for
- Quickly finding View CB candidates without manually scanning analyzer JSON
- Identifying LEFT-only producers that need synth or skip in dup_cfg

### Example output
```
=== Top View CB candidates (20 of 47) ===
PS CRC        Root  L samples  R samples       CB Δ
------------------------------------------------------------
0x4d44ce74      3      14901       4963       -10240
0xf7ddfe06      4       1834       1820        -8192
...

=== LEFT-only producers (need synth or skip) ===
  0x9d14fcf0   root=3   L=109230
  0x009f8918   root=4   L=2348
```

---

## sn2_fix_db.py

**Purpose**: Persistent SQLite-backed catalog of validated fix rules. Provides curated DB with provenance + status tracking.

### Default DB path
`E:\Github\Subnautica 2\moddingkit\artifacts\sn2_fix_db.sqlite`

### Subcommands

#### `init`
Create empty DB (idempotent).

#### `seed`
Load 3 pre-known fixes (restricted-dup-cfg, skyatmos-skip-right, fog-srv-redirect-partial).

#### `list [--target sn2]`
Print all fixes (one line each: name, target, status, description).

#### `show <name>`
Print full record: rule JSON, provenance, validates, affected PSOs.

#### `add --name X --target sn2 --description "..." --rule '{...}' --provenance "..." --validates "..."`
Register a new fix.

#### `update --name X [--status validated] [--description "..."]`
Update status (experimental / validated / deprecated) or other fields.

#### `emit-launch --active <name1>,<name2> --out launch.ps1`
Combine multiple fixes into a single launch script — merges env vars, dup_cfg references, etc.

### Rule JSON format
```json
{
  "env": {"UEVR_SN2_SKYATMOS_SKIP_RIGHT": "1"},
  "dup_cfg_entries": {
    "0x9d14fcf0": {"mode": "synthesize_right_cb", "view_cb_roots": [3]}
  }
}
```

### Example
```bash
python sn2_fix_db.py emit-launch \
    --active restricted-dup-cfg,skyatmos-skip-right \
    --out launch_combo.ps1
```
→ Generates a clean PowerShell launch script with merged env vars.

### Goal
Curated, versioned fix catalog so multi-fix combos can be experimented with cleanly + provenance is preserved across sessions.

---

## frame_capture_inspect.py

**Purpose**: Analyze the `events.json` output from `Sn2FrameCppExport` to inspect a captured frame's D3D12 event stream.

### Inputs
- Capture dir or events.json path

### Subcommands / flags
- `--summary` — count of events by kind
- `--range start-end` — filter by event index range
- `--filter-kind X` — only show events of kind X
- `--filter-pso 0xCRC` — filter by PSO pointer
- `--head N` — limit output

### Used for
- Understanding what was captured in a single frame
- Identifying sub-frames for specific PSOs (e.g., just the VoxelizePS draws)
- Sanity check that `Sn2FrameCppExport` is recording the right events

---

## sn2_fix_rules.py (Phase GG)

**Purpose**: CLI manager for the live-reloaded UEVR fix rules. Lets you experiment with rules WITHOUT hand-editing JSON. Each command modifies `C:\tmp\sn2_fix_rules.json` and UEVR reloads it within ~1 second (`Sn2FixRuleEngine::refresh_rules` polls every 60 frames).

### Subcommands

| Command | Action |
|---|---|
| `list` | Print all active + disabled rules |
| `add --ps-crc 0x... --eye right --swap-cb 'root=3,to=synth(donor=0x4d44ce74)' [--redirect-rtv mirror] [--no-viewport-shift] [--watch]` | Add or replace a rule |
| `remove --ps-crc X --eye E` | Delete a rule |
| `disable --ps-crc X --eye E [--reason ...]` | Move to disabled_rules |
| `enable --ps-crc X --eye E` | Restore from disabled |
| `clear --force` | Drop all active rules |
| `apply-preset NAME [--merge]` | Apply a canonical preset |
| `watch-reload [--timeout 15]` | Tail UEVR log for next reload event |

### Presets shipped
- `voxelize-synth-test` — VoxelizePS donor synth + RTV mirror
- `consumer-srv-redirect` — Right-eye SLW PS SRV slot 5 → mirror
- `restricted-donor-only` — donor right-eye delta swap only

### Swap-cb format

`root=<N>,to=<spec>` where `<spec>` is one of:
- `synth(donor=0xCRC)` — bind donor's snapshotted right-eye CB
- `delta=-N` — view_cb_only mode with pool delta

### Example workflow

```bash
# Apply known preset for testing
python sn2_fix_rules.py apply-preset voxelize-synth-test

# Inspect current state
python sn2_fix_rules.py list

# Try a different donor
python sn2_fix_rules.py add --ps-crc 0x9d14fcf0 --eye right \
    --swap-cb 'root=3,to=synth(donor=0xf7ddfe06)' \
    --redirect-rtv mirror --no-viewport-shift --watch

# (--watch tails the UEVR log to confirm reload happened)

# Disable while iterating elsewhere
python sn2_fix_rules.py disable --ps-crc 0x9d14fcf0 --eye right --reason "debugging"
```

### Why this matters

Before Phase GG, each fix experiment was: edit dup_cfg JSON → kill game → rebuild → relaunch → wait for game to render → trigger → inspect. **~3 minutes per iteration.**

After Phase GG: `python sn2_fix_rules.py add ... --watch` → ~2 seconds for rule to hot-reload. **~50x faster iteration** for the most common fix experimentation cycle.

---

## cb_reflect_decode.py

**Purpose**: Decode constant buffer binary dumps using SHADER REFLECTION extracted from the DXBC bytecode — works for any shader, not just hardcoded UE5 layouts.

### Inputs
- DXBC bytecode files (from `Sn2PsoBytecodeDumper`)
- CB binary dumps (from `Sn2CbDumper`)
- `dxc.exe` for disassembly + cbuffer block extraction

### Subcommands
- `reflect <shader.dxbc> [--out layout.json]` — extract cbuffer layouts to JSON
- `reflect-dir <dir> --out <layouts_dir>` — bulk
- `decode <cb.bin> --layout layout.json` — decode a single CB with a known layout
- `diff --layout L --left L.bin --right R.bin` — symbolic per-eye diff
- `auto --shader S.dxbc --cb-left L.bin --cb-right R.bin` — reflect + decode + diff in one

### Field type coverage
- Scalars: float, int, uint, bool (incl. arrays)
- Vectors: float2-4, int2-4, uint2-4
- Matrices: float4x4, float3x4, float4x3
- Half precision: half, half2-4

### Why this matters
`decode_view_cb.py` hardcodes UE5's `ViewUniformShaderParameters` (~50 known fields). For any OTHER shader (material PS CBs, lighting CBs, fog params), it can't decode. This tool reads the actual cbuffer declaration from the compiled DXBC's reflection metadata — so it decodes ANY shader's CBs symbolically.

### Example
```bash
# End-to-end: figure out what fields differ per-eye in VoxelizePS root 3
python cb_reflect_decode.py auto \\
    --shader pso_0x9d14fcf0_PS.dxbc \\
    --cb-left  G_0x9d14fcf0_root3_eye0_seq0001.bin \\
    --cb-right G_0x9d14fcf0_root3_eye0_seq0001.bin
```

---

## uevr_rd_bridge.py

**Purpose**: Translate UEVR live patches (from `Sn2CaptureSidecar` JSON) into RenderDoc replay-time overrides — fix iteration in seconds instead of game-restart minutes.

### Workflow
1. Run game with UEVR live patches
2. Simultaneously trigger:
   - A RenderDoc capture (F12)
   - A UEVR sidecar emission
3. Run `python uevr_rd_bridge.py --sidecar sidecar_seq0001.json --emit-python apply.py`
4. Open the RD capture in qrenderdoc → Python shell → `exec(open("apply.py").read())`
5. RD replays the captured frame with overrides matching UEVR's live patches
6. Iterate by modifying the override plan + re-running the apply script

### Inputs
- `--sidecar <path>` — JSON sidecar from `Sn2CaptureSidecar`
- `--capture <path>` — RD capture path (for context/docs)
- `--out-plan <path>` — output override plan JSON
- `--emit-python <path>` — runnable script for qrenderdoc Python shell

### Translation rules
- `view_cb_only` (delta=-N) → `cb_swap` with delta_bytes
- `synthesize_right_cb` → `cb_synth` (bind donor's right CB content)
- `skip` → noop in plan

### Status
Scaffold complete; the actual RD-side API calls (SetBufferOverrideGPU on identified resources) are TODO comments in the emitted Python — needs adapter to find the capture's CB resource by (event, ps_crc, root) tuple.
