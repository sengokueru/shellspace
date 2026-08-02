# ShellSpace が正しく置かれているか診断する。
# PowerShell で実行するだけ。何も書き換えない（-Fix を付けたときだけブロック解除する）。
#
#   .\check-install.ps1
#   .\check-install.ps1 -Fix     ... Mark of the Web のブロックを解除する
param([switch]$Fix)

$ErrorActionPreference = 'Continue'

$paths = @(
    @{ Name = 'システム共通（規格上の正式な場所）'; Path = "$env:CommonProgramFiles\VST3" },
    @{ Name = 'ユーザー用（新しめのDAWのみ走査）';   Path = "$env:LOCALAPPDATA\Programs\Common\VST3" }
)

Write-Host ""
Write-Host "=== ShellSpace インストール診断 ===" -ForegroundColor Cyan
Write-Host ""

$found = @()

foreach ($p in $paths) {
    $bundle = Join-Path $p.Path 'ShellSpace.vst3'
    Write-Host ("[{0}]" -f $p.Name)
    Write-Host ("  {0}" -f $p.Path)

    if (-not (Test-Path $p.Path)) {
        Write-Host "  -> フォルダ自体がありません" -ForegroundColor DarkGray
        Write-Host ""
        continue
    }

    if (-not (Test-Path $bundle)) {
        Write-Host "  -> ShellSpace.vst3 が見つかりません" -ForegroundColor DarkGray
        Write-Host ""
        continue
    }

    $item = Get-Item $bundle -Force

    if ($item.PSIsContainer) {
        Write-Host "  -> ShellSpace.vst3 フォルダあり" -ForegroundColor Green
        $found += $bundle
    } else {
        Write-Host "  -> [NG] フォルダではなくファイルとして置かれています" -ForegroundColor Red
        Write-Host "         VST3はバンドル(フォルダ)です。中の DLL だけをコピーすると認識されません。" -ForegroundColor Red
        Write-Host "         zip の ShellSpace.vst3 フォルダを『まるごと』コピーし直してください。" -ForegroundColor Yellow
        Write-Host ""
        continue
    }

    $dll = Join-Path $bundle 'Contents\x86_64-win\ShellSpace.vst3'
    if (Test-Path $dll) {
        $d = Get-Item $dll
        Write-Host ("     Contents\x86_64-win\ShellSpace.vst3  {0:N0} bytes" -f $d.Length) -ForegroundColor Green
        if ($d.Length -lt 1MB) {
            Write-Host "     [NG] サイズが小さすぎます。展開に失敗している可能性があります" -ForegroundColor Red
        }
    } else {
        Write-Host "     [NG] Contents\x86_64-win\ShellSpace.vst3 がありません" -ForegroundColor Red
        Write-Host "          構造が壊れています。zip から展開し直してください。" -ForegroundColor Yellow
    }

    $mi = Join-Path $bundle 'Contents\Resources\moduleinfo.json'
    if (Test-Path $mi) { Write-Host "     moduleinfo.json あり" -ForegroundColor Green }

    # Mark of the Web（ネットから落としたファイルのブロック）
    $blocked = Get-ChildItem $bundle -Recurse -File -Force |
               Where-Object { Get-Item $_.FullName -Stream Zone.Identifier -ErrorAction SilentlyContinue }

    if ($blocked) {
        Write-Host ("     [NG] Windows にブロックされているファイルが {0} 件あります" -f $blocked.Count) -ForegroundColor Red
        if ($Fix) {
            $blocked | Unblock-File
            Write-Host "     -> 解除しました" -ForegroundColor Green
        } else {
            Write-Host "     -> このスクリプトを  .\check-install.ps1 -Fix  で実行すると解除します" -ForegroundColor Yellow
        }
    } else {
        Write-Host "     ブロックなし" -ForegroundColor Green
    }

    Write-Host ""
}

Write-Host "=== まとめ ===" -ForegroundColor Cyan

if ($found.Count -eq 0) {
    Write-Host "ShellSpace.vst3 がどちらの場所にも見つかりませんでした。" -ForegroundColor Red
    Write-Host ""
    Write-Host "zip の中の『ShellSpace.vst3 フォルダ』を、次の場所へまるごとコピーしてください:" -ForegroundColor Yellow
    Write-Host ("  {0}" -f "$env:CommonProgramFiles\VST3") -ForegroundColor Yellow
    Write-Host "  （管理者権限が必要です。エクスプローラーが確認を出したら許可してください）" -ForegroundColor Yellow
} else {
    Write-Host ("配置は問題ありません（{0} 箇所）。" -f $found.Count) -ForegroundColor Green
    Write-Host ""
    Write-Host "それでもDAWに出てこない場合、ブロックリスト入りしている可能性が高いです:" -ForegroundColor Yellow
    Write-Host "  Cubase: スタジオ > VST プラグインマネージャー > Blocklist タブ を開き、" -ForegroundColor Yellow
    Write-Host "          ShellSpace があれば選んで再有効化してから再スキャン" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "一度読み込みに失敗したプラグインは、原因を直しても" -ForegroundColor Yellow
    Write-Host "ブロックリストから外さない限り再スキャンでは復活しません。" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "--- 環境 ---" -ForegroundColor DarkGray
Write-Host ("OS      : {0}" -f (Get-CimInstance Win32_OperatingSystem).Caption)
Write-Host ("アーキ  : {0}" -f $env:PROCESSOR_ARCHITECTURE)
Write-Host ("PS      : {0}" -f $PSVersionTable.PSVersion)
Write-Host ""
Write-Host "この出力をそのまま共有してもらえると原因が特定できます。" -ForegroundColor Cyan
Write-Host ""
