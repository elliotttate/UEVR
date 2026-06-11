# Using RenderDoc with UEVR — A Field Guide

This guide covers capturing and analyzing UEVR-modified Unreal Engine games with
RenderDoc: how to get a capture that doesn't crash, what a UEVR frame looks like
inside RenderDoc, how to analyze captures interactively and from scripts, and —
just as important — when RenderDoc is the wrong tool and this fork's in-process
diagnostics are the right one.

Everything here was learned the hard way on a UE5.7 D3D12 title (Subnautica 2)
during the DIBR/AFW synthetic-stereo work. Crash modes, workarounds, and tooling
are real, not theoretical.

---

## 1. Why this is hard: two hook frameworks fighting over the same APIs

RenderDoc and UEVR both work by interposing the graphics API:

- **RenderDoc** wraps D3D12/DXGI objects at creation time. Every
  `ID3D12Device`, `ID3D12CommandQueue`, swapchain, command list, resource, and
  descriptor heap the game sees is actually a RenderDoc wrapper object that
  records calls before forwarding them.
- **UEVR** hooks the same layer from the other direction: vtable hooks and
  inline hooks on `Present`, `ExecuteCommandLists`, resource creation, plus
  deep engine-level hooks (FFakeStereoRendering vtables, scene view families,
  pointer hooks into engine structures).

The order these two get into the process determines everything:

| Injection order | Result |
|---|---|
| **RenderDoc first, UEVR second** | ✅ The supported configuration. UEVR hooks RenderDoc's wrappers; RenderDoc sees everything UEVR submits (its compute passes, copies, swapchain blits) as ordinary API calls and records them faithfully. |
| **UEVR first, RenderDoc attached later** | ❌ Usually fatal. RenderDoc's late-attach replaces objects/vtables under UEVR's feet. In our testing the title died with `c0000005` in `D3D12Core.dll` / `nvwgf2umx.dll` mid-frame, or earlier: the game process exits during UEVR's PointerHook installation (the UEVR log just stops mid-hook-install). |
| **Same time / racing (global hook)** | 🎲 Nondeterministic flavor of the above. Avoid RenderDoc's global hook with UEVR entirely. |

**Golden rule: RenderDoc must be loaded into the process before any D3D12/DXGI
device exists, and before UEVR.** Everything in section 2 is about achieving
that ordering reliably.

A second, subtler consequence of the ordering: once RenderDoc is in first,
every device/queue/list pointer UEVR sees is a **RenderDoc wrapper**, not the
runtime object. This is normally fine (wrappers forward correctly), but it
matters when UEVR code does pointer-identity tricks, vtable scans, or hands
raw pointers to other components. This fork's capture service tracks exactly
this: `RenderDocCaptureService` records, for every important object kind
(present-time device/queue/swapchain, created command lists, resources,
descriptor heaps, root signatures, PSOs), whether the pointer
`com_object_looks_renderdoc_wrapped()` — and
`uevr_render_diag_renderdoc_status_json()` (exposed over the MCP as
`uevr_render_renderdoc_status`) reports it per object. If a capture behaves
strangely, check that report first: a half-wrapped world (some objects wrapped,
some not) means the bootstrap order was violated.

---

## 2. Getting a capture: three setups, in order of preference

### 2.1 This fork's embedded integration (preferred)

This UEVR fork builds and ships a **full RenderDoc runtime alongside the
backend**. `cmake/RenderDocFull.cmake` drives MSBuild on a RenderDoc source
checkout (`UEVR_RENDERDOC_SOURCE_DIR`, default `E:/Github/renderdoc`,
configuration `Development`) every time the `uevr` target builds, and copies
`renderdoc.dll` next to `UEVRBackend.dll` — that's the
"`Copying full RenderDoc runtime beside UEVRBackend.dll`" line you see in every
build log.

On top of that, `src/render/RenderDocCaptureService.{hpp,cpp}` implements an
in-process bootstrap:

- At backend init it loads the side-by-side `renderdoc.dll` (or the path in
  the **`UEVR_RENDERDOC_DLL`** env var) and acquires the
  `RENDERDOC_API_1_7_0` in-application API.
- The `BootstrapResult` records whether RenderDoc was *preloaded* (already in
  the process — the good case), whether it had to *late-load*, and crucially
  whether D3D12/DXGI **were already loaded** when RenderDoc arrived
  (`d3d12_was_loaded` / `dxgi_was_loaded` / `loaded_before_graphics_modules`).
  If graphics modules beat RenderDoc into the process, `capture_safe` is false
  and you should not trust the session for capture work.
- **`UEVR_DISABLE_RENDERDOC_BOOTSTRAP=1`** turns all of this off (use when you
  want a completely clean backend, e.g. perf measurement).
- **`UEVR_RENDERDOC_LAUNCHED_SUSPENDED=1`** tells the service it was set up by
  the suspended-prehook launcher (below) so it skips redundant work.

The cleanest launch ordering is the dedicated launcher,
**`UEVRRenderDocLauncher.exe`** (built beside the backend, along with the
`UEVRRenderDocSmoke.exe` self-test): it starts the game **suspended**, injects
`renderdoc.dll` first, then `UEVRBackend.dll`, calls the
`RENDERDOC_UEVR_RefreshHooks()` export so UEVR's first
`GetProcAddress(D3D12CreateDevice)` resolves through RenderDoc's hook stack,
and only then resumes the main thread. No race, no late-attach, every object
wrapped from frame zero (the status report shows `wrapped=true
vtable_module=renderdoc.dll` for present-time objects).

**Steam titles** exit and relaunch through Steam when started directly
(`SteamAPI_RestartAppIfNecessary`), which discards the launcher's injection.
With Steam running, any of these defeats the bounce — the launcher's child
inherits its environment and defaults the working directory to the exe's
folder, so all three compose with it:

1. `steam_appid.txt` containing just the AppID, placed next to the shipping
   exe; launch as normal.
2. `SteamAppId`/`SteamGameId` env vars set in the shell before running the
   launcher.
3. Steam Launch Options:
   `"<path>\UEVRRenderDocLauncher.exe" --exe "<shipping exe>" -- %command%` —
   Steam starts the launcher in full Steam context. Keep the explicit `--exe`
   aimed at the *shipping* exe: `%command%` often points at a root bootstrap
   stub that respawns the real game, and injecting into the stub captures
   nothing (the launcher does not follow children). The `-- %command%` tail
   absorbs Steam's substitution as a harmless extra game argument. Disable the
   Steam overlay for capture sessions — `GameOverlayRenderer64.dll` is a third
   Present-hooking framework in the process.

Capture triggering in this mode is file-based: a request writes
`%TEMP%\uevr_renderdoc_capture.req` (first line = capture path template,
optional `frames=N` on later lines); the in-process watcher performs
Start/EndFrameCapture on the exact `{device, HWND}` pair the present hook
observed. The MCP `uevr_renderdoc_request_capture` tool wraps this end to end
(write request → wait for the .rdc → optional `validate` via renderdoccmd
index + `thumbnail`).

Full mechanics: **`docs/RENDERDOC_EMBEDDED_PORT.md`** (this repo). The
replay-time mutation bridge (applying UEVR's live shader/CB patches inside a
RenderDoc replay) is documented in **`docs/sn2/UEVR_RD_BRIDGE_WORKFLOW.md`**.

For Subnautica 2 specifically, the project launcher wraps all of this:

```powershell
# Embedded launcher: RenderDoc first, UEVR second, suspended prehook. Preferred.
& 'E:\Github\Subnautica 2\moddingkit\runs\launch_hunter_baseline.ps1' -EmbeddedRenderDoc

# Same, but resume with RenderDoc only and inject the backend N ms later
# (for heavy diagnostics where backend startup must not overlap capture init):
& '...\launch_hunter_baseline.ps1' -EmbeddedRenderDoc -EmbeddedBackendDelayMs 3000

# Legacy capture mode: renderdoccmd preload with --opt-hook-children, then
# remote-inject UEVR:
& '...\launch_hunter_baseline.ps1' -RenderDoc
```

### 2.2 Stock RenderDoc UI + manual UEVR injection (vanilla UEVR)

If you're on vanilla UEVR without the embedded integration:

1. Open `qrenderdoc`, **File → Launch Application**.
2. Executable = the game's *shipping* exe (not a launcher stub). If the game
   goes through a launcher, enable **"Hook into children"** so RenderDoc
   follows the process tree.
3. Capture options that matter for UEVR work:
   - **Capture callstacks**: off unless you need them (huge overhead).
   - **API validation**: off — the debug layer plus two hook frameworks is
     asking for trouble.
   - **Verify buffer access / ref-all-resources**: leave defaults; UE titles
     are already heavy.
4. Launch. Wait until the game **window exists and the engine has presented
   frames** (main menu is enough).
5. Now inject UEVR with its frontend (or your injector). **Inject after the
   window is up** — injecting into a suspended or pre-window process
   destabilizes both UEVR's hook installation and (in our experience) OpenXR
   session bring-up.
6. The RenderDoc overlay should still show in a corner; UEVR's VR output goes
   to the headset/runtime while the flat window keeps presenting.

### 2.3 renderdoccmd (headless / scripted)

```
renderdoccmd.exe capture --opt-hook-children --working-dir <gamedir> <Game-Win64-Shipping.exe> <args>
```

Then inject UEVR once the window is up. `renderdoccmd` is also what you use
for post-processing captures (section 6). This is what the project launcher's
`-RenderDoc` switch automates, including keeping the game's full baseline env
block intact.

---

## 3. Triggering captures

Four ways, all usable simultaneously:

1. **Hotkey** — F12/PrintScreen by default in the overlay. Fine interactively.
2. **In-application API** — this fork drives
   `StartFrameCapture`/`EndFrameCapture`/`TriggerMultiFrameCapture` through
   `RenderDocCaptureService` (`start_capture`, `end_capture`,
   `capture_blocking`, all keyed to the active device+window pair the present
   hook observed). The service also:
   - sets the **capture path template** (`set_capture_template`) so .rdc files
     land where you want them, with a sane default if empty;
   - writes **capture file comments** into the .rdc after the fact
     (`write_capture_file_comments`) — use it to stamp the exact UEVR config
     (mode, env vars, build) into the capture so "what build was this?"
     never happens again;
   - sets the in-progress **capture title** (`set_capture_title`).
3. **MCP tools** (this repo's companion tooling), for agent/scripted workflows:
   - `uevr_render_renderdoc_status` — bootstrap state + the
     object-ownership/wrapped report described in section 1.
   - `uevr_render_renderdoc_trigger_capture` — capture N frames now.
   - `uevr_render_renderdoc_set_capture_template` — set the .rdc path template.
   - `uevr_render_renderdoc_launch_ui` — spawn qrenderdoc pointed at captures.
   - `uevr_renderdoc_launch_game` / `uevr_renderdoc_capture_game` /
     `uevr_renderdoc_request_capture` / `uevr_renderdoc_list_captures` /
     `uevr_renderdoc_validate_capture` / `uevr_renderdoc_paths` — end-to-end
     launch/capture/enumerate/validate plumbing.
4. **Title-specific capture hooks** — e.g. this fork's SN2 diagnostics can fire
   a capture on a specific render event (`Sn2RdCapture.cpp`), which is how you
   capture *the exact frame* a transient bug occurs instead of mashing F12.

**Capture timing tip:** if a configuration is known to crash some seconds in
(copy-on-transient device removals were ~40–50s in our SN2 work), capture
*inside* the stable window (~25–30s) rather than hoping to outrun the crash.

---

## 4. Reading a UEVR frame in RenderDoc

A captured UEVR frame is the game's frame **plus UEVR's appended GPU work**.
Knowing the anatomy saves hours.

### 4.1 The scene render target

- In UEVR's Native Stereo (and this fork's Synthetic Stereo / AFW) the engine
  renders into a **double-wide render target**: left eye in the left half,
  right eye in the right half (e.g. 2872×720 for 1436-wide eyes). In true AFR
  the RT is single-wide and eyes alternate per frame.
- UE5 renders through RDG with pooled, renamed textures. The scene color is
  typically `R16G16B16A16_FLOAT` pre-tonemap; the scene depth is
  `R32G8X24_TYPELESS` (32-bit depth + stencil), **reversed-Z** (near = 1.0,
  far = 0.0 — in the texture viewer near geometry is *bright*).
- Pooled depth ping-pongs between physical resources every frame — don't
  assume the pointer/resource ID from one capture means anything in another.

### 4.2 Finding UEVR's own work

UEVR's GPU work shows up at the **end of the frame**, after the engine's post
processing, usually as compute dispatches and `CopyTextureRegion` calls on the
game's queue. There are no UE pass markers around them (UEVR doesn't emit
RDG events), so they look "naked" in the Event Browser. Anchor points:

- The **present** is your reference. Walk backwards from it.
- UEVR's resources are **named** (this fork especially): look for
  `DIBR Packed Stereo Output`, `DIBR Scatter Key A/B`, `DIBR Scatter Color
  A/B`, `DIBR AFW History Key/Color` in the Resource Inspector. Find a named
  resource, then use its usage list to jump to the dispatches that touch it.
- The DIBR/AFW pipeline (this fork) is the dispatch chain
  `clear → scatter depth → scatter color → fill → compose [→ AFW stash]`
  followed by a `CopyTextureRegion` of the packed output back over the
  double-wide RT, then the **per-eye swapchain copies** (into
  `NATIVE_STEREO_ARRAY` array slices for OpenXR — slice 0 = left, 1 = right).
- The UI/overlay layer is a separate small RT and its own copies.

### 4.3 What is NOT in the capture

The OpenXR/OpenVR **compositor's** work — reprojection, lens distortion, layer
composition, the actual HMD output — happens in the runtime's process, not the
game's. A capture proves what UEVR *submitted* (images + which swapchain
slices), not what the compositor displayed. If submitted frames are correct
but the headset looks wrong, the problem is metadata (poses/FOV per layer) or
the runtime — RenderDoc cannot see it. Use the runtime's own diagnostics for
that (with the OpenXR simulator: its projection log records per-frame
submitted pose/FOV/rects).

### 4.4 Inspecting UEVR's compute state

Select a DIBR dispatch and use the Pipeline State tab:

- **Root signature**: one CBV + one descriptor table (SRVs t0/t1 = source
  color + engine depth; UAVs u0..u4 = output, scatter key/color, history
  color/key) in this fork.
- **CBuffer**: `DIBRStereoParams` is a flat struct of ~250 scalars plus three
  `float4x4` (the source→target reprojection matrices and the temporal
  history matrix). RenderDoc decodes it against the shader's reflection — this
  is the fastest way to confirm what divergence/overscan/temporal values a
  frame actually ran with.
- The compute shaders are DXC `cs_6_0` DXIL; RenderDoc's disassembly works,
  and source is embedded in this repo (`src/mods/vr/d3d12/shaders/`).

**Capture ≠ live.** Values you read in a capture are that frame's values. If a
fix depends on a per-eye constant, re-confirm it in the live game (watches,
logging) before building on it — engine CBs especially get reused/renamed
across frames.

---

## 5. Interactive analysis techniques that pay off

- **Event Browser + resource usage cross-reference** beats scrolling. Find the
  resource (by name), open its usage timeline, jump to writes.
- **Pixel History** on an artifact pixel of the final double-wide answers
  "which pass last touched this" instantly — invaluable for per-eye bugs
  (run it once on a left-half pixel, once on the mirrored right-half pixel,
  and diff the event lists).
- **Texture viewer, custom range**: for reversed-Z depth set the range to
  ~[0.99, 1.0] to see near-field structure; the default [0,1] mapping makes UE
  depth look uniformly white-ish.
- **Save texture** (EXR for HDR scene color) and diff outside RenderDoc —
  numpy mean-abs-diff between the two halves of the double-wide is the
  fastest left/right-eye divergence metric.
- **Compare two captures** (e.g. flag on vs off): capture both, then script
  the comparison (section 6) — eyeballing two qrenderdoc windows does not
  scale.

---

## 6. Scripted / headless analysis

Interactive qrenderdoc doesn't scale to 1.4 GB captures or repeated questions.
Two tool surfaces:

### 6.1 `renderdoccmd`

```
renderdoccmd.exe thumb  capture.rdc --out thumb.png      # quick look
renderdoccmd.exe convert capture.rdc --format xml ...    # structural export
```

This fork wraps both over MCP (`uevr_render_renderdoccmd_thumb`,
`uevr_render_renderdoccmd_convert`) so an agent can sanity-check a capture
without opening a UI.

### 6.2 qrenderdoc Python

```
qrenderdoc.exe --ui-python my_script.py capture.rdc
```

Your script gets the full replay API (`pyrenderdoc`). Patterns that work:

```python
# Skeleton: open is done by the harness; iterate actions, find dispatches
ctx = pyrenderdoc
def visit(d, depth=0):
    for c in d.children:
        visit(c, depth+1)
    # d.flags, d.eventId, d.customName ...
for d in ctx.CurReplay().GetRootActions():   # API names vary per RD version
    visit(d)
```

Hard-won pitfalls:

- **Do not full-scan big captures.** Resolve the event IDs you need once
  (e.g. "left-eye resolve = EID 17813, right = 17874") and jump straight to
  them in subsequent runs. Keep a notes file per capture.
- **`GetMinMax()` hangs on 3D textures** (volumetric fog grids etc.). Read the
  raw bytes (`GetTextureData`) and reduce yourself.
- Texture byte layouts: mind row pitch alignment when you reinterpret
  buffers; D3D12 row pitch is 256-byte aligned in readbacks.
- Replay sessions are stateful and slow to open — batch all your questions
  into one script invocation rather than reopening per question.

---

## 7. Known failure modes and what they mean

| Symptom | Cause | Fix |
|---|---|---|
| Game exits during UEVR injection; UEVR log ends mid "installing hooks" | RenderDoc attached *after* UEVR, or attach raced hook installation | Use RenderDoc-first ordering (embedded launcher / launch-from-RenderDoc) |
| `c0000005` in `D3D12Core.dll`/`nvwgf2umx.dll` seconds after attach | Late-attach wrapper swap under live UEVR hooks | Same as above — never late-attach to a UEVR process |
| Device removed ~40–50s in while capturing copy-heavy experiments | Driver-level fragility under wrapper + heavy `CopyTextureRegion` traffic | Capture inside the stable window; prefer shader-side approaches over copy spam |
| Overlay shows, capture produces empty/black frames | Captured the wrong swapchain/window (games with multiple swapchains) | Set the active window pair explicitly (this fork: `set_active_window` from the present hook does it automatically) |
| Capture works, VR output broken only in headset | Compositor-side (poses/FOV metadata or runtime) — not visible to RenderDoc | Runtime-side logs/diagnostics (e.g. simulator projection log) |
| Suspended launch + early inject "works" but OpenXR session/screenshots break | Suspended-launch destabilizes session bring-up in some titles | Inject after window-up; or use the dedicated suspended-prehook launcher which sequences it correctly |
| Half the objects report `renderdoc_wrapped=false` in the status JSON | Graphics modules loaded before RenderDoc (bootstrap too late) | Treat the session as capture-unsafe; relaunch with correct ordering |

---

## 8. When RenderDoc is the wrong tool

RenderDoc costs: launch fragility, capture-time stalls, gigabyte files, and
*it cannot see across frames* (one capture = one frame; per-frame alternation
bugs like AFR/AFW flicker need consecutive-frame evidence). This fork grew a
set of in-process tools precisely because of those limits:

| Question | Better tool (this fork) |
|---|---|
| "What do the submitted eyes look like, pixel-exact?" | `uevr_render_eye_dump_both` (swapchain dumps, PNG) |
| "Is something alternating frame-to-frame?" | AFW consecutive-frame dumper (`UEVR_DIBR_AFW_DUMP=1`, motion-triggered, 4 adjacent presented frames to `%TEMP%`) |
| "Which RTV/DSV binds happen, in order, with what shapes?" | DIBR depth-tracker bind census (`UEVR_DIBR_BIND_CENSUS=1`) |
| "What does scene color contain *before translucency*, per pass?" | Pre-translucency probe (`UEVR_DIBR_PRETRANS_DUMP=1` — in-list `CopyResource` at bind time, readback to .ppm) |
| "What draw/bind/root-constant traffic differs between eyes?" | Render inspector suite (`uevr_render_stereo_forensics`, `uevr_forensics_eye_diff`, descriptor lineage tools) |
| "Did my shader override actually change pixels?" | `uevr_render_ab_pixel_diff` (automated off/on A/B sampling) |
| "GPU cost of a pass?" | `uevr_render_gpu_timings` / frame timing stats — RenderDoc replay timings are not live timings |

Rule of thumb from the DIBR project: **use RenderDoc to understand structure
(what passes exist, what state they run with), use in-process tools to prove
behavior (what actually happens frame-over-frame, live)**. The bugs that cost
us days were all frame-to-frame association problems — invisible in any single
capture by construction.

---

## 9. Quick recipes

**The proven end-to-end SN2 recipe (live cvars + native .rdc, no rebuild)** —
this exact sequence produced validated 1.4 GB captures with live render-state
changes applied:

```powershell
# 1) Embedded launcher, FULL UEVR (do NOT set UEVR_CAPTURE_ONLY_D3D12):
& 'E:\Github\Subnautica 2\moddingkit\runs\launch_hunter_baseline.ps1' -EmbeddedRenderDoc
#    RenderDoc overlay shows "Capturing D3D12... F12 to capture".

# 2) Wait ~60-90s for the menu AND the MCP server (uevr_get_status -> pipe up).

# 3) Apply cvars LIVE via uevr_exec_command AFTER the menu has rendered.
#    Verify by screenshot, not by cvar readback.

# 4) Capture via MCP:
#    uevr_renderdoc_request_capture { captureTemplate, frames: 1,
#                                     validate: true, thumbnail: true }
#    -> <template>_capture.rdc + index JSONL + thumbnail.
```

Gotchas that cost real debugging cycles — bake them in:

- **`UEVR_CAPTURE_ONLY_D3D12=1` disables the MCP server.** Capture-only mode
  early-returns out of `Framework::on_frame_d3d12`, so the mod frame loop,
  the MCP pipe/HTTP endpoint, and `user_script.txt` execution never start —
  meaning **no live cvars**. If you need live render-state changes in the
  captured frame, run FULL UEVR with the embedded launcher; it is stable.
- **Startup cvar injection does not stick on some menus.** `-DPCVars=...` and
  `-ExecCmds="..."` both *apply* and are then re-enabled by the menu world's
  load (observed with `r.Water.SingleLayer` / `r.SkyAtmosphere` on SN2). Only
  a live set after the menu loads survives. Judge cvar effect by screenshot —
  cvar *readback* returns garbage in shipping builds.
- **The stability matrix:** embedded launcher (`-EmbeddedRenderDoc`,
  renderdoc-first + prehook + full UEVR) is the stable mode. The historical
  "UEVR + RenderDoc crashes" experience came from the other modes (late
  attach, or `renderdoccmd` preload racing full VR bring-up).

**Stamp config into a capture you just took:**
use `uevr_render_renderdoc_set_capture_template` before, and write capture
comments after (the service exposes `write_capture_file_comments`) — include
the UEVR build hash and the full `UEVR_*` env block.

**Thumbnail-check a capture without opening the UI:**
```
renderdoccmd.exe thumb "C:\captures\sn2_frame.rdc" --out check.png
```

**Find the DIBR work in a capture:** Resource Inspector → search "DIBR" →
`DIBR Packed Stereo Output` → usage → the compose dispatch; the five
dispatches immediately before it are the scatter chain; the
`CopyTextureRegion` after it writes the double-wide back.

**Verify the bootstrap is capture-safe (MCP):**
`uevr_render_renderdoc_status` → check `api_loaded`, `capture_safe`,
`loaded_before_graphics_modules`, and that present-time device/queue report
`renderdoc_wrapped=true`.

---

## 10. Checklist

Before you spend an hour on a capture session:

- [ ] RenderDoc loaded **before** D3D12/DXGI and before UEVR (status JSON says `capture_safe`)
- [ ] API validation off, callstacks off
- [ ] Inject UEVR only after the game window is up (unless using the suspended-prehook launcher)
- [ ] Capture path template set; plan to write config comments into the .rdc
- [ ] Know your stable window if the config crashes eventually; capture early
- [ ] For per-frame/alternation questions: stop — use the in-process consecutive-frame tools instead
- [ ] For headset-only symptoms: stop — check submitted pose/FOV metadata at the runtime, RenderDoc can't see it
