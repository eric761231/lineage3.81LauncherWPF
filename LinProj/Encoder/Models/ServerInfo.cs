using System.Runtime.InteropServices;
using LinEncoder.ViewModels;

namespace LinEncoder.Models
{
    // 移除 [StructLayout]，因為繼承了 BaseViewModel 的類別無法使用 Sequential 佈局
    // 這將解決 TypeLoadException 導致的啟動閃退
    public class ServerInfo : BaseViewModel
    {
        private string _name = "";
        public string Name { get => _name; set { _name = value; OnPropertyChanged(); } }

        private string _ip = "";
        public string Ip { get => _ip; set { _ip = value; OnPropertyChanged(); } }

        private int _port = 2000;
        public int Port { get => _port; set { _port = value; OnPropertyChanged(); } }

        // 預設值對齊目前 pack.properties 的正確金鑰，避免還沒按過「產生金鑰」的既有設定檔變成 0
        private uint _e = 65537;
        public uint E { get => _e; set { _e = value; OnPropertyChanged(); } }

        private uint _d = 2789970495;
        public uint D { get => _d; set { _d = value; OnPropertyChanged(); } }

        private uint _n = 2591889826;
        public uint N { get => _n; set { _n = value; OnPropertyChanged(); } }

        private bool _isUsed;
        public bool IsUsed { get => _isUsed; set { _isUsed = value; OnPropertyChanged(); } }

        private bool _useBd;
        public bool UseBd { get => _useBd; set { _useBd = value; OnPropertyChanged(); } }

        private string? _bdFile = "";
        public string? BdFile { get => _bdFile; set { _bdFile = value; OnPropertyChanged(); } }

        private bool _encrypt;
        public bool Encrypt { get => _encrypt; set { _encrypt = value; OnPropertyChanged(); } }

        private bool _randKey;
        public bool RandKey { get => _randKey; set { _randKey = value; OnPropertyChanged(); } }
    }
}