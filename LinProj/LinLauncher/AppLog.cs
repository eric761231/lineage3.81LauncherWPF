// AppLog.cs: 全專案唯一的 log 寫入實作。LogService/ErrorLog/StartupLog 都只是薄
// 外殼，實際寫檔都透過這裡，統一寫進 Core\launcher.log（跟注入遊戲行程的
// LauncherDll.dll 用的是同一個實體檔案，見 launcherdll_net.log 已改名並搬進
// Core 的那次調整）。
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace LinLauncher
{
    internal static class AppLog
    {
        private const string FileName = "launcher.log";
        private static readonly object Sync = new();

        /// <summary>寫一行（可含換行的多行內容）。依序嘗試候選路徑，寫入成功就停止
        /// ——正常情況下只會有 Core\launcher.log 這一份，只有主要路徑寫入失敗
        /// （例如沒有寫入權限）才會落到備援位置。</summary>
        public static void WriteLine(string tag, string message)
        {
            string line = $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] {tag} {message}";
            lock (Sync)
            {
                foreach (string path in GetCandidatePaths())
                {
                    try
                    {
                        string? dir = Path.GetDirectoryName(path);
                        if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
                            Directory.CreateDirectory(dir);
                        using (var fs = new FileStream(path, FileMode.Append, FileAccess.Write, FileShare.Read))
                        using (var sw = new StreamWriter(fs, Encoding.UTF8))
                        {
                            sw.WriteLine(line);
                        }
                        return; // 寫入成功，不再嘗試其他候選路徑
                    }
                    catch
                    {
                        // 換下一個候選路徑
                    }
                }
            }
        }

        private static IEnumerable<string> GetCandidatePaths()
        {
            // 1) exe 所在目錄（正常情況下就是 Core，也是唯一會用到的路徑）
            string? ep = Environment.ProcessPath;
            if (!string.IsNullOrEmpty(ep))
            {
                string? dir = Path.GetDirectoryName(ep);
                if (!string.IsNullOrEmpty(dir))
                    yield return Path.Combine(dir, FileName);
            }

            // 2) AppDomain 基底（跟 1) 通常是同一個目錄，ProcessPath 拿不到時的備援）
            string baseDir = AppDomain.CurrentDomain.BaseDirectory;
            if (!string.IsNullOrEmpty(baseDir))
            {
                string trimmed = baseDir.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
                yield return Path.Combine(trimmed, FileName);
            }

            // 3) 目前工作目錄
            yield return Path.Combine(Environment.CurrentDirectory, FileName);

            // 4) Temp（Core 目錄真的不可寫時的最後備援）
            yield return Path.Combine(Path.GetTempPath(), FileName);

            // 5) LocalAppData\LinLauncher
            string local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            if (!string.IsNullOrEmpty(local))
                yield return Path.Combine(local, "LinLauncher", FileName);
        }
    }
}
