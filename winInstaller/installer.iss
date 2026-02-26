; Inno Setup Script for OneDirectionCore
#define MyAppName "OneDirectionCore"
#define MyAppVersion "1.0"
#define MyAppPublisher "OneDirection Team"
#define MyAppExeName "OneDirectionCore.exe"

[Setup]
AppId={{D3B3A5E5-7B1A-4F4E-9E8D-7B9C4D5E6F7A}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
OutputDir=.
OutputBaseFilename=OneDirectionCore_Setup
Compression=lzma
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "installvbcable"; Description: "Install VB-Cable Virtual Audio Driver (super required for 7.1 capture)"; GroupDescription: "Audio Driver:"

[Files]
Source: "OneDirectionCore.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "od_core.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "settings_dotnet.cfg"; DestDir: "{app}"; Flags: ignoreversion
Source: "imgui.ini"; DestDir: "{app}"; Flags: ignoreversion
Source: "drivers\*"; DestDir: "{app}\drivers"; Flags: ignoreversion recursesubdirs; Tasks: installvbcable

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\drivers\VBCABLE_Setup_x64.exe"; StatusMsg: "Please click 'Install Driver' in the VB-Cable window..."; Flags: waituntilterminated; Tasks: installvbcable
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{app}\drivers\VBCABLE_Setup_x64.exe"; Parameters: "-u -h"; Flags: waituntilterminated runhidden

