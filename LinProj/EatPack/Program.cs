using System.Text;
using Lin.Helper.Core.Pak;

namespace EatPack;

/// <summary>
/// 獨立命令列吃檔工具（對應客戶端原廠 eat.exe / eat.dll）。
/// 把散檔寫進 Sprite / Text / Tile 的 idx/pak，以及自製 UI 的 ui.pak/ui.idx。
/// 登入器內建同邏輯見 <c>LinLauncher/Services/EatService.cs</c>，兩邊請保持對齊。
/// </summary>
internal static class Program
{
    // idx 檔名欄位固定 20 bytes（Latin1），超過會截斷，與原廠 eat 行為相同。
    // 例：orcfhuwoomoscroll-c.html → idx 裡變成 orcfhuwoomoscroll-c.
    private const int IdxNameBytes = 20;

    // Sprite 分卷軟上限：接近此大小就改寫下一個還有空間的 SpriteNN.pak，
    // 預留 4KB 緩衝，避免剛好踩到檔案系統／遊戲讀取的硬上限。
    private const long PakSoftLimit = 1_900_000_000L;

    // 資料夾名稱（不分大小寫）→ 允許的副檔名白名單。
    // icon / sprite / Surf 最終都進 Sprite 系列 idx；text→Text.idx；tile→Tile.idx。
    private static readonly Dictionary<string, string[]> FolderExts = new(StringComparer.OrdinalIgnoreCase)
    {
        ["icon"] = [".tbt", ".ico"],
        ["sprite"] = [".spr", ".png"],
        ["surf"] = [".img"],
        ["text"] = [".html", ".tbl", ".txt", ".spr", ".spz"],
        ["tile"] = [".til", ".xml"],
    };

    public static int Main(string[] args)
    {
        // PakFile / 舊檔名可能含 Big5 等非 UTF-8 編碼，先註冊 code page provider。
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);

        var clientRoot = Directory.GetCurrentDirectory();
        var dryRun = args.Any(a => a is "-n" or "--dry-run");
        var keep = args.Any(a => a is "-k" or "--keep");

        if (args.Any(a => a is "-h" or "--help"))
        {
            PrintHelp();
            return 0;
        }

        // -d / --dir 指定客戶端根目錄（應含 Sprite.idx、Text.idx、Tile.idx）。
        for (var i = 0; i < args.Length; i++)
        {
            if (args[i] is "-d" or "--dir" && i + 1 < args.Length)
            {
                clientRoot = Path.GetFullPath(args[++i]);
            }
        }

        Console.WriteLine($"EatPack 客戶端目錄: {clientRoot}");
        if (dryRun)
        {
            Console.WriteLine("（預覽模式，不會寫入 idx/pak）");
        }

        // ---- 輔助子命令：從既有 idx/pak 匯出單一檔 ----
        if (args.Any(a => a is "--extract"))
        {
            var rest = args.SkipWhile(a => a != "--extract").Skip(1).ToArray();
            if (rest.Length < 3)
            {
                Console.WriteLine("用法: EatPack --extract <idx檔> <檔名> <輸出路徑>");
                return 1;
            }

            using var pak = new PakFile(Path.IsPathRooted(rest[0]) ? rest[0] : Path.Combine(clientRoot, rest[0]));
            // idx 內可能是截斷後的名稱，先用 SameIdxName 找真實存檔名再 Extract。
            var name = FindStoredName(pak, rest[1]) ?? rest[1];
            var data = pak.Extract(name);
            File.WriteAllBytes(rest[2], data);
            Console.WriteLine($"已匯出 {name} ({data.Length} bytes) -> {rest[2]}");
            return 0;
        }

        // ---- 輔助子命令：列出 Text.idx 內檔名（可帶關鍵字過濾）----
        if (args.Any(a => a is "--list"))
        {
            var filter = args.SkipWhile(a => a != "--list").Skip(1).FirstOrDefault() ?? "";
            using var pak = new PakFile(Path.Combine(clientRoot, "Text.idx"));
            Console.WriteLine($"Text.idx count={pak.Count} enc={pak.EncryptionType}");
            foreach (var f in pak.Files.Where(f =>
                         filter.Length == 0 || f.FileName.Contains(filter, StringComparison.OrdinalIgnoreCase)))
            {
                Console.WriteLine($"{f.FileName}\t{f.FileName.Length} chars");
            }
            return 0;
        }

        // ---- 主流程：收集散檔 → 寫入對應 idx/pak →（可選）刪散檔 ----
        var jobs = CollectJobs(clientRoot);
        var uiLooseFiles = CollectUiLooseFiles(clientRoot);
        if (jobs.Count == 0 && uiLooseFiles.Count == 0)
        {
            Console.WriteLine("沒有找到可吃檔的散檔。請放到 icon / sprite / Surf / text / Tile / ui。");
            return 0;
        }

        Console.WriteLine($"待處理 {jobs.Count + uiLooseFiles.Count} 個檔案。");

        // 一次開好所有可能寫入的 pak，迴圈內只 Resolve / Add / Replace，最後統一 Save。
        var spritePaks = OpenSpritePaks(clientRoot);
        var textPak = OpenOptional(Path.Combine(clientRoot, "Text.idx"));
        var tilePak = OpenOptional(Path.Combine(clientRoot, "Tile.idx"));

        var updated = 0;
        var added = 0;
        var failed = 0;

        try
        {
            foreach (var job in jobs)
            {
                try
                {
                    var data = File.ReadAllBytes(job.FullPath);
                    var target = ResolveTarget(job, spritePaks, textPak, tilePak);
                    if (target is null)
                    {
                        // Text/Tile idx 不存在，或 Sprite 系列一個都打不開。
                        Console.WriteLine($"[略過] {job.ArchiveName} 找不到對應 idx: {job.FileName}");
                        failed++;
                        continue;
                    }

                    // 用截斷後的 20-byte key 比對；回傳的是 idx 裡「實際存的」原始檔名字串。
                    var existingName = FindStoredName(target, job.FileName);

                    if (dryRun)
                    {
                        Console.WriteLine(
                            $"[{(existingName is null ? "新增" : "更新")}] {job.Folder}\\{job.FileName}" +
                            $" (idx名稱={IdxKey(job.FileName)}) -> {Path.GetFileName(IdxPathOf(target))}");
                        if (existingName is null)
                        {
                            added++;
                        }
                        else
                        {
                            updated++;
                        }
                        continue;
                    }

                    if (existingName is not null)
                    {
                        // 已存在：資料接到 pak 尾端並改 idx 位移（PakFile.Replace 內部處理）。
                        target.Replace(existingName, data);
                        Console.WriteLine($"[更新] {job.Folder}\\{job.FileName} ({data.Length} bytes) -> {Path.GetFileName(IdxPathOf(target))}");
                        updated++;
                    }
                    else
                    {
                        // 新檔：Sprite 還要依 pak 剩餘容量選分卷，避免單一 SpriteNN.pak 爆掉。
                        if (job.Kind == JobKind.Sprite)
                        {
                            target = PickSpritePakForNewFile(spritePaks, data.Length) ?? target;
                        }

                        // maintainSort: true → 寫入後維持 idx 內檔名排序（遊戲／工具有時依序查找）。
                        target.Add(IdxKey(job.FileName), data, maintainSort: true);
                        Console.WriteLine($"[新增] {job.Folder}\\{job.FileName} ({data.Length} bytes) -> {Path.GetFileName(IdxPathOf(target))}");
                        added++;
                    }

                    // 預設吃完刪散檔；-k/--keep 則保留，方便除錯或重複預覽。
                    if (!keep)
                    {
                        File.Delete(job.FullPath);
                    }
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[失敗] {job.FileName}: {ex.Message}");
                    failed++;
                }
            }

            // 逐檔 Add/Replace 只改記憶體 pending list；真正 I/O 在 Save()。
            if (!dryRun)
            {
                foreach (var pak in spritePaks)
                {
                    pak.Save();
                }
                textPak?.Save();
                tilePak?.Save();
            }
        }
        finally
        {
            // 無論成功失敗都要釋放檔案鎖，否則下一輪吃檔／遊戲開不起來。
            foreach (var pak in spritePaks)
            {
                pak.Dispose();
            }
            textPak?.Dispose();
            tilePak?.Dispose();
        }

        // ui.pak 格式與原生 PakFile 不同，獨立合併（見 MergeUiPak）。
        if (uiLooseFiles.Count > 0)
        {
            var (uiUpdated, uiAdded, uiFailed) = MergeUiPak(clientRoot, uiLooseFiles, dryRun, keep);
            Console.WriteLine($"自製UI（ui.pak）：更新 {uiUpdated}，新增 {uiAdded}，失敗 {uiFailed}");
            updated += uiUpdated;
            added += uiAdded;
            failed += uiFailed;
        }

        Console.WriteLine($"完成：更新 {updated}，新增 {added}，失敗 {failed}。");
        return failed == 0 ? 0 : 1;
    }

    private static void PrintHelp()
    {
        Console.WriteLine("""
            EatPack — 對應 eat.exe 的吃檔工具

            用法:
              EatPack.exe [-d 客戶端目錄] [-n] [-k]

            目錄對應:
              icon\   *.tbt *.ico     → Sprite.idx / Sprite00-15.idx
              sprite\ *.spr *.png     → Sprite.idx / Sprite00-15.idx
              Surf\   *.img           → Sprite.idx / Sprite00-15.idx
              text\   *.html *.tbl *.txt list.spr list.spz → Text.idx
              Tile\   *.til *.xml     → Tile.idx
              ui\     *.*（除 ui.pak/ui.idx）→ ui.pak/ui.idx（自製UI疊圖資源）

            選項:
              -d, --dir       客戶端根目錄（預設為目前目錄）
              -n, --dry-run   只預覽，不寫入
              -k, --keep      吃檔後保留散檔
              --list [關鍵字] 列出 Text.idx 檔名
            """);
    }

    /// <summary>
    /// 把檔名壓成 idx 用的 20-byte Latin1 key（超過截斷、尾端 NUL 去掉）。
    /// </summary>
    private static string IdxKey(string fileName)
    {
        var bytes = Encoding.Latin1.GetBytes(fileName);
        if (bytes.Length > IdxNameBytes)
        {
            bytes = bytes[..IdxNameBytes];
        }
        return Encoding.Latin1.GetString(bytes).TrimEnd('\0');
    }

    /// <summary>
    /// 比對兩個檔名在 idx 語意上是否相同（都先過 IdxKey，再忽略大小寫）。
    /// </summary>
    private static bool SameIdxName(string a, string b)
    {
        return string.Equals(IdxKey(a), IdxKey(b), StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>
    /// 在 pak 裡找「與 fileName 對得上的實際存檔名」；找不到回傳 null。
    /// Replace 必須用 idx 裡那份原始字串，不能用呼叫端傳進來的散檔名。
    /// </summary>
    private static string? FindStoredName(PakFile pak, string fileName)
    {
        foreach (var f in pak.Files)
        {
            if (SameIdxName(f.FileName, fileName))
            {
                return f.FileName;
            }
        }
        return null;
    }

    /// <summary>
    /// 反射讀 PakFile 的路徑屬性（不同版本可能叫 IdxPath / IndexPath / Path / FilePath）。
    /// 僅供 log 顯示用。
    /// </summary>
    private static string IdxPathOf(PakFile pak)
    {
        foreach (var name in new[] { "IdxPath", "IndexPath", "Path", "FilePath" })
        {
            var prop = pak.GetType().GetProperty(name);
            var val = prop?.GetValue(pak)?.ToString();
            if (!string.IsNullOrEmpty(val))
            {
                return val;
            }
        }
        return "(idx)";
    }

    private enum JobKind
    {
        Sprite,
        Text,
        Tile,
    }

    /// <param name="FullPath">散檔完整路徑</param>
    /// <param name="Folder">來源資料夾顯示名（icon/sprite/...）</param>
    /// <param name="FileName">檔名（含副檔名）</param>
    /// <param name="Kind">決定寫入哪一類 idx</param>
    /// <param name="ArchiveName">log 用的封存名稱（Sprite/Text/Tile）</param>
    private sealed record Job(string FullPath, string Folder, string FileName, JobKind Kind, string ArchiveName);

    /// <summary>
    /// 掃描客戶端根目錄下 icon/sprite/Surf/text/Tile，依副檔名白名單收集待吃散檔。
    /// list.spr / list.spz 雖副檔名像 sprite，但實際屬於 Text.idx，會強制改 Kind.Text。
    /// </summary>
    private static List<Job> CollectJobs(string root)
    {
        var jobs = new List<Job>();
        foreach (var (folder, exts) in FolderExts)
        {
            // 用目錄名不分大小寫比對（客戶端可能是 Surf / SURF / surf）。
            var dir = Directory.GetDirectories(root)
                .FirstOrDefault(d => string.Equals(Path.GetFileName(d), folder, StringComparison.OrdinalIgnoreCase));
            if (dir is null)
            {
                continue;
            }

            foreach (var file in Directory.GetFiles(dir))
            {
                var ext = Path.GetExtension(file);
                if (!exts.Contains(ext, StringComparer.OrdinalIgnoreCase))
                {
                    continue;
                }

                var name = Path.GetFileName(file);
                var kind = folder.Equals("text", StringComparison.OrdinalIgnoreCase)
                    ? JobKind.Text
                    : folder.Equals("tile", StringComparison.OrdinalIgnoreCase)
                        ? JobKind.Tile
                        : JobKind.Sprite;

                // text 白名單含 .spr/.spz 是為了 list.*；一般 sprite 不該進 Text。
                if (name.StartsWith("list.", StringComparison.OrdinalIgnoreCase) &&
                    (name.EndsWith(".spr", StringComparison.OrdinalIgnoreCase) ||
                     name.EndsWith(".spz", StringComparison.OrdinalIgnoreCase)))
                {
                    kind = JobKind.Text;
                }

                var archive = kind switch
                {
                    JobKind.Text => "Text",
                    JobKind.Tile => "Tile",
                    _ => "Sprite",
                };
                jobs.Add(new Job(file, Path.GetFileName(dir), name, kind, archive));
            }
        }

        return jobs;
    }

    /// <summary>
    /// 開啟 Sprite.idx 以及 Sprite00.idx～Sprite15.idx（存在才開）。
    /// 順序固定：主卷在前、分卷依編號，PickSpritePakForNewFile 會依此順序找空間。
    /// </summary>
    private static List<PakFile> OpenSpritePaks(string root)
    {
        var list = new List<PakFile>();
        var main = Path.Combine(root, "Sprite.idx");
        if (File.Exists(main))
        {
            list.Add(new PakFile(main));
        }

        for (var i = 0; i <= 15; i++)
        {
            var idx = Path.Combine(root, $"Sprite{i:00}.idx");
            if (File.Exists(idx))
            {
                list.Add(new PakFile(idx));
            }
        }

        return list;
    }

    /// <summary>idx 存在才開啟，否則回傳 null（該類型這次就無法寫入）。</summary>
    private static PakFile? OpenOptional(string idxPath)
    {
        return File.Exists(idxPath) ? new PakFile(idxPath) : null;
    }

    /// <summary>
    /// 依 JobKind 選目標 pak。
    /// Sprite：優先選「已經有同名 entry」的那一卷（更新時必須寫回原卷），
    /// 都沒有才暫用清單第一個（真正新增時會再經 PickSpritePakForNewFile 重選）。
    /// </summary>
    private static PakFile? ResolveTarget(Job job, List<PakFile> sprites, PakFile? text, PakFile? tile)
    {
        return job.Kind switch
        {
            JobKind.Text => text,
            JobKind.Tile => tile,
            _ => sprites.FirstOrDefault(p => FindStoredName(p, job.FileName) is not null)
                 ?? sprites.FirstOrDefault(),
        };
    }

    /// <summary>
    /// 新增 Sprite 時選還有空間的分卷：現有 .pak 大小 + 新檔 + 4KB &lt; PakSoftLimit。
    /// 全部塞不下就退回清單最後一個（仍寫入，由使用者自行處理空間問題）。
    /// </summary>
    private static PakFile? PickSpritePakForNewFile(List<PakFile> sprites, int fileSize)
    {
        foreach (var pak in sprites)
        {
            var idx = IdxPathOf(pak);
            var pakPath = Path.ChangeExtension(idx, ".pak");
            if (!File.Exists(pakPath))
            {
                continue;
            }
            if (new FileInfo(pakPath).Length + fileSize + 4096 < PakSoftLimit)
            {
                return pak;
            }
        }

        return sprites.LastOrDefault();
    }

    // -------------------------------------------------------------------------
    // 自製 UI（ui.pak / ui.idx）
    // 格式見 tools\Pack-UiAssets.ps1 / LauncherDll OverlayAssets.cpp：
    //   - pak：所有 entry raw bytes 依序 concat，再 whole-file XOR
    //   - idx：純文字每行 "name=offset,length"（UTF-8 無 BOM）
    // 與原生 Sprite/Text/Tile 的 PakFile 完全不同，不能走 ResolveTarget。
    // <clientRoot>\ui\ 底下除 ui.pak/ui.idx 外一律當散檔（無副檔名白名單）。
    // 與 EatService.cs 同名邏輯保持對齊。
    // -------------------------------------------------------------------------
    private const string UiFileEncryptKey = "PAt82IqEvNBmERYl"; // 跟 OverlayAssets.cpp / Pack-UiAssets.ps1 同一把 key

    /// <summary>whole-file XOR；加密與解密同一函式（對稱）。</summary>
    private static byte[] UiXor(byte[] data)
    {
        var key = Encoding.ASCII.GetBytes(UiFileEncryptKey);
        var result = new byte[data.Length];
        for (var i = 0; i < data.Length; i++)
        {
            result[i] = (byte)(data[i] ^ key[i % key.Length]);
        }
        return result;
    }

    /// <summary>收集 ui\ 下待合併散檔（排除 ui.pak / ui.idx 本身）。</summary>
    private static List<string> CollectUiLooseFiles(string root)
    {
        var dir = Path.Combine(root, "ui");
        if (!Directory.Exists(dir))
        {
            return [];
        }

        return Directory.GetFiles(dir)
            .Where(f =>
            {
                var name = Path.GetFileName(f);
                return !name.Equals("ui.pak", StringComparison.OrdinalIgnoreCase) &&
                       !name.Equals("ui.idx", StringComparison.OrdinalIgnoreCase);
            })
            .ToList();
    }

    /// <summary>
    /// 讀現有 ui.pak/ui.idx（沒有就當空）→ 用散檔覆蓋／新增 → 全量重建寫回。
    /// whole-file XOR＋固定 offset 無法原地改單一 entry，全量重建最穩
    /// （與 Pack-UiAssets.ps1 每次重包相同）。
    /// </summary>
    private static (int updated, int added, int failed) MergeUiPak(
        string root, List<string> looseFiles, bool dryRun, bool keepLooseFiles)
    {
        var uiDir = Path.Combine(root, "ui");
        var pakPath = Path.Combine(uiDir, "ui.pak");
        var idxPath = Path.Combine(uiDir, "ui.idx");

        // entries：檔名 → 明文 bytes；order：維持原 idx 順序，新檔附加在尾端。
        var entries = new Dictionary<string, byte[]>(StringComparer.Ordinal);
        var order = new List<string>();
        if (File.Exists(pakPath) && File.Exists(idxPath))
        {
            var plain = UiXor(File.ReadAllBytes(pakPath));
            foreach (var line in File.ReadAllLines(idxPath))
            {
                // 格式：name=offset,length；任一欄解析失敗就跳過該行。
                var eq = line.IndexOf('=');
                if (eq <= 0)
                {
                    continue;
                }
                var name = line[..eq];
                var rest = line[(eq + 1)..].Split(',');
                if (rest.Length != 2)
                {
                    continue;
                }
                if (!long.TryParse(rest[0], out var offset) || !long.TryParse(rest[1], out var length))
                {
                    continue;
                }
                if (offset < 0 || length < 0 || offset + length > plain.Length)
                {
                    continue;
                }
                entries[name] = plain[(int)offset..(int)(offset + length)];
                order.Add(name);
            }
        }

        var updated = 0;
        var added = 0;
        var failed = 0;
        foreach (var file in looseFiles)
        {
            try
            {
                var name = Path.GetFileName(file);
                var data = File.ReadAllBytes(file);
                if (entries.ContainsKey(name))
                {
                    updated++;
                }
                else
                {
                    added++;
                    order.Add(name);
                }
                // 同名直接覆蓋明文；寫回時再依 order concat＋XOR。
                entries[name] = data;
            }
            catch
            {
                failed++;
            }
        }

        if (!dryRun)
        {
            Directory.CreateDirectory(uiDir);
            using var ms = new MemoryStream();
            var idxLines = new List<string>();
            foreach (var name in order)
            {
                if (!entries.TryGetValue(name, out var data))
                {
                    continue;
                }
                // offset 用目前 MemoryStream 位置；length 用明文長度。
                idxLines.Add($"{name}={ms.Position},{data.Length}");
                ms.Write(data, 0, data.Length);
            }
            File.WriteAllBytes(pakPath, UiXor(ms.ToArray()));
            File.WriteAllLines(idxPath, idxLines, new UTF8Encoding(false));

            if (!keepLooseFiles)
            {
                foreach (var file in looseFiles)
                {
                    try
                    {
                        File.Delete(file);
                    }
                    catch
                    {
                        // best-effort：散檔刪不掉不影響已寫入的 pak/idx。
                    }
                }
            }
        }

        return (updated, added, failed);
    }
}
