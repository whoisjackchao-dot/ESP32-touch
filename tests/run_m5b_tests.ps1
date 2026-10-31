# M5b host 单测构建/运行器（mingw gcc，无 CMake；T 类新逻辑 RED→GREEN）。
# 用法（仓库根 D:\DSH\ESP32-touch）：
#   pwsh -NoProfile -File tests/run_m5b_tests.ps1                    # 默认日志 tests/m5b-run.log（覆盖）
#   pwsh -NoProfile -File tests/run_m5b_tests.ps1 -LogPath tests/m5b-red-run.log   # RED 留档
# 覆盖：pet_net_dedupe（C08 §1.1 跨通道去重：16 环 / 1000 ms 窗口 / 32 位回绕 / NULL 防御）
#       + pet_linebuf 行切分复用（C08 §1：≤512 B、CRLF、超长丢弃）。
# 被测源（真实现，只链接不改动）：
#   firmware/components/pet_net/pet_net_dedupe.c
#   firmware/components/pet_ui/pet_linebuf.c
# 退出码：0 = 全部 PASS；非 0 = 有 FAIL 或编译失败。
param(
    [string]$LogPath = ''
)
$ErrorActionPreference = 'Stop'

$testsDir = $PSScriptRoot
$root = Split-Path -Parent $testsDir
if ([string]::IsNullOrEmpty($LogPath)) {
    $LogPath = Join-Path $testsDir 'm5b-run.log'
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

$incNet = Join-Path $root 'firmware\components\pet_net\include'
$netDir = Join-Path $root 'firmware\components\pet_net'
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
$exe = Join-Path $build 'test_m5b_net.exe'
$cflags = @('-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Wno-unused-parameter')

Add-Both @(
    "[MODE] GREEN: firmware/components/pet_net/pet_net_dedupe.c + firmware/components/pet_ui/pet_linebuf.c",
    "[CC] tests/test_m5b_net.c"
)

$compile = & $gcc @cflags -I $incNet -I $incUi -o $exe `
    (Join-Path $testsDir 'test_m5b_net.c') `
    (Join-Path $netDir 'pet_net_dedupe.c') `
    (Join-Path $uiDir 'pet_linebuf.c') 2>&1
$compileCode = $LASTEXITCODE
foreach ($t in $compile) {
    Add-Both @([string]$t)
}
if ($compileCode -ne 0) {
    Add-Both @("[FAIL] compile: exit $compileCode")
    $exitCode = $compileCode
} else {
    Add-Both @('[RUN] test_m5b_net.exe')
    $run = & $exe 2>&1
    $runCode = $LASTEXITCODE
    foreach ($t in $run) {
        Add-Both @([string]$t)
    }
    Add-Both @("[EXIT] $runCode (test_m5b_net.exe)")
    if ($runCode -ne 0) {
        $exitCode = $runCode
    }
}

if ($exitCode -eq 0) {
    Add-Both @('RESULT: PASS (all M5b host tests green)')
} else {
    Add-Both @("RESULT: FAIL (exit $exitCode)")
}

Set-Content -LiteralPath $LogPath -Value $lines -Encoding utf8
Write-Host "[LOG] $LogPath"
exit $exitCode
