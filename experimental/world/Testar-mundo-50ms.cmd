@echo off
setlocal
set "XCL_PSO_WAIT_MS=50"
echo [XCL] Limite de espera para pipelines: 50 ms. Alguns dispatches podem ser pulados.
call "%~dp0Pular-introducao.cmd"
endlocal
