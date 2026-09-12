; 天堂 381 懶人包（Inno Setup 7）
; 換包來源時只改 SourceDir。編譯產出在 installer\output\
;
; 本機編譯器：D:\程式碼測試區\其他\Inno Setup 7\ISCC.exe

#define SourceDir "D:\天堂資料\天堂專案#380客戶端+自製登入器"
#define MyAppName "天堂381"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Lineage381"
#define MyAppExeName "LinLauncher.exe"

[Setup]
AppId={{8C3E1A72-9B4D-4F61-A2C8-7D5E0F1B3A94}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={sd}C:\Program Files
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=no
OutputDir=output
OutputBaseFilename=天堂(Lineage 3.81C)_{#MyAppVersion}
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

; === 磁碟分割設定 ===
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
; 建議排除（預設開啟）：打包工具、吃檔散檔目錄。若要打進包，刪掉下列 Excludes 對應項即可。
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; \
    Excludes: "*.loc,*.log,*.pdb,*.tmp,.eat_pending,*.pending,createdump.exe,*.dmp,eat.exe,EatPack.exe,log\*,Capture\*,lineage381.exe.WebView2\*,Core\LinLauncher.exe.WebView2\*,icon\*,sprite\*,Surf\*,text\*,Tile\*"

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\Core\{#MyAppExeName}"; WorkingDir: "{app}\Core"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\Core\{#MyAppExeName}"; WorkingDir: "{app}\Core"; Tasks: desktopicon

[Run]
Filename: "{app}\Core\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent shellexec
