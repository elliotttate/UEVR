# Subnautica 2 Mono 120 Hz Performance Writeup

Date: 2026-06-14
Repo: `E:\Github\UEVRJ`
Reference checkout: `E:\Github\UEVR`
Runtime under test: Meta XR Simulator, configured for 120 Hz
Game profile: `%APPDATA%\UnrealVRMod\Subnautica2-Win64-Shipping`

## Summary

The original mono path was not limited by shaders, game rendering, or direct CPU work inside `UEVRBackend.dll`. Profiling and runtime telemetry showed that the slow path was induced OpenXR/kernel/driver work: the fork was submitting mono through a stereo-shaped OpenXR path with a double-wide scene image and render-thread waits around OpenXR frame pacing and swapchain handoff.

The main fixes were:

- Request and verify the Meta/OpenXR 120 Hz display refresh rate through `XR_FB_display_refresh_rate`.
- Make mono allocate and submit a single-eye-width scene swapchain instead of a double-wide scene image.
- Move `xrWaitFrame` off the render thread for mono into a persistent async worker.
- Pre-acquire the mono scene swapchain on that worker after `xrWaitFrame`.
- Defer mono scene swapchain release to the async worker by default.
- Add telemetry that separates `xrWaitFrame`, `xrBeginFrame`, `xrEndFrame`, swapchain acquire/wait/copy/release, and per-callsite wait cost.

After the changes, low-resolution mono can hit the requested 120 Hz cadence, and full-resolution mono improved dramatically from the original 25-40 FPS range to the high-110s with short windows reaching essentially 120 FPS. The remaining full-resolution loss is tied to Meta XR Simulator/OpenXR compositor handoff cost for a full `1680x1760` scene image submitted as stereo projection views.

## Baseline Observations

The first decisive profiler result was that `UEVRBackend.dll` was not directly consuming the missing CPU time. The heavy CPU was in the game, Windows kernel, and NVIDIA driver/runtime. That meant our code was likely causing expensive runtime/driver synchronization, not simply burning CPU in fork code.

Clean mono with shaders already compiled still showed a large render-thread Draw cost:

- Our pre-fix build: about `37 ms` Draw, about `27 FPS`.
- Native-stereo-fix change: still about `37 ms` Draw, about `27 FPS`.
- Reference `E:\Github\UEVR`: about `3.4 ms` Draw, about `38 FPS` in the same earlier comparison.

That ruled out shader compilation and the native-stereo-fix submit path as the dominant bottlenecks.

The next important observation was that mono in this fork was running through the DIBR/mono machinery and still used stereo-shaped OpenXR submission. Logs showed mono mirroring and a swapchain shape that was effectively wider than needed. GPU time was low compared with frame time, so the problem was render-thread/runtime synchronization, not GPU fill alone.

## Refresh Rate Findings

The Meta XR Simulator UI showed 120 FPS, but we needed runtime proof. The code now enables `XR_FB_display_refresh_rate`, loads the extension functions, requests `UEVR_OPENXR_REQUEST_REFRESH_HZ=120`, and reports the result through `/api/render/vr-state`.

Runtime telemetry confirmed:

- `display_refresh_rate_request_result_name = XR_SUCCESS`
- `display_refresh_rate_current_hz = 120`
- `predicted_display_period_ns = 8333333`
- `predicted_display_hz = 120`

So the session is actually running against a 120 Hz OpenXR prediction cadence. The remaining misses are not caused by the simulator staying at 60 or 90 Hz.

## Game Pacing Findings

Live Unreal CVars were checked through diagnostics:

- `t.MaxFPS = 0`
- `r.VSync = 0`
- dynamic resolution was not enforcing a frame-time budget
- no obvious `rhi.SyncInterval` cap was identified

The old mono submit throttle path was also removed from the active full-rate path. Telemetry after the fix showed:

- `mono_openxr_skipped_submit_count = 0`

So the current full-res misses are not caused by an intentional UEVR mono submit skip or a game-side FPS cap.

## Single-Wide Mono Swapchain

A major issue was that mono was still behaving like a double-wide stereo scene in parts of D3D12/OpenXR setup.

Changes made:

- Added `VR::is_mono_rendering_configured()` so OpenXR sizing can use the configured render mode before the runtime is fully active.
- Made mono OpenXR width/height respect `UEVR_MONO_OPENXR_RENDER_SCALE` using configured mono state.
- Changed D3D12 setup so mono keeps the scene backbuffer at one eye width instead of halving and then treating it like a double-wide stereo texture.
- Changed FFakeStereoRenderingHook mono view rect and render target sizing to use one-eye dimensions.
- Changed OpenXR projection rect selection so mono single-wide submit uses the full mono image for both submitted views.

Verified runtime state after the single-wide work:

- HMD/backbuffer: `1680x1760`
- Both eyes reported `OpenXR/MONO_SINGLE_WIDE`
- Projection submit used mono single-wide rects instead of right-half double-wide sampling

This was one of the biggest structural fixes. Lowering render scale after this change proved that resolution still mattered, but the path was no longer wasting a double-wide color image in plain mono.

## Async Wait and Pre-Acquire

Before the async wait work, `xrWaitFrame` could land on the render thread in the late/very-late submit path. That caused the render thread to pay runtime pacing waits directly.

Changes made:

- Added `VRMonoAsyncPostPresent` as a separate wait callsite.
- Added a persistent mono OpenXR worker thread.
- The D3D12 mono submit path requests async wait immediately after a successful `xrEndFrame`.
- The worker calls `openxr->synchronize_frame(..., VRMonoAsyncPostPresent)`.
- After a successful async wait, the worker pre-acquires and waits the mono scene swapchain image before the render thread needs it.
- The worker is named `UEVR Mono OpenXR Wait` and raised to `THREAD_PRIORITY_HIGHEST`.

Telemetry showed the intended shift:

- `wait_runtime_fix_ms = 0`
- `wait_very_late_ms = 0`
- `wait_mono_async_ms` holds the OpenXR wait cost

This moved the major pacing wait out of the render-thread submit path.

## Async Release

After pre-acquire, the remaining render-thread cost was not the copy payload. It was OpenXR swapchain handoff, especially release/end-frame cost.

The scene copy payload itself was small:

- Full-res copy execution was around `0.09-0.10 ms` in representative samples.
- Skipping the scene copy did not remove the remaining frame miss.

The useful change was to defer mono scene swapchain release:

- Default behavior now defers the mono scene swapchain release to the async worker.
- `UEVR_MONO_OPENXR_ASYNC_RELEASE=0` disables this path.
- If `xrEndFrame` fails, the code falls back to immediate release to avoid leaving an acquired image outstanding.

This reduced the render-thread submit cost in good runs and helped short windows reach near 120 Hz.

## Performance Measurements

Representative progression from the investigation:

| State | Resolution | Result | Notes |
| --- | --- | ---: | --- |
| Original problem | full | about 25-40 FPS | large Draw/render-thread cost |
| Clean mono, shaders done | full | about 27 FPS | not shader compile |
| Single-wide and pacing improvements, before final async work | full | about 102-118 FPS | render-thread wait moved out |
| Persistent async wait, full | `1680x1760` | about 116 FPS | wait moved to async callsite |
| Pre-acquire, full | `1680x1760` | about 119.1 FPS | remaining acquire/release/end cost |
| Async release, full short window | `1680x1760` | up to about 119.8-120.0 FPS | best short windows hit target |
| Async release, full longer/noisy windows | `1680x1760` | about 117-119 FPS | Meta/runtime handoff remains noisy |
| Async release, low scale | `168x176` | about 120.1 FPS | proves refresh/pacing can hit 120 |

The strongest discriminator was low render scale. With the same OpenXR frame loop, low resolution hit the 120 Hz cadence. Full resolution still loses frames in some windows. That means the remaining limit scales with submitted scene image/compositor handoff size, not with game tick, refresh request, shaders, or our old mono throttle.

## Rejected Hypotheses

These were tested and did not explain or fix the remaining full-res miss:

- Shader compilation: clean read after shaders completed still showed the problem.
- Native stereo fix path: disabling/adjusting it did not move Draw time.
- Direct UEVRBackend CPU burn: profiler showed `UEVRBackend.dll` was a tiny CPU percentage.
- Game-side cap: live CVars did not show a 60/90/120 clamp, and low-res hit 120.
- Intentional mono submit skip: telemetry showed zero skipped submits in fixed runs.
- Depth submit: disabling OpenXR depth submit did not materially improve full-res.
- UI/quad layers: skipping UI copy/layers made performance worse in testing.
- Transfer-only scene swapchain usage: acquire/copy got cheaper, but `xrEndFrame` got worse and total FPS regressed.
- Process priority High: made the result worse.
- True OpenXR mono view config: Meta XR Simulator reported only `PRIMARY_STEREO`; `PRIMARY_MONO` was not supported.

## Current Bottleneck

The remaining full-resolution gap is best described as Meta XR Simulator/OpenXR compositor handoff overhead for a full-size scene image that must still be submitted as stereo projection views.

The runtime supports only:

- `XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO`

It does not support:

- `XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO`

So even plain mono must submit through a stereo projection layer shape. We can use one mono source image and one-eye-width swapchain, but the runtime still composites stereo views. At full `1680x1760`, the expensive buckets are still `xrEndFrame` plus swapchain acquire/release. At `168x176`, the same path reaches 120 Hz.

## Useful Runtime Knobs

Implemented/used knobs:

- `UEVR_OPENXR_REQUEST_REFRESH_HZ=120`
  - Requests the Meta/OpenXR display refresh rate through `XR_FB_display_refresh_rate`.
- `UEVR_MONO_OPENXR_UNPACED=0`
  - Opts out of the unpaced mono submit behavior.
- `UEVR_MONO_OPENXR_ASYNC_WAIT=0`
  - Opts out of async mono `xrWaitFrame`.
- `UEVR_MONO_OPENXR_ASYNC_RELEASE=0`
  - Opts out of async mono scene swapchain release.
- `UEVR_MONO_OPENXR_RENDER_SCALE=<float>`
  - Scales mono OpenXR render size for testing.
- `UEVR_OPENXR_PRIMARY_MONO=1`
  - Diagnostic probe for `XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO`. Meta XR Simulator did not support it.

## Diagnostics Added

`/api/render/vr-state` now exposes:

- predicted display period/time/hz
- display refresh extension state
- current/requested display refresh rate
- refresh request result
- supported refresh rates
- selected runtime/session state
- live pacing CVars

`/api/render/frame-timing` now exposes:

- total timing, not only average/max
- OpenXR wait callsite buckets
- swapchain acquire
- swapchain wait
- D3D12 command wait
- copy record
- copy execute
- swapchain release
- mono skipped submit count

These were necessary to separate render-thread work from async wait/compositor pacing.

## Current Practical Conclusion

The fork is no longer stuck at 25-40 FPS because of the old mono implementation. It can hit 120 Hz when the submitted mono image is very small, and it can reach near-120 short windows at full Meta Quest 3 simulator resolution. The remaining full-res instability is not a hard cap and not a game-side cap. It is the cost/noise of full-resolution OpenXR compositor handoff in the Meta XR Simulator while using the only supported stereo view configuration.

The most promising future directions would be:

- Test the same build on a real runtime/headset or another OpenXR runtime to see whether the Meta simulator handoff is uniquely expensive.
- Investigate whether a composition-layer format or runtime-specific submit path can reduce `xrEndFrame` cost for full-res stereo projection views.
- Keep `UEVR_MONO_OPENXR_RENDER_SCALE` available as a practical quality/perf knob for simulator testing.
- Avoid revisiting shader compile, native-stereo-fix, UI layers, depth submit, or process priority unless new telemetry contradicts these findings.
