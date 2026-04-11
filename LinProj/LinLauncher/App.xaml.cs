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
                }
                catch { }
            };

            AppDomain.CurrentDomain.UnhandledException += (_, ex) =>
            {
                try
                {
                    if (ex.ExceptionObject is Exception uex)
                        StartupLog.Append("AppDomain UnhandledException", uex);
                    else
                        StartupLog.Append("AppDomain UnhandledException (非 Exception): " + ex.ExceptionObject);
                }
                catch { }
                try
                {
                    MessageBox.Show("Fatal Error: " + ex.ExceptionObject, "Launcher Crash");
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
                try
                {
                    MessageBox.Show(
                        "無法啟動登入器主視窗。詳情已寫入日誌（LinLauncher_boot.log、%TEMP%\\LinLauncher_startup.log 等）。\n\n"
                        + "若完全沒有任何日誌檔，請改以同目錄的 diag_host_trace.cmd 啟動以取得 host 追蹤。\n\n"
                        + ex.Message,
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
            try
            {
                MessageBox.Show("UI Error: " + e.Exception, "Launcher UI Crash");
            }
            catch { }
            e.Handled = true;
        }
    }
}
