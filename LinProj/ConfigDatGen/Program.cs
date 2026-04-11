// =============================================================================
// ConfigDatGen — 建置／部署用 CLI，非玩家執行檔
// -----------------------------------------------------------------------------
// 作用：
//   從 Encoder 夥伴設定的 LinEncoder.ini 之 [LauncherMaker] 區段，產生與目前
//   LinLauncher 程式碼相容的二進位 config.dat（經 ConfigDatWriter 加密）。
//
// 功能流程：
//   1) 讀取 INI → 填入 LauncherConfig（標題、版本、網址、視窗大小、更新開關、五組連結等）
//   2) 呼叫 ConfigDatWriter.BuildEncryptedFile → 寫出檔案
//   3) 立即用 ConfigDatCodec.TryDecrypt 驗證（與登入器載入路徑一致），失敗則 exit 4
//
// 典型用法（亦見專案根 deploy.ps1）：
//   dotnet run --project ConfigDatGen.csproj -- --ini EncoderForPartners\LinEncoder.ini --out ...\config.dat
//
// 依賴：LinLauncher 專案中的 Models.LauncherConfig、Services.ConfigDatWriter / ConfigDatCodec
// =============================================================================

using System.Runtime.InteropServices;
using System.Text;
using LinLauncher.Models;
using LinLauncher.Services;

if (args.Any(a => a is "-h" or "--help"))
{
    Console.WriteLine("用法: ConfigDatGen --ini <LinEncoder.ini> --out <config.dat>");
    Console.WriteLine("從 [LauncherMaker] 讀取欄位，產生與目前 LinLauncher 相容的加密 config.dat。");
    Console.WriteLine("      ConfigDatGen --self-test   驗證加解密與 Marshal 往返（不寫檔）。");
    return;
}

// --self-test：不讀 INI、不寫檔；僅驗證「加密 → 解密」與欄位往返，供 CI 或發版前自檢
if (args.Any(a => a == "--self-test"))
{
    RunSelfTest();
    return;
}

string? iniPath = null;
string? outPath = null;
for (int i = 0; i < args.Length; i++)
{
    if (args[i] == "--ini" && i + 1 < args.Length) { iniPath = args[++i]; continue; }
    if (args[i] == "--out" && i + 1 < args.Length) { outPath = args[++i]; continue; }
}

if (string.IsNullOrEmpty(iniPath) || string.IsNullOrEmpty(outPath))
{
    Console.Error.WriteLine("請指定 --ini 與 --out。加 --help 查看說明。");
    Environment.Exit(1);
}

if (!File.Exists(iniPath))
{
    Console.Error.WriteLine($"找不到 INI：{iniPath}");
    Environment.Exit(2);
}

// 簡易 INI 解析（UTF-8）；僅支援 [區段] 與 key=value，與 Encoder 產出之 LinEncoder.ini 對齊
var ini = ReadIni(iniPath!);
if (!ini.TryGetValue("LauncherMaker", out var lm))
{
    Console.Error.WriteLine("INI 缺少 [LauncherMaker] 區段。");
    Environment.Exit(3);
}

// LauncherConfig 為 blittable 結構，欄位順序／大小須與 C++/登入器共用定義一致
var cfg = new LauncherConfig
{
    Title = Get(lm, "title", "Lineage Launcher"),
    Ver = Get(lm, "ver", "1001"),
    Web = Get(lm, "web", "http://www.google.com/"),
    List = Get(lm, "list", "http://www.google.com/"),
    UseUpdate = GetBool(lm, "enable_update", false),
    Update = Get(lm, "update", ""),
    Helper = Get(lm, "helper", ""),
    Width = GetInt(lm, "width", 1000),
    Height = GetInt(lm, "height", 600),
};

// 五組「自訂連結」：名稱／網址以固定長度 Unicode 位元組寫入 Raw 緩衝（與舊版登入器版面相容）
for (int i = 0; i < 5; i++)
{
    int n = i + 1;
    cfg.UseLink[i] = GetBool(lm, $"link_enable{n}", false);
    string name = Get(lm, $"link_name{n}", "");
    string url = Get(lm, $"link_url{n}", "");
    byte[] nameBytes = Encoding.Unicode.GetBytes(name.PadRight(16, '\0').Substring(0, 16));
    byte[] urlBytes = Encoding.Unicode.GetBytes(url.PadRight(256, '\0').Substring(0, 256));
    Array.Copy(nameBytes, 0, cfg.LinkNamesRaw, i * 32, 32);
    Array.Copy(urlBytes, 0, cfg.LinkUrlsRaw, i * 512, 512);
}

byte[] fileBytes = ConfigDatWriter.BuildEncryptedFile(cfg);
Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(outPath!))!);
File.WriteAllBytes(outPath!, fileBytes);

// 產檔後立刻用與 LinLauncher 相同的解密入口驗證，避免金鑰／長度不符卻仍寫出損壞檔案
var verify = ConfigDatCodec.TryDecrypt(fileBytes);
if (verify == null || !verify.Configed)
{
    Console.Error.WriteLine("錯誤：產生的 config.dat 無法由 ConfigDatCodec 還原（與登入器載入邏輯相同）。");
    Environment.Exit(4);
}

Console.WriteLine($"已寫入 config.dat（{fileBytes.Length} bytes），結構大小 {Marshal.SizeOf(typeof(LauncherConfig))}，已通過還原驗證。");
Console.WriteLine(outPath);

static string Get(Dictionary<string, string> d, string key, string def) =>
    d.TryGetValue(key, out var v) ? v : def;

// INI 布林常見寫法：true/false、1/0
static bool GetBool(Dictionary<string, string> d, string key, bool def)
{
    if (!d.TryGetValue(key, out var v)) return def;
    if (bool.TryParse(v, out var b)) return b;
    if (v == "1") return true;
    if (v == "0") return false;
    return def;
}

static int GetInt(Dictionary<string, string> d, string key, int def)
{
    if (!d.TryGetValue(key, out var v)) return def;
    return int.TryParse(v, out var n) ? n : def;
}

/// <summary>讀取 UTF-8 INI：略過空行與 ; # 註解，區段名大小寫不敏感。</summary>
static Dictionary<string, Dictionary<string, string>> ReadIni(string path)
{
    var result = new Dictionary<string, Dictionary<string, string>>(StringComparer.OrdinalIgnoreCase);
    string section = "";
    foreach (var line in File.ReadAllLines(path, Encoding.UTF8))
    {
        var t = line.Trim();
        if (t.Length == 0 || t.StartsWith(';') || t.StartsWith('#')) continue;
        if (t.StartsWith('[') && t.EndsWith(']'))
        {
            section = t.Substring(1, t.Length - 2).Trim();
            if (!result.ContainsKey(section))
                result[section] = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            continue;
        }
        int eq = t.IndexOf('=');
        if (eq <= 0 || string.IsNullOrEmpty(section)) continue;
        string k = t.Substring(0, eq).Trim();
        string v = t.Substring(eq + 1).Trim();
        result[section][k] = v;
    }
    return result;
}

/// <summary>
/// 自測：建立最小 LauncherConfig → 加密 → 解密，比對關鍵字串欄位。
/// 用於確認 ConfigDatWriter 與 ConfigDatCodec 未因重構而漂移。
/// </summary>
static void RunSelfTest()
{
    var cfg = new LauncherConfig
    {
        Title = "SelfTest",
        Web = "http://example.com/",
        List = "http://example.com/list.txt",
        UseUpdate = true,
        Update = "http://example.com/",
        Configed = true
    };
    byte[] bytes = ConfigDatWriter.BuildEncryptedFile(cfg);
    var loaded = ConfigDatCodec.TryDecrypt(bytes);
    bool ok = loaded != null
        && loaded.Configed
        && loaded.Web == cfg.Web
        && loaded.List == cfg.List
        && loaded.Update == cfg.Update;
    Console.WriteLine(ok ? "PASS: ConfigDatWriter + ConfigDatCodec 往返一致。" : "FAIL: 往返後字串不符。");
    if (!ok && loaded != null)
    {
        Console.WriteLine($"  預期 Web={cfg.Web} 實際={loaded.Web}");
        Console.WriteLine($"  預期 List={cfg.List} 實際={loaded.List}");
    }
    Environment.Exit(ok ? 0 : 1);
}
