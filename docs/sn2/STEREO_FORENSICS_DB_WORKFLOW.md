# Stereo Forensics Knowledge DB Workflow

The database is the source of truth for reusable investigation memory. Markdown
handoffs should reference DB IDs or be generated from DB content; they should
not be the only place a finding exists.

## Default DB

```bat
set UEVR_STEREO_FORENSICS_DB=C:\tmp\uevr_forensics\forensics.db
```

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
executes descriptor swap, CBV swap, and forced SRV array-slice rules. Candidate
actions that are not executable yet are converted to color probes by default so
they cannot be ranked as false non-causal failures. Use
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

## Standard

Use IDs in notes and commits:

- `capture:<id>`
- `suspect:<id>`
- `finding:<SN2-FIND-0001>`
- `experiment:<id>`
- `rule:<name>`

That gives future sessions a queryable graph:

```text
shader -> event -> descriptor view -> resource -> producer -> issue -> finding -> experiment -> fix rule
```
