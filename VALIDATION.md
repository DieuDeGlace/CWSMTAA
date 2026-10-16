# Validation

2026-10-16 update. Built with MSVC 14.44 x86 C++17 `/O2 /MT /EHsc`.

DLL: `AlphaTAA.dll`

SHA-256: `087fc1d6cda65cdaf5b635349d2e7b96e38a978b27170b038a472fdbcfbdbbe4`

`TemporalGpuTest.exe` passes on a hardware D3D9 device: temporal
reconstruction, optional display paths, native depth/state restoration, HUD
exclusion, twelve device resets and edge stability, plus two checks for the
Steam fixes:

| Check | Installed build (6e6333b6) | This build |
| --- | --- | --- |
| Static slanted edge, mean per-pixel range over 16 settled jittered frames | 14.47 | 1.19 |
| Panning grass/stone edge over sky, worst distance from a real color mix | 1.60 | 2.32 |

Both fringe values are rounding-level; the color clip is what keeps the
longer 90% edge history free of fringes. In-game visuals and frame time are
not yet verified with this build.
