# ShellSpace をビルドして VST3 を出力する。
#   .\build.ps1            … Release ビルド
#   .\build.ps1 -Clean     … build/ を消してから
param([switch]$Clean)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

# --- 日本語パス回避 --------------------------------------------------------
# JUCEの juceaide は非ASCIIパスで例外を吐いて .rc 生成に失敗する。
# 実体は同じなので、ASCIIのジャンクション経由でビルドする。
if ($root -match '[^\x00-\x7F]') {
    $parent = Split-Path $root -Parent
    if (-not (Test-Path "C:\ws")) {
        cmd /c mklink /J "C:\ws" "$parent" | Out-Null
    }
    $ascii = Join-Path "C:\ws" (Split-Path $root -Leaf)
    if (Test-Path (Join-Path $ascii "CMakeLists.txt")) {
        Write-Host "日本語パスのため C:\ws 経由でビルドします: $ascii" -ForegroundColor Yellow
        $root = $ascii
    } else {
        throw "ASCIIパスの用意に失敗しました: $ascii"
    }
}

# --- cmake を探す（winget のユーザースコープ導入はPATHに出ないことがある）---
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $cand = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter cmake.exe `
            -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cand) { $cmake = $cand.FullName }
}
if (-not $cmake) { throw "cmake が見つかりません。winget install Kitware.CMake --scope user" }
Write-Host "cmake: $cmake"

# --- IR が生成済みか確認（CMakeでも弾くが、先に分かりやすく出す）---
$irDir = Join-Path $root "..\IR"
if (-not (Test-Path (Join-Path $irDir "Shell_Kick_body.wav"))) {
    Write-Host "IRが未生成なので生成します..." -ForegroundColor Yellow
    py -3 (Join-Path $irDir "make_ir.py") $irDir
}

$build = Join-Path $root "build"
if ($Clean -and (Test-Path $build)) { Remove-Item $build -Recurse -Force }

& $cmake -B $build -S $root -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { throw "configure に失敗しました" }

& $cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) { throw "ビルドに失敗しました" }

$vst3 = Get-ChildItem $build -Recurse -Filter "ShellSpace.vst3" -ErrorAction SilentlyContinue |
        Select-Object -First 1
if ($vst3) {
    Write-Host "`n[OK] $($vst3.FullName)" -ForegroundColor Green
    Write-Host "ユーザー用VST3フォルダにも配置されます (管理者権限不要):"
    Write-Host "  $env:LOCALAPPDATA\Programs\Common\VST3\ShellSpace.vst3"
    Write-Host "`n検証するには:" -ForegroundColor Cyan
    Write-Host "  .\build\ShellSpaceTest_artefacts\Release\ShellSpaceTest.exe `"$env:LOCALAPPDATA\Programs\Common\VST3\ShellSpace.vst3`""
} else {
    throw "VST3 が生成されていません"
}
