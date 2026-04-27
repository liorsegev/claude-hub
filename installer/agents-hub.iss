; -- Agents-Hub installer (Inno Setup 6) -------------------------------------
;
; Build prerequisite: agents-hub.exe must already be built with STATIC_RUNTIME=ON
; into ..\cmake-build-installer\. Use installer\build-installer.ps1 to do
; both steps in one shot.
;
; Compile this script:
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\agents-hub.iss
; or just run installer\build-installer.ps1.

#define MyAppName        "Agents-Hub"
#define MyAppVersion     "0.1.0"
#define MyAppPublisher   "Lior Segev"
#define MyAppExeName     "agents-hub.exe"
#define MyAppURL         "https://github.com/liorsegev/claude-hub"
; Multi-config generator output lives under <build_dir>/Release.
#define BuildDir         "..\cmake-build-installer\Release"

[Setup]
; Stable AppId so future installer versions cleanly upgrade in place. DO NOT
; change once shipped — it's how Windows recognises an existing install.
AppId={{C1F7B4D5-3A9F-4E2A-9A4C-AGENTS-HUB-V1}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableDirPage=auto
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#MyAppExeName}
OutputDir=Output
OutputBaseFilename=agents-hub-setup-{#MyAppVersion}
Compression=lzma2/ultra
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequiredOverridesAllowed=dialog
; Win10 1809 minimum (matches our README requirement).
MinVersion=10.0.17763

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#BuildDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "..\scripts\claude_hub_notify.ps1"; DestDir: "{app}\scripts"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
