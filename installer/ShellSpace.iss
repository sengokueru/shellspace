; ShellSpace インストーラ (Inno Setup 6)
;
; ビルド: iscc installer\ShellSpace.iss
;   -DSourceDir=<配布物のフォルダ>  で入力元を指定できる（既定は ..\dist）
;
; 設計の要点:
;   - Cubase 12 には標準の Common Files\VST3 を走査せず、自身の VST3 しか
;     見ないことがある。DAWのインストール先を検出して、そちらにも入れられるようにする。
;     これをやらないと「入れたのに出てこない」が起きる。
;   - 管理者権限が取れない環境では、ユーザー用VST3フォルダに入れる。

#ifndef SourceDir
  #define SourceDir "..\dist"
#endif

#ifndef AppVersion
  #define AppVersion "0.4.1"
#endif

#define AppName    "ShellSpace"
#define Publisher  "Yokosuka"

[Setup]
AppId={{7C4E0A1E-2B6D-4C39-9A55-5348454C4C53}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#Publisher}
AppSupportURL=https://github.com/sengokueru/shellspace/issues
; 権限に応じて入れ先を変える。固定値にすると、ユーザーモードでも
; Program Files を作りにいってアクセス拒否で失敗する。
DefaultDirName={code:GetVst3Dir}
; アンインストーラをVST3フォルダに置くとプラグインと混ざるので別にする
UninstallFilesDir={code:GetUninstDir}
DisableDirPage=yes
DisableProgramGroupPage=yes
CreateAppDir=yes
UninstallDisplayName={#AppName} {#AppVersion}
OutputDir=.
OutputBaseFilename=ShellSpace-{#AppVersion}-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline dialog
SetupLogging=yes

[Languages]
Name: "ja"; MessagesFile: "compiler:Languages\Japanese.isl"

[CustomMessages]
ja.VstGroup=VST3 プラグイン
ja.ExtraGroup=おまけ
ja.IrTask=IR の wav ファイルも入れる（Cubase の REVerence でそのまま使えます）
ja.DawTask=検出した DAW のフォルダにも入れる（Cubase 12 で認識されない問題への対策）
ja.NoDawDetected=DAW は検出されませんでした
ja.FinishNote=インストールが終わりました。DAW を再起動してプラグインを再スキャンしてください。

[Types]
Name: "full";   Description: "すべて"
Name: "custom"; Description: "カスタム"; Flags: iscustom

[Components]
Name: "vst3"; Description: "{cm:VstGroup}"; Types: full custom; Flags: fixed
Name: "ir";   Description: "{cm:IrTask}";   Types: full

[Files]
; --- VST3 バンドル（標準の場所。{app} = GetVst3Dir）---
Source: "{#SourceDir}\ShellSpace.vst3\*"; DestDir: "{app}\ShellSpace.vst3"; \
  Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3

; --- 検出した DAW の VST3 フォルダにも（Cubase 12 対策）---
Source: "{#SourceDir}\ShellSpace.vst3\*"; DestDir: "{code:GetDawVst3Dir}\ShellSpace.vst3"; \
  Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3; Check: ShouldInstallToDaw

; --- IR の wav ---
Source: "{#SourceDir}\IR\*.wav"; DestDir: "{code:GetDocsDir}\ShellSpace IR"; \
  Flags: ignoreversion; Components: ir

; --- 説明・診断ツール ---
Source: "{#SourceDir}\README.md";            DestDir: "{code:GetDocsDir}\ShellSpace IR"; Flags: ignoreversion; Components: ir
Source: "{#SourceDir}\check-install.ps1";    DestDir: "{code:GetDocsDir}\ShellSpace IR"; Flags: ignoreversion; Components: ir

[Icons]
Name: "{group}\ShellSpace の IR フォルダ"; Filename: "{code:GetDocsDir}\ShellSpace IR"; Components: ir

[Run]
Filename: "{code:GetDocsDir}\ShellSpace IR"; Description: "IR フォルダを開く"; \
  Flags: postinstall shellexec skipifsilent unchecked; Components: ir

[UninstallDelete]
Type: filesandordirs; Name: "{app}\ShellSpace.vst3"

[Code]
var
  DawPage: TInputOptionWizardPage;
  DawDirs: TStringList;

{ 管理者かどうかで VST3 の入れ先を変える }
function GetVst3Dir(Param: String): String;
begin
  if IsAdminInstallMode then
    Result := ExpandConstant('{commoncf}\VST3')
  else
    Result := ExpandConstant('{localappdata}\Programs\Common\VST3');
end;

function GetDocsDir(Param: String): String;
begin
  Result := ExpandConstant('{userdocs}');
end;

{ アンインストーラの置き場所。VST3フォルダに混ぜない }
function GetUninstDir(Param: String): String;
begin
  if IsAdminInstallMode then
    Result := ExpandConstant('{commonpf}\ShellSpace')
  else
    Result := ExpandConstant('{localappdata}\ShellSpace');
end;

{ Program Files\Steinberg\<製品>\VST3 を探す }
procedure FindDawDirs;
var
  Roots: array[0..1] of String;
  FR: TFindRec;
  I: Integer;
  Root, Candidate: String;
begin
  DawDirs := TStringList.Create;
  Roots[0] := ExpandConstant('{commonpf64}\Steinberg');
  Roots[1] := ExpandConstant('{commonpf32}\Steinberg');

  for I := 0 to 1 do
  begin
    Root := Roots[I];
    if not DirExists(Root) then
      Continue;
    if FindFirst(Root + '\*', FR) then
    begin
      try
        repeat
          if (FR.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0 then
          begin
            if (FR.Name <> '.') and (FR.Name <> '..') then
            begin
              Candidate := Root + '\' + FR.Name + '\VST3';
              if DirExists(Candidate) and (DawDirs.IndexOf(Candidate) < 0) then
                DawDirs.Add(Candidate);
            end;
          end;
        until not FindNext(FR);
      finally
        FindClose(FR);
      end;
    end;
  end;
end;

procedure InitializeWizard;
var
  I: Integer;
begin
  FindDawDirs;

  DawPage := CreateInputOptionPage(wpSelectComponents,
    'DAW のフォルダにも入れますか',
    'Cubase 12 には、標準の VST3 フォルダを見に行かないことがある既知の問題があります。',
    'その場合、下のフォルダにも入れておくと確実に認識されます。' + #13#10 +
    '不要なら外して構いません（あとから手でコピーしても直せます）。',
    False, False);

  if DawDirs.Count = 0 then
  begin
    DawPage.Add(ExpandConstant('{cm:NoDawDetected}'));
    DawPage.CheckListBox.ItemEnabled[0] := False;
  end
  else
  begin
    for I := 0 to DawDirs.Count - 1 do
    begin
      DawPage.Add(DawDirs[I]);
      { Cubase 12 は既定でON。それ以外もONにしておく（害がない） }
      DawPage.Values[I] := True;
    end;
  end;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if (PageID = DawPage.ID) and (DawDirs.Count = 0) then
    Result := True;
end;

{ 選ばれた DAW フォルダ（先頭の1つ）。複数ある場合は CurStepChanged で追加コピーする }
function GetDawVst3Dir(Param: String): String;
var
  I: Integer;
begin
  Result := '';
  if DawDirs = nil then
    Exit;
  for I := 0 to DawDirs.Count - 1 do
    if DawPage.Values[I] then
    begin
      Result := DawDirs[I];
      Exit;
    end;
end;

function ShouldInstallToDaw: Boolean;
begin
  Result := (GetDawVst3Dir('') <> '');
end;

{ 2つ目以降の DAW フォルダへは、インストール後にコピーする }
procedure CurStepChanged(CurStep: TSetupStep);
var
  I, Count: Integer;
  Src, Dst: String;
begin
  if CurStep <> ssPostInstall then
    Exit;
  if DawDirs = nil then
    Exit;

  Src := GetVst3Dir('') + '\ShellSpace.vst3';
  Count := 0;

  for I := 0 to DawDirs.Count - 1 do
  begin
    if not DawPage.Values[I] then
      Continue;
    Count := Count + 1;
    if Count = 1 then
      Continue;  { 1つ目は [Files] で処理済み }

    Dst := DawDirs[I] + '\ShellSpace.vst3';
    if DirExists(Dst) then
      DelTree(Dst, True, True, True);
    if not DirExists(Dst) then
      ForceDirectories(Dst);
    { フォルダごとコピー }
    Exec(ExpandConstant('{cmd}'), '/c xcopy "' + Src + '" "' + Dst + '" /E /I /Y /Q',
         '', SW_HIDE, ewWaitUntilTerminated, I);
  end;
end;

procedure DeinitializeSetup;
begin
  if DawDirs <> nil then
    DawDirs.Free;
end;
