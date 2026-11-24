# VIctory geometry preview with software D3D12 renderer (WARP).
# Diagnostic build only: avoids another RX 9070 XT DEVICE_HUNG while inspecting targets.
[CmdletBinding()]
param(
    [string]$GameDir = 'D:\GTA6\GTA6.Xbox.v1.0.2.1-CYBERLEEK',
    [string]$Target = '2560x1440-f28'
)
$ErrorActionPreference = 'Stop'
$exe = Join-Path $PSScriptRoot 'victory.exe'
$gameExe = Join-Path $GameDir 'game_scarlett_master.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Faltou victory.exe: $exe" }
if (-not (Test-Path -LiteralPath $gameExe -PathType Leaf)) { throw "Faltou jogo: $gameExe" }
$env:VICTORY_GAME_DIR = $GameDir
$env:VICTORY_D3D12_WARP = '1'
$env:VICTORY_SHOW_TARGET = $Target
$env:VICTORY_D3D12_DEBUG = '0'
$env:VICTORY_D3D12_GBV = '0'
$env:VICTORY_D3D12_DRED = '0'
$env:VICTORY_LOG = 'victory_gpu=info,info'
Remove-Item Env:VICTORY_SKIP_PIPELINES -ErrorAction SilentlyContinue
$env:VICTORY_SHADER_LOOP_LIMIT = '512'
Write-Host "================ VIctory WARP DIAGNOSTICO ================" -ForegroundColor Cyan
Write-Host "GPU de software WARP: ATIVADA. Sua RX 9070 XT nao e usada como dispositivo D3D12 primario." -ForegroundColor Green
Write-Host "Mostrando render target intermediario: $Target" -ForegroundColor Green
Write-Host "Limite de loops por lane: 512 (diagnostico conservador)" -ForegroundColor Yellow
Write-Host "Atencao: WARP pode ser extremamente lento; o objetivo e diagnosticar, nao jogar."
Write-Host "Jogo: $gameExe"
Write-Host "==========================================================" -ForegroundColor Cyan
& $exe run $gameExe --execute
Write-Host "VIctory terminou com codigo $LASTEXITCODE"
