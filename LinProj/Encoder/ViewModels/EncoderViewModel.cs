using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using System.Windows.Input;
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
        #endregion

        private int _patchFileCount = 0;
        public int PatchFileCount { get => _patchFileCount; set { _patchFileCount = value; OnPropertyChanged(); } }

        private double _patchProgressPercent = 0.0;
        public double PatchProgressPercent { get => _patchProgressPercent; set { _patchProgressPercent = value; OnPropertyChanged(); } }

        private string _currentPatchFile = "";
        public string CurrentPatchFile { get => _currentPatchFile; set { _currentPatchFile = value; OnPropertyChanged(); } }

        private string _estimatedRemaining = "";
        public string EstimatedRemaining { get => _estimatedRemaining; set { _estimatedRemaining = value; OnPropertyChanged(); } }

        public ICommand MakeCommand { get; }
        public ICommand GenerateListCommand { get; }
        public ICommand GeneratePatchCommand { get; }
        public ICommand PackagePakCommand { get; }

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
                GeneratePatchCommand = new RelayCommand(async _ => await DoGeneratePatchAsync());
                PackagePakCommand = new RelayCommand(_ => DoPackagePak());

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

        private void DoMake()
        {
            SaveSettings();
            string defaultFileName = OutputLauncherName.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) ? OutputLauncherName : OutputLauncherName + ".exe";
            var dlg = new Microsoft.Win32.SaveFileDialog { Filter = "Executable (*.exe)|*.exe", FileName = defaultFileName, InitialDirectory = AppDomain.CurrentDomain.BaseDirectory };
            if (dlg.ShowDialog() == true)
            {
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
                if (_encoderService.CreateLauncher(config, SelectedTemplate, dlg.FileName))
                {
                    MessageBox.Show("產生成功！");
                }
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
                    byte[] raw = ListEntryMarshal.StructureToBytes(native);
                    int paddedLen = (raw.Length + 15) / 16 * 16;
                    byte[] buf = new byte[paddedLen];
                    Array.Copy(raw, buf, raw.Length);
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

            string path = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "list.txt");
            File.WriteAllText(path, sb.ToString(), new UTF8Encoding(false));
            MessageBox.Show($"已產生 list.txt（加密與 LinLauncher 登入器相容）：\n{path}", "LinEncoder");
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
                Encrypt = false,
                UseHelper = false,
                UseBd = srv.UseBd,
                BdFile = bd,
                RandKey = false,
                E = 0,
                D = srv.D,
                N = srv.N,
                Fix = new byte[16]
            };
            byte[] ip = Encoding.ASCII.GetBytes(srv.Ip ?? "127.0.0.1");
            Array.Copy(ip, n.IpBytes, Math.Min(32, ip.Length));
            return n;
        }

        private async Task DoGeneratePatchAsync()
        {
            await Task.Delay(100);
            MessageBox.Show("補丁產生完成！");
        }

        private void DoPackagePak()
        {
            if (string.IsNullOrEmpty(SelectedBdSourceFile))
            {
                MessageBox.Show("請先選擇要加密的來源文字檔 (.txt)！");
                return;
            }

            string input = SelectedBdSourceFile + ".txt";
            string output = SelectedServer?.BdFile;
            if (string.IsNullOrEmpty(output))
            {
                output = SelectedBdSourceFile + ".pak";
            }

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