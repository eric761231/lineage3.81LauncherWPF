using System.Text;
using Lin.Helper.Core.Pak;

namespace EatPack;

internal static class Program
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

    public static int Main(string[] args)
    {
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);

        var clientRoot = Directory.GetCurrentDirectory();
        var dryRun = args.Any(a => a is "-n" or "--dry-run");
        var keep = args.Any(a => a is "-k" or "--keep");

        if (args.Any(a => a is "-h" or "--help"))
        {
            PrintHelp();
            return 0;
        }

        for (var i = 0; i < args.Length; i++)
        {
            if (args[i] is "-d" or "--dir" && i + 1 < args.Length)
                clientRoot = Path.GetFullPath(args[++i]);
        }

        Console.WriteLine($"EatPack 客戶端目錄: {clientRoot}");
        if (dryRun) Console.WriteLine("（預覽模式，不會寫入 idx/pak）");

        if (args.Any(a => a is "--extract"))
        {
            var rest = args.SkipWhile(a => a != "--extract").Skip(1).ToArray();
            if (rest.Length < 3)
            {
                Console.WriteLine("用法: EatPack --extract <idx檔> <檔名> <輸出路徑>");
                return 1;
            }
            using var pak = new PakFile(Path.IsPathRooted(rest[0]) ? rest[0] : Path.Combine(clientRoot, rest[0]));
            var name = FindStoredName(pak, rest[1]) ?? rest[1];
            var data = pak.Extract(name);
            File.WriteAllBytes(rest[2], data);
            Console.WriteLine($"已匯出 {name} ({data.Length} bytes) -> {rest[2]}");
            return 0;
        }

        if (args.Any(a => a is "--list"))
        {
            var filter = args.SkipWhile(a => a != "--list").Skip(1).FirstOrDefault() ?? "";
            using var pak = new PakFile(Path.Combine(clientRoot, "Text.idx"));
            Console.WriteLine($"Text.idx count={pak.Count} enc={pak.EncryptionType}");
            foreach (var f in pak.Files.Where(f =>
                         filter.Length == 0 || f.FileName.Contains(filter, StringComparison.OrdinalIgnoreCase)))
                Console.WriteLine($"{f.FileName}\t{f.FileName.Length} chars");
            return 0;
        }

        var jobs = CollectJobs(clientRoot);
        if (jobs.Count == 0)
        {
            Console.WriteLine("沒有找到可吃檔的散檔。請放到 icon / sprite / Surf / text / Tile。");
            return 0;
        }

        Console.WriteLine($"待處理 {jobs.Count} 個檔案。");

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
                        Console.WriteLine($"[略過] {job.ArchiveName} 找不到對應 idx: {job.FileName}");
                        failed++;
                        continue;
                    }

                    var existingName = FindStoredName(target, job.FileName);

                    if (dryRun)
                    {
                        Console.WriteLine(
                            $"[{(existingName is null ? "新增" : "更新")}] {job.Folder}\\{job.FileName}" +
                            $" (idx名稱={IdxKey(job.FileName)}) -> {Path.GetFileName(IdxPathOf(target))}");
                        if (existingName is null) added++; else updated++;
                        continue;
                    }

                    if (existingName is not null)
                    {
                        target.Replace(existingName, data);
                        Console.WriteLine($"[更新] {job.Folder}\\{job.FileName} ({data.Length} bytes) -> {Path.GetFileName(IdxPathOf(target))}");
                        updated++;
                    }
                    else
                    {
                        if (job.Kind == JobKind.Sprite)
                            target = PickSpritePakForNewFile(spritePaks, data.Length) ?? target;

                        target.Add(IdxKey(job.FileName), data, maintainSort: true);
                        Console.WriteLine($"[新增] {job.Folder}\\{job.FileName} ({data.Length} bytes) -> {Path.GetFileName(IdxPathOf(target))}");
                        added++;
                    }

                    if (!keep)
                        File.Delete(job.FullPath);
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[失敗] {job.FileName}: {ex.Message}");
                    failed++;
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

            選項:
              -d, --dir       客戶端根目錄（預設為目前目錄）
              -n, --dry-run   只預覽，不寫入
              -k, --keep      吃檔後保留散檔
              --list [關鍵字] 列出 Text.idx 檔名
            """);
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
            var prop = pak.GetType().GetProperty(name);
            var val = prop?.GetValue(pak)?.ToString();
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
