# Launching Steam Games with UEVRRenderDocLauncher

`UEVRRenderDocLauncher.exe` needs to create the game process itself (suspended,
RenderDoc injected before UEVR, resumed only after the prehook is armed). Steam
games fight this: launched outside Steam, they call
`SteamAPI_RestartAppIfNecessary` during startup, exit, and ask Steam to relaunch
them — and the Steam-relaunched process has none of our injection.

Measured on Subnautica 2 (Steam build, AppID 1962700): the directly-launched
exe exits after ~4 seconds and Steam spawns a fresh, uninjected instance ~14
seconds after that. If your game "closes and reopens itself", this is what's
happening.

## Why the workarounds work

Two implementation facts about the launcher make all three methods compose
with it (see `tools/renderdoc-launcher/RenderDocLauncher.cpp`):

- It calls `CreateProcessW` with a null environment block, so **the game
  inherits the launcher's environment** — anything you `set` in the shell
  before running the launcher reaches SteamAPI inside the game.
- When `--cwd` is omitted, **the working directory defaults to the exe's
  folder**, which is where SteamAPI looks for `steam_appid.txt`.

`SteamAPI_RestartAppIfNecessary` returns false (no relaunch) when it can
already tell which app it is: from the `SteamAppId` environment variable, from
`steam_appid.txt` in the working directory, or from an actual Steam launch
context. Steam itself must be running and logged in for all methods — the game
still talks to the Steam client; the workarounds only suppress the *relaunch*.

## Method 1 — `steam_appid.txt` (simplest)

Create a file named `steam_appid.txt` containing only the AppID (the number in
the game's store page URL, or in `steamapps/appmanifest_<id>.acf`), next to the
**shipping** exe:

```
echo 1962700 > "E:\SteamLibrary\steamapps\common\Subnautica2\Subnautica2\Binaries\Win64\steam_appid.txt"
UEVRRenderDocLauncher.exe --exe "E:\...\Binaries\Win64\Subnautica2-Win64-Shipping.exe"
```

Pros: set-and-forget. Cons: leaves a file in the install dir (some games'
integrity checks re-download around it; harmless but noisy).

## Method 2 — environment variables (no file, verified end-to-end)

```bat
set SteamAppId=1962700
set SteamGameId=1962700
UEVRRenderDocLauncher.exe --exe "E:\SteamLibrary\steamapps\common\Subnautica2\Subnautica2\Binaries\Win64\Subnautica2-Win64-Shipping.exe" --args "-windowed -ResX=1280 -ResY=720"
```

This exact sequence was verified against Steam Subnautica 2 on 2026-06-11:

1. Launcher output: suspended create → `Injecting RenderDoc first` →
   `Injecting UEVR backend` → ready event → `Resumed process. UEVR/RenderDoc
   were resident before the game main thread ran.`
2. No exit, no Steam relaunch (watched 45+ s; the unpatched control bounced
   at 4 s).
3. Backend log (`%APPDATA%\UnrealVRMod\<exe-stem>\log.txt`):
   `[RenderDoc] integration READY: v1.7.0 (preloaded=true ... capture_safe=true)`
   and the first observed device reports `wrapped=true
   vtable_module='...\renderdoc.dll'`.
4. A capture request (`%TEMP%\uevr_renderdoc_capture.req`, first line = output
   path template) produced a 2.2 GB `.rdc` of the menu frame, which
   `renderdoccmd thumb` opened and thumbnailed successfully.

Note the `.rdc` keeps growing for a while after it first appears — wait for
the size to settle before validating.

## Method 3 — Steam Launch Options (when the game insists on a real Steam launch)

Game Properties → Launch Options:

```
"<bundle>\UEVRRenderDocLauncher.exe" --exe "<full path to Game-Win64-Shipping.exe>" -- %command%
```

Steam starts the launcher inside the full Steam context; the game inherits it.
Two things to get right:

- **Keep the explicit `--exe` aimed at the shipping exe.** For many UE titles
  `%command%` expands to a root-level bootstrap stub that respawns the real
  game. The launcher injects only into the process *it* creates — injecting
  the stub captures nothing. The trailing `-- %command%` exists purely to
  absorb Steam's substitution (the launcher passes it through as a harmless
  extra game argument; the arg parser rejects unknown options outside the
  `--` tail, so don't drop the `--`).
- **Disable the Steam overlay** for the game (Properties → General).
  `GameOverlayRenderer64.dll` is a third Present-hooking framework in the
  process; captures are cleaner without it. Methods 1 and 2 avoid the overlay
  automatically since Steam never sees the launch.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Game exits seconds after launch, then reopens by itself | The bounce — no workaround active. Check spelling: `SteamAppId`, not `SteamAppID`; `steam_appid.txt` next to the *shipping* exe, not the root stub. |
| Game exits and does NOT reopen | Usually not Steam — check the launcher output and the backend log; could be DRM that requires a real Steam launch → use Method 3. |
| Steam shows "Failed to start game (app already running)" | A previous instance is still alive — kill it before relaunching. |
| Everything launches but the RenderDoc overlay is missing | Verify the log shows `capture_safe=true`; if `d3d12_loaded_before` is true *and* `preloaded=false`, the injection ordering was violated — relaunch via the launcher rather than attaching late. |
| Captures are tiny / black | You captured a loading frame. Wait for the menu/scene, then re-trigger. |

AppID lookup: store page URL (`store.steampowered.com/app/<id>/`),
`steamapps/appmanifest_<id>.acf` (`"appid"` field), or steamdb.info.
