# RenderDoc 1:1 Audit

This document tracks the remaining work needed for UEVR captures to be native
RenderDoc captures, not just UEVR captures that resemble RenderDoc output.

## Non-Negotiable Rule

RenderDoc must be the owner of D3D12/DXGI capture serialization. UEVR can layer
diagnostics and shader replacement on top of RenderDoc wrappers, but it must not
replace RenderDoc's first interception of the game device, factory, swapchain,
queue, command lists, resources, descriptors, or PSOs.

## Residency Requirement

The process is 1:1-capable only when RenderDoc is in-process before the game's
first real graphics object creation:

- `D3D12CreateDevice`
- `CreateDXGIFactory`, `CreateDXGIFactory1`, `CreateDXGIFactory2`
- swapchain creation through `IDXGIFactory*`
- command queue and command list creation

The current fork can report whether RenderDoc was present before `d3d12.dll` or
`dxgi.dll` were loaded, but that is still a heuristic. The runtime wrapper
ownership ledger is the stronger proof: the first UEVR-observed present device,
swapchain, command queue, and command list must all report vtables from
`renderdoc.dll`.

The deterministic launch path is:

```powershell
.\build-renderdoc\bin\uevr\UEVRRenderDocLauncher.exe `
  --exe "C:\path\Game.exe" `
  --args "-dx12"
```

The launcher creates the game process suspended, injects `renderdoc.dll`, then
injects `UEVRBackend.dll`, sets the embedded RenderDoc proof environment, waits
for UEVR's startup thread to signal its early-ready event, and only then resumes
the game main thread. The launcher also enables `UEVR_RENDERDOC_PREHOOK_D3D12=1`,
so UEVR installs its D3D12 hook stack against RenderDoc-wrapped dummy objects
before the game thread can create the first real device.

The same build emits `UEVRRenderDocSmoke.exe`, a tiny D3D12 swapchain app used
to validate this launch/capture path without requiring a full game install.
Run `tools\run_renderdoc_smoke_capture.ps1` to launch it through UEVR, request a
capture, and validate the resulting `.rdc` with `renderdoccmd`.

UEVR calls RenderDoc's UEVR-specific `RENDERDOC_UEVR_RefreshHooks()` export
after `UEVRBackend.dll` is loaded and before the early D3D12 prehook. That
export reapplies RenderDoc's Windows import-table hook pass to all loaded
modules, covering the launcher case where UEVR was loaded by a direct remote
`LoadLibraryW` call instead of through RenderDoc's hooked `LoadLibraryW`.

## Pointer Policy

Default policy for embedded RenderDoc mode:

- RenderDoc-facing calls get wrapper pointers. `StartFrameCapture`,
  `EndFrameCapture`, and `SetActiveWindow` must use the exact wrapped
  `{ID3D12Device*, HWND}` pair registered by RenderDoc.
- UEVR registries should store wrapper pointers unless a call site has a
  specific need to compare against raw application objects.
- Any raw pointer escape must be explicit and documented at the call site.
  Do not infer raw/wrapped equivalence from address equality.
- UEVR hook detours must call the captured original method from the vtable they
  patched. In embedded mode that original should be the RenderDoc wrapper
  method, not the underlying real D3D12 method.
- If UEVR needs stable identity across raw and wrapped objects, add a mapping
  layer rather than mixing both pointer forms in the same registry key space.

## Current Automatic Proof

`RenderDocCaptureService` records first/current object ownership for:

- UEVR dummy D3D12 device
- UEVR dummy DXGI factory
- UEVR dummy swapchain
- UEVR dummy command queue
- UEVR dummy command list
- first/current game Present swapchain observed by UEVR
- first/current game Present device from `IDXGISwapChain::GetDevice`
- first/current game Present command queue recovered from the swapchain
- first/current queue passed to `ExecuteCommandLists`
- first/current command list created or submitted through observed hooks
- first/current DXGI factory observed from the game swapchain parent
- optional `CreateDXGIFactory*` export-hook observations when
  `UEVR_RENDERDOC_DXGI_FACTORY_PROOF=1` is explicitly enabled
- first/current created or observed D3D12 resource
- first/current created or bound descriptor heap
- first/current created or bound root signature
- first/current created or bound pipeline state

`uevr_render_diag_renderdoc_status_json()` reports:

- `capture_safe`
- `first_present_objects_renderdoc_wrapped`
- `first_dxgi_factory_renderdoc_wrapped`
- `first_command_list_renderdoc_wrapped`
- `first_resource_renderdoc_wrapped`
- `first_descriptor_heap_renderdoc_wrapped`
- `first_root_signature_renderdoc_wrapped`
- `first_pipeline_state_renderdoc_wrapped`
- `object_ownership[]` with pointer, source, vtable module, wrapper flag, and
  observation sequence

Interpretation:

- `capture_safe=false`: not 1:1. RenderDoc was initialized too late or the
  runtime could not prove early residency.
- `capture_safe=true` but first present objects are not wrapped: not 1:1. UEVR
  is seeing raw D3D12/DXGI objects and RenderDoc did not own the capture path.
- first present objects wrapped but command lists absent: inconclusive until a
  command-list creation/submission is observed.
- first dummy objects, first present objects, first DXGI factory, first command
  list, and first state/resource objects wrapped plus a passing
  `renderdoccmd index-capture`: the observed D3D12/DXGI path is compatible
  with native RenderDoc ownership.

## D3D12 Detour Audit

All UEVR D3D12 detours should satisfy this pattern:

1. Resolve the hook record from the live object's current vtable slot.
2. Get the captured original from that hook record.
3. Apply UEVR inspection or replacement.
4. Call the captured original exactly once unless the feature intentionally
   suppresses the command.
5. Treat any intentional suppression as incompatible with strict 1:1 replay
   unless it is explicitly requested by a UEVR feature.

High-priority detours for audit:

- `CreateGraphicsPipelineState`
- `CreateComputePipelineState`
- `CreatePipelineState`
- `CreateCommandList`
- `CreateCommandList1`
- `CreateDescriptorHeap`
- descriptor view creation and copy methods
- `SetPipelineState`
- command list reset/close
- draw/dispatch/execute-indirect/execute-bundle
- root signature and root binding methods
- `ExecuteCommandLists`
- `Present` and `Present1`

Current branch status:

- The PSO and shader replacement path already resolves replacement PSOs before
  calling the captured original.
- Present capture control now updates the exact RenderDoc capture pair before
  UEVR takes the broad hook monitor mutex.
- The ownership ledger now proves dummy command lists, Present command queues,
  created command lists, descriptor heaps, resources, root signatures, and PSOs
  are RenderDoc wrappers in the smoke path.
- In embedded mode (`UEVR_RENDERDOC_BOOTSTRAP=1`) or explicit strict mode
  (`UEVR_RENDERDOC_STRICT_ORIGINALS=1`), vtable hook installation warns when the
  original slot UEVR is about to capture does not resolve to `renderdoc.dll`.
  That catches the common bypass where UEVR would call the real D3D12 method
  instead of RenderDoc's wrapper original.

## Shader Replacement Placement

Strict 1:1 replay accepts runtime shader replacement only if the replacement is
the PSO that the game actually used while RenderDoc captured the frame.

Preferred ordering:

1. RenderDoc wraps the device first.
2. UEVR hooks the wrapped device method tables.
3. UEVR replaces PSO bytecode or PSO pointers.
4. UEVR calls the RenderDoc wrapper original.
5. RenderDoc serializes the final state that reached D3D12.

If UEVR ever calls the underlying real D3D12 method directly, RenderDoc may miss
the replacement and the capture is not 1:1.

## Capture Validation

Use:

```powershell
powershell -ExecutionPolicy Bypass -File tools\validate_renderdoc_capture.ps1 C:\path\capture.rdc
```

The script locates `E:\Github\renderdoc\x64\Development\renderdoccmd.exe` by
default and runs:

```powershell
renderdoccmd.exe index-capture --out <temp-output-dir> <capture.rdc>
```

Passing this test proves that the file is a native RenderDoc capture that the
same RenderDoc checkout can open and index. It does not prove visual correctness
by itself; visual or state correctness still needs replay inspection.

For a running game with UEVR's RenderDoc watcher active, request a capture and
validate it in one step:

```powershell
powershell -ExecutionPolicy Bypass -File tools\capture_and_validate_renderdoc.ps1
```

Latest smoke validation:

```text
C:\Users\ellio\AppData\Local\Temp\uevr_renderdoc_smoke\smoke_20260527_111227_capture.rdc
Indexed 125 events, 31 actions, 0 unique shaders
```
