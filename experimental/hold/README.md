# XCL Upstream Watchdog-Hold (24 Nov 2026)

Goal: investigate and mitigate the title's crash at `game_scarlett_master.exe+0x23cf683`, linked to the game's own stall clock when a graphics bind blocks for pipeline compilation.

**Based on the current official** `ivall/xcl` main at `930e8b49c1c2ec683d22c724a398b39111cd0e56`, using the same compatibility loader patch and optional mesh-shader PR #541 that worked in the user's prior upstream build.

**New patch**: upstream PR #1288 `95299f47ddc4dc842045f4c1b48cc7910eb7aa68`, changes `kernelx Sleep` to pause the title's sleep-based stall timer for time spent held in an XCL PSO bind. Does NOT modify the game executable; does NOT skip any draw/dispatch; **does NOT fix shader compilation speed or GPU queue stalls**.

Upstream PR #1288 reports 3/3 controlled title runs surviving past 330s with no skipped dispatches, vs. 3/3 triggering the stall check without the patch. On an RTX 5080, not tested on RX 9070 XT and not confirmed world rendered. A separate newer-main report (#1307) warns that commit `b8b1f49` may trigger excessive committed memory on a 32GB PC in a different combined branch; watch memory and stop if consumption grows uncontrollably.

### Running (Windows)
Extract to **a new folder**, not inside game. Use:
- `Iniciar-Correcao-Watchdog.cmd`: tests PR #1288, normal PSO waits (0 skipped), official DXC alongside xcl.exe; disables XCL outbound networking.
- `Comparar-Watchdog-Original.cmd`: same binary, same settings, but disables the watchdog hold using `XCL_SLEEP_HOLD=0`. Allows a controlled A/B test with caches and game files untouched.
- `Pular-Intro-Correcao.cmd`: same as corrected run, but bypasses intro Bink video (not graphical pipeline work).

The game folder remains read-only; XCL stores derived data in `%LOCALAPPDATA%\XCL`. A full world render is **not promised**. If the patched run remains active beyond the previous crash but world stays black, the stall has been mitigated while graphics synchronization/PSOs still need work.

**License:** Upstream XCL license and third-party attribution packaged. Game binaries and asset archives are never included or altered.
