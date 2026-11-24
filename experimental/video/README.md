# XVal | Mesh + Video Performance + Optional Intro Skip

Compilação experimental do [XCL](https://github.com/ivall/xcl). **Não promete jogabilidade**.

Incorpora:
- XCL PR #541: tratamento dos shaders mesh encontrados no slot AS e pipelines auxiliares;
- XCL PR #455: uploads de texturas na fila DIRECT do jogo sem bloqueios desnecessários na CPU;
- XCL PR #1053: controle opt-in de introdução Bink, padrão DESLIGADO;
- nossas correções anteriores de carregamento para xmem.dll e FidelityFX sem relocação.

## Uso no Windows

1. Extraia tudo para `C:\XCL-Video\`. Não misture com outras builds.
2. Confira se existem `dxcompiler.dll` e `dxil.dll` em `%USERPROFILE%\Downloads\XCL-Windows-DXC-Oficial-2026\`.
3. Execute **Iniciar-com-video.cmd** para assistir à introdução e comparar FPS.
4. Execute **Pular-introducao.cmd** para testar o carregamento posterior (opcional).
5. Alternativamente: `powershell -NoProfile -ExecutionPolicy Bypass -File .\Start-XVal.ps1 [-SkipIntro] -GameDir "D:\SEU_JOGO" -DxcDir "C:\SEU_DXC"`.

Arquivos do jogo **não são incluídos nem modificados**. Microsoft DXC DLLs precisam ser obtidas separadamente pelos canais oficiais.
O otimização das texturas pode melhorar, mas não garante, os FPS e o áudio; há limitações de compatibilidade não resolvidas.
Você pode comparar com a versão anterior mantendo ambas em pastas separadas.

Fonte e licenças originais: GPL-2.0-only do XCL. Consulte os commits e arquivos de provenance no pacote.
