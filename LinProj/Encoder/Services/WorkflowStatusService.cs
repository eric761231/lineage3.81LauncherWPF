using System;
using System.IO;
using System.Linq;

namespace LinEncoder.Services
{
    /// <summary>右側常駐流程導覽面板用的狀態快照，一次計算好所有燈號。</summary>
    public class WorkflowStatus
    {
        /// <summary>是否找得到 repo 原始碼（LinProj\），找不到就不做建置狀態偵測（例如純部署環境沒有原始碼）。</summary>
        public bool RepoFound { get; set; }
        public bool EncoderNeedsRebuild { get; set; }
        public bool LauncherNeedsRebuild { get; set; }
        public string? RepoRoot { get; set; }

        public bool KeyGenerated { get; set; }
        public bool ListGenerated { get; set; }
        /// <summary>金鑰產生後沒有重新「產生清單」——list.txt 裡埋的金鑰過時了，真正有技術依據的順序錯誤。</summary>
        public bool ListStaleVsKey { get; set; }
        public bool LauncherExeGenerated { get; set; }
        public bool PakGenerated { get; set; }

        public bool PatchGenerated { get; set; }
        public bool PatchUpToDate { get; set; }
        /// <summary>本次執行期間是否已成功按過「上傳補丁」——沒有可靠的方式從本機檔案判斷遠端 FTP 狀態，
        /// 所以這個欄位不是 Compute() 算出來的，是呼叫端（EncoderViewModel）自己維護、每次 Compute() 後蓋回去的。</summary>
        public bool PatchUploaded { get; set; }
    }

    /// <summary>
    /// 計算 Encoder 右側常駐面板要顯示的流程狀態：原始碼是否比已建置版本新、
    /// 一般操作（金鑰／清單／登入器／pak）是否已產生過、補丁是否為最新。
    /// 純讀檔案時間戳／是否存在，不做任何寫入。
    /// </summary>
    public static class WorkflowStatusService
    {
        private static readonly string[] SourcePatterns = { "*.cs", "*.xaml" };

        public static WorkflowStatus Compute(
            string partnersRootDir,
            string? outputLauncherName,
            string? bdOutputDir,
            string? patchSourceDir,
            string? patchOutputDir)
        {
            var status = new WorkflowStatus();

            string? repoRoot = Directory.GetParent(partnersRootDir.TrimEnd('\\', '/'))?.FullName;
            string? linProj = repoRoot != null ? Path.Combine(repoRoot, "LinProj") : null;
            if (repoRoot != null && linProj != null && Directory.Exists(linProj))
            {
                status.RepoFound = true;
                status.RepoRoot = repoRoot;
                string encoderExe = Path.Combine(partnersRootDir, "EncoderTool", "LinEncoder.exe");
                string launcherDll = Path.Combine(partnersRootDir, "Core", "LinLauncher.dll");
                status.EncoderNeedsRebuild = IsSourceNewer(Path.Combine(linProj, "Encoder"), encoderExe);
                status.LauncherNeedsRebuild = IsSourceNewer(Path.Combine(linProj, "LinLauncher"), launcherDll);
            }

            // 用實際檔案（EncoderTool\pack.properties）判斷金鑰是否產生過，而不是看目前選取伺服器的記憶體值——
            // 這樣重開 Encoder 之後、以及跟 list.txt 的時間戳比對「順序是否正確」才會準。
            string packPropPath = Path.Combine(partnersRootDir, "EncoderTool", "pack.properties");
            string listPath = Path.Combine(partnersRootDir, "login", "list.txt");
            status.KeyGenerated = File.Exists(packPropPath);
            status.ListGenerated = File.Exists(listPath);
            if (status.KeyGenerated && status.ListGenerated)
            {
                status.ListStaleVsKey = File.GetLastWriteTimeUtc(packPropPath) > File.GetLastWriteTimeUtc(listPath);
            }

            string safeName = string.IsNullOrWhiteSpace(outputLauncherName) ? "Launcher" : outputLauncherName!;
            status.LauncherExeGenerated = File.Exists(Path.Combine(partnersRootDir, safeName + ".exe"));

            status.PakGenerated = !string.IsNullOrWhiteSpace(bdOutputDir)
                && Directory.Exists(bdOutputDir)
                && Directory.EnumerateFiles(bdOutputDir).Any();

            if (!string.IsNullOrWhiteSpace(patchOutputDir))
            {
                string updateTxt = Path.Combine(patchOutputDir, "update.txt");
                status.PatchGenerated = File.Exists(updateTxt);
                if (status.PatchGenerated && !string.IsNullOrWhiteSpace(patchSourceDir) && Directory.Exists(patchSourceDir))
                {
                    DateTime updateTxtTime = File.GetLastWriteTimeUtc(updateTxt);
                    DateTime newestSource = GetNewestFileTime(patchSourceDir, Array.Empty<string>());
                    status.PatchUpToDate = newestSource <= updateTxtTime;
                }
            }

            return status;
        }

        private static bool IsSourceNewer(string sourceDir, string artifactPath)
        {
            if (!Directory.Exists(sourceDir)) return false;
            if (!File.Exists(artifactPath)) return true; // 從沒建置過，視為需要建置
            DateTime artifactTime = File.GetLastWriteTimeUtc(artifactPath);
            DateTime newestSource = GetNewestFileTime(sourceDir, SourcePatterns);
            return newestSource > artifactTime;
        }

        private static DateTime GetNewestFileTime(string dir, string[] patterns)
        {
            DateTime newest = DateTime.MinValue;
            string[] effectivePatterns = patterns.Length > 0 ? patterns : new[] { "*" };
            foreach (string pattern in effectivePatterns)
            {
                foreach (string file in Directory.EnumerateFiles(dir, pattern, SearchOption.AllDirectories))
                {
                    if (file.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}") ||
                        file.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}"))
                        continue;
                    DateTime t = File.GetLastWriteTimeUtc(file);
                    if (t > newest) newest = t;
                }
            }
            return newest;
        }
    }
}
