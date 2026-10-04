; Inno Setup Script for OneDirectionCore
;
; Build the app first (see README, "Build the .NET Application"), then compile this script:
;   ISCC.exe winInstaller\installer.iss
; The installer is written to Download\ODC-Setup.exe.
#define MyAppName "OneDirectionCore"
#define MyAppVersion "1.1.0"
#define MyAppPublisher "OneDirection Team"
#define MyAppExeName "OneDirectionCore.exe"
#define PublishDir "..\ui\dotnet\OneDirectionCore\bin\Release\net10.0-windows\win-x64\publish"

[Setup]
AppId={{D3B3A5E5-7B1A-4F4E-9E8D-7B9C4D5E6F7A}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
; Installs for the current user, so no administrator prompt is needed.
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\Download
OutputBaseFilename=ODC-Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; od_core.dll is bundled inside the single-file executable.
Source: "{#PublishDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion

[Registry]
; The app's "Launch on Startup" option writes this value; remove it on uninstall.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "OneDirectionCore"; Flags: dontcreatekey uninsdeletevalue

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
