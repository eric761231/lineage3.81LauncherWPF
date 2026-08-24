using System;
using System.IO;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace LinLauncher
{
    /// <summary>
    /// 啟動診斷日誌：盡早在 ModuleInitializer 寫入，透過 AppLog 寫進
    /// Core\launcher.log（tag #開機）。若連 launcher.log 都沒有，代表多半在
    /// 「載入 .NET host / hostfxr / 原生相依」階段即失敗，請看 TryEmergencyWrite
    /// 落下的 %TEMP% 緊急檔案，或用同目錄的 diag_host_trace.cmd 取得 host 追蹤。
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
                AppLog.WriteLine("#開機", "========== 新工作階段 ==========");
                AppLog.WriteLine("#開機", "CLR: ModuleInitializer（LinLauncher 組件已載入，尚未 Main）");
                WriteEnvironmentSnapshot();
            }
            catch (Exception ex)
            {
                TryEmergencyWrite("ModuleInitializer: " + ex);
            }
        }

        internal static void Append(string line)
        {
            AppLog.WriteLine("#開機", line);
        }

        internal static void Append(string context, Exception? ex)
        {
            if (ex == null)
            {
                AppLog.WriteLine("#開機", context);
                return;
            }

            var sb = new StringBuilder();
            sb.AppendLine(context);
            sb.AppendLine(ex.ToString());
            if (ex.InnerException != null)
                sb.AppendLine("Inner: " + ex.InnerException);
            AppLog.WriteLine("#開機", sb.ToString().TrimEnd());
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
                AppLog.WriteLine("#開機", sb.ToString().TrimEnd());
            }
            catch (Exception ex)
            {
                AppLog.WriteLine("#開機", "環境快照寫入失敗: " + ex.Message);
            }
        }

        private static string SafeStr(string? s)
        {
            if (string.IsNullOrEmpty(s)) return "(null)";
            return s.Length > 2048 ? s.Substring(0, 2048) + "…" : s;
        }

        /// <summary>最後手段：連 AppLog 自己都寫失敗時，只寫固定的緊急路徑，避免
        /// ModuleInitializer 完全無法留下紀錄。刻意跟 launcher.log 分開，因為這是
        /// 「其他機制全部失敗」才會出現的極端情況，不算是要收斂的重複 log。</summary>
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
    }
}
