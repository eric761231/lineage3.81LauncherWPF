using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using System.Windows.Input;
using LinLauncher.Models;
using LinLauncher.Services;

namespace LinLauncher.ViewModels
{
    public class EncoderViewModel : BaseViewModel
    {
        private readonly EncoderService _encoderService = new EncoderService();
        private readonly IniService _ini = new IniService("./Encoder.ini");

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

        private string _helper = "";
        public string Helper { get => _helper; set { _helper = value; OnPropertyChanged(); } }

        private int _width = 1000;
        public int Width { get => _width; set { _width = value; OnPropertyChanged(); } }

        private int _height = 600;
        public int Height { get => _height; set { _height = value; OnPropertyChanged(); } }
        #endregion

        #region Server List Properties
        public ObservableCollection<ServerInfo> Servers { get; } = new ObservableCollection<ServerInfo>();
        private ServerInfo _selectedServer;
        public ServerInfo SelectedServer { get => _selectedServer; set { _selectedServer = value; OnPropertyChanged(); } }

        public ObservableCollection<string> BdFiles { get; } = new ObservableCollection<string>();
        public ObservableCollection<string> BdSourceFiles { get; } = new ObservableCollection<string>();
        private string _selectedBdSourceFile;
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

        public ICommand MakeCommand { get; }
        public ICommand GenerateListCommand { get; }
        public ICommand GeneratePatchCommand { get; }
        public ICommand PackagePakCommand { get; }
        public ICommand GenerateRsaCommand { get; }

        public EncoderViewModel()
        {
            for (int i = 0; i < 5; i++) Links.Add(new LinkItem { DisplayIndex = i + 1 });
            for (int i = 0; i < 8; i++) Servers.Add(new ServerInfo { Name = $"Server {i + 1}", Ip = "127.0.0.1", Port = 2000 });
            SelectedServer = Servers[0];

            // Mock some .pak files for the UI
            SearchBDFiles();
            SearchBdPaks();

            MakeCommand = new RelayCommand(_ => DoMake());
            GenerateListCommand = new RelayCommand(_ => DoGenerateList());
            GeneratePatchCommand = new RelayCommand(_ => DoGeneratePatch());
            PackagePakCommand = new RelayCommand(_ => DoPackagePak());
            GenerateRsaCommand = new RelayCommand(_ => DoGenerateRsa());

            LoadSettings();
        }

        private void LoadSettings()
        {
            // Maker
            Title = _ini.Read("LauncherMaker", "title", "天堂登入器");
            Version = _ini.Read("LauncherMaker", "ver", "1001");
            Web = _ini.Read("LauncherMaker", "web", "http://www.google.com/");
            List = _ini.Read("LauncherMaker", "list", "http://www.google.com/list.txt");
            UseUpdate = _ini.ReadBool("LauncherMaker", "enable_update", false);
            UpdateUrl = _ini.Read("LauncherMaker", "update", "http://www.google.com/update.txt");
            for (int i = 0; i < 5; i++)
            {
                Links[i].Enabled = _ini.ReadBool("LauncherMaker", $"link_enable{i + 1}", false);
                Links[i].Name = _ini.Read("LauncherMaker", $"link_name{i + 1}", $"Link {i + 1}");
                Links[i].Url = _ini.Read("LauncherMaker", $"link_url{i + 1}", "http://www.google.com/");
            }
            Helper = _ini.Read("LauncherMaker", "helper", "");
            Width = _ini.ReadInt("LauncherMaker", "width", 1000);
            Height = _ini.ReadInt("LauncherMaker", "height", 600);

            // ServerList
            ListVersion = _ini.Read("ServerList", "ver", "1001");
            ListUpdateUrl = _ini.Read("ServerList", "update", "http://www.google.com/update/launcher.exe");
            for (int i = 0; i < 8; i++)
            {
                Servers[i].Name = _ini.Read("ServerList", $"server_name{i + 1}", Servers[i].Name);
                Servers[i].Ip = _ini.Read("ServerList", $"server_ip{i + 1}", Servers[i].Ip);
                Servers[i].Port = _ini.ReadInt("ServerList", $"server_port{i + 1}", Servers[i].Port);
                Servers[i].IsUsed = _ini.ReadBool("ServerList", $"server_enable{i + 1}", false);
                Servers[i].Encrypt = _ini.ReadBool("ServerList", $"server_encrypt{i + 1}", true);
                Servers[i].UseBd = _ini.ReadBool("ServerList", $"server_usebd{i + 1}", false);
                Servers[i].BdFile = _ini.Read("ServerList", $"server_bdfile{i + 1}", "");
                Servers[i].UseHelper = _ini.ReadBool("ServerList", $"server_helper{i + 1}", true);
                Servers[i].RandKey = _ini.ReadBool("ServerList", $"server_randkey{i + 1}", false);
            }

            // Patcher
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
                _ini.WriteBool("LauncherMaker", $"link_enable{i + 1}", Links[i].Enabled);
                _ini.Write("LauncherMaker", $"link_name{i + 1}", Links[i].Name);
                _ini.Write("LauncherMaker", $"link_url{i + 1}", Links[i].Url);
            }
            _ini.Write("LauncherMaker", "helper", Helper);
            _ini.Write("LauncherMaker", "width", Width.ToString());
            _ini.Write("LauncherMaker", "height", Height.ToString());

            _ini.Write("ServerList", "ver", ListVersion);
            _ini.Write("ServerList", "update", ListUpdateUrl);
            for (int i = 0; i < 8; i++)
            {
                _ini.Write("ServerList", $"server_name{i + 1}", Servers[i].Name);
                _ini.Write("ServerList", $"server_ip{i + 1}", Servers[i].Ip);
                _ini.Write("ServerList", $"server_port{i + 1}", Servers[i].Port.ToString());
                _ini.WriteBool("ServerList", $"server_enable{i + 1}", Servers[i].IsUsed);
                _ini.WriteBool("ServerList", $"server_encrypt{i + 1}", Servers[i].Encrypt);
                _ini.WriteBool("ServerList", $"server_usebd{i + 1}", Servers[i].UseBd);
                _ini.Write("ServerList", $"server_bdfile{i + 1}", Servers[i].BdFile ?? "");
                _ini.WriteBool("ServerList", $"server_helper{i + 1}", Servers[i].UseHelper);
                _ini.WriteBool("ServerList", $"server_randkey{i + 1}", Servers[i].RandKey);
            }

            _ini.Write("PatcherMaker", "compress", CompressionLevel.ToString());
            _ini.Write("PatcherMaker", "url", PatchBaseUrl);
        }

        private void SearchBDFiles()
        {
            BdSourceFiles.Clear();
            if (Directory.Exists("."))
            {
                foreach (var file in Directory.GetFiles(".", "*.txt"))
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
            if (Directory.Exists("."))
            {
                foreach (var file in Directory.GetFiles(".", "*.pak"))
                {
                    BdFiles.Add(Path.GetFileName(file));
                }
            }
        }
        private void DoMake()
        {
            SaveSettings();
            var dlg = new Microsoft.Win32.SaveFileDialog { Filter = "Executable (*.exe)|*.exe", FileName = "LinLauncher.exe" };
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
                    Helper = Helper,
                    Width = Width,
                    Height = Height
                };
                for (int i = 0; i < 5; i++)
                {
                    config.UseLink[i] = Links[i].Enabled;
                    // Need to convert string to fixed bytes for LinkNamesRaw and LinkUrlsRaw
                    byte[] nameBytes = Encoding.Unicode.GetBytes(Links[i].Name.PadRight(16, '\0').Substring(0, 16));
                    byte[] urlBytes = Encoding.Unicode.GetBytes(Links[i].Url.PadRight(256, '\0').Substring(0, 256));
                    Array.Copy(nameBytes, 0, config.LinkNamesRaw, i * 32, 32);
                    Array.Copy(urlBytes, 0, config.LinkUrlsRaw, i * 512, 512);
                }

                if (_encoderService.CreateLauncher(config, "LinLauncher.dat", dlg.FileName))
                {
                    MessageBox.Show("產生登入器成功！");
                }
                else
                {
                    MessageBox.Show("產生登入器失敗（找不到範本或簽章錯誤）。");
                }
            }
        }

        private void DoGenerateList()
        {
            SaveSettings();
            StringBuilder sb = new StringBuilder();
            sb.AppendLine("[update]");
            sb.AppendLine($"ver={ListVersion}");
            sb.AppendLine($"url={ListUpdateUrl}");
            sb.AppendLine();
            sb.AppendLine("[list]");

            int count = 0;
            byte[] listKey = Encoding.ASCII.GetBytes(Constants.ServerListKey);
            
            // Read RSA from pack.properties
            string rsaD = _ini.Read("PACK", "RSA_KEY_D", "0", "./pack.properties");
            string rsaN = _ini.Read("PACK", "RSA_KEY_N", "0", "./pack.properties");
            uint d = uint.TryParse(rsaD, out uint du) ? du : 0;
            uint n = uint.TryParse(rsaN, out uint nu) ? nu : 0;

            foreach (var s in Servers.Where(x => x.IsUsed))
            {
                s.D = d ^ Constants.ServerListRsaXorD;
                s.N = n ^ Constants.ServerListRsaXorN;
                
                int size = Marshal.SizeOf(typeof(ServerInfo));
                byte[] data = new byte[size];
                IntPtr ptr = Marshal.AllocHGlobal(size);
                try {
                    Marshal.StructureToPtr(s, ptr, false);
                    Marshal.Copy(ptr, data, 0, size);
                } finally { Marshal.FreeHGlobal(ptr); }

                CryptoService.ConfigEncrypt(listKey, data);
                sb.AppendLine($"ServerData{count}={Convert.ToBase64String(data)}");
                count++;
            }
            File.WriteAllText("./list.txt", sb.ToString(), Encoding.Default);
            MessageBox.Show("已儲存 list.txt！");
        }

        private void DoGeneratePatch()
        {
            // Simplified: user should select from UI
            if (string.IsNullOrEmpty(PatchSourceDir) || string.IsNullOrEmpty(PatchOutputDir))
            {
                MessageBox.Show("請選擇來源與輸出目錄。");
                return;
            }
            SaveSettings();
            var files = Directory.GetFiles(PatchSourceDir, "*.*", SearchOption.AllDirectories);
            List<string> lines = new List<string>();
            foreach (var f in files)
            {
                string rel = Path.GetRelativePath(PatchSourceDir, f);
                _encoderService.GeneratePatch(rel, File.ReadAllBytes(f), CompressionLevel, PatchOutputDir, PatchBaseUrl, lines);
            }
            
            StringBuilder sb = new StringBuilder();
            sb.AppendLine("[main]");
            sb.AppendLine($"count={files.Length}");
            sb.AppendLine($"url={PatchBaseUrl}");
            sb.AppendLine("[update]");
            for (int i = 0; i < files.Length; i++)
            {
                sb.AppendLine($"file_{i}={lines[i*2]}");
                sb.AppendLine($"md5_{i}={lines[i*2 + 1]}");
            }
            File.WriteAllText(Path.Combine(PatchOutputDir, "update.txt"), sb.ToString(), Encoding.Default);
            MessageBox.Show("補丁產生完成！");
        }

        private void DoPackagePak()
        {
            if (string.IsNullOrEmpty(SelectedBdSourceFile)) return;
            string input = SelectedBdSourceFile + ".txt";
            string output = SelectedBdSourceFile + ".pak";
            if (_encoderService.PackagePak(input, output))
            {
                MessageBox.Show("加密完成。", "提示", MessageBoxButton.OK, MessageBoxImage.Information);
                SearchBdPaks();
            }
        }

        private void DoGenerateRsa()
        {
            var keys = _encoderService.GenerateRSAKey();
            // Write to pack.properties
            var ppi = new IniService("./pack.properties");
            ppi.Write("PACK", "RSA_KEY_E", keys.e.ToString());
            ppi.Write("PACK", "RSA_KEY_D", keys.d.ToString());
            ppi.Write("PACK", "RSA_KEY_N", keys.n.ToString());
            MessageBox.Show("已產生新的 RSA 金鑰並寫入 pack.properties。");
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
