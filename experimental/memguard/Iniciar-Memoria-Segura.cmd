@echo off
set "XCL_PSO_PRELOAD=0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -NoExit -File "%~dp0Start-FPS.ps1" -CompactCs auto -Preload 0
