; DontGoCat installer (Inno Setup 6). Build with scripts\package.cmd (it passes /DAppVersion=<project version>).
; Manual build: ISCC /DAppVersion=1.0.0 installer\ccat.iss   (dist\DontGoCat\ must exist, see scripts\package.cmd)
; Keep this file ASCII-only (Inno reads non-BOM files as ANSI); Korean UI text comes from Korean.isl.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#define AppName "DontGoCat"
#define AppExeName "DontGoCat.exe"
#define RunKey "Software\Microsoft\Windows\CurrentVersion\Run"

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
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"

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
// Stop a running DontGoCat (no main window, so it cannot be asked to close). Scoped to this installer/uninstaller.
procedure StopApp();
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM {#AppExeName}', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
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
  end;
end;
