# CWSMTAA: Steam stable TAA test

Branch `test/steam-stable-taa` ports Steam TemporalAA's accepted `main` behavior at `2e0d10b` to Alpha D3D9. `main` retains the accepted Alpha SMAA/display-softness build. Made by DieuDeGlace with AI assistance from OpenAI ChatGPT/Codex.

The source Steam chat restored stable color-history TAA and rejected its depth-reprojection/SMAA experiment for cost. This branch therefore uses the accepted eight-sample Halton sequence at 0.75 strength, 65% bounded history, thin-detail retention and mild 0.12 blur fused into the temporal resolve. Existing samples provide the blur; it adds no texture samples or fullscreen pass. The mild filtered resolve is stored as history, matching Steam's method. Optional legacy display filters never feed back into history.

SMAA and the separate display-softness pass are disabled by default, removing four display passes and three full-resolution intermediates compared with Alpha's accepted hybrid setup. Optional legacy resources are allocated only if requested. Native depth remains untouched: there is no depth replay, motion-vector buffer or camera-reprojection experiment. Native world/HUD separation, rejection, Reset handling and shared shadow jitter remain intact.

D3D11 projection-buffer upload reuse cannot be copied literally because Alpha uses per-object combined WVP constants and restores them after each draw. This port keeps the native D3D9 constant path. Sparse GPU timestamps measure copy/resolve cost on supporting devices without flushing or waiting. Actual Alpha visual quality and FPS still require live testing.

## Build and install

Use an **x86 Native Tools Command Prompt** with Visual Studio Build Tools and a Windows SDK, then run `build.cmd`. `build\tests\TemporalGpuTest.exe` exercises default and optional filter paths plus twelve resets. Output is `build/Mods/AlphaTAA.dll`.

Close Alpha, place the DLL in `Mods` and `config/AlphaTemporal.ini` in `graphics-rebuild`; restart through the mod loader. Disable native AA. Pair with AlphaCWShadows's `test/steam-cascades` branch. F8 toggles TAA; F9 toggles optional SMAA and recreates its resources.

## Settings

```
[TAA]
HistoryPercent=65
SMAAEnabled=0
DisplaySoftness=0
ResolveBlur=0.12
JitterScale=0.75
```

`ResolveBlur` and `JitterScale` accept 0-1. The old SMAA/display-softness path remains available for comparison, but the test defaults match stable Steam TAA. The previous accepted DLL/settings and main source are backed up in `graphics-rebuild/backups/accepted-before-steam-cascades` for rollback with Alpha closed.

Code follows the checked-in `.clang-format`. Preserve third-party SMAA files and embedded shader strings. MIT licensed; retained SMAA code/lookups have their own license in `THIRD_PARTY_NOTICES.md`. No game assets, proprietary shaders or saves are included.
