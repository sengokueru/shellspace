# ShellSpace が正しく置かれ、実際に読み込めるかを診断する。
# 何も書き換えない（-Fix を付けたときだけ Mark of the Web を解除する）。
#
#   .\check-install.ps1
#   .\check-install.ps1 -Fix
param([switch]$Fix)

$ErrorActionPreference = 'Continue'

$ExpectedHash = '861DEADAFEEC62E933F5C431048FC7BED1A31E51B3907C561DACF74C858009CF'
$ExpectedSize = 13743616

Add-Type -Namespace Win -Name Native -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
public static extern IntPtr LoadLibraryW(string lpFileName);
[DllImport("kernel32.dll", SetLastError=true)]
public static extern IntPtr GetProcAddress(IntPtr hModule, string procName);
[DllImport("kernel32.dll", SetLastError=true)]
public static extern bool FreeLibrary(IntPtr hModule);
'@

function Write-Head($t) { Write-Host ""; Write-Host "=== $t ===" -ForegroundColor Cyan }

Write-Head "ShellSpace インストール診断"

if (-not [Environment]::Is64BitProcess) {
    Write-Host "[NG] 32bit の PowerShell で動いています。64bit の PowerShell で実行してください。" -ForegroundColor Red
    return
}

$paths = @(
    @{ Name = 'システム共通（規格上の正式な場所）'; Path = "$env:CommonProgramFiles\VST3" },
    @{ Name = 'ユーザー用（新しめのDAWのみ走査）';   Path = "$env:LOCALAPPDATA\Programs\Common\VST3" }
)

$found = @()

foreach ($p in $paths) {
    $bundle = Join-Path $p.Path 'ShellSpace.vst3'
    Write-Host ""
    Write-Host ("[{0}]" -f $p.Name)
    Write-Host ("  {0}" -f $p.Path)

    if (-not (Test-Path $p.Path)) { Write-Host "  -> フォルダ自体がありません" -ForegroundColor DarkGray; continue }
    if (-not (Test-Path $bundle)) { Write-Host "  -> ShellSpace.vst3 がありません" -ForegroundColor DarkGray; continue }

    $item = Get-Item $bundle -Force

    if (-not $item.PSIsContainer) {
        # 旧来の単一DLL形式。これはこれで動くホストもある
        Write-Host "  -> ShellSpace.vst3 がファイルとして置かれています（旧来の単一DLL形式）" -ForegroundColor Yellow
        $found += @{ Dll = $bundle; Layout = 'single-file' }
        continue
    }

    Write-Host "  -> ShellSpace.vst3 フォルダあり（バンドル形式）" -ForegroundColor Green

    $dll = Join-Path $bundle 'Contents\x86_64-win\ShellSpace.vst3'
    if (-not (Test-Path $dll)) {
        Write-Host "     [NG] Contents\x86_64-win\ShellSpace.vst3 がありません" -ForegroundColor Red
        Write-Host "          構造が壊れています。zip から展開し直してください。" -ForegroundColor Yellow
        continue
    }
    $found += @{ Dll = $dll; Layout = 'bundle' }
}

if ($found.Count -eq 0) {
    Write-Head "VST3フォルダに無いので、置き間違いを探します"

    # VST2フォルダに置くのが最も多い間違い。VST3はVST2フォルダを一切走査しない。
    $wrong = @(
        "$env:ProgramFiles\VSTPlugins",
        "$env:ProgramFiles\Steinberg\VSTPlugins",
        "$env:ProgramFiles\Common Files\Steinberg\VST2",
        "${env:ProgramFiles(x86)}\VSTPlugins",
        "${env:ProgramFiles(x86)}\Steinberg\VSTPlugins",
        "${env:ProgramFiles(x86)}\Common Files\VST3",
        "$env:USERPROFILE\Downloads",
        "$env:USERPROFILE\Desktop",
        "$env:USERPROFILE\Documents"
    )

    $hits = @()
    foreach ($w in $wrong) {
        if (-not (Test-Path $w)) { continue }
        Get-ChildItem $w -Recurse -Force -Filter 'ShellSpace.vst3' -Depth 4 -EA SilentlyContinue |
            ForEach-Object { $hits += $_ }
    }

    if ($hits) {
        Write-Host ""
        Write-Host "見つかりました。ただし DAW が走査しない場所です:" -ForegroundColor Red
        $hits | Select-Object -Unique FullName | ForEach-Object {
            $kind = if ((Get-Item $_.FullName -Force).PSIsContainer) { 'フォルダ' } else { 'ファイル' }
            Write-Host ("  {0}  [{1}]" -f $_.FullName, $kind) -ForegroundColor Yellow
        }
        Write-Host ""
        Write-Host "★ VST3 は VST2 とは別のフォルダです。VST2フォルダに置いても認識されません。" -ForegroundColor Red
    } else {
        Write-Host ""
        Write-Host "よくある置き間違い先にも見つかりませんでした。" -ForegroundColor DarkGray
    }

    Write-Head "やること"
    Write-Host "zip の中の『ShellSpace.vst3 フォルダ』を、次の場所へまるごとコピーしてください:" -ForegroundColor Yellow
    Write-Host ""
    Write-Host ("  {0}" -f "$env:CommonProgramFiles\VST3") -ForegroundColor Green
    Write-Host ""
    Write-Host "エクスプローラーのアドレス欄に次を貼れば、その場所が直接開きます:" -ForegroundColor Yellow
    Write-Host "  %CommonProgramFiles%\VST3" -ForegroundColor Green
    Write-Host ""
    Write-Host "※ バンドル内に同じ名前の ShellSpace.vst3（DLL本体）があります。" -ForegroundColor Yellow
    Write-Host "   そちらではなく、外側のフォルダをコピーしてください。" -ForegroundColor Yellow
    Write-Host "※ VSTPlugins など VST2 用のフォルダではありません。" -ForegroundColor Yellow
}

foreach ($f in $found) {
    $dll = $f.Dll
    Write-Head ("ファイル検査: {0}" -f $dll)

    $d = Get-Item $dll -Force
    Write-Host ("  サイズ  : {0:N0} bytes" -f $d.Length) -NoNewline
    if ($d.Length -eq $ExpectedSize) { Write-Host "  (一致)" -ForegroundColor Green }
    else { Write-Host ("  [NG] 期待値 {0:N0}" -f $ExpectedSize) -ForegroundColor Red }

    $h = (Get-FileHash $dll -Algorithm SHA256).Hash
    if ($h -eq $ExpectedHash) {
        Write-Host "  ハッシュ: 一致（ファイルは無傷）" -ForegroundColor Green
    } else {
        Write-Host "  ハッシュ: [NG] 不一致" -ForegroundColor Red
        Write-Host ("            実際 {0}" -f $h) -ForegroundColor DarkGray
        Write-Host "  -> ダウンロードが壊れているか、アンチウイルスに書き換えられています" -ForegroundColor Yellow
    }

    # Mark of the Web
    $zone = Get-Item $dll -Stream Zone.Identifier -ErrorAction SilentlyContinue
    if ($zone) {
        Write-Host "  ブロック: [NG] Windows にブロックされています（Mark of the Web）" -ForegroundColor Red
        if ($Fix) {
            Get-ChildItem (Split-Path (Split-Path $dll -Parent) -Parent) -Recurse -File -Force | Unblock-File
            Unblock-File $dll
            Write-Host "  -> 解除しました。DAWで再スキャンしてください。" -ForegroundColor Green
        } else {
            Write-Host "  -> .\check-install.ps1 -Fix で解除できます" -ForegroundColor Yellow
        }
    } else {
        Write-Host "  ブロック: なし" -ForegroundColor Green
    }

    # ---- 実際に読み込めるか（ここが決定的）----
    Write-Host ""
    Write-Host "  DLLを実際に読み込んでみます..." -ForegroundColor Cyan
    $h1 = [Win.Native]::LoadLibraryW($dll)

    if ($h1 -eq [IntPtr]::Zero) {
        $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
        $msg = (New-Object System.ComponentModel.Win32Exception($err)).Message
        Write-Host ("  [NG] 読み込み失敗  Win32 error {0}: {1}" -f $err, $msg) -ForegroundColor Red
        Write-Host ""
        switch ($err) {
            5    { Write-Host "  -> アクセス拒否。アンチウイルスがブロックしている可能性が高いです。" -ForegroundColor Yellow }
            126  { Write-Host "  -> 依存DLLが見つかりません。想定外です（このプラグインはWindows標準DLLしか使いません）。" -ForegroundColor Yellow }
            193  { Write-Host "  -> 形式が不正。32bit環境か、ファイルが壊れています。" -ForegroundColor Yellow }
            225  { Write-Host "  -> ウイルス対策ソフトにブロックされています。除外設定を追加してください。" -ForegroundColor Yellow }
            default { Write-Host "  -> このエラーコードを共有してください。" -ForegroundColor Yellow }
        }
        Write-Host "  ★ ファイル側の問題です。DAWの設定ではありません。" -ForegroundColor Red
    } else {
        $fn = [Win.Native]::GetProcAddress($h1, "GetPluginFactory")
        [void][Win.Native]::FreeLibrary($h1)

        if ($fn -eq [IntPtr]::Zero) {
            Write-Host "  [NG] 読み込めましたが VST3 のエントリポイントがありません" -ForegroundColor Red
        } else {
            Write-Host "  [OK] 読み込み成功。VST3 のエントリポイントも確認できました。" -ForegroundColor Green
            Write-Host "  ★ ファイルは正常です。原因はDAW側（走査パス or ブロックリスト）です。" -ForegroundColor Green
        }
    }
}

# ---- DAW の情報 ----
Write-Head "DAW"
$cubase = @()
foreach ($root in @("$env:ProgramFiles\Steinberg", "${env:ProgramFiles(x86)}\Steinberg")) {
    if (Test-Path $root) {
        Get-ChildItem $root -Directory -EA SilentlyContinue | ForEach-Object { $cubase += $_.Name }
    }
}
if ($cubase) { $cubase | Sort-Object -Unique | ForEach-Object { Write-Host ("  {0}" -f $_) } }
else { Write-Host "  Steinberg 製品は見つかりませんでした（別の場所にある可能性あり）" -ForegroundColor DarkGray }

Write-Head "環境"
Write-Host ("  OS    : {0}" -f (Get-CimInstance Win32_OperatingSystem).Caption)
Write-Host ("  アーキ: {0}" -f $env:PROCESSOR_ARCHITECTURE)
Write-Host ("  PS    : {0}" -f $PSVersionTable.PSVersion)

Write-Host ""
Write-Host "この出力をそのまま共有してください。" -ForegroundColor Cyan
Write-Host ""
Write-Host "【あわせて確認してほしいこと】" -ForegroundColor Cyan
Write-Host "  Cubase の スタジオ > VST プラグインマネージャー > Blocklist に" -ForegroundColor Cyan
Write-Host "  ShellSpace が『ある』か『ない』か。" -ForegroundColor Cyan
Write-Host "    ない -> Cubaseはファイルを一度も見ていない（置き場所の問題）" -ForegroundColor Cyan
Write-Host "    ある -> 見つけてはいるが読み込みに失敗している" -ForegroundColor Cyan
Write-Host ""
