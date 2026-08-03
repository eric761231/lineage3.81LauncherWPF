using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;

var root = AppContext.BaseDirectory;
var repoRoot = FindRepoRoot(root);
if (repoRoot is null)
{
    Console.Error.WriteLine("無法定位專案根目錄。") ;
    Environment.Exit(1);
}

var encoderProject = Path.Combine(repoRoot, "LinProj", "Encoder", "Encoder.csproj");
var launcherProject = Path.Combine(repoRoot, "LinProj", "LinLauncher", "LinLauncher.csproj");
var publishRoot = Path.Combine(repoRoot, "LinProj", "LinLauncher", "publish");
var outputRoot = Path.Combine(repoRoot, "updates");
var baseUrl = Environment.GetEnvironmentVariable("LAUNCHER_UPDATE_BASE_URL") ?? "http://localhost/updates";
var version = Environment.GetEnvironmentVariable("LAUNCHER_VERSION") ?? DateTime.UtcNow.ToString("yyyyMMddHHmm");

Console.WriteLine($"專案根目錄: {repoRoot}");
Console.WriteLine($"更新網址基底: {baseUrl}");
Console.WriteLine($"版本號: {version}");

var publishDir = Path.Combine(publishRoot, version);
Directory.CreateDirectory(publishDir);

var publishProcess = new System.Diagnostics.Process
{
    StartInfo = new System.Diagnostics.ProcessStartInfo
    {
        FileName = "dotnet",
        Arguments = $"publish \"{launcherProject}\" -c Release -r win-x86 --self-contained false -o \"{publishDir}\"",
        RedirectStandardOutput = true,
        RedirectStandardError = true,
        UseShellExecute = false,
        CreateNoWindow = true
    }
};
publishProcess.Start();
string stdout = publishProcess.StandardOutput.ReadToEnd();
string stderr = publishProcess.StandardError.ReadToEnd();
publishProcess.WaitForExit();
if (publishProcess.ExitCode != 0)
{
    Console.Error.WriteLine(stdout);
    Console.Error.WriteLine(stderr);
    Environment.Exit(publishProcess.ExitCode);
}

var outputDir = Path.Combine(outputRoot, version);
var result = BuildUpdatePackage(publishDir, outputDir, baseUrl, 2);
if (!result.Success)
{
    Console.Error.WriteLine(result.ErrorMessage);
    Environment.Exit(1);
}

File.WriteAllText(Path.Combine(outputDir, "version.txt"), version, new UTF8Encoding(false));
Console.WriteLine($"更新包已生成: {outputDir}");
Console.WriteLine($"更新清單: {Path.Combine(outputDir, "update.txt")}");

static (bool Success, string? ErrorMessage) BuildUpdatePackage(string sourceDir, string outputDir, string baseUrl, int compressionLevel)
{
    if (string.IsNullOrWhiteSpace(sourceDir) || !Directory.Exists(sourceDir))
        return (false, "請選擇有效的來源目錄。" );
    if (string.IsNullOrWhiteSpace(outputDir))
        return (false, "請填寫輸出目錄。" );

    Directory.CreateDirectory(outputDir);
    var xorKey = Encoding.ASCII.GetBytes("PAt82IqEvNBmERYl");
    var workList = Directory.GetFiles(sourceDir, "*", SearchOption.AllDirectories)
        .Where(f => !string.Equals(Path.GetFileName(f), "Thumbs.db", StringComparison.OrdinalIgnoreCase))
        .OrderBy(f => f, StringComparer.OrdinalIgnoreCase)
        .ToList();

    if (workList.Count == 0)
        return (false, "來源目錄內沒有可封裝的檔案。" );

    var lines = new List<string>();
    var root = Path.GetFullPath(sourceDir);
    int idx = 0;

    foreach (var fullPath in workList)
    {
        string rel = Path.GetRelativePath(root, fullPath).Replace('\\', '/');
        byte[] raw = File.ReadAllBytes(fullPath);
        string md5Hex = Convert.ToHexString(MD5.HashData(raw)).ToLowerInvariant();
        byte[] compressed = Compress(raw, compressionLevel);
        int totalLen = 4 + compressed.Length;
        if (totalLen < 20)
            return (false, $"封裝後長度不足 20 bytes：{rel}");

        var packet = new byte[totalLen];
        Buffer.BlockCopy(compressed, 0, packet, 4, compressed.Length);
        for (int i = 0; i < 16; i++)
            packet[4 + i] ^= xorKey[i % xorKey.Length];

        string outPath = Path.Combine(outputDir, rel.Replace('/', Path.DirectorySeparatorChar)) + ".bin";
        Directory.CreateDirectory(Path.GetDirectoryName(outPath)!);
        File.WriteAllBytes(outPath, packet);

        idx++;
        lines.Add($"file_{idx - 1}={rel}");
        lines.Add($"md5_{idx - 1}={md5Hex}");
    }

    var sb = new StringBuilder();
    sb.AppendLine("[main]");
    sb.AppendLine($"count={idx}");
    sb.AppendLine($"url={baseUrl.TrimEnd('/')}");
    sb.AppendLine();
    sb.AppendLine("[update]");
    foreach (var line in lines)
        sb.AppendLine(line);
    File.WriteAllText(Path.Combine(outputDir, "update.txt"), sb.ToString(), new UTF8Encoding(false));
    return (true, null);
}

static byte[] Compress(byte[] raw, int compressionLevel)
{
    using var ms = new MemoryStream();
    using (var zs = new ZLibStream(ms, compressionLevel switch { 1 => CompressionLevel.Fastest, 2 => CompressionLevel.SmallestSize, _ => CompressionLevel.Optimal }, leaveOpen: true))
    {
        zs.Write(raw, 0, raw.Length);
    }
    return ms.ToArray();
}

static string? FindRepoRoot(string start)
{
    var dir = new DirectoryInfo(start);
    while (dir is not null)
    {
        if (File.Exists(Path.Combine(dir.FullName, "LauncherWPF381.sln")))
            return dir.FullName;
        dir = dir.Parent;
    }
    return null;
}
