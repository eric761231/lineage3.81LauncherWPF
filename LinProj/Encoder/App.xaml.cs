using System.Windows;
using System.Windows.Threading;
using LinEncoder.Services;

namespace LinEncoder
{
    /// <summary>
    /// 應用程式進入點與全域生命週期管理類別。
    /// </summary>
    public partial class App : Application
    {
        /// <summary>
        /// 應用程式啟動時呼叫，註冊全域未處理例外處理常式與寫入啟動日誌。
        /// </summary>
        /// <param name="e">啟動事件參數</param>
        protected override void OnStartup(StartupEventArgs e)
        {
            LogService.Info("=== Application Startup ===");
            this.DispatcherUnhandledException += App_DispatcherUnhandledException;
            base.OnStartup(e);
        }

        /// <summary>
        /// 捕獲 Dispatcher 全域未處理之異常，記錄錯誤資訊並顯示提示訊息後關閉應用程式。
        /// </summary>
        /// <param name="sender">事件來源物件</param>
        /// <param name="e">未處理例外事件參數</param>
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
