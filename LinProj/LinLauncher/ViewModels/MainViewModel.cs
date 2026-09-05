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
        private bool _windowed = true;
        private uint _windowMode = 6;
        private bool _prefsReady;

        public ObservableCollection<ServerInfo> Servers { get => _servers; set { _servers = value; OnPropertyChanged(); } }

        /// <summary>視窗模式偏好（預設 true，對齊 Rust）。</summary>
        public bool Windowed
        {
            get => _windowed;
            set
            {
                if (_windowed == value) return;
                _windowed = value;
                OnPropertyChanged();
                PersistDisplayPrefs();
            }
        }

        /// <summary>WindowMode 4..=7（預設 5=800x600）。</summary>
        public uint WindowMode
        {
            get => _windowMode;
            set
            {
                uint mode = value is >= 4 and <= 7 ? value : 6u;
                if (_windowMode == mode) return;
                _windowMode = mode;
                OnPropertyChanged();
                PersistDisplayPrefs();
            }
        }

        public IReadOnlyList<WindowModeOption> WindowModeOptions { get; } = new[]
        {
            new WindowModeOption(4, "400×300"),
            new WindowModeOption(5, "800×600"),
            new WindowModeOption(6, "1200×900"),
            new WindowModeOption(7, "1600×1200"),
        };
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
                OnPropertyChanged(nameof(IsNotBusy));
                CommandManager.InvalidateRequerySuggested();
            }
        }

        /// <summary>
        /// 給右上角關閉鈕的 IsEnabled 綁定：忙碌中（遊戲啟動中、sync/patch 執行中）
        /// 暫時不能關閉，避免中途強制關閉登入器打斷正在寫入的 pak/idx（吃檔）或
        /// 遊戲注入流程。XAML 沒有內建的布林反相 converter，直接開一個計算屬性
        /// 比另外寫 IValueConverter 簡單。
        /// </summary>
        public bool IsNotBusy => !IsBusy;
        public ICommand StartCommand { get; }
        public ICommand SyncCommand { get; }
        public ICommand PatchCommand { get; }
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
            SyncCommand = new RelayCommand(async _ => await RunManualSyncAsync(), _ => !IsBusy);
            PatchCommand = new RelayCommand(async _ => await RunManualPatchAsync(), _ => !IsBusy);

            // 選取的伺服器維護中時，「開始遊戲」按鈕文字顯示即時倒數；每秒重新計算一次
            // （MaintenanceEndAtUtc 是查詢當下算好的固定時間點，這裡只是每秒重新算「還剩多少」，
            // 不用每秒重新查詢伺服器）。
            _maintenanceCountdownTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
            _maintenanceCountdownTimer.Tick += (s, e) => UpdateMaintenanceCountdownCaption();
            _maintenanceCountdownTimer.Start();

            _updateService.ProgressChanged += OnUpdateProgressChanged;
            _launchService.GameExited += (s, e) => { IsBusy = false; StatusText = "Ready"; };
            // 遊戲進程建起來到主視窗真的出現，殼解密＋掛勾流程實測要 10~20 秒不等，
            // 這段期間畫面完全沒變化，使用者容易誤以為卡住了。這裡借用原本下載更新
            // 用的 OverallProgress/StatusText 顯示一個「模擬」進度條，實際完成時機
            // 還是以視窗真的出現為準（見 LaunchService.WaitForGameWindowAsync）。
            _launchService.GameProcessStarted += async (s, pid) => await WatchGameStartupProgressAsync(pid);
            
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

            try
            {
                UserDisplayPrefs prefs = UserPrefsService.Load();
                _windowed = prefs.Windowed;
                _windowMode = prefs.WindowMode is >= 4 and <= 7 ? prefs.WindowMode : 6u;
                _prefsReady = true;
                OnPropertyChanged(nameof(Windowed));
                OnPropertyChanged(nameof(WindowMode));
            }
            catch (Exception ex)
            {
                StartupLog.Append("MainViewModel: 載入顯示偏好失敗", ex);
                _prefsReady = true;
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
        /// 遊戲根目錄標記：上次更新已下載、但因遊戲在跑而尚未吃檔。
        /// 僅在更新流程補吃，開始遊戲不會讀這個旗標。
        /// </summary>
        private const string EatPendingFlagName = ".eat_pending";

        private static string GetEatPendingFlagPath(string gameRoot) =>
            Path.Combine(gameRoot.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar), EatPendingFlagName);

        private static bool HasEatPendingFlag(string gameRoot) =>
            File.Exists(GetEatPendingFlagPath(gameRoot));

        private static void MarkEatPending(string gameRoot)
        {
            try
            {
                File.WriteAllText(GetEatPendingFlagPath(gameRoot), DateTime.UtcNow.ToString("o"));
            }
            catch (Exception ex)
            {
                StartupLog.Append("MarkEatPending: 寫入旗標失敗", ex);
            }
        }

        private static void ClearEatPending(string gameRoot)
        {
            try
            {
                string path = GetEatPendingFlagPath(gameRoot);
                if (File.Exists(path))
                    File.Delete(path);
            }
            catch (Exception ex)
            {
                StartupLog.Append("ClearEatPending: 刪除旗標失敗", ex);
            }
        }

        /// <summary>
        /// 記錄「上次成功吃檔時套用的 update.txt 原始內容」，供下次
        /// UpdateService.CheckFilesAsync 的 alreadyEatenLookup 參數比對——散檔被
        /// 吃掉刪除後，靠這份紀錄分辨「正常吃掉」跟「真的遺失」，見
        /// UpdateService.CheckFilesAsync 的說明。只在吃檔真的完全成功時才覆寫，
        /// 跳過／失敗時保留舊紀錄不動。
        /// </summary>
        private const string LastSyncedManifestFileName = ".last_synced_update.txt";

        private static string GetLastSyncedManifestPath(string gameRoot) =>
            Path.Combine(gameRoot.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar), LastSyncedManifestFileName);

        private static async Task SaveLastSyncedManifestAsync(string gameRoot, byte[] manifestBytes)
        {
            try
            {
                await File.WriteAllBytesAsync(GetLastSyncedManifestPath(gameRoot), manifestBytes).ConfigureAwait(false);
            }
            catch (Exception ex)
            {
                StartupLog.Append("SaveLastSyncedManifest: 寫入失敗", ex);
            }
        }

        /// <summary>
        /// 把遊戲根目錄的 icon/sprite/Surf/text/Tile 散檔寫入 idx/pak。
        /// 失敗只提示，不擋開始遊戲或後續流程。
        /// 實際檔案 I/O（EatService.Run）包在 Task.Run 裡跑在背景執行緒——原本直接在 UI
        /// 執行緒同步呼叫，實測 1444 個檔案要花 47 秒~1 分 40 秒，整段時間 UI 執行緒被佔用，
        /// Progress&lt;EatProgress&gt; 排回 UI 執行緒的更新只能排隊、畫面完全沒機會重繪，
        /// 使用者看到的是整個視窗凍結，不是「沒進度可看」。改成背景執行緒後，UI 執行緒才有
        /// 機會處理 Progress 回報、讓進度真的能動。
        /// 回傳值代表「這次呼叫完之後，本機狀態是否已經跟 update.txt 完全同步」——true 時
        /// 呼叫端會更新 .last_synced_update.txt（見 ProcessUpdateManifestFromBytesAsync），
        /// 讓下次檢查不會把剛被正常吃掉、刪除的散檔誤判成遺失、重新下載一輪。
        /// </summary>
        private async Task<bool> TryEatClientPacksAsync(string gameRoot)
        {
            gameRoot = gameRoot.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            if (!EatService.HasPendingFiles(gameRoot))
            {
                StartupLog.Append($"TryEatClientPacks: 無散檔 ({gameRoot})");
                ClearEatPending(gameRoot);
                StatusText = "無須套用更新檔案";
                return true; // 沒有散檔要吃，代表本機已經跟目前的 update.txt 一致
            }

            if (IsGameProcessRunning())
            {
                StartupLog.Append("[吃檔] 遊戲執行中，跳過並標記 .eat_pending");
                MarkEatPending(gameRoot);
                StatusText = "請先關閉遊戲";
                return false; // 還沒真正吃檔，狀態還沒同步，不能當作已同步處理
            }

            try
            {
                StartupLog.Append($"TryEatClientPacks: 開始 {gameRoot}");
                var progress = new Progress<EatProgress>(p =>
                {
                    void Apply()
                    {
                        StatusText = p.Message;
                        OverallProgress = Math.Clamp(p.OverallPercent, 0, 100);
                    }

                    if (Application.Current?.Dispatcher.CheckAccess() == true)
                    {
                        Apply();
                    }
                    else
                    {
                        Application.Current?.Dispatcher.Invoke(Apply);
                    }
                });

                var result = await Task.Run(() => EatService.Run(gameRoot, keepLooseFiles: false, progress: progress));
                StartupLog.Append($"TryEatClientPacks: {result.Summary}");
                // Done 回報已寫短文案；再覆寫一次確保最終狀態正確。
                StatusText = result.Ok ? "套用完成" : "套用未完成";

                if (result.Ok)
                {
                    ClearEatPending(gameRoot);
                    return true;
                }

                MarkEatPending(gameRoot);
                StatusText = "套用未完成";
                return false;
            }
            catch (Exception ex)
            {
                StartupLog.Append("TryEatClientPacks: 執行失敗", ex);
                MarkEatPending(gameRoot);
                StatusText = "套用失敗";
                return false;
            }
        }

        /// <summary>更新下載成功後才吃檔；遊戲在跑則寫 .eat_pending，下次更新檢查無下載時補吃。</summary>
        private async Task<bool> ApplyEatAfterSuccessfulDownloadAsync(string gameRoot, bool anyDeferred)
        {
            if (IsGameProcessRunning())
            {
                StartupLog.Append("[更新] 偵測到遊戲仍在執行中，跳過吃檔並寫入 .eat_pending");
                MarkEatPending(gameRoot);
                StatusText = "請關閉遊戲後重開登入器";
                return false;
            }

            bool eaten = await TryEatClientPacksAsync(gameRoot);
            if (anyDeferred)
            {
                StartupLog.Append("[更新] 部分登入器自身檔案使用中，已保留待下次啟動套用");
            }
            StatusText = "更新完成，請重開登入器";
            return eaten;
        }

        /// <summary>更新檢查無需下載時，只在有 .eat_pending 才補吃，不會因為資料夾剛好有散檔就吃。</summary>
        private async Task<bool> TryEatIfPendingAfterUpdateAsync(string gameRoot)
        {
            if (!HasEatPendingFlag(gameRoot))
                return true; // 沒有待補吃的旗標，代表本機早就跟當時的 update.txt 一致
            if (IsGameProcessRunning())
            {
                StartupLog.Append("[吃檔] 有 .eat_pending 但遊戲仍在執行中，下次再試");
                return false;
            }

            StartupLog.Append("[吃檔] 更新無需下載，補做上次未完成的吃檔");
            return await TryEatClientPacksAsync(gameRoot);
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
                                    "無法載入伺服器清單",
                                    "Launcher",
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

        /// <summary>「sync」按鈕：手動重新跑一次完整更新檢查（下載+自動吃檔），
        /// 直接重用啟動時 InitializeAsync 用的同一段更新階段邏輯。</summary>
        private async Task RunManualSyncAsync()
        {
            if (IsBusy) return;
            IsBusy = true;
            try
            {
                StartupLog.Append("[手動 sync] 開始");
                await RunUpdatePhaseAsync(prefetchedManifest: null, manifestFetchAlreadyDone: false);
                StartupLog.Append("[手動 sync] 結束");
            }
            catch (Exception ex)
            {
                StartupLog.Append("[手動 sync] 未預期錯誤", ex);
            }
            finally
            {
                IsBusy = false;
                OverallProgress = 0;
                StatusText = "Ready";
            }
        }

        /// <summary>「patch」按鈕：只手動觸發吃檔，不檢查、不下載更新檔。</summary>
        private async Task RunManualPatchAsync()
        {
            if (IsBusy) return;
            IsBusy = true;
            try
            {
                string gameRoot = GamePathHelper.GetGameRootDirectory();
                if (IsGameProcessRunning())
                {
                    StatusText = "請先關閉遊戲";
                    return;
                }

                bool hadPending = EatService.HasPendingFiles(gameRoot);
                bool ok = await TryEatClientPacksAsync(gameRoot);
                if (ok && !hadPending)
                {
                    StatusText = "無須套用更新檔案";
                }
                // ok && hadPending：進度已由 EatService 回報寫在 StatusText。
                // ok == false：TryEatClientPacksAsync 已寫短文案到 StatusText。
            }
            catch (Exception ex)
            {
                StartupLog.Append("[手動 patch] 未預期錯誤", ex);
                StatusText = "套用失敗";
            }
            finally
            {
                IsBusy = false;
            }
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
                    await TryEatIfPendingAfterUpdateAsync(GamePathHelper.GetGameRootDirectory());
                    return;
                }

                // update.txt 裡的檔名（例如 "sprite/12267-0.spr"）是相對「遊戲根目錄」，
                // 不是相對登入器自己的 Core 目錄——sprite 跟 Core 是同層目錄，用
                // AppDomain.CurrentDomain.BaseDirectory（= Core）會把檔案解到 Core\sprite\
                // 這個不存在、遊戲也不會讀的地方。
                string updateRoot = GamePathHelper.GetGameRootDirectory();

                // 上次成功吃檔時套用的清單：散檔被正常吃掉刪除後，靠這份紀錄分辨
                // 「正常吃掉」跟「真的遺失」，見 CheckFilesAsync 的 alreadyEatenLookup 說明。
                Dictionary<string, string>? alreadyEatenLookup = null;
                string lastSyncedPath = GetLastSyncedManifestPath(updateRoot);
                if (File.Exists(lastSyncedPath))
                {
                    var (lastSyncedFiles, _) = await _updateService.LoadUpdateListAsync(lastSyncedPath);
                    alreadyEatenLookup = lastSyncedFiles.ToDictionary(f => f.Filename, f => f.Md5, StringComparer.OrdinalIgnoreCase);
                }

                var needUpdate = await _updateService.CheckFilesAsync(
                    allFiles, updateRoot, msg => StartupLog.Append($"UpdateCheck: {msg}"), alreadyEatenLookup);
                StartupLog.Append($"[更新] 需更新檔案數={needUpdate.Count}");
                if (needUpdate.Count == 0)
                {
                    StartupLog.Append("[更新] 本機檔案已是最新，無需下載");
                    if (await TryEatIfPendingAfterUpdateAsync(updateRoot))
                        await SaveLastSyncedManifestAsync(updateRoot, bytes);
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
                    if (await ApplyEatAfterSuccessfulDownloadAsync(updateRoot, anyDeferred))
                        await SaveLastSyncedManifestAsync(updateRoot, bytes);
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
            if (string.IsNullOrEmpty(s)) 
            {
                return "(empty)"; 
            }
            if (s.Length <= max) 
            {
                return s;
            }
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
                if (!CanRefreshServers) 
                {
                    return;
                }
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

            bool launched = await _launchService.LaunchGameAsync(
                SelectedServer, gameExe, dllPath, "", "", Windowed, WindowMode);
            // launched==true 之後的狀態文字交給 WatchGameStartupProgressAsync
            // （由 LaunchService.GameProcessStarted 事件觸發）接手顯示啟動進度，
            // 這裡不要覆蓋掉，不然模擬進度條剛開始跑就被蓋成「Game Running...」。
            if (!launched) { StatusText = "Launch failed!"; IsBusy = false; }
        }

        /// <summary>
        /// 遊戲進程建立後、主視窗出現前，顯示一個模擬進度條讓使用者知道還在跑，
        /// 不是卡住了。借用下載更新用的 OverallProgress/StatusText。
        /// </summary>
        private async Task WatchGameStartupProgressAsync(int pid)
        {
            var progress = new Progress<int>(pct =>
            {
                OverallProgress = pct;
                StatusText = "遊戲啟動中…"; // % 數已經由進度條本身顯示，不重複寫在文字裡
            });
            bool windowAppeared = await _launchService.WaitForGameWindowAsync(pid, progress);
            OverallProgress = 0;
            StatusText = windowAppeared
                ? "遊戲啟動中..."
                : "遊戲仍在啟動中，若持續無回應請檢查防毒/防火牆是否擋住了遊戲。";
            // 啟動流程（等視窗出現）本身跑完就放開「開始遊戲」鈕，不用等這個
            // 遊戲行程真正結束（GameExited）才放開——否則同一個登入器視窗沒辦法
            // 在第一個遊戲還開著時再啟動第二個，雙開只能被迫開兩個登入器視窗。
            IsBusy = false;
        }

        private void PersistDisplayPrefs()
        {
            if (!_prefsReady) return;
            UserPrefsService.Save(new UserDisplayPrefs { Windowed = Windowed, WindowMode = WindowMode });
        }
    }

    public sealed class WindowModeOption
    {
        public WindowModeOption(uint value, string label)
        {
            Value = value;
            Label = label;
        }

        public uint Value { get; }
        public string Label { get; }
    }
}
