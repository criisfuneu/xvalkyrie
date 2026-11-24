VIctory geometry-preview diagnostic, 24 Nov 2026

Purpose
-------
See if the GTA VI world writes a 2560x1440 RGBA8 (DXGI f28) render target even
when the final presented frame is black. No game files, encryption keys, or
copyrighted assets are included. The title files remain read-only.

Included source patches, pinned
-------------------------------
- VIctory main ffe310eb2098e52169f4c44db5ffdabf43a72622
- PR #322: avoid expensive VirtualQuery in copies to title memory
- Backported PR #464: stop repeatedly trying to read absent guarded pages
- PR #530: diagnostic override of presented image with a source render target
- preview-descriptor variant ONLY: PR #1067, recovering a descriptor-only shader class
- Diagnostics profile, not release profile. More diagnostic validation available.

Safe first test
---------------
1. DO NOT immediately run this on the RX 9070 XT after the previous GPU reset.
2. Unzip to its own folder; never overwrite your working VIctory installation.
3. Double-click Iniciar-VIctory-WARP.cmd.
4. Software D3D12 adapter WARP is enabled, so this could be far slower than your
   Radeon and may not reach the world quickly. Your computer remains responsive
   better without running GPU compute commands on the real adapter.
5. The output may show a false-colour G-buffer: geometry, not final lighting.
6. Missing picture is not proof of no rendering. Compare to diagnostics captures.
7. The diagnostic loop limit (512) is not a gameplay-correctness guarantee.
8. To inspect the scene, keep VICTORY_SHOW_TARGET=2560x1440-f28.

Avoid enabling D3D12 debug + GPU-based validation and full frame dumps at once.
The README documents optional VICTORY_CAPTURE and VICTORY_DUMP_TARGETS flags,
but they can consume a lot of disk and memory.
Any driver hang, visual vibration, or abnormal RGB blink: stop the test,
reboot Windows and inspect system stability before returning.

Limitations
-----------
No actual retail title was executed during CI. Rust builds/unit tests prove
integration, not in-game picture, FPS, or safety. CGBJ is private; this version
does not copy any unknown CGBJ modifications.
