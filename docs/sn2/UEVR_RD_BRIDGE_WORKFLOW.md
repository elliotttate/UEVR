# UEVR Frame Capture + RenderDoc Bridge

There are TWO capture paths now. Choose based on your tolerance for risk:

## Path 1 — `Sn2FrameCapture` (RECOMMENDED, no interference)

A self-contained orchestrator. On file trigger, atomically captures everything UEVR knows about one frame to a unified directory. **No external DLL loaded.**

```powershell
$env:UEVR_SN2_FRAME_CAPTURE_DIR          = "C:\tmp\uevr_captures"
$env:UEVR_SN2_FRAME_CAPTURE_TRIGGER_FILE = "C:\tmp\uevr_frame_cap.txt"

# Plus enable individual sub-modules you want included:
$env:UEVR_SN2_EYE_SCREENSHOT_TRIGGER_FILE = "C:\tmp\uevr_shot_req.txt"
$env:UEVR_SN2_EYE_SCREENSHOT_OUTPUT_DIR   = "C:\tmp\uevr_screenshots"
$env:UEVR_SN2_CAPTURE_SIDECAR_DIR         = "C:\tmp\sidecars"
$env:UEVR_SN2_STATE_INSPECTOR_DIR         = "C:\tmp\state_inspect"
$env:UEVR_SN2_CB_DUMP_DIR                 = "C:\tmp\cb_dumps"
$env:UEVR_SN2_PSO_BYTECODE_DIR            = "C:\tmp\pso_bytecode"
$env:UEVR_SN2_GPU_COUNTERS                = "1"
$env:UEVR_SN2_RES_TIMELINE_DIR            = "C:\tmp\res_timeline"
$env:UEVR_SN2_DESCRIPTOR_LINEAGE          = "1"
# (etc.)

# Trigger:
Set-Content C:\tmp\uevr_frame_cap.txt "go"
```

UEVR detects the trigger, all sub-modules fire under one capture sequence, and a `manifest.json` is written that points at every artifact.

**Why this is the RECOMMENDED path:** RenderDoc's in-app DLL hooks D3D12 vtables. UEVR also hooks D3D12 vtables. Loading both into the same process causes conflicts (similar class to ImGui interference observed in earlier sessions). Self-contained capture avoids the conflict entirely.

## Path 2 — `Sn2RdCapture` (RISK: hook interference)

Loads `renderdoc.dll` into the running game process via RD's in-app API + drives `TriggerCapture()`. Produces real `.rdc` files openable in qrenderdoc.

```powershell
$env:UEVR_SN2_RD_CAPTURE                 = "1"
$env:UEVR_SN2_RD_CAPTURE_DLL             = "C:\Program Files\RenderDoc\renderdoc.dll"
$env:UEVR_SN2_RD_CAPTURE_TRIGGER_FILE    = "C:\tmp\rd_capture.txt"
$env:UEVR_SN2_RD_CAPTURE_OUT_TEMPLATE    = "C:\tmp\uevr_rd_captures\sn2"
$env:UEVR_SN2_RD_CAPTURE_ALSO_EMIT_SIDECAR = "1"

Set-Content C:\tmp\rd_capture.txt "go"
```

This path is NOT WIRED INTO THE DEFAULT BUILD as of 2026-05-22 — the files exist but `Sn2RdCapture.cpp` is not in `uevr.vcxproj`. If you want to try it, add the file + rebuild. Use with caution: RD's hooks may break UEVR's live patches.

---

# UEVR ↔ RenderDoc Bridge Workflow (Path 1 detail)

The bridge connects UEVR's live patches to RenderDoc's replay-time mutation, giving you **seconds-per-iteration fix experimentation** instead of minutes-per-restart.

## Quick start

1. **Launch the game with bridge env vars active:**

```powershell
$env:UEVR_SN2_CAPTURE_SIDECAR_DIR    = "C:\tmp\sidecars"
$env:UEVR_SN2_EYE_SCREENSHOT_TRIGGER_FILE = "C:\tmp\uevr_shot_req.txt"
$env:UEVR_SN2_EYE_SCREENSHOT_OUTPUT_DIR   = "C:\tmp\uevr_screenshots"
# ... plus your normal UEVR_SN2_* env vars
& "E:\Github\Subnautica 2\moddingkit\runs\launch_phase_u.ps1"
```

2. **In a second terminal, simultaneously trigger UEVR screenshot AND a RenderDoc capture:**

```powershell
# Touch the UEVR trigger (this also fires the sidecar emit)
Set-Content C:\tmp\uevr_shot_req.txt "request"

# Within the same frame, press F12 in qrenderdoc, or use:
# (assumes qrenderdoc is already attached / RD layer is injecting)
```

3. **Inspect what UEVR captured:**

```bash
ls C:\tmp\sidecars\
# sidecar_seq0001.json
# synth_donor_cb_seq0001.bin    (if synth donor active)
```

4. **Generate the qrenderdoc bridge script:**

```bash
python "E:\Github\Subnautica 2\moddingkit\tools\uevr_rd_bridge.py" \
    --sidecar C:\tmp\sidecars\sidecar_seq0001.json \
    --capture C:\path\to\capture.rdc \
    --emit-python C:\tmp\apply_uevr.py
```

5. **Open the RD capture and apply UEVR's patches:**

```bash
qrenderdoc.exe --capture "C:\path\to\capture.rdc" --python "C:\tmp\apply_uevr.py"
```

The qrenderdoc UI is now showing the captured frame with UEVR's patches applied at replay time via `SetBufferOverrideGPU`.

6. **Iterate:** modify the sidecar's dup_cfg references or the synth donor bytes, re-run step 5. Each iteration takes ~seconds (RD replay) instead of ~minutes (game restart).

## What the sidecar contains

```json
{
  "schema_version": 1,
  "emitted_at_iso8601": "2026-05-22T15:42:00Z",
  "seq": 1,
  "pid": 12345,
  "dup_cfg_file": "E:\\Github\\UEVRJ\\artifacts\\sn2_dup_cfg_voxelize_synth.json",
  "magic_ink_file": "C:\\tmp\\ink_skip.txt",
  "env_vars": {
    "UEVR_SN2_DUP_CONFIG_FILE": "...",
    "UEVR_SN2_DUPLICATE_SLW_BASEPASS_RIGHT": "1"
  },
  "active_mirrors": [
    {
      "game_ptr": 12345678,
      "mirror_ptr": 87654321,
      "dim": {"width": 54, "height": 30, "depth_or_array": 48},
      "format": 26,
      "flags": 5,
      "dimension": 4,
      "has_rtv": true,
      "seq": 38
    }
  ],
  "magic_ink_skips": ["0x009f8918", "0x37558de4"],
  "magic_ink_eye": 1,
  "synth_donor_cb_dump": "synth_donor_cb_seq0001.bin",
  "synth_donor_cb_size": 4096,
  "synth_donor_gpu_va": 0x77ff000,
  "synth_donor_snapshot_count": 2400
}
```

The companion `synth_donor_cb_seq0001.bin` contains the actual right-eye CB bytes UEVR was binding for synth-mode PSOs.

## How the bridge translates UEVR patches to RD overrides

### `view_cb_only` mode (delta=-N)

UEVR runtime: at right-eye dup of PSO X, swap CBV from `(base, current_offset)` to `(base, current_offset + delta)`.

RD equivalent:
1. Find all right-eye action events whose bound PS matches PSO X
2. At each event, get the CBV resource at the configured root parameter
3. Read N bytes from offset `current_offset + delta` of that buffer (via `GetBufferData`)
4. Call `SetBufferOverrideGPU(buf_id, current_offset, those_bytes)`
5. Re-replay → right-eye CBV now contains the delta-shifted bytes

### `synthesize_right_cb` mode

UEVR runtime: bind donor snapshot bytes from the synth upload buffer.

RD equivalent:
1. Find right-eye action events for PSO X
2. Get CBV resource at the configured root
3. Read sidecar's `synth_donor_cb_dump` bytes
4. `SetBufferOverrideGPU(buf_id, current_offset, donor_bytes)`
5. Re-replay → right-eye CBV now contains donor's snapshotted right-eye View CB

### Mirror RTV substitution

Currently NOT translated by the bridge — RD doesn't natively support per-eye RTV redirect because it's read-only at the resource binding level. UEVR's mirror approach is unique to live-patching.

If you need to test what right-eye looks like with mirrored fog volume, the workaround:
- Capture state with UEVR's mirror active (sidecar shows the mirror inventory)
- In RD, manually swap the SRV that points at the volume → another resource the operator selects

## Verifying the bridge worked

After applying overrides in qrenderdoc:
1. Navigate to a right-eye action event
2. Open the Pipeline State → Pixel Shader tab
3. Inspect the bound CBV's contents
4. The bytes should match UEVR's patched content (not the engine's original)
5. Open the texture viewer → the rendered RT should change accordingly

## Limitations + Known gaps

- **PS hash mapping**: UEVR identifies PSOs by PS CRC (32-bit hash of bytecode). RD identifies by `ResourceId` of the shader resource. The bridge currently doesn't auto-map these — operator must identify the matching shader in qrenderdoc and provide it to the script. Future: extend sidecar to include the PS bytecode hash RD uses.

- **Mirror RTV translation**: not supported (see above).

- **Multi-frame patches**: UEVR's `synthesize_right_cb` donor snapshot is refreshed every 30 frames during live game. The sidecar captures ONE point-in-time. If the donor content changes meaningfully across the frames RD captures (it shouldn't in a static scene), the bridge would only apply ONE version.

- **Bind kind mismatch**: UEVR can redirect SRVs/UAVs/RTVs. RD's `SetBufferOverrideGPU` only patches BUFFERS. For texture overrides, RD's `ReplaceResource` API would be needed — not yet wired in the bridge.

## Iteration workflow example

Suppose you want to test "what if VoxelizePS used a different donor's CB content":

1. Live UEVR + RD capture with current dup_cfg (donor=0x4d44ce74)
2. Sidecar captured, RD replay shows UEVR-equivalent state
3. **Iterate**: in qrenderdoc, hand-edit the sidecar JSON or the side-binary `synth_donor_cb_seq0001.bin` (e.g., zero out specific bytes you suspect)
4. Re-run `qrenderdoc --capture X --python apply_uevr.py`
5. See if the right-eye fog tint changes
6. Repeat with different patches

Each iteration: ~5 seconds (RD replay) vs ~3 minutes (UEVR rebuild + game restart).

## Future work

- Auto-extract PS bytecode hash at capture sidecar emit time so bridge can auto-match
- Capture-vs-replay diff: re-capture LEFT and RIGHT scene-color RTs from RD after each override, run `diff_rt_dumps.py` automatically
- `Sn2DebugColorOverride` translation — emit RD shader replacement for magenta visualization
- Mirror RTV via `ReplaceResource`
