# VIctory diagnostics runner. No game binaries or assets are modified.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$VictoryExe,
    [Parameter(Mandatory=$true)][string]$GameExe,
    [Parameter(Mandatory=$true)][string]$GameDir
)
$ErrorActionPreference = 'Continue'
$folder = Join-Path $env:LOCALAPPDATA 'XValLauncher\logs'
New-Item -ItemType Directory -Force -Path $folder | Out-Null
$log = Join-Path $folder ("victory-{0}.txt" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
$env:VICTORY_GAME_DIR = $GameDir
$env:VICTORY_LOG = 'info'
Write-Host "Motor VIctory: $VictoryExe" -ForegroundColor Cyan
Write-Host "Jogo: $GameExe"
Write-Host "Logs completos desta sessao: $log" -ForegroundColor Yellow
Write-Host "Observe: draws/frame, dropped, being made. FPS != renderizacao correta." -ForegroundColor Yellow
Write-Host ""
try { Start-Transcript -LiteralPath $log -Force | Out-Null } catch { Write-Warning "Transcript indisponivel: $_" }
try {
    & $VictoryExe run $GameExe --execute
    $exit = $LASTEXITCODE
    Write-Host "VIctory encerrou com codigo: $exit"
} catch {
    Write-Host "Falha na execucao: $($_.Exception.Message)" -ForegroundColor Red
} finally {
    try { Stop-Transcript | Out-Null } catch {}
}
Write-Host "A janela permanece aberta para inspecao do erro e logs."
