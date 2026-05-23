# Stereo Forensics Knowledge DB Workflow

The database is the source of truth for reusable investigation memory. Markdown
handoffs should reference DB IDs or be generated from DB content; they should
not be the only place a finding exists.

## Default DB

```bat
set UEVR_STEREO_FORENSICS_DB=C:\tmp\uevr_forensics\forensics.db
```

Recommended live-capture env:

```bat
set UEVR_STEREO_FORENSICS=1
set UEVR_STEREO_FORENSICS_DIR=C:\tmp\uevr_forensics
set UEVR_ENABLE_D3D12_DIAGNOSTIC_COMMAND_LIST_HOOKS=1
```

The command-list hook env is now auto-forced by `UEVR_STEREO_FORENSICS=1`, but
setting it explicitly keeps launch logs unambiguous. If `events.jsonl` contains
only bind/create/frame/lifetime events and `eye_diff.json` reports
`work_event_groups: 0`, the capture is bind-only and cannot produce a real
left/right diff.

Default capture limiters are intentionally conservative for live games:
`UEVR_STEREO_FORENSICS_FRAME_STRIDE=30`,
`UEVR_STEREO_FORENSICS_MAX_CAPTURED_FRAMES=4`,
`UEVR_STEREO_FORENSICS_MAX_EVENTS_PER_FRAME=12000`,
`UEVR_STEREO_FORENSICS_MAX_TOTAL_EVENTS=120000`, and
`UEVR_STEREO_FORENSICS_MAX_TOTAL_BYTES=268435456`. Raise these only for a
specific capture. The defaults favor a short dense gameplay burst over a long
startup/menu capture.

To capture the active scene instead of startup/menu frames, re-arm a fresh burst
while the game is already in the target state:

```bat
echo arm > C:\tmp\uevr_forensics_arm.txt
```

The runtime consumes the sentinel, resets the captured-frame burst counter, and
starts the next eligible captured frame immediately. Skipped and stopped frames
also skip expensive descriptor-read/draw-detail collection unless
`UEVR_STEREO_FORENSICS_KEEP_HOOK_DETAIL_ON_SKIPPED_FRAMES=1` is set.
They still keep descriptor/resource side tables and lightweight producer
history current by default, so a later re-armed gameplay burst can classify
static imports, frame-produced targets, and recent RTV/copy producers.

If the UEVR MCP plugin is loaded, the same re-arm can be triggered without a
file:

```bat
curl -X POST http://localhost:8899/api/render/stereo-forensics/arm
```

The MCP tool name is `uevr_render_stereo_forensics_arm`, and the dashboard has
an `Arm Capture` button on the Stereo Forensics card.

All commands also accept:

```bat
--db C:\tmp\uevr_forensics\forensics.db
```

## Required Workflow

Every investigation should leave at least one durable record:

- an ingested capture
- a finding
- evidence linked to a finding
- an experiment score
- a fix rule
- a rejected/superseded hypothesis

## Commands

Initialize:

```bat
python E:\Github\UEVRJ\tools\stereo_forensics_db.py init
```

Ingest a capture:

```bat
python E:\Github\UEVRJ\tools\stereo_forensics_db.py ingest-session C:\tmp\uevr_forensics\session_... --game SN2
```

Ingest and analyze a capture in one step:

```bat
python E:\Github\UEVRJ\tools\stereo_forensics_db.py analyze-session C:\tmp\uevr_forensics\session_... --game SN2
```

This materializes derived tables:

- `event_pairs`
- `lineage_paths`
- `resource_lifetimes`
- `suspects`

Rank the current suspects:

```bat
python E:\Github\UEVRJ\tools\stereo_forensics_db.py rank-suspects --game SN2
```

Explain a specific event:

```bat
python E:\Github\UEVRJ\tools\stereo_forensics_db.py explain-event --capture-id 1 --event 18422
```

Generate candidate runtime experiment rules from ranked suspects:

```bat
python E:\Github\UEVRJ\tools\stereo_forensics_run_experiments.py generate ^
  --db C:\tmp\uevr_forensics\forensics.db ^
  --game SN2 ^
  --mode probes ^
  --out-dir C:\tmp\uevr_forensics\experiments
```

Use `--mode mutations` to emit heavier candidate actions. Runtime currently
executes descriptor swap, CBV swap, forced SRV array-slice, and
`neutralize_texture` null-SRV rules. Candidate actions that are not executable
yet are converted to color probes by default so they cannot be ranked as false
non-causal failures. Use
`--include-unsupported` only when you explicitly want skeleton rules.

Run the closed-loop baseline/trial scorer:

```bat
python E:\Github\UEVRJ\tools\stereo_forensics_ab_loop.py run ^
  --candidate-rules C:\tmp\uevr_forensics\experiments\candidate_rules.json ^
  --rules-file C:\tmp\uevr_forensics\experiments\live_rules.json ^
  --runtime-experiments-json C:\tmp\uevr_forensics\session_latest\experiments.json ^
  --out-dir C:\tmp\uevr_forensics\ab_loop ^
  --roi auto ^
  --db C:\tmp\uevr_forensics\forensics.db ^
  --game SN2
```

The game must be running with:

```bat
set UEVR_STEREO_EXPERIMENTS=1
set UEVR_STEREO_EXPERIMENTS_FILE=C:\tmp\uevr_forensics\experiments\live_rules.json
set UEVR_SN2_EYE_SCREENSHOT_TRIGGER_FILE=C:\tmp\uevr_shot_req.txt
set UEVR_SN2_EYE_SCREENSHOT_OUTPUT_DIR=C:\tmp\uevr_screenshots
```

The loop disables rules for baseline, enables one rule for trial, captures both
eyes, waits for the rule to appear in runtime `experiments.json` when available,
scores same-eye baseline-vs-trial deltas, and records apply observations.
`--roi auto` uses the candidate event viewport/scissor and normalizes
side-by-side backbuffer coordinates into the per-eye screenshot space. Scores
are marked untrusted unless the runtime reports a hit/apply confirmation for the
rule; mutation actions specifically require an `applied` observation.

For in-engine C-API ROI summaries instead of PPM pixel loops, use a manual
eye-local sample ROI:

```bat
python E:\Github\UEVRJ\tools\stereo_forensics_ab_loop.py run ^
  --candidate-rules C:\tmp\uevr_forensics\experiments\candidate_rules.json ^
  --rules-file C:\tmp\uevr_forensics\experiments\live_rules.json ^
  --runtime-experiments-json C:\tmp\uevr_forensics\session_latest\experiments.json ^
  --out-dir C:\tmp\uevr_forensics\ab_loop_samples ^
  --score-mode sample-json ^
  --sample-roi 200,120,360,240 ^
  --baseline-mode per-rule ^
  --db C:\tmp\uevr_forensics\forensics.db ^
  --game SN2
```

This writes `left_sample.json` and `right_sample.json` sidecars through the SN2
eye screenshot trigger and scores baseline-vs-trial deltas from those runtime
samples.

Add a finding:

```bat
python E:\Github\UEVRJ\tools\stereo_forensics_db.py add-finding ^
  --game SN2 ^
  --title "DE7C3822 samples static 512x512x8 LUT" ^
  --status confirmed ^
  --confidence 0.95 ^
  --summary "Consumer t5 has no frame producer in the captured frame." ^
  --implication "Producer duplication cannot fix this path." ^
  --next-action "Patch shader branch or test CB-gated path."
```

Add evidence:

```bat
python E:\Github\UEVRJ\tools\stereo_forensics_db.py add-evidence ^
  --finding-id SN2-FIND-0001 ^
  --session C:\tmp\uevr_forensics\session_... ^
  --event 18422 ^
  --shader-crc 0xDE7C3822 ^
  --slot t5 ^
  --type lineage ^
  --summary "t5 points at static 512x512x8 R10G10B10A2 resource with no producer."
```

Generate report:

```bat
python E:\Github\UEVRJ\tools\stereo_forensics_db.py report --game SN2 --out E:\Github\UEVRJ\docs\sn2\STEREO_FORENSICS_FINDINGS.md
```

Show next actions:

```bat
python E:\Github\UEVRJ\tools\stereo_forensics_db.py next-actions --game SN2
```

## Tool Integration

The lower-level tools can write to the DB:

```bat
python E:\Github\UEVRJ\tools\stereo_forensics_query.py summary C:\tmp\uevr_forensics\session_... --ingest-db C:\tmp\uevr_forensics\forensics.db --game SN2
```

```bat
python E:\Github\UEVRJ\tools\stereo_forensics_query.py summary C:\tmp\uevr_forensics\session_... --ingest-db C:\tmp\uevr_forensics\forensics.db --analyze-db --game SN2
```

```bat
python E:\Github\UEVRJ\tools\stereo_forensics_experiment.py score ... --db C:\tmp\uevr_forensics\forensics.db --session C:\tmp\uevr_forensics\session_...
```

```bat
python E:\Github\UEVRJ\tools\stereo_forensics_compile_rule.py compile ... --db C:\tmp\uevr_forensics\forensics.db --game SN2 --finding-id SN2-FIND-0001
```

```bat
python E:\Github\UEVRJ\tools\stereo_forensics_shader_semantics.py analyze-dir E:\captures\shaders --db C:\tmp\uevr_forensics\forensics.db
```

Use `--dxil-patch E:\Github\UEVRJ\build\bin\uevr\dxil-patch.exe` when DXIL
disassembly is available. SM4/SM5 DXBC still gets conservative reflection,
signature, and opcode facts from RDEF/ISGN/OSGN/SHEX/SHDR chunks.

## Standard

Use IDs in notes and commits:

- `capture:<id>`
- `suspect:<id>`
- `finding:<SN2-FIND-0001>`
- `experiment:<id>`
- `rule:<name>`
- `resource_instance_uid:<resource:0x...#gen:N>`

That gives future sessions a queryable graph:

```text
shader -> event -> descriptor view -> resource -> producer -> issue -> finding -> experiment -> fix rule
```
