# M2 host 单测构建/运行器（mingw gcc，无 CMake）
# 用法：pwsh -NoProfile -File tests/run_tests.ps1
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

function Invoke-Gcc {
    param([string[]]$gccArgs, [string]$label)
    Write-Host "[CC] $label"
    & $gcc @gccArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[FAIL] compile: $label (exit $LASTEXITCODE)"
        exit $LASTEXITCODE
    }
}

$exeState = Join-Path $build 'test_pet_state.exe'
$exeUi = Join-Path $build 'test_ui240.exe'

Invoke-Gcc -gccArgs ($cflags + @(
    '-I', $inc,
    '-o', $exeState,
    (Join-Path $testsDir 'test_pet_state.c'),
    (Join-Path $pui 'pet_state.c'))) -label 'test_pet_state.c + pet_state.c'

Invoke-Gcc -gccArgs ($cflags + @(
    '-I', $inc,
    '-I', $pui,
    '-o', $exeUi,
    (Join-Path $testsDir 'test_ui240.c'),
    (Join-Path $pui 'pet_ui_240.c'),
    (Join-Path $pui 'pet_render.c'),
    (Join-Path $pui 'pet_text.c'),
    '-lm')) -label 'test_ui240.c + pet_ui_240.c + pet_render.c + pet_text.c'

$failed = 0
# 从仓库根运行：test_ui240 用相对路径读取 assets/robot.look
Push-Location $root
try {
    foreach ($exeName in @('test_pet_state.exe', 'test_ui240.exe')) {
        $exe = Join-Path $build $exeName
        Write-Host "=== RUN $exeName ==="
        & $exe
        $code = $LASTEXITCODE
        Write-Host "=== EXIT $code ($exeName) ==="
        if ($code -ne 0) {
            $failed++
        }
    }
} finally {
    Pop-Location
}

if ($failed -ne 0) {
    Write-Host "RESULT: FAIL ($failed test binary/binaries failed)"
    exit 1
}
Write-Host 'RESULT: PASS (all host tests green)'
exit 0

