Dynamic Performance Tuner

## Update 2026-10-24 — ported to an F4SE plugin

The mod no longer uses a `dxgi.dll` proxy. It is now an F4SE plugin
(`PerformanceTuner.dll`) built on CommonLibF4, and the five addresses below are
obsolete: nothing is patched by offset any more.

- Shadow distance is driven through the engine cache
  `BSShaderManager::CachedDirectionalShadowDistance`. Writing the INI setting, or the
  `Setting` object behind it, does not affect rendering at runtime — the cache does.
- Loading / main-menu / paused states come from `RE::UI` menu checks instead of globals.
- Godray quality is applied with the game's own `gr quality N` command.
- The old proxy, `dynaperf.ini` and `fallout4-addresses-1.10.163.0.ini` are archived in
  `backup/legacy-dxgi/`; ReShade is back in the `dxgi.dll` slot.
- Configuration: see `README.md`. Technical findings and update instructions:
  `AGENTS.md`.

The notes below are kept as the original motivation and history.

---

Original MOD: https://github.com/StochasticTinkr/PerformanceTuner

FO4 Install: C:\SteamLibrary\steamapps\common\Fallout 4

Cheat Engine for getting Adresses: C:\Program Files\Cheat Engine
But maybe there's a better idea to get this, because updates to FO4 will be a pain otherwise.
A new mod that may help:
Runtime DB mod: https://github.com/Zzyxz/CommonLibF4RD
Example Plugin using Runtime DB: https://github.com/Zzyxz/CommonLibF4RD-ExamplePlugin

Adresses for 1.10.163, no longer valid.
[Addresses]
fShadowDirDistance=67333DC
iVolumetricQuality=38C9F08
bGameUpdatePaused=5A66FD0
bIsMainMenu=5A66FB8
bIsLoading=5ABBDFC


To rework:
The Framerate cap feature is handled by other FO4 mods now and can be removed, just FPS target should remain.
Load Acceleration can also be removed as it's also handled by other mods.

The mod works by placing a dxgi.dll that hooks into FO4, but maybe with F4SE which everyone running mods is using nowadays, it can be done that way instead.
