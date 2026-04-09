using System.Windows;
using System.Windows.Threading;
using LinEncoder.Services;

namespace LinEncoder
{
    public partial class App : Application
    {
        protected override void OnStartup(StartupEventArgs e)
        {
            LogService.Info("=== Application Startup ===");
            this.DispatcherUnhandledException += App_DispatcherUnhandledException;
            base.OnStartup(e);
        }

        private void App_DispatcherUnhandledException(object sender, DispatcherUnhandledExceptionEventArgs e)
        {
            LogService.Error("全域未處理異常捕獲", e.Exception);
            
            MessageBox.Show($"程式發生未預期的錯誤，即將關閉：\n\n{e.Exception.Message}\n\n詳細資訊已記錄於 LinEncoder.log", 
                "啟動失敗", MessageBoxButton.OK, MessageBoxImage.Error);
            
            e.Handled = true;
            Application.Current.Shutdown();
        }
    }
}
