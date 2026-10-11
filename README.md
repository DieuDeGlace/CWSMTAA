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

Steam-style eight-sample Halton jitter, linear reconstruction, neighborhood
history bounds, color-change rejection, and thin-detail history retention.
SMAA Ultra and 50% 3x3 Gaussian softness run **after storing temporal history**,
before the HUD. There is no depth replacement or motion-vector buffer.

`graphics-rebuild/AlphaTemporal.ini`: HistoryPercent=65 (0–85), SMAAEnabled=1,
DisplaySoftness=0.50 (0–1). F8 toggles the temporal setup; F9 toggles SMAA.
Keep built-in AA off. AlphaCWShadows can share this mod's actual scene jitter.

Runtime compilation requires Windows d3dcompiler_47.dll. Native MSAA is not
supported by this prototype. The user reported improved, stable visuals;
**distant banding/shimmer remains**. Added display passes can affect performance.
No Omega-equivalence or universal crash-free claim is made.

The GPU test covers temporal reconstruction, individual/hybrid display paths,
unblurred history, native depth/state restoration, sharp HUD, and twelve resets.
Run `build\tests\TemporalGpuTest.exe`. Tests passed locally on 2026-10-11.

## Source formatting

The project uses the checked-in `.clang-format` style (four-space indentation and a 100-column target). From the repository root, run `clang-format -i src/*.cpp src/*.h` on the source files that exist. Keep third-party SMAA files and embedded shader strings unchanged.

The cleanup was rebuilt with MSVC for x86 and checked against the previous revision for identical executable tokens and shader strings. Runtime settings and installed DLLs are unchanged.
