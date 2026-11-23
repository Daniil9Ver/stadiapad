; Inno Setup script for StadiaPad.
; Build: build.ps1 first, then:  ISCC.exe installer.iss   ->  bin\StadiaPad-Setup.exe

#define AppName "StadiaPad"
#define AppVersion "1.0.1"
#define AppExe "StadiaPad.exe"
#define AppUrl "https://github.com/Daniil9Ver/stadiapad"

[Setup]
; AppId must never change between versions (it identifies the app for upgrades/uninstall).
AppId={{6EB852BA-5635-423E-8B7D-1C03A56B38F6}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Daniil9Ver
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
AppUpdatesURL={#AppUrl}/releases
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableDirPage=no
DisableProgramGroupPage=yes
OutputDir=bin
OutputBaseFilename=StadiaPad-Setup
SetupIconFile=res\icon.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
VersionInfoVersion={#AppVersion}.0
VersionInfoDescription={#AppName} Setup

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Shortcuts:"

[Files]
Source: "bin\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

[Code]
var
  RemoveDrivers: Boolean;
  NeedRestart: Boolean;

{ Ask StadiaPad to quit gracefully (it un-hides the controller on exit), then force it if needed. }
procedure CloseStadiaPad();
var
  Code: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/im {#AppExe}', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Sleep(1500);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/im {#AppExe} /f', '', SW_HIDE, ewWaitUntilTerminated, Code);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Code: Integer;
begin
  if CurUninstallStep = usUninstall then
  begin
    CloseStadiaPad();
    { Remove StadiaPad's own entries from HidHide (whitelist + hidden Stadia devices). }
    Exec(ExpandConstant('{app}\{#AppExe}'), '--cleanup', '', SW_HIDE, ewWaitUntilTerminated, Code);
    if not UninstallSilent() then
      RemoveDrivers := MsgBox('Also uninstall the ViGEmBus and HidHide drivers?' + #13#10#13#10 +
        'Choose No if other programs on this PC use them (for example DS4Windows).',
        mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
    if RemoveDrivers then
    begin
      { The uninstaller is already elevated, so the helper runs directly. }
      Exec(ExpandConstant('{app}\{#AppExe}'), '--uninstall-drivers', '', SW_HIDE, ewWaitUntilTerminated, Code);
      NeedRestart := True;
    end;
  end;

  if CurUninstallStep = usPostUninstall then
  begin
    { Settings and log of the user who runs the uninstaller. }
    RegDeleteKeyIncludingSubkeys(HKCU, 'Software\StadiaPad');
    DelTree(ExpandConstant('{localappdata}\StadiaPad'), True, True, True);
  end;
end;

function UninstallNeedRestart(): Boolean;
begin
  Result := NeedRestart;
end;
