using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace LinLauncher
{
    /// <summary>
    /// 啟動診斷日誌：盡早在 ModuleInitializer 寫入。
    /// 若連任一 *.log 都沒有，代表多半在「載入 .NET host / hostfxr / 原生相依」階段即失敗，請用同目錄的 diag_host_trace.cmd 取得 host 追蹤。
    /// </summary>
    internal static class StartupLog
    {
        private static readonly object Sync = new();
        private static bool _environmentWritten;

        [ModuleInitializer]
        internal static void LogModuleLoad()
        {
            try
            {
                RawWrite("========== 新工作階段 ==========");
                RawWrite("CLR: ModuleInitializer（LinLauncher 組件已載入，尚未 Main）");
                WriteEnvironmentSnapshot();
            }
            catch (Exception ex)
            {
                TryEmergencyWrite("ModuleInitializer: " + ex);
            }
        }

        internal static void Append(string line)
        {
            RawWrite(line);
        }

        internal static void Append(string context, Exception? ex)
        {
            if (ex == null)
            {
                RawWrite(context);
                return;
            }

            var sb = new StringBuilder();
            sb.AppendLine(context);
            sb.AppendLine(ex.ToString());
            if (ex.InnerException != null)
                sb.AppendLine("Inner: " + ex.InnerException);
            RawWrite(sb.ToString().TrimEnd());
        }

        private static void WriteEnvironmentSnapshot()
        {
            lock (Sync)
            {
                if (_environmentWritten) return;
                _environmentWritten = true;
            }

            try
            {
                var sb = new StringBuilder();
                sb.AppendLine("--- 環境快照 ---");
                sb.AppendLine($"Framework: {RuntimeInformation.FrameworkDescription}");
                sb.AppendLine($"CLR: {Environment.Version}");
                sb.AppendLine($"進程架構: {RuntimeInformation.ProcessArchitecture}  OS 架構: {RuntimeInformation.OSArchitecture}");
                sb.AppendLine($"OS: {Environment.OSVersion}");
                sb.AppendLine($"ProcessPath: {SafeStr(Environment.ProcessPath)}");
                sb.AppendLine($"BaseDirectory: {SafeStr(AppDomain.CurrentDomain.BaseDirectory)}");
                sb.AppendLine($"CurrentDirectory: {SafeStr(Environment.CurrentDirectory)}");
                sb.AppendLine($"UserName: {Environment.UserName}  Machine: {Environment.MachineName}");
                sb.AppendLine($"CommandLine: {SafeStr(Environment.CommandLine)}");
                sb.AppendLine($"DOTNET_ROOT: {SafeStr(Environment.GetEnvironmentVariable("DOTNET_ROOT"))}");
                sb.AppendLine($"DOTNET_ENVIRONMENT: {SafeStr(Environment.GetEnvironmentVariable("DOTNET_ENVIRONMENT"))}");
                RawWrite(sb.ToString().TrimEnd());
            }
            catch (Exception ex)
            {
                RawWrite("環境快照寫入失敗: " + ex.Message);
            }
        }

        private static string SafeStr(string? s)
        {
            if (string.IsNullOrEmpty(s)) return "(null)";
            return s.Length > 2048 ? s.Substring(0, 2048) + "…" : s;
        }

        private static void RawWrite(string line)
        {
            string msg = $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] {line}\r\n";
            foreach (string path in GetLogPaths())
            {
                try
                {
                    string? dir = Path.GetDirectoryName(path);
                    if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
                        Directory.CreateDirectory(dir);

                    using (var fs = new FileStream(path, FileMode.Append, FileAccess.Write, FileShare.Read))
                    using (var sw = new StreamWriter(fs, Encoding.UTF8))
                    {
                        sw.Write(msg);
                        sw.Flush();
                        fs.Flush(true);
                    }
                }
                catch
                {
                    // 嘗試下一個路徑
                }
            }
        }

        /// <summary>最後手段：只寫一個固定路徑，避免 ModuleInitializer 完全無法留下紀錄。</summary>
        private static void TryEmergencyWrite(string line)
        {
            string msg = $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] {line}\r\n";
            foreach (string path in new[] { Path.Combine(Path.GetTempPath(), "LinLauncher_startup.log"), @"C:\Windows\Temp\LinLauncher_emergency.log" })
            {
                try
                {
                    File.AppendAllText(path, msg, Encoding.UTF8);
                    return;
                }
                catch { }
            }
        }

        private static IEnumerable<string> GetLogPaths()
        {
            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            var list = new List<string>();

            void Add(string path)
            {
                if (string.IsNullOrEmpty(path)) return;
                try
                {
                    string full = Path.GetFullPath(path);
                    if (seen.Add(full))
                        list.Add(full);
                }
                catch
                {
                    if (seen.Add(path))
                        list.Add(path);
                }
            }

            // 1) 與 exe 同目錄（玩家最直覺找得到）
            try
            {
                string? ep = Environment.ProcessPath;
                if (!string.IsNullOrEmpty(ep))
                {
                    string? dir = Path.GetDirectoryName(ep);
                    if (!string.IsNullOrEmpty(dir))
                        Add(Path.Combine(dir, "LinLauncher_boot.log"));
                }
            }
            catch { }

            // 2) AppDomain 基底（與 WorkingDirectory 可能不同時仍有用）
            try
            {
                string baseDir = AppDomain.CurrentDomain.BaseDirectory;
                if (!string.IsNullOrEmpty(baseDir))
                {
                    string trimmed = baseDir.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
                    Add(Path.Combine(trimmed, "LinLauncher_boot.log"));
                }
            }
            catch { }

            // 3) 暫存目錄
            Add(Path.Combine(Path.GetTempPath(), "LinLauncher_startup.log"));

            // 4) LocalAppData
            try
            {
                string folder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "LinLauncher");
                Add(Path.Combine(folder, "startup.log"));
            }
            catch { }

            // 5) ProgramData（權限通常 OK，避免 TEMP 被政策擋）
            try
            {
                string folder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "LinLauncher");
                Add(Path.Combine(folder, "startup.log"));
            }
            catch { }

            // 6) 目前目錄（若從錯誤 cwd 啟動仍可對照）
            try
            {
                Add(Path.Combine(Environment.CurrentDirectory, "LinLauncher_cwd.log"));
            }
            catch { }

            return list;
        }
    }
}
