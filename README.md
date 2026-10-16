# CWSMTAA

Steam-method temporal anti-aliasing for Cube World Alpha, with SMAA Ultra and display-only softness.

Made by DieuDeGlace with AI assistance from OpenAI ChatGPT/Codex. MIT licensed. No game executable, game assets, saves, or runtime journals are included.

## Build

Install Visual Studio 2022 Build Tools with Desktop development with C++ and
the Windows SDK. Open an **x86 Native Tools Command Prompt for VS 2022**.
These Alpha DLLs are 32-bit; an x64 build will not load into Cube.exe.

```bat
git clone https://github.com/DieuDeGlace/CWSMTAA.git
cd CWSMTAA
build.cmd
```

The DLL appears under `build/Mods/`. No toolchain or game files are bundled.
Builds use C++17, /O2 /MT /EHsc, Unicode, and the Windows SDK. clang-cl in an
x86 MSVC environment may be substituted for cl. Test programs are built into
`build/tests/`; run them with Alpha and other exclusive-fullscreen games closed.
GPU tests require a working Windows D3D9 driver and do not measure in-game FPS.

## Install

Close Alpha, extract the release ZIP into the Alpha game folder, and launch
through the Alpha mod loader. The ZIP's `Mods` folder must merge with the
game's existing `Mods` folder. Keep the included configuration/support folders.
The repository name is the project name; the established DLL filename stays
unchanged for compatibility. Remove the DLL with the game closed to uninstall.

## Rendering and controls

Steam-style eight-sample Halton jitter (scaled by JitterScale), linear
reconstruction and a fused resolve blur. Ported from the Steam TemporalAA
fixes (2026-10-13):

- History is clipped toward the current pixel's color until it enters this
  frame's 3x3 neighborhood, so edges only show mixes of real colors (no
  green/red/yellow/blue fringes from per-channel box corners).
- History is released only when the clip had to move it (real change), not
  because it differs from the jittered current pixel, which made strong
  edges shimmer.
- Edges and thin details keep up to EdgeHistoryPercent (default 90) of
  history; flat areas keep HistoryPercent (default 65).
- Luminance-weighted (Karis) blend of current and history.
- The draw hooks look up the back buffer and device state once per frame
  instead of on every world draw.

The Steam version's depth-based camera reprojection is **not** ported: Alpha's
D3D9 depth buffer is not readable without replacing it, and this mod keeps
native depth. History is read at the same pixel and released on large camera
cuts or zoom changes, as before.

SMAA Ultra is on by default and runs on the resolved image **after storing
temporal history**, before the HUD; optional 3x3 Gaussian display softness
also runs there. There is no depth replacement or motion-vector buffer.

`graphics-rebuild/AlphaTemporal.ini`: HistoryPercent=65 (0-85),
EdgeHistoryPercent=90 (0-95), SMAAEnabled=1, DisplaySoftness=0 (0-1),
ResolveBlur=0.12, JitterScale=0.75, EdgeStability=0.24 (0-0.5). F8 toggles
the temporal setup; F9 toggles SMAA. Keep built-in AA off. AlphaCWShadows can
share this mod's actual scene jitter.

Runtime compilation requires Windows d3dcompiler_47.dll. Native MSAA is not
supported by this prototype. Added display passes can affect performance.
No Omega-equivalence or universal crash-free claim is made.

The GPU test covers temporal reconstruction, individual/hybrid display paths,
unblurred history, native depth/state restoration, sharp HUD, twelve resets,
edge stability, a static slanted edge under the full jitter sequence (mean
per-pixel range over settled frames 14.47 before the port, 1.19 after) and a
panning grass/stone edge over a sky horizon (worst distance from a real
color mix 2.3 of 255). Run `build\tests\TemporalGpuTest.exe`.

## Source formatting

The project uses the checked-in `.clang-format` style (four-space indentation and a 100-column target). From the repository root, run `clang-format -i src/*.cpp src/*.h` on the source files that exist. Keep third-party SMAA files and embedded shader strings unchanged.

The cleanup was rebuilt with MSVC for x86 and checked against the previous revision for identical executable tokens and shader strings. Runtime settings and installed DLLs are unchanged.
