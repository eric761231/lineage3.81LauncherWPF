using System;
using System.IO;
using System.Text;

namespace LinEncoder.Services
{
    public static class LogService
    {
        private static string _logPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "LinEncoder.log");

        public static void Info(string message) => Write("INFO", message);
        public static void Error(string message, Exception? ex = null)
        {
            var sb = new StringBuilder();
            sb.AppendLine(message);
            if (ex != null)
            {
                sb.AppendLine($"[Exception] {ex.GetType().Name}: {ex.Message}");
                sb.AppendLine($"[StackTrace] {ex.StackTrace}");
                if (ex.InnerException != null)
                {
                    sb.AppendLine("--- Inner Exception ---");
                    sb.AppendLine($"{ex.InnerException.GetType().Name}: {ex.InnerException.Message}");
                    sb.AppendLine(ex.InnerException.StackTrace);
                }
            }
            Write("ERROR", sb.ToString());
        }

        private static void Write(string level, string message)
        {
            try
            {
                string logLine = $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] [{level}] {message}{Environment.NewLine}";
                File.AppendAllText(_logPath, logLine, Encoding.UTF8);
            }
            catch { /* 忽略日誌寫入錯誤以免造成二次崩潰 */ }
        }
    }
}
