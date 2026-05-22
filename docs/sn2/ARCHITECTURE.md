# Architecture

## Table of Contents

1. [The Stereo VR Problem](#the-stereo-vr-problem)
2. [Bucket-Shadow Fix Pattern](#bucket-shadow-fix-pattern)
3. [Dup Config System](#dup-config-system)
4. [Descriptor Management](#descriptor-management)
5. [Resource Mirror Infrastructure](#resource-mirror-infrastructure)
6. [Right-Eye View CB Synthesis](#right-eye-view-cb-synthesis)
7. [Live-Reload Subsystems](#live-reload-subsystems)

---

## The Stereo VR Problem

Subnautica 2 is rendered with Unreal Engine 5.6. Under UEVR's `-emulatestereo` mode, UE5 renders each eye independently — for most passes. But some passes (particularly volumetric fog producers) only dispatch on LEFT eye and write into shared 3D volumes that the consumer reads for both eyes.

### Symptom

- **LEFT eye**: correct teal underwater fog atmosphere
- **RIGHT eye**: missing fog tint → bright sky / blown-white / cyan look

### Root Cause Chain

```
┌─────────────────────────────────────────────────────────────────────┐
│  PRODUCER: VoxelizePS (PS CRC 0x9D14FCF0)                           │
│  - Pixel shader expanded to 3D slices via Geometry Shader           │
│  - Native dispatch: LEFT eye only (R=0)                             │
│  - View CB at root 3 (UE5 ViewUniformShaderParameters layout)       │
│  - Writes via 3 RTV slots to 3D textures (54×30×48 R11G11B10F)      │
└──────────────────────────────┬──────────────────────────────────────┘
                               │
                               ▼
        ┌──────────────────────────────────────────┐
        │  VOLUME: IntegratedLightScattering 3D    │
        │  - LEFT-frustum-projected fog data       │
        │  - Sampled by both eyes' consumers       │
        └──────────────────────┬───────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│  CONSUMER: SLW water material PS (PS CRC 0x37558DE4)                │
│  - Fires on BOTH eyes (L=14,901  R=4,963)                           │
│  - Reads IntegratedLightScattering via SRV                          │
│  - Right-eye samples LEFT-projected data with RIGHT matrices        │
│  - Output: wrong fog tint on right eye                              │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Bucket-Shadow Fix Pattern

The "bucket-shadow" architecture creates a parallel right-eye version of the volume:

```
┌────────────────────┐   ┌─────────────────────────────────┐
│ LEFT VoxelizePS   │──▶│ ORIGINAL VOLUME (LEFT-projected)│ ◀───┐
└────────────────────┘   └─────────────────────────────────┘     │
                                                                 │
┌────────────────────┐   ┌─────────────────────────────────┐    │
│ RIGHT VoxelizePS  │──▶│ MIRROR VOLUME (RIGHT-projected) │ ◀───┼──┐
│ (our DUP, with    │   └─────────────────────────────────┘    │   │
│  synth RIGHT CB)  │                                          │   │
└────────────────────┘                                          │   │
                                                                │   │
                              ┌──────────────────────────┐     │   │
                              │ LEFT consumer (0x37558DE4)│─────┘   │
                              │ reads ORIGINAL            │         │
                              └──────────────────────────┘         │
                                                                    │
                              ┌──────────────────────────┐         │
                              │ RIGHT consumer (0x37558DE4)│────────┘
                              │ reads MIRROR              │
                              │ (our SRV REDIRECT)        │
                              └──────────────────────────┘
```

### Four pieces

1. **Native LEFT writer**: untouched. Engine writes the original volume LEFT-projected.
2. **RIGHT writer dup**: UEVR re-issues the LEFT draw with right-eye View CB + RTVs redirected to mirror.
3. **LEFT consumer**: untouched. Reads original (correct LEFT data).
4. **RIGHT consumer SRV redirect**: UEVR swaps the SRV at the consumer to point at mirror's SRV.

### Why "bucket-shadow"

The term mirrors UE5's "transient resource bucket" concept — for each fog-shape resource the engine allocates, we shadow it with a parallel resource of identical dimensions/format. The bucket (game's resource) is the canonical; the shadow is per-eye-2's version.

---

## Dup Config System

The "dup" mechanism re-issues LEFT-only draw calls for the right eye with modified state. Configured via JSON at `E:\Github\UEVRJ\artifacts\sn2_dup_cfg_*.json`:

```json
{
  "entries": {
    "0x9d14fcf0": {
      "mode": "synthesize_right_cb",
      "view_cb_roots": [3],
      "note": "VoxelizePS — fog volume writer"
    },
    "0x4d44ce74": {
      "mode": "view_cb_only",
      "view_cb_roots": [3],
      "delta": -10240
    }
  }
}
```

### DupMode enum (in `Sn2DupConfigFile.hpp`)

| Mode | Purpose |
|---|---|
| `ViewportShift` | Default. Re-issue draw with viewport shifted to right-half of RT. Used for split-screen RTs. |
| `ViewCbOnly` | Re-issue with View CB swapped to right-eye (via pool delta). No viewport shift. |
| `Skip` | Don't dup at all. Used to exclude PSOs that don't need stereo handling. |
| `SynthesizeRightCb` | Re-issue with synthesized right-eye View CB (donor snapshot). No viewport shift. RTVs redirected to mirror. |

### How dup happens

In `D3D12Hook.cpp::sn2_try_duplicate_slw_basepass_right`:
1. Match PSO against dup_cfg entries
2. For matching entry's mode, compute right-eye View CB GPU_VA
3. Swap CBVs from LEFT VA → RIGHT VA at configured roots
4. If `SynthesizeRightCb`: also redirect RTVs to mirror UAVs (Stage V)
5. Re-issue the same Draw call
6. Restore LEFT CBVs and RTVs

---

## Descriptor Management

D3D12 descriptors come in two categories:
- **CPU-only heaps**: source for `CreateXxxView` calls; not shader-visible
- **GPU-visible (bindless) heaps**: bound to command list; shaders index into them

UEVR tracks descriptors via `sn2_descriptor_registry`:
- Hooks `Create{CBV,SRV,UAV,RTV}` calls → records `(CPU handle, resource ptr)` pair
- Hooks `CopyDescriptors` (both variants) → propagates the (CPU handle, resource ptr) from src to dst handle
- Hooks `SetDescriptorHeaps` → tracks current bindless heap base
- `lookup_resource_by_cpu_ptr(cpu_handle)` → returns the resource at that handle

### Known gap

In some scenes, UAVs/SRVs bound via descriptor table at certain slots return null from `lookup_resource_by_cpu_ptr`. The registry is large (80k+ entries) but specific slots may be populated via a path UEVR doesn't intercept. See [TROUBLESHOOTING.md](TROUBLESHOOTING.md#descriptor-registry-gaps).

**Workaround for VoxelizePS specifically**: it writes via RTV slots (tracked via `state.last_rtv_handles`), not descriptor tables. The RTV path is more reliable.

---

## Resource Mirror Infrastructure

`Sn2UweFogMirrorHook` allocates parallel "mirror" resources for fog-shape textures.

### Matcher

In `Sn2UweFogMirrorHook.hpp::matches_uwe_fog_signature`:
- TEXTURE3D R11G11B10_FLOAT 54×30×48 (VoxelizePS targets — primary fog volume)
- TEXTURE3D R11G11B10_FLOAT 30×36×28 (older IntegratedLightScattering shape)
- TEXTURE3D R16G16B16A16_FLOAT 32×32×8 (AerialPerspective volume)
- Requires `ALLOW_UNORDERED_ACCESS` flag
- Allows RT-flagged resources (was previously excluded — bug)

### Allocation

`create_mirror_for(device, game_resource, desc)`:
1. Idempotent check — if mirror already exists for this resource, return existing
2. Allocate parallel `ID3D12Resource` with same desc, initial state `COMMON`
3. Allocate UAV + SRV descriptors in our shader-visible heap
4. Store in `MirrorRegistry`

### Descriptor heaps owned

| Heap | Type | Purpose |
|---|---|---|
| `mirror_heap` | CBV/SRV/UAV (shader-visible) | UAV + SRV per mirror |
| `scratch_heap` | CBV/SRV/UAV (shader-visible) | Temporary tables for runtime redirect (ring buffer) |
| `mirror_rtv_heap` | RTV (CPU-only) | RTV per mirror (lazy-allocated on first dup hit) |

### Resource state strategy

Mirror initial state is `D3D12_RESOURCE_STATE_COMMON`. D3D12's implicit promotion rules allow promotion from COMMON to:
- `RENDER_TARGET` when bound via `OMSetRenderTargets`
- `UNORDERED_ACCESS` when used as UAV in a draw/dispatch
- Other states as needed

Implicit decay returns to COMMON after the command. This avoids needing explicit `ResourceBarrier` calls.

**Caveat**: implicit promotion only works for non-tracked promotion-capable resources. RT-flagged resources may have stricter requirements — see [TROUBLESHOOTING.md](TROUBLESHOOTING.md#rt-state-crashes).

---

## Right-Eye View CB Synthesis

UE5's `ViewUniformShaderParameters` constant buffer holds per-eye state (matrices, camera position, viewport size, eye index). For PSOs that fire on both eyes, the engine binds the appropriate eye's CB.

For LEFT-only PSOs (like VoxelizePS), the right-eye View CB doesn't exist in any pool the engine creates. We must synthesize it.

### Donor snapshot approach

`Sn2RightCbSynth` snapshots a both-eye PSO's right-eye CB content:
1. At a target "donor" PSO's draw (env-configurable, default `0x4D44CE74` root 3)
2. Detect right-eye execution via `state.last_viewport_bucket == StereoTraceBucket::Right`
3. Memcpy the CBV bytes into a UEVR-owned upload buffer (SEH-guarded against freed source memory)
4. Expose the upload buffer's GPU_VA via `sn2_right_cb_synth::get_right_va()`

At dup time, the synth GPU_VA is bound at the configured root.

### Limitation

The donor's CB content is correct for the donor's shader needs, but VoxelizePS may read fields the donor leaves zeroed/different. This causes "wrong fog colors" when the dup actually runs.

### Proper synthesis (future work)

Snapshot BOTH eyes of donor each frame, compute per-byte LEFT→RIGHT diff, apply diff to VoxelizePS's own LEFT CB to synthesize a fully-correct VoxelizePS right CB. Requires 80 divergent-slot patching (we have the byte-map from `diff_cb_dumps.py`).

---

## Live-Reload Subsystems

Several modules support file-trigger live reload to avoid game restarts during iteration:

### Magic Ink — PS-skip list (`Sn2MagicInk.hpp`)
- `UEVR_SN2_MAGIC_INK_SKIP_FILE=C:\tmp\ink_skip.txt`
- Polled every 60 frames (~1s)
- Edit file, save, change takes effect next poll
- Comments via `#` to end-of-line

### Eye Screenshot — file trigger (`Sn2EyeScreenshot`)
- `UEVR_SN2_EYE_SCREENSHOT_TRIGGER_FILE=C:\tmp\uevr_shot_req.txt`
- `UEVR_SN2_EYE_SCREENSHOT_OUTPUT_DIR=C:\tmp\uevr_screenshots`
- Create trigger file → UEVR captures L+R+BB on next Present → writes `done.txt`

### Dup Config — NOT live-reloaded (yet)
- `UEVR_SN2_DUP_CONFIG_FILE` loaded once at startup
- Changing the JSON requires game restart
- Future: extend Magic Ink's live-reload pattern to dup config too
