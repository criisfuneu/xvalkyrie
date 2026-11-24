# XCL experimental: faster huge compute shaders, optional graphics PSO worker priority.
# Runs game folder read-only. Ships DXC alongside this launcher.
[CmdletBinding()]
param(
    [ValidateSet('auto','0','1')][string]$CompactCs = 'auto',
    [ValidateSet('0','1')][string]$Preload = '0',
    [string]$GameDir = 'D:\GTA6\GTA6.Xbox.v1.0.2.1-CYBERLEEK'
)
$ErrorActionPreference = 'Stop'
$exe = Join-Path $PSScriptRoot 'xcl.exe'
foreach($name in @('xcl.exe','dxcompiler.dll','dxil.dll')) {
    if(-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot $name) -PathType Leaf)) { throw "Arquivo ausente: $name" }
}
if(-not (Test-Path -LiteralPath $GameDir -PathType Container)) { throw "Pasta do jogo nao encontrada: $GameDir" }
$env:XCL_DXC_DIR = $PSScriptRoot
$env:XCL_COMPACT_CS = $CompactCs
$env:XCL_SLEEP_HOLD = '1'
$env:XCL_PSO_WAIT_MS = '0'
$env:XCL_NETWORK = '0'
$env:XCL_ACTIVITY = '1'
$env:XCL_PSO_PRELOAD = $Preload
Remove-Item Env:XCL_SKIP_BINK -ErrorAction SilentlyContinue
Write-Host ""
Write-Host "XCL COMPACT: $env:XCL_COMPACT_CS" -ForegroundColor Cyan
Write-Host "Preload especulativo: $env:XCL_PSO_PRELOAD (0 = desligado por seguranca de memoria)" -ForegroundColor Yellow
Write-Host "HLSL memory guard #1321: compiled in" -ForegroundColor Green
Write-Host "Watchdog hold #1288: ON" -ForegroundColor Green
Write-Host "Sem pular desenhos ou dispatches: XCL_PSO_WAIT_MS=0" -ForegroundColor Green
Write-Host "Jogo: $GameDir (somente leitura)"
Write-Host "Atenção: cache compacto usa chaves separadas. Primeira compilacao pode demorar." -ForegroundColor Yellow
Write-Host "Interrompa se a memoria comprometida do Windows se aproximar do limite." -ForegroundColor Yellow
& $exe run $GameDir
Write-Host "XCL encerrou com codigo: $LASTEXITCODE"
