@echo off
setlocal
set "XCL_PSO_WAIT_MS=0"
set "XCL_DSTORAGE_PARALLEL_DECOMPRESS="
echo [XCL Streaming Seguro] Sem pular draw/dispatch por atraso de pipeline.
echo [XCL Streaming Seguro] Leitura de arquivos acelerada por VFS, com BCPack intacto.
call "%~dp0Pular-introducao.cmd"
endlocal
