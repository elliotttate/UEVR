# UEVRJ vs stock UEVR — default-settings performance audit

Date: 2026-06-12. Compared `ue57performance` (77a1b84) against praydog `upstream/master`
(merge-base 6f66aff). "Defaults" = no `UEVR_*` env vars set, fresh config, arbitrary
(non-Subnautica-2) game. Diff scope: ~153k inserted lines across 160 files; all findings
below were verified by reading the current source, not just agent reports.

## Verdict

The big subsystems (DIBR/AFW synthesis, SN2 hooks, StereoForensics, D3D12Diagnostics,
RenderDoc/PIX capture) are correctly gated off at defaults. However, two **always-installed
D3D12 command-list hooks** do ungated per-call work that stock UEVR doesn't do at all, and
the SetPipelineState one is expensive enough to plausibly cost **several ms of CPU per frame
in PSO-heavy D3D12 scenes**, with global-mutex serialization across UE's parallel
command-list recording threads. D3D11 games are essentially unaffected.

---

## Finding 1 (HIGH): SetPipelineState detour — ungated ~20 KB copy + 3 global mutexes + live getenv, per call

Stock UEVR does not hook `SetPipelineState`. The fork hooks it on every graphics command
list in every D3D12 game (`install_command_list_hooks`, src/hooks/D3D12Hook.cpp:3571-3578,
called from the always-on `CreateCommandList`/`CreateCommandList1` hooks at 5493/5530).

Per call at defaults, `D3D12Hook::set_pipeline_state` (src/hooks/D3D12Hook.cpp:17923-18020) does:

1. `update_cmdlist_pso()` (17945 → 11438): **global `g_cmdlist_state_mutex` lock** +
   `unordered_map operator[]`. Ungated — comment at 11436 says "both need this even when
   descriptor_table_correlation_enabled() is off".
2. `read_cmdlist_state()` (17946 → 12584): **global mutex lock again** + return **by value**
   of `CommandListCorrelationState` (src/hooks/D3D12Hook.cpp:254-317). The struct contains
   two `RootConstantValuesArray` = 32 slots × 64 dwords × 4 B = **8 KB each**, plus 8
   `RootSlotArray` + 6 `RootHashArray` (256 B each) → **~20 KB copied under the lock, every
   call**. The author knows this is hot: a comment at 12946 calls it "global-mutex lock +
   full state copy" when describing a fast-path that *other* call sites got — this one didn't.
3. `env_flag_enabled_a("UEVR_STEREO_FORENSICS_BIND_ONLY_RECORD_PSO")` (17950 → 724): a **live
   `GetEnvironmentVariableA` per call** — PEB lock + linear scan of the env block. Every
   other gate in this file is a cached `static const bool`; this one isn't. (The `||`
   short-circuit never saves it because `enable_d3d12_diagnostic_command_list_hooks()` is
   false at defaults.)
4. `renderdoc_capture::note_object()` (17954 → src/render/RenderDocCaptureService.cpp:606):
   **another global mutex** (`g_mutex`) acquired per call, even with no RenderDoc session.
5. `update_cmdlist_effective_pso()` (18019... only on tracked path; at defaults path exits at
   17968 after `original()`).

Cost estimate: ~1.5–4 µs serial per call (memcpy + env scan dominate). UE5 issues roughly
500–3000 `SetPipelineState` calls/frame from multiple parallel RHI translate threads →
**~1–8 ms added CPU per frame**, partially serialized through two global mutexes (the 20 KB
copy happens *while holding* `g_cmdlist_state_mutex`, so parallel recorders queue behind it).

### Cheap fixes
- Cache the env flag in a `static const bool` like every other gate.
- Gate `update_cmdlist_pso` + `read_cmdlist_state` behind
  `should_track_d3d12_pipelines() || enable_d3d12_diagnostic_command_list_hooks() || is_subnautica2_process()` — at defaults none are true, so the
  detour collapses to a tail-call of `original`.
- If state must stay: return only the few fields callers need (PSO/eye bucket) instead of the
  20 KB struct, or shard the map per-thread / use `thread_local` staging.
- Skip `note_object` when no RenderDoc/PIX session is possible (cached "renderdoc.dll loaded"
  check before taking the lock).

## Finding 2 (MEDIUM): OMSetRenderTargets detour — global DIBR mutex per call, always installed

Also hooked unconditionally (src/hooks/D3D12Hook.cpp:3585-3592, comment confirms "Always
installed (independent of the diagnostic env)"). Per call at defaults
(`om_set_render_targets`, src/hooks/D3D12Hook.cpp:32083-32168):

- `dibr_depth_tracker::record_dsv_bind()` (32099 → src/hooks/DIBRDepthTracker.cpp:365): takes
  the **global `g_mtx`** then probes `g_dsv_to_resource`. At defaults the map is empty
  (creation-side recording at DIBRDepthTracker.cpp:166 is gated by `view_tracking_enabled()`),
  so it's lock + empty find + unlock — cheap serially (~50–100 ns) but a shared lock across
  parallel recording threads on every DSV bind.
- Builds a `SIZE_T[8]` census array (32104-32112) then calls 4 record functions
  (`record_census_bind`/`record_probe_bind`/`record_afw_depth_bind`/`record_velocity_bind`)
  that each early-out on cached atomics/statics — fine.
- The diagnostic block (32133-32163) is properly gated.

At hundreds of OMSetRenderTargets calls/frame this is tens of µs serial — minor, but the
mutex is the same one AFW/census use, and it's contended across recording threads.
`begin_render_pass` (32170+) is the same pattern for render-pass-based RHIs.

### Cheap fix
Make `record_dsv_bind` early-out before the lock when tracking has never been armed
(e.g. `if (!g_ever_armed.load(relaxed)) return;` — set when `record_dsv` first inserts).

## Finding 3 (LOW): per-PSO-creation overhead in always-on creation hooks

`CreateGraphicsPipelineState`/`CreateComputePipelineState`/`CreateRootSignature`/
`CreateRenderTargetView`/`CreateDepthStencilView`/`CreateCommandList` are all hooked
unconditionally (src/hooks/D3D12Hook.cpp:3009-3257). At defaults each PSO creation does a
`QueryInterface`, two `note_object` calls (each taking the RenderDocCaptureService global
mutex), and early-outs (`record_pipeline_cache_event` exits on `is_enabled()`;
ShaderOverrideRegistry registration is off — verified ShaderOverrideRegistry.cpp:1348-1369
all atomics false at defaults). Negligible steady-state; worth a thought only during
shader-precompile storms where thousands of parallel creations funnel through one mutex.

## Finding 4 (LOW): RenderInspector mod runs every Present

New mod, unconditionally registered (src/mods/Mods.cpp). `RenderInspector::on_present`
(src/mods/RenderInspector.cpp:843-893) runs per frame: 6 sidebar string checks, atomics, and
`ShaderOverrideRegistry::on_present` (frame counter + PSO-churn ring buffer upkeep). Per-frame
(not per-draw) and small — fine, just nonzero vs stock. Heavy paths (D3D12Diagnostics,
FrameResourceInspector) are correctly gated on sidebar tabs / force flags.

## Finding 5 (NOTE): PIX capturer DLL auto-load + watcher threads

Framework tries to load `WinPixGpuCapturer.dll` if PIX is installed and spawns a 250 ms-poll
sentinel watcher thread unless `UEVR_DISABLE_PIX_BOOTSTRAP=1` (src/Framework.cpp:112-157,
724-737). RenderDoc bootstrap is env-gated off. Perf impact ~zero (sleeping thread), but it's
a default behavioral difference worth knowing about (foreign capture DLL resident in every
game on dev machines with PIX).

---

## Verified NON-issues (claims checked and cleared)

- **Present-path mutex**: fork's Present takes `g_framework->get_hook_monitor_mutex()` — so
  does stock (upstream D3D12Hook.cpp). Not a regression.
- **Present-path SN2 calls**: `sn2_sky_atmos_scan::scan_and_patch_uploads()` early-outs on
  env-gated `g_enabled` (D3D12Hook.cpp:4218-4221, 4280); the ~14 `sn2_*::on_present()` calls
  early-out on cached env statics; `sn2_fix_rules::refresh_rules()` is a file-stat once per
  60 frames only if a rules path is configured (Sn2FixRuleEngine.cpp:148-159). All trivial.
- **OpenXR `synchronize_frame` holding `sync_mtx` across `xrWaitFrame`**: stock does exactly
  the same (upstream OpenXR.cpp:119-141). Fork adds only chrono stats + throttled logging.
- **VR mod defaults**: no perf-relevant default changed vs stock. DIBR mode defaults to 0
  (off); DIBRSynthesis is lazily initialized (`ensure()` only when mode != 0 —
  D3D12Component.cpp:~5021); the 15 embedded kernels compile on first enable, not at startup.
  MatchGameFOV, hitch diagnostics, 16:9 compat etc. all default off with cheap early returns.
- **DIBRDepthTracker maps**: empty at defaults; population gated by `view_tracking_enabled()`
  (census/probe/AFW env vars or runtime AFW arm).
- **SN2 hooks**: install only when exe is `Subnautica2-Win64-Shipping` (D3D12Hook.cpp:~3381),
  and most individual hooks additionally need their own env var.
- **StereoForensics / D3D12Diagnostics / draw-call & root-bind hooks**: only installed/enabled
  via `UEVR_STEREO_FORENSICS` / `UEVR_ENABLE_D3D12_DIAGNOSTIC_COMMAND_LIST_HOOKS` / sidebar
  tabs (enable_d3d12_diagnostic_command_list_hooks, D3D12Hook.cpp:1728).
- **CVarManager game workarounds** (Stalker2/Aphelion/Windrose): only for those titles, retry
  loop stops after success / 600 ticks. Zero cost elsewhere.
- **g_cmdlist_state_map growth**: ~20 KB per distinct command-list pointer, never erased at
  defaults (erase hooks are diagnostic-gated), but UE pools command lists so it's bounded in
  practice (~1–2 MB). SN2 tracking maps don't populate outside SN2.
- **UE5.7 render-timing logger** (FFakeStereoRenderingHook.cpp:~4592): 5.7+-only, atomics +
  a log every 5 s. Negligible.

## Priority

1. Gate / slim the `set_pipeline_state` detour (Finding 1) — biggest win, affects every D3D12 game.
2. Pre-lock early-out in `record_dsv_bind` (Finding 2).
3. Cached env flag + note_object fast-path (Findings 1/3) — one-liners.

---

## Fixes applied (2026-06-12, same day)

**Finding 1 — `set_pipeline_state` default fast-path** (src/hooks/D3D12Hook.cpp):
- `UEVR_STEREO_FORENSICS_BIND_ONLY_RECORD_PSO` is now read once into a
  `static const bool` (was a live `GetEnvironmentVariableA` per PSO bind).
- New fast-path: when no forensics/diagnostic recording, not SN2, and
  `should_track_d3d12_pipelines()` is false, the detour tail-calls `original`
  immediately — skipping both `g_cmdlist_state_mutex` acquisitions, the ~20 KB
  `CommandListCorrelationState` copy, and the RenderDoc bound-PSO `note_object`.
  Safety: every reader of the cmdlist state lives in detours that only install
  (env) or only run (SN2/tracking) under the conditions checked; the tracking
  check is re-evaluated per call so runtime sidebar toggles re-engage
  bookkeeping on the next bind. The bound-PSO RenderDoc note is intentionally
  dropped at defaults (creation-time notes still flow).

**Finding 2 — depth-tracker bind liveness now armed-gated**
(src/hooks/DIBRDepthTracker.cpp/.hpp, both bind detours in D3D12Hook.cpp):
- `record_dsv_bind` early-outs on a new relaxed atomic `g_liveness_armed`
  before touching `g_mtx`. Armed by the first `select_scene_depth` call (the
  per-frame DIBR consumer) and by `set_afw_depth_snapshot_enabled(true)` /
  `set_afw_depth_sequence_enabled(true)`. Candidate population at DSV creation
  stays ungated so runtime DIBR enablement still sees all depth targets; the
  first armed select uses its existing stale-candidate fallback for one frame.
- The census/probe/AFW/velocity block in `om_set_render_targets` and
  `begin_render_pass` is now wrapped in a single
  `dibr_depth_tracker::bind_capture_armed()` superset check (new helper), so
  the default path pays a few cached loads instead of building the RTV array
  and making four cross-TU calls.

Residual default-path cost in every D3D12 game, post-fix: one hook-lookup map
find + a handful of cached/atomic loads per SetPipelineState /
OMSetRenderTargets / BeginRenderPass call, and per-creation bookkeeping on
PSO/view/command-list creation. No locks, no state copies, no env reads on the
per-call paths.

**Left as-is, deliberately**: `note_object`'s internal mutex (remaining call
sites are per-frame or per-creation); RenderInspector + ShaderOverrideRegistry
per-frame upkeep (bounded: one mutex + ring push per frame, disk scan every
60 s idle); PIX capturer DLL auto-load (no measurable frame cost — PIX shims
device creation, which precedes injection; disable with
`UEVR_DISABLE_PIX_BOOTSTRAP=1`).
