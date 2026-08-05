using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using System.Windows.Input;
using System.Windows.Interop;
using System.Threading.Tasks;
using LinEncoder.Models;
using LinEncoder.Services;

namespace LinEncoder.ViewModels
{
    public class EncoderViewModel : BaseViewModel
    {
        private readonly EncoderService _encoderService = new EncoderService();
        private readonly IniService _ini = new IniService(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "LinEncoder.ini"));

        #region Launcher Maker Properties
        private string _title = "天堂登入器";
        public string Title { get => _title; set { _title = value; OnPropertyChanged(); } }

        private string _version = "1001";
        public string Version { get => _version; set { _version = value; OnPropertyChanged(); } }

        private string _web = "http://www.google.com/";
        public string Web { get => _web; set { _web = value; OnPropertyChanged(); } }

        private string _list = "http://www.google.com/list.txt";
        public string List { get => _list; set { _list = value; OnPropertyChanged(); } }

        private bool _useUpdate;
        public bool UseUpdate { get => _useUpdate; set { _useUpdate = value; OnPropertyChanged(); } }

        private string _updateUrl = "http://www.google.com/update.txt";
        public string UpdateUrl { get => _updateUrl; set { _updateUrl = value; OnPropertyChanged(); } }

        public ObservableCollection<LinkItem> Links { get; } = new ObservableCollection<LinkItem>();

        private int _width = 1000;
        public int Width { get => _width; set { _width = value; OnPropertyChanged(); } }

        private int _height = 600;
        public int Height { get => _height; set { _height = value; OnPropertyChanged(); } }

        private string _outputLauncherName = "LinLauncher";
        public string OutputLauncherName { get => _outputLauncherName; set { _outputLauncherName = value; OnPropertyChanged(); } }

        public ObservableCollection<string> LauncherTemplates { get; } = new ObservableCollection<string>();
        private string _selectedTemplate = "Default.dat";
        public string SelectedTemplate { get => _selectedTemplate; set { _selectedTemplate = value; OnPropertyChanged(); } }
        #endregion

        #region Server List Properties
        public ObservableCollection<ServerInfo> Servers { get; } = new ObservableCollection<ServerInfo>();
        private ServerInfo _selectedServer = default!;
        public ServerInfo SelectedServer { get => _selectedServer; set { _selectedServer = value; OnPropertyChanged(); } }

        public ObservableCollection<string> BdFiles { get; } = new ObservableCollection<string>();
        public ObservableCollection<string> BdSourceFiles { get; } = new ObservableCollection<string>();
        private string _selectedBdSourceFile = default!;
        public string SelectedBdSourceFile { get => _selectedBdSourceFile; set { _selectedBdSourceFile = value; OnPropertyChanged(); } }

        private string _listVersion = "1001";
        public string ListVersion { get => _listVersion; set { _listVersion = value; OnPropertyChanged(); } }

        private string _listUpdateUrl = "http://www.google.com/update/launcher.exe";
        public string ListUpdateUrl { get => _listUpdateUrl; set { _listUpdateUrl = value; OnPropertyChanged(); } }
        #endregion

        #region Patcher Properties
        private string _patchSourceDir = "";
        public string PatchSourceDir { get => _patchSourceDir; set { _patchSourceDir = value; OnPropertyChanged(); } }

        private string _patchOutputDir = "";
        public string PatchOutputDir { get => _patchOutputDir; set { _patchOutputDir = value; OnPropertyChanged(); } }

        private string _patchBaseUrl = "";
        public string PatchBaseUrl { get => _patchBaseUrl; set { _patchBaseUrl = value; OnPropertyChanged(); } }

        private int _compressionLevel = 0; // 0: Normal, 1: Fast, 2: Best
        public int CompressionLevel { get => _compressionLevel; set { _compressionLevel = value; OnPropertyChanged(); } }

        private string _ftpHost = "";
        public string FtpHost { get => _ftpHost; set { _ftpHost = value; OnPropertyChanged(); } }

        private int _ftpPort = 21;
        public int FtpPort { get => _ftpPort; set { _ftpPort = value; OnPropertyChanged(); } }

        private string _ftpUsername = "";
        public string FtpUsername { get => _ftpUsername; set { _ftpUsername = value; OnPropertyChanged(); } }

        private string _ftpPassword = "";
        public string FtpPassword { get => _ftpPassword; set { _ftpPassword = value; OnPropertyChanged(); } }

        private string _ftpRemoteDir = "";
        public string FtpRemoteDir { get => _ftpRemoteDir; set { _ftpRemoteDir = value; OnPropertyChanged(); } }
        #endregion

        #region BD Maker Properties
        private string _bdOutputDir = "";
        public string BdOutputDir { get => _bdOutputDir; set { _bdOutputDir = value; OnPropertyChanged(); } }
        #endregion

        private int _patchFileCount = 0;
        public int PatchFileCount { get => _patchFileCount; set { _patchFileCount = value; OnPropertyChanged(); } }

        private double _patchProgressPercent = 0.0;
        public double PatchProgressPercent { get => _patchProgressPercent; set { _patchProgressPercent = value; OnPropertyChanged(); } }

        private string _currentPatchFile = "";
        public string CurrentPatchFile { get => _currentPatchFile; set { _currentPatchFile = value; OnPropertyChanged(); } }

        private string _estimatedRemaining = "";
        public string EstimatedRemaining { get => _estimatedRemaining; set { _estimatedRemaining = value; OnPropertyChanged(); } }

        private bool _patchBusy;
        public ObservableCollection<PatchFileRow> PatchFileRows { get; } = new();

        private bool _uploadBusy;

        private double _uploadProgressPercent = 0.0;
        public double UploadProgressPercent { get => _uploadProgressPercent; set { _uploadProgressPercent = value; OnPropertyChanged(); } }

        private string _currentUploadFile = "";
        public string CurrentUploadFile { get => _currentUploadFile; set { _currentUploadFile = value; OnPropertyChanged(); } }

        private string _uploadEstimatedRemaining = "";
        public string UploadEstimatedRemaining { get => _uploadEstimatedRemaining; set { _uploadEstimatedRemaining = value; OnPropertyChanged(); } }

        public ICommand MakeCommand { get; }
        public ICommand GenerateListCommand { get; }
        public ICommand GeneratePatchCommand { get; }
        public ICommand UploadPatchCommand { get; }
        public ICommand PackagePakCommand { get; }
        public ICommand BrowsePatchSourceDirCommand { get; }
        public ICommand BrowsePatchOutputDirCommand { get; }
        public ICommand BrowseBdOutputDirCommand { get; }
        public ICommand GenerateRsaKeyCommand { get; }

        public EncoderViewModel()
        {
            try
            {
                LogService.Info("EncoderViewModel: 建構函式開始");
                
                LogService.Info("EncoderViewModel: 初始化 Links 與 Servers 列表");
                for (int i = 0; i < 5; i++) Links.Add(new LinkItem { DisplayIndex = i + 1 });
                for (int i = 0; i < 8; i++) Servers.Add(new ServerInfo { Name = "Server " + (i + 1), Ip = "127.0.0.1", Port = 2000, BdFile = "" });
                
                SelectedServer = Servers[0];

                LogService.Info("EncoderViewModel: 執行檔案掃描 (Templates, BD Files, Paks)");
                SearchLauncherTemplates();
                SearchBDFiles();
                SearchBdPaks();

                LogService.Info("EncoderViewModel: 綁定 RelayCommands");
                MakeCommand = new RelayCommand(_ => DoMake());
                GenerateListCommand = new RelayCommand(_ => DoGenerateList());
                GeneratePatchCommand = new RelayCommand(_ => { _ = DoGeneratePatchAsync(); }, _ => !_patchBusy);
                UploadPatchCommand = new RelayCommand(_ => { _ = DoUploadPatchAsync(); }, _ => !_uploadBusy);
                PackagePakCommand = new RelayCommand(_ => DoPackagePak());
                BrowsePatchSourceDirCommand = new RelayCommand(_ => BrowsePatchSourceDir());
                BrowsePatchOutputDirCommand = new RelayCommand(_ => BrowsePatchOutputDir());
                BrowseBdOutputDirCommand = new RelayCommand(_ => BrowseBdOutputDir());
                GenerateRsaKeyCommand = new RelayCommand(_ => DoGenerateRsaKey());

                LogService.Info("EncoderViewModel: 執行 LoadSettings()");
                LoadSettings();
                
                LogService.Info("EncoderViewModel: 建構函式完成");
            }
            catch (Exception ex)
            {
                LogService.Error("EncoderViewModel 建構函式發生致命錯誤", ex);
                throw; // 重新拋出以觸發 App 的全域報錯
            }
        }

        private void LoadSettings()
        {
            Title = _ini.Read("LauncherMaker", "title", "天堂登入器");
            Version = _ini.Read("LauncherMaker", "ver", "1001");
            Web = _ini.Read("LauncherMaker", "web", "http://www.google.com/");
            List = _ini.Read("LauncherMaker", "list", "http://www.google.com/list.txt");
            UseUpdate = _ini.ReadBool("LauncherMaker", "enable_update", false);
            UpdateUrl = _ini.Read("LauncherMaker", "update", "http://www.google.com/update.txt");
            for (int i = 0; i < 5; i++)
            {
                Links[i].Enabled = _ini.ReadBool("LauncherMaker", "link_enable" + (i + 1), false);
                Links[i].Name = _ini.Read("LauncherMaker", "link_name" + (i + 1), "Link " + (i + 1));
                Links[i].Url = _ini.Read("LauncherMaker", "link_url" + (i + 1), "http://www.google.com/");
            }
            Width = _ini.ReadInt("LauncherMaker", "width", 1000);
            Height = _ini.ReadInt("LauncherMaker", "height", 600);

            ListVersion = _ini.Read("ServerList", "ver", "1001");
            ListUpdateUrl = _ini.Read("ServerList", "update", "http://www.google.com/update/launcher.exe");
            for (int i = 0; i < 8; i++)
            {
                Servers[i].Name = _ini.Read("ServerList", "server_name" + (i + 1), Servers[i].Name);
                Servers[i].Ip = _ini.Read("ServerList", "server_ip" + (i + 1), Servers[i].Ip);
                Servers[i].Port = _ini.ReadInt("ServerList", "server_port" + (i + 1), Servers[i].Port);
                Servers[i].IsUsed = _ini.ReadBool("ServerList", "server_enable" + (i + 1), false);
                Servers[i].UseBd = _ini.ReadBool("ServerList", "server_usebd" + (i + 1), false);
                Servers[i].BdFile = _ini.Read("ServerList", "server_bdfile" + (i + 1), "");
            }

            CompressionLevel = _ini.ReadInt("PatcherMaker", "compress", 0);
            PatchBaseUrl = _ini.Read("PatcherMaker", "url", "");
            PatchSourceDir = _ini.Read("PatcherMaker", "patch_source", "");
            PatchOutputDir = _ini.Read("PatcherMaker", "patch_output", "");
            FtpHost = _ini.Read("PatcherMaker", "ftp_host", "");
            FtpPort = _ini.ReadInt("PatcherMaker", "ftp_port", 21);
            FtpUsername = _ini.Read("PatcherMaker", "ftp_user", "");
            FtpPassword = _ini.Read("PatcherMaker", "ftp_pass", "");
            FtpRemoteDir = _ini.Read("PatcherMaker", "ftp_remote_dir", "");
            BdOutputDir = _ini.Read("BdMaker", "bd_output_dir", "");
            RefreshPatchSourcePreview();
        }

        private void SaveSettings()
        {
            _ini.Write("LauncherMaker", "title", Title);
            _ini.Write("LauncherMaker", "ver", Version);
            _ini.Write("LauncherMaker", "web", Web);
            _ini.Write("LauncherMaker", "list", List);
            _ini.WriteBool("LauncherMaker", "enable_update", UseUpdate);
            _ini.Write("LauncherMaker", "update", UpdateUrl);
            for (int i = 0; i < 5; i++)
            {
                _ini.WriteBool("LauncherMaker", "link_enable" + (i + 1), Links[i].Enabled);
                _ini.Write("LauncherMaker", "link_name" + (i + 1), Links[i].Name);
                _ini.Write("LauncherMaker", "link_url" + (i + 1), Links[i].Url);
            }
            _ini.Write("LauncherMaker", "width", Width.ToString());
            _ini.Write("LauncherMaker", "height", Height.ToString());
            _ini.Write("LauncherMaker", "output_name", OutputLauncherName);
            _ini.Write("LauncherMaker", "template", SelectedTemplate);

            _ini.Write("ServerList", "ver", ListVersion);
            _ini.Write("ServerList", "update", ListUpdateUrl);
            for (int i = 0; i < 8; i++)
            {
                _ini.Write("ServerList", "server_name" + (i + 1), Servers[i].Name);
                _ini.Write("ServerList", "server_ip" + (i + 1), Servers[i].Ip);
                _ini.Write("ServerList", "server_port" + (i + 1), Servers[i].Port.ToString());
                _ini.WriteBool("ServerList", "server_enable" + (i + 1), Servers[i].IsUsed);
                _ini.WriteBool("ServerList", "server_usebd" + (i + 1), Servers[i].UseBd);
                _ini.Write("ServerList", "server_bdfile" + (i + 1), Servers[i].BdFile ?? "");
            }

            _ini.Write("PatcherMaker", "compress", CompressionLevel.ToString());
            _ini.Write("PatcherMaker", "url", PatchBaseUrl);
            _ini.Write("PatcherMaker", "patch_source", PatchSourceDir ?? "");
            _ini.Write("PatcherMaker", "patch_output", PatchOutputDir ?? "");
            _ini.Write("PatcherMaker", "ftp_host", FtpHost ?? "");
            _ini.Write("PatcherMaker", "ftp_port", FtpPort.ToString());
            _ini.Write("PatcherMaker", "ftp_user", FtpUsername ?? "");
            _ini.Write("PatcherMaker", "ftp_pass", FtpPassword ?? "");
            _ini.Write("PatcherMaker", "ftp_remote_dir", FtpRemoteDir ?? "");
            _ini.Write("BdMaker", "bd_output_dir", BdOutputDir ?? "");
        }

        private void SearchBDFiles()
        {
            BdSourceFiles.Clear();
            string baseDir = AppDomain.CurrentDomain.BaseDirectory;
            if (Directory.Exists(baseDir))
            {
                foreach (var file in Directory.GetFiles(baseDir, "*.txt"))
                {
                    string name = Path.GetFileNameWithoutExtension(file);
                    if (name.Equals("list", StringComparison.OrdinalIgnoreCase) ||
                        name.Equals("update", StringComparison.OrdinalIgnoreCase)) continue;
                    BdSourceFiles.Add(name);
                }
            }
        }

        private void SearchBdPaks()
        {
            BdFiles.Clear();
            string baseDir = AppDomain.CurrentDomain.BaseDirectory;
            if (Directory.Exists(baseDir))
            {
                foreach (var file in Directory.GetFiles(baseDir, "*.pak"))
                {
                    BdFiles.Add(Path.GetFileName(file));
                }
            }
        }

        private void SearchLauncherTemplates()
        {
            LauncherTemplates.Clear();
            string baseDir = AppDomain.CurrentDomain.BaseDirectory;
            if (Directory.Exists(baseDir))
            {
                foreach (var file in Directory.GetFiles(baseDir, "*.dat"))
                {
                    LauncherTemplates.Add(Path.GetFileName(file));
                }
            }
            if (LauncherTemplates.Count == 0) LauncherTemplates.Add("Default.dat");
            SelectedTemplate = LauncherTemplates[0];
        }

        /// <summary>
        /// EncoderForPartners 根目錄——Encoder.exe 實際跑在其下的 EncoderTool\ 子目錄，
        /// 殼 exe／login\／update\ 這些「要給客戶端」的產出都要往上一層寫回根目錄。
        /// </summary>
        private static string GetPartnersRootDir()
        {
            string baseDir = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\', '/');
            return Directory.GetParent(baseDir)?.FullName ?? baseDir;
        }

        /// <summary>
        /// 產生一組新的 RSA-32 金鑰（Rsa32Service，移植自 Rust rsa32.rs），寫入目前選中的伺服器槽位，
        /// 並在 EncoderTool\pack.properties（跟 LinEncoder.exe 同層）寫一份給伺服器端參考的設定檔——
        /// 操作者需要手動把內容合併到伺服器的 ./config/pack.properties 並重啟伺服器。
        /// </summary>
        private void DoGenerateRsaKey()
        {
            if (SelectedServer == null)
            {
                MessageBox.Show("請先選擇一個伺服器槽位。");
                return;
            }

            var key = Rsa32Service.Generate();
            SelectedServer.E = key.E;
            SelectedServer.D = key.D;
            SelectedServer.N = key.N;

            string packPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "pack.properties");
            string content = "; 由 LinEncoder.exe 產生 — 伺服器綑綁金鑰\n" +
                "; 請將此檔內容合併到伺服器的 ./config/pack.properties（Autoentication + RSA_KEY_E/D/N 三行）\n" +
                "Autoentication=True\n" +
                $"RSA_KEY_E={key.E}\n" +
                $"RSA_KEY_D={key.D}\n" +
                $"RSA_KEY_N={key.N}\n";
            File.WriteAllText(packPath, content, new UTF8Encoding(false));

            MessageBox.Show(
                $"已產生新金鑰並套用到「{SelectedServer.Name}」\n\nE = {key.E}\nD = {key.D}\nN = {key.N}\n\n" +
                $"已寫入 {packPath}\n（請將此檔內容合併到伺服器的 ./config/pack.properties 並重啟伺服器）",
                "產生金鑰", MessageBoxButton.OK, MessageBoxImage.Information);

            LogService.WriteOperationSummary(
                "產生金鑰",
                $"已為「{SelectedServer.Name}」產生新 RSA-32 金鑰（E={key.E}, D={key.D}, N={key.N}），寫入 `{packPath}`。",
                "1. 把 `pack.properties` 的內容合併到伺服器的 `./config/pack.properties`，重啟伺服器\n2. 用「產生清單」把新的 D/N 寫進 `login\\list.txt`\n3. 兩邊都更新後再測試登入");
        }

        private void DoMake()
        {
            SaveSettings();
            // 自動命名＋直接寫入 EncoderForPartners 最上層（跟 Core 同層）——
            // 這是要交給客戶端的殼 exe，不需要每次手動選存檔位置，重新產生就直接覆蓋舊檔。
            string safeName = string.IsNullOrWhiteSpace(OutputLauncherName) ? "Launcher" : OutputLauncherName;
            if (safeName.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
                safeName = safeName.Substring(0, safeName.Length - 4);
            string outputPath = Path.Combine(GetPartnersRootDir(), safeName + ".exe");

            var config = new LauncherConfig
            {
                Title = Title,
                Ver = Version,
                Web = Web,
                List = List,
                UseUpdate = UseUpdate,
                Update = UpdateUrl,
                Width = Width,
                Height = Height,
                Configed = true
            };
            for (int i = 0; i < 5; i++)
            {
                config.UseLink[i] = Links[i].Enabled;
                byte[] nameBytes = Encoding.Unicode.GetBytes(Links[i].Name.PadRight(16, '\0').Substring(0, 16));
                byte[] urlBytes = Encoding.Unicode.GetBytes(Links[i].Url.PadRight(256, '\0').Substring(0, 256));
                Array.Copy(nameBytes, 0, config.LinkNamesRaw, i * 32, 32);
                Array.Copy(urlBytes, 0, config.LinkUrlsRaw, i * 512, 512);
            }
            if (_encoderService.CreateLauncher(config, SelectedTemplate, outputPath))
            {
                MessageBox.Show($"產生成功！\n{outputPath}");
                LogService.WriteOperationSummary(
                    "產生登入器",
                    $"已產生登入殼 `{Path.GetFileName(outputPath)}`，寫入 `{outputPath}`。",
                    "1. 測試這個 exe 能否正常啟動並進入遊戲\n2. 確認 `Core\\` 是最新版（用 deploy.ps1 建置的版本）\n3. 確認後即可把這個 exe 連同 `Core\\` 一起交給客戶端");
            }
            else
            {
                MessageBox.Show($"產生失敗，請確認範本檔案存在：{SelectedTemplate}");
            }
        }

        private void DoGenerateList()
        {
            SaveSettings();
            byte[] key = Encoding.ASCII.GetBytes(Constants.ServerListKey);
            if (key.Length != 16)
            {
                MessageBox.Show("Constants.ServerListKey 必須為 16 個 ASCII 字元（須與 LinLauncher.Models.Constants.ServerListKey 一致）。", "LinEncoder");
                return;
            }

            var sb = new StringBuilder();
            sb.AppendLine("[list]");
            int idx = 0;
            for (int i = 0; i < Servers.Count; i++)
            {
                ServerInfo srv = Servers[i];
                if (!srv.IsUsed) continue;
                try
                {
                    ServerListEntryNative native = BuildListEntryNative(srv);
                    // 不補齊到 16 的倍數 — ConfigEncrypt 本身就支援非對齊長度（尾端只做 XOR，
                    // 不做 AES），補 padding 只會讓密文長度跟 Server_Info 原始格式（213 bytes）
                    // 對不起來，導致跟 Rust 工具／真實伺服器產生的 list.txt 無法互通。
                    byte[] buf = ListEntryMarshal.StructureToBytes(native);
                    CryptoService.ConfigEncrypt(key, buf);
                    sb.AppendLine($"ServerData{idx}={Convert.ToBase64String(buf)}");
                    idx++;
                }
                catch (Exception ex)
                {
                    MessageBox.Show($"伺服器 {i + 1} 打包失敗：{ex.Message}", "LinEncoder");
                    return;
                }
            }

            if (idx == 0)
            {
                MessageBox.Show("請至少啟用一筆伺服器（伺服器列表勾選「啟用」）。", "LinEncoder");
                return;
            }

            string loginDir = Path.Combine(GetPartnersRootDir(), "login");
            Directory.CreateDirectory(loginDir);
            string path = Path.Combine(loginDir, "list.txt");
            File.WriteAllText(path, sb.ToString(), new UTF8Encoding(false));
            MessageBox.Show($"已產生 list.txt（加密與 LinLauncher 登入器相容）：\n{path}", "LinEncoder");
            LogService.WriteOperationSummary(
                "產生清單",
                $"已產生 `login\\list.txt`（{idx} 筆啟用中的伺服器），寫入 `{path}`。",
                "把 `login\\` 整個資料夾上傳到 `LinEncoder.ini` 裡 `list=` 網址對應的路徑（例：`list=http://你的網址/login/list.txt` → 上傳到網站的 `/login/` 目錄）。");
        }

        /// <summary>對應 LinLauncher.Models.ServerListEntryNative 欄位。</summary>
        private static ServerListEntryNative BuildListEntryNative(ServerInfo srv)
        {
            string nameSrc = srv.Name ?? "";
            if (nameSrc.Length > 32) nameSrc = nameSrc.Substring(0, 32);
            string name = nameSrc.PadRight(32, '\0');

            string bdSrc = srv.BdFile ?? "";
            if (bdSrc.Length > 32) bdSrc = bdSrc.Substring(0, 32);
            string bd = bdSrc.PadRight(32, '\0');

            var n = new ServerListEntryNative
            {
                Name = name,
                IpBytes = new byte[32],
                Port = srv.Port,
                Used = srv.IsUsed,
                Key = new byte[16],
                Encrypt = srv.Encrypt,
                UseHelper = false,
                UseBd = srv.UseBd,
                BdFile = bd,
                RandKey = srv.RandKey,
                E = srv.E,
                D = srv.D,
                N = srv.N,
                Fix = new byte[16]
            };
            byte[] ip = Encoding.ASCII.GetBytes(srv.Ip ?? "127.0.0.1");
            Array.Copy(ip, n.IpBytes, Math.Min(32, ip.Length));
            return n;
        }

        private void BrowsePatchSourceDir()
        {
            string? path = ShowFolderDialog(PatchSourceDir, "選擇補丁來源目錄");
            if (path != null)
            {
                PatchSourceDir = path;
                SaveSettings();
                RefreshPatchSourcePreview();
            }
        }

        /// <summary>依目前「來源目錄」重新填入檔案清單（選完目錄或手動改路徑後呼叫）。</summary>
        public void RefreshPatchSourcePreview()
        {
            PatchFileRows.Clear();
            if (string.IsNullOrWhiteSpace(PatchSourceDir) || !Directory.Exists(PatchSourceDir))
            {
                PatchFileCount = 0;
                return;
            }

            try
            {
                foreach (PatchFileRow row in EncoderService.BuildPatchFilePreview(PatchSourceDir))
                    PatchFileRows.Add(row);
                PatchFileCount = PatchFileRows.Count;
            }
            catch (Exception ex)
            {
                LogService.Error("RefreshPatchSourcePreview", ex);
                PatchFileCount = 0;
            }
        }

        private void BrowsePatchOutputDir()
        {
            string? path = ShowFolderDialog(PatchOutputDir, "選擇補丁輸出目錄");
            if (path != null)
            {
                PatchOutputDir = path;
                SaveSettings();
            }
        }

        private void BrowseBdOutputDir()
        {
            string? path = ShowFolderDialog(BdOutputDir, "選擇變身檔輸出目錄");
            if (path != null)
            {
                BdOutputDir = path;
                SaveSettings();
            }
        }

        private static string? ShowFolderDialog(string? initialPath, string description)
        {
            using var dlg = new System.Windows.Forms.FolderBrowserDialog
            {
                Description = description,
                UseDescriptionForTitle = true,
                ShowNewFolderButton = true
            };
            if (!string.IsNullOrWhiteSpace(initialPath))
            {
                try
                {
                    string full = Path.GetFullPath(initialPath);
                    if (Directory.Exists(full))
                        dlg.SelectedPath = full;
                }
                catch
                {
                    /* ignore invalid path */
                }
            }

            System.Windows.Forms.DialogResult r;
            Window? main = Application.Current?.MainWindow;
            if (main != null)
            {
                IntPtr owner = new WindowInteropHelper(main).Handle;
                if (owner != IntPtr.Zero)
                    r = dlg.ShowDialog(new Win32Window(owner));
                else
                    r = dlg.ShowDialog();
            }
            else
            {
                r = dlg.ShowDialog();
            }

            return r == System.Windows.Forms.DialogResult.OK ? dlg.SelectedPath : null;
        }

        private sealed class Win32Window : System.Windows.Forms.IWin32Window
        {
            public IntPtr Handle { get; }
            public Win32Window(IntPtr handle) => Handle = handle;
        }

        private async Task DoGeneratePatchAsync()
        {
            if (_patchBusy)
                return;
            _patchBusy = true;
            CommandManager.InvalidateRequerySuggested();
            PatchProgressPercent = 0;
            CurrentPatchFile = "";
            EstimatedRemaining = "…";
            var sw = Stopwatch.StartNew();

            try
            {
                SaveSettings();
                if (string.IsNullOrWhiteSpace(PatchSourceDir) || !Directory.Exists(PatchSourceDir))
                {
                    MessageBox.Show("請選擇有效的來源目錄。", "補丁打包", MessageBoxButton.OK,
                        MessageBoxImage.Warning);
                    return;
                }

                if (string.IsNullOrWhiteSpace(PatchOutputDir))
                {
                    MessageBox.Show("請選擇輸出目錄。", "補丁打包", MessageBoxButton.OK,
                        MessageBoxImage.Warning);
                    return;
                }

                System.Windows.Threading.Dispatcher? ui = Application.Current?.Dispatcher;
                var progress = new Progress<(int current, int total, string relativePath)>(p =>
                {
                    void Apply()
                    {
                        if (p.total <= 0)
                        {
                            PatchProgressPercent = 0;
                        }
                        else if (p.current <= 0)
                        {
                            PatchProgressPercent = 0;
                        }
                        else
                        {
                            PatchProgressPercent = Math.Min(100.0,
                                Math.Round(100.0 * p.current / p.total, 1));
                        }

                        CurrentPatchFile = string.IsNullOrEmpty(p.relativePath)
                            ? "準備中…"
                            : p.relativePath;

                        double pct = PatchProgressPercent;
                        if (pct > 0.5 && sw.Elapsed.TotalSeconds > 0)
                        {
                            double totalEstSec = sw.Elapsed.TotalSeconds * 100.0 / pct;
                            double rem = Math.Max(0, totalEstSec - sw.Elapsed.TotalSeconds);
                            EstimatedRemaining = $"{rem:F0} 秒";
                        }
                        else
                            EstimatedRemaining = "…";
                    }

                    if (ui != null && !ui.CheckAccess())
                        ui.Invoke(Apply);
                    else
                        Apply();
                });

                PatchPackageResult result = await Task.Run(() =>
                    _encoderService.BuildUpdatePackage(
                        PatchSourceDir,
                        PatchOutputDir,
                        PatchBaseUrl ?? "",
                        CompressionLevel,
                        progress));

                if (result.Success)
                {
                    PatchFileRows.Clear();
                    foreach (PatchFileRow f in result.Files)
                        PatchFileRows.Add(f);
                    PatchFileCount = result.Files.Count;
                    PatchProgressPercent = 100;
                    EstimatedRemaining = "0 秒";
                    MessageBox.Show(
                        $"打包完成。\n\n更新清單：\n{result.UpdateListPath}\n\n" +
                        "產物位於「輸出目錄」根目錄：update.txt 與各相對路徑.bin。請上傳至網站；「下載網址」請填可對應到此路徑的基底 URL（與 update.txt 內 [main] url 一致，且能下載 相對路徑.bin）。",
                        "補丁打包",
                        MessageBoxButton.OK,
                        MessageBoxImage.Information);
                    LogService.WriteOperationSummary(
                        "補丁產生工具",
                        $"已把 `{PatchSourceDir}` 的素材（共 {result.Files.Count} 個檔案）打包到 `{PatchOutputDir}`，更新清單：`{result.UpdateListPath}`。",
                        $"把 `{PatchOutputDir}` 整個資料夾上傳到網站，路徑要對齊「下載網址」（`{PatchBaseUrl}`）。");
                }
                else
                {
                    PatchProgressPercent = 0;
                    MessageBox.Show(result.ErrorMessage ?? "未知錯誤", "補丁打包失敗", MessageBoxButton.OK,
                        MessageBoxImage.Error);
                }
            }
            finally
            {
                _patchBusy = false;
                CommandManager.InvalidateRequerySuggested();
            }
        }

        private async Task DoUploadPatchAsync()
        {
            if (_uploadBusy)
                return;
            _uploadBusy = true;
            CommandManager.InvalidateRequerySuggested();
            UploadProgressPercent = 0;
            CurrentUploadFile = "";
            UploadEstimatedRemaining = "…";
            var sw = Stopwatch.StartNew();

            try
            {
                SaveSettings();
                if (string.IsNullOrWhiteSpace(PatchOutputDir) || !Directory.Exists(PatchOutputDir))
                {
                    MessageBox.Show("找不到補丁輸出目錄，請先執行「產生補丁」。", "補丁上傳", MessageBoxButton.OK,
                        MessageBoxImage.Warning);
                    return;
                }

                if (string.IsNullOrWhiteSpace(FtpHost))
                {
                    MessageBox.Show("請填寫 FTP 主機。", "補丁上傳", MessageBoxButton.OK, MessageBoxImage.Warning);
                    return;
                }

                System.Windows.Threading.Dispatcher? ui = Application.Current?.Dispatcher;
                var progress = new Progress<(int current, int total, string relativePath)>(p =>
                {
                    void Apply()
                    {
                        UploadProgressPercent = p.total <= 0 ? 0 : Math.Min(100.0, Math.Round(100.0 * p.current / p.total, 1));
                        CurrentUploadFile = string.IsNullOrEmpty(p.relativePath) ? "準備中…" : p.relativePath;

                        double pct = UploadProgressPercent;
                        if (pct > 0.5 && sw.Elapsed.TotalSeconds > 0)
                        {
                            double totalEstSec = sw.Elapsed.TotalSeconds * 100.0 / pct;
                            double rem = Math.Max(0, totalEstSec - sw.Elapsed.TotalSeconds);
                            UploadEstimatedRemaining = $"{rem:F0} 秒";
                        }
                        else
                            UploadEstimatedRemaining = "…";
                    }

                    if (ui != null && !ui.CheckAccess())
                        ui.Invoke(Apply);
                    else
                        Apply();
                });

                FtpUploadResult result = await Task.Run(() =>
                    FtpUploadService.UploadDirectory(
                        PatchOutputDir,
                        FtpHost,
                        FtpPort,
                        FtpUsername,
                        FtpPassword,
                        FtpRemoteDir,
                        progress));

                if (result.Success)
                {
                    UploadProgressPercent = 100;
                    UploadEstimatedRemaining = "0 秒";
                    MessageBox.Show(
                        $"上傳完成，共 {result.UploadedCount} 個檔案。",
                        "補丁上傳",
                        MessageBoxButton.OK,
                        MessageBoxImage.Information);
                    LogService.WriteOperationSummary(
                        "補丁上傳",
                        $"已把 `{PatchOutputDir}`（共 {result.UploadedCount} 個檔案）上傳到 `ftp://{FtpHost}:{FtpPort}/{FtpRemoteDir}`。",
                        $"確認下載網址（`{PatchBaseUrl}`）能對應到這個 FTP 遠端目錄，再讓登入器測試更新。");
                }
                else
                {
                    UploadProgressPercent = 0;
                    MessageBox.Show(result.ErrorMessage ?? "未知錯誤", "補丁上傳失敗", MessageBoxButton.OK,
                        MessageBoxImage.Error);
                }
            }
            finally
            {
                _uploadBusy = false;
                CommandManager.InvalidateRequerySuggested();
            }
        }

        private void DoPackagePak()
        {
            if (string.IsNullOrEmpty(SelectedBdSourceFile))
            {
                MessageBox.Show("請先選擇要加密的來源文字檔 (.txt)！");
                return;
            }

            // 沒有記錄過輸出目錄就先跳瀏覽視窗讓使用者選一次；選了才繼續，取消就中止打包
            if (string.IsNullOrWhiteSpace(BdOutputDir))
            {
                BrowseBdOutputDir();
                if (string.IsNullOrWhiteSpace(BdOutputDir))
                    return;
            }

            string input = SelectedBdSourceFile + ".txt";
            string? fileName = SelectedServer?.BdFile;
            if (string.IsNullOrEmpty(fileName))
            {
                fileName = SelectedBdSourceFile + ".pak";
            }
            string output = Path.Combine(BdOutputDir, fileName);

            if (_encoderService.PackagePak(input, output))
            {
                MessageBox.Show($"加密完成！\n輸出檔案：{output}", "完成", MessageBoxButton.OK, MessageBoxImage.Information);
                SearchBdPaks();
            }
            else
            {
                MessageBox.Show("加密失敗！請檢查來源檔案是否存在。", "錯誤", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }
    }

    public class LinkItem : BaseViewModel
    {
        public int DisplayIndex { get; set; }
        private bool _enabled;
        public bool Enabled { get => _enabled; set { _enabled = value; OnPropertyChanged(); } }
        private string _name = "";
        public string Name { get => _name; set { _name = value; OnPropertyChanged(); } }
        private string _url = "";
        public string Url { get => _url; set { _url = value; OnPropertyChanged(); } }
    }
}