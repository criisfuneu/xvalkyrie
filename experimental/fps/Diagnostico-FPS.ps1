$ErrorActionPreference = 'Continue'
$log = Join-Path $env:LOCALAPPDATA 'XCL\RockstarGamesInc.28872443D20A7\logs\xcl.log'
Get-Process -Name xcl -ErrorAction SilentlyContinue |
    Select-Object Id, CPU, @{N='CommittedGB';E={[math]::Round($_.PrivateMemorySize64/1GB,2)}},
      @{N='ResidentGB';E={[math]::Round($_.WorkingSet64/1GB,2)}} |
    Format-Table -AutoSize
if (Test-Path -LiteralPath $log) {
    Write-Host "Log: $log"
    Get-Content -LiteralPath $log -Tail 6000 |
      Select-String 'compact backend|compact form|compute PSO|graphics pipeline|frames presented|console code so far|still building|GPU wait|device removed|ERROR|WARN|Sleep\(1000\)' |
      Select-Object -Last 40
} else { Write-Warning "Log ausente: $log" }
