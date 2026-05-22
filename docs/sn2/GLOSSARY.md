# Glossary

Domain terms used throughout this codebase, in alphabetical order.

## A

### `Sn2BindingAnalyzer`
UEVR module that records per-(PSO, root) CBV binding GPU_VAs and emits a JSON report. The key tool for finding View CB locations and per-eye deltas.

## B

### Bindless heap
A large GPU-visible descriptor heap (typically 1M slots) used by UE5 for bindless rendering. Tracked in UEVR as `tls_bindless_heap`.

### Bucket-shadow
Our architecture pattern: for each engine-allocated fog texture (the "bucket"), allocate a parallel mirror (the "shadow") so per-eye writers can produce per-eye-correct fog without overwriting each other.

## C

### CBV (Constant Buffer View)
A descriptor pointing to a constant buffer. Bound at root parameters in D3D12. The View CB is the most-bound CBV in UE5.

### CommandListCorrelationState (`s` / `state`)
The per-thread struct in `D3D12Hook.cpp` that tracks the current command list's bound state (PSO, viewports, RTVs, root descriptors, etc.) at draw/dispatch sites.

### Consumer
A shader (typically pixel shader) that READS from a resource. In our context: `0x37558DE4` is the SLW water-material PS that reads the IntegratedLightScattering volume.

## D

### Donor (PSO)
A PSO that fires on BOTH eyes and shares its right-eye View CB content via `Sn2RightCbSynth` snapshot. Default donor: `0x4D44CE74` (a basepass PS).

### DSV (Depth Stencil View)
A descriptor pointing to a depth buffer. Bound alongside RTVs via `OMSetRenderTargets`. We currently don't preserve DSV during our redirect — suspected freeze cause.

### Dup (duplication)
Our term for re-issuing a LEFT-only draw call for the right eye with modified state.

### dup_cfg
The JSON file controlling per-PSO dup behavior. See [DUP_CONFIG.md](DUP_CONFIG.md).

### DupMode
Enum of dup behaviors: `ViewportShift`, `ViewCbOnly`, `Skip`, `SynthesizeRightCb`.

### DXBC
Direct3D Byte Code — the compiled shader bytecode format used by D3D12. UE5 dumps these via `Sn2PsoBytecodeDumper`.

## E

### `eye_bucket`
The integer eye bucket. UEVR's `StereoTraceBucket` enum: Unknown=0, Left=1, Right=2, Full=3, Multi=4.

### `-emulatestereo`
UE5 command-line flag that simulates stereo rendering without actual HMD. UEVR uses this for VR injection.

## F

### Fog volume / IntegratedLightScattering
UE5's 3D volumetric fog accumulation texture. The visible scene's fog tint comes from sampling this volume per pixel.

## G

### GPU_VA
GPU Virtual Address. The address space resources live in for GPU access. `ID3D12Resource::GetGPUVirtualAddress()`.

### GS (Geometry Shader)
A pipeline stage between VS and PS that can emit primitives. Used by VoxelizePS to expand triangles into multiple 3D-slice render targets.

## I

### IntegratedLightScattering
See "Fog volume". Specifically the 3D R11G11B10F texture (54×30×48) that VoxelizePS writes and the SLW PS reads.

## L

### Lumen
UE5's real-time global illumination system. Produces several fog-adjacent volumes (probe atlas, AerialPerspective).

## M

### Magic Ink
Our diagnostic naming for env-driven PS-skip-at-draw — "whatever you see disappear identifies the PSO."

### MetaXR Simulator
Meta's OpenXR runtime simulator. Renders the game's per-eye output in side-by-side view in a desktop window. Our primary visual debugging tool.

### Mirror
A parallel `ID3D12Resource` allocated by `Sn2UweFogMirrorHook` to provide per-eye separate storage for shared fog volumes. The "shadow" in bucket-shadow.

## O

### `OMSetRenderTargets`
D3D12 method to bind render target views + optional depth-stencil view to the pipeline. Hooked by us to track RTV bindings + substituted by our redirect.

## P

### PIX
Microsoft's GPU debugger. Provides `pixtool.exe` for headless captures. Used for shader bytecode analysis via the pix-mcp tools.

### PPM
A simple bitmap format (P6 = binary RGB). Used for our screenshot outputs because Python can read/write it without dependencies.

### Producer
A shader that WRITES to a resource. In our context: `0x9D14FCF0` (VoxelizePS) is the producer that writes the IntegratedLightScattering volume.

### PSO (Pipeline State Object)
D3D12 object encapsulating a shader pipeline + state. Identified by PS CRC.

### PS CRC
A 32-bit hash of the pixel shader's DXBC bytecode, used to identify which shader is which across builds. Stable across game restarts.

## R

### Right-eye fog bug
The session's primary target: right eye missing the underwater teal fog tint that LEFT eye has.

### Root parameter / Root descriptor / Root descriptor table
D3D12 binding mechanisms. Root descriptors bind a single resource; root descriptor tables hold multiple. View CB is usually a root descriptor at index 3 in SN2.

### RT (Render Target)
A texture bound as a write target for the pixel shader output. VoxelizePS writes to RT slots 0,1,2.

### RTV (Render Target View)
A descriptor pointing to a render target.

### RVA
Relative Virtual Address. Offset from the binary's image base.

## S

### Scratch heap
UEVR-owned ring buffer of descriptor table windows used to build runtime-modified descriptor tables for redirect.

### SLW (Single Layer Water)
UE5's specialized water material/pass. PS CRC `0x37558DE4` is the SLW water material PS.

### SRV (Shader Resource View)
A descriptor pointing to a read-only resource (typically textures sampled by shaders).

### Stereo bucket (`StereoTraceBucket`)
UEVR enum for classifying draws by which eye they target. Determined heuristically from viewport position.

### Synth (synthesis)
Our process of creating a right-eye View CB for a LEFT-only PSO. Currently: donor-snapshot approach in `Sn2RightCbSynth`.

## T

### Title menu cave (scene)
The test scene Subnautica 2 auto-loads to on startup. Used throughout this session for reproducibility — no game progression needed.

## U

### UAV (Unordered Access View)
A descriptor pointing to a read-write resource. Compute shaders typically write to UAVs.

### UEVR
Unreal Engine VR — community injection mod for UE4/UE5 games. Backend is a D3D12-hooking C++ DLL (`UEVRBackend.dll`).

### UEVR backend
The C++ DLL that's injected into the game process. Built from `E:\Github\UEVRJ\`.

### UE5
Unreal Engine 5. Subnautica 2 uses UE5.6.

## V

### View CB / ViewUniformShaderParameters
UE5's per-view constant buffer containing matrices, camera position, viewport size, eye index, etc. The most-bound CBV per draw. ~4KB in size; 80/256 of its 16-byte slots differ per-eye in stereo mode.

### Viewport shift
Our dup technique of shifting the viewport rect from (0,0,W,H) to (W,0,W,H) so the dup draws to the right half of a split-screen RT.

### VoxelizePS
The PS that writes the IntegratedLightScattering volume via GS-expanded slices. PS CRC `0x9D14FCF0`.

## W

### `wslsnapit`
The MCP server for Windows desktop screenshot capture. Despite "WSL" in name, captures the actual Windows desktop. Primary way Claude observes the Meta XR Simulator output.

## X

### XR Simulator
Meta XR Simulator. See "MetaXR Simulator".
