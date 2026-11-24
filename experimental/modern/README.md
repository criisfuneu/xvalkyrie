# XCL upstream-modern (24 Nov 2026)

Goal: stop building on XCL's November 6 (PR #541) base. **Build the latest pinned upstream main `930e8b49c1c2ec683d22c724a398b39111cd0e56`**, retaining all upstream fixes already merged, then apply our loader boot fixes to support this title's non-relocatable FidelityFX loader.

Separate variants:
- `upstream`: upstream main + fixed-base loader / xmem import compatibility.
- `upstream-mesh`: above plus PR #541 mesh modifications, only if they can be ported onto main and pass all tests.

Upstream includes the official BCPack decoder PR #905, PSO timeout PR #891, texture upload optimizations, DXIL cache improvements, and recent command-list fixes. There is no value in cherry-picking these a second time.

Run `Iniciar-Upstream-Normal.cmd` for faithful PSO waits; `Testar-Upstream-50ms.cmd` is optional troubleshooting that may skip GPU work.

Official Microsoft DXC DLLs are bundled. Unpack to a completely new folder; do not overwrite your known-good Mesh Preview. Nothing writes into game files.

Passing unit tests does NOT prove the game reaches world. Screen output and logs must be compared against baseline on the same title and driver. If upstream-only renders less than the old mesh variant, test the mesh variant instead.
