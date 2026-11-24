# XVal: experimental mesh-shader preview, optional Bink video skip
# No game files or DLLs are changed. Only this PowerShell process's environment is set.
param(
  [string]$GameDir = 'D:\GTA6\GTA6.Xbox.v1.0.2.1-CYBERLEEK',
  [string]$DxcDir = "$env:USERPROFILE\Downloads\XCL-Windows-DXC-Oficial-2026",
  [switch]$PlayVideo
)
$ErrorActionPreference = 'Stop'
$exe = Join-Path $PSScriptRoot 'xcl.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "xcl.exe nao encontrado ao lado do script: $PSScriptRoot" }
if (-not (Test-Path -LiteralPath $GameDir -PathType Container)) { throw "Pasta do jogo nao existe: $GameDir" }
foreach ($file in @('dxcompiler.dll', 'dxil.dll')) {
  if (-not (Test-Path -LiteralPath (Join-Path $DxcDir $file))) {
    throw "Arquivo DXC nao encontrado: $file na pasta $DxcDir"
  }
}
$env:XCL_DXC_DIR = $DxcDir
if ($PlayVideo) {
  Remove-Item Env:XCL_SKIP_BINK -ErrorAction SilentlyContinue
  Write-Host 'O video de introducao sera reproduzido normalmente.' -ForegroundColor Yellow
} else {
  $env:XCL_SKIP_BINK = '1'
  Write-Host 'Modo fastboot: video Bink de introducao ignorado (experimental).' -ForegroundColor Cyan
}
Write-Host "Executavel: $exe"
Write-Host "DXC: $env:XCL_DXC_DIR"
Write-Host "Pasta jogo: $GameDir"
& $exe run $GameDir
Write-Host "XCL encerrou. ExitCode: $LASTEXITCODE"
