using System.Windows;
using LinEncoder.ViewModels;

namespace LinEncoder
{
    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            InitializeComponent();
        }

        // PasswordBox 不能資料綁定，初始值要在 DataContext 就緒後手動從 ViewModel 帶進來。
        private void Window_Loaded(object sender, RoutedEventArgs e)
        {
            if (DataContext is EncoderViewModel vm)
                FtpPasswordBox.Password = vm.FtpPassword;
        }

        private void Window_Closing(object sender, System.ComponentModel.CancelEventArgs e)
        {
            if (DataContext is EncoderViewModel vm)
                vm.PersistSettings();
        }

        private void TitleBar_MouseDown(object sender, System.Windows.Input.MouseButtonEventArgs e)
        {
            if (e.ChangedButton == System.Windows.Input.MouseButton.Left)
                this.DragMove();
        }

        private void Minimize_Click(object sender, RoutedEventArgs e)
        {
            this.WindowState = WindowState.Minimized;
        }

        private void Close_Click(object sender, RoutedEventArgs e)
        {
            this.Close();
        }

        private void ComboBox_SelectionChanged(object sender, System.Windows.Controls.SelectionChangedEventArgs e)
        {

        }

        private void Button_Click(object sender, RoutedEventArgs e)
        {

        }

        private void Button_Click_1(object sender, RoutedEventArgs e)
        {

        }

        private void PatchSourceDir_LostFocus(object sender, RoutedEventArgs e)
        {
            if (DataContext is EncoderViewModel vm)
                vm.RefreshPatchSourcePreview();
        }

        // PasswordBox.Password 基於安全考量無法直接資料綁定，這裡用 code-behind 手動同步回 ViewModel。
        private void FtpPassword_PasswordChanged(object sender, RoutedEventArgs e)
        {
            if (DataContext is EncoderViewModel vm && sender is System.Windows.Controls.PasswordBox pb)
                vm.FtpPassword = pb.Password;
        }
    }
}
