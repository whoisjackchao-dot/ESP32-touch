# M5b-2 host 单测构建/运行器（mingw gcc，无 CMake；BLE-01 行重装 RED→GREEN）。
# 用法（仓库根 D:\DSH\ESP32-touch）：
#   pwsh -NoProfile -File tests/run_m5b_ble_tests.ps1                    # 默认日志 tests/m5b2-run.log（覆盖）
#   pwsh -NoProfile -File tests/run_m5b_ble_tests.ps1 -LogPath tests/m5b2-red-run.log   # RED 留档
# 覆盖：pet_ble_lines（C09 §1.1 行重装：单 write 多行 / 跨 write 半行 / CRLF / 超长丢弃 /
#       恰好 512 B / MTU 分片 / 空行 / 序号与计数 / 防御参数 / 重连重装 / 连接隔离）
#       + pet_linebuf 行切分复用（C05/C09 语义）。
# 被测源（真实现，只链接不改动）：
#   firmware/components/pet_ble/pet_ble_lines.c
#   firmware/components/pet_ui/pet_linebuf.c
# 退出码：0 = 全部 PASS；非 0 = 有 FAIL 或编译失败。
param(
    [string]$LogPath = ''
)
$ErrorActionPreference = 'Stop'

$testsDir = $PSScriptRoot
$root = Split-Path -Parent $testsDir
if ([string]::IsNullOrEmpty($LogPath)) {
    $LogPath = Join-Path $testsDir 'm5b2-run.log'
} elseif (-not [System.IO.Path]::IsPathRooted($LogPath)) {
    $LogPath = Join-Path $root $LogPath
}

$gcc = 'C:\ProgramData\mingw64\mingw64\bin\gcc.exe'
if (-not (Test-Path $gcc)) {
    $cmd = Get-Command gcc -ErrorAction SilentlyContinue
    if ($null -eq $cmd) {
        Write-Host '[FAIL] gcc not found'
        exit 2
    }
    $gcc = $cmd.Source
}

$incBle = Join-Path $root 'firmware\components\pet_ble\include'
$bleDir = Join-Path $root 'firmware\components\pet_ble'
$incUi = Join-Path $root 'firmware\components\pet_ui\include'
$uiDir = Join-Path $root 'firmware\components\pet_ui'
$build = Join-Path $testsDir 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null

$lines = New-Object 'System.Collections.Generic.List[string]'
function Add-Both {
    param([string[]]$Text)
    foreach ($t in $Text) {
        Write-Host $t
        $lines.Add($t)
    }
}

$exitCode = 0
$exe = Join-Path $build 'test_m5b_ble.exe'
$cflags = @('-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Wno-unused-parameter')

Add-Both @(
    "[MODE] GREEN: firmware/components/pet_ble/pet_ble_lines.c + firmware/components/pet_ui/pet_linebuf.c",
    "[CC] tests/test_m5b_ble.c"
)

$compile = & $gcc @cflags -I $incBle -I $incUi -o $exe `
    (Join-Path $testsDir 'test_m5b_ble.c') `
    (Join-Path $bleDir 'pet_ble_lines.c') `
    (Join-Path $uiDir 'pet_linebuf.c') 2>&1
$compileCode = $LASTEXITCODE
foreach ($t in $compile) {
    Add-Both @([string]$t)
}
if ($compileCode -ne 0) {
    Add-Both @("[FAIL] compile: exit $compileCode")
    $exitCode = $compileCode
} else {
    Add-Both @('[RUN] test_m5b_ble.exe')
    $run = & $exe 2>&1
    $runCode = $LASTEXITCODE
    foreach ($t in $run) {
        Add-Both @([string]$t)
    }
    Add-Both @("[EXIT] $runCode (test_m5b_ble.exe)")
    if ($runCode -ne 0) {
        $exitCode = $runCode
    }
}

if ($exitCode -eq 0) {
    Add-Both @('RESULT: PASS (all M5b-2 BLE host tests green)')
} else {
    Add-Both @("RESULT: FAIL (exit $exitCode)")
}

Set-Content -LiteralPath $LogPath -Value $lines -Encoding utf8
Write-Host "[LOG] $LogPath"
exit $exitCode
