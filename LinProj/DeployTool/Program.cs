// =============================================================================
// DeployTool — 建置＋部署用 CLI，非玩家執行檔
// -----------------------------------------------------------------------------
// 取代 deploy.ps1／deploy.bat：把 Encoder／LinLauncher publish 到
// EncoderForPartners\EncoderTool\／Core\，並同步 config.dat、DEPLOY_REPORT.md。
//
// 存在的必要理由：Encoder.exe 沒辦法安全地重新建置＋覆蓋自己正在執行中的檔案，
// 所以「重新建置 Encoder」這個動作要交給一個獨立於 Encoder.exe 之外的 exe 來做——
// Encoder 側按下「重新建置」時會啟動這支工具、然後結束自己的行程。
//
// 用法：
//   DeployTool.exe                     建置 Encoder + LinLauncher（預設，等同原 deploy.ps1）
//   DeployTool.exe --target encoder    只建置 Encoder
//   DeployTool.exe --target launcher   只建置 LinLauncher
//   DeployTool.exe --relaunch-encoder  完成後重新啟動 EncoderTool\LinEncoder.exe
//   DeployTool.exe --root <path>       手動指定 repo 根目錄（找不到時的備援）
//
// 不處理 LauncherDll（C++）：dotnet publish 管不到，維持手動用 Visual Studio 編譯＋
// 手動複製進 Core\ 這個既有限制。
// =============================================================================

using System.Diagnostics;

string target = "all";
bool relaunchEncoder = false;
string? rootOverride = null;

for (int i = 0; i < args.Length; i++)
{
    switch (args[i])
    {
        case "--target" when i + 1 < args.Length:
            target = args[++i].ToLowerInvariant();
            break;
        case "--relaunch-encoder":
            relaunchEncoder = true;
            break;
        case "--root" when i + 1 < args.Length:
            rootOverride = args[++i];
            break;
        case "-h":
        case "--help":
            Console.WriteLine("用法: DeployTool [--target all|encoder|launcher] [--relaunch-encoder] [--root <repo根目錄>]");
            return 0;
    }
}

// 宣告在 try 外面，因為 WriteDeployReport() 這個 local function 是在 catch 之後才定義，
// 需要能讀到這些值——變數宣告在 try{} 裡面的話，try 外面（含 catch 後面的 local function）看不到。
string partnerDir = "";
string coreDir = "";
string encoderToolDir = "";
bool buildEncoder = false;
bool buildLauncher = false;
string? configDatStatus = null;

try
{

string root = rootOverride ?? FindRepoRoot();
string encoderProj = Path.Combine(root, "LinProj", "Encoder", "Encoder.csproj");
string launcherProj = Path.Combine(root, "LinProj", "LinLauncher", "LinLauncher.csproj");
partnerDir = Path.Combine(root, "EncoderForPartners");
coreDir = Path.Combine(partnerDir, "Core");
encoderToolDir = Path.Combine(partnerDir, "EncoderTool");
string iconPath = Path.Combine(root, "LinProj", "LinLauncher", "Assets", "LinLauncher.ico");

string[] devOnlyLogFiles = { "launcher.log", "LinLauncher_boot.log", "LinLauncher_cwd.log", "LinLauncher_errors.log" };

buildEncoder = target is "all" or "encoder";
buildLauncher = target is "all" or "launcher";

Console.WriteLine($">>> Repo root: {root}");
Console.WriteLine($">>> Target: {target}");

Console.WriteLine(">>> [1] Stopping running processes...");
KillProcess("Encoder");
KillProcess("LinEncoder");
KillProcess("LinLauncher");
Thread.Sleep(1000);

Console.WriteLine(">>> [2] Restoring solution...");
int rc = RunProcess("dotnet", $"restore \"{Path.Combine(root, "LinProj", "LinProj381.sln")}\"", root);
if (rc != 0) return Fail("dotnet restore (solution) failed", rc);

if (buildEncoder)
{
    Console.WriteLine(">>> [3] Publishing Encoder...");
    string encoderPublish = Path.Combine(root, "LinProj", "Encoder", "publish");
    if (Directory.Exists(encoderPublish)) Directory.Delete(encoderPublish, true);

    rc = RunProcess("dotnet", $"restore \"{Path.Combine(root, "LinProj", "LinLauncher.Proxy", "LinLauncher.Proxy.csproj")}\" -r win-x86", root);
    if (rc != 0) return Fail("dotnet restore (LinLauncher.Proxy) failed", rc);

    rc = RunProcess("dotnet", $"publish \"{encoderProj}\" -c Release -r win-x86 --no-restore --self-contained true -p:PublishSingleFile=true -o \"{encoderPublish}\"", root);
    if (rc != 0) return Fail("dotnet publish (Encoder) failed", rc);

    Directory.CreateDirectory(encoderToolDir);
    CopyDirectory(encoderPublish, encoderToolDir);

    // 舊命名清理，避免混淆
    TryDelete(Path.Combine(encoderToolDir, "encoder.exe"));
    TryDelete(Path.Combine(encoderToolDir, "encoder.pdb"));
    if (File.Exists(iconPath))
        File.Copy(iconPath, Path.Combine(encoderToolDir, "LinLauncher.ico"), true);

    // 從 LinEncoder.ini 重新產生 config.dat，避免沿用舊檔造成金鑰／內容不符
    string iniForConfig = Path.Combine(encoderToolDir, "LinEncoder.ini");
    if (File.Exists(iniForConfig))
    {
        string configDatGenProj = Path.Combine(root, "LinProj", "ConfigDatGen", "ConfigDatGen.csproj");
        string outConfigDat = Path.Combine(coreDir, "config.dat");
        rc = RunProcess("dotnet", $"run --project \"{configDatGenProj}\" -c Release -r win-x86 -- --ini \"{iniForConfig}\" --out \"{outConfigDat}\"", root);
        if (rc != 0) return Fail("ConfigDatGen failed", rc);
        configDatStatus = $"已從 {iniForConfig} 重新產生 {outConfigDat}";
    }
    else
    {
        Console.WriteLine($"    [!] 略過 config.dat：找不到 {iniForConfig}");
        configDatStatus = $"略過（找不到 {iniForConfig}）";
    }
}

if (buildLauncher)
{
    Console.WriteLine(">>> [4] Publishing LinLauncher...");
    string launcherPublish = Path.Combine(root, "LinProj", "LinLauncher", "publish");
    if (Directory.Exists(launcherPublish)) Directory.Delete(launcherPublish, true);

    rc = RunProcess("dotnet", $"publish \"{launcherProj}\" -c Release -r win-x86 --no-restore --self-contained true -o \"{launcherPublish}\"", root);
    if (rc != 0) return Fail("dotnet publish (LinLauncher) failed", rc);

    Directory.CreateDirectory(coreDir);
    CopyDirectory(launcherPublish, coreDir);

    foreach (string devLog in devOnlyLogFiles)
        TryDelete(Path.Combine(coreDir, devLog));
}

Console.WriteLine(">>> [5] Ensuring login\\ folder exists...");
string loginDir = Path.Combine(partnerDir, "login");
Directory.CreateDirectory(loginDir);
string strayList = Path.Combine(partnerDir, "list.txt");
if (File.Exists(strayList))
    File.Move(strayList, Path.Combine(loginDir, "list.txt"), true);

Console.WriteLine(">>> [6] Writing DEPLOY_REPORT.md...");
WriteDeployReport();

if (relaunchEncoder)
{
    string encoderExe = Path.Combine(encoderToolDir, "LinEncoder.exe");
    if (File.Exists(encoderExe))
    {
        Console.WriteLine(">>> Relaunching LinEncoder.exe...");
        Process.Start(new ProcessStartInfo(encoderExe) { WorkingDirectory = encoderToolDir, UseShellExecute = true });
    }
}

Console.WriteLine("\n✅ Deploy finished.");
Console.WriteLine($"Target: {partnerDir}");
Pause();
return 0;

}
catch (Exception ex)
{
    // 任何沒被個別 Fail() 攔到的例外（例如 FindRepoRoot 找不到目錄）都要走這裡才會 Pause()，
    // 不然視窗會在開發者看清楚錯誤訊息之前就自動關掉。
    return Fail($"未預期的錯誤：{ex.Message}", 1);
}

// ---------------------------------------------------------------------------

string FindRepoRoot()
{
    string dir = AppContext.BaseDirectory;
    for (int depth = 0; depth < 12 && !string.IsNullOrEmpty(dir); depth++)
    {
        if (File.Exists(Path.Combine(dir, "deploy.ps1")) && Directory.Exists(Path.Combine(dir, "LinProj")))
            return dir;
        string? parent = Path.GetDirectoryName(dir.TrimEnd(Path.DirectorySeparatorChar));
        if (parent == dir) break;
        dir = parent ?? "";
    }
    throw new DirectoryNotFoundException("找不到 repo 根目錄（找不到含 deploy.ps1 與 LinProj\\ 的上層目錄），請用 --root 手動指定。");
}

void KillProcess(string name)
{
    foreach (var p in Process.GetProcessesByName(name))
    {
        try { p.Kill(true); p.WaitForExit(5000); }
        catch { /* best effort */ }
    }
}

int RunProcess(string exe, string arguments, string workingDir)
{
    Console.WriteLine($"    $ {exe} {arguments}");
    var psi = new ProcessStartInfo(exe, arguments)
    {
        WorkingDirectory = workingDir,
        UseShellExecute = false,
    };
    using var p = Process.Start(psi) ?? throw new InvalidOperationException($"無法啟動: {exe}");
    p.WaitForExit();
    return p.ExitCode;
}

void CopyDirectory(string sourceDir, string destDir)
{
    Directory.CreateDirectory(destDir);
    foreach (string dir in Directory.GetDirectories(sourceDir, "*", SearchOption.AllDirectories))
        Directory.CreateDirectory(dir.Replace(sourceDir, destDir));
    foreach (string file in Directory.GetFiles(sourceDir, "*", SearchOption.AllDirectories))
        File.Copy(file, file.Replace(sourceDir, destDir), true);
}

void TryDelete(string path)
{
    try { if (File.Exists(path)) File.Delete(path); } catch { /* best effort */ }
}

int Fail(string message, int exitCode)
{
    Console.Error.WriteLine($"\n❌ {message} (exit code {exitCode})");
    Pause();
    return exitCode == 0 ? 1 : exitCode;
}

// 開發者不管手動跑還是從 Encoder 的「重新建置」觸發，都要能在視窗關掉前看到完整結果
// （尤其是失敗訊息）——跑完不自動關窗，等按鍵才關，讓視窗留著對照查看。
void Pause()
{
    Console.WriteLine("\n按任意鍵關閉視窗...");
    try { Console.ReadKey(true); } catch { /* 非互動環境（例如背景執行）就直接略過 */ }
}

void WriteDeployReport()
{
    string reportPath = Path.Combine(partnerDir, "DEPLOY_REPORT.md");
    var sb = new System.Text.StringBuilder();
    sb.AppendLine($"## {DateTime.Now:yyyy-MM-dd HH:mm:ss}");
    sb.AppendLine();
    sb.AppendLine("### 這次做了什麼");
    sb.AppendLine();
    if (buildEncoder)
        sb.AppendLine("- Encoder 已重新建置並複製到 `EncoderTool\\`");
    if (buildLauncher)
        sb.AppendLine("- LinLauncher 已重新建置（自封裝）並複製到 `Core\\`（已清除開發期診斷/紀錄檔）");
    if (configDatStatus != null)
        sb.AppendLine($"- config.dat：{configDatStatus}");
    sb.AppendLine();
    sb.AppendLine("### 接下來要做什麼");
    sb.AppendLine();
    sb.AppendLine("1. 用 `EncoderTool\\LinEncoder.exe` 確認／重新產生 `login\\list.txt` 與登入殼（{名稱}.exe）");
    sb.AppendLine("2. 確認 `addon\\update\\` 內容是最新的；如有新素材，先在 `addon\\add\\` 準備好，再用「補丁產生工具」重新打包");
    sb.AppendLine("3. 把 `login\\`、`addon\\update\\`、殼 exe＋`Core\\` 上傳／複製到對應位置");
    sb.AppendLine("4. 實際測試登入流程");
    sb.AppendLine();
    sb.AppendLine("---");
    sb.AppendLine();

    if (!File.Exists(reportPath))
        File.WriteAllText(reportPath, "# 部署報告\n\n", System.Text.Encoding.UTF8);
    File.AppendAllText(reportPath, sb.ToString(), System.Text.Encoding.UTF8);
}
