# Dynamic Performance Tuner for Fallout 4

An F4SE plugin that keeps the frame rate inside a configurable band by adjusting the
directional shadow distance of the game in real time.

It is the modern replacement for the original `dxgi.dll` proxy of the same name:
no DXGI hijack, no ReShade/ENB conflict, no hard-coded addresses and no per-version
offset files. Game versions are resolved through the F4SE Address Library.

## How it works

- The renderer does not read `fDirShadowDistance` while the game is running: at startup
  `BSShaderManager` copies it into an internal cache (`CachedDirectionalShadowDistance`)
  and the shadow code reads the cache. The plugin drives that cache directly, which is
  why tuning works without editing any game INI.
- Frame times are measured once per frame and reduced to a median over a configurable
  time window, so camera turns, streaming and other short hitches do not trigger the
  controller.
- While FPS is above `fMaxFPS` the shadow distance grows; while FPS is below `fMinFPS`
  it is pulled in. Between the two it holds. The two edges are independent, so the
  response can be as tight or as relaxed as you like.
- Movement uses "momentum": the distance accelerates while the load stays on one side
  and resets when it crosses over. Moving down is deliberately faster than moving up.

## Requirements

- **Fallout 4 AE 1.11.x** (tested on 1.11.240). The plugin relies on the
  `AddTaskPermanent` task interface and the plugin metadata introduced with the
  F4SE 0.7.x (NG/AE) generation.
- The NG runtime (1.10.984) uses the same F4SE API and the plugin carries NG IDs, but
  it has not been tested.
- The original OG runtime (1.10.163, including the GOG release) uses F4SE 0.6.23, which
  predates `AddTaskPermanent`. An experimental fallback (a paced task) is included, but
  it has not been tested on an OG install yet.
- F4SE matching the game version
- [Address Library for F4SE Plugins](https://www.nexusmods.com/fallout4/mods/47327)
  (`version-<game version>.bin` in `Data\F4SE\Plugins`)
- The plugin DLL built below

## Installation

Copy `PerformanceTuner.dll` and (optionally) `PerformanceTuner.ini` into:

```
Data\F4SE\Plugins\
```

Start the game through F4SE. If `PerformanceTuner.ini` is missing, the built-in
defaults are used. The plugin log is written to:

```
Documents\My Games\Fallout4\F4SE\PerformanceTuner.log
```

## Configuration

All distances are world units, times are seconds, momentum values are world units per
frame.

```ini
[Simple]
; Frame rate band. Grow the shadow distance while FPS is above fMaxFPS, pull it in
; while FPS is below fMinFPS, hold in between.
fMaxFPS=70
fMinFPS=64

; Momentum. Decreasing is faster than increasing.
fMomentumDownAcceleration=1.1
fMinMomentum=-64
fMomentumUpAcceleration=1.05
fMaxMomentum=1

; Bounds for the directional shadow distance.
fShadowDirDistanceMin=3000
fShadowDirDistanceMax=8000

; Optionally derive the godray quality from the shadow distance.
bAdjustGRQuality=0
fGRQualityShadowDist1=3000
fGRQualityShadowDist2=6000
fGRQualityShadowDist3=10000

[Advanced]
; Frame history used for the frame rate estimate. Longer is calmer.
fLoadWindowTime=2.0

; Time ignored after a load or menu close.
fResumeGraceTime=1.0

; Diagnostics in the plugin log.
bShowDiagnostics=0
fDebugMessageFrequency=1
```

Tuning notes:

- To track the target closely, bring `fMinFPS` closer to `fMaxFPS` (for example 68/70)
  and increase `fMaxMomentum`/`fMinMomentum`.
- To be calm about hitches from other mods, widen the band and lower the momentum caps.
- The band can be narrow but not zero; a small minimum band is enforced.

## What changed compared to the original mod

- F4SE plugin instead of a `dxgi.dll` proxy, so ReShade/ENB/other injectors can use the
  usual `dxgi.dll` slot.
- No memory offsets and no `fallout4-addresses-<version>.ini`; IDs come from the
  Address Library and the plugin declares address independence to F4SE.
- The frame rate cap and the loading-screen acceleration were removed; dedicated mods
  handle those now.
- The shadow distance is applied through the engine cache instead of a raw global, and
  the godray quality is applied through the game's own `gr quality` command.
- The controller uses a median frame time, a load window, a resume grace period and an
  asymmetric FPS band, which removes the hunting the old version worked around with
   extremely slow growth.

## Building

Requirements: Visual Studio 2022 with "Desktop development with C++", Windows SDK, and
[xmake](https://xmake.io) 3.0 or newer.

```bat
git clone --recurse-submodules <repository>
xmake f -m release -a x64
xmake build
```

The DLL is written to `build\windows\x64\release\PerformanceTuner.dll`.

To install automatically during development, set `XSE_FO4_GAME_PATH` to the game folder
(or `XSE_FO4_MODS_PATH` to a mod-manager mods folder) and run `xmake install`.

## Troubleshooting

- `PerformanceTuner.log` should contain `Target band <min>-<max> FPS ...` after loading.
- If the plugin does not appear in `f4se.log`, check that F4SE and the Address Library
  match the game version.
- The controller only becomes active once the world is loaded; main menu, loading
  screens, pause menu, Pip-Boy and sleep/wait menus are ignored.

## Credits and license

- Original idea and first implementation: [xwise](https://github.com/xwize/)
- Rewritten and maintained as a controller by Daniel "StochasticTinkr" Pitts
- F4SE port and modernization: this repository

The code in this repository is released under the MIT license (see the original
notice in the git history). It links against
[CommonLibF4](https://github.com/Dear-Modding-FO4/commonlibf4), which is licensed
GPL-3.0-or-later with a modding exception; distributed binaries are therefore subject
to those terms, and the source of this plugin is available here.

The old `dxgi.dll` implementation is preserved under `legacy/` for reference.

