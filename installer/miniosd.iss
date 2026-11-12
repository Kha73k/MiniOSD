; Mini OSD - Inno Setup Script
#define AppName "Mini OSD"
#define AppVersion "1.1.0"
#define Publisher "Kha73k"
#define AppURL "https://github.com/Kha73k/MiniOSD"
#define AppExeName "osd.exe"

[Setup]
AppId={{D37E90B2-8594-4F85-B2A8-71C569D0E4A1}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#Publisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
UsePreviousAppDir=yes
UsePreviousGroup=yes
UsePreviousTasks=yes
UsePreviousLanguage=yes
UsePreviousSetupType=yes
PrivilegesRequired=admin
OutputDir=..
OutputBaseFilename=MiniOSD-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
AppMutex=MiniOSD.single
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#AppExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "startupicon"; Description: "Start Mini OSD automatically on Windows logon"; GroupDescription: "Startup options:"; Flags: unchecked

[Files]
Source: "..\osd.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\IntelMSR.bin"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\osd.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon
Name: "{commonstartup}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: startupicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: postinstall nowait skipifsilent

[Code]
function IsUpgrade(): Boolean;
begin
  Result := RegKeyExists(HKEY_LOCAL_MACHINE, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#SetupSetting("AppId")}_is1');
end;

procedure InitializeWizard();
begin
  if IsUpgrade() then
  begin
    WizardForm.WelcomeLabel2.Caption :=
      'Setup will update ' + '{#AppName}' + ' on your computer.' + #13#10#13#10 +
      'Any running instance of Mini OSD will be closed automatically, updated, and restarted upon completion.' + #13#10#13#10 +
      'Click Next to proceed with the update.';
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  // Terminate any running osd.exe so files can be cleanly updated
  Exec('taskkill.exe', '/f /im osd.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Sleep(250);
  Result := '';
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
begin
  if CurUninstallStep = usUninstall then
  begin
    Exec('taskkill.exe', '/f /im osd.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Sleep(250);
  end;
end;
