// MainWindow.xaml.cs: 包含 WebView2 初始化與視窗基本控制邏輯。
using System;
using System.Windows;
using System.Windows.Input;

namespace LinLauncher
{
    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            InitializeComponent();
            InitializeWebView();
        }
        private async void InitializeWebView()
        {
            try { await webView.EnsureCoreWebView2Async(null); }
            catch (Exception ex) { MessageBox.Show($"WebView2 Error: {ex.Message}"); }
        }
        private void TitleBar_MouseDown(object sender, MouseButtonEventArgs e)
        {
            if (e.ChangedButton == MouseButton.Left) this.DragMove();
        }
        private void Minimize_Click(object sender, RoutedEventArgs e) => this.WindowState = WindowState.Minimized;
        private void Close_Click(object sender, RoutedEventArgs e) => this.Close();
    }
}