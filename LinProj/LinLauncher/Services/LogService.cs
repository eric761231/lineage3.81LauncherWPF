// LogService.cs: 一般執行期 Info/Warn/Error，實際寫檔透過 AppLog（Core\launcher.log）。
using System;

namespace LinLauncher.Services
{
    public static class LogService
    {
        public static void Info(string message) => LinLauncher.AppLog.WriteLine("#資訊", message);
        public static void Warn(string message) => LinLauncher.AppLog.WriteLine("#警告", message);
        public static void Error(string message) => LinLauncher.AppLog.WriteLine("#錯誤", message);
        public static void Error(string message, Exception ex) =>
            LinLauncher.AppLog.WriteLine("#錯誤", $"{message}\n  Exception: {ex.GetType().Name}: {ex.Message}\n  StackTrace: {ex.StackTrace}");
    }
}
