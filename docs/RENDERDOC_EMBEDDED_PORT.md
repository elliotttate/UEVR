# RenderDoc Embedded Port

This fork is the workspace for integrating full RenderDoc capture into UEVR
without disturbing the main `uevrj` checkout.

## Workspace

- UEVR fork: `E:\github\uevrj-renderdoc`
- Branch: `renderdoc-embedded-port`
- RenderDoc source: `E:\Github\renderdoc`

## Current Integration Layer

The current fork has four pieces in place:

1. A build bridge for RenderDoc's own runtime.
2. A shared in-process capture service used by both UEVR diagnostics and the
   SN2 file-trigger capture path.
3. A deterministic suspended launcher that makes RenderDoc and UEVR resident
   before the game main thread can create D3D12/DXGI objects.
4. A RenderDoc hook-refresh export used by UEVR after its backend DLL is loaded,
   so UEVR's first `GetProcAddress(D3D12CreateDevice)` resolves through
   RenderDoc's hook stack.

`cmake/RenderDocFull.cmake` builds RenderDoc's own Visual Studio solution and
targets the `DLL\renderdoc` project:

```powershell
cmake --build build --config Release --target renderdoc_full
```

When the `uevr` target builds, it depends on `renderdoc_full` and copies the
resulting `renderdoc.dll` beside `UEVRBackend.dll`. The target also prefers
RenderDoc's source-tree `renderdoc/api/app/renderdoc_app.h` over UEVR's older
vendored copy.

`src/render/RenderDocCaptureService.*` owns RenderDoc API discovery, capture
template management, default capture options, capture enumeration, and
Start/EndFrameCapture. `RenderDiagnosticsCAPI` and `Sn2RdCapture` both route
through that service so they no longer maintain separate RenderDoc loaders or
handwritten API structs.

Late-loading `renderdoc.dll` is explicitly opt-in (`UEVR_LOAD_RENDERDOC_DLL=1`,
`UEVR_SN2_RD_CAPTURE_LOAD_DLL=1`, or an explicit DLL path). Without that, the
service only uses a RenderDoc module that was already present in-process.
The service records whether `d3d12.dll` or `dxgi.dll` were already loaded when
RenderDoc was initialized. UEVR-loaded RenderDoc is considered capture-safe only
when it was loaded before those graphics modules were present; otherwise it is
reported as degraded.

`src/Main.cpp` now runs the optional RenderDoc bootstrap on UEVR's startup
thread before `Framework` construction when `UEVR_RENDERDOC_BOOTSTRAP=1`.
`Framework` runs a second pass after logging is configured to report status and
start the capture watcher.

The RenderDoc checkout also exposes `RENDERDOC_UEVR_RefreshHooks()`, which calls
the Windows `LibraryHooks::Refresh()` path. On Windows that refresh now reapplies
RenderDoc's normal import-table hook pass to all loaded modules. UEVR calls it
before the early D3D12 prehook so its dummy device, factory, swapchain, queue,
and command list are RenderDoc wrappers before UEVR installs its own vtable
hooks.

The SN2 Present hook polls RenderDoc triggers before taking UEVR's hook monitor
mutex, so RenderDoc Start/EndFrameCapture is not called while UEVR is holding
its broad hook lock.

The D3D12 Present hook also publishes the latest `{ID3D12Device*, HWND}` pair
to the shared RenderDoc service when embedded tracking is enabled
(`UEVR_RENDERDOC_BOOTSTRAP=1`, `UEVR_RENDERDOC_TRACK_ACTIVE_PAIR=1`, or the SN2
RenderDoc trigger path). General diagnostics and the watcher now prefer this
exact pair and only fall back to wildcard capture when no exact pair is known or
the exact-pair capture fails.

The shared service now keeps a wrapper-ownership ledger for UEVR's first/current
observed dummy objects and game Present objects. Status JSON reports whether
the first observed Present device, swapchain, command queue, and command list
look RenderDoc-wrapped. See `docs/RENDERDOC_1TO1_AUDIT.md`.

`UEVRRenderDocLauncher.exe` is built beside `UEVRBackend.dll`. It creates the
game process suspended, injects RenderDoc first, injects UEVR second, sets the
embedded proof environment, waits for UEVR to signal that its early RenderDoc
startup has reached the D3D12 prehook point, and resumes the game only after
that event. That is the current deterministic before-first-device path.

The launcher sets `UEVR_RENDERDOC_PREHOOK_D3D12=1`. In that mode UEVR installs
the D3D12 hook stack from the startup thread before the suspended game main
thread is resumed. This closes the fast-start race where the process had both
DLLs resident but the game could still reach `D3D12CreateDevice` before UEVR's
own hook monitor ran.

`UEVRRenderDocSmoke.exe` is built into the same folder. It is a minimal D3D12
swapchain app for validating the launcher and `.rdc` capture path when no game
target is available.
`tools\run_renderdoc_smoke_capture.ps1` launches that smoke app through UEVR,
requests one capture through the UEVR watcher, and validates the produced
`.rdc` with `renderdoccmd index-capture`.

By default UEVR records DXGI factory ownership non-invasively from
`IDXGISwapChain::GetParent` on the first game Present. The older inline
`CreateDXGIFactory*` proof hooks are now explicit diagnostics only via
`UEVR_RENDERDOC_DXGI_FACTORY_PROOF=1`, because installing another export hook
before RenderDoc's own DXGI hook can perturb the very factory path being tested.

When `UEVR_RENDERDOC_BOOTSTRAP=1` and the RenderDoc API is available, UEVR's
D3D12 dummy-device bootstrap preserves the active `D3D12CreateDevice` hook
instead of restoring the original export bytes. This lets RenderDoc wrap UEVR's
dummy device first, then UEVR hooks the wrapped method tables in embedded mode.
RenderDoc status JSON also reports the active device vtable module and whether
that COM object appears to be RenderDoc-wrapped.

## Supported 1:1 Path And Limits

The stable 1:1 path is the suspended launcher with RenderDoc injected before
`UEVRBackend.dll`. UEVR then refreshes RenderDoc's hook tables before its own
D3D12 prehook and resumes the game only after the wrapper proof path is ready.

This still ships RenderDoc as `renderdoc.dll` beside UEVR rather than statically
merging RenderDoc into `UEVRBackend.dll`. That is intentional for now: it keeps
RenderDoc's normal `DllMain`, hook registration, D3D12 serialization, and replay
tooling intact while UEVR drives capture through the in-app API.

The launcher has an explicit `--backend-load-renderdoc` mode where only
`UEVRBackend.dll` is injected and UEVR loads RenderDoc while the process is still
suspended. That mode can produce a valid `.rdc`, but the smoke app currently
shows a teardown access violation, so it is not the default compatibility path.
Late injection into an already-running game remains degraded by design; no code
can retroactively make RenderDoc own objects that were created before RenderDoc
was present.

## Compatibility Target

"100% compatible with RenderDoc" means:

- UEVR produces native `.rdc` files through RenderDoc's own capture machinery.
- The file opens in qrenderdoc/renderdoccmd from the same RenderDoc checkout
  without a repair step or UEVR-specific importer.
- D3D12 initial resource contents, descriptor state, command lists, queues,
  barriers, PSOs, root signatures, markers, and debug names serialize through
  RenderDoc's native D3D12 driver.
- Capture starts from an exact `{ID3D12Device*, HWND}` pair when possible.
  Wildcard capture is only a fallback for diagnostics.
- RenderDoc is present before the first real D3D12/DXGI object creation for
  compatibility runs. Late-load mode remains marked degraded.

## Implementation Plan

### Phase 0 - Build and Packaging

Status: implemented.

- Build `E:\Github\renderdoc\renderdoc.sln` target `DLL\renderdoc` from UEVR's
  CMake build.
- Copy `renderdoc.dll` and `renderdoc.pdb` beside `UEVRBackend.dll`.
- Prefer RenderDoc's source-tree `renderdoc_app.h` in embedded builds.
- Default to rebuilding RenderDoc whenever UEVR builds; set
  `UEVR_RENDERDOC_ALWAYS_BUILD=OFF` when iterating only on UEVR sources.

### Phase 1 - One UEVR RenderDoc API Surface

Status: implemented.

- Keep RenderDoc API loading, capture options, templates, capture listing, and
  Start/EndFrameCapture in `RenderDocCaptureService`.
- Route `RenderDiagnosticsCAPI` and `Sn2RdCapture` through that service.
- Do not keep a handwritten `renderdoc_app.h` struct in SN2 code.
- Keep late-load opt-in and visibly degraded.

### Phase 2 - Earliest Possible Residency

Status: implemented for the launcher path.

- Startup thread calls the optional bootstrap before `Framework` construction.
- Startup thread refreshes RenderDoc hooks before `Framework` construction.
- Framework refreshes again after logging is ready and starts the watcher.
- The suspended launcher waits for UEVR's early D3D12 prehook before resuming
  the game main thread.

### Phase 3 - Hook Ownership

Status: implemented for observed D3D12/DXGI paths.

- Choose one owner for D3D12/DXGI interception in embedded mode: RenderDoc.
- Initialize and refresh RenderDoc's D3D12/DXGI hook stack before UEVR's first
  dummy device and before the game main thread runs.
- UEVR's dummy-device bootstrap now preserves RenderDoc's active
  `D3D12CreateDevice` hook when `UEVR_RENDERDOC_BOOTSTRAP=1`.
- Smoke validation proves UEVR dummy objects and first observed game Present
  device/factory/swapchain/queue are RenderDoc wrappers before capture.
- Embedded mode currently keeps UEVR's vtable hooks layered on top of RenderDoc
  wrappers.
- Keep a non-RenderDoc mode for the existing lightweight UEVR hook behavior.

### Phase 4 - Wrapped Object Interop

Status: implemented for the tracked ledger, with broader registry audits still
needed as UEVR adds new D3D12 state.

- Audit every place UEVR stores or compares `ID3D12Device`, command queue,
  command list, resource, descriptor heap, swapchain, and PSO pointers.
- Decide per call site whether UEVR should use the RenderDoc wrapper pointer or
  the unwrapped real pointer.
- Ensure command queue and swapchain tracking uses RenderDoc's wrapped objects
  consistently so Start/EndFrameCapture can find the active frame capturer.
- Diagnostics now report whether the active device appears to be
  RenderDoc-wrapped.
- The shared service now records first/current wrapper ownership for dummy
  objects, DXGI factory results, Present device/swapchain/queue, observed
  command lists, resources, descriptor heaps, root signatures, and PSOs.
- Extend wrapper/raw equivalence to any future UEVR registry that is not yet
  observed by the shared RenderDoc ledger.

### Phase 5 - Shader Replacement Integration

Status: design needed.

- Existing shader replacement hooks are centered on:
  `CreateGraphicsPipelineState`, `CreateComputePipelineState`,
  `CreatePipelineState`, `SetPipelineState`, and root binding/draw paths.
- Embedded RenderDoc mode should either:
  - install UEVR's PSO/shader hooks after RenderDoc wraps the device, or
  - move PSO bytecode inspection and replacement into callbacks adjacent to
    RenderDoc's D3D12 serialization/wrapper code.
- Runtime replacement can stay runtime-only; the `.rdc` will naturally contain
  the replacement PSO if the game actually used it during capture.
- Any UEVR-only diagnostic metadata should be emitted as debug names, markers,
  or capture comments so qrenderdoc can display it without custom tooling.

### Phase 6 - Capture Control

Status: implemented for watcher and SN2 trigger paths.

- SN2 file-trigger capture now uses exact device/window Start/End pairs and
  runs before UEVR takes the broad hook monitor mutex.
- General RenderDoc diagnostics and the capture watcher now use the active
  exact pair first and keep wildcard capture as a fallback.
- Status fields include `late_loaded`, `capture_safe`, loaded path, active pair,
  frame-capturing state, capture list, and active-device wrapper diagnostics.
- Remaining polish: add an explicit exact-pair C API entrypoint and return the
  newest `.rdc` path directly from every capture command.

### Phase 7 - Validation

Status: automated smoke validation implemented.

- Build UEVR + full RenderDoc.
- Launch a minimal D3D12 sample with UEVR/RenderDoc resident before device
  creation.
- Trigger one exact-pair capture and verify RenderDoc can index it.
- Run `tools\validate_renderdoc_capture.ps1 <capture.rdc>` so
  `renderdoccmd index-capture` validates that the same RenderDoc checkout can
  open and index the produced capture.
- For a running game, run `tools\capture_and_validate_renderdoc.ps1` to request
  a watcher capture and validate the newest `.rdc` automatically.
- Remaining game validation: repeat on a UE D3D12 title.
- Remaining game validation: repeat on SN2 with shader replacement enabled.
- Negative test late-load mode and verify status reports degraded capture.
- Use renderdoccmd/qrenderdoc to open the capture produced by UEVR and confirm
  no repair/import path is required.

## Current Limits

- If UEVR is injected after the game has created D3D12 objects, no code can make
  the capture fully equivalent to a RenderDoc launch-time capture. The launcher
  or proxy path must place UEVR/RenderDoc in-process earlier.
- UEVR's D3D12 hook system still owns a large amount of state. The current
  policy is wrapper-first, with explicit raw/wrapped mapping required for any
  registry that cannot store wrappers.
- Single-backend-injection mode (`--backend-load-renderdoc`) captures and
  validates but is not the default until its smoke teardown crash is fixed.
- UE title and SN2 shader-replacement validation still need to be run against
  real game workloads.

## Remaining Hardening

- Promote the deterministic suspended launcher into the normal UI flow so UEVR
  and RenderDoc are present before any game thread can call `D3D12CreateDevice`,
  `CreateDXGIFactory*`, or swapchain creation.
- Keep extending first-object ownership proof as new D3D12 paths are observed.
- Maintain the pointer ownership contract: every UEVR registry must either store
  wrapper pointers consistently or map wrapper/raw equivalence explicitly.
- Audit all D3D12 detours layered over RenderDoc wrappers to ensure they call
  the captured wrapper original and never bypass RenderDoc serialization.
- Extend exact-pair capture beyond the latest Present pair for queued and
  multi-frame capture requests.
- Wire capture metadata parity: callstacks, all resources, API validation,
  debug output policy, capture comments, debug names, and UEVR annotations.
- Decide packaging for the final port: keep `renderdoc.dll` beside
  `UEVRBackend.dll`, embed RenderDoc as a UEVR-owned runtime module, or port the
  needed initialization path into the backend while preserving RenderDoc's
  normal `DllMain`/hook registration behavior.
- Update or retire older SN2 bridge docs that still describe self-contained
  capture as the preferred path.

## Next Steps

1. Run the launcher path against a UE D3D12 title and SN2.
2. Fix or remove `--backend-load-renderdoc` after resolving its teardown crash.
3. Route UEVR shader replacement through RenderDoc-owned wrapper callbacks, or
   explicitly order the UEVR hooks after RenderDoc's wrapped interfaces.
4. Carry exact device/window capture pairs everywhere and avoid wildcard
   capture except as a diagnostics fallback.
5. Write `.rdc` captures through RenderDoc's native capture path and verify the
   files open in qrenderdoc without repair.
