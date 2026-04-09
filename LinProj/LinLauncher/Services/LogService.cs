// LogService.cs: 簡易檔案日誌服務，輸出至執行目錄下的 launcher.log。
using System;
using System.IO;

namespace LinLauncher.Services
{
    public static class LogService
    {
        private static readonly object _lock = new object();
        private static readonly string _logPath;

        static LogService()
        {
            string dir = AppDomain.CurrentDomain.BaseDirectory;
            _logPath = Path.Combine(dir, "launcher.log");
        }

        public static void Info(string message) => Write("INFO", message);
        public static void Warn(string message) => Write("WARN", message);
        public static void Error(string message) => Write("ERROR", message);
        public static void Error(string message, Exception ex) => Write("ERROR", $"{message}\n  Exception: {ex.GetType().Name}: {ex.Message}\n  StackTrace: {ex.StackTrace}");

        private static void Write(string level, string message)
        {
            try
            {
                string line = $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] [{level}] {message}";
                lock (_lock)
                {
                    File.AppendAllText(_logPath, line + Environment.NewLine);
                }
                System.Diagnostics.Debug.WriteLine(line);
            }
            catch
            {
                // 日誌寫入失敗不應影響主程式
            }
        }
    }
}
