using System;
using System.IO;
using System.Text;

namespace LinEncoder.Services
{
    public static class LogService
    {
        private static readonly string _logDir = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "log");
        private static readonly string _logPath = Path.Combine(_logDir, "LinEncoder.log");

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
                Directory.CreateDirectory(_logDir);
                string logLine = $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] [{level}] {message}{Environment.NewLine}";
                File.AppendAllText(_logPath, logLine, Encoding.UTF8);
            }
            catch { /* 忽略日誌寫入錯誤以免造成二次崩潰 */ }
        }

        /// <summary>
        /// 寫操作摘要（做了什麼／接下來要做什麼），每次呼叫都是新增一筆帶時間戳記的紀錄、
        /// 附加在檔案最後——不覆蓋，保留完整歷史，方便回頭查之前每次操作實際做了什麼。
        /// </summary>
        public static void WriteOperationSummary(string action, string whatHappened, string nextSteps)
        {
            try
            {
                Directory.CreateDirectory(_logDir);
                string path = Path.Combine(_logDir, "操作摘要.md");
                bool isNewFile = !File.Exists(path);
                string entry = $"## {DateTime.Now:yyyy-MM-dd HH:mm:ss}　{action}\n\n" +
                    $"### 做了什麼\n{whatHappened}\n\n" +
                    $"### 接下來要做什麼\n{nextSteps}\n\n" +
                    "---\n\n";
                if (isNewFile)
                {
                    entry = "# 操作摘要\n\n" + entry;
                }
                File.AppendAllText(path, entry, Encoding.UTF8);
            }
            catch { /* 忽略摘要寫入錯誤以免影響主流程 */ }
        }
    }
}
