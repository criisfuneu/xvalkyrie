# XVal Launcher experimental
# Community-side front-end for separately installed XCL and VIctory.
# Never downloads, decrypts, patches or distributes game content.
# Windows PowerShell 5.1+, Windows Forms.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

$profileDir = Join-Path $env:LOCALAPPDATA 'XValLauncher'
$configPath = Join-Path $profileDir 'settings.json'
$gameDefault = 'D:\GTA6\GTA6.Xbox.v1.0.2.1-CYBERLEEK'
$xclDefault = 'C:\XCL-Render\xcl.exe'
$victoryDefault = 'C:\VIctory\victory.exe'
$dxcDefault = Join-Path $env:USERPROFILE 'Downloads\XCL-Windows-DXC-Oficial-2026'

$settings = @{
    Game = $gameDefault
    Xcl = $xclDefault
    Victory = $victoryDefault
    Dxc = $dxcDefault
    Engine = 'XCL'
    Capture = $false
}
if (Test-Path -LiteralPath $configPath) {
    try {
        $old = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
        foreach ($key in @('Game','Xcl','Victory','Dxc','Engine','Capture')) {
            if ($null -ne $old.$key) { $settings[$key] = $old.$key }
        }
    } catch {
        [System.Windows.Forms.MessageBox]::Show("Configuracao anterior invalida: $($_.Exception.Message)", 'XVal', 'OK', 'Warning') | Out-Null
    }
}
$bg = [Drawing.Color]::FromArgb(18, 24, 38)
$fg = [Drawing.Color]::FromArgb(232, 238, 249)
$muted = [Drawing.Color]::FromArgb(158, 174, 194)
$inputBg = [Drawing.Color]::FromArgb(35, 45, 64)
$accent = [Drawing.Color]::FromArgb(47, 126, 214)

$form = New-Object System.Windows.Forms.Form
$form.Text = 'XVal Launcher | Xbox-to-PC experimental'
$form.StartPosition = 'CenterScreen'
$form.ClientSize = New-Object Drawing.Size(790, 495)
$form.MinimumSize = New-Object Drawing.Size(790, 525)
$form.BackColor = $bg
$form.ForeColor = $fg
$form.Font = New-Object Drawing.Font('Segoe UI', 9)
$form.MaximizeBox = $false

function New-Label([string]$value, [int]$x, [int]$y, [int]$width, [int]$height = 22) {
    $l = New-Object System.Windows.Forms.Label
    $l.Text = $value
    $l.ForeColor = $muted
    $l.Location = New-Object Drawing.Point($x, $y)
    $l.Size = New-Object Drawing.Size($width, $height)
    $form.Controls.Add($l)
    return $l
}
function New-Entry([string]$value, [int]$y) {
    $t = New-Object System.Windows.Forms.TextBox
    $t.Text = $value
    $t.Location = New-Object Drawing.Point(22, $y)
    $t.Size = New-Object Drawing.Size(660, 26)
    $t.BackColor = $inputBg
    $t.ForeColor = $fg
    $t.BorderStyle = 'FixedSingle'
    $form.Controls.Add($t)
    return $t
}
function New-Button([string]$value, [int]$x, [int]$y, [int]$w = 75, [int]$h = 27) {
    $b = New-Object System.Windows.Forms.Button
    $b.Text = $value
    $b.Location = New-Object Drawing.Point($x, $y)
    $b.Size = New-Object Drawing.Size($w, $h)
    $b.BackColor = $inputBg
    $b.ForeColor = $fg
    $b.FlatStyle = 'Flat'
    $b.FlatAppearance.BorderSize = 0
    $form.Controls.Add($b)
    return $b
}
function Choose-Directory([System.Windows.Forms.TextBox]$field) {
    $dlg = New-Object System.Windows.Forms.FolderBrowserDialog
    $dlg.Description = 'Selecione a pasta'
    if (Test-Path -LiteralPath $field.Text -PathType Container) { $dlg.SelectedPath = $field.Text }
    if ($dlg.ShowDialog() -eq 'OK') { $field.Text = $dlg.SelectedPath }
    $dlg.Dispose()
}
function Choose-Exe([System.Windows.Forms.TextBox]$field) {
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Filter = 'Executaveis (*.exe)|*.exe'
    $dlg.CheckFileExists = $true
    if (Test-Path -LiteralPath $field.Text -PathType Leaf) { $dlg.FileName = $field.Text }
    if ($dlg.ShowDialog() -eq 'OK') { $field.Text = $dlg.FileName }
    $dlg.Dispose()
}

$headline = New-Label 'XVAL  /  LAUNCHER EXPERIMENTAL' 22 15 720 29
$headline.Font = New-Object Drawing.Font('Segoe UI', 15, [Drawing.FontStyle]::Bold)
$headline.ForeColor = $fg
New-Label 'Camada de compatibilidade em desenvolvimento. Sem garantia de gameplay.' 23 48 730 20 | Out-Null

New-Label 'Pasta do jogo (arquivos fornecidos pelo usuario)' 22 84 650 | Out-Null
$game = New-Entry $settings.Game 105
(New-Button 'Procurar' 691 104).Add_Click({ Choose-Directory $game })

New-Label 'Executavel do XCL' 22 143 650 | Out-Null
$xcl = New-Entry $settings.Xcl 164
(New-Button 'Procurar' 691 163).Add_Click({ Choose-Exe $xcl })

New-Label 'Executavel do VIctory' 22 200 650 | Out-Null
$victory = New-Entry $settings.Victory 221
(New-Button 'Procurar' 691 220).Add_Click({ Choose-Exe $victory })

New-Label 'Pasta DXC da Microsoft (para XCL)' 22 257 650 | Out-Null
$dxc = New-Entry $settings.Dxc 278
(New-Button 'Procurar' 691 277).Add_Click({ Choose-Directory $dxc })

New-Label 'Motor' 22 323 65 | Out-Null
$engine = New-Object System.Windows.Forms.ComboBox
$engine.DropDownStyle = 'DropDownList'
[void]$engine.Items.Add('XCL')
[void]$engine.Items.Add('VIctory')
$engine.SelectedItem = if ($settings.Engine -eq 'VIctory') { 'VIctory' } else { 'XCL' }
$engine.Location = New-Object Drawing.Point(90, 318)
$engine.Size = New-Object Drawing.Size(145, 26)
$engine.BackColor = $inputBg
$engine.ForeColor = $fg
$form.Controls.Add($engine)

$capture = New-Object System.Windows.Forms.CheckBox
$capture.Text = 'Capturar quadros para diagnostico (XCL)'
$capture.Checked = [bool]$settings.Capture
$capture.Location = New-Object Drawing.Point(255, 322)
$capture.Size = New-Object Drawing.Size(300, 26)
$capture.ForeColor = $fg
$form.Controls.Add($capture)

$start = New-Button 'INICIAR' 22 375 170 43
$start.BackColor = $accent
$start.Font = New-Object Drawing.Font('Segoe UI', 10, [Drawing.FontStyle]::Bold)
$save = New-Button 'Salvar ajustes' 208 376 145 40
$logs = New-Button 'Abrir logs' 370 376 130 40

$status = New-Label 'Pronto. Instale o XCL ou o VIctory separadamente e selecione o motor.' 22 432 746 49
$status.ForeColor = $muted

$saveHandler = {
    if (-not (Test-Path -LiteralPath $profileDir)) {
        New-Item -Path $profileDir -ItemType Directory -Force | Out-Null
    }
    $config = [ordered]@{
        Game = $game.Text.Trim()
        Xcl = $xcl.Text.Trim()
        Victory = $victory.Text.Trim()
        Dxc = $dxc.Text.Trim()
        Engine = [string]$engine.SelectedItem
        Capture = $capture.Checked
    }
    $config | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $configPath -Encoding UTF8
}
$save.Add_Click({
    try {
        & $saveHandler
        $status.Text = "Configuracoes salvas em $configPath"
    } catch {
        [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Erro', 'OK', 'Error') | Out-Null
    }
})
$logs.Add_Click({
    $path = if ([string]$engine.SelectedItem -eq 'VIctory') {
        Join-Path $env:LOCALAPPDATA 'victory\data'
    } else {
        Join-Path $env:LOCALAPPDATA 'XCL\RockstarGamesInc.28872443D20A7\logs'
    }
    if (Test-Path -LiteralPath $path) {
        Start-Process explorer.exe -ArgumentList ('"' + $path + '"')
    } else {
        [System.Windows.Forms.MessageBox]::Show("Pasta ainda nao existe: $path", 'Logs', 'OK', 'Information') | Out-Null
    }
})
$start.Add_Click({
    try {
        & $saveHandler
        $gameDir = $game.Text.Trim()
        if (-not (Test-Path -LiteralPath $gameDir -PathType Container)) {
            throw "A pasta do jogo nao existe: $gameDir"
        }
        $gameExe = Join-Path $gameDir 'game_scarlett_master.exe'
        if (-not (Test-Path -LiteralPath $gameExe -PathType Leaf)) {
            throw "Nao encontrei game_scarlett_master.exe na pasta selecionada."
        }
        $selected = [string]$engine.SelectedItem
        $binary = if ($selected -eq 'XCL') { $xcl.Text.Trim() } else { $victory.Text.Trim() }
        if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
            throw "O executavel $selected nao existe: $binary"
        }
        if ($selected -eq 'XCL') {
            $dxcb = $dxc.Text.Trim()
            if (-not ((Test-Path -LiteralPath (Join-Path $dxcb 'dxcompiler.dll')) -and
                       (Test-Path -LiteralPath (Join-Path $dxcb 'dxil.dll')))) {
                throw 'DXC nao encontrado: selecione a pasta que contem dxcompiler.dll e dxil.dll.'
            }
        }
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = $binary
        $psi.WorkingDirectory = Split-Path -Path $binary -Parent
        $psi.UseShellExecute = $false
        $psi.CreateNoWindow = $false
        if ($selected -eq 'XCL') {
            $psi.Arguments = 'run "' + $gameDir + '"'
            $psi.EnvironmentVariables['XCL_DXC_DIR'] = $dxc.Text.Trim()
            if ($capture.Checked) { $psi.EnvironmentVariables['XCL_FRAME_DUMP'] = '120' }
            else { [void]$psi.EnvironmentVariables.Remove('XCL_FRAME_DUMP') }
        } else {
            $psi.Arguments = 'run "' + $gameExe + '" --execute'
            $psi.EnvironmentVariables['VICTORY_GAME_DIR'] = $gameDir
        }
        $process = [System.Diagnostics.Process]::Start($psi)
        if ($null -eq $process) { throw 'Falha ao iniciar o processo.' }
        $status.Text = "$selected iniciado. PID: $($process.Id). Confira a janela do jogo e os logs."
    } catch {
        $status.Text = "Falha: $($_.Exception.Message)"
        [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'XVal Launcher', 'OK', 'Error') | Out-Null
    }
})

[void][System.Windows.Forms.Application]::Run($form)
