using System.IO;
using System.Text;
using Lin.Helper.Core.Pak;

namespace LinLauncher.Services;

public sealed class EatProgress
{
    public int Current { get; init; }
    public int Total { get; init; }
    public string Message { get; init; } = "";
}

public sealed class EatResult
{
    public int Updated { get; set; }
    public int Added { get; set; }
    public int Failed { get; set; }
    public int JobCount { get; set; }

    public bool HasWork => JobCount > 0;
    public bool Ok => Failed == 0;

    public string Summary =>
        JobCount == 0
            ? "沒有需要吃檔的散檔"
            : $"吃檔完成：更新 {Updated}，新增 {Added}，失敗 {Failed}";
}

/// <summary>
/// 內建吃檔：把 icon / sprite / Surf / text / Tile 散檔寫入 Sprite/Text/Tile 的 idx/pak。
/// 登入器更新流程使用本類別；獨立命令列工具見 <c>LinProj/EatPack</c>。
/// 取代外掛 eat.exe。
/// </summary>
public static class EatService
{
    private const int IdxNameBytes = 20;
    private const long PakSoftLimit = 1_900_000_000L;

    private static readonly Dictionary<string, string[]> FolderExts = new(StringComparer.OrdinalIgnoreCase)
    {
        ["icon"] = [".tbt", ".ico"],
        ["sprite"] = [".spr", ".png"],
        ["surf"] = [".img"],
        ["text"] = [".html", ".tbl", ".txt", ".spr", ".spz"],
        ["tile"] = [".til", ".xml"],
    };

    public static bool HasPendingFiles(string clientRoot) => CollectJobs(clientRoot).Count > 0;

    public static EatResult Run(
        string clientRoot,
        bool dryRun = false,
        bool keepLooseFiles = false,
        IProgress<EatProgress>? progress = null)
    {
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);

        var result = new EatResult();
        var jobs = CollectJobs(clientRoot);
        result.JobCount = jobs.Count;
        if (jobs.Count == 0)
        {
            progress?.Report(new EatProgress { Message = result.Summary });
            return result;
        }

        progress?.Report(new EatProgress
        {
            Current = 0,
            Total = jobs.Count,
            Message = $"開始吃檔（{jobs.Count} 個檔案）…",
        });

        var spritePaks = OpenSpritePaks(clientRoot);
        var textPak = OpenOptional(Path.Combine(clientRoot, "Text.idx"));
        var tilePak = OpenOptional(Path.Combine(clientRoot, "Tile.idx"));

        try
        {
            var index = 0;
            foreach (var job in jobs)
            {
                index++;
                try
                {
                    var data = File.ReadAllBytes(job.FullPath);
                    var target = ResolveTarget(job, spritePaks, textPak, tilePak);
                    if (target is null)
                    {
                        result.Failed++;
                        progress?.Report(new EatProgress
                        {
                            Current = index,
                            Total = jobs.Count,
                            Message = $"略過 {job.FileName}（找不到 {job.ArchiveName}.idx）",
                        });
                        continue;
                    }

                    var existingName = FindStoredName(target, job.FileName);
                    var action = existingName is null ? "新增" : "更新";

                    if (!dryRun)
                    {
                        if (existingName is not null)
                        {
                            target.Replace(existingName, data);
                            result.Updated++;
                        }
                        else
                        {
                            if (job.Kind == JobKind.Sprite)
                                target = PickSpritePakForNewFile(spritePaks, data.Length) ?? target;

                            target.Add(IdxKey(job.FileName), data, maintainSort: true);
                            result.Added++;
                        }

                        if (!keepLooseFiles)
                            File.Delete(job.FullPath);
                    }
                    else if (existingName is null)
                        result.Added++;
                    else
                        result.Updated++;

                    progress?.Report(new EatProgress
                    {
                        Current = index,
                        Total = jobs.Count,
                        Message = $"[{action}] {job.Folder}\\{job.FileName}",
                    });
                }
                catch (Exception ex)
                {
                    result.Failed++;
                    progress?.Report(new EatProgress
                    {
                        Current = index,
                        Total = jobs.Count,
                        Message = $"失敗 {job.FileName}: {ex.Message}",
                    });
                }
            }

            if (!dryRun)
            {
                foreach (var pak in spritePaks)
                    pak.Save();
                textPak?.Save();
                tilePak?.Save();
            }
        }
        finally
        {
            foreach (var pak in spritePaks)
                pak.Dispose();
            textPak?.Dispose();
            tilePak?.Dispose();
        }

        progress?.Report(new EatProgress
        {
            Current = jobs.Count,
            Total = jobs.Count,
            Message = result.Summary,
        });
        return result;
    }

    private static string IdxKey(string fileName)
    {
        var bytes = Encoding.Latin1.GetBytes(fileName);
        if (bytes.Length > IdxNameBytes)
            bytes = bytes[..IdxNameBytes];
        return Encoding.Latin1.GetString(bytes).TrimEnd('\0');
    }

    private static bool SameIdxName(string a, string b) =>
        string.Equals(IdxKey(a), IdxKey(b), StringComparison.OrdinalIgnoreCase);

    private static string? FindStoredName(PakFile pak, string fileName)
    {
        foreach (var f in pak.Files)
        {
            if (SameIdxName(f.FileName, fileName))
                return f.FileName;
        }
        return null;
    }

    private static string IdxPathOf(PakFile pak)
    {
        foreach (var name in new[] { "IdxPath", "IndexPath", "Path", "FilePath" })
        {
            var val = pak.GetType().GetProperty(name)?.GetValue(pak)?.ToString();
            if (!string.IsNullOrEmpty(val))
                return val;
        }
        return "(idx)";
    }

    private enum JobKind { Sprite, Text, Tile }

    private sealed record Job(string FullPath, string Folder, string FileName, JobKind Kind, string ArchiveName);

    private static List<Job> CollectJobs(string root)
    {
        var jobs = new List<Job>();
        if (!Directory.Exists(root))
            return jobs;

        foreach (var (folder, exts) in FolderExts)
        {
            var dir = Directory.GetDirectories(root)
                .FirstOrDefault(d => string.Equals(Path.GetFileName(d), folder, StringComparison.OrdinalIgnoreCase));
            if (dir is null)
                continue;

            foreach (var file in Directory.GetFiles(dir))
            {
                var ext = Path.GetExtension(file);
                if (!exts.Contains(ext, StringComparer.OrdinalIgnoreCase))
                    continue;

                var name = Path.GetFileName(file);
                var kind = folder.Equals("text", StringComparison.OrdinalIgnoreCase)
                    ? JobKind.Text
                    : folder.Equals("tile", StringComparison.OrdinalIgnoreCase)
                        ? JobKind.Tile
                        : JobKind.Sprite;

                if (name.StartsWith("list.", StringComparison.OrdinalIgnoreCase) &&
                    (name.EndsWith(".spr", StringComparison.OrdinalIgnoreCase) ||
                     name.EndsWith(".spz", StringComparison.OrdinalIgnoreCase)))
                    kind = JobKind.Text;

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

    private static List<PakFile> OpenSpritePaks(string root)
    {
        var list = new List<PakFile>();
        var main = Path.Combine(root, "Sprite.idx");
        if (File.Exists(main))
            list.Add(new PakFile(main));

        for (var i = 0; i <= 15; i++)
        {
            var idx = Path.Combine(root, $"Sprite{i:00}.idx");
            if (File.Exists(idx))
                list.Add(new PakFile(idx));
        }

        return list;
    }

    private static PakFile? OpenOptional(string idxPath) =>
        File.Exists(idxPath) ? new PakFile(idxPath) : null;

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

    private static PakFile? PickSpritePakForNewFile(List<PakFile> sprites, int fileSize)
    {
        foreach (var pak in sprites)
        {
            var idx = IdxPathOf(pak);
            var pakPath = Path.ChangeExtension(idx, ".pak");
            if (!File.Exists(pakPath))
                continue;
            if (new FileInfo(pakPath).Length + fileSize + 4096 < PakSoftLimit)
                return pak;
        }

        return sprites.LastOrDefault();
    }
}
