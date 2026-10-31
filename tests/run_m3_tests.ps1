# M3 host 单测构建/运行器（mingw gcc，无 CMake；T 类 RED→GREEN）
# 用法（仓库根 D:\DSH\ESP32-touch）：
#   GREEN（真实实现）：pwsh -NoProfile -File tests/run_m3_tests.ps1
#   RED  （桩副本，复现 RED）：pwsh -NoProfile -File tests/run_m3_tests.ps1 -UseStubs
# 说明：与本仓库 M2 的 run_tests.ps1 分开。M2 用例（test_pet_state/test_ui240）
#       仍由 run_tests.ps1 负责；本脚本只跑 M3 的 test_m3_logic / test_m3_ui。
# 退出码：0 = 全部 PASS；非 0 = 有 FAIL 或编译失败。
param(
    [switch]$UseStubs,
    [string]$StubDir = 'D:\DSH\esp-build\m3-stubs'
)
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

# 被测源文件目录：GREEN 用 firmware 真实现；RED 用 Lead 提供的桩副本
# （D:\DSH\esp-build\m3-stubs\，只读，不改动）。pet_render.c / pet_text.c
# 是两套共用的真实渲染/字库实现，始终链接 firmware 版本。
$srcDir = $pui
if ($UseStubs) {
    if (-not (Test-Path $StubDir)) {
        Write-Host "[FAIL] stub dir not found: $StubDir"
        exit 2
    }
    $srcDir = $StubDir
    Write-Host "[MODE] RED: stubs from $StubDir"
} else {
    Write-Host '[MODE] GREEN: firmware/components/pet_ui'
}

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

$exeLogic = Join-Path $build 'test_m3_logic.exe'
$exeUi = Join-Path $build 'test_m3_ui.exe'

# 逻辑组：button / keymap / menu / pomo（不链接 pet_pomo_store.c：NVS 属设备层）
Invoke-Gcc -gccArgs ($cflags + @(
    '-I', $inc,
    '-o', $exeLogic,
    (Join-Path $testsDir 'test_m3_logic.c'),
    (Join-Path $srcDir 'pet_button.c'),
    (Join-Path $srcDir 'pet_keymap.c'),
    (Join-Path $srcDir 'pet_menu.c'),
    (Join-Path $srcDir 'pet_pomo.c'))) -label 'test_m3_logic.c + button/keymap/menu/pomo'

# 像素组：番茄钟页 / 菜单页 + 真实 pet_render.c / pet_text.c（环与 8×8 字库）
Invoke-Gcc -gccArgs ($cflags + @(
    '-I', $inc,
    '-I', $pui,
    '-o', $exeUi,
    (Join-Path $testsDir 'test_m3_ui.c'),
    (Join-Path $srcDir 'pet_ui_pomo_240.c'),
    (Join-Path $srcDir 'pet_ui_menu_240.c'),
    (Join-Path $pui 'pet_render.c'),
    (Join-Path $pui 'pet_text.c'),
    '-lm')) -label 'test_m3_ui.c + ui_pomo_240/ui_menu_240 + pet_render.c + pet_text.c'

$failed = 0
Push-Location $root
try {
    foreach ($exeName in @('test_m3_logic.exe', 'test_m3_ui.exe')) {
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
Write-Host 'RESULT: PASS (all M3 host tests green)'
exit 0
