@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -NoExit -File "%~dp0Start-Upstream.ps1" -PsoWaitMs 0
