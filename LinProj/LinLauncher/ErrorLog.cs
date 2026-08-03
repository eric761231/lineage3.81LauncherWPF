using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace LinLauncher
{
    /// <summary>
    /// 使用者可見的錯誤／警告（MessageBox）與未處理例外，寫入 <c>LinLauncher_errors.log</c>，
    /// 避免因視窗被擋、一瞬間消失或遠端桌面看不到而無法除錯。
    /// </summary>
    internal static class ErrorLog
    {
        private static readonly object Sync = new();

        /// <summary>記錄例外（含堆疊）。</summary>
        public static void WriteException(string source, string title, Exception ex)
        {
            Write(source, title, ex.Message, ex);
        }

        /// <summary>記錄一般說明文字（無例外）。</summary>
        public static void Write(string source, string title, string message, Exception? ex = null)
        {
            var sb = new StringBuilder();
            sb.AppendLine($"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] source={source} title={title}");
            sb.AppendLine(message);
            if (ex != null)
            {
                sb.AppendLine(ex.ToString());
                if (ex.InnerException != null)
                    sb.AppendLine("Inner: " + ex.InnerException);
            }
            sb.AppendLine("---");
            AppendText(sb.ToString());
        }

        /// <summary>與 MessageBox 對應的一筆紀錄（標題 + 內文）。</summary>
        public static void LogMessageBox(string title, string message, string source = "MessageBox")
        {
            Write(source, title, message, null);
        }

        private static void AppendText(string text)
        {
            lock (Sync)
            {
                foreach (string path in GetErrorLogPaths())
                {
                    try
                    {
                        string? dir = Path.GetDirectoryName(path);
                        if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
                            Directory.CreateDirectory(dir);
                        File.AppendAllText(path, text, Encoding.UTF8);
                    }
                    catch
                    {
                        // 嘗試下一個路徑
                    }
                }
            }
        }

        private static IEnumerable<string> GetErrorLogPaths()
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

            const string fileName = "LinLauncher_errors.log";

            try
            {
                string? ep = Environment.ProcessPath;
                if (!string.IsNullOrEmpty(ep))
                {
                    string? dir = Path.GetDirectoryName(ep);
                    if (!string.IsNullOrEmpty(dir))
                        Add(Path.Combine(dir, fileName));
                }
            }
            catch { }

            try
            {
                string baseDir = AppDomain.CurrentDomain.BaseDirectory;
                if (!string.IsNullOrEmpty(baseDir))
                {
                    string trimmed = baseDir.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
                    Add(Path.Combine(trimmed, fileName));
                }
            }
            catch { }

            Add(Path.Combine(Path.GetTempPath(), fileName));

            try
            {
                string folder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "LinLauncher");
                Add(Path.Combine(folder, "errors.log"));
            }
            catch { }

            try
            {
                Add(Path.Combine(Environment.CurrentDirectory, fileName));
            }
            catch { }

            return list;
        }
    }
}
