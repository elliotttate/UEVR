# Stereo Forensics Layer Status - 2026-05-23

## Purpose

The goal is to turn the current pile of SN2/UEVR probes into one reusable
forensics system:

- answer what differs between left and right eyes
- answer what produced a resource/view/slice/mip before a draw sampled it
- rank which shader/draw/descriptor/CB intervention actually changes the image
- emit durable fixes instead of hand-writing one-off SN2 hooks

This is now started as a general UEVR D3D12 facility, not another SN2-only
hook.

## Current implementation

New core files:

- `src/render/StereoForensics.hpp`
- `src/render/StereoForensics.cpp`

Main integration points:

- `src/hooks/D3D12Hook.cpp`
- `src/render/RenderDiagnosticsCAPI.hpp`
- `src/render/RenderDiagnosticsCAPI.cpp`
- `src/render/RenderAnalysisExport.cpp`
- `CMakeLists.txt`

Build status:

```text
cmake --build build --target uevr --config Release -- /m /v:minimal /clp:Summary
Build succeeded.
Output: E:\Github\UEVRJ\build\bin\uevr\UEVRBackend.dll
```

The layer is disabled by default. Enable it with:

```bat
set UEVR_STEREO_FORENSICS=1
```

`UEVR_STEREO_FORENSICS=1` now forces the D3D12 command-list diagnostic hooks
needed for draw/dispatch work events, even if an older launch script sets
`UEVR_ENABLE_D3D12_DIAGNOSTIC_COMMAND_LIST_HOOKS=0`. Use
`UEVR_STEREO_FORENSICS_BIND_ONLY=1` only as a crash-isolation opt-out; bind-only
captures are not useful for left/right diffing unless
`UEVR_STEREO_FORENSICS_BIND_ONLY_RECORD_PSO=1` is also set for a targeted PSO
bind investigation.

Optional output directory:

```bat
set UEVR_STEREO_FORENSICS_DIR=C:\tmp\uevr_forensics
```

Default output is:

```text
C:\tmp\uevr_forensics\session_YYYYMMDD_HHMMSS_pidNNNN\
```

Current output files:

- `manifest.json`
- `events.jsonl`
- `resources.json`
- `descriptors.json`
- `descriptor_heaps.json`
- `lineage.json`
- `eye_diff.json`
- `experiments.json`
- `frames/frame_<N>_summary.json`

The render analysis export bundle now also includes `stereo_forensics.json`,
which points at the active session paths and reports whether forensics and
experiments are enabled.

Stability guardrail: all public `StereoForensics` record/query entrypoints are
now exception-isolated. A JSON/filesystem/allocation edge case logs and drops
the diagnostics event instead of unwinding through the D3D12 hook into the game.

Flood guardrails: forensics is now a bounded burst sampler by default. It
captures every 30th frame, stops after 16 captured frames, caps captured frames
at 5000 events, caps `set_pso` binds at 256 per frame, caps root binds at 2048
per frame, and stops the session around 80k events or 128 MiB of JSONL. The
current limiter state is written into `manifest.json` and each
`frames/frame_<N>_summary.json`.

Load guardrail: the D3D12 draw/root hooks now query
`StereoForensics::is_capturing_this_frame()` before building descriptor-read
detail or recording legacy D3D12Diagnostics draw/root rows. On skipped frames
and after the burst stops, the hook path is cheap by default. Set
`UEVR_STEREO_FORENSICS_KEEP_HOOK_DETAIL_ON_SKIPPED_FRAMES=1` only when a legacy
D3D12Diagnostics capture explicitly needs full-session draw/root detail.

Scene-targeting guardrail: create `C:\tmp\uevr_forensics_arm.txt` while the game
is in the scene you care about to re-arm a fresh capture burst. The sentinel
resets `captured_frames`, clears the stopped latch, and starts capturing on the
next frame.

## Checklist against the full plan

### 1. Unified D3D12 frame database

Status: **partially implemented, usable first pass**.

Implemented:

- frame begin events
- descriptor heap creation
- committed resource creation
- placed resource creation
- resource descriptors: dimensions, format, flags, heap pointer, heap offset
- alias group assignment for placed resources using heap plus heap offset
- CBV/SRV/UAV/RTV/DSV descriptor creation
- SRV/UAV/RTV/DSV view metadata, including mip and array slice where available
- descriptor copies
- descriptor heap binds
- root CBV/SRV/UAV/table/root-constant binds
- render-target and depth-stencil binds
- resource barriers, including aliasing barrier fields
- RTV clears
- copy/resolve/resource-copy events
- explicit `SetPipelineState` bind events
- draw, indexed draw, dispatch, and mesh dispatch events
- PSO/root-signature/shader CRCs where UEVR already tracks them
- stable per-session IDs for events, resources, PSOs, shaders, and descriptor
  views
- viewport/scissor snapshots
- bound CBV hashes for root CBVs
- descriptor reads/writes resolved through the D3D12 diagnostics descriptor map

Still missing:

- explicit shader bind events separate from draw/dispatch snapshots
- full root signature layout normalization
- complete descriptor-table range reconstruction without depending on existing
  `D3D12Diagnostics`
- pass tags and UE/RDG names
- stable cross-run IDs that survive pointer reuse across game launches
- binary or SQLite storage for large captures

Current value:

This already replaces a lot of ad hoc text scraping for D3D12 facts. It gives a
single JSONL stream and normalized side tables for resources/descriptors/heaps.

### 2. Automatic left/right diff engine

Status: **first pass implemented**.

Implemented diff checks:

- left/right event count mismatches
- PSO or shader differences inside a paired group
- graphics CBV hash differences at the same root slot
- descriptor missing on right
- same resource with different SRV/UAV/RTV slice
- same descriptor shape but different physical resource
- descriptor resource differences
- producer lineage differences
- per-group pair score, confidence, and matching reasons

Pairing today groups by:

- event kind
- root signature hash
- viewport dimensions
- draw/dispatch arguments
- render-target descriptor shape
- then chooses the highest-scored left/right pair inside the group

Still missing:

- fuzzy matching by shader family/permutation cluster
- event-neighborhood matching
- UE View index as a source-of-truth pairing key
- pass-name matching
- resource-lineage similarity scoring
- material/object grouping
- confidence scores and ranked root-cause summaries
- full root-cause ranking beyond pair confidence

Current value:

It should catch the common SN2/UEVR failures immediately: missing right-eye
draws, right-eye PSO drift, CB divergence, descriptor slot differences,
Texture2DArray slice differences, and producer mismatch.

### 3. Resource lineage DAG with alias awareness

Status: **started, good enough for common questions, not a full DAG yet**.

Implemented:

- tracks latest producer per resource
- tracks latest producer per descriptor view key
- keeps bounded writer history
- attaches producer info to descriptor reads
- records read edges from draw/dispatch events to producer events
- records write edges for RTV/UAV/copy/clear style operations
- tracks descriptor copies
- records placed resource heap and heap offset
- assigns alias groups from heap plus heap offset
- records aliasing barriers
- records alias barrier generation on affected resources
- includes SRV/UAV/RTV slice/mip metadata in descriptor records
- stamps resources, descriptor reads, writes, lineage edges, and DB rows with
  `resource_generation` plus `resource_instance_uid` so COM pointer reuse is
  visible instead of silently merging identities
- classifies sampled resources as `frame_produced_view`,
  `frame_produced_resource`, `history_produced_view`,
  `history_produced_resource`, `static_or_imported`,
  `created_unwritten_this_frame`, or alias-related where known
- records first/last seen and first/last writer frames for resources in the
  runtime JSON and SQLite DB
- records `ID3D12Resource::Release`/final-release observations when forensics is
  enabled. The release hook is gated behind `UEVR_STEREO_FORENSICS`, uses the
  pointer only as an identity key after calling the real `Release`, and
  tombstones upload-buffer cache entries instead of compacting vectors.

Still missing:

- true graph query API
- lifetime intervals per resource alias group
- complete lifetime intervals per resource alias group
- transient RDG lifetime reconstruction
- copy subregion tracking
- full multi-writer query semantics beyond the bounded history dump

Current value:

The system can now answer a useful first version of:

```text
This draw sampled t5. What resource/view did that descriptor point at, and
what was the latest known producer of that resource?
```

It is not yet strong enough to definitively distinguish every RDG transient
reuse case without the UE/RDG bridge.

### 4. Automated intervention runner

Status: **minimal live runner implemented**.

Enable with:

```bat
set UEVR_STEREO_EXPERIMENTS=1
set UEVR_STEREO_EXPERIMENTS_FILE=path\to\rules.json
```

Current supported actions:

- skip draw
- skip indexed draw
- skip dispatch
- skip mesh dispatch
- color override for indexed graphics draws, reusing `Sn2DebugColorOverride`
  when a v2 rule uses `{"type":"color_override"}`
- `swap_cbv_left_to_right`
- `swap_descriptor_from_left`
- `force_srv_array_slice`
- `neutralize_texture`

Current rule fields:

- `name`
- `enabled`
- `action`
- `kind`
- `eye`
- `ps_crc`
- `cs_crc`
- mutation actions also use `root`, `slot`, `array_slice`, `source_eye`, and
  `target_eye` inside the action object

Current output:

- experiment hit counts and event records in `experiments.json`
- experiment observations in `experiments.json` with outcomes such as
  `source_snapshot`, `applied`, `missing_source`, `stale_source`, and
  `apply_failed`. Skip hits and color-override applies are counted too, so null
  visual deltas from rules that never matched are marked untrusted.

New offline tools:

- `tools/stereo_forensics_query.py` queries a session bundle by issue, event,
  shader, lineage slot, or alias group.
- `tools/stereo_forensics_db.py analyze-session` ingests a capture and
  materializes event pairs, lineage paths, resource lifetimes, and ranked
  suspects into SQLite.
- `tools/stereo_forensics_experiment.py` emits simple v2 experiment rules and
  scores left/right PPM captures by ROI delta.
- `tools/stereo_forensics_run_experiments.py` turns ranked DB suspects into
  probe or mutation candidate rule files. It tags executable actions and falls
  back unsupported prescriptions to color probes unless explicitly told to emit
  skeletons. Runtime action capabilities come from
  `docs/sn2/sn2_hook_manifest.generated.json`.
- `tools/stereo_forensics_ab_loop.py` drives baseline/trial rule replay using
  the SN2 eye-screenshot trigger, scores same-eye deltas, carries runtime apply
  confirmation into the score, derives `--roi auto` from the candidate
  viewport/scissor, waits for runtime rule reload when `experiments.json` is
  supplied, and can promote likely-causal rules into the DB.
- `tools/stereo_forensics_compile_rule.py` compiles a captured event into a
  durable v2 rule skeleton.
- `tools/stereo_forensics_shader_semantics.py` produces lightweight DXBC/DXIL
  shader semantic summaries, extracts conservative SM4/SM5 DXBC reflection and
  token facts when RDEF/SHEX/SHDR chunks are present, uses `dxil-patch` for
  richer DXIL disassembly when available, and can import roles into the DB.

Still missing:

- PS bytecode replacement
- CB byte patch
- duplicate draw into right bucket
- duplicate dispatch
- clamp CB value/range
- automatic per-rule ROI selection for `sample-json` mode

Current value:

The forensics layer now has a control plane for interventions, a v2 rule file
shape, live skip/color probes, closed-loop screenshot A/B scoring, persistent
suspect ranking, candidate experiment generation, and a first generic D3D12
mutation runner for CBV swaps, descriptor swaps, forced SRV array slices, and
same-shape null-SRV texture neutralization.
Descriptor sources are snapshotted into UEVR scratch descriptors, CBV sources
are snapshotted into UEVR upload memory, and stale-source applications are
rejected instead of scored as real failures. Duplicate-work and bytecode/byte-
patch actions still need deeper replay or patch infrastructure.

The SN2 eye screenshot trigger can also request runtime C-API ROI sidecars with
`sample=x,y,w,h`; the A/B loop exposes this as `--score-mode sample-json
--sample-roi x,y,w,h` for in-engine region summaries instead of Python PPM
pixel loops.

### 5. Shader semantic analysis service

Status: **offline first pass implemented; runtime event enrichment not wired yet**.

Existing related pieces:

- shader hunter
- `Sn2PsoBytecodeDumper`
- shader override registry CRC tracking
- DXBC/DXIL dumps from earlier SN2 work
- `tools/stereo_forensics_shader_semantics.py`

Implemented offline:

- parse DXBC container metadata and CRCs
- identify DXIL containers
- when `dxil-patch` is supplied, disassemble DXIL and list createHandle,
  cbufferLoad, sample/textureLoad, store, branch, and discard/clip hints
- for SM4/SM5 DXBC, parse RDEF reflection where present, including constant
  buffers, variable byte ranges, SRV/UAV/sampler bindings, input/output
  signatures, and conservative SHEX/SHDR opcode counts for sample/load/branch/
  discard/output hints
- import semantic summaries into SQLite as shader roles such as
  `visible_texture_consumer`, `cb_driven`, `branch_gated_texture_path`, or
  `has_reflection_bindings`

Still needed:

- validate DXBC token/reflection parsing across a larger set of UE shaders
- map exact sampled SRV/UAV slots through root-signature binding resolution
- list output targets and depth writes
- identify discard/clip paths
- identify branch predicates tied to CB values
- identify texture samples gated by branches
- cluster similar PSO permutations
- attach semantic summaries to `events.jsonl` and `eye_diff.json`

This remains one of the highest-value next layers because it directly addresses
the "many cycled shaders do nothing" problem. The scanner should rank shaders
by whether they can affect visible pixels, not just by whether they exist.

### 6. UE/RDG semantic bridge

Status: **not implemented in this layer yet**.

Existing related pieces:

- UE SDK wrappers
- prior SN2 material hook work
- source review of UE 5.6.1 RDG/resource pooling behavior
- D3D12 placed-resource and aliasing capture now in Stereo Forensics

Still needed hooks/enrichment:

- `FRDGBuilder::AddPass`
- RDG texture creation/import
- RDG pass execution
- `FSceneRenderer::Views[]`
- base pass setup
- material pass setup
- pooled render target allocation/reuse
- transient heap placement naming

Target output:

```text
BasePass, View[1], material X, mesh Y, reads SkyAtmosphere/Fog LUT,
writes SceneColor right.
```

instead of only:

```text
PS DE7C3822, root5, t5
```

This is the bridge that will make the D3D12 stream deterministic for UE games,
especially under ISR/MultiView/RDG pooling.

### 7. Material/object attribution

Status: **not implemented in this layer yet**.

Existing related pieces:

- SN2 material-name hook experiments
- UE SDK object access
- shader hunter and draw logging

Still needed:

- material name per draw where recoverable
- primitive/mesh/component name per draw where recoverable
- pass type classification: depth, base pass, translucency, post, fog, water,
  VSM, shadow, compute, etc.
- visible-output classification: color, depth-only, stencil-only, UAV-only,
  intermediate-only, overwritten later, offscreen/culled

This is what will explain why many shader hunter entries appear inert when
cycled: they may be depth-only, shadow-only, offscreen, masked, overwritten, or
only feeding an intermediate.

### 8. Fix rule compiler

Status: **not implemented, but the storage/control path is started**.

Existing related pieces:

- `Sn2FixRuleEngine`
- debug color override
- shader override registry
- new experiment rule loading/hit counting in `StereoForensics`

Still needed:

- promote a successful experiment into a durable rule
- rule guards:
  - shader hash
  - root signature hash
  - pass name
  - view index
  - resource descriptor
  - descriptor slot
  - format/dimensions
  - heap alias group
  - UE version/game executable hash
- rule actions:
  - swap descriptor slot
  - force array slice
  - replace shader bytecode
  - patch CB bytes
  - duplicate draw/dispatch
  - skip draw
  - override RTV/DSV
  - insert copy/resource transition

The current experiment runner gives this somewhere to land, but it does not yet
compile or emit reusable fix recipes.

## What this changes for SN2 debugging

Before this layer, each question required a custom probe:

- Is this draw left-only or both-eye?
- Is right using a different PSO?
- Is the SRV the same resource or a different slice?
- Is the texture static, produced this frame, pooled, or aliased?
- Did a descriptor copy hide the original resource?
- Did a skip/override intervention actually hit?

Now those facts are at least being captured into one session bundle. The first
captures should be used to validate schema quality before building more
interventions.

For the current right-eye fog case, the most important immediate tests are:

1. Enable forensics and reproduce one frame near the missing fog.
2. Inspect `eye_diff.json` for the fog consumer draw family.
3. Inspect `lineage.json` for the consumer's SRV slot, especially resource
   dimensions, array slice, producer event, alias group, and whether it is
   static/imported by absence of a frame producer.
4. Use the experiment runner to skip only suspect right-eye or left-eye draws
   and confirm the hit path is correct before adding heavier actions.

## Highest-value next work

### Next 1: Live capture validation

Run a real SN2 capture with `UEVR_STEREO_FORENSICS=1` and verify:

- no exception guard trips during D3D12 init
- `Release`/final-release lifetime events appear only when forensics is enabled
- `lineage.json.validation` counts match expectations for static/imported,
  alias, and history-produced reads
- `neutralize_texture` rules produce `applied` observations before scoring

### Next 2: Upgrade lineage from latest-writer to view DAG

Change lineage keys from mostly resource-level to:

```text
resource + view type + mip + array slice + plane
```

Then keep writer history intervals, not just latest writer. This is required
for RDG transient aliasing and Texture2DArray stereo cases.

### Next 3: Improve automated ROI selection

`--roi auto` uses event viewport/scissor today, and `sample-json` uses a manual
eye-local ROI. Next step is deriving tighter per-rule ROIs from diff hotspots or
the localized issue evidence so small fog/water fixes are not diluted by
whole-frame scoring.

### Next 4: Runtime shader semantic enrichment

The offline parser exists. Next step is attaching its DB rows back onto runtime
events and candidates:

- CB ranges read
- SRV/UAV slots sampled
- output/depth behavior
- branch/discard gates

Then shader hunter can suppress or demote shaders that cannot affect the visible
target.

### Next 5: UE/RDG bridge and broader mutators

Attach pass/view/resource names from UE 5.6.1 where possible, then continue
adding durable actions that need deeper infrastructure: bytecode replacement,
CB byte patch/clamp, duplicate draw/dispatch replay, and copy/transition
insertion.

## Bottom line

Items 1-4 are now functional inside UEVR. Item 5 has an offline first pass.
Items 6-7 remain mostly future work, and item 8 has a shared rule schema,
candidate generation, scoring, and promotion path but still needs the heavier
executor actions.

The important architectural shift is done: new SN2 findings should feed the
Stereo Forensics bundle instead of becoming more isolated one-off logs. The
next milestone is to run a live SN2 capture, validate the new stability/lifetime
signals, and use real output to drive the next schema/intervention upgrades.
