# ShellSpace が正しく置かれ、実際に読み込めるかを診断する。
# 何も書き換えない（-Fix を付けたときだけ Mark of the Web を解除する）。
#
#   .\check-install.ps1
#   .\check-install.ps1 -Fix
param([switch]$Fix)

$ErrorActionPreference = 'Continue'

# 版ごとに変わる値を固定で持たない。壊れているかどうかの下限だけを見る。
$MinPluginBytes = 4MB

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

# Cubase 12 には、標準の Common Files\VST3 を走査せず
# 自身のインストール先の VST3 しか見ないことがある（既知の問題）。
$daw = @()
foreach ($root in @("$env:ProgramFiles\Steinberg", "${env:ProgramFiles(x86)}\Steinberg")) {
    if (-not (Test-Path $root)) { continue }
    Get-ChildItem $root -Directory -EA SilentlyContinue | ForEach-Object {
        $v = Join-Path $_.FullName 'VST3'
        if (Test-Path $v) {
            $paths += @{ Name = ("DAW内部（{0}）" -f $_.Name); Path = $v }
            $daw += @{ Name = $_.Name; Vst3 = $v }
        }
    }
}

$found = @()

foreach ($p in $paths) {
    $bundle = Join-Path $p.Path 'ShellSpace.vst3'
    Write-Host ""
    Write-Host ("[{0}]" -f $p.Name)
    Write-Host ("  {0}" -f $p.Path)

    if (-not (Test-Path $p.Path)) { Write-Host "  -> フォルダ自体がありません" -ForegroundColor DarkGray; continue }

    # このフォルダに何が入っているかを全部出す。
    # 「VST3に置いた」と思っていても階層が1つ深いことがあるので、目で確認できるようにする。
    $entries = Get-ChildItem $p.Path -Force -EA SilentlyContinue
    if ($entries) {
        Write-Host "  中身:" -ForegroundColor DarkGray
        foreach ($e in $entries) {
            $kind = if ($e.PSIsContainer) { '[フォルダ]' } else { '[ファイル]' }
            Write-Host ("    {0} {1}" -f $kind, $e.Name) -ForegroundColor DarkGray
        }
    } else {
        Write-Host "  中身: (空)" -ForegroundColor DarkGray
    }

    if (-not (Test-Path $bundle)) {
        # 直下に無くても、深い階層に置かれている可能性がある
        $deep = Get-ChildItem $p.Path -Recurse -Force -Filter 'ShellSpace.vst3' -Depth 4 -EA SilentlyContinue |
                Select-Object -First 5
        if ($deep) {
            Write-Host "  -> [NG] 直下にはありませんが、深い階層に見つかりました:" -ForegroundColor Red
            foreach ($d in $deep) { Write-Host ("       {0}" -f $d.FullName) -ForegroundColor Yellow }
            Write-Host "       DAWによっては深い階層を走査しません。" -ForegroundColor Yellow
            Write-Host ("       ShellSpace.vst3 フォルダを {0} の『直下』へ移動してください。" -f $p.Path) -ForegroundColor Yellow
            foreach ($d in $deep) {
                $dd = Get-Item $d.FullName -Force
                if ($dd.PSIsContainer) {
                    $inner = Join-Path $d.FullName 'Contents\x86_64-win\ShellSpace.vst3'
                    if (Test-Path $inner) { $found += @{ Dll = $inner; Layout = 'bundle(deep)' } }
                } else {
                    $found += @{ Dll = $d.FullName; Layout = 'single-file(deep)' }
                }
            }
        } else {
            Write-Host "  -> ShellSpace.vst3 がありません" -ForegroundColor DarkGray
        }
        continue
    }

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
    foreach ($d in $daw) {
        Write-Host ("  {0}   ← {1} 用。こちらにも置くと確実" -f $d.Vst3, $d.Name) -ForegroundColor Green
    }
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

    # サイズとハッシュは版ごとに変わるので、固定値との一致では判定しない。
    # (固定値を持つと、更新のたびに全ユーザーへ誤警告が出る)
    # ここでは「明らかに壊れている」場合だけを弾き、ハッシュは表示にとどめる。
    $d = Get-Item $dll -Force
    Write-Host ("  サイズ  : {0:N0} bytes" -f $d.Length) -NoNewline
    if ($d.Length -ge $MinPluginBytes) {
        Write-Host "  (妥当)" -ForegroundColor Green
    } else {
        Write-Host ("  [NG] 小さすぎます（{0:N0} bytes 未満）" -f $MinPluginBytes) -ForegroundColor Red
        Write-Host "  -> 展開に失敗しているか、アンチウイルスに削られています" -ForegroundColor Yellow
    }

    $h = (Get-FileHash $dll -Algorithm SHA256).Hash
    Write-Host ("  SHA256  : {0}" -f $h) -ForegroundColor DarkGray
    Write-Host "            リリースの SHA256SUMS.txt と見比べてください" -ForegroundColor DarkGray

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
$anyDaw = $false
foreach ($root in @("$env:ProgramFiles\Steinberg", "${env:ProgramFiles(x86)}\Steinberg")) {
    if (-not (Test-Path $root)) { continue }
    Get-ChildItem $root -Directory -EA SilentlyContinue | ForEach-Object {
        $anyDaw = $true
        $exe = Get-ChildItem $_.FullName -Filter '*.exe' -EA SilentlyContinue | Select-Object -First 1
        $ver = if ($exe) { $exe.VersionInfo.ProductVersion } else { '' }
        Write-Host ("  {0}  {1}" -f $_.Name, $ver)
    }
}
if (-not $anyDaw) { Write-Host "  Steinberg 製品は見つかりませんでした（別の場所にある可能性あり）" -ForegroundColor DarkGray }

if ($daw | Where-Object { $_.Name -match 'Cubase 12' }) {
    Write-Host ""
    Write-Host "  ★ Cubase 12 には、標準の Common Files\VST3 を走査せず" -ForegroundColor Yellow
    Write-Host "     自身の VST3 フォルダしか見ないことがある既知の問題があります。" -ForegroundColor Yellow
    Write-Host "     出てこない場合は、上記の DAW内部 VST3 フォルダにも置いてください。" -ForegroundColor Yellow
    Write-Host "     12.0.20 以降への更新で直ったという報告もあります。" -ForegroundColor Yellow
}

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
