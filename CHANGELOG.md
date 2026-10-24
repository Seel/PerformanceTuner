# Changelog

## 1.0.0

First F4SE release, replacing the old `dxgi.dll` proxy.

- Dynamic directional shadow distance driven by an FPS band (`fMaxFPS` / `fMinFPS`).
- Robust controller: time-windowed median frame time, asymmetric tolerance, post-load
  grace period and momentum, so it holds instead of hunting.
- Optional godray quality and godray grid adjustment (off by default).
- No DXGI hijack, no ReShade/ENB conflict, no per-version offsets; IDs come from the
  Address Library.
- Supports Fallout 4 AE 1.11.x (tested on 1.11.240) and the original 1.10.163 runtime
  including GOG (tested). The NG 1.10.980/984 runtime uses the same code paths but has
  not been tested.
- Logging is off by default; enable it with `bEnableLogging=1` in the INI.
