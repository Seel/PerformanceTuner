# AGENTS.md

Notes for anyone (human or agent) working on this repository. They record the
non-obvious findings from porting the mod from a `dxgi.dll` proxy to an F4SE plugin.

## What this repository is

An F4SE plugin for Fallout 4 that keeps the frame rate inside a configurable band by
moving the directional shadow distance at runtime. It replaces the original `dxgi.dll`
proxy, which is preserved under `legacy/`.

- Game: Fallout 4 AE 1.11.240.0 was the development target.
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

## Godray quality

There is no cached wrapper and `iVolumetricLightingQuality` is not in
`INISettingCollection` (`GetINISetting` returns null). The only reliable path is the
game's own command, issued when the quality bucket changes:

```cpp
RE::Console::ExecuteCommand("gr quality 2");
```

Quality is derived from the shadow distance with `fGRQualityShadowDist1..3` and clamped
to `iMinGRQuality..iMaxGRQuality`. It is off by default (`bAdjustGRQuality=0`).

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
