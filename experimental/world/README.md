# XCL World-Waits experiment (2026-11-24)

Uses the same unmodified game files as XCL Safe Textures. Experimental combination:
- PR #541 mesh
- PR #905 BCPack
- PR #1053 optional Bink intro skip
- PR #891 bounded compute PSO waits (verified upstream to show partial world on Radeon RX 7800 XT)
- PR #549 DirectStorage queue late-page handling (targeted 0x89240009)
- PR #979 DirectStorage GPU/CPU mirror address resolution (targeted 0x89240009)

# Run
Extract to a *new* folder containing xcl.exe and the CMD files. Do not overwrite an existing build.
- `Testar-mundo-50ms.cmd`: enables `XCL_PSO_WAIT_MS=50` and skips the intro. **It may skip compute dispatches and therefore render garbage / leave buffers unwritten.** This is NOT a permanent fix.
- `Controle-sem-pular-pipelines.cmd`: skips the intro but does not bound PSO waits. Helps isolate the effect.

Requires the local game folder configured in `Start-XVal.ps1` and the DXC DLL pair already installed in Downloads.

Game files are never modified. CI unit tests do NOT prove visual correctness or eliminate the title's GPU watchdog.
