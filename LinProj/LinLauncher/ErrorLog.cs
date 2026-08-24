using System;
using System.Text;

namespace LinLauncher
{
    /// <summary>
    /// 使用者可見的錯誤／警告（MessageBox）與未處理例外，寫入 Core\launcher.log
    /// （透過 AppLog），避免因視窗被擋、一瞬間消失或遠端桌面看不到而無法除錯。
    /// </summary>
    internal static class ErrorLog
    {
        /// <summary>記錄例外（含堆疊）。</summary>
        public static void WriteException(string source, string title, Exception ex)
        {
            Write(source, title, ex.Message, ex);
        }

        /// <summary>記錄一般說明文字（無例外）。</summary>
        public static void Write(string source, string title, string message, Exception? ex = null)
        {
            var sb = new StringBuilder();
            sb.AppendLine($"source={source} title={title}");
            sb.AppendLine(message);
            if (ex != null)
            {
                sb.AppendLine(ex.ToString());
                if (ex.InnerException != null)
                    sb.AppendLine("Inner: " + ex.InnerException);
            }
            AppLog.WriteLine("#例外", sb.ToString().TrimEnd());
        }

        /// <summary>與 MessageBox 對應的一筆紀錄（標題 + 內文）。</summary>
        public static void LogMessageBox(string title, string message, string source = "MessageBox")
        {
            AppLog.WriteLine("#訊息框", $"source={source} title={title}\n{message}");
        }
    }
}
