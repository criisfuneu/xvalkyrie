# VIctory world-rendering experiment (24 Nov 2026)

This is an experimental **VIctory** build, not a combined executable with XCL.
VIctory is GPL-3.0-or-later. XCL is independently built; do not copy project
sources across licenses without compatibility review.

Source candidate: Marvisak/VIctory main at ffe310eb2098e52169f4c44db5ffdabf43a72622 plus PR #294 / #709:
- #294 Root SRV/UAV extent refresh and Root UAV support (GPU memory/data correctness).
- #709 structured buffer access via root SRV/UAV (translation coverage).
- Existing main includes merged PR #336: avoid interpreting bad descriptor tables as arbitrary GPU views.

The source PRs are experimental. **Passing compilation/tests doesn't prove playability**.
The publicly documented world view is incomplete (often dark/black) and can run
below 1 FPS. Some GPU tasks can hang the display driver.

Extract this package into its own folder. Do **not** move anything into the game
folder. Run `Testar-VIctory-RDNA4.cmd` (uses the existing game path
D:\GTA6\GTA6.Xbox.v1.0.2.1-CYBERLEEK). Update that path in the script if needed.
The emulator provides offline networking by design.

If the game starts but draws black, compare with your known-good XCL Mesh Preview,
without overwriting it; they are separate renderers and caches.
