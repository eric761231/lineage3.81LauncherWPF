using System;
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

        // 預設值對齊目前 pack.properties（RSA_KEY_E/D/N + Autoentication + RandomEnc）
        private uint _e = 746996399;
        public uint E
        {
            get => _e;
            set { _e = value; OnPropertyChanged(); OnPropertyChanged(nameof(EText)); }
        }
        public string EText
        {
            get => _e.ToString();
            set { if (TryParseKey(value, out uint v)) E = v; else OnPropertyChanged(); }
        }

        private uint _d = 365159519;
        public uint D
        {
            get => _d;
            set { _d = value; OnPropertyChanged(); OnPropertyChanged(nameof(DText)); }
        }
        public string DText
        {
            get => _d.ToString();
            set { if (TryParseKey(value, out uint v)) D = v; else OnPropertyChanged(); }
        }

        private uint _n = 1833162673;
        public uint N
        {
            get => _n;
            set { _n = value; OnPropertyChanged(); OnPropertyChanged(nameof(NText)); }
        }
        public string NText
        {
            get => _n.ToString();
            set { if (TryParseKey(value, out uint v)) N = v; else OnPropertyChanged(); }
        }

        private static bool TryParseKey(string? text, out uint value)
        {
            return uint.TryParse((text ?? "").Trim(), out value);
        }

        private bool _isUsed;
        public bool IsUsed { get => _isUsed; set { _isUsed = value; OnPropertyChanged(); } }

        private bool _useBd;
        public bool UseBd { get => _useBd; set { _useBd = value; OnPropertyChanged(); } }

        private string? _bdFile = "";
        public string? BdFile { get => _bdFile; set { _bdFile = value; OnPropertyChanged(); } }

        private bool _encrypt = true;
        public bool Encrypt { get => _encrypt; set { _encrypt = value; OnPropertyChanged(); } }

        private bool _randKey = true;
        public bool RandKey { get => _randKey; set { _randKey = value; OnPropertyChanged(); } }
    }
}
