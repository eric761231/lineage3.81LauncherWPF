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
            StartupLog.Append("MainWindow: 建構函式開始");
            try
            {
                InitializeComponent();
                StartupLog.Append("MainWindow: InitializeComponent 完成");
            }
            catch (Exception ex)
            {
                StartupLog.Append("MainWindow: InitializeComponent 失敗", ex);
                throw;
            }

            MainViewModel vm;
            try
            {
                vm = new MainViewModel();
                this.DataContext = vm;
                StartupLog.Append("MainWindow: MainViewModel 已建立");
            }
            catch (Exception ex)
            {
                StartupLog.Append("MainWindow: MainViewModel 失敗", ex);
                throw;
            }

            ApplyWindowSizeFromConfig(vm);
            Visibility = Visibility.Visible;
            WindowState = WindowState.Normal;

            Loaded += MainWindow_Loaded;
            InitializeWebView(vm);
        }

        private void MainWindow_Loaded(object sender, RoutedEventArgs e)
        {
            Loaded -= MainWindow_Loaded;
            try
            {
                Activate();
                Focus();
                StartupLog.Append("MainWindow: Loaded — Activate/Focus 已呼叫");
            }
            catch (Exception ex)
            {
                StartupLog.Append("MainWindow: Loaded 時 Activate 失敗", ex);
            }
        }

        private void ApplyWindowSizeFromConfig(MainViewModel vm)
        {
            int w = vm.Config.Width;
            int h = vm.Config.Height;
            if (w >= 400 && w <= 4000) Width = w;
            if (h >= 300 && h <= 4000) Height = h;
        }

        protected override void OnSourceInitialized(EventArgs e)
        {
            base.OnSourceInitialized(e);
            StartupLog.Append("MainWindow: OnSourceInitialized");
            if (DataContext is MainViewModel vm)
            {
                ApplyWindowSizeFromConfig(vm);
                UpdateLayout();
            }
        }

        private async void InitializeWebView(MainViewModel vm)
        {
            StartupLog.Append("MainWindow: WebView2 初始化開始");
            try
            {
                await webView.EnsureCoreWebView2Async(null);
                string rawWeb = vm.Config.Web?.Trim() ?? "";
                if (string.IsNullOrWhiteSpace(rawWeb))
                {
                    StartupLog.Append("MainWindow: Config.Web 為空或無效，WebView2 使用 about:blank。");
                    webView.Source = new Uri("about:blank");
                }
                else if (Uri.TryCreate(rawWeb, UriKind.Absolute, out var uri) &&
                    (uri.Scheme == Uri.UriSchemeHttp || uri.Scheme == Uri.UriSchemeHttps))
                {
                    webView.Source = uri;
                }
                else
                {
                    StartupLog.Append($"MainWindow: Config.Web 非合法 http(s) URL（長度={rawWeb.Length}），改為 about:blank。");
                    webView.Source = new Uri("about:blank");
                }

                StartupLog.Append("MainWindow: WebView2 初始化成功");
            }
            catch (Exception ex)
            {
                StartupLog.Append("MainWindow: WebView2 初始化失敗", ex);
                try
                {
                    MessageBox.Show(
                        "WebView2 初始化失敗（可能未安裝 WebView2 Runtime）。\n\n" + ex.Message + "\n\n其餘登入功能仍可使用。",
                        "LinLauncher",
                        MessageBoxButton.OK,
                        MessageBoxImage.Warning);
                }
                catch { }
            }
        }

        private void TitleBar_MouseDown(object sender, MouseButtonEventArgs e)
        {
            if (e.ChangedButton == MouseButton.Left) this.DragMove();
        }

        private void Minimize_Click(object sender, RoutedEventArgs e) => this.WindowState = WindowState.Minimized;

        private void Close_Click(object sender, RoutedEventArgs e) => this.Close();
    }
}
