# Debugging Workflows

End-to-end workflows for common SN2 stereo debugging tasks.

## Index

- [Workflow 1: Identify a per-eye-broken PSO](#workflow-1-identify-a-per-eye-broken-pso)
- [Workflow 2: Find View CB layout for a PSO](#workflow-2-find-view-cb-layout-for-a-pso)
- [Workflow 3: Diff per-eye CB content](#workflow-3-diff-per-eye-cb-content)
- [Workflow 4: Capture per-eye screenshots programmatically](#workflow-4-capture-per-eye-screenshots-programmatically)
- [Workflow 5: Find LEFT-only producers](#workflow-5-find-left-only-producers)
- [Workflow 6: Test a fix in a single iteration](#workflow-6-test-a-fix-in-a-single-iteration)
- [Workflow 7: Visual diff per-pixel](#workflow-7-visual-diff-per-pixel)

---

## Workflow 1: Identify a per-eye-broken PSO

You see a visual artifact on one eye but not the other. Which PSO is responsible?

### Quick path: automated bisection

```bash
# 1. Launch game with all required env vars
powershell -File launch_phase_u.ps1

# 2. From the binding analyzer JSON or restricted dup_cfg, get all candidate CRCs
cat E:\Github\UEVRJ\artifacts\sn2_dup_cfg_restricted.json | jq -r '.entries | keys[]' | tr '\n' ',' > /tmp/candidates.csv

# 3. Run the auto-bisector
python sn2_auto_bisect.py \
    --crcs $(cat /tmp/candidates.csv) \
    --eye left \
    --output-dir C:\tmp\bisect_run

# 4. Check result.json for converged CRC(s)
cat C:\tmp\bisect_run\result.json
```

### Manual path (when auto-bisect can't converge)

```bash
# 1. Launch game with Magic Ink live-reload enabled
powershell -File launch_phase_u.ps1

# 2. Write half the candidate list to ink_skip.txt
echo "0x009f8918
0x03f0d897
0x0b0e2356" > C:\tmp\ink_skip.txt

# 3. Wait ~2 seconds for live-reload to kick in
# 4. Observe Meta XR Simulator visually
# 5. If artifact moved/changed → halve, recurse on candidates
# 6. If artifact unchanged → producer not in this half, try the other
```

---

## Workflow 2: Find View CB layout for a PSO

Which root holds the View CB for PSO X?

### Method 1: Binding analyzer sample counts

The View CB binds many times per frame. Looking at `Sn2BindingAnalyzer` JSON:

```bash
cat sn2_analyzer.json | jq '.psos["0x17ad293f2d0"].roots'
```

Output like:
```json
{
  "2": {"samples_l": 14901, "samples_r": 4963, ...},
  "3": {"samples_l": 4967, "samples_r": 4963, ...}
}
```

The root with highest sample counts AND tight L/R matching is usually View CB.

### Method 2: CB byte pattern match (definitive)

```bash
# Set env vars to dump CB at the target PSO
$env:UEVR_SN2_CB_DUMP_DIR = 'C:\tmp\cb_dumps'
$env:UEVR_SN2_CB_DUMP_PSOS = '0x9d14fcf0'
$env:UEVR_SN2_CB_DUMP_ROOTS = '3,4,5,6,7,8,11'   # try several

# Launch, observe, then decode
python -c "
import struct
for root in [3,4,5,6,7,8]:
    with open(f'C:/tmp/cb_dumps/G_0x9d14fcf0_root{root}_eye0_seq0001.bin','rb') as f:
        data = f.read(16)
        floats = struct.unpack('<4f', data)
        print(f'root {root}: {floats}')
"
```

The root whose first 4 floats look like a matrix row (one element ~0.5, one near 0, one 0, one ~-0.7) is View CB.

### Confirmed for SN2 (2026-05-22)

Most graphics PSes including VoxelizePS, 0x37558DE4, 0x4D44CE74 use **root 3** for View CB.

---

## Workflow 3: Diff per-eye CB content

You want to find which slots in the View CB differ between LEFT and RIGHT eye.

```bash
# 1. Dump CB at a both-eye PSO (e.g., 0x4d44ce74) for both eyes
$env:UEVR_SN2_CB_DUMP_DIR = 'C:\tmp\cb_dumps'
$env:UEVR_SN2_CB_DUMP_PSOS = '0x4d44ce74'
$env:UEVR_SN2_CB_DUMP_ROOTS = '3'
$env:UEVR_SN2_CB_DUMP_MAX = '8'

# 2. Launch + run for some seconds
powershell -File launch_phase_u.ps1

# 3. Byte-diff
python diff_cb_dumps.py --dir C:\tmp\cb_dumps --out cb_diff.json
cat cb_diff.json
```

Or symbolic field-by-field:

```bash
python decode_view_cb.py \
    G_0x4d44ce74_root3_eye0_seq0001.bin \
    G_0x4d44ce74_root3_eye1_seq0001.bin \
    --diff-only
```

### Expected output

~80 of 256 16-byte slots differ. Notable:
- Slot 0 (offset 0): WorldToClip[0] — X component changes
- Slot 12 (offset 192): ViewToClip — minor differences
- Slot 59 (offset 944): camera world position — IPD shift in X
- Slot 79 (offset 1264 W slot): eye marker — LEFT=0.25, RIGHT=0.75

---

## Workflow 4: Capture per-eye screenshots programmatically

You want LEFT and RIGHT eye PPM files automatically without manual Meta XR Simulator screenshotting.

```bash
# Required env (set via launch_phase_u.ps1 or manually)
# $env:UEVR_SN2_EYE_SCREENSHOT_TRIGGER_FILE = 'C:\tmp\uevr_shot_req.txt'
# $env:UEVR_SN2_EYE_SCREENSHOT_OUTPUT_DIR = 'C:\tmp\uevr_screenshots'
# $env:UEVR_ENABLE_D3D12_DIAGNOSTIC_COMMAND_LIST_HOOKS = '1'

# Trigger + wait
echo "req" > C:\tmp\uevr_shot_req.txt
until [ -f "C:/tmp/uevr_screenshots/done.txt" ]; do sleep 0.2; done

# Read PPMs
ls C:\tmp\uevr_screenshots\
# left.ppm
# right.ppm
# backbuffer.ppm
# done.txt

# Clean up before next trigger
rm C:\tmp\uevr_screenshots\done.txt
```

### Convert PPM to PNG for inline viewing

```python
from PIL import Image
with open(r'C:\tmp\uevr_screenshots\left.ppm', 'rb') as f:
    magic = f.readline().strip()
    w, h = map(int, f.readline().strip().split())
    f.readline()
    raw = f.read(w*h*3)
Image.frombytes('RGB', (w,h), raw).save('left.png')
```

### Caveats
- L+R captures require `UEVR_ENABLE_D3D12_DIAGNOSTIC_COMMAND_LIST_HOOKS=1` (BB-only without)
- Game must be actively rendering — game splash/loading screens won't produce useful captures
- If trigger lands mid-frame between LEFT and RIGHT draws, only one eye may be captured
- Re-trigger on next frame if a piece is missing

---

## Workflow 5: Find LEFT-only producers

You want to find producers (compute/draw) that only fire on LEFT eye, leaving RIGHT eye reading stale data.

```bash
# 1. Enable binding analyzer
$env:UEVR_SN2_BINDING_ANALYZER_JSON = 'C:\tmp\sn2_analyzer.json'

# 2. Launch, play for ~60 seconds
powershell -File launch_restricted_cfg.ps1

# 3. Parse the analyzer JSON for LEFT-only PSOs
python -c "
import json
with open('C:/tmp/sn2_analyzer.json') as f: a = json.load(f)
for pso, data in a.get('psos', {}).items():
    for root, info in data.get('roots', {}).items():
        l = info.get('samples_l', 0)
        r = info.get('samples_r', 0)
        if l > 100 and r == 0:
            print(f'{data.get(\"ps_crc\")} root={root} L={l} R=0')
"
```

### Output format
```
0x009f8918 root=4 L=2348 R=0
0x9d14fcf0 root=3 L=109230 R=0
```

These are LEFT-only PSOs. They're candidates for synth_right_cb dup.

---

## Workflow 6: Test a fix in a single iteration

You have a hypothesis (e.g., "dup PSO X with synth_right_cb at root 3 should fix the bug").

### Iteration loop

```bash
# 1. Edit dup config
python -c "
import json
with open(r'E:/Github/UEVRJ/artifacts/sn2_dup_cfg_voxelize_synth.json') as f: d = json.load(f)
d['entries']['0x9d14fcf0'] = {'mode': 'synthesize_right_cb', 'view_cb_roots': [3]}
with open(r'E:/Github/UEVRJ/artifacts/sn2_dup_cfg_voxelize_synth.json', 'w') as f: json.dump(d, f, indent=2)
"

# 2. Kill game (dup_cfg is NOT live-reloaded yet)
wmic process where "Name='Subnautica2-Win64-Shipping.exe'" delete

# 3. (If code changed) Rebuild
"E:/Github/UEVRJ/build_run.cmd"

# 4. Launch
powershell -File launch_phase_u.ps1

# 5. Wait for game to render
# ~30s for full UEVR init + scene load

# 6. Capture eye PPMs
echo "req" > C:\tmp\uevr_shot_req.txt
# wait for done.txt
# read left.ppm + right.ppm

# 7. Visually inspect or auto-diff vs expected
```

### Iteration time estimate
- No code change: ~1.5 minutes (kill + relaunch + render)
- Code change: ~2 minutes (build + kill + relaunch + render)

### Iteration without restart (when possible)
- Magic Ink skip list: edit `C:\tmp\ink_skip.txt`, wait ~1s
- Otherwise, restart needed

---

## Workflow 7: Visual diff per-pixel

You want a heatmap showing where LEFT and RIGHT eye images differ.

```bash
# 1. Enable RT diff in launch
$env:UEVR_SN2_RT_DIFF_DIR = 'C:\tmp\rt_diff'
$env:UEVR_SN2_RT_DIFF_EVERY_N_FRAMES = '60'
$env:UEVR_SN2_RT_SNAPSHOT = '1'

# 2. Launch
powershell -File launch_phase_u.ps1

# 3. After some seconds, PPMs accumulate in rt_snapshots dir

# 4. Process with diff tool
python diff_rt_dumps.py \
    --in C:\Users\ellio\AppData\Roaming\UnrealVRMod\Subnautica2-Win64-Shipping\rt_snapshots

# Output: <in>/diff/diff_seq<NNNN>.ppm (red heatmap) + diff_summary.json
```

### Interpreting heatmaps
- Bright red regions = pixels where L and R eye scene-color differ significantly
- Expected: some difference everywhere due to stereo parallax (~mm of disparity)
- Diagnostic: regions of dramatic red intensity (>100 per channel) = real divergence (e.g., the missing fog on right eye)

### `diff_summary.json`
- `mean_diff`: average per-pixel L2 across image
- `max_diff`: maximum per-pixel L2
- `differing_pixels`: count of pixels with any L≠R
- `region_mean_diff`: 8×8 grid of mean diffs (spatial summary)
