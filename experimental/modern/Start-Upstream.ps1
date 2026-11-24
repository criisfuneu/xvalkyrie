# XCL upstream 2026-11-24. Derived files outside the game directory.
[CmdletBinding()]
param([int]$PsoWaitMs = 0, [string]$GameDir = 'D:\GTA6\GTA6.Xbox.v1.0.2.1-CYBERLEEK')
$ErrorActionPreference = 'Stop'
$exe = Join-Path $PSScriptRoot 'xcl.exe'
foreach($file in @($exe,(Join-Path $PSScriptRoot 'dxcompiler.dll'),(Join-Path $PSScriptRoot 'dxil.dll'))) {
  if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Arquivo ausente: $file" }
}
if (-not (Test-Path -LiteralPath $GameDir -PathType Container)) { throw "Jogo nao encontrado: $GameDir" }
$env:XCL_DXC_DIR = $PSScriptRoot
$env:XCL_PSO_WAIT_MS = [string]$PsoWaitMs
Remove-Item Env:XCL_SKIP_BINK -ErrorAction SilentlyContinue
Write-Host "XCL upstream atualizado: $exe"
Write-Host "GTA VI (somente leitura): $GameDir"
if ($PsoWaitMs -gt 0) {
 Write-Host "Modo diagnostico: limite de PSO $PsoWaitMs ms. Podem ser puladas operacoes graficas." -ForegroundColor Yellow
} else {
 Write-Host "Modo fiel: pipelines aguardados (sem pular tarefas graficas)." -ForegroundColor Green
}
& $exe run $GameDir
Write-Host "Saida do XCL: $LASTEXITCODE"
