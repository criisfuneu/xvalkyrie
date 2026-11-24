@echo off
setlocal
set "GAME_DIR=D:\GTA6\GTA6.Xbox.v1.0.2.1-CYBERLEEK"
set "GAME_EXE=%GAME_DIR%\game_scarlett_master.exe"
if not exist "%~dp0victory.exe" (
  echo ERRO: victory.exe nao encontrado nesta pasta.
  pause
  exit /b 2
)
if not exist "%GAME_EXE%" (
  echo ERRO: jogo nao encontrado: "%GAME_EXE%"
  pause
  exit /b 2
)
echo VIctory experimental - AMD Radeon RX 9070 XT
echo Sem modificar arquivos do jogo. Driver D3D12 real.
echo Logs e arquivos derivados ficam fora da pasta do jogo.
echo.
"%~dp0victory.exe" run "%GAME_EXE%" --execute
echo.
echo VIctory encerrou. Codigo: %ERRORLEVEL%
pause
