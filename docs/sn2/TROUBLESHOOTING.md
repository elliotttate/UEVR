# Troubleshooting

Issues encountered during the SN2 right-eye fog investigation and their resolutions.

## Index

- [Build errors](#build-errors)
- [Runtime crashes](#runtime-crashes)
- [Visual artifacts](#visual-artifacts)
- [Tooling false negatives](#tooling-false-negatives)
- [Descriptor registry gaps](#descriptor-registry-gaps)
- [RT state crashes](#rt-state-crashes)
- [Wrong fog colors with synth](#wrong-fog-colors-with-synth)
- [Subagent bugs we caught](#subagent-bugs-we-caught)

---

## Build errors

### LNK1104 cannot open file UEVRBackend.dll

**Cause**: Game is running. The DLL is locked by the game process.

**Resolution**:
```cmd
wmic process where "Name='Subnautica2-Win64-Shipping.exe'" delete
```
Then rebuild.

### MSVC C++ SEH with destructor objects

**Cause**: `__try` / `__except` cannot share a function with C++ objects that have non-trivial destructors (`std::scoped_lock`, `std::vector`, ComPtr, etc.). MSVC restriction.

**Symptom**:
```
error C2712: cannot use __try in functions that require object unwinding
```

**Resolution**: Extract the `__try` block into a helper function with only POD types:
```cpp
bool safe_memcpy_seh(void* dst, const void* src, size_t bytes) {
    __try {
        std::memcpy(dst, src, bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
```
Call the helper from the function with destructor objects.

### std::min type mismatch

**Cause**: `state.last_rtv_count` is `uint8_t` but `static_cast<UINT>(8)` is `uint32_t`. `std::min` can't deduce the common type.

**Resolution**: Use explicit ternary instead:
```cpp
const UINT rtv_n = static_cast<UINT>(state.last_rtv_count) < 8u
    ? static_cast<UINT>(state.last_rtv_count) : 8u;
```

---

## Runtime crashes

### Crash in KERNELBASE.dll after binding mirror RTV

**Cause**: Mirror resource created in `D3D12_RESOURCE_STATE_UNORDERED_ACCESS`, then bound via `OMSetRenderTargets`. D3D12 runtime detects state violation; driver crashes.

**Resolution**: Change mirror initial state to `D3D12_RESOURCE_STATE_COMMON`. D3D12's implicit promotion rules will lift it to RENDER_TARGET when bound. See `Sn2UweFogMirrorHook.cpp::create_mirror_for`.

```cpp
m.current_state = D3D12_RESOURCE_STATE_COMMON;  // was UNORDERED_ACCESS
```

### Crash with synth_right_cb on certain LEFT-only PSOs

**Cause**: 5 LEFT-only PSes have View CBs in a different pool from the donor. The pool-relative delta (-10240) lands on memory the pool doesn't own → invalid GPU_VA bind → driver fault.

**Symptom**: Game runs ~5s then exception 0x80000003 in KERNELBASE.dll.

**Resolution**: Revert those 5 PSes to `Skip` mode in dup_cfg. Validated list lives in `sn2_dup_cfg_restricted.json`.

### Game freezes + closes after extended use of synth+RTV active

**Suspected causes** (priority order):
1. DSV not preserved during our `OMSetRenderTargets` substitution (we pass `nullptr` for DSV in both substitute and restore)
2. Resource state churn — 22 mirrors transitioning UAV↔RT every frame
3. RTV cache grows unbounded
4. GPU sync issue between our re-issued draws and game's command stream

**Investigation steps** (TODO for next session):
- Capture game's bound DSV at draw entry, restore it
- Add explicit `ResourceBarrier` for mirror state instead of relying on implicit promotion
- Cap RTV cache size with LRU

---

## Visual artifacts

### LEFT eye flickers with synth+RTV redirect active

**Symptom**: Static screenshot looks OK; real-time observation shows fog flickering.

**Cause**: RTV redirect fires (36+ events per session) but LEFT-eye writes apparently still leak into mirror, or our restore changes state subtly.

**Possible specific causes**:
- `state.last_rtv_handles[i]` may not be CPU handles in the format expected
- The OMSetRenderTargets restore with nullptr DSV changes depth-write state
- VoxelizePS may bind UAVs in descriptor tables beyond what we scan (kSearch=64 only)

**Mitigation**: Currently keep VoxelizePS as `Skip` in dup_cfg. Re-enable in next session after DSV preservation fix.

### Right eye stays "blown-white" even with skip-right env vars

**Cause**: `UEVR_SN2_SKYATMOS_SKIP_RIGHT=1` uses a hardcoded list of 5 sky-atmosphere CRCs (`0x009f8918, 0x1c5283f9, 0x2bbaec7f, 0x89fcbc93, 0xbe2d188b`). In the title menu scene, these CRCs barely fire (0x009f8918 fires occasionally but the bright sky is from a different pass).

**Resolution paths**:
- Bisect via Magic Ink to find what actually draws the bright sky in this scene
- Add discovered CRC to a new env-driven skip list
- OR address the underlying fog bug so the sky is properly hidden by atmospheric scattering

---

## Tooling false negatives

### Magic Ink "no skip" state actually still skipping

**This was a multi-hour debugging mystery.**

**Cause**: `Sn2MagicInk.hpp::parse_crc_csv` treated `#` as a comment-marker that skipped only the next whitespace-separated token. A comment like `# bisection done — fog producer is 0x37558de4` had EVERY token after `#` parsed individually. The `0x37558de4` token was a valid hex number → added to skip set.

**Symptom**: User "cleared" the ink_skip file with a comment containing previous-test CRCs. The LEFT eye visibly lost fog rendering. We assumed this was a different bug (mirror corruption, etc.) and spent hours investigating.

**Resolution**: Fixed parser to skip to end-of-line on `#`:
```cpp
if (s[pos] == '#') {
    while (pos < s.size() && s[pos] != '\n') ++pos;
    continue;
}
```

**Lesson**: Diagnostic tools must have their negative case (empty / no-op) be visually identical to "not enabled at all". User real-time observation caught what static reasoning missed.

### Screenshot static image misses flicker

**Cause**: Single-frame screenshots can capture a "good" frame in a flickering pattern, hiding the bug.

**Resolution**: Trust user real-time observation. When testing fixes, wait + take multiple screenshots, or have user verify in Meta XR Simulator.

---

## Descriptor registry gaps

### `sn2_descriptor_registry::lookup_resource_by_cpu_ptr` returns null for bindless slots

**Cause**: The descriptor registry hooks `Create{CBV,SRV,UAV,RTV}` calls but in some scenes, descriptors get into bindless slots via a path UEVR doesn't intercept (could be `CopyDescriptors` from a heap UEVR doesn't track, or a separate worker thread, or direct memory writes).

**Symptom**: Diagnostic dumps show "slot0=0x0 slot1=0x0 ..." for tables that clearly have bound resources.

**Resolution paths**:
1. **Use a different state path**: For VoxelizePS we used `state.last_rtv_handles` (tracked via `OMSetRenderTargets` hook) instead of descriptor tables. RTV path is more reliable.
2. **Extend tracking**: Hook `ID3D12DebugCommandList::AssertResourceState` for parallel tracking. Hook every variant of `CopyDescriptors`. Long-term project.
3. **Byte-decode descriptors**: Read descriptor bytes from CPU handle directly, extract GPU_VA, match to known resources. Vendor-specific format.

---

## RT state crashes

### "Resource being used as render target but not in RT state"

**Cause**: D3D12 validation. When you bind a resource via `OMSetRenderTargets`, the runtime expects it in `RENDER_TARGET` state. Mirror was created in `UNORDERED_ACCESS` → state mismatch.

**Resolution**: 
- **Implicit**: Create mirror in `COMMON` state. D3D12 implicit promotion rules lift it to RT/UAV/SRV as needed for non-tracked promotion-capable resources. After the draw, decays back to COMMON.
- **Explicit**: Insert `ID3D12GraphicsCommandList::ResourceBarrier(D3D12_RESOURCE_TRANSITION{ res, before, after })` before each state change.

We use implicit (COMMON) for simplicity. Future: explicit barriers if implicit causes issues at scale.

---

## Wrong fog colors with synth

### Right eye gets DIFFERENT fog (not just missing) — colors wrong

**Cause**: We use a donor PSO's right-eye CB as the synth source. The donor (`0x4D44CE74`, basepass PS) and target (`0x9D14FCF0`, VoxelizePS) share the View matrix portion of the View CB but may differ in:
- Lumen scattering parameters
- Volumetric fog density tables
- Atmospheric scattering coefficients
- Any per-shader-permutation parameters

**Resolution paths** (priority order):
1. **Proper synthesis**: snapshot VoxelizePS's OWN LEFT CB + apply donor's per-eye-diff (the 80 divergent slots) to produce a fully-correct synthesized right CB. Requires running `diff_cb_dumps.py` per-frame.
2. **Better donor**: snapshot from a different both-eye PSO that shares more state with VoxelizePS (e.g., another fog-related shader). Need to find one.
3. **Field-by-field patching**: identify which specific fields VoxelizePS reads (via DXBC reflection or `decode_view_cb.py`), patch only those from observed donor diff.

---

## Subagent bugs we caught

### Sn2EyeScreenshot eye_bucket enum mismatch

**Cause**: Subagent built `note_scene_color_write` assuming `eye_bucket == 0` for left, `== 1` for right. UEVR's `StereoTraceBucket` enum is:
```cpp
enum class StereoTraceBucket {
    Unknown = 0,
    Left = 1,
    Right = 2,
    Full = 3,
    Multi = 4
};
```

So the gate `if (eye_bucket != 0 && eye_bucket != 1) return` rejected LEFT (=1) wrongly (passes != 0 but fails != 1, returns).

**Resolution**:
```cpp
if (eye_bucket != 1 && eye_bucket != 2) return;
if (eye_bucket == 1) s.latest_left = scene_color;
else if (eye_bucket == 2) s.latest_right = scene_color;
```

**Lesson**: When delegating to subagents, communicate enum values explicitly. Don't assume conventions.

### Sn2RtDiff had the same bug (same subagent author)

Fixed in the same patch.

---

## General principles learned

### 1. Test the negative case
Whenever you build a diagnostic / config tool, verify that the "off / empty / disabled" state is visually identical to "not enabled at all". This catches the silent-modifier bug class.

### 2. Don't lock the build
A running game process locks the DLL. Either kill before rebuild, or build to a different path and swap.

### 3. Trust real-time observation
A static screenshot can lie. If user reports flicker, believe them, even if your captured frame looks correct.

### 4. Validate enum mappings across module boundaries
Subagents/subsystems may use different enum conventions. Always pass through the canonical value (here: `StereoTraceBucket`).

### 5. D3D12 state transitions are strict
COMMON for non-tracked resources is the safest default. Explicit barriers when needed. Don't mix promotion + barriers.

### 6. Hot-reload everything that benefits from iteration
Magic Ink lives in a file polled every 60 frames → ~1 second iteration. Same pattern for dup_cfg, fix rules, etc. would massively speed up future debug sessions.

### 7. Document while building
This file (and the sibling ARCHITECTURE.md, MODULES.md, etc.) catches insights at the moment they're learned. Without these docs, the institutional memory is lost between sessions.
