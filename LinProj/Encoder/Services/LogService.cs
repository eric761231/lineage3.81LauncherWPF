using System;
using System.IO;
using System.Text;

namespace LinEncoder.Services
{
    /// <summary>
    /// 提供應用程式日誌寫入與操作摘要記錄之服務類別。
    /// </summary>
    public static class LogService
    {
        private static readonly string _logDir = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "log");
        private static readonly string _logPath = Path.Combine(_logDir, "LinEncoder.log");

        /// <summary>
        /// 寫入一般資訊 (INFO) 日誌。
        /// </summary>
        /// <param name="message">日誌訊息</param>
        public static void Info(string message) => Write("INFO", message);

        /// <summary>
        /// 寫入錯誤資訊 (ERROR) 日誌與例外堆疊細節。
        /// </summary>
        /// <param name="message">錯誤描述</param>
        /// <param name="ex">例外物件 (可選)</param>
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

        /// <summary>
        /// 寫入格式化日誌訊息至檔案。
        /// </summary>
        /// <param name="level">日誌等級</param>
        /// <param name="message">日誌訊息</param>
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
        /// <param name="action">操作動作名稱</param>
        /// <param name="whatHappened">已完成內容描述</param>
        /// <param name="nextSteps">後續建議步驟描述</param>
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
