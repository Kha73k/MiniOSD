; Mini OSD - Inno Setup Script
#define AppName "Mini OSD"
#define AppVersion "1.0.0"
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
PrivilegesRequired=admin
OutputDir=..
OutputBaseFilename=MiniOSD-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
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
