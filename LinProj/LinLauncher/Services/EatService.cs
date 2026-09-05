using System.IO;
using System.Text;
using Lin.Helper.Core.Pak;

namespace LinLauncher.Services;

/// <summary>
/// 吃檔進度階段。顯示文案請對齊：Staging＝準備排程（尚未寫碟），Writing＝寫入封裝（真正吃檔）。
/// </summary>
public enum EatPhase
{
    Preparing,
    Staging,   // 準備：本段進度條 0%～100%
    Writing,   // 套用：本段進度條 0%～100%（歸零後重跑）
    Done,
}

public sealed class EatProgress
{
    public EatPhase Phase { get; init; }
    public int Current { get; init; }
    public int Total { get; init; }
    public int OverallPercent { get; init; } // 0～100，給進度條
    public string Message { get; init; } = "";
}

public sealed class EatResult
{
    public int Updated { get; set; }
    public int Added { get; set; }
    public int Failed { get; set; }
    public int JobCount { get; set; }

    public bool HasWork
    {
        get { return JobCount > 0; }
    }

    public bool Ok
    {
        get { return Failed == 0; }
    }

    public string Summary
    {
        get
        {
            // 僅供 log／開發診斷；玩家 UI 請用簡短 StatusText，不要直接秀這串。
            return JobCount == 0
                ? "沒有需要吃檔的散檔"
                : $"吃檔完成：更新 {Updated}，新增 {Added}，失敗 {Failed}";
        }
    }
}

/// <summary>
/// 內建吃檔：把 icon / sprite / Surf / text / Tile 散檔寫入 Sprite/Text/Tile 的 idx/pak，
/// 以及自製 UI 的 ui.pak/ui.idx。
/// 登入器更新流程使用本類別；獨立命令列工具見 <c>LinProj/EatPack</c>。
/// 兩邊邏輯請保持對齊。取代外掛 eat.exe。
/// </summary>
public static class EatService
{
    // idx 檔名欄位固定 20 bytes（Latin1），超過會截斷，與原廠 eat 行為相同。
    private const int IdxNameBytes = 20;

    // Sprite 分卷軟上限：接近此大小就改寫下一個還有空間的 SpriteNN.pak。
    private const long PakSoftLimit = 1_900_000_000L;

    // 資料夾名稱（不分大小寫）→ 允許的副檔名白名單。
    private static readonly Dictionary<string, string[]> FolderExts = new(StringComparer.OrdinalIgnoreCase)
    {
        ["icon"] = [".tbt", ".ico"],
        ["sprite"] = [".spr", ".png"],
        ["surf"] = [".img"],
        ["text"] = [".html", ".tbl", ".txt", ".spr", ".spz"],
        ["tile"] = [".til", ".xml"],
    };

    /// <summary>客戶端根目錄是否還有待吃散檔（含 ui\）。</summary>
    public static bool HasPendingFiles(string clientRoot)
    {
        return CollectJobs(clientRoot).Count > 0 || CollectUiLooseFiles(clientRoot).Count > 0;
    }

    /// <summary>
    /// 執行一次完整吃檔。
    /// dryRun：只統計不寫入；keepLooseFiles：寫入後保留散檔。
    /// progress：回報進度（總步數含逐檔處理 + 各 pak 的 Save + UI 合併）。
    /// </summary>
    public static EatResult Run(
        string clientRoot,
        bool dryRun = false,
        bool keepLooseFiles = false,
        IProgress<EatProgress>? progress = null)
    {
        // PakFile / 舊檔名可能含 Big5 等非 UTF-8 編碼。
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);

        var result = new EatResult();
        var jobs = CollectJobs(clientRoot);
        var uiLooseFiles = CollectUiLooseFiles(clientRoot);
        result.JobCount = jobs.Count + uiLooseFiles.Count;
        if (result.JobCount == 0)
        {
            ReportProgress(progress, EatPhase.Done, 0, 0, "無須套用更新檔案");
            return result;
        }

        // 一次開好所有可能寫入的 pak，迴圈內只 Resolve / Add / Replace，最後統一 Save。
        var spritePaks = OpenSpritePaks(clientRoot);
        var textPak = OpenOptional(Path.Combine(clientRoot, "Text.idx"));
        var tilePak = OpenOptional(Path.Combine(clientRoot, "Tile.idx"));

        // Staging／Writing 各自跑滿 0～100%（不再 40/60 混算）。
        var stagingTotal = jobs.Count;
        var stagingDone = 0;

        var willWrite = !dryRun && (
            spritePaks.Count > 0 || textPak != null || tilePak != null || uiLooseFiles.Count > 0);

        ReportProgress(progress, EatPhase.Staging, stagingDone, stagingTotal, "準備中…");

        // 診斷用（追查「吃檔一次遊戲讀不到，吃第二次才看得到」的 idx 損毀問題）：
        // 記錄每個 PakFile 實際 Add/Replace 過的檔名；Save 後用 VerifyPakRoundTrip 從硬碟重開驗證。
        // 新檔可能被 PickSpritePakForNewFile 換卷，故以「最終呼叫 Add/Replace 的 target」為 key。
        var touchedByPak = new Dictionary<PakFile, List<string>>();
        void MarkTouched(PakFile pak, string fileName)
        {
            if (!touchedByPak.TryGetValue(pak, out var list))
            {
                touchedByPak[pak] = list = new List<string>();
            }
            list.Add(fileName);
        }

        try
        {
            foreach (var job in jobs)
            {
                stagingDone++;
                try
                {
                    var data = File.ReadAllBytes(job.FullPath);
                    var target = ResolveTarget(job, spritePaks, textPak, tilePak);
                    if (target is null)
                    {
                        // Text/Tile idx 不存在，或 Sprite 系列一個都打不開。
                        result.Failed++;
                        // UI 不顯示檔名；細節由呼叫端 StartupLog 記 Summary。
                        ReportProgress(progress, EatPhase.Staging, stagingDone, stagingTotal,
                            $"準備中… {stagingDone}/{stagingTotal}");
                        continue;
                    }

                    // 用截斷後的 20-byte key 比對；回傳 idx 裡實際存的原始檔名字串。
                    var existingName = FindStoredName(target, job.FileName);

                    if (!dryRun)
                    {
                        if (existingName is not null)
                        {
                            // 已存在：資料接到 pak 尾端並改 idx 位移。
                            target.Replace(existingName, data);
                            result.Updated++;
                            MarkTouched(target, job.FileName);
                        }
                        else
                        {
                            // 新檔：Sprite 依剩餘容量選分卷。
                            if (job.Kind == JobKind.Sprite)
                            {
                                target = PickSpritePakForNewFile(spritePaks, data.Length) ?? target;
                            }

                            // maintainSort: true → 維持 idx 內檔名排序。
                            target.Add(IdxKey(job.FileName), data, maintainSort: true);
                            result.Added++;
                            MarkTouched(target, job.FileName);
                        }

                        if (!keepLooseFiles)
                        {
                            File.Delete(job.FullPath);
                        }
                    }
                    else if (existingName is null)
                    {
                        result.Added++;
                    }
                    else
                    {
                        result.Updated++;
                    }

                    ReportProgress(progress, EatPhase.Staging, stagingDone, stagingTotal,
                        $"準備中… {stagingDone}/{stagingTotal}");
                }
                catch (Exception)
                {
                    result.Failed++;
                    // 例外不進 UI Message；呼叫端 StartupLog 記詳細。
                    ReportProgress(progress, EatPhase.Staging, stagingDone, stagingTotal,
                        $"準備中… {stagingDone}/{stagingTotal}");
                }
            }

            // 真正寫碟：進度條重跑 0～100%，均分 100 格平滑推進（與 Save 並行）。
            if (willWrite)
            {
                RunWritingWithHundredTicks(
                    progress,
                    clientRoot,
                    spritePaks,
                    textPak,
                    tilePak,
                    uiLooseFiles,
                    keepLooseFiles,
                    touchedByPak,
                    result);
            }
        }
        finally
        {
            // 無論成功失敗都釋放檔案鎖。
            foreach (var pak in spritePaks)
            {
                pak.Dispose();
            }
            textPak?.Dispose();
            tilePak?.Dispose();
        }

        // dryRun 不寫碟，但仍跑 ui 合併模擬（與舊行為一致）。
        if (dryRun && uiLooseFiles.Count > 0)
        {
            var (uiUpdated, uiAdded, uiFailed) = MergeUiPak(clientRoot, uiLooseFiles, dryRun: true, keepLooseFiles);
            result.Updated += uiUpdated;
            result.Added += uiAdded;
            result.Failed += uiFailed;
        }

        // 玩家可見文案保持簡短；詳細 Summary 由呼叫端寫 log。
        ReportProgress(progress, EatPhase.Done, 0, 0, result.Ok ? "套用完成" : "套用未完成");
        return result;
    }

    // 套用階段估時用：約 2MB/s，再夾在 2s～180s，切成 100 格。
    private const double WriteEstimateBytesPerMs = 2.0 * 1024 * 1024 / 1000.0;
    private const int WriteMinTotalMs = 2000;
    private const int WriteMaxTotalMs = 180_000;

    /// <summary>
    /// Writing：先估總時長再均分 100 格；Save／MergeUiPak 與 ticker 並行。
    /// I/O 早結束則加速收尾；ticker 到 99% 仍在寫則停住等 I/O，再跳 100%。
    /// </summary>
    private static void RunWritingWithHundredTicks(
        IProgress<EatProgress>? progress,
        string clientRoot,
        List<PakFile> spritePaks,
        PakFile? textPak,
        PakFile? tilePak,
        List<string> uiLooseFiles,
        bool keepLooseFiles,
        Dictionary<PakFile, List<string>> touchedByPak,
        EatResult result)
    {
        ReportProgress(progress, EatPhase.Writing, 0, 100, "套用中…", overallPercent: 0);

        long totalBytes = EstimateWriteBytes(spritePaks, textPak, tilePak, clientRoot, uiLooseFiles);
        var totalEstMs = (int)Math.Clamp(totalBytes / WriteEstimateBytesPerMs, WriteMinTotalMs, WriteMaxTotalMs);
        var tickMs = Math.Max(20, totalEstMs / 100);

        var saveTask = Task.Run(() =>
        {
            foreach (var pak in spritePaks)
            {
                var beforeType = pak.EncryptionType;
                pak.Save();
                if (touchedByPak.TryGetValue(pak, out var names))
                {
                    VerifyPakRoundTrip(IdxPathOf(pak), beforeType, names);
                }
            }
            textPak?.Save();
            tilePak?.Save();

            if (uiLooseFiles.Count > 0)
            {
                var (uiUpdated, uiAdded, uiFailed) = MergeUiPak(clientRoot, uiLooseFiles, dryRun: false, keepLooseFiles);
                result.Updated += uiUpdated;
                result.Added += uiAdded;
                result.Failed += uiFailed;
            }
        });

        for (var pct = 1; pct <= 99; pct++)
        {
            if (saveTask.Wait(tickMs))
            {
                break; // I/O 已結束，加速收尾
            }
            ReportProgress(progress, EatPhase.Writing, pct, 100, $"套用中… {pct}%", overallPercent: pct);
        }

        // 若仍在寫（已到 99%），停在 99% 等到完成，避免條滿了還在寫。
        if (!saveTask.IsCompleted)
        {
            ReportProgress(progress, EatPhase.Writing, 99, 100, "套用中… 99%", overallPercent: 99);
        }

        // 傳播 Save 執行緒上的例外
        saveTask.GetAwaiter().GetResult();

        ReportProgress(progress, EatPhase.Writing, 100, 100, "套用中… 100%", overallPercent: 100);
    }

    /// <summary>僅供估時；不拿來切百分比權重。</summary>
    private static long EstimateWriteBytes(
        List<PakFile> spritePaks,
        PakFile? textPak,
        PakFile? tilePak,
        string clientRoot,
        List<string> uiLooseFiles)
    {
        long total = 0;
        foreach (var pak in spritePaks)
        {
            total += PakFileBytes(pak);
        }
        if (textPak != null)
        {
            total += PakFileBytes(textPak);
        }
        if (tilePak != null)
        {
            total += PakFileBytes(tilePak);
        }
        if (uiLooseFiles.Count > 0)
        {
            var uiPak = Path.Combine(clientRoot, "ui", "ui.pak");
            if (File.Exists(uiPak))
            {
                total += new FileInfo(uiPak).Length;
            }
            foreach (var f in uiLooseFiles)
            {
                try
                {
                    total += new FileInfo(f).Length;
                }
                catch
                {
                    // ignore
                }
            }
        }
        return Math.Max(total, 1);
    }

    private static long PakFileBytes(PakFile pak)
    {
        try
        {
            var idx = IdxPathOf(pak);
            if (idx == "(idx)")
            {
                return 0;
            }
            var pakPath = Path.ChangeExtension(idx, ".pak");
            if (!string.IsNullOrEmpty(pakPath) && File.Exists(pakPath))
            {
                return new FileInfo(pakPath).Length;
            }
        }
        catch
        {
            // ignore
        }
        return 0;
    }

    // -------------------------------------------------------------------------
    // 自製 UI（ui.pak / ui.idx）
    // 格式見 tools\Pack-UiAssets.ps1 / LauncherDll OverlayAssets.cpp：
    //   - pak：entry raw bytes concat 後 whole-file XOR
    //   - idx：純文字 "name=offset,length"
    // 與原生 PakFile 完全不同；與 EatPack\Program.cs 同名邏輯保持對齊。
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
    /// 讀現有 ui.pak/ui.idx → 散檔覆蓋／新增 → 全量重建寫回。
    /// whole-file XOR＋固定 offset 無法原地改單一 entry，全量重建最穩。
    /// </summary>
    private static (int updated, int added, int failed) MergeUiPak(
        string root, List<string> looseFiles, bool dryRun, bool keepLooseFiles)
    {
        var uiDir = Path.Combine(root, "ui");
        var pakPath = Path.Combine(uiDir, "ui.pak");
        var idxPath = Path.Combine(uiDir, "ui.idx");

        var entries = new Dictionary<string, byte[]>(StringComparer.Ordinal);
        var order = new List<string>();
        if (File.Exists(pakPath) && File.Exists(idxPath))
        {
            var plain = UiXor(File.ReadAllBytes(pakPath));
            foreach (var line in File.ReadAllLines(idxPath))
            {
                // 格式：name=offset,length；解析失敗就跳過該行。
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

    /// <summary>
    /// Save() 後從硬碟重開同一 idx，確認 EncryptionType 沒跑掉、剛寫入的檔名還讀得到。
    /// 記憶體內 _records 更新成功不代表「寫進硬碟的 idx 格式」round-trip 正確。
    /// </summary>
    private static void VerifyPakRoundTrip(string idxPath, string beforeType, List<string> touchedNames)
    {
        try
        {
            using var reopened = new PakFile(idxPath);
            var name = Path.GetFileName(idxPath);
            AppLog.WriteLine("[Eat]",
                $"verify {name}: before={beforeType} after={reopened.EncryptionType} count={reopened.Count}");
            if (!string.Equals(beforeType, reopened.EncryptionType, StringComparison.Ordinal))
            {
                AppLog.WriteLine("[Eat]",
                    $"verify {name}: EncryptionType changed after Save()+reopen ({beforeType} -> {reopened.EncryptionType}), idx round-trip is likely corrupted");
            }

            foreach (var touched in touchedNames)
            {
                var found = FindStoredName(reopened, touched) is not null;
                AppLog.WriteLine("[Eat]",
                    found
                        ? $"verify {name}: OK {touched}"
                        : $"verify {name}: MISSING {touched} after reload (idx round-trip lost this entry)");
            }
        }
        catch (Exception ex)
        {
            AppLog.WriteLine("[Eat]", $"verify {Path.GetFileName(idxPath)}: reopen threw {ex.GetType().Name}: {ex.Message}");
        }
    }

    /// <summary>把檔名壓成 idx 用的 20-byte Latin1 key（超過截斷、尾端 NUL 去掉）。</summary>
    private static string IdxKey(string fileName)
    {
        var bytes = Encoding.Latin1.GetBytes(fileName);
        if (bytes.Length > IdxNameBytes)
        {
            bytes = bytes[..IdxNameBytes];
        }
        return Encoding.Latin1.GetString(bytes).TrimEnd('\0');
    }

    /// <summary>比對兩個檔名在 idx 語意上是否相同。</summary>
    private static bool SameIdxName(string a, string b)
    {
        return string.Equals(IdxKey(a), IdxKey(b), StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>
    /// 在 pak 裡找與 fileName 對得上的實際存檔名；找不到回傳 null。
    /// Replace 必須用 idx 裡那份原始字串。
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

    /// <summary>反射讀 PakFile 路徑屬性（僅供 log／選分卷用）。</summary>
    private static string IdxPathOf(PakFile pak)
    {
        foreach (var name in new[] { "IdxPath", "IndexPath", "Path", "FilePath" })
        {
            var val = pak.GetType().GetProperty(name)?.GetValue(pak)?.ToString();
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
    /// <param name="Folder">來源資料夾顯示名</param>
    /// <param name="FileName">檔名（含副檔名）</param>
    /// <param name="Kind">決定寫入哪一類 idx</param>
    /// <param name="ArchiveName">log 用封存名稱</param>
    private sealed record Job(string FullPath, string Folder, string FileName, JobKind Kind, string ArchiveName);

    /// <summary>
    /// 掃描 icon/sprite/Surf/text/Tile，依副檔名白名單收集待吃散檔。
    /// list.spr / list.spz 強制走 Text.idx。
    /// </summary>
    private static List<Job> CollectJobs(string root)
    {
        var jobs = new List<Job>();
        if (!Directory.Exists(root))
        {
            return jobs;
        }

        foreach (var (folder, exts) in FolderExts)
        {
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
    /// 開啟 Sprite.idx 以及 Sprite00～Sprite15（存在才開）。
    /// 順序：主卷在前、分卷依編號。
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

    /// <summary>idx 存在才開啟，否則回傳 null。</summary>
    private static PakFile? OpenOptional(string idxPath)
    {
        return File.Exists(idxPath) ? new PakFile(idxPath) : null;
    }

    /// <summary>
    /// 依 JobKind 選目標 pak。
    /// Sprite：優先選已有同名 entry 的分卷；都沒有才暫用清單第一個。
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
    /// 新增 Sprite 時選還有空間的分卷（現有大小 + 新檔 + 4KB &lt; PakSoftLimit）。
    /// 全部塞不下退回清單最後一個。
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

    /// <summary>
    /// 統一回報進度：組 EatProgress + 算 OverallPercent，避免 Run() 裡重複 new。
    /// Writing 均分 ticker 可直接指定 <paramref name="overallPercent"/>。
    /// </summary>
    private static void ReportProgress(
        IProgress<EatProgress>? progress,
        EatPhase phase,
        int current,
        int total,
        string message,
        int? overallPercent = null)
    {
        progress?.Report(new EatProgress
        {
            Phase = phase,
            Current = current,
            Total = total,
            OverallPercent = overallPercent ?? CalcOverallPercent(phase, current, total),
            Message = message,
        });
    }

    /// <summary>Staging／Writing 各自 0～100%；Done 100%。</summary>
    private static int CalcOverallPercent(EatPhase phase, int current, int total)
    {
        if (phase == EatPhase.Done)
        {
            return 100;
        }
        if (total <= 0)
        {
            return 0;
        }
        var ratio = Math.Clamp(current, 0, total) / (double)total;
        return phase switch
        {
            EatPhase.Staging => (int)(ratio * 100),
            EatPhase.Writing => (int)(ratio * 100),
            EatPhase.Preparing => 0,
            _ => 0,
        };
    }
}
