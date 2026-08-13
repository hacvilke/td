; TD Engine Inno Setup Script
; Creates a Windows installer for TD Engine

[Setup]
AppName=TD Engine
AppVersion=1.0.0
AppPublisher=TD Engine Team
AppPublisherURL=https://github.com/td-engine
DefaultDirName={autopf}\TDEngine
DefaultGroupName=TD Engine
OutputBaseFilename=td-engine-1.0.0-setup
OutputDir=..\build\installer
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64
SetupIconFile=..\assets\icon.ico
UninstallDisplayIcon={app}\td-editor.exe
LicenseFile=..\LICENSE
WizardStyle=modern
WizardResizable=no
DisableWelcomePage=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "Full installation"
Name: "compact"; Description: "Compact installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "editor"; Description: "TD Engine Editor"; Types: full compact custom; Flags: fixed
Name: "examples"; Description: "Example Projects"; Types: full
Name: "docs"; Description: "Documentation"; Types: full
Name: "sdk"; Description: "Development SDK (headers & lib)"; Types: full

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "quicklaunchicon"; Description: "{cm:CreateQuickLaunchIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Editor executable
Source: "..\build\bin\td-editor.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: editor

; Example game executables
Source: "..\build\bin\pong.exe"; DestDir: "{app}\examples"; Flags: ignoreversion; Components: examples
Source: "..\build\bin\platformer.exe"; DestDir: "{app}\examples"; Flags: ignoreversion; Components: examples

; Engine library
Source: "..\build\lib\td-engine.a"; DestDir: "{app}\sdk\lib"; Flags: ignoreversion; Components: sdk
Source: "..\build\lib\td-engine.lib"; DestDir: "{app}\sdk\lib"; Flags: ignoreversion skipifsourcedoesntexist; Components: sdk

; Headers
Source: "..\src\*.h"; DestDir: "{app}\sdk\include"; Flags: ignoreversion recursesubdirs; Components: sdk

; Assets
Source: "..\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs; Components: editor

; Example source code
Source: "..\examples\*"; DestDir: "{app}\examples\src"; Flags: ignoreversion recursesubdirs; Components: examples

; Documentation
Source: "..\README.md"; DestDir: "{app}\docs"; Flags: ignoreversion; Components: docs

[Icons]
Name: "{group}\TD Engine Editor"; Filename: "{app}\td-editor.exe"
Name: "{group}\Pong Example"; Filename: "{app}\examples\pong.exe"; Components: examples
Name: "{group}\Platformer Example"; Filename: "{app}\examples\platformer.exe"; Components: examples
Name: "{group}\Documentation"; Filename: "{app}\docs\README.md"; Components: docs
Name: "{group}\{cm:UninstallProgram,TD Engine}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\TD Engine Editor"; Filename: "{app}\td-editor.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\td-editor.exe"; Description: "Launch TD Engine Editor"; Flags: nowait postinstall skipifsilent

[Code]
function InitializeSetup: Boolean;
begin
  Result := True;
end;
