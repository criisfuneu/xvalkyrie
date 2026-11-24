# Preparação antecipada de shaders do XCL

Esse script **não altera arquivos do jogo**. Usa o comando oficial `xcl shaders ... --precompile` e salva no cache de DXIL já usado pelo XCL.

## Pré-requisito

Informe um pacote de shaders RPF9 (ex.: `D:\...\shaders.rpf`) ou uma pasta que **contenha somente blobs de shaders extraídos/capturados**. Não passe o diretório inteiro do jogo, pois o scanner lê todos os arquivos selecionados e o pacote do jogo é enorme.

Por padrão: `C:\XCL-Novo\xcl.exe`, DXC em Downloads e cache `%LOCALAPPDATA%\xcl\dxil`.

## PowerShell

```powershell
# Computação, primeiro lote (mais seguro para 32 GB RAM)
.\Prepare-XCL-Cache.ps1 -ShaderSource "D:\MEUS_SHADERS\shaders.rpf"

# Computação + pipelines da GPU (custo maior; fazer com o jogo fechado)
.\Prepare-XCL-Cache.ps1 -ShaderSource "D:\MEUS_SHADERS\shaders.rpf" -BuildGpuPipelines

# Todos os shaders, sem limite
.\Prepare-XCL-Cache.ps1 -ShaderSource "D:\MEUS_SHADERS\shaders.rpf" -Mode All -Limit 0
```

Para apontar outra versão, use `-XclExe 'C:\OutraPasta\xcl.exe'`. O script escreve apenas em `%LOCALAPPDATA%\xcl\dxil` e `%LOCALAPPDATA%\XValLauncher\logs`.

## Observações

- Os blobs devem existir fora do jogo; o script não consegue extrair shaders automaticamente de todos os arquivos de 209 GB.
- O cache em disco é reaproveitado quando o código HLSL e as opções combinam; diferentes builds podem produzir shaders diferentes e recompilar parte deles.
- Traduções `translated` podem depender do hash do executável XCL.
- Pipelines compilados no driver dependem também de GPU e versão do driver.
- Mesmo com cache quente, XCL continua identificando shaders, descompactando texturas e sincronizando a GPU. O comando não resolve bugs de DirectStorage ou watchdog.
- `XCL_DXC_THREADS=4` e `XCL_PIPELINE_THREADS=2` são limites conservadores; são ajustáveis.
