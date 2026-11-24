# Prepare-XCL-Cache.ps1
# Offline XCL shader/DXIL cache warmup. Does not modify the game files.
# Supply a shader corpus directory or a RPF9 shader pack (NOT the whole game root).
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$ShaderSource,
  [string]$XclExe = 'C:\XCL-Novo\xcl.exe',
  [string]$DxcDir = "$env:USERPROFILE\Downloads\XCL-Windows-DXC-Oficial-2026",
  [string]$CacheDir = "$env:LOCALAPPDATA\xcl\dxil",
  [ValidateSet('Compute','All')][string]$Mode = 'Compute',
  [ValidateRange(0,2147483647)][int]$Limit = 5000,
  [switch]$BuildGpuPipelines,
  [switch]$AllowLargeRpf
)
$ErrorActionPreference = 'Stop'
$XclExe = [System.IO.Path]::GetFullPath($XclExe)
$ShaderSource = [System.IO.Path]::GetFullPath($ShaderSource)
if (-not (Test-Path -LiteralPath $XclExe -PathType Leaf)) {
  throw "XCL nao encontrado: $XclExe. Aponte -XclExe para a build que deseja testar."
}
if (-not (Test-Path -LiteralPath $ShaderSource)) {
  throw "Corpus de shaders nao encontrado: $ShaderSource"
}
if (Test-Path -LiteralPath $ShaderSource -PathType Container) {
  if (Test-Path -LiteralPath (Join-Path $ShaderSource 'game_scarlett_master.exe') -PathType Leaf) {
    throw 'Nao selecione a pasta inteira do jogo (~209 GB). Use a pasta de dumps de shaders ou um arquivo shader .rpf.'
  }
  $count = @(Get-ChildItem -LiteralPath $ShaderSource -File -Recurse -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -notlike '*.txt' -and $_.Name -notlike '*.fssm*' } |
    Select-Object -First 2).Count
  if ($count -eq 0) { throw 'Pasta sem arquivos de shaders reconheciveis.' }
} else {
  $file = Get-Item -LiteralPath $ShaderSource
  if ($file.Length -gt 3GB -and -not $AllowLargeRpf) {
    throw 'Arquivo maior que 3 GB: XCL carrega packs RPF9 inteiros na RAM. Use -AllowLargeRpf somente se tiver memoria disponivel.'
  }
  $ext = $file.Extension.ToLowerInvariant()
  if ($ext -notin @('.rpf','.bin','.dxbc','.dxil','.shader')) {
    Write-Warning "Extensao incomum '$ext'; confira se o arquivo contem shaders e nao assets do jogo."
  }
}
foreach ($dll in @('dxcompiler.dll','dxil.dll')) {
  if (-not (Test-Path -LiteralPath (Join-Path $DxcDir $dll) -PathType Leaf)) {
    throw "DXC nao encontrado: $dll em $DxcDir"
  }
}
New-Item -ItemType Directory -Force -Path $CacheDir | Out-Null
$env:XCL_DXC_DIR = $DxcDir
$env:XCL_DXIL_CACHE = $CacheDir
# Four concurrent DXC compilations is a conservative 32-GB-RAM default.
$env:XCL_DXC_THREADS = '4'
$env:XCL_PIPELINE_THREADS = '2'
Remove-Item Env:XCL_DUMP_SHADERS -ErrorAction SilentlyContinue
$argv = @('shaders', $ShaderSource, '--precompile')
if ($Mode -eq 'Compute') { $argv += @('--only','cs') }
if ($Limit -gt 0) { $argv += @('--limit',"$Limit") }
if ($BuildGpuPipelines) { $argv += '--pipelines' }
$logFolder = Join-Path $env:LOCALAPPDATA 'XValLauncher\logs'
New-Item -ItemType Directory -Path $logFolder -Force | Out-Null
$logPath = Join-Path $logFolder ("shader-precompile-{0}.txt" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
Write-Host "XCL: $XclExe" -ForegroundColor Cyan
Write-Host "Shader source: $ShaderSource"
Write-Host "Cache (reaproveitado entre builds): $CacheDir" -ForegroundColor Cyan
Write-Host "Modo: $Mode; Limite: $(if ($Limit -eq 0) { 'todos' } else { $Limit }); pipelines GPU: $BuildGpuPipelines"
Write-Host "Log: $logPath"
Write-Host 'Nao execute o jogo ao mesmo tempo: a precompilacao consome RAM e GPU.' -ForegroundColor Yellow
try { Start-Transcript -LiteralPath $logPath -Force | Out-Null } catch { Write-Warning "Nao foi possivel iniciar log: $_" }
try {
  & $XclExe @argv
  $exitCode = $LASTEXITCODE
  Write-Host "XCL encerrou precompilacao com codigo $exitCode"
  if ($exitCode -ne 0) { throw "Falha na precompilacao: codigo $exitCode" }
} finally {
  try { Stop-Transcript | Out-Null } catch {}
}
