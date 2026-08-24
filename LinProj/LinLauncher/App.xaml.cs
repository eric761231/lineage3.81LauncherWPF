using System;
using System.Threading.Tasks;
using System.Windows;

namespace LinLauncher
{
    public partial class App : Application
    {
        protected override void OnStartup(StartupEventArgs e)
        {
            StartupLog.Append($"OnStartup: Args=[{string.Join(" | ", e.Args ?? Array.Empty<string>())}]");

            TaskScheduler.UnobservedTaskException += (_, ev) =>
            {
                try
                {
                    ev.SetObserved();
                    StartupLog.Append("UnobservedTaskException", ev.Exception);
                    ErrorLog.WriteException("UnobservedTaskException", "背景工作例外", ev.Exception);
                }
                catch { }
            };

            AppDomain.CurrentDomain.UnhandledException += (_, ex) =>
            {
                try
                {
                    if (ex.ExceptionObject is Exception uex)
                    {
                        StartupLog.Append("AppDomain UnhandledException", uex);
                        ErrorLog.WriteException("AppDomain.UnhandledException", "嚴重錯誤", uex);
                    }
                    else
                    {
                        string t = "AppDomain UnhandledException (非 Exception): " + ex.ExceptionObject;
                        StartupLog.Append(t);
                        ErrorLog.Write("AppDomain.UnhandledException", "嚴重錯誤", t, null);
                    }
                }
                catch { }
                try
                {
                    string fatal = "Fatal Error: " + ex.ExceptionObject;
                    ErrorLog.LogMessageBox("Launcher Crash", fatal, "AppDomain.FatalMessageBox");
                    MessageBox.Show(fatal, "Launcher Crash");
                }
                catch { }
            };

            base.OnStartup(e);

            try
            {
                StartupLog.Append("建立 MainWindow…");
                var main = new MainWindow();
                MainWindow = main;
                main.Show();
                StartupLog.Append("MainWindow.Show() 完成");
            }
            catch (Exception ex)
            {
                StartupLog.Append("MainWindow 建立或顯示失敗", ex);
                ErrorLog.WriteException("OnStartup", "無法啟動主視窗", ex);
                try
                {
                    string msg =
                        "無法啟動登入器主視窗。詳情已寫入日誌（launcher.log，或 %TEMP%\\LinLauncher_startup.log 等緊急備援位置）。\n\n"
                        + "若完全沒有任何日誌檔，請改以同目錄的 diag_host_trace.cmd 啟動以取得 host 追蹤。\n\n"
                        + ex.Message;
                    ErrorLog.LogMessageBox("LinLauncher", msg, "OnStartup.MainWindow");
                    MessageBox.Show(
                        msg,
                        "LinLauncher",
                        MessageBoxButton.OK,
                        MessageBoxImage.Error);
                }
                catch { }
                Shutdown(1);
            }
        }

        private void Application_DispatcherUnhandledException(object sender, System.Windows.Threading.DispatcherUnhandledExceptionEventArgs e)
        {
            StartupLog.Append("DispatcherUnhandledException", e.Exception);
            ErrorLog.WriteException("DispatcherUnhandledException", "UI 執行緒例外", e.Exception);
            try
            {
                string ui = "UI Error: " + e.Exception;
                ErrorLog.LogMessageBox("Launcher UI Crash", ui, "DispatcherUnhandledException.MessageBox");
                MessageBox.Show(ui, "Launcher UI Crash");
            }
            catch { }
            e.Handled = true;
        }
    }
}
