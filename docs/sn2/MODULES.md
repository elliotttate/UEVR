# UEVR C++ Modules

All modules live in `E:\Github\UEVRJ\src\hooks\`. They're integrated via `D3D12Hook.cpp` and built into `UEVRBackend.dll`.

## Index

### Core debugging modules
- [Sn2BindingAnalyzer](#sn2bindinganalyzer) — Per-PSO View CB delta tracking
- [Sn2CbDumper](#sn2cbdumper) — Dump CBV bytes to disk
- [Sn2PsoBytecodeDumper](#sn2psobytecodedumper) — Dump DXBC bytecode at PSO create
- [Sn2FrameCppExport](#sn2framecppexport) — RenderDoc-like frame→C++ export (scaffold)
- [Sn2RightCbSynth](#sn2rightcbsynth) — Donor snapshot for right-eye View CB
- [Sn2MagicInk](#sn2magicink) — Live PS skip with file-reload + comment parsing
- [Sn2RtDiff](#sn2rtdiff) — Pair LEFT+RIGHT scene-color RTs for offline diff
- [Sn2EyeScreenshot](#sn2eyescreenshot) — File-trigger per-eye PPM capture
- [Sn2StateInspector](#sn2stateinspector) — On-demand draw-state JSON dump
- [Sn2UweFogMirrorHook](#sn2uwefogmirrorhook) — Parallel UAV+SRV+RTV for fog-shape resources
- [Sn2FixRuleEngine](#sn2fixruleengine) — Declarative JSON fix rules (header-only scaffold)

### Phase X — RenderDoc-parity modules
- [Sn2ResourceTimeline](#sn2resourcetimeline) — Per-resource access timeline (NOW IMPLEMENTED)
- [Sn2GpuCounters](#sn2gpucounters) — Per-PSO GPU timing via D3D12 timestamp queries
- [Sn2PixelHistory](#sn2pixelhistory) — For any pixel, list every draw that covered it
- [Sn2MeshDump](#sn2meshdump) — Per-draw VB/IB metadata dump

---

## Sn2BindingAnalyzer

**Purpose**: For every CBV binding observed in the draw stream, track per-(PSO, root) which GPU_VAs are seen for LEFT vs RIGHT eye. Emit JSON every 120 frames.

### Files
- `Sn2BindingAnalyzer.hpp` / `.cpp`

### Env vars
- `UEVR_SN2_BINDING_ANALYZER_JSON=<path>` — output JSON path

### How it works
- Hooks into `set_graphics_root_constant_buffer_view` and `set_compute_root_constant_buffer_view`
- Records `(pso, root_param, eye_bucket, gpu_va)` in a hash map
- Resolves each GPU_VA to `(parent_resource, byte_offset)` via `sn2_upload_buf_map::resolve_va`
- Identifies pool-relative deltas between LEFT and RIGHT samples for the same (pso, root)
- Periodically dumps full state to JSON

### JSON output shape
```json
{
  "psos": {
    "0x17ad293f2d0": {
      "ps_crc": "0x37558de4",
      "roots": {
        "3": {
          "samples_l": 4967,
          "samples_r": 4963,
          "computed_delta": 256,
          "parent_resource_ptr": "0x17929009510"
        }
      }
    }
  }
}
```

### Used for
- Identifying which root holds View CB (via large sample counts)
- Computing per-PSO per-eye View CB pool deltas
- Generating safe `view_cb_only` dup_cfg entries (the script `generate_dup_cfg_from_analyzer.py`)

---

## Sn2CbDumper

**Purpose**: At configured PSO draws, dump the bytes of CBV at each root to disk. Separate file per (PSO, root, eye, sequence). Enables offline byte-diff to find which View CB slots diverge per-eye.

### Files
- `Sn2CbDumper.hpp` / `.cpp`

### Env vars
- `UEVR_SN2_CB_DUMP_DIR=<path>` — output dir
- `UEVR_SN2_CB_DUMP_PSOS=<csv>` — PS CRCs to dump (e.g., `0x9d14fcf0,0x4d44ce74`)
- `UEVR_SN2_CB_DUMP_ROOTS=<csv>` — root params to dump (e.g., `3,4,5,8,10`)
- `UEVR_SN2_CB_DUMP_MAX=<N>` — max dumps per (pso, root, eye) tuple

### Output files
`G_0x<ps_crc>_root<N>_eye<0|1>_seq<NNNN>.bin` (raw CB bytes)
`G_0x<ps_crc>_root<N>_eye<0|1>_seq<NNNN>.json` (metadata)

### Used for
- Comparing donor vs target CBs for compatibility (`decode_view_cb.py`)
- Finding per-eye divergent slots (`diff_cb_dumps.py`)
- Validating that synth CB is correct content

---

## Sn2PsoBytecodeDumper

**Purpose**: At PSO create time (`CreateGraphicsPipelineState` / `CreateComputePipelineState`), dump the DXBC bytecode of each shader stage to disk. Enables offline reflection via dxc.

### Files
- `Sn2PsoBytecodeDumper.hpp` / `.cpp`

### Env vars
- `UEVR_SN2_PSO_BYTECODE_DIR=<path>` — output dir

### Output files
- `pso_0x<crc>_PS.dxbc` — pixel shader bytecode
- `pso_0x<crc>_CS.dxbc` — compute shader bytecode
- VS dumping intentionally disabled (UE5's `d3d12_pso_vertex_crc32` not exposed)

### Used for
- Disassembly: `dxc -dumpbin file.dxbc`
- Reflection: identify which roots hold which resources, which fields the shader reads from each cbuffer
- Validating struct compatibility for CB synthesis

---

## Sn2FrameCppExport

**Purpose**: Capture one frame's D3D12 calls and emit a standalone C++ source that can be replayed. RenderDoc-equivalent.

### Files
- `Sn2FrameCppExport.hpp` / `.cpp`

### Status
**Scaffold only**. Writes basic event log + skeleton `CMakeLists.txt` + `main.cpp` + `capture_frame.cpp`. The actual D3D12 call recording logic is TODO.

### Env vars
- `UEVR_SN2_FRAME_CPP_EXPORT_DIR=<path>` — output dir
- `UEVR_SN2_FRAME_CPP_EXPORT_FRAME=<N>` — frame number to capture

### Future work
Hook every D3D12 method on the command list, serialize args, generate replay code that reconstructs resources/PSOs/heaps and re-issues the calls. RenderDoc has ~10k LOC for this; would be a substantial UEVR-side build.

---

## Sn2RightCbSynth

**Purpose**: Snapshot a "donor" PSO's right-eye CB content into a UEVR-owned upload buffer. Expose the GPU_VA for binding at right-eye dup of LEFT-only PSOs.

### Files
- `Sn2RightCbSynth.hpp` / `.cpp`

### Env vars
- `UEVR_SN2_RIGHT_CB_SYNTH=1` — enable
- `UEVR_SN2_RIGHT_CB_SYNTH_DONOR=0x<crc>` — donor PS CRC (default `0x4d44ce74`)
- `UEVR_SN2_RIGHT_CB_SYNTH_ROOT=<N>` — donor root holding View CB (default `3`)
- `UEVR_SN2_RIGHT_CB_SYNTH_SIZE=<bytes>` — snapshot buffer size (default 4096)

### How it works
1. On init, allocate a 4KB upload buffer; remember its CPU/GPU pointers
2. Hook every `set_graphics_root_constant_buffer_view` call
3. If PSO matches donor + root matches + viewport bucket is RIGHT:
   - Resolve GPU_VA to CPU pointer via `sn2_upload_buf_map`
   - SEH-guarded memcpy into our buffer
   - Log every 600 captures
4. Caller (in dup function) calls `get_right_va()` to get the synth GPU_VA

### SEH guard
The source upload buffer may be freed between snapshot calls. The memcpy is wrapped in `__try/__except` via a destructor-free helper `safe_memcpy_seh()` (MSVC restriction).

### Limitation
Donor CB may not be content-compatible with target PSO's expectations. See [TROUBLESHOOTING.md](TROUBLESHOOTING.md#wrong-fog-colors-with-synth) and "Proper synthesis" in [ARCHITECTURE.md](ARCHITECTURE.md#proper-synthesis-future-work).

---

## Sn2MagicInk

**Purpose**: Live PS skip — for any configured PS CRC, skip the `original()` call in the draw hook. Used for binary-search PSO bisection: whatever screen region disappears identifies the PSO.

### Files
- `Sn2MagicInk.hpp` (header-only)

### Env vars
- `UEVR_SN2_MAGIC_INK_SKIP_PS_CRCS=<csv>` — static skip list, or
- `UEVR_SN2_MAGIC_INK_SKIP_FILE=<path>` — **file-based live-reload**
- `UEVR_SN2_MAGIC_INK_EYE=left|right|both` — gate by eye bucket

### File format (live-reload)
```
# This is a comment — # to end-of-line is ignored
0x009f8918
0x37558de4
0x9d14fcf0  # inline comment also works
```

### Live reload
- Polled every 60 frames (~1s at 60fps)
- File mtime compared to last-seen
- On change, re-parse + atomically replace skip set

### Critical parser detail
`#` starts a comment that runs to the **end of the line**, not just the next whitespace. Earlier bug: parser only skipped the `#` token, so example CRCs embedded in comment prose (like `# saw 0x37558de4 earlier`) silently parsed and added to skip set. Fixed in this session.

### Eye bucket mapping
UEVR's `StereoTraceBucket` enum: Unknown=0, Left=1, Right=2, Full=3, Multi=4.

`UEVR_SN2_MAGIC_INK_EYE`:
- `left` → matches eye_bucket == 1
- `right` → matches eye_bucket == 2
- (anything else) → matches both eyes

---

## Sn2RtDiff

**Purpose**: Pair the latest LEFT and RIGHT eye scene-color RTs every N frames and queue both for PPM capture. Offline tool diffs them per-pixel.

### Files
- `Sn2RtDiff.hpp` / `.cpp`

### Env vars
- `UEVR_SN2_RT_DIFF_DIR=<path>` — output dir
- `UEVR_SN2_RT_DIFF_EVERY_N_FRAMES=<N>` — capture frequency (default 60)
- Requires `UEVR_SN2_RT_SNAPSHOT=1` (uses that infra)

### How it works
1. At `draw_indexed_instanced`, if RTV slot 0 is non-null, look up its resource
2. Tag the resource per eye (`note_scene_color_write`)
3. At Present, if both eyes seen and frame % N == 0, queue `RTDIFF-L_<seq>` + `RTDIFF-R_<seq>` snapshot intents

### Output
PPMs at `<dir>/RTDIFF_L_<seq>.ppm` and `RTDIFF_R_<seq>.ppm`.

Then run `diff_rt_dumps.py --in <dir>` to produce heatmaps.

---

## Sn2EyeScreenshot

**Purpose**: File-trigger driven per-eye PPM capture for headless tooling. Create a trigger file → UEVR captures L+R+BB → emits PPMs + sentinel.

### Files
- `Sn2EyeScreenshot.hpp` / `.cpp`

### Env vars
- `UEVR_SN2_EYE_SCREENSHOT_TRIGGER_FILE=<path>` — required
- `UEVR_SN2_EYE_SCREENSHOT_OUTPUT_DIR=<path>` — required
- `UEVR_ENABLE_D3D12_DIAGNOSTIC_COMMAND_LIST_HOOKS=1` — required for L+R captures (BB-only without)

### Protocol
1. Tooling writes any content to the trigger file
2. On next Present, UEVR notices, schedules captures, executes our private command list, signals fence
3. Subsequent frames: rt_snapshot drain writes PPMs, our poll moves them into output dir
4. When all complete, writes `done.txt`, deletes trigger
5. Tooling reads `<output_dir>/{left,right,backbuffer}.ppm`, deletes `done.txt`, can re-trigger

### Internal design
- Hooks `draw_indexed_instanced` to cache the latest per-eye scene-color RT pointer
- Uses a private `ID3D12CommandList` + `CommandAllocator` to avoid mid-game-recording state races
- Reuses `sn2_rt_snapshot::schedule_capture` for actual readback + PPM write
- 120-frame timeout marks request as "partial" if any piece never arrives

### Known timing gotcha
If the trigger lands between LEFT and RIGHT basepass draws, only LEFT is cached → only LEFT.ppm + BB.ppm produced. Re-trigger on a later frame.

### Important behavior
After completing a request, the cached per-eye RT pointers are nulled. Next request waits for new draws to repopulate.

---

## Sn2StateInspector

**Purpose**: On-demand "what is bound at THIS draw" inspector. Writes structured JSON per matching draw with every CBV/SRV/UAV/RTV root binding + resolved resource pointer + dimensions + format + GPU_VA.

### Files
- `Sn2StateInspector.hpp` / `.cpp`

### Env vars
- `UEVR_SN2_STATE_INSPECTOR_DIR=<path>` — output dir
- `UEVR_SN2_STATE_INSPECTOR_PS_CRCS=<csv>` — target PSOs
- `UEVR_SN2_STATE_INSPECTOR_EYE=left|right|both` — eye filter
- `UEVR_SN2_STATE_INSPECTOR_MAX=<N>` — max dumps per CRC (default 32)

### API surface
```cpp
bool should_dump(uint32_t ps_crc, int eye_bucket);
void write_snapshot(
    uint32_t ps_crc, int eye_bucket, void* pso_ptr,
    uint32_t vx, uint32_t vy, uint32_t vw, uint32_t vh,
    uint32_t rtv_count, ID3D12Resource** rtv_resources,
    const BindingEntry* graphics_bindings, size_t binding_count);
```

### Integration in D3D12Hook.cpp
```cpp
if (sn2_state_inspector::should_dump(ps_crc, eye_bucket)) {
    // extract bindings into local BindingEntry[]
    sn2_state_inspector::BindingEntry entries[64];
    size_t n = 0;
    // ...fill entries from state...
    sn2_state_inspector::write_snapshot(
        ps_crc, eye_bucket, state.current_pso,
        vp.TopLeftX, vp.TopLeftY, vp.Width, vp.Height,
        state.last_rtv_count, rtv_resource_array,
        entries, n);
}
```

### JSON output shape
```json
{
  "ps_crc": 2638139252,
  "eye_bucket": 1,
  "pso_ptr": 2733678500144,
  "seq": 1,
  "viewport": {"x": 0, "y": 0, "w": 1180, "h": 616},
  "rtv": [
    {"slot": 0, "resource": {"ptr": ..., "format": 26, "width": 54, ...}}
  ],
  "graphics_root": [
    {"kind": "root_cbv", "slot": 3, "gpu_va": ..., "resource": {...}}
  ]
}
```

---

## Sn2UweFogMirrorHook

**Purpose**: Allocate parallel "mirror" resources for fog-shape 3D textures. Provides UAV+SRV (and lazy-RTV) descriptors so consumers/writers can be redirected to read/write the mirror instead of the original.

### Files
- `Sn2UweFogMirrorHook.hpp` / `.cpp`

### Env vars
- `UEVR_SN2_UWE_FOG_MIRROR=1` — enable
- `UEVR_SN2_UWE_FOG_MIRROR_LOG=1` — verbose logging

### Public API
```cpp
const Mirror* MirrorRegistry::find(ID3D12Resource* game_resource);
const Mirror* create_mirror_for(ID3D12Device*, ID3D12Resource*, const D3D12_RESOURCE_DESC&);
bool ensure_mirror_heap(ID3D12Device*);
bool ensure_scratch_heap(ID3D12Device*);
bool ensure_mirror_rtv_heap(ID3D12Device*);   // V4 addition
UINT alloc_scratch_range(UINT count);
UINT alloc_rtv_slot();                         // V4 addition
const ScratchHeapState& scratch_heap();
const MirrorRtvHeapState& mirror_rtv_heap();   // V4 addition
const Mirror* find_bucket_shadow(ID3D12Resource*);
void register_placed(ID3D12Heap*, UINT64 offset, ID3D12Resource*);
```

### Mirror struct
```cpp
struct Mirror {
    ComPtr<ID3D12Resource> resource;       // parallel resource
    D3D12_RESOURCE_DESC desc;              // same as game's
    D3D12_CPU_DESCRIPTOR_HANDLE uav_cpu;
    D3D12_GPU_DESCRIPTOR_HANDLE uav_gpu;
    D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu;
    D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu;   // V4: filled lazily at first dup hit
    bool has_rtv;
    D3D12_RESOURCE_STATES current_state;   // initial COMMON
    uint64_t seq;
};
```

### Hooks installed
- `CreateCommittedResource` → match desc, allocate mirror if signature matches
- `CreatePlacedResource` → match + `register_placed` (bucket by heap+offset)

### Heaps owned
| Heap | Type | Capacity | Purpose |
|---|---|---|---|
| mirror_heap | CBV_SRV_UAV (shader-visible) | ~16K descriptors | 2 slots per mirror (UAV + SRV) |
| scratch_heap | CBV_SRV_UAV (shader-visible) | 4096 descriptors | Ring of 16-slot ranges for runtime redirect |
| mirror_rtv_heap | RTV (CPU-only) | 1024 descriptors | One slot per mirror with RT flag |

---

## Sn2ResourceTimeline

**Purpose**: Per-resource access timeline. For each tracked resource, record write events (PSO, eye, frame) and read events (PSO, eye, frame). Answers "what wrote IntegratedLightScattering on frame N event 7245?".

### Files
- `Sn2ResourceTimeline.hpp` (header-only scaffold)

### Status
Header-only scaffold. The implementation requires hooking access points (draw/dispatch) with knowledge of which resources are read/written. Most of this data is available via `state.last_rtv_handles` (writes) and `state.last_graphics_root_desc_tables` (reads).

### Future env vars
- `UEVR_SN2_RES_TIMELINE_DIR=<path>` — output dir
- `UEVR_SN2_RES_TIMELINE_TRACKED_RESOURCES=<csv>` — resource pointers to track

### Future output
Per-resource JSON file `<dir>/res_<ptr>.json` with arrays of write/read events.

---

## Sn2FixRuleEngine

**Purpose**: Declarative JSON rules for per-PSO fixes. Replaces per-fix C++ code with config edits.

### Files
- `Sn2FixRuleEngine.hpp` (header-only scaffold)

### Status
Header-only scaffold. Implementation requires JSON parser + integration into the dup function's logic.

### Future file format
```json
{
  "rules": [
    {
      "when": { "ps_crc": "0x9d14fcf0", "eye": "right" },
      "then": {
        "swap_cb": [{"root": 3, "from": "auto", "to": "synth(donor=0x4d44ce74)"}],
        "redirect_rtv": "mirror",
        "no_viewport_shift": true
      }
    },
    {
      "when": { "ps_crc": "0x37558de4", "eye": "right" },
      "then": { "redirect_srv": [{ "slot": 5, "to": "mirror_srv" }] }
    }
  ]
}
```

### Goal
Reduce per-fix engineering effort from "edit C++ + rebuild" to "edit JSON + live-reload".

---

## Sn2ResourceTimeline

**Purpose**: For each tracked resource, record every write and read event (PSO, eye, frame, event-in-frame). Answer "what wrote IntegratedLightScattering on frame N event 7245?".

### Files
- `Sn2ResourceTimeline.hpp` / `.cpp`

### Env vars
- `UEVR_SN2_RES_TIMELINE_DIR=<path>` — output dir
- (Resources tracked via `register_track(ID3D12Resource*)` at runtime — not env-configurable currently)

### How it works
1. Caller registers resource pointers to track via `register_track`
2. At each draw/dispatch, caller calls `note_access(resource, ps_crc, cs_crc, eye_bucket, is_write)`
3. Events accumulated in a per-resource vector
4. Flushed to disk every 600 frames as `<dir>/res_<ptr>.json`

### JSON output shape
```json
{
  "resource_ptr": 12345678,
  "event_count": 87,
  "events": [
    {"ps_crc": 2638139252, "eye": 1, "frame": 100, "event_in_frame": 234, "kind": "write"},
    {"ps_crc": 928739492, "eye": 1, "frame": 100, "event_in_frame": 567, "kind": "read"},
    ...
  ]
}
```

---

## Sn2GpuCounters

**Purpose**: Per-PSO GPU timing via D3D12 timestamp queries. Identify slow draws.

### Files
- `Sn2GpuCounters.hpp` / `.cpp`

### Env vars
- `UEVR_SN2_GPU_COUNTERS=1` — enable
- `UEVR_SN2_GPU_COUNTERS_DUMP=<path>` — output JSON (default `C:\tmp\sn2_gpu_counters.json`)
- `UEVR_SN2_GPU_COUNTERS_DUMP_EVERY_N_FRAMES=<N>` — dump frequency (default 600)

### How it works
1. Allocate `ID3D12QueryHeap` (TIMESTAMP, 65536 queries) + readback buffer
2. At each draw, `EndQuery(TIMESTAMP)` records end-of-draw timestamp
3. Per-PSO accumulator tracks draw count + total ticks
4. Every N frames, dump as JSON + reset

### JSON output shape
```json
{
  "frame_count": 600,
  "timestamp_freq": 10000000,
  "psos": {
    "0x9d14fcf0": {
      "draws": 4267,
      "total_gpu_ticks": 12345678,
      "total_gpu_ms": 1.234,
      "ticks_per_draw_avg": 2893
    },
    ...
  }
}
```

### Python helper
`tools/sn2_gpu_counters_top.py` — sort by total ms, draws, or ticks-per-draw and print top N.

---

## Sn2PixelHistory

**Purpose**: For any (x, y) pixel, list every draw that covered it. The RenderDoc "pixel history" equivalent.

### Files
- `Sn2PixelHistory.hpp` / `.cpp`

### Env vars
- `UEVR_SN2_PIXEL_HISTORY=1` — enable
- `UEVR_SN2_PIXEL_HISTORY_DUMP=<path>` — query result output (default `C:\tmp\pixel_history.json`)
- `UEVR_SN2_PIXEL_HISTORY_TRACK_PSOS=<csv>` — only track these PSOs (empty = all)
- `UEVR_SN2_PIXEL_HISTORY_MAX_DRAWS=<N>` — ring buffer size (default 10000)

### How it works
1. At each `draw_indexed_instanced`, `note_draw` records the draw's viewport + scissor + RTV0 into a ring buffer
2. Every 60 frames, check `C:\tmp\pixel_history_query.txt` (trigger file)
3. If present, parse "x,y", scan ring for draws whose viewport+scissor cover that pixel
4. Write results as JSON to `C:\tmp\pixel_history_query_out.json`
5. Delete the trigger file

### Limitations vs RenderDoc
- "Covered" = pixel inside viewport rect AND scissor rect
- Doesn't track per-pixel depth occlusion or alpha blending (would need shader injection)
- Shows ALL draws covering the pixel; RenderDoc shows only the winner

### Python helper
`tools/sn2_pixel_history_query.py` — write trigger + display sorted results, optionally filtered by eye/PSO.

### Example
```bash
python sn2_pixel_history_query.py --x 540 --y 360 --filter-eye 2
# Lists right-eye draws covering pixel (540, 360)
```

---

## Sn2MeshDump

**Purpose**: For configured PSOs, dump VB/IB binding metadata at draw time. Foundation for future actual byte capture + OBJ/PLY conversion.

### Files
- `Sn2MeshDump.hpp` / `.cpp`

### Env vars
- `UEVR_SN2_MESH_DUMP_DIR=<path>` — output dir
- `UEVR_SN2_MESH_DUMP_PS_CRCS=<csv>` — target PSOs (required)
- `UEVR_SN2_MESH_DUMP_MAX=<N>` — max dumps per PSO (default 8)

### Output JSON shape
```json
{
  "ps_crc": 2638139252,
  "pso_ptr": 2733678500144,
  "seq": 1,
  "draw": {
    "index_count": 192,
    "instance_count": 1,
    "start_index": 0,
    "base_vertex": 0
  },
  "vertex_buffers": [
    {"slot": 0, "gpu_va": 12345, "size_in_bytes": 4608, "stride_in_bytes": 24}
  ],
  "index_buffer": {"gpu_va": 67890, "size_in_bytes": 384, "format": 57}
}
```

### Future work
- Hook resource readback to dump actual VB/IB bytes
- Python tool to convert to OBJ/PLY/glTF for viewing in Blender etc.
- Currently `mesh_dump_inspect.py` lists metadata only.

### Python helper
`tools/mesh_dump_inspect.py` — list mesh dumps + detail first one.

---

## Sn2FixRuleEngine (Phase Y full impl)

**Purpose**: Declarative JSON fix rules. Replaces per-fix C++ with config edits + live reload.

### Files
- `Sn2FixRuleEngine.hpp` / `.cpp`

### Env vars
- `UEVR_SN2_FIX_RULES_FILE=<path>` — JSON file with rules; polled every 60 frames for mtime-based live reload

### File format
```json
{
  "rules": [
    {
      "when": {"ps_crc": "0x9d14fcf0", "eye": "right"},
      "then": {
        "swap_cb": [{"root": 3, "to": "synth(donor=0x4d44ce74)"}],
        "redirect_rtv": "mirror",
        "no_viewport_shift": true
      }
    },
    {
      "when": {"ps_crc": "0x37558de4", "eye": "right"},
      "then": {"redirect_srv": [{"slot": 5, "to": "mirror_srv"}]}
    }
  ]
}
```

### `when` clause
- `ps_crc` (required): hex string or number
- `eye`: `"left"` / `"right"` / `"both"` / omit (defaults to both)

### `then` clause
- `swap_cb`: array of `{root, to}` — `to` values: `"synth(donor=0xCRC)"`, `"left"`, `"delta=-10240"`
- `redirect_rtv`: `"mirror"`
- `redirect_srv`: array of `{slot, to}` — `to` values: `"mirror_srv"`, `"left"`
- `no_viewport_shift`: bool

### Public API
```cpp
void refresh_rules();                          // Call once per Present
const Rule* find_rule(uint32_t ps_crc, int eye_bucket);
size_t rule_count();
const std::string& rules_file_path();
```

### Goal
Replace future per-fix C++ engineering with config edits.

---

## Sn2DescriptorLineage (Phase Y full impl)

**Purpose**: Track every descriptor from CreateXxxView → CopyDescriptors chain → bound location. Reverse-lookup any bound CPU handle back to its original resource.

### Files
- `Sn2DescriptorLineage.hpp` / `.cpp`

### Env vars
- `UEVR_SN2_DESCRIPTOR_LINEAGE=1` — enable
- `UEVR_SN2_DESCRIPTOR_LINEAGE_DUMP=<path>` — dump path (default `C:\tmp\desc_lineage.json`)
- `UEVR_SN2_DESCRIPTOR_LINEAGE_DUMP_EVERY_N_FRAMES=<N>` — dump frequency (default 600)

### Public API
```cpp
void note_create(CreateKind, ID3D12Resource*, SIZE_T cpu_handle);
void note_copy(SIZE_T src, SIZE_T dst);
void note_copy_range(SIZE_T src_start, SIZE_T dst_start, UINT count, UINT stride);
ID3D12Resource* trace_back(SIZE_T cpu_handle);  // reverse lookup
const LineageEntry* lookup_lineage(SIZE_T cpu_handle);
void on_present();
```

### Why this matters
Phase F identified gaps in `sn2_descriptor_registry`: many bindless slots return null from `lookup_resource_by_cpu_ptr`. The lineage tracker provides a SEPARATE chain-based reverse lookup that's more robust to gaps — if any link in the chain was tracked, we can walk back from the bound handle to the original resource.

### Integration
Caller hooks into `Create*View` and `CopyDescriptors` and calls `note_create` / `note_copy(_range)`. At consumer scan time, use `trace_back(bound_cpu_handle)` instead of (or in addition to) the registry's direct lookup.

---

## Sn2DebugColorOverride (Phase Z — RD-gap module)

**Purpose**: At runtime, for configured PS CRCs, replace the bound PSO with a substitute whose pixel shader outputs solid magenta. Visualizes EXACTLY which pixels a given PSO touches — the inverse of `Sn2MagicInk` (which makes them disappear).

### Files
- `Sn2DebugColorOverride.hpp` / `.cpp`

### Env vars
- `UEVR_SN2_DEBUG_COLOR_OVERRIDE_FILE=<path>` — file containing PS CRCs to override (live-reloaded every 60 frames)
- `UEVR_SN2_DEBUG_COLOR_OVERRIDE_EYE=left|right|both`

### How it works
1. At every `CreateGraphicsPipelineState`, cache the full desc (bytecode blobs + input layout deep-copied) by PSO pointer
2. At draw time for a configured target PSO:
   - Look up cached replacement; if absent, clone original desc with PS replaced by a compiled `float4 main() { return float4(1,0,1,1); }` shader
   - Call `SetPipelineState(replacement)` before draw
3. Original draw fires with magenta PS
4. Restore original after

### Use cases
- "Which pixels does PS X actually draw?" — answer: magenta ones
- Combined with Magic Ink: skip everything else, override target → SOLELY magenta from target PSO visible
- Find producer of a visible region without needing per-pixel history queries

### Public API
```cpp
bool should_override(uint32_t ps_crc, int eye_bucket);
void note_create_graphics_pso(const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, ID3D12PipelineState*);
ID3D12PipelineState* get_or_create_replacement(ID3D12Device*, ID3D12PipelineState* original);
```

### Limitations
- Requires `note_create_graphics_pso` to be wired into the CreateGraphicsPipelineState hook to capture original descs
- Stream-PSO API (`CreatePipelineState`) not yet wired — only classic desc
- The replacement uses the same root signature as the original, so root parameter bindings are preserved

---

## Sn2CaptureSidecar (Phase Z — UEVR↔RD bridge)

**Purpose**: Emit a JSON sidecar describing UEVR's current live state alongside an `Sn2EyeScreenshot` capture. Python tooling can then translate this to equivalent RenderDoc replay-time overrides via the fork's `SetBufferOverrideGPU` API — so fix iteration becomes "modify sidecar, re-replay" instead of "rebuild + restart game."

### Files
- `Sn2CaptureSidecar.hpp` / `.cpp`

### Env vars
- `UEVR_SN2_CAPTURE_SIDECAR_DIR=<path>` — output directory

### Sidecar JSON shape
```json
{
  "schema_version": 1,
  "emitted_at_iso8601": "2026-05-22T14:33:00Z",
  "seq": 42,
  "pid": 12345,
  "dup_cfg_file": "E:\\Github\\UEVRJ\\artifacts\\sn2_dup_cfg_restricted.json",
  "magic_ink_file": "C:\\tmp\\ink_skip.txt",
  "debug_color_override_file": "C:\\tmp\\debug_color_override.txt",
  "env_vars": {
    "UEVR_SN2_DUP_CONFIG_FILE": "...",
    "UEVR_SN2_DUPLICATE_SLW_BASEPASS_RIGHT": "1"
  },
  "synth_donor": {"donor_crc": "0x4d44ce74", "donor_root": "3", "size": "4096"}
}
```

### Bridge workflow
1. Game running with UEVR live patches active
2. **Simultaneously**: trigger an RD capture (F12 in qrenderdoc) AND a UEVR sidecar emission
3. Python `tools/uevr_rd_bridge.py` reads the sidecar, generates an RD override plan
4. Plan applied to RD capture via qrenderdoc Python — same visual fix as live UEVR
5. Iterate by modifying the plan + re-replaying (seconds, not minutes)

### Public API
```cpp
void emit(uint64_t seq);   // Write sidecar to <dir>/sidecar_seq<NNNN>.json
```

### Python companion
`tools/uevr_rd_bridge.py` — loads sidecar + dup_cfg, emits an RD override plan as JSON + a runnable script for qrenderdoc's Python shell.
