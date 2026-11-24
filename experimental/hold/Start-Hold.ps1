# XCL X3: upstream main + mesh PR541 + watchdog hold PR1288.
# Keeps the title's graphics commands intact. Does not modify game files.
[CmdletBinding()]
param(
    [switch]$OriginalWatchdog,
    [switch]$SkipIntro,
    [string]$GameDir = 'D:\GTA6\GTA6.Xbox.v1.0.2.1-CYBERLEEK'
)
$ErrorActionPreference = 'Stop'
$exe = Join-Path $PSScriptRoot 'xcl.exe'
foreach ($file in @($exe, (Join-Path $PSScriptRoot 'dxcompiler.dll'), (Join-Path $PSScriptRoot 'dxil.dll'))) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Arquivo ausente: $file" }
}
if (-not (Test-Path -LiteralPath $GameDir -PathType Container)) {
    throw "Pasta do jogo nao encontrada: $GameDir"
}
$env:XCL_DXC_DIR = $PSScriptRoot
$env:XCL_NETWORK = '0'      # no outbound crash report upload
$env:XCL_PSO_WAIT_MS = '0' # NEVER skip an unfinished draw or dispatch
if ($OriginalWatchdog) {
    $env:XCL_SLEEP_HOLD = '0'
    Write-Host "CONTROLE: relogio original do watchdog, sem compensacao" -ForegroundColor Yellow
} else {
    $env:XCL_SLEEP_HOLD = '1'
    Write-Host "CORRECAO #1288: espera de pipeline nao conta no Sleep do jogo" -ForegroundColor Green
}
if ($SkipIntro) { $env:XCL_SKIP_BINK = '1' } else { Remove-Item Env:XCL_SKIP_BINK -ErrorAction SilentlyContinue }
Write-Host "XCL: $exe"
Write-Host "Jogo: $GameDir (somente leitura)"
Write-Host "DXC: $env:XCL_DXC_DIR"
Write-Host "PSO wait: $env:XCL_PSO_WAIT_MS (espera completa, sem pular)"
& $exe run $GameDir
Write-Host "XCL encerrou com codigo: $LASTEXITCODE"
