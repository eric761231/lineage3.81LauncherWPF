using System.Windows;
using LinEncoder.ViewModels;

namespace LinEncoder
{
    /// <summary>
    /// 主視窗 後端 UI 邏輯類別。
    /// </summary>
    public partial class MainWindow : Window
    {
        /// <summary>
        /// 建構函式，初始化 UI 組件。
        /// </summary>
        public MainWindow()
        {
            InitializeComponent();
        }

        /// <summary>
        /// 視窗載入事件處理函式：初始化密碼欄位與註冊批次上傳日誌事件。
        /// </summary>
        /// <param name="sender">事件來源</param>
        /// <param name="e">事件參數</param>
        // PasswordBox 不能資料綁定，初始值要在 DataContext 就緒後手動從 ViewModel 帶進來。
        private void Window_Loaded(object sender, RoutedEventArgs e)
        {
            if (DataContext is EncoderViewModel vm)
            {
                FtpPasswordBox.Password = vm.FtpPassword;

                // 通訊結果顯示區改用「事件 + 批次 AppendText」，不是綁定一個越長越大的
                // 字串屬性再整個 Text 換值：ViewModel 端有一個 DispatcherTimer 每 150ms
                // 把佇列裡累積的訊息合併成一段文字才觸發這個事件，這裡收到就是一次性的
                // 一整段（可能好幾行），直接 AppendText 一次即可。詳見
                // EncoderViewModel 裡 _uploadLogQueue/_uploadLogFlushTimer 的說明。
                vm.UploadLogBatchReady += batch => UploadLogBox.AppendText(batch);
                vm.UploadLogCleared += () => UploadLogBox.Clear();
            }
        }

        /// <summary>
        /// 視窗關閉事件處理函式：持久化 ViewModel 設定。
        /// </summary>
        /// <param name="sender">事件來源</param>
        /// <param name="e">事件參數</param>
        private void Window_Closing(object sender, System.ComponentModel.CancelEventArgs e)
        {
            if (DataContext is EncoderViewModel vm)
            {
                vm.PersistSettings();
            }
        }

        /// <summary>
        /// 視窗標題列滑鼠按下事件處理函式：實現視窗拖曳功能。
        /// </summary>
        /// <param name="sender">事件來源</param>
        /// <param name="e">滑鼠事件參數</param>
        private void TitleBar_MouseDown(object sender, System.Windows.Input.MouseButtonEventArgs e)
        {
            if (e.ChangedButton == System.Windows.Input.MouseButton.Left)
            {
                this.DragMove();
            }
        }

        /// <summary>
        /// 最小化按鈕點擊事件處理函式。
        /// </summary>
        /// <param name="sender">事件來源</param>
        /// <param name="e">事件參數</param>
        private void Minimize_Click(object sender, RoutedEventArgs e)
        {
            this.WindowState = WindowState.Minimized;
        }

        /// <summary>
        /// 關閉按鈕點擊事件處理函式。
        /// </summary>
        /// <param name="sender">事件來源</param>
        /// <param name="e">事件參數</param>
        private void Close_Click(object sender, RoutedEventArgs e)
        {
            this.Close();
        }

        /// <summary>
        /// 下拉式選單選擇改變事件處理函式。
        /// </summary>
        /// <param name="sender">事件來源</param>
        /// <param name="e">選擇改變事件參數</param>
        private void ComboBox_SelectionChanged(object sender, System.Windows.Controls.SelectionChangedEventArgs e)
        {

        }

        /// <summary>
        /// 通用按鈕點擊事件處理函式。
        /// </summary>
        /// <param name="sender">事件來源</param>
        /// <param name="e">事件參數</param>
        private void Button_Click(object sender, RoutedEventArgs e)
        {

        }

        /// <summary>
        /// 通用按鈕點擊事件處理函式 1。
        /// </summary>
        /// <param name="sender">事件來源</param>
        /// <param name="e">事件參數</param>
        private void Button_Click_1(object sender, RoutedEventArgs e)
        {

        }

        /// <summary>
        /// 補丁來源目錄輸入框失去焦點事件處理函式：刷新補丁來源預覽。
        /// </summary>
        /// <param name="sender">事件來源</param>
        /// <param name="e">事件參數</param>
        private void PatchSourceDir_LostFocus(object sender, RoutedEventArgs e)
        {
            if (DataContext is EncoderViewModel vm)
            {
                vm.RefreshPatchSourcePreview();
            }
        }

        /// <summary>
        /// FTP 密碼變更事件處理函式：手動同步 PasswordBox 文字回 ViewModel。
        /// </summary>
        /// <param name="sender">事件來源</param>
        /// <param name="e">事件參數</param>
        // PasswordBox.Password 基於安全考量無法直接資料綁定，這裡用 code-behind 手動同步回 ViewModel。
        private void FtpPassword_PasswordChanged(object sender, RoutedEventArgs e)
        {
            if (DataContext is EncoderViewModel vm && sender is System.Windows.Controls.PasswordBox pb)
            {
                vm.FtpPassword = pb.Password;
            }
        }

        /// <summary>
        /// 補丁上傳日誌輸入框文字改變事件處理函式：自動捲動至底端。
        /// </summary>
        /// <param name="sender">事件來源</param>
        /// <param name="e">事件參數</param>
        // 補丁上傳頁的通訊結果顯示區：新訊息進來就自動捲到最下面，不用手動拉捲軸。
        private void UploadLogBox_TextChanged(object sender, System.Windows.Controls.TextChangedEventArgs e)
        {
            if (sender is System.Windows.Controls.TextBox tb)
            {
                tb.ScrollToEnd();
            }
        }
    }
}
