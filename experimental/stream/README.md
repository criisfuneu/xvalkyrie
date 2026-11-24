# XCL Streaming Seguro - 24 November 2026

Built independently from `xcl-world-waits-20261124`. No original game files are touched. This is not a full emulator speed fix.

The existing mesh shader support (upstream PR #541), BCPack decoder (#905), optional Bink skip (#1053), optional PSO wait budget (#891) and DirectStorage page queue fix (#549) are **unchanged**.

**New:** two VFS read-path optimizations:
- #1092 removes redundant `metadata` queries when opening a game file for plain read.
- #1106 bypasses pointless overlay file/dir probes until the overlay actually contains files.

Speedup observed upstream: ordinary game-file open decision ~90.5us to ~30us with #1092, and ~95.3us to ~46.5us with #1106. These are per-open microbenchmarks, **NOT overall GTA VI load time**. End-to-end speed must be measured on the user's PC.

**Run:** extract to a new directory. Use `Iniciar-Streaming-Seguro.cmd` to skip only the introduction video, **not** graphics tasks. No shader precompile or game asset changes needed.
For optional world-render investigation, `Testar-mundo-50ms.cmd` is also included, but its 50ms budget may skip dispatches; not recommended for a faithful load.

The bundle includes Microsoft's signed DXC DLLs from DirectXShaderCompiler September 2026 release. The launcher searches the bundle first, then your earlier configured Downloads folder.

No unverified DirectStorage parallel decode is enabled: PR #819 covers certain zlib requests only and remains untested on this retail title. We will not silently enable it for BCPack.
