; 天堂 381 懶人包（Inno Setup 7）
; 換包來源時只改 SourceDir。編譯產出在 installer\output\
;
; 本機編譯器：D:\程式碼測試區\其他\Inno Setup 7\ISCC.exe

#define SourceDir "D:\天堂資料\天堂專案#380客戶端+自製登入器"
#define MyAppName "天堂381"
#define MyAppVersion "1.0"
#define MyAppPublisher "Lineage381"
#define MyAppExeName "LinLauncher.exe"

[Setup]
AppId={{8C3E1A72-9B4D-4F61-A2C8-7D5E0F1B3A94}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={sd}\天堂381
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=no
OutputDir=output
OutputBaseFilename=天堂(Lineage 3.81C)
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x86 x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
SetupLogging=yes
InfoAfterFile=dotnet-note.txt
; 卸載不碰玩家後來自己產生的 *.loc
UninstallDisplayIcon={app}\Core\{#MyAppExeName}

[Languages]
Name: "chinesetraditional"; MessagesFile: "compiler:Languages\ChineseTraditional.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; 建議排除（預設開啟）：打包工具、吃檔散檔目錄。若要打進包，刪掉下列 Excludes 對應項即可。
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; \
    Excludes: "*.loc,*.log,*.pdb,*.tmp,.eat_pending,*.pending,createdump.exe,eat.exe,EatPack.exe,log\*,lineage381.exe.WebView2\*,Core\LinLauncher.exe.WebView2\*,icon\*,sprite\*,Surf\*,text\*,Tile\*"

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\Core\{#MyAppExeName}"; WorkingDir: "{app}\Core"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\Core\{#MyAppExeName}"; WorkingDir: "{app}\Core"; Tasks: desktopicon

[Run]
Filename: "{app}\Core\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent shellexec
