; instalador do ditado — simples, confiável, sem elevação
#define MyAppVersion "0.2.1"

[Setup]
AppId={{7A9C4B8E-1B7F-4D3E-9C1A-2F5E8D6C0A4B}
AppName=ditado
AppVersion={#MyAppVersion}
AppPublisher=drico
DefaultDirName={localappdata}\Programs\ditado
DefaultGroupName=ditado
DisableProgramGroupPage=yes
DisableWelcomePage=yes
DisableReadyPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputDir=..\build\installer
OutputBaseFilename=ditado-setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=..\src\icon.ico

[Languages]
Name: "ptbr"; MessagesFile: "compiler:Languages\Portuguese.isl"

[Files]
Source: "..\build\ditado.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\ditado"; Filename: "{app}\ditado.exe"; Comment: "Ditado por voz no gate"

[Run]
Filename: "{app}\ditado.exe"; Description: "Abrir o ditado agora"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{localappdata}\Programs\ditado"
