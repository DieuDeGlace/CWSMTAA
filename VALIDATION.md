# Validation

2026-10-11: MSVC x86 /O2 build and hidden HAL D3D9 checks pass. Synthetic high-contrast subpixel variation is reduced from 89 to 70 for a thin edge and 95 to 89 for a silhouette; flat black/white pixels are unchanged. Default and optional display-filter paths retain native depth/state and HUD exclusion across twelve resets.

The previous test build was reported visually improved, stable and fast, but models still jittered. This refinement requires live model-jitter and FPS verification. Cloud speed remains unchanged until that check.
