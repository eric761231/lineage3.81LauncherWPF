// MainViewModel.cs: 主視窗的 ViewModel，處理伺服器清單、更新與啟動邏輯。
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Input;
using System.Windows.Threading;
using LinLauncher.Models;
using LinLauncher.Services;
using LinLauncher;

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

        private CancellationTokenSource? _serverProbeCts;
        private int _serverProbeSeq;
        private bool _serverProbePending;
        private bool _serverReachable;
        private string _startButtonCaption = "遊戲開始";

        public ObservableCollection<ServerInfo> Servers { get => _servers; set { _servers = value; OnPropertyChanged(); } }
        public ServerInfo? SelectedServer
        {
            get => _selectedServer;
            set
            {
                if (_selectedServer == value) return;
                _selectedServer = value;
                OnPropertyChanged();
                CommandManager.InvalidateRequerySuggested();
                _ = RunServerProbeAsync();
            }
        }

        /// <summary>開始按鈕文字：檢查中／可玩／無法遊玩。</summary>
        public string StartButtonCaption
        {
            get => _startButtonCaption;
            private set { _startButtonCaption = value; OnPropertyChanged(); }
        }

        public string StatusText { get => _statusText; set { _statusText = value; OnPropertyChanged(); } }
        public int OverallProgress { get => _overallProgress; set { _overallProgress = value; OnPropertyChanged(); } }
        public bool IsBusy
        {
            get => _isBusy;
            set
            {
                _isBusy = value;
                OnPropertyChanged();
                CommandManager.InvalidateRequerySuggested();
            }
        }
        public ICommand StartCommand { get; }
        private readonly LauncherConfig _config = new LauncherConfig();
        public LauncherConfig Config => _config;

        public MainViewModel()
        {
            // 初始化指令與事件訂閱。 (Initialize commands and event subscriptions.)
            StartCommand = new RelayCommand(
                async _ => await StartGameAsync(),
                _ => SelectedServer != null && !IsBusy && !_serverProbePending && _serverReachable);
            _updateService.ProgressChanged += (s, e) => { OverallProgress = e.OverallPercent; StatusText = $"Updating {e.CurrentFile} ({e.FilePercent}%)"; };
            _launchService.GameExited += (s, e) => { IsBusy = false; StatusText = "Ready"; };
            
            try
            {
                LoadConfigFromResource();
                NormalizeConfigStrings();
                StartupLog.Append(
                    $"MainViewModel: 組態摘要 — List 有效 URL={IsValidHttpUrl(Config.List)}, Web 有效 URL={IsValidHttpUrl(Config.Web)}, Update 有效 URL={IsValidHttpUrl(Config.Update)}, UseUpdate={Config.UseUpdate}");
                if (Config.Configed && !IsValidHttpUrl(Config.List) && !IsValidHttpUrl(Config.Web))
                    StartupLog.Append("MainViewModel: 提示 config.dat 內嵌網址欄位非合法 http(s)。常見原因：① config.dat 與 LinLauncher 非同一版建置／未整包複製 publish；② 檔案損毀或長度不正確。請用 ConfigDatGen 依 LinEncoder.ini 重產，或於程式目錄／上一層放置 list.txt。");
            }
            catch (Exception ex)
            {
                StartupLog.Append("MainViewModel: LoadConfigFromResource 失敗", ex);
            }

            _ = InitializeAsync();
        }

        /// <summary>常見配置：清單放在 LinLauncher_Environment 或上一層遊戲根目錄（如 C:\3.81Lineage\）。</summary>
        private static IEnumerable<string> EnumerateCandidatePaths(string fileName)
        {
            var list = new List<string>();
            string baseDir = AppDomain.CurrentDomain.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            list.Add(Path.Combine(baseDir, fileName));
            try
            {
                var parent = Directory.GetParent(baseDir);
                if (parent != null)
                    list.Add(Path.Combine(parent.FullName, fileName));
            }
            catch { }
            return list;
        }

        private static string? FindFirstExistingLocalListPath()
        {
            foreach (string p in EnumerateCandidatePaths("list.txt"))
            {
                if (File.Exists(p)) return p;
            }
            return null;
        }

        /// <summary>修剪字串、截斷第一個 NUL，避免解密錯誤時整段亂碼進入 HttpClient。</summary>
        private static string SanitizeEmbeddedString(string? s)
        {
            if (string.IsNullOrEmpty(s)) return "";
            int nul = s.IndexOf('\0');
            if (nul >= 0) s = s.Substring(0, nul);
            s = s.Trim();
            return s;
        }

        private static bool IsValidHttpUrl(string? s)
        {
            if (string.IsNullOrWhiteSpace(s)) return false;
            s = s.Trim();
            if (s.Length > 2048) return false;
            foreach (char c in s)
            {
                if (char.IsControl(c)) return false;
            }
            if (!Uri.TryCreate(s, UriKind.Absolute, out var u)) return false;
            return u.Scheme == Uri.UriSchemeHttp || u.Scheme == Uri.UriSchemeHttps;
        }

        private void NormalizeConfigStrings()
        {
            _config.Title = SanitizeEmbeddedString(_config.Title);
            _config.Web = SanitizeEmbeddedString(_config.Web);
            _config.List = SanitizeEmbeddedString(_config.List);
            _config.Update = SanitizeEmbeddedString(_config.Update);
            if (!IsValidHttpUrl(_config.Web)) _config.Web = "";
            if (!IsValidHttpUrl(_config.List)) _config.List = "";
            if (!IsValidHttpUrl(_config.Update)) _config.Update = "";
            EnsureSafeWindowDimensions();
        }

        /// <summary>解密錯誤時寬高可能為 0 或極大，導致視窗不可見或超出螢幕。</summary>
        private void EnsureSafeWindowDimensions()
        {
            const int defW = 1000, defH = 600;
            if (_config.Width < 400 || _config.Width > 4000) _config.Width = defW;
            if (_config.Height < 300 || _config.Height > 4000) _config.Height = defH;
        }

        /// <summary>
        /// 從 Win32 資源 (Resource 101, Type CONFIG) 讀取、解密並結構化組態資料。
        /// (Reads, decrypts, and marshals config data from Win32 resources.)
        /// </summary>
        private void LoadConfigFromResource()
        {
            int structSize = Marshal.SizeOf(typeof(LauncherConfig));
            try
            {
                string path = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "config.dat");
                if (!File.Exists(path))
                    return;

                byte[] onDisk = File.ReadAllBytes(path);
                StartupLog.Append($"LoadConfig: config.dat 位元組長度={onDisk.Length}，LauncherConfig 結構大小={structSize}（須完全相等）。");
                if (onDisk.Length != structSize)
                {
                    StartupLog.Append(
                        "LoadConfig: 已略過此檔（長度不符無法安全解密）。常見原因：曾執行外層 LinLauncher.dat（Proxy），會用包裝 exe 尾端覆寫 config.dat，長度常變成非 "
                        + structSize
                        + "。請在專案執行 ConfigDatGen 依 LinEncoder.ini 重寫，或從 EncoderForPartners\\LinLauncher_Environment 複製正確 config.dat。目前使用程式內建預設網址。");
                    return;
                }

                byte[]? data = new ResourceReaderService().ReadConfig();
                if (data == null)
                {
                    StartupLog.Append("LoadConfig: 讀取失敗（簽名非預期），使用程式內建預設網址。");
                    return;
                }

                LauncherConfig? loaded = ConfigDatCodec.TryDecrypt(data);
                if (loaded == null)
                {
                    StartupLog.Append("LoadConfig: 解密或還原 LauncherConfig 失敗，使用程式內建預設網址。");
                    return;
                }

                if (loaded.Configed)
                {
                    _config.Configed = true;
                    _config.Title = loaded.Title;
                    _config.Web = loaded.Web;
                    _config.List = loaded.List;
                    _config.UseUpdate = loaded.UseUpdate;
                    _config.Update = loaded.Update;
                    _config.Width = loaded.Width;
                    _config.Height = loaded.Height;
                    _config.UseLink = loaded.UseLink ?? _config.UseLink;
                    if (loaded.LinkNamesRaw != null) _config.LinkNamesRaw = loaded.LinkNamesRaw;
                    if (loaded.LinkUrlsRaw != null) _config.LinkUrlsRaw = loaded.LinkUrlsRaw;
                    _config.Helper = loaded.Helper ?? "";
                }
            }
            catch (Exception ex)
            {
                StartupLog.Append("LoadConfigFromResource 例外", ex);
            }
        }

        /// <summary>
        /// 異步初始化：載入伺服器列表 -> 檢查更新。
        /// (Async init: Load server list -> Check updates.)
        /// </summary>
        private async Task InitializeAsync()
        {
            try
            {
                StartupLog.Append("InitializeAsync: 開始（更新檢查與伺服器清單）");
                IsBusy = true;

                // 1. 自動更新檢查（僅在 URL 為合法 http(s) 時執行，避免亂碼導致 HttpClient 例外）
                if (Config.UseUpdate && IsValidHttpUrl(Config.Update))
                {
                    StatusText = "Checking for updates...";
                    StartupLog.Append($"InitializeAsync: 檢查更新 URL（前 80 字）={SafePreview(Config.Update, 80)}");
                    try
                    {
                        string tempFile = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "update.tmp");
                        using (var hc = new System.Net.Http.HttpClient())
                        {
                            var resp = await hc.GetAsync(Config.Update);
                            StartupLog.Append($"InitializeAsync: 更新 HTTP 狀態={(int)resp.StatusCode} {resp.ReasonPhrase}");
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
                                        }
                                    }
                                }
                                File.Delete(tempFile);
                            }
                        }
                    }
                    catch (Exception ex)
                    {
                        StartupLog.Append("InitializeAsync: 更新檢查失敗（已略過）", ex);
                    }
                }
                else
                {
                    if (Config.UseUpdate && !IsValidHttpUrl(Config.Update))
                        StartupLog.Append("InitializeAsync: 略過更新檢查（Update 非合法 http(s) URL，可能為 config 解密錯誤）");
                    else
                        StartupLog.Append("InitializeAsync: 略過更新檢查（UseUpdate 關閉或 URL 空白）");
                }

                // 2. 載入伺服器列表：以 config.dat 內合法 http(s) List 為主；失敗或 0 筆時才用本機 list.txt
                StatusText = "Loading server list...";
                List<ServerInfo> servers = new List<ServerInfo>();

                if (IsValidHttpUrl(Config.List))
                {
                    string remote = Config.List!.Trim();
                    StartupLog.Append($"InitializeAsync: 優先載入遠端清單（前 80 字）={SafePreview(remote, 80)}");
                    try
                    {
                        servers = await _serverService.LoadServerListAsync(remote);
                        if (servers.Count > 0)
                            StartupLog.Append($"InitializeAsync: 遠端清單成功，筆數={servers.Count}");
                        else
                            StartupLog.Append("InitializeAsync: 遠端清單解析後筆數為 0，將嘗試本機 list.txt 後援");
                    }
                    catch (Exception ex)
                    {
                        StartupLog.Append("InitializeAsync: 遠端清單載入失敗，將嘗試本機 list.txt 後援", ex);
                    }

                    if (servers.Count == 0)
                    {
                        string? localPath = FindFirstExistingLocalListPath();
                        if (localPath != null)
                        {
                            try
                            {
                                servers = await _serverService.LoadServerListAsync(localPath);
                                StartupLog.Append($"InitializeAsync: 已改用本機清單（後援）：{localPath}，筆數={servers.Count}");
                            }
                            catch (Exception ex)
                            {
                                StartupLog.Append($"InitializeAsync: 本機 list.txt 後援失敗（{localPath}）", ex);
                            }
                        }
                        else
                            StartupLog.Append("InitializeAsync: 遠端未載入成功且無本機 list.txt 可後援");
                    }
                }
                else
                {
                    string? localPath = FindFirstExistingLocalListPath();
                    if (localPath != null)
                    {
                        try
                        {
                            StartupLog.Append($"InitializeAsync: 無有效遠端清單網址，僅使用本機 list.txt（{localPath}）");
                            servers = await _serverService.LoadServerListAsync(localPath);
                            StartupLog.Append($"InitializeAsync: 本機清單筆數={servers.Count}");
                        }
                        catch (Exception ex)
                        {
                            StartupLog.Append("InitializeAsync: 本機 list.txt 載入失敗", ex);
                        }
                    }
                    else
                        StartupLog.Append("InitializeAsync: 無有效遠端網址（Config.List）且找不到本機 list.txt");
                }

                if (servers.Count == 0)
                {
                    StatusText = "請設定遠端清單網址或本機 list.txt";
                    StartupLog.Append("InitializeAsync: 最終清單筆數為 0：請在 config.dat 提供合法 http(s) List，或放置本機 list.txt");
                    IsBusy = false;
                    Application.Current?.Dispatcher.BeginInvoke(
                        DispatcherPriority.ApplicationIdle,
                        new Action(() =>
                        {
                            try
                            {
                                MessageBox.Show(
                                    "無法載入伺服器清單。\n\n"
                                    + "請在 config.dat（Encoder／Proxy 產生）中設定合法 http(s) 清單網址，\n"
                                    + "或於登入器同目錄／上一層目錄放置本機 list.txt。\n"
                                    + "若 config 解密異常，請用 Encoder 重打包。",
                                    "LinLauncher",
                                    MessageBoxButton.OK,
                                    MessageBoxImage.Warning);
                            }
                            catch { }
                        }));
                    return;
                }

                try
                {
                    Servers.Clear();
                    foreach (var s in servers) Servers.Add(s);
                    if (Servers.Count > 0) SelectedServer = Servers[0];
                    StatusText = "Ready";
                    StartupLog.Append($"InitializeAsync: 伺服器清單已套用，筆數={servers.Count}");
                }
                catch (Exception ex)
                {
                    StatusText = "Error loading servers.";
                    StartupLog.Append("InitializeAsync: 填入伺服器清單至介面失敗", ex);
                }

                IsBusy = false;
                StartupLog.Append("InitializeAsync: 結束");
            }
            catch (Exception ex)
            {
                StartupLog.Append("InitializeAsync: 未預期錯誤", ex);
                try
                {
                    IsBusy = false;
                    StatusText = "初始化失敗";
                }
                catch { }
            }
        }

        private static string SafePreview(string? s, int max = 120)
        {
            if (string.IsNullOrEmpty(s)) return "(empty)";
            if (s.Length <= max) return s;
            return s.Substring(0, max) + "…";
        }

        /// <summary>
        /// 對目前選取之伺服器做 TCP 連線測試；成功才可按「遊戲開始」。
        /// 使用序號避免快速換選時舊工作回寫狀態。
        /// </summary>
        private async Task RunServerProbeAsync()
        {
            int seq = Interlocked.Increment(ref _serverProbeSeq);
            _serverProbeCts?.Cancel();
            _serverProbeCts?.Dispose();
            _serverProbeCts = new CancellationTokenSource();
            CancellationToken ct = _serverProbeCts.Token;

            ServerInfo? server = SelectedServer;
            if (server == null)
            {
                if (Volatile.Read(ref _serverProbeSeq) == seq)
                {
                    _serverProbePending = false;
                    _serverReachable = false;
                    StartButtonCaption = "遊戲開始";
                    CommandManager.InvalidateRequerySuggested();
                }
                return;
            }

            _serverProbePending = true;
            _serverReachable = false;
            StartButtonCaption = "檢查連線中...";
            CommandManager.InvalidateRequerySuggested();

            const int timeoutMs = 5000;
            StatusText = "檢查伺服器連線...";
            try
            {
                var probe = await ServerReachabilityService.ProbeAsync(server.Ip, server.Port, timeoutMs, ct).ConfigureAwait(true);
                if (ct.IsCancellationRequested) return;
                if (!ReferenceEquals(SelectedServer, server)) return;
                if (Volatile.Read(ref _serverProbeSeq) != seq) return;

                _serverReachable = probe.Ok;
                StartButtonCaption = probe.Ok ? "遊戲開始" : "無法遊玩";
                if (probe.Ok)
                {
                    StartupLog.Append($"RunServerProbe: TCP {server.Ip}:{server.Port} 成功");
                    StatusText = "Ready";
                }
                else
                {
                    StartupLog.Append($"RunServerProbe: TCP {server.Ip}:{server.Port} 失敗 — {probe.ErrorSummary ?? "未知"}");
                    StatusText = $"無法連線 {server.Ip}:{server.Port} — {probe.ErrorSummary ?? ""}";
                }
            }
            catch (OperationCanceledException)
            {
                return;
            }
            catch
            {
                if (!ReferenceEquals(SelectedServer, server)) return;
                if (Volatile.Read(ref _serverProbeSeq) != seq) return;
                _serverReachable = false;
                StartButtonCaption = "無法遊玩";
            }
            finally
            {
                if (Volatile.Read(ref _serverProbeSeq) == seq)
                {
                    _serverProbePending = false;
                    CommandManager.InvalidateRequerySuggested();
                }
            }
        }

        /// <summary>
        /// 處理「開始遊戲」按鈕邏輯。包含自動路徑偵測、DLL 注入啟動。
        /// (Handles "Start Game" logic: auto path detection, DLL injection launch.)
        /// </summary>
        private async Task StartGameAsync()
        {
            if (SelectedServer == null || !_serverReachable || _serverProbePending) return;
            IsBusy = true;
            StatusText = "Launching game...";
            string appDir = AppDomain.CurrentDomain.BaseDirectory;

            if (!GamePathHelper.TryResolveGameExecutablePath(out string gameExe))
            {
                string a = Path.Combine(appDir.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar), GamePathHelper.DefaultGameExeFileName);
                string b = Path.Combine(GamePathHelper.GetGameRootDirectory(), GamePathHelper.DefaultGameExeFileName);
                System.Windows.MessageBox.Show(
                    $"找不到遊戲主程式 ({GamePathHelper.DefaultGameExeFileName})。已搜尋：\n{a}\n{b}\n（以及開發用 Client 目錄）\n\n請將主程式放在登入器目錄、或遊戲根目錄（例如 LinLauncher_Environment 的上一層）。");
                IsBusy = false;
                StatusText = "Ready";
                return;
            }

            if (!GamePathHelper.TryResolveLauncherDllPath(out string dllPath))
            {
                string a = Path.Combine(appDir.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar), GamePathHelper.LauncherDllFileName);
                string b = Path.Combine(GamePathHelper.GetGameRootDirectory(), GamePathHelper.LauncherDllFileName);
                System.Windows.MessageBox.Show(
                    $"找不到 {GamePathHelper.LauncherDllFileName}。已搜尋：\n{a}\n{b}\n（以及開發用 LauncherDll\\Debug 與 Release）\n\n請將 DLL 放在登入器目錄、或遊戲根目錄（LinLauncher_Environment 的上一層），或確認建置輸出。");
                IsBusy = false;
                StatusText = "Ready";
                return;
            }
            bool launched = await _launchService.LaunchGameAsync(SelectedServer, gameExe, dllPath, "", "");
            if (launched) StatusText = "Game Running...";
            else { StatusText = "Launch failed!"; IsBusy = false; }
        }
    }
}
