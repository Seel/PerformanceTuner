# AGENTS.md

Notes for anyone (human or agent) working on this repository. They record the
non-obvious findings from porting the mod from a `dxgi.dll` proxy to an F4SE plugin.

## What this repository is

An F4SE plugin for Fallout 4 that keeps the frame rate inside a configurable band by
moving the directional shadow distance at runtime. It replaces the original `dxgi.dll`
proxy, which is preserved under `legacy/`.

- Game: developed on AE 1.11.240.0; OG 1.10.163.0 (GOG) is also tested.
- Runtime dependency: F4SE + the Address Library for F4SE Plugins.
- Build dependency: CommonLibF4, pinned as the `lib/commonlibf4` submodule
  (`Dear-Modding-FO4/commonlibf4`, branch `main`).

## Layout

```
src/main.cpp      F4SE entry points, config load, permanent per-frame task
src/Tuner.cpp     controller: frame timing, shadow distance, godray quality
src/Config.cpp    INI parsing/validation
res/              shipped PerformanceTuner.ini template
lib/commonlibf4   git submodule (build dependency)
legacy/           original VS2015 dxgi proxy and solution
backup/           local-only archive of the legacy proxy that was installed in the game
```

## Building

Requirements: Visual Studio 2022 with "Desktop development with C++" (MSVC 14.4x and a
Windows 10/11 SDK) and xmake 3.0 or newer.

```bat
git clone --recurse-submodules <repo>
xmake f -m release -a x64
xmake build
```

Output: `build\windows\x64\release\PerformanceTuner.dll`.

- MSVC is required. CommonLibF4 is C++23 with MSVC ABI assumptions; MinGW cannot build
  the plugin. (MinGW *can* build the legacy proxy after small portability fixes; it was
  verified with g++ 8.1, but that is not maintained.)
- `XSE_FO4_GAME_PATH` (game folder) or `XSE_FO4_MODS_PATH` (mod manager folder) makes
  `xmake install` copy the DLL and INI into `Data\F4SE\Plugins`.

## Testing

- Install to `...\Fallout 4\Data\F4SE\Plugins\PerformanceTuner.dll` and `.ini`.
- Launch through F4SE. Plugin log:
  `Documents\My Games\Fallout4\F4SE\PerformanceTuner.log` (truncated each launch).
- `bShowDiagnostics=1` logs one line every `1/fDebugMessageFrequency` seconds with
  `cached=`, `setting=`, `momentum=`, `load=`, `fps=`, `median=`, `paused=`, `grace=`.
- The task only runs while the world renders; main menu, loading, pause, Pip-Boy and
  sleep/wait menus are gated.

## The single most important finding: settings are not the render values

`RE::ShadowSettings::GetDirShadowDistance()` returns a `Setting` object, but **the
renderer does not read it while the game is running**. `BSShaderManager` copies the INI
value into internal caches during initialization:

```cpp
RE::BSShaderManager::GetCachedDirectionalShadowDistance()        // float&, what shadows use
RE::BSShaderManager::GetCachedLocalShadowDistance()              // float&, local lights
RE::BSShaderManager::GetCachedLocalShadowDistanceSquared()       // float&
RE::BSShaderManager::GetCachedDirectionalBlendWidth()            // float&
```

For 1.11.240 these live contiguously at RVAs `0x3E71D80`…`0x3E71D8C`
(`CachedShadowMapResolution` 0x3E71D80, local 0x3E71D84, local² 0x3E71D88,
directional 0x3E71D8C).

Observed behaviour when only the `Setting` is written:

- lowering it below the cached value *does* visibly shorten shadows (caster culling
  reads the setting), so it looks like it works;
- raising it above the cached value does **nothing** (the baked shadow camera range
  wins), which is the bug that took several in-game tests to isolate.

The plugin therefore writes `GetCachedDirectionalShadowDistance()` and never edits the
game INIs. Verified in game: `10` removes shadows almost completely, `3000 -> 8000`
visibly stretches them.

Do not "fix" this by also writing `fShadowDistance`/`fDirShadowDistance`: the presets
keep them equal, but they are only startup inputs.

Relevant IDs for AE 1.11.240 (from `version-1-11-240-0.bin`):

| Symbol | AE ID | RVA |
|---|---|---|
| `ShadowSettings::fDirShadowDistance` | 4784973 | 0x2F99690 |
| `BSShaderManager::CachedDirectionalShadowDistance` | 2713320 | 0x3E71D8C |
| `UI::Singleton` | 4796314 | 0x30E8930 |
| `UI::GetMenuMapRWLock` | 2707105 | 0x3DBD370 |
| `BSTimer::Singleton` | 2703179 | 0x339F420 |
| `Console::ExecuteCommand` | 2248537 | 0x1036250 |

Relevant IDs for OG 1.10.163 (verified against `version-1-10-163-0.bin`):

| Symbol | OG ID | RVA |
|---|---|---|
| `Main::OnIdle` | 633524 | 0xD38E60 |
| `Main::Run` | 1125396 | 0xD36D50 |
| `BSShaderManager::CachedDirectionalShadowDistance` | 777729 | 0x67333DC |
| `ShadowSettings::fDirShadowDistance` | 962234 | 0x38CB438 |
| `UI::Singleton` | 548587 | 0x58D0898 |
| `BSTimer::Singleton` | 1256126 | 0x5B5B6C0 |
| `Console::ExecuteCommand` | 1061864 | 0x125B4A0 |

The OG cache RVA (`0x67333DC`) is exactly the address the original dxgi mod called
`fShadowDirDistance`, confirming that the old global and the modern cache are the same
variable.

## Godray quality

There is no cached wrapper and `iVolumetricLightingQuality` is not in
`INISettingCollection` (`GetINISetting` returns null). The only reliable path is the
game's own command, issued when the quality bucket changes:

```cpp
RE::Console::ExecuteCommand("gr quality 2");
```

Quality is derived from the shadow distance with `fGRQualityShadowDist1..3` and clamped
to `iMinGRQuality..iMaxGRQuality`. It is off by default (`bAdjustGRQuality=0`).

The godray **grid** is the alternative performance knob (`bAdjustGRGrid=0` by default).
`gr grid <size>` sets the base godray grid resolution (the executable's help text says
so; there is also an internal `UpdateGodraySettings`). There is no INI key for it and
`BSShadowLight::godrayGridResolution` is only a per-light copy, so the console command
is the reliable path. The plugin interpolates the grid from `iGRGridMin` (at
`fShadowDirDistanceMin`) to `iGRGridMax` (at `fShadowDirDistanceMax`), rounded to an
integer, and only issues a command when the value changes and the distance has moved at
least 150 units since the last change (hysteresis against flutter). Both knobs can be
enabled together for maximum savings. Godray values are re-applied after every
load/menu resume because the engine may recreate the volumes.

The `gr` command handler was disassembled to understand the cost: for `grid` it simply
stores `(int)argument` into an engine global (AE 1.11.240: RVA `0x3E7136C`, an `int32`
in `.data`); the renderer reads that global every frame. The command is therefore only
a writer, and each call echoes/prints in the console.

The plugin locates that global silently, without running the command: it finds the
`UpdateGodraySettings` entry in the console function table through
`RE::SCRIPT_FUNCTION::LocateConsoleCommand`, scans the handler for the grid branch's
`F3 0F 2C C1 89 05` store (present in both OG and AE), and extracts the RIP-relative
target, validating that it lands inside `.data`. The resulting address is used from then
on as a plain int store — no command, no echo, no parsing. If the command or a unique
candidate is not found it logs a warning and falls back to the console command path.
`iGRGridStep` still quantises the values.

Note: an earlier attempt calibrated by executing `gr grid 6/13/6` and diffing `.data`;
that fails because `Console::ExecuteCommand` does not apply the command synchronously,
so the scan runs before the global changes.

## Per-frame execution

- `F4SE::GetTaskInterface()->AddTaskPermanent(...)` runs the tick once per frame on the
  main thread. No hooks, no trampolines, no Present interception.
- Frame time comes from `RE::BSTimer::GetSingleton()->realTimeDelta` (seconds). It is
  **quantized to whole milliseconds** (`realDeltaMs`), which matters for controller
  tuning: bands narrower than ~1 ms are noise.
- Do not export `F4SEPlugin_Preload`. CommonLibF4's `Init(PreLoadInterface*)` and
  `Init(LoadInterface*)` each own a separate `once_flag`; the template's habit of
  calling the load initializer from the preload export leaves `taskInterface` null.
  `F4SE_PLUGIN_LOAD` alone is sufficient.

## Controller lessons

The controller is deliberately more complicated than the original:

- Load estimate = **median** frame time over a **time window** (`fLoadWindowTime`,
  default 2 s). A mean over a fixed frame count reacted to single hitches; a 30-frame
  window (~0.45 s) flipped completely on 0.5 s camera-turn spikes.
- Asymmetric FPS band: grow while FPS > `fMaxFPS`, pull in while FPS < `fMinFPS`,
  hold in between. Internally `load = frameTime / (1000 / fMaxFPS)`; grow when
  `load < 1`, shrink when `load > fMaxFPS / fMinFPS`.
- The band must be wider than the scene's natural frame-time noise. With a narrow band
  the tuner chased noise in a 2000 ↔ 8000 limit cycle ("heartbeat").
- **Momentum zero trap**: once inside the band momentum is set to 0; multiplying by the
  acceleration factor then keeps it at 0 forever. Seed `+1`/`-2` when leaving the band.
- Sample buffer is reset and a grace period (`fResumeGraceTime`) is applied after every
  menu/load transition, so streaming hitches cannot trigger a slide.
- Downward response is intentionally faster than upward (`MomentumDown...` vs
  `MomentumUp...`).

## Runtime compatibility (F4SE generations)

The plugin is built against the F4SE 0.7.x (NG/AE) API generation. This matters more
than the game version itself:

| Runtime | F4SE | Status |
|---|---|---|
| AE 1.11.x | 0.7.x | supported, tested on 1.11.240 |
| NG 1.10.980/984 | 0.7.x | same API and NG IDs are compiled in; untested |
| OG 1.10.163 (incl. GOG) | 0.6.23 | supported, tested on GOG 1.10.163 (OnIdle hook) |

Why OG needs a different tick: F4SE 0.6.23 has no `F4SEPlugin_Version` export, so the
plugin metadata (`addressIndependence`, `compatibleVersions`) is never read (harmless),
and its `F4SETaskInterface` has only `AddTask` / `AddUITask` — no `AddTaskPermanent`. It
also drains the task queue in a `while (!s_tasks.empty())` loop, so a task must not
re-queue itself; it would run again in the same pass, forever.

The plugin branches on `F4SE::GetF4SEVersion()`. 0.7.x+ uses `AddTaskPermanent`. 0.6.x
(OG) patches the `call Main::OnIdle` instruction in the main loop: `FindOnIdleCallSite`
scans `Main::Run` and `Main::Run_WindowsMessageLoop` for a direct `E8` call whose target
is `Main::OnIdle`, then `REL::Trampoline::write_call<5>` replaces that one instruction.
The hook calls the original OnIdle and then runs the tick. If no call site is found (or
the whole-`.text` scan is ambiguous), the plugin logs an error and fails to load; the
game continues without tuning.

Validated on GOG 1.10.163.0 with F4SE 0.6.23: `Main::Run` makes a single direct
`call Main::OnIdle` (call at RVA `0xD36E0B`, target `0xD38E60`) and OnIdle receives
`this` in RCX. The hook was tested in game; the log shows
`Hooked the Main::OnIdle call at 0x...`.

OG still needs `version-1-10-163-0.bin` next to the plugin and F4SE 0.6.23; the OG IDs
are compiled in through `COMMONLIB_RUNTIMECOUNT=3`.

F4SE also ignores `CompatibleVersions` when the plugin declares AE address-library and
layout independence, so within AE 1.11.x a new patch only needs an updated Address
Library file — no plugin rebuild. A new runtime family (e.g. 1.12.x) changes the
independence constants and does require a rebuild against updated CommonLibF4.

## Updating for a new game version

1. Update the submodule to the CommonLibF4 commit that supports the new runtime family
   (`git submodule update --remote`, review, commit the new pin).
2. Install the matching Address Library file
   `Data\F4SE\Plugins\version-<major>-<minor>-<patch>-0.bin`.
3. Rebuild. IDs are resolved at runtime; there are no offsets in this repository.
4. If a required ID is missing, CommonLibF4 fails loudly in the plugin log. The plugin
   metadata currently declares `CompatibleVersions({ F4SE::RUNTIME_LATEST })` and
   layout dependence; extend it only after testing OG/NG layouts.

`f4rd-runtime.bin` (CommonLibF4RD) is a different resolver used by other installed
mods; this plugin uses CommonLibF4's Address Library path.

## Environment notes (development machine)

- Visual Studio 2022 Build Tools 17.14 / MSVC 14.44 / Windows SDK 10.0.26100 installed
  through the VS bootstrapper (`--quiet --wait --norestart --add
  Microsoft.VisualStudio.Workload.VCTools --includeRecommended`).
- xmake installed per-user through scoop (`scoop install xmake`).
- ReShade 6.7.2 now occupies the game's `dxgi.dll`; the legacy proxy chain
  (`dxgi.dll` -> `dxgi_linked.dll`) was removed. The proxy and its INIs are archived in
  `backup/legacy-dxgi/` (git-ignored).

## Licensing

The plugin code is MIT (original copyright 2017 Daniel Pitts, see `LICENSE`).
CommonLibF4 is GPL-3.0-or-later with a modding exception; distributed binaries that link
it are subject to those terms.

