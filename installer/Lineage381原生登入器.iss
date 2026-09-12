; 天堂 381 測試懶人包（Inno Setup 7）— 原廠 Login.exe，無自製 Core\LinLauncher
; 換包來源時只改 SourceDir。編譯產出在 installer\output\
;
; 本機編譯器：D:\程式碼測試區\其他\Inno Setup 7\ISCC.exe
;
; 自製登入器版請用 Lineage381.iss

#define SourceDir "D:\天堂資料\3.81測試懶人包"
#define MyAppName "天堂(Lineage 3.81C)"
#define MyDate GetDateTimeString('yyyyMMdd', '', '')
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Lineage381"
#define MyAppExeName "Login.exe"

[Setup]
AppId={{8C3E1A72-9B4D-4F61-A2C8-7D5E0F1B3A94}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={sd}C:\Program Files
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=no
OutputDir=output
OutputBaseFilename=天堂(Lineage 3.81C) {#MyDate}
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x86 x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
SetupLogging=yes
InfoAfterFile=dotnet-note.txt

; === 自訂安裝檔圖示 (請確保資料夾內有 logo.ico，若無可先註解掉此行) ===
; SetupIconFile=logo.ico
UninstallDisplayIcon={app}\{#MyAppExeName}

; === 磁碟分割與防毒安全設定 ===
DiskSpanning=yes
DiskSliceSize=max

[Languages]
Name: "chinesetraditional"; MessagesFile: "compiler:Languages\ChineseTraditional.isl"

; === 自訂安裝過程文字 ===
[Messages]
chinesetraditional.WelcomeLabel1=歡迎安裝 {#MyAppName}
chinesetraditional.WelcomeLabel2=即將開始安裝Lineage 3.81C.exe。%n%n建議安裝前先暫時關閉防毒軟體即時防護，以免遊戲核心檔案被誤判阻擋。
chinesetraditional.ClickFinish=點選 [完成] 即可關閉安裝程式。

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; 建議排除：暫存、吃檔散檔、擷圖目錄。根目錄 Sprite*/Text/Tile pak/idx 仍會打進包。
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; \
    Excludes: "*.loc,*.log,*.pdb,*.tmp,.eat_pending,*.pending,dmp,createdump.exe,EatPack.exe,log\*,Capture\*,icon\*,sprite\*,Surf\*,text\*,Tile\*"

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent shellexec