# XCL Experimental: Compact Compute + Graphics PSO Priority (2026-11-24)

This version is an **experimental build**, not verified playable. It does not modify the Xbox game, and it does not bypass DRM or provide game data. Compatible, already owned unpacked files are needed.

## What changed
- Pinned official ivall/xcl main at `930e8b49c1c2ec683d22c724a398b39111cd0e56`; keeps the known title loader fix.
- PR #541: console mesh shaders in the AS slot are treated as actual mesh shaders.
- PR #1288: do not count time spent waiting for an XCL pipeline's compilation in the title's Sleep-based watchdog. **Does not skip graphics commands.**
- PRs #1315 + #1316: use a compact HLSL/DXIL form for a few large repetitive compute shaders, enabled in `auto` mode, with separate cache keys and fallback to the old translator on failure.
- Optional matrix variant `compact-priority`: PR #628 prioritizes graphics jobs over speculative compute jobs in the same async worker pool, FIFO preserved within lanes.

## Controls
- `Iniciar-FPS-Compacto.cmd`: recommended, `XCL_COMPACT_CS=auto`; `XCL_PSO_WAIT_MS=0`; `XCL_SLEEP_HOLD=1`.
- `Comparar-FPS-Sem-Compacto.cmd`: same binary with `XCL_COMPACT_CS=0`, for isolating translator effects. The cache uses separate keys.
- `Diagnostico-FPS.ps1`: reads existing XCL log and Windows process memory/CPU information only, no changes to game.

## Expectations and limits
The PR #1315 benchmark on a GeForce RTX 5070 Ti reduced ONE shader's DXIL from 29.75 MB to 1.05 MB, DXC time from 138.6s to 12.5s, and successful driver build from >600s to 9.7s. This does not establish improved in-game FPS, and no Ryzen/Radeon runtime validation of the new combination has been performed here. Some giant compute programs cannot use the compact form and fall back normally.
PR #628 prioritization may improve time-to-first-render when graphics PSOs are queued behind compute PSOs, but it is NOT a general FPS booster.
Do not run both XCL versions at once. Do not delete existing caches. Watch Windows "Committed" system memory; exit if it approaches system limit. The Bink intro is intentionally not skipped.
