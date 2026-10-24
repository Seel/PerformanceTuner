# Legacy DXGI proxy (archived)

This is the original implementation of the Dynamic Performance Tuner: a `dxgi.dll`
proxy that hooked `IDXGISwapChain::Present` and patched hard-coded memory offsets
(`fallout4-addresses-<version>.ini`).

It is kept for reference only. It does not work on current Fallout 4 versions and is
not built by the project.

The F4SE plugin in the repository root replaces it. Two reasons it was retired:

- every game update moved the five offsets, and the mod could only be fixed by
  scanning for them again in Cheat Engine;
- a `dxgi.dll` proxy conflicts with ReShade, ENB and other present-hooking mods.

Build files (`XF4DSF.sln`, `XF4DSF.vcxproj`) target Visual Studio 2015 / v140 and are
left as they were.
