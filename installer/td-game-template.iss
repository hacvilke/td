; =============================================================================
;  TD Engine — Inno Setup installer template
; =============================================================================
;  This is a TEMPLATE. The Python orchestrator (tools/bundler/bundle.py) reads
;  this file, substitutes every {{PLACEHOLDER}}, and writes a concrete .iss
;  into the staging directory before invoking ISCC.exe.
;
;  Customisation points (all filled by bundle.py from --name / --version /
;  --publisher / --icon / --game-id CLI args, or from game.tdproj in the
;  game folder):
;
;     {{APP_NAME}}        e.g. "My Cool Game"
;     {{APP_VERSION}}     e.g. "1.0.0"
;     {{APP_PUBLISHER}}   e.g. "Some Studio"
;     {{APP_ID}}          e.g. "my-cool-game"          (folder + registry key)
;     {{APP_EXE}}         e.g. "MyCoolGame.exe"        (the host shell)
;     {{APP_ICON}}        path to .ico, or "host.exe" to use the default
;     {{APP_URL}}         e.g. "https://example.com"   (empty string if none)
;
;  Staging layout produced by bundle.py (relative to the staging dir):
;     host\MyCoolGame.exe        ← the WebView2 host shell (renamed)
;     runtime\td-engine.js       ← Emscripten glue
;     runtime\td-engine.wasm     ← compiled C++ engine
;     runtime\js_bridge.js       ← TDBridge global
;     runtime\*.js                ← any other web/ modules the game imports
;     game\index.html            ← user's game entry point
;     game\*.js / *.ts / assets/ ← user's game files (recursive)
;     webview2\MicrosoftEdgeWebview2Setup.exe  (optional, if --bundle-runtime)
;
;  To customise further, copy this file into your game folder as
;  `installer.iss` and bundle.py will use yours instead of this template.
; =============================================================================

#define MyAppName          "{{APP_NAME}}"
#define MyAppVersion       "{{APP_VERSION}}"
#define MyAppPublisher     "{{APP_PUBLISHER}}"
#define MyAppExeName       "{{APP_EXE}}"
#define MyAppId            "{{APP_ID}}"
#define MyAppURL           "{{APP_URL}}"
#define MyIconSource       "{{APP_ICON}}"

[Setup]
; ---- App identity -----------------------------------------------------------
; AppId: the {{ is an escaped { in Inno Setup. {#MyAppId} is the ISPP
; preprocessor substitution. Result after both passes: {td-bouncing-ball}
AppId={{#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}

; ---- Install location -------------------------------------------------------
; {autopf} resolves to Program Files\ on 64-bit (respects 32/64-bit install
; scope per InstallScope setting below).
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}

; ---- Uninstall --------------------------------------------------------------
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName}
; Allow the user to keep or wipe save data on uninstall. See [Code] block.
Uninstallable=yes
CreateUninstallRegKey=yes

; ---- Output -----------------------------------------------------------------
; bundle.py sets OutputDir= to the absolute staging path before invoking ISCC.
OutputBaseFilename={#MyAppId}-setup

; ---- Compression ------------------------------------------------------------
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes

; ---- Wizard style -----------------------------------------------------------
WizardStyle=modern
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DisableDirPage=no
DisableProgramGroupPage=yes

; ---- Misc -------------------------------------------------------------------
LanguageDetectionMethod=uilanguage
ShowLanguageDialog=yes
CloseApplications=no
RestartIfNeededByRun=no

; ---- Optional: bundle the WebView2 Evergreen bootstrapper -------------------
; If --bundle-runtime was passed, bundle.py emits this line verbatim. Otherwise
; it is stripped. The bootstrapper (~2 MB) installs the WebView2 runtime on
; systems that don't already have it (Win10 1809+ / Win11).
{{WEBVIEW2_BOOTSTRAPPER_LINE}}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "armenian"; MessagesFile: "compiler:Languages\Armenian.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "bulgarian"; MessagesFile: "compiler:Languages\Bulgarian.isl"
Name: "catalan"; MessagesFile: "compiler:Languages\Catalan.isl"
Name: "corsican"; MessagesFile: "compiler:Languages\Corsican.isl"
Name: "czech"; MessagesFile: "compiler:Languages\Czech.isl"
Name: "danish"; MessagesFile: "compiler:Languages\Danish.isl"
Name: "dutch"; MessagesFile: "compiler:Languages\Dutch.isl"
Name: "finnish"; MessagesFile: "compiler:Languages\Finnish.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "hebrew"; MessagesFile: "compiler:Languages\Hebrew.isl"
Name: "icelandic"; MessagesFile: "compiler:Languages\Icelandic.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "norwegian"; MessagesFile: "compiler:Languages\Norwegian.isl"
Name: "polish"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "portuguese"; MessagesFile: "compiler:Languages\Portuguese.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "slovak"; MessagesFile: "compiler:Languages\Slovak.isl"
Name: "slovenian"; MessagesFile: "compiler:Languages\Slovenian.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "ukrainian"; MessagesFile: "compiler:Languages\Ukrainian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "quicklaunchicon"; Description: "{cm:CreateQuickLaunchIcon}"; GroupDescription: "{cm:AdditionalIcons}"; OnlyBelowVersion: 6.01; Flags: unchecked
Name: "associate_tdsave"; Description: "Associate .tdsave files with {#MyAppName}"; GroupDescription: "File associations:"
Name: "associate_tdscene"; Description: "Associate .tdscene files with {#MyAppName}"; GroupDescription: "File associations:"

[Files]
; ---- The host shell ---------------------------------------------------------
Source: "host\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion

; ---- The engine runtime (WASM + JS bridge) ----------------------------------
Source: "runtime\*"; DestDir: "{app}\runtime"; Flags: ignoreversion recursesubdirs createallsubdirs

; ---- The user's game files --------------------------------------------------
Source: "game\*"; DestDir: "{app}\game"; Flags: ignoreversion recursesubdirs createallsubdirs

; ---- WebView2 bootstrapper (only emitted if --bundle-runtime) ---------------
{{WEBVIEW2_FILES_LINE}}

; ---- License + readme (optional, bundle.py skips if absent) -----------------
{{LICENSE_FILE_LINE}}
{{README_FILE_LINE}}

[Icons]
; ---- Start Menu shortcut ----------------------------------------------------
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"

; ---- Desktop shortcut (optional, gated by Tasks) ----------------------------
Name: "{commondesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon; IconFilename: "{app}\{#MyAppExeName}"

; ---- Quick Launch (legacy, pre-Win7) ----------------------------------------
Name: "{userappdata}\Microsoft\Internet Explorer\Quick Launch\User Pinned\TaskBar\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: quicklaunchicon; IconFilename: "{app}\{#MyAppExeName}"

[Registry]
; ---- Per-machine install record (for "is this installed?" checks) -----------
Root: HKLM; Subkey: "Software\{#MyAppPublisher}\{#MyAppName}"; ValueType: string; ValueName: "InstallPath"; ValueData: "{app}"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\{#MyAppPublisher}\{#MyAppName}"; ValueType: string; ValueName: "Version"; ValueData: "{#MyAppVersion}"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\{#MyAppPublisher}\{#MyAppName}"; ValueType: string; ValueName: "ExePath"; ValueData: "{app}\{#MyAppExeName}"; Flags: uninsdeletekey

; ---- File associations (.tdsave — TD Engine save file) ----------------------
Root: HKCR; Subkey: ".tdsave"; ValueType: string; ValueName: ""; ValueData: "{#MyAppName}.SaveFile"; Flags: uninsdeletevalue; Tasks: associate_tdsave
Root: HKCR; Subkey: "{#MyAppName}.SaveFile"; ValueType: string; ValueName: ""; ValueData: "{#MyAppName} Save File"; Flags: uninsdeletekey; Tasks: associate_tdsave
Root: HKCR; Subkey: "{#MyAppName}.SaveFile\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"; Flags: uninsdeletekey; Tasks: associate_tdsave
Root: HKCR; Subkey: "{#MyAppName}.SaveFile\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" --load-save ""%1"""; Flags: uninsdeletekey; Tasks: associate_tdsave

; ---- File associations (.tdscene — TD Engine scene file) --------------------
Root: HKCR; Subkey: ".tdscene"; ValueType: string; ValueName: ""; ValueData: "{#MyAppName}.SceneFile"; Flags: uninsdeletevalue; Tasks: associate_tdscene
Root: HKCR; Subkey: "{#MyAppName}.SceneFile"; ValueType: string; ValueName: ""; ValueData: "{#MyAppName} Scene File"; Flags: uninsdeletekey; Tasks: associate_tdscene
Root: HKCR; Subkey: "{#MyAppName}.SceneFile\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"; Flags: uninsdeletekey; Tasks: associate_tdscene
Root: HKCR; Subkey: "{#MyAppName}.SceneFile\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" --load-scene ""%1"""; Flags: uninsdeletekey; Tasks: associate_tdscene

; ---- Windows Game Explorer / GameUX registration (optional) -----------------
; Registers the game in Windows Games Explorer so it shows up in Win+G and
; the Games folder. Commented out by default — uncomment if you ship a
; GameDefinitionFile (GDF).
; Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\GameUX\GamesToFindOnUpgrade"; ValueType: string; ValueName: "{#MyAppId}"; ValueData: "{app}\{#MyAppExeName}"; Flags: uninsdeletekey

[Run]
; ---- Post-install: launch the game (optional) -------------------------------
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent runascurrentuser

; ---- Post-install: custom hook the host shell can implement ----------------
; If the host .exe supports --post-install, it can do first-run setup here
; (e.g. unpacking compressed assets, creating user save dirs, etc.).
; The hook MUST exit 0 quickly — Inno Setup blocks on it.
Filename: "{app}\{#MyAppExeName}"; Parameters: "--post-install ""{app}"""; Flags: runhidden nowait; Check: CanRunPostInstallHook

[UninstallRun]
; ---- Pre-uninstall: custom hook the host shell can implement ---------------
; Lets the host clean up before its files are deleted (e.g. flush saves to
; a cloud sync, deregister from a multiplayer service, kill a daemon).
Filename: "{app}\{#MyAppExeName}"; Parameters: "--pre-uninstall ""{app}"""; Flags: runhidden; Check: CanRunPreUninstallHook

[UninstallDelete]
; ---- Clean up generated files the installer didn't create -------------------
; Type: files;  Name: "{app}\*.log"
; Type: dirifempty; Name: "{app}"

; ---- IMPORTANT: save data cleanup is in the [Code] block below, gated by a
; ---- user prompt, because silently deleting saves is hostile.

[Code]
// =============================================================================
//  Custom wizard logic
// =============================================================================
//  Inno Setup's [Code] section is Pascal. Functions here are called by name
//  from the [Tasks]/[Files]/[Run]/[Registry] sections via Check: / Components:
//  attributes, or by the wizard at well-defined lifecycle points
//  (InitializeWizard, PrepareToInstall, CurStepChanged, CurUninstallStepChanged).
// =============================================================================

var
  // Custom wizard page: "Install scope" (per-user vs all-users) shown before
  // the standard ready page. Inno Setup already supports this via
  // PrivilegesRequiredOverridesAllowed=dialog, but we also expose a custom
  // page for games that want to explain the trade-off in their own words.
  SaveDataPromptPage: TOutputMsgWizardPage;

// ---- Helper: read a registry string with a sane default --------------------
function RegGetString(const Root: Cardinal; const Key, Name: String; var OutVal: String): Boolean;
var
  Temp: String;
begin
  Result := RegQueryStringValue(Root, Key, Name, Temp);
  if Result then OutVal := Temp;
end;

// ---- Helper: does the host .exe accept --post-install? ---------------------
// We probe by checking a sentinel registry value the host writes on first run.
// This avoids invoking the hook on hosts that don't implement it (the host
// would just exit 0 immediately, but probing is cleaner).
function CanRunPostInstallHook: Boolean;
var
  Dummy: String;
begin
  Result := RegQueryStringValue(HKLM, 'Software\' + '{#MyAppPublisher}' + '\' + '{#MyAppName}',
                                'SupportsPostInstall', Dummy);
end;

// ---- Helper: does the host .exe accept --pre-uninstall? --------------------
function CanRunPreUninstallHook: Boolean;
var
  Dummy: String;
begin
  Result := RegQueryStringValue(HKLM, 'Software\' + '{#MyAppPublisher}' + '\' + '{#MyAppName}',
                                'SupportsPreUninstall', Dummy);
end;

// ---- Wizard init: build the save-data prompt page --------------------------
procedure InitializeWizard;
begin
  SaveDataPromptPage := CreateOutputMsgPage(wpSelectTasks,
    'Save Data',
    'Would you like to keep your save files when uninstalling?',
    'When {#MyAppName} is uninstalled, the installer can optionally remove your save files and settings.' + #13#10 + #13#10 +
    'If you plan to reinstall later, keep them. If you are done with the game, remove them to free up space.' + #13#10 + #13#10 +
    'This choice only takes effect during uninstall — your saves are always kept during updates.');
end;

// ---- Pre-install: system requirements check --------------------------------
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Version: TWindowsVersion;
  WebView2Installed: Boolean;
  EdgeKey: String;
  ResultCode: Integer;
begin
  Result := '';

  // ---- Check: Windows 10 1809 (build 17763) or later -----------------------
  // WebView2 requires Win10 1809+ / Win11. Earlier versions can install the
  // app but the host will fail to create a WebView2 environment at runtime.
  GetWindowsVersionEx(Version);
  if (Version.Major < 10) or
     ((Version.Major = 10) and (Version.Build < 17763)) then begin
    Result := '{#MyAppName} requires Windows 10 version 1809 (build 17763) or later.' + #13#10 +
              'You can install anyway, but the game will not start until you upgrade Windows.';
    Exit;
  end;

  // ---- Check: WebView2 runtime present (unless we are bundling it) ---------
  // If --bundle-runtime was passed, bundle.py emits the bootstrapper line in
  // [Setup] and we skip this check (the bootstrapper runs before the host).
  {{WEBVIEW2_CHECK_GUARD_BEGIN}}
  // {F3017226-FE2A-4295-8BDF-00C3A9C29AB5} is the official Evergreen
  // WebView2 Runtime client ID. Stable channel, machine or user scope.
  EdgeKey := 'Software\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9C29AB5}';
  WebView2Installed := RegKeyExists(HKLM, EdgeKey) or RegKeyExists(HKCU, EdgeKey);
  if not WebView2Installed then begin
    if MsgBox('The Microsoft WebView2 Runtime was not detected. {#MyAppName} needs it to run.' + #13#10 + #13#10 +
              'Do you want to download and install it now from Microsoft?',
              mbConfirmation, MB_YESNO) = IDYES then begin
      // ShellExecute the official Evergreen bootstrapper URL. The user must
      // complete this before continuing.
      ShellExec('open', 'https://go.microsoft.com/fwlink/p/?LinkId=2124703', '', '',
                SW_SHOW, ewWaitUntilTerminated, ResultCode);
      // Re-check after install
      WebView2Installed := RegKeyExists(HKLM, EdgeKey) or RegKeyExists(HKCU, EdgeKey);
      if not WebView2Installed then begin
        Result := 'WebView2 Runtime installation could not be verified. Please install it manually from https://developer.microsoft.com/microsoft-edge/webview2/ and re-run this installer.';
        Exit;
      end;
    end else begin
      Result := 'WebView2 Runtime is required. Installation aborted.';
      Exit;
    end;
  end;
  {{WEBVIEW2_CHECK_GUARD_END}}
end;

// ---- Install step: write a marker file so the host knows it was installed --
procedure CurStepChanged(CurStep: TSetupStep);
var
  MarkerFile: String;
begin
  if CurStep = ssPostInstall then begin
    MarkerFile := ExpandConstant('{app}\.td-installed');
    SaveStringToFile(MarkerFile,
      'app={{#MyAppName}}' + #13#10 +
      'version={{#MyAppVersion}}' + #13#10 +
      'installed_at=' + GetDateTimeString('yyyy-mm-dd hh:nn:ss', '-', ':') + #13#10 +
      'install_path=' + ExpandConstant('{app}') + #13#10,
      False);
  end;
end;

// ---- Uninstall step: optional save data cleanup ----------------------------
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  SaveDir: String;
  SettingsDir: String;
  FindRec: TFindRec;
  HasOtherFiles: Boolean;
begin
  if CurUninstallStep = usPostUninstall then begin
    // Per-user save data lives in %APPDATA%\{AppPublisher}\{AppName}
    SaveDir := ExpandConstant('{userappdata}\') + '{#MyAppPublisher}' + '\' + '{#MyAppName}' + '\saves';
    SettingsDir := ExpandConstant('{userappdata}\') + '{#MyAppPublisher}' + '\' + '{#MyAppName}';

    if DirExists(SaveDir) then begin
      if MsgBox('Remove your save files and settings for {#MyAppName}?' + #13#10 +
                'This cannot be undone.',
                mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then begin
        DelTree(SaveDir, True, True, True);
        // Also remove the settings dir if it's now empty (no leftover config files)
        if DirExists(SettingsDir) then begin
          HasOtherFiles := False;
          if FindFirst(SettingsDir + '\*', FindRec) then begin
            try
              repeat
                if (FindRec.Name <> '.') and (FindRec.Name <> '..') then begin
                  HasOtherFiles := True;
                  Break;
                end;
              until not FindNext(FindRec);
            finally
              FindClose(FindRec);
            end;
          end;
          if not HasOtherFiles then
            RemoveDir(SettingsDir);
        end;
      end;
    end;
  end;
end;
