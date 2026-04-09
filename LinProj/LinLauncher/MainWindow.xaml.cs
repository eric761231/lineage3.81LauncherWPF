// MainWindow.xaml.cs: 包含 WebView2 初始化與視窗基本控制邏輯。
using System;
using System.Windows;
using System.Windows.Input;
using LinLauncher.ViewModels;

namespace LinLauncher
{
    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            InitializeComponent();
            var vm = new MainViewModel();
            this.DataContext = vm;

            // Apply custom dimensions from config
            if (vm.Config.Width > 0) this.Width = vm.Config.Width;
            if (vm.Config.Height > 0) this.Height = vm.Config.Height;

            InitializeWebView();
        }
        protected override void OnSourceInitialized(EventArgs e)
        {
            base.OnSourceInitialized(e);
            if (DataContext is MainViewModel vm)
            {
                if (vm.Config.Width > 400 && vm.Config.Width < 4000) this.Width = vm.Config.Width;
                if (vm.Config.Height > 300 && vm.Config.Height < 4000) this.Height = vm.Config.Height;
                UpdateLayout();
            }
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