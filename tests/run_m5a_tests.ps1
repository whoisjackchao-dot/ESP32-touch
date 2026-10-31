# M5a host 单测构建/运行器（mingw gcc，无 CMake；C 类契约测试，非 TDD 新逻辑）
# 用法（仓库根 D:\DSH\ESP32-touch）：
#   pwsh -NoProfile -File tests/run_m5a_tests.ps1
# 说明：本脚本只跑 M5a 的 test_m5a_event（C05 pet_linebuf + pet_link 契约测试），
#       与 M2 的 run_tests.ps1、M3 的 run_m3_tests.ps1 分开，互不覆盖；
#       被测源文件为 firmware 真实现（无 stub），不链接 pet_state.c（pet_link 不依赖其实现）。
# 退出码：0 = 全部 PASS；非 0 = 有 FAIL 或编译失败。
$ErrorActionPreference = 'Stop'

$testsDir = $PSScriptRoot
$root = Split-Path -Parent $testsDir
$gcc = 'C:\ProgramData\mingw64\mingw64\bin\gcc.exe'
if (-not (Test-Path $gcc)) {
    $cmd = Get-Command gcc -ErrorAction SilentlyContinue
    if ($null -eq $cmd) {
        Write-Host '[FAIL] gcc not found'
        exit 2
    }
    $gcc = $cmd.Source
}

$inc = Join-Path $root 'firmware\components\pet_ui\include'
$pui = Join-Path $root 'firmware\components\pet_ui'
$build = Join-Path $testsDir 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null

Write-Host '[MODE] GREEN: firmware/components/pet_ui (pet_linebuf.c + pet_link.c)'
$cflags = @('-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Wno-unused-parameter')

function Invoke-Gcc {
    param([string[]]$gccArgs, [string]$label)
    Write-Host "[CC] $label"
    & $gcc @gccArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[FAIL] compile: $label (exit $LASTEXITCODE)"
        exit $LASTEXITCODE
    }
}

$exeEvent = Join-Path $build 'test_m5a_event.exe'

Invoke-Gcc -gccArgs ($cflags + @(
    '-I', $inc,
    '-o', $exeEvent,
    (Join-Path $testsDir 'test_m5a_event.c'),
    (Join-Path $pui 'pet_linebuf.c'),
    (Join-Path $pui 'pet_link.c'))) -label 'test_m5a_event.c + pet_linebuf.c + pet_link.c'

$failed = 0
Push-Location $root
try {
    Write-Host '=== RUN test_m5a_event.exe ==='
    & $exeEvent
    $code = $LASTEXITCODE
    Write-Host "=== EXIT $code (test_m5a_event.exe) ==="
    if ($code -ne 0) {
        $failed++
    }
} finally {
    Pop-Location
}

if ($failed -ne 0) {
    Write-Host "RESULT: FAIL ($failed test binary/binaries failed)"
    exit 1
}
Write-Host 'RESULT: PASS (all M5a host tests green)'
exit 0
