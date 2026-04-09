// MainViewModel.cs: 主視窗的 ViewModel，處理伺服器清單、更新與啟動邏輯。
using System;
using System.Collections.ObjectModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Input;
using LinLauncher.Models;
using LinLauncher.Services;

namespace LinLauncher.ViewModels
{
    public class MainViewModel : BaseViewModel
    {
        private readonly ServerService _serverService = new ServerService();
        private readonly UpdateService _updateService = new UpdateService();
        private readonly LaunchService _launchService = new LaunchService();

        private ObservableCollection<ServerInfo> _servers = new ObservableCollection<ServerInfo>();
        private ServerInfo? _selectedServer;
        private string _statusText = "Ready";
        private int _overallProgress = 0;
        private bool _isBusy = false;

        public ObservableCollection<ServerInfo> Servers { get => _servers; set { _servers = value; OnPropertyChanged(); } }
        public ServerInfo? SelectedServer 
        { 
            get => _selectedServer; 
            set { _selectedServer = value; OnPropertyChanged(); ((RelayCommand)StartCommand).CanExecute(null); } 
        }

        public string StatusText { get => _statusText; set { _statusText = value; OnPropertyChanged(); } }
        public int OverallProgress { get => _overallProgress; set { _overallProgress = value; OnPropertyChanged(); } }
        public bool IsBusy { get => _isBusy; set { _isBusy = value; OnPropertyChanged(); } }

        public ICommand StartCommand { get; }
        private readonly LauncherConfig _config = new LauncherConfig();
        public LauncherConfig Config => _config;

        public MainViewModel()
        {
            // 初始化指令與事件訂閱。 (Initialize commands and event subscriptions.)
            StartCommand = new RelayCommand(async _ => await StartGameAsync(), _ => SelectedServer != null && !IsBusy);
            _updateService.ProgressChanged += (s, e) => { OverallProgress = e.OverallPercent; StatusText = $"Updating {e.CurrentFile} ({e.FilePercent}%)"; };
            _launchService.GameExited += (s, e) => { IsBusy = false; StatusText = "Ready"; };
            
            // 從 EXE 資源中讀取並解析組態。 (Read and parse config from EXE resources.)
            LoadConfigFromResource();
            _ = InitializeAsync();
        }

        /// <summary>
        /// 從 Win32 資源 (Resource 101, Type CONFIG) 讀取、解密並結構化組態資料。
        /// (Reads, decrypts, and marshals config data from Win32 resources.)
        /// </summary>
        private void LoadConfigFromResource()
        {
            var reader = new ResourceReaderService();
            byte[]? data = reader.ReadConfig();
            if (data != null && data.Length >= System.Runtime.InteropServices.Marshal.SizeOf(typeof(LauncherConfig)))
            {
                // The correct key is embedded in the struct header at offset 10 (8 bytes sign + 1 byte enc + 1 byte cfg).
                byte[] key = new byte[16];
                Array.Copy(data, 10, key, 0, 16);

                // We must decrypt ONLY the payload (everything after the 26 byte header)
                int headerSize = 26;
                int payloadSize = data.Length - headerSize;
                byte[] payload = new byte[payloadSize];
                Array.Copy(data, headerSize, payload, 0, payloadSize);

                CryptoService.ConfigDecrypt(key, payload);
                
                // Copy decrypted payload back into data
                Array.Copy(payload, 0, data, headerSize, payloadSize);
                
                IntPtr ptr = System.Runtime.InteropServices.Marshal.AllocHGlobal(data.Length);
                try
                {
                    System.Runtime.InteropServices.Marshal.Copy(data, 0, ptr, data.Length);
                    LauncherConfig? loaded = System.Runtime.InteropServices.Marshal.PtrToStructure(ptr, typeof(LauncherConfig)) as LauncherConfig;
                    if (loaded != null && loaded.Configed)
                    {
                        _config.Title = loaded.Title;
                            _config.Web = loaded.Web;
                            _config.List = loaded.List;
                            _config.UseUpdate = loaded.UseUpdate;
                            _config.Update = loaded.Update;
                            _config.Width = loaded.Width;
                            _config.Height = loaded.Height;
                    }
                }
                catch (Exception ex)
                {
                    System.Diagnostics.Debug.WriteLine($"Config Marshal Error: {ex.Message}");
                }
                finally { System.Runtime.InteropServices.Marshal.FreeHGlobal(ptr); }
            }
        }

        /// <summary>
        /// 異步初始化：載入伺服器列表 -> 檢查更新。
        /// (Async init: Load server list -> Check updates.)
        /// </summary>
        private async Task InitializeAsync()
        {
            IsBusy = true;
            
            // 1. 自動更新檢查 (Automated Update Check)
            if (Config.UseUpdate && !string.IsNullOrWhiteSpace(Config.Update))
            {
                StatusText = "Checking for updates...";
                try
                {
                    string tempFile = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "update.tmp");
                    using (var hc = new System.Net.Http.HttpClient())
                    {
                        var resp = await hc.GetAsync(Config.Update);
                        if (resp.IsSuccessStatusCode)
                        {
                            var bytes = await resp.Content.ReadAsByteArrayAsync();
                            await File.WriteAllBytesAsync(tempFile, bytes);
                            
                            var (allFiles, baseUrl) = await _updateService.LoadUpdateListAsync(tempFile);
                            if (allFiles.Count > 0)
                            {
                                var needUpdate = await _updateService.CheckFilesAsync(allFiles, AppDomain.CurrentDomain.BaseDirectory);
                                if (needUpdate.Count > 0)
                                {
                                    StatusText = $"Downloading {needUpdate.Count} updates...";
                                    bool success = await _updateService.DownloadUpdatesAsync(needUpdate, baseUrl, AppDomain.CurrentDomain.BaseDirectory);
                                    if (success)
                                    {
                                        MessageBox.Show("更新下載完成。請重新啟動登入器以套用變更。", "更新提示", MessageBoxButton.OK, MessageBoxImage.Information);
                                        // Application.Current.Shutdown(); // 選擇性：自動關閉
                                    }
                                }
                            }
                            File.Delete(tempFile);
                        }
                    }
                }
                catch (Exception ex)
                {
                    System.Diagnostics.Debug.WriteLine($"Update Error: {ex.Message}");
                }
            }

            // 2. 載入伺服器列表 (Load Server List)
            StatusText = "Loading server list...";
            if (string.IsNullOrWhiteSpace(Config.List))
            {
                StatusText = "Error: Server list URL is empty.";
                IsBusy = false;
                return;
            }

            try {
                var servers = await _serverService.LoadServerListAsync(Config.List);
                Servers.Clear();
                foreach (var s in servers) Servers.Add(s);
                if (Servers.Count > 0) SelectedServer = Servers[0];
                StatusText = "Ready";
            } catch (Exception ex) {
                StatusText = "Error loading servers.";
                System.Diagnostics.Debug.WriteLine($"Init Error: {ex.Message}");
            }
            IsBusy = false;
        }

        /// <summary>
        /// 處理「開始遊戲」按鈕邏輯。包含自動路徑偵測、DLL 注入啟動。
        /// (Handles "Start Game" logic: auto path detection, DLL injection launch.)
        /// </summary>
        private async Task StartGameAsync()
        {
            if (SelectedServer == null) return;
            IsBusy = true;
            StatusText = "Launching game...";
            string appDir = AppDomain.CurrentDomain.BaseDirectory;
            string gameExe = Path.Combine(appDir, "TW13081901.bin");
            string dllPath = Path.Combine(appDir, "LauncherDll.dll");
            
            // Development fallback for DLL: if not in appDir, check project structure
            if (!File.Exists(dllPath))
            {
                string devPath = Path.GetFullPath(Path.Combine(appDir, @"..\..\..\..\LauncherDll\Debug\LauncherDll.dll"));
                if (File.Exists(devPath)) dllPath = devPath;
                else
                {
                    devPath = Path.GetFullPath(Path.Combine(appDir, @"..\..\..\..\LauncherDll\Release\LauncherDll.dll"));
                    if (File.Exists(devPath)) dllPath = devPath;
                }
            }

            // Development fallback for Game: if not in appDir, check Client folder
            if (!File.Exists(gameExe))
            {
                string clientPath = Path.GetFullPath(Path.Combine(appDir, @"..\..\..\..\Client\TW13081901.bin"));
                if (File.Exists(clientPath)) gameExe = clientPath;
            }

            if (!File.Exists(gameExe)) 
            { 
                System.Windows.MessageBox.Show($"Game executable (TW13081901.bin) not found at: {gameExe}\nPlease make sure 'TW13081901.bin' is in the application folder or a 'Client' subfolder."); 
                IsBusy = false; StatusText = "Ready"; return; 
            }
            if (!File.Exists(dllPath))
            {
                System.Windows.MessageBox.Show($"LauncherDll.dll not found at: {dllPath}\nPlease check the build output or path logic.");
                IsBusy = false; StatusText = "Ready"; return;
            }
            bool launched = await _launchService.LaunchGameAsync(SelectedServer, gameExe, dllPath);
            if (launched) StatusText = "Game Running...";
            else { StatusText = "Launch failed!"; IsBusy = false; }
        }
    }
}
