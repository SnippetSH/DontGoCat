; DontGoCat installer (Inno Setup 6). Build with scripts\package.cmd (it passes /DAppVersion=<project version>).
; Manual build: ISCC /DAppVersion=1.2.0 installer\ccat.iss   (dist\DontGoCat\ must exist, see scripts\package.cmd)
; Keep this file ASCII-only (Inno reads non-BOM files as ANSI); Korean UI text comes from Korean.isl.

#ifndef AppVersion
  #define AppVersion "1.2.0"
#endif
#define AppName "DontGoCat"
#define AppExeName "DontGoCat.exe"
#define RunKey "Software\Microsoft\Windows\CurrentVersion\Run"
#define UserDataKey "Software\DontGoCat"
; How long StopApp waits for a graceful exit (WM_CLOSE) before force-killing.
#define GracefulQuitMs 3000

[Setup]
; Stable id: never change it, otherwise updates install side by side instead of upgrading.
AppId={{6E1B7C52-3D8A-4F09-9A4E-C1A5D2B07F63}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=DontGoCat
; Per-user install, no admin prompt. With lowest privileges {autopf} resolves to %LOCALAPPDATA%\Programs.
PrivilegesRequired=lowest
DefaultDirName={autopf}\{#AppName}
DisableProgramGroupPage=yes
SetupIconFile=..\assets\ccat.ico
UninstallDisplayIcon={app}\{#AppExeName}
OutputDir=..\dist
OutputBaseFilename=DontGoCat-Setup-{#AppVersion}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; Close a running DontGoCat on update/uninstall. DontGoCat has no main window, so Restart Manager may not be able to
; close it; [Code] below also stops the app (the only place an image-name kill is acceptable).
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl,custom_ko.isl"

[CustomMessages]
english.DeleteUserData=Also delete DontGoCat statistics and settings (scale, sound)?
; The Korean text lives in custom_ko.isl (UTF-8 with BOM) to keep this file ASCII-only.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\dist\DontGoCat\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Code]
// Stop a running DontGoCat. Scoped to this installer/uninstaller.
// 1) graceful: taskkill without /F posts WM_CLOSE; the app saves its statistics and quits itself.
//    Poll taskkill every 100 ms until it reports exit code 128 (process not found), for at most GracefulQuitMs.
// 2) fallback: force kill.
procedure StopApp();
var
  ResultCode, Waited: Integer;
  Taskkill: String;
  Gone: Boolean;
begin
  Taskkill := ExpandConstant('{sys}\taskkill.exe');
  Gone := False;
  Waited := 0;
  while True do
  begin
    if Exec(Taskkill, '/IM {#AppExeName}', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) and (ResultCode = 128) then
    begin
      Gone := True;
      Break;
    end;
    if Waited >= {#GracefulQuitMs} then
      Break;
    Sleep(100);
    Waited := Waited + 100;
  end;
  if not Gone then
    Exec(Taskkill, '/F /IM {#AppExeName}', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Sleep(300);   // let Windows release the file locks
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  // before Restart Manager checks for files in use
  if CurPageID = wpReady then
    StopApp();
  Result := True;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopApp();   // silent installs never show wpReady
  Result := '';
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    StopApp();
    // The tray popup (not the installer) creates this value, so [Registry] uninsdeletevalue cannot remove it.
    RegDeleteValue(HKEY_CURRENT_USER, '{#RunKey}', 'DontGoCat');
    // Statistics and settings (scale, sound) live in HKCU\{#UserDataKey}. Updates keep them; ask on uninstall only.
    // Silent uninstalls never ask and keep the data. Default button is No.
    if not UninstallSilent then
      if MsgBox(CustomMessage('DeleteUserData'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
        RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, '{#UserDataKey}');
  end;
end;
