# Leonida x XValkyrie shader bridge (experimental)

This branch retains XValkyrie's Windows PE loader, memory, TLS, Xbox system shims
and D3D12 compatibility code. It adds an **optional, source-level shader bridge**:
Leonida pre-translates Xbox RDNA2 shaders to SPIR-V; `spirv-cross` converts
individual SPIR-V modules to HLSL; `dxc` compiles HLSL to D3D12 DXIL; and
XValkyrie looks up exact-title-container matches in a DXIL cache.

No game data, shaders, or binaries from the game are committed here.
No game boot or valid frame has been demonstrated by this bridge. It does
**not** merge the two PE loaders, memory managers or Vulkan/D3D12 backends.

## Building the Windows runtime

Use the existing `Experimental Windows x64 build` GitHub Actions workflow for
this branch. There is no new dependency in the runtime build itself.

## Prepare the cache from Leonida output

You need a local Leonida checkout, its shader stage **already completed**
(`<work>/shaders/dxbc/index.json` and `<work>/shaders/spirv`), a current
`spirv-cross` executable and Microsoft's `dxc` shader compiler. Run:

```powershell
python tools/leonida_dxil_bridge.py --work "D:\\leonida-work" --output "D:\\xv-leonida-dxil" --max 50
```

Remove `--max 50` for all supported shaders. The tool reports missing SPIR-V,
conversion failures and successful DXIL modules in
`D:\xv-leonida-dxil\bridge-report.json`. For absent or failed cache entries,
XValkyrie keeps its existing native shader translator.

## Opt in when starting XValkyrie

```powershell
$env:XV_LEONIDA_DXIL_DIR = "D:\\xv-leonida-dxil"
$env:XV_SCARLETT_PLACEHOLDERS = "1"
$env:XV_SCARLETT_DEVICE_133 = "1"
$env:XV_SCARLETT_DEVICE_70_127 = "1"
$env:XV_SCARLETT_DESCRIPTOR_ALIAS = "1"
& ".\\xvboot.exe" exec "D:\\path\\to\\game_scarlett_master.exe" --allow-missing
```

The log writes `[Runtime] Leonida DXIL bridge: ...` only when an exact
original-container match was found and the DXIL container was accepted.

## Limitations

- Running the converter alone does not make the game playable.
- Shader root signatures, resource bindings, mesh shaders, graphics pipeline
  state, synchronized memory and runtime validation remain unsupported or
  only partially implemented. Conversion may also fail on most shaders.
- The title's original DXBC container is matched by a 64-bit FNV-1a fingerprint
  and shader stage. The experimental cache is opt-in and should only contain
  trusted locally produced files.
- Don't execute game binaries of unknown provenance outside an isolated test
  environment. The compatibility layer does not sandbox them.
- Leonida shader/asset tooling has its own license and is not vendored. Consult
  upstream licensing before redistributing any converted work.

References:
- https://github.com/ItsNotRexed/Leonida
- https://github.com/sammwyy/xvalkyrie
