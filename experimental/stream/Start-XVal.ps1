# XVal experimental build: mesh shaders + optimized uploads + optional intro skip.
# Run from the same folder as xcl.exe. Game files stay untouched.
[CmdletBinding()]
param(
    [switch]$SkipIntro,
    [string]$GameDir = 'D:\GTA6\GTA6.Xbox.v1.0.2.1-CYBERLEEK',
    [string]$DxcDir = "$env:USERPROFILE\Downloads\XCL-Windows-DXC-Oficial-2026"
)
$ErrorActionPreference = 'Stop'
$xcl = Join-Path $PSScriptRoot 'xcl.exe'
if (-not (Test-Path -LiteralPath $xcl -PathType Leaf)) { throw "xcl.exe nao encontrado: $xcl" }
if (-not (Test-Path -LiteralPath $GameDir -PathType Container)) { throw "Pasta do jogo nao encontrada: $GameDir" }
# Prefer the pair bundled next to xcl.exe. This avoids missing user download paths.
# Keep the former external DXC folder as a fallback.
$bundledCompiler = Join-Path $PSScriptRoot 'dxcompiler.dll'
$bundledDxil = Join-Path $PSScriptRoot 'dxil.dll'
if ((Test-Path -LiteralPath $bundledCompiler -PathType Leaf) -and
    (Test-Path -LiteralPath $bundledDxil -PathType Leaf)) {
    $DxcDir = $PSScriptRoot
}
foreach ($dll in @('dxcompiler.dll','dxil.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $DxcDir $dll) -PathType Leaf)) {
        throw "DXC ausente: $dll. Caminho: $DxcDir"
    }
}
$env:XCL_DXC_DIR = $DxcDir
if ($SkipIntro) {
    $env:XCL_SKIP_BINK = '1'
    Write-Host "XVal: PULAR VIDEO DE INTRODUCAO (ativado)" -ForegroundColor Yellow
} else {
    Remove-Item Env:XCL_SKIP_BINK -ErrorAction SilentlyContinue
    Write-Host "XVal: REPRODUZIR VIDEO DE INTRODUCAO (padrao)" -ForegroundColor Green
}
Write-Host "XCL: $xcl"
Write-Host "Jogo: $GameDir"
Write-Host "DXC: $env:XCL_DXC_DIR"
& $xcl run $GameDir
Write-Host "XCL encerrou com codigo: $LASTEXITCODE"
