# M4 host 单测构建/运行器（mingw gcc，无 CMake）
# 用法（仓库根 D:\DSH\ESP32-touch）：
#   pwsh -NoProfile -File tests/run_m4_tests.ps1
# 说明：T 类（语音播报纯逻辑 pet_audio_policy）+ 资产格式校验（C04 §2）。
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

$cflags = @('-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Wno-unused-parameter')
$exe = Join-Path $build 'test_m4_audio.exe'

Write-Host '[CC] test_m4_audio.c + pet_audio_policy.c'
& $gcc @($cflags + @(
    '-I', $inc,
    '-o', $exe,
    (Join-Path $testsDir 'test_m4_audio.c'),
    (Join-Path $pui 'pet_audio_policy.c')))
if ($LASTEXITCODE -ne 0) {
    Write-Host "[FAIL] compile (exit $LASTEXITCODE)"
    exit $LASTEXITCODE
}

Push-Location $root
try {
    Write-Host '=== RUN test_m4_audio.exe ==='
    & $exe
    $code = $LASTEXITCODE
    Write-Host "=== EXIT $code (test_m4_audio.exe) ==="
} finally {
    Pop-Location
}

if ($code -ne 0) {
    Write-Host 'RESULT: FAIL (M4 audio tests failed)'
    exit 1
}
Write-Host 'RESULT: PASS (all M4 audio host tests green)'
exit 0
