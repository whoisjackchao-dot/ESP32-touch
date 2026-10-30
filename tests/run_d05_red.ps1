# D-05 RED 复现：用"未修正的 MSB 点阵表"编译同一 test_ui240.c，确认
# UI-11b-d05-marker-orientation 的镜像断言会失败（而 UI-11-markers 仍通过）。
#
# 不修改 firmware/（该目录是 Lead 写作用域）：从**当前** pet_ui_240.c 复制一份到
# tests/build/d05-red/pet_ui_240_msb.c，仅把镜像修正过的两张表还原成上游 MSB 版本，
# 再用同一 test_ui240.c 链接该副本运行。
#
# 退出码：0 = 成功复现 RED（UI-11b 失败、UI-11-markers 通过）；1 = 未能复现。
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
$build = Join-Path $testsDir 'build\d05-red'
New-Item -ItemType Directory -Force -Path $build | Out-Null

$srcPath = Join-Path $pui 'pet_ui_240.c'
$src = Get-Content -Raw -Encoding UTF8 $srcPath

# 当前（Lead 已镜像修正）→ 下游 MSB 原表（即 D-05 缺陷版本）
$gtFixed = 'GLYPH_GT\[7\]\s*=\s*\{0x01, 0x02, 0x04, 0x08, 0x04, 0x02, 0x01\}'
$gtMsb = 'GLYPH_GT[7]    = {0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10}'
$questFixed = 'GLYPH_QUEST\[7\]\s*=\s*\{0x0E, 0x11, 0x10, 0x08, 0x04, 0x00, 0x04\}'
$questMsb = 'GLYPH_QUEST[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}'

foreach ($pair in @(@($gtFixed, 'GLYPH_GT'), @($questFixed, 'GLYPH_QUEST'))) {
    $n = ([regex]::Matches($src, $pair[0])).Count
    if ($n -ne 1) {
        Write-Host "[FAIL] $($pair[1]) 镜像表模式命中 $n 次（期望 1）；pet_ui_240.c 可能已变更"
        exit 2
    }
}

$red = [regex]::Replace($src, $gtFixed, $gtMsb)
$red = [regex]::Replace($red, $questFixed, $questMsb)
$redPath = Join-Path $build 'pet_ui_240_msb.c'
Set-Content -Path $redPath -Value $red -Encoding utf8

if (-not ($red -match 'GLYPH_GT\[7\]\s*=\s*\{0x10') -or
    -not ($red -match 'GLYPH_QUEST\[7\]\s*=\s*\{0x0E, 0x11, 0x01')) {
    Write-Host '[FAIL] 临时副本未成功还原 MSB 表'
    exit 2
}

$exe = Join-Path $build 'test_ui240_msb.exe'
$cflags = @('-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Wno-unused-parameter')
Write-Host "[CC] D-05 RED: test_ui240.c + (MSB 表副本) pet_ui_240_msb.c + pet_render.c + pet_text.c"
& $gcc @cflags '-I' $inc '-I' $pui '-o' $exe `
    (Join-Path $testsDir 'test_ui240.c') $redPath `
    (Join-Path $pui 'pet_render.c') (Join-Path $pui 'pet_text.c') '-lm'
if ($LASTEXITCODE -ne 0) {
    Write-Host "[FAIL] compile (exit $LASTEXITCODE)"
    exit 2
}

Push-Location $root
try {
    $runOut = & $exe 2>&1
    $code = $LASTEXITCODE
} finally {
    Pop-Location
}
$runOut | ForEach-Object { Write-Output $_ }

$ui11bFail = @($runOut | Select-String -SimpleMatch 'FAIL UI-11b-d05-marker-orientation').Count -gt 0
$ui11Pass = @($runOut | Select-String -SimpleMatch 'PASS UI-11-markers').Count -gt 0
if ($code -eq 0) {
    Write-Host 'RESULT: [FAIL] 未产生 RED —— MSB 表下 UI-11b 仍全通过'
    exit 1
}
if (-not $ui11bFail) {
    Write-Host 'RESULT: [FAIL] 有失败但 UI-11b 未失败，RED 归因不明'
    exit 1
}
if (-not $ui11Pass) {
    Write-Host 'RESULT: [FAIL] UI-11-markers 应通过（方向断言只应影响 UI-11b）'
    exit 1
}
Write-Host "RESULT: D-05 RED reproduced as expected (exit $code; UI-11b FAIL, UI-11-markers PASS)"
exit 0

