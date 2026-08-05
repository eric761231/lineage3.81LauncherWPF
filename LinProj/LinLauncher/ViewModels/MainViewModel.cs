// MainViewModel.cs: 主視窗的 ViewModel，處理伺服器清單、更新與啟動邏輯。
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Net.Http;
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

        private string _startButtonCaption = "遊戲開始";
        private DateTime _lastServerRefreshUtc = DateTime.MinValue;
        private bool _canRefreshServers = true;
        private readonly DispatcherTimer _maintenanceCountdownTimer;

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
                UpdateMaintenanceCountdownCaption();
            }
        }

        /// <summary>重新整理伺服器狀態指令：5 分鐘內只能觸發一次。</summary>
        public ICommand RefreshServersCommand { get; }

        public bool CanRefreshServers
        {
            get => _canRefreshServers;
            private set { _canRefreshServers = value; OnPropertyChanged(); }
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
            // 灰燈（IsStatusUnknown，離線/查詢失敗）鎖住開始按鈕——這是使用者明確要求的行為。
            // 注意：這跟先前拿掉「背景探測」硬性條件的取捨不同：之前是探測機制本身不穩定、
            // 會「意外」把使用者鎖死；這次是使用者「刻意」要求灰燈就不能玩，且有「重新整理」
            // 按鈕可以手動重試，不是無法挽回的死鎖。
            StartCommand = new RelayCommand(
                async _ => await StartGameAsync(),
                _ => SelectedServer != null && !IsBusy && !SelectedServer.IsStatusUnknown);
            RefreshServersCommand = new RelayCommand(
                async _ => await RefreshAllServerStatusAsync(isManualRefresh: true),
                _ => CanRefreshServers);

            // 選取的伺服器維護中時，「開始遊戲」按鈕文字顯示即時倒數；每秒重新計算一次
            // （MaintenanceEndAtUtc 是查詢當下算好的固定時間點，這裡只是每秒重新算「還剩多少」，
            // 不用每秒重新查詢伺服器）。
            _maintenanceCountdownTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
            _maintenanceCountdownTimer.Tick += (s, e) => UpdateMaintenanceCountdownCaption();
            _maintenanceCountdownTimer.Start();

            _updateService.ProgressChanged += OnUpdateProgressChanged;
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

        /// <summary>常見配置：清單放在 Core 或上一層遊戲根目錄（如 C:\3.81Lineage\）。</summary>
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

        private void OnUpdateProgressChanged(object? sender, UpdateProgressEventArgs e)
        {
            void Apply()
            {
                OverallProgress = e.OverallPercent;
                if (string.IsNullOrEmpty(e.CurrentFile))
                    StatusText = $"下載更新… {e.OverallPercent}%";
                else
                    StatusText = $"下載更新：{e.CurrentFile}（總進度 {e.OverallPercent}% · 目前檔 {e.FilePercent}%）";
            }

            if (Application.Current?.Dispatcher.CheckAccess() == true)
                Apply();
            else
                Application.Current?.Dispatcher.Invoke(Apply);
        }

        /// <summary>
        /// 下載完成後執行 eat.exe。預期位置為 Core 的上一層（遊戲根目錄），
        /// 與 <see cref="GamePathHelper.GetGameRootDirectory"/> 相同；不在 Core 資料夾內尋找。
        /// </summary>
        private static void TryRunEatExe()
        {
            string gameRoot = GamePathHelper.GetGameRootDirectory().TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            string eatPath = Path.Combine(gameRoot, "eat.exe");

            if (!File.Exists(eatPath))
            {
                StartupLog.Append(
                    $"TryRunEatExe: 未找到 eat.exe。預期路徑（Core 上一層）：{eatPath}");
                return;
            }

            try
            {
                StartupLog.Append($"TryRunEatExe: 啟動 {eatPath}");
                var psi = new ProcessStartInfo(eatPath)
                {
                    WorkingDirectory = gameRoot,
                    UseShellExecute = true
                };
                using Process? p = Process.Start(psi);
                if (p != null)
                {
                    p.WaitForExit(600000);
                    StartupLog.Append($"TryRunEatExe: 結束代碼={p.ExitCode}");
                }
            }
            catch (Exception ex)
            {
                StartupLog.Append($"TryRunEatExe: 執行失敗 {eatPath}", ex);
            }
        }

        /// <summary>依 SelectedServer 目前的維護狀態，更新「開始遊戲」按鈕文字（每秒由計時器呼叫一次）。</summary>
        private void UpdateMaintenanceCountdownCaption()
        {
            var server = SelectedServer;
            if (server == null || !server.IsMaintenance)
            {
                if (StartButtonCaption != "遊戲開始") StartButtonCaption = "遊戲開始";
                return;
            }

            if (server.MaintenanceEndAtUtc is DateTime endAtUtc)
            {
                TimeSpan remaining = endAtUtc - DateTime.UtcNow;
                if (remaining < TimeSpan.Zero) remaining = TimeSpan.Zero;
                StartButtonCaption = $"維護倒數 {remaining:hh\\:mm\\:ss}";
            }
            else
            {
                StartButtonCaption = "維護中";
            }
        }

        /// <summary>偵測遊戲主程式是否正在執行中（不限於這次登入器工作階段啟動的行程）。</summary>
        private static bool IsGameProcessRunning()
        {
            try
            {
                string name = Path.GetFileNameWithoutExtension(GamePathHelper.DefaultGameExeFileName);
                return Process.GetProcessesByName(name).Length > 0;
            }
            catch
            {
                return false;
            }
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
                        + "。請在專案執行 ConfigDatGen 依 LinEncoder.ini 重寫，或從 EncoderForPartners\\Core 複製正確 config.dat。目前使用程式內建預設網址。");
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
        /// 非同步初始化：[清單] list.txt 與 [更新] update.txt 分開處理；
        /// 兩者皆為合法 http(s) 時以 Task.WhenAll 並行取得，再套用清單、再解析更新。
        /// </summary>
        private async Task InitializeAsync()
        {
            try
            {
                StartupLog.Append("InitializeAsync: 開始 — [清單]list 與 [更新]update 分開處理（兩者皆 HTTP 時並行取得）");
                IsBusy = true;

                OverallProgress = 0;
                StatusText = "Loading server list...";

                bool listRemote = IsValidHttpUrl(Config.List);
                bool wantUpdate = Config.UseUpdate && IsValidHttpUrl(Config.Update);

                List<ServerInfo> servers;
                byte[]? updateManifestPrefetch = null;
                bool updateManifestFetchDone = false;

                if (listRemote && wantUpdate)
                {
                    var pair = await ParallelFetchListAndUpdateManifestAsync();
                    servers = pair.Servers;
                    updateManifestPrefetch = pair.ManifestBytes;
                    updateManifestFetchDone = pair.ManifestFetchAttempted;
                }
                else
                {
                    servers = await LoadServerListSequentialAsync();
                }

                if (servers.Count == 0)
                {
                    StatusText = "請設定遠端清單網址或本機 list.txt";
                    StartupLog.Append("[清單] 最終筆數為 0：請在 config.dat 提供合法 http(s) List，或放置本機 list.txt");
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
                    StartupLog.Append($"[清單] 已套用至介面，筆數={servers.Count}");
                    _ = RefreshAllServerStatusAsync(isManualRefresh: false);
                }
                catch (Exception ex)
                {
                    StatusText = "Error loading servers.";
                    StartupLog.Append("[清單] 填入介面失敗", ex);
                }

                await RunUpdatePhaseAsync(updateManifestPrefetch, updateManifestFetchDone);

                OverallProgress = 0;
                StatusText = "Ready";
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

        /// <summary>[清單] 與 [更新] 各發一個 HTTP，並行完成後再分別處理。</summary>
        private async Task<(List<ServerInfo> Servers, byte[]? ManifestBytes, bool ManifestFetchAttempted)> ParallelFetchListAndUpdateManifestAsync()
        {
            string listUrl = Config.List!.Trim();
            string updateUrl = Config.Update!.Trim();

            StartupLog.Append("[清單] 與 [更新] 並行 HTTP（Task.WhenAll）");
            StartupLog.Append($"[清單] list.txt URL（前 80 字）={SafePreview(listUrl, 80)}");
            StartupLog.Append($"[更新] update.txt URL（前 80 字）={SafePreview(updateUrl, 80)}");

            var tList = _serverService.LoadServerListAsync(listUrl);
            var tManifest = FetchUpdateManifestAsync(updateUrl);
            await Task.WhenAll(tList, tManifest);

            var servers = await tList;
            var manifestResult = await tManifest;

            StartupLog.Append($"[清單] 遠端完成，筆數={servers.Count}");
            StartupLog.Append($"[更新] HTTP {manifestResult.StatusCode} {manifestResult.ReasonPhrase}");

            if (servers.Count == 0)
                servers = await TryLoadLocalListFallbackAsync();

            byte[]? bytes = manifestResult.Ok && manifestResult.Bytes != null ? manifestResult.Bytes : null;
            if (!manifestResult.Ok)
                StartupLog.Append("[更新] 並行取得失敗（略過重試；請看上一行 HTTP 或例外）");

            return (Servers: servers, ManifestBytes: bytes, ManifestFetchAttempted: true);
        }

        /// <summary>僅 [清單]：遠端或本機，與更新階段無並行。</summary>
        private async Task<List<ServerInfo>> LoadServerListSequentialAsync()
        {
            List<ServerInfo> servers = new List<ServerInfo>();

            if (IsValidHttpUrl(Config.List))
            {
                string remote = Config.List!.Trim();
                StartupLog.Append($"[清單] 遠端 list.txt URL（前 80 字）={SafePreview(remote, 80)}");
                try
                {
                    servers = await _serverService.LoadServerListAsync(remote);
                    if (servers.Count > 0)
                        StartupLog.Append($"[清單] 遠端成功，筆數={servers.Count}");
                    else
                        StartupLog.Append("[清單] 遠端解析後 0 筆，將嘗試本機後援");
                }
                catch (Exception ex)
                {
                    StartupLog.Append("[清單] 遠端載入失敗，將嘗試本機後援", ex);
                }

                if (servers.Count == 0)
                    servers = await TryLoadLocalListFallbackAsync();
            }
            else
            {
                string? localPath = FindFirstExistingLocalListPath();
                if (localPath != null)
                {
                    try
                    {
                        StartupLog.Append($"[清單] 無有效遠端網址，僅本機 list.txt（{localPath}）");
                        servers = await _serverService.LoadServerListAsync(localPath);
                        StartupLog.Append($"[清單] 本機筆數={servers.Count}");
                    }
                    catch (Exception ex)
                    {
                        StartupLog.Append("[清單] 本機 list.txt 載入失敗", ex);
                    }
                }
                else
                    StartupLog.Append("[清單] 無有效 Config.List 且找不到本機 list.txt");
            }

            return servers;
        }

        private async Task<List<ServerInfo>> TryLoadLocalListFallbackAsync()
        {
            string? localPath = FindFirstExistingLocalListPath();
            if (localPath == null)
            {
                StartupLog.Append("[清單] 無本機 list.txt 可後援");
                return new List<ServerInfo>();
            }

            try
            {
                var list = await _serverService.LoadServerListAsync(localPath);
                StartupLog.Append($"[清單] 本機後援：{localPath}，筆數={list.Count}");
                return list;
            }
            catch (Exception ex)
            {
                StartupLog.Append($"[清單] 本機後援失敗（{localPath}）", ex);
                return new List<ServerInfo>();
            }
        }

        private async Task<(bool Ok, byte[]? Bytes, int StatusCode, string ReasonPhrase)> FetchUpdateManifestAsync(string url)
        {
            try
            {
                using var hc = new HttpClient();
                hc.Timeout = TimeSpan.FromMinutes(5);
                hc.DefaultRequestHeaders.UserAgent.ParseAdd("LinLauncher/1.0");
                var resp = await hc.GetAsync(url);
                byte[]? bytes = null;
                if (resp.IsSuccessStatusCode)
                    bytes = await resp.Content.ReadAsByteArrayAsync();
                return (resp.IsSuccessStatusCode, bytes, (int)resp.StatusCode, resp.ReasonPhrase ?? "");
            }
            catch (Exception ex)
            {
                StartupLog.Append($"[更新] 取得 update.txt 例外", ex);
                return (false, null, 0, ex.Message);
            }
        }

        private async Task RunUpdatePhaseAsync(byte[]? prefetchedManifest, bool manifestFetchAlreadyDone)
        {
            if (!Config.UseUpdate || !IsValidHttpUrl(Config.Update))
            {
                if (Config.UseUpdate && !IsValidHttpUrl(Config.Update))
                    StartupLog.Append("[更新] 略過（Update 非合法 http(s) URL）");
                else if (!Config.UseUpdate)
                    StartupLog.Append("[更新] 略過（UseUpdate 關閉）");
                else
                    StartupLog.Append("[更新] 略過（URL 空白）");
                return;
            }

            StatusText = "檢查更新中…";
            OverallProgress = 0;

            if (prefetchedManifest != null)
            {
                StartupLog.Append("[更新] 使用並行階段已取得的 update.txt 內容");
                await ProcessUpdateManifestFromBytesAsync(prefetchedManifest);
                return;
            }

            if (manifestFetchAlreadyDone)
            {
                StartupLog.Append("[更新] 並行請求已嘗試過且無可用內容，不重複 GET");
                return;
            }

            StartupLog.Append($"[更新] 單獨 GET URL（前 80 字）={SafePreview(Config.Update, 80)}");
            var r = await FetchUpdateManifestAsync(Config.Update!.Trim());
            StartupLog.Append($"[更新] HTTP {r.StatusCode} {r.ReasonPhrase}");
            if (r.Ok && r.Bytes != null)
                await ProcessUpdateManifestFromBytesAsync(r.Bytes);
            else
                StartupLog.Append("[更新] 無法取得更新清單");
        }

        private async Task ProcessUpdateManifestFromBytesAsync(byte[] bytes)
        {
            string tempFile = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "update.tmp");
            try
            {
                await File.WriteAllBytesAsync(tempFile, bytes);

                var (allFiles, baseUrl) = await _updateService.LoadUpdateListAsync(tempFile);
                StartupLog.Append($"[更新] 清單解析 count={allFiles.Count}, baseUrl={(string.IsNullOrEmpty(baseUrl) ? "(empty)" : SafePreview(baseUrl, 80))}");
                if (allFiles.Count == 0)
                {
                    StartupLog.Append("[更新] 清單為 0 筆（請檢查 update.txt 與 [main] count）");
                    return;
                }

                // update.txt 裡的檔名（例如 "sprite/12267-0.spr"）是相對「遊戲根目錄」，
                // 不是相對登入器自己的 Core 目錄——sprite 跟 Core 是同層目錄，用
                // AppDomain.CurrentDomain.BaseDirectory（= Core）會把檔案解到 Core\sprite\
                // 這個不存在、遊戲也不會讀的地方。
                string updateRoot = GamePathHelper.GetGameRootDirectory();
                var needUpdate = await _updateService.CheckFilesAsync(allFiles, updateRoot);
                StartupLog.Append($"[更新] 需更新檔案數={needUpdate.Count}");
                if (needUpdate.Count == 0)
                {
                    StartupLog.Append("[更新] 本機檔案已是最新，無需下載");
                    return;
                }

                OverallProgress = 0;
                StatusText = $"下載更新（{needUpdate.Count} 個檔案）…";
                var (ok, err, anyDeferred) = await _updateService.DownloadUpdatesAsync(
                    needUpdate,
                    baseUrl,
                    updateRoot,
                    msg => StartupLog.Append($"UpdateDownload: {msg}"));

                if (ok)
                {
                    // Sprite.pak 等資源檔案是 eat.exe 寫入的目標；若遊戲本體目前正在執行中，
                    // 這些檔案可能被遊戲行程開著讀取，eat.exe 這時候去寫可能會失敗或寫壞。
                    // 偵測到遊戲還在跑就跳過這次 eat.exe，提示使用者關閉遊戲後重開登入器再套用。
                    if (IsGameProcessRunning())
                    {
                        StartupLog.Append("[更新] 偵測到遊戲仍在執行中，跳過 eat.exe，避免寫入衝突");
                        MessageBox.Show(
                            "更新檔案已下載完成，但偵測到遊戲目前仍在執行中，暫時跳過套用資源封裝（eat.exe）步驟。\n\n" +
                            "請先關閉遊戲，再重新開啟登入器一次以完成套用。",
                            "更新提示",
                            MessageBoxButton.OK,
                            MessageBoxImage.Warning);
                    }
                    else
                    {
                        TryRunEatExe();
                        string extra = anyDeferred
                            ? "\n\n部分登入器自身檔案目前使用中，已保留待更新，將於下次啟動登入器時自動完成。"
                            : "";
                        MessageBox.Show(
                            "更新下載完成，已執行 eat.exe（若存在）。請重新啟動登入器以套用變更。" + extra,
                            "更新提示",
                            MessageBoxButton.OK,
                            MessageBoxImage.Information);
                    }
                }
                else if (!string.IsNullOrEmpty(err))
                {
                    StartupLog.Append($"[更新] 下載失敗 {err}");
                    MessageBox.Show(err, "更新失敗", MessageBoxButton.OK, MessageBoxImage.Warning);
                }
            }
            catch (Exception ex)
            {
                StartupLog.Append("[更新] 解析或套用更新失敗（已略過）", ex);
            }
            finally
            {
                try
                {
                    if (File.Exists(tempFile))
                        File.Delete(tempFile);
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
        /// 對整個伺服器清單查詢狀態（在線人數／上限），更新每一項的燈號。
        /// 開啟登入器時自動跑一次（isManualRefresh=false，不佔用冷卻額度）；
        /// 之後只能透過使用者按「重新整理」觸發（isManualRefresh=true），
        /// 且 5 分鐘內只能觸發一次。
        /// </summary>
        private async Task RefreshAllServerStatusAsync(bool isManualRefresh)
        {
            if (isManualRefresh)
            {
                if (!CanRefreshServers) return;
                _lastServerRefreshUtc = DateTime.UtcNow;
                CanRefreshServers = false;
                CommandManager.InvalidateRequerySuggested();
                _ = Task.Delay(TimeSpan.FromMinutes(5)).ContinueWith(_ =>
                {
                    CanRefreshServers = true;
                    CommandManager.InvalidateRequerySuggested();
                }, TaskScheduler.FromCurrentSynchronizationContext());
            }

            const int timeoutMs = 3000;
            var servers = Servers.ToList();
            var tasks = servers.Select(async s =>
            {
                var status = await ServerReachabilityService.QueryStatusAsync(s.Ip, s.Port, timeoutMs).ConfigureAwait(true);
                if (status.Ok)
                {
                    s.IsMaintenance = status.IsMaintenance;
                    s.OnlineCount = status.IsMaintenance ? -1 : status.Online;
                    s.MaxOnline = status.IsMaintenance ? -1 : status.Max;
                    s.MaintenanceEndAtUtc = (status.IsMaintenance && status.MaintenanceRemainingSeconds >= 0)
                        ? DateTime.UtcNow.AddSeconds(status.MaintenanceRemainingSeconds)
                        : (DateTime?)null;
                }
                else
                {
                    s.IsMaintenance = false;
                    s.OnlineCount = -1;
                    s.MaxOnline = -1;
                    s.MaintenanceEndAtUtc = null;
                }
            });
            try
            {
                await Task.WhenAll(tasks).ConfigureAwait(true);
                StartupLog.Append($"RefreshAllServerStatus: 完成，共 {servers.Count} 台");
            }
            catch (Exception ex)
            {
                StartupLog.Append("RefreshAllServerStatus: 發生錯誤", ex);
            }
            finally
            {
                // 狀態查詢結果會影響「開始遊戲」按鈕（灰燈鎖住），查詢完一定要重新檢查一次 CanExecute。
                CommandManager.InvalidateRequerySuggested();
                UpdateMaintenanceCountdownCaption();
            }
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

            if (!GamePathHelper.TryResolveGameExecutablePath(out string gameExe))
            {
                string a = Path.Combine(appDir.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar), GamePathHelper.DefaultGameExeFileName);
                string b = Path.Combine(GamePathHelper.GetGameRootDirectory(), GamePathHelper.DefaultGameExeFileName);
                System.Windows.MessageBox.Show(
                    $"找不到遊戲主程式 ({GamePathHelper.DefaultGameExeFileName})。已搜尋：\n{a}\n{b}\n（以及開發用 Client 目錄）\n\n請將主程式放在登入器目錄、或遊戲根目錄（例如 Core 的上一層）。");
                IsBusy = false;
                StatusText = "Ready";
                return;
            }

            if (!GamePathHelper.TryResolveLauncherDllPath(out string dllPath))
            {
                string a = Path.Combine(appDir.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar), GamePathHelper.LauncherDllFileName);
                string b = Path.Combine(GamePathHelper.GetGameRootDirectory(), GamePathHelper.LauncherDllFileName);
                System.Windows.MessageBox.Show(
                    $"找不到 {GamePathHelper.LauncherDllFileName}。已搜尋：\n{a}\n{b}\n（以及開發用 LauncherDll\\Debug 與 Release）\n\n請將 DLL 放在登入器目錄、或遊戲根目錄（Core 的上一層），或確認建置輸出。");
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
