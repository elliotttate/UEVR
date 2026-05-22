# UEVR vs RenderDoc-Fork — Gap Analysis

The RenderDoc fork at `E:\Github\renderdoc` (branch `uevr-nsight-automation`) has substantial capabilities built specifically for the same SN2/UEVR work. This doc inventories what they have that we don't, ranked by usefulness for obscure-bug hunting.

## What they have, we don't

### Tier 1 — would unlock new debugging classes

**1. Replay-time GPU buffer override (`SetBufferOverrideGPU`)**
The single biggest gap. RD lets you take a captured frame, patch any buffer's bytes (via UPLOAD heap + CopyBufferRegion), and re-replay — visual output reflects the patch. This means you can test "what if View CB offset +880 was 1 instead of 0?" in **seconds**, not the 2-5 minutes per live-game iteration cycle we have. This is exactly the capability that found their SN2 headline finding (1-vs-0 uint at offset +880).

UEVR equivalent would need: a capture mechanism + replayer (massive). Better path: build a UEVR↔RD bridge.

**2. Pixel lineage with binding snapshot (full version)**
Our `Sn2PixelHistory` tracks which draws cover a pixel via viewport/scissor. RD's `pixel_lineage` goes further:
- For each draw that wrote to the pixel, snapshot the BOUND state (CBVs/SRVs/UAVs)
- Resolve those bindings to actual resources
- Optionally invoke `DebugPixel` to trace shader execution
- Answer "this pixel is THIS color because at event X the shader sampled SRV Y which contained Z"

We have the parts (state inspector + pixel history) but not the orchestration.

**3. Replay probes** (`force_magenta_ps`, `swap_resource`, `override_cbv_range`)
Pre-built mutation kit:
- Replace a PS with one that outputs solid magenta → see exactly which pixels that PSO touches
- Swap resource IDs at replay (e.g., right-eye t9 → left-eye t9)
- Override a CBV byte range
- Bind a neutral 1×1 texture
- Skip a draw via discard-only PS
- ROI sampling: per-channel min/max/mean before+after each mutation

Result: **automated probe-and-measure** loop. Currently we manually iterate via Magic Ink + screenshot.

**4. CB symbolic decoder via shader reflection**
Our `decode_view_cb.py` hardcodes UE5's `ViewUniformShaderParameters` layout. RD's `cbv_decode` uses `GetCBufferVariableContents` to decode ANY CBV per the actual shader's reflection metadata — no hardcoded layout needed. Works for material CBs, lighting CBs, any UE5 uniform.

**5. Shader debugger** (`DebugPixel` / `DebugThread`)
Step through HLSL/DXIL execution for a specific pixel. Side-by-side compare same pixel at two events. We have nothing equivalent.

### Tier 2 — high value for SN2-style work

**6. Geometry stage inspection** (`GetPostVSData`)
Compare VS-input → VS-output → PS-input deltas between events. We have only mesh VB/IB views.

**7. `compare_eyes` orchestrator**
Auto-pair LEFT/RIGHT events, run `event_diff` + `eye_image_diff` + `geometry_diff` (+ optional `shader_debug`), rank by divergence. Our `eye_pair_correlator.py` only does the pairing.

**8. `explain_pixel` end-to-end**
One script: pixel/eye/lineage/uevr/probe in sequence, emit comprehensive report. We have the components, not the orchestrator.

**9. Capture diff**
Two captures → action counts, PSO usage, binding deltas, **first divergent event**. Massively useful for "did my fix change the right thing?"

**10. `descriptor_history`** (per-(heap,slot) timeline)
Poll `GetDescriptors` at every event, build a complete timeline of which descriptor was where when. Our `Sn2DescriptorLineage` tracks via hook chain (has gaps); their approach is post-hoc from a capture (lossless).

**11. RDG pass classifier**
Regex-based UE5 RDG pass categorization ("SingleLayerWater", "TranslucentLightingVolume", "ExponentialFog", etc.). We have `Sn2MaterialNameHook` (proxies/meshes) but no pass-level semantic tagging.

**12. Resource statistics on export**
Per-channel min/max/mean in DDS/PNG/EXR exports. Our PPM dumps lack stats.

### Tier 3 — nice to have

**13. `export_cpp` (RD's version)**
Much more complete than our `Sn2FrameCppExport`: covers descriptor handle tracking, RootSig + PSO desc reconstruction, shader bytecode loading via `LoadBlob("shaders/<hash>.cso")`. Our scaffold writes most chunks as TODOs.

**14. Pipeline-stats counters** (`FetchCounters`)
VS invocations, PS invocations, RT pixels written. Our `Sn2GpuCounters` only has timestamps.

**15. Automation HTTP server**
JSON-RPC over HTTP — external tools (incl. LLMs) drive analysis over the network. We have file-trigger only.

**16. Per-resource barrier history**
State-transition timeline per resource. We have access timeline (Sn2ResourceTimeline), not state transitions.

**17. Launch profiles** (formalized JSON)
Deterministic launch profiles: exe, args, env, capture trigger. We have launch_*.ps1 scripts but no schema.

**18. Driver-recorded descriptor writes** (`GetDescriptorWrites`)
Direct dump of every `Device_CopyDescriptors[Simple]` + `Create*View` chunk. Lossless. We track via hooks (has gaps).

## What UEVR has, RD doesn't

These are our durable wins. Keep building on these:

| Capability | Why we're ahead |
|---|---|
| **Live patching of running game** | RD is read-only at replay |
| **Magic Ink with file-trigger live reload** | RD has no equivalent for live binary search |
| **Donor snapshot CB synthesis** | Runtime CB transformation |
| **Mirror resource allocation** | Parallel UAV/SRV/RTV at runtime |
| **Live SRV/UAV/RTV redirect** | The actual fix mechanism, not just inspection |
| **Stereo bucket awareness throughout** | Built into UEVR's hooks |
| **PS-CRC stable IDs** | Works across game restarts |
| **Continuous diagnostic** | Runs hours, not per-capture |
| **Fix DB with emit-launch** | Curated, combinable fix sets |

## Strategic recommendations

### Path A — Build live equivalents in UEVR (build-first)

Highest ROI to build in UEVR:

1. **Sn2DebugColorOverride** — at draw time, for configured PS CRCs, swap to a PS that outputs solid magenta. Sees coverage in real-time. Parallel to Magic Ink but additive. (~200 LOC)

2. **Shader reflection-based CB decoder** — at PSO create time, use `D3DReflect` (or dxc's reflection API) to extract CB layouts. At CB dump time, write `{ varname: value }` JSON instead of raw bytes. Works for any shader. (~400 LOC)

3. **Capture log diff** — Python tool: diff two UEVR session log files, find first divergent event (PSO dispatch, RTV bind, etc.). Identifies "what changed between this session and the previous." (~150 LOC Python)

4. **Pipeline-stats GPU counters** — extend `Sn2GpuCounters` to also run `D3D12_QUERY_TYPE_PIPELINE_STATISTICS` queries. Per-PSO breakdown of VS/PS/CS invocations. (~150 LOC)

5. **HTTP automation server** — small HTTP server in UEVR exposing the existing file-trigger commands. Lets Claude/external tools drive analysis without writing trigger files. (~300 LOC)

6. **Per-resource state-transition timeline** — extend `Sn2ResourceTimeline` to also track resource state via `ResourceBarrier` hook. Answers "what state was this resource in at event X?" (~200 LOC)

7. **`explain_pixel` orchestrator** — Python tool chaining `Sn2PixelHistory` query → `Sn2StateInspector` dump for each hit → output unified report. (~200 LOC Python)

### Path B — Integrate with RD fork (use-first)

Some capabilities are better consumed via the existing RD fork:

1. **Replay-time GPU buffer override** — use RD's `SetBufferOverrideGPU` instead of building our own replay engine. Workflow:
   - Capture a frame via RD while UEVR is running with `restricted-dup-cfg`
   - Save current UEVR state (dup_cfg + env vars + live patches) as JSON sidecar
   - Python script: load RD capture + UEVR sidecar, apply equivalent overrides via `SetBufferOverrideGPU`
   - Iterate on alternative patches in seconds via replay

2. **Pixel lineage with DebugPixel** — call RD's API from a Python helper

3. **Geometry stage inspection** — use RD's `GetPostVSData`

4. **Capture diff** — use RD's `capture_diff`

The integration needs a UEVR↔RD bridge: a JSON sidecar emitted by UEVR alongside each captured frame that lists the current dup_cfg, active mirrors, magic-ink skips, and synth donor state. Python tooling then translates this to equivalent RD overrides.

### Recommended next session — implement just 3 things

Out of all the above, the three highest-ROI items for the next 1-2 sessions:

1. **Shader reflection-based CB decoder** (Path A #2). Today's `decode_view_cb.py` is hardcoded; an automatic decoder would have made finding the +880 byte trivial.

2. **Sn2DebugColorOverride** (Path A #1). "Show me which pixels this PSO touches" is the inverse of Magic Ink. Used together, they catalog every PSO's visual contribution.

3. **UEVR↔RD bridge** (Path B integration). Specifically: emit a JSON sidecar at `Sn2EyeScreenshot` capture time with current dup_cfg + active mirrors. Then `replay_probe.py` can be run against an RD capture taken at the same moment, with overrides matching UEVR's live state. This unlocks all of RD's replay-time mutation for fix experimentation.

## Honest assessment

We've built a strong **live injection / live fix** toolkit. The RD fork is a strong **post-hoc analysis + replay-time experimentation** toolkit. They're complementary, not competitors.

The biggest single capability gap is **replay-time buffer override** for fix iteration speed. We could either:
- Build a from-scratch replay engine in UEVR (~10K LOC, multi-week project)
- Use RD's via a bridge (~500 LOC bridge, 1-2 days)

The bridge is the right call.

For everything else, our live-patching is more powerful than RD's read-only replay. Adding the few items in Path A (debug color, shader reflection, perf counters, HTTP server, explain_pixel orchestrator) closes most of the remaining gap while preserving UEVR's unique strengths.
