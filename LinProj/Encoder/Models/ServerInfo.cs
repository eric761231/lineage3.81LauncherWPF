using System;
using LinEncoder.ViewModels;

namespace LinEncoder.Models
{
    /// <summary>
    /// 伺服器設定資訊資料模型。
    /// 移除 [StructLayout]，因為繼承了 BaseViewModel 的類別無法使用 Sequential 佈局，避免 TypeLoadException 導致啟動閃退。
    /// </summary>
    // 移除 [StructLayout]，因為繼承了 BaseViewModel 的類別無法使用 Sequential 佈局
    // 這將解決 TypeLoadException 導致的啟動閃退
    public class ServerInfo : BaseViewModel
    {
        private string _name = "";

        /// <summary>
        /// 伺服器名稱。
        /// </summary>
        public string Name { get => _name; set { _name = value; OnPropertyChanged(); } }

        private string _ip = "";

        /// <summary>
        /// 伺服器 IP 位址。
        /// </summary>
        public string Ip { get => _ip; set { _ip = value; OnPropertyChanged(); } }

        private int _port = 2000;

        /// <summary>
        /// 伺服器 通訊埠 (Port)。
        /// </summary>
        public int Port { get => _port; set { _port = value; OnPropertyChanged(); } }

        // 預設值對齊目前 pack.properties（RSA_KEY_E/D/N + Autoentication + RandomEnc）
        private uint _e = 746996399;

        /// <summary>
        /// RSA 金鑰 E 參數。
        /// </summary>
        public uint E
        {
            get => _e;
            set { _e = value; OnPropertyChanged(); OnPropertyChanged(nameof(EText)); }
        }

        /// <summary>
        /// RSA 金鑰 E 參數文字表現。
        /// </summary>
        public string EText
        {
            get => _e.ToString();
            set
            {
                if (TryParseKey(value, out uint v))
                {
                    E = v;
                }
                else
                {
                    OnPropertyChanged();
                }
            }
        }

        private uint _d = 365159519;

        /// <summary>
        /// RSA 金鑰 D 參數。
        /// </summary>
        public uint D
        {
            get => _d;
            set { _d = value; OnPropertyChanged(); OnPropertyChanged(nameof(DText)); }
        }

        /// <summary>
        /// RSA 金鑰 D 參數文字表現。
        /// </summary>
        public string DText
        {
            get => _d.ToString();
            set
            {
                if (TryParseKey(value, out uint v))
                {
                    D = v;
                }
                else
                {
                    OnPropertyChanged();
                }
            }
        }

        private uint _n = 1833162673;

        /// <summary>
        /// RSA 金鑰 N 參數。
        /// </summary>
        public uint N
        {
            get => _n;
            set { _n = value; OnPropertyChanged(); OnPropertyChanged(nameof(NText)); }
        }

        /// <summary>
        /// RSA 金鑰 N 參數文字表現。
        /// </summary>
        public string NText
        {
            get => _n.ToString();
            set
            {
                if (TryParseKey(value, out uint v))
                {
                    N = v;
                }
                else
                {
                    OnPropertyChanged();
                }
            }
        }

        /// <summary>
        /// 嘗試解析金鑰字串數值。
        /// </summary>
        /// <param name="text">金鑰字串</param>
        /// <param name="value">輸出數值</param>
        /// <return>解析成功回傳 true，否則為 false</return>
        private static bool TryParseKey(string? text, out uint value)
        {
            return uint.TryParse((text ?? "").Trim(), out value);
        }

        private bool _isUsed;

        /// <summary>
        /// 是否啟用此伺服器。
        /// </summary>
        public bool IsUsed { get => _isUsed; set { _isUsed = value; OnPropertyChanged(); } }

        private bool _useBd;

        /// <summary>
        /// 是否使用邊界/封包數據。
        /// </summary>
        public bool UseBd { get => _useBd; set { _useBd = value; OnPropertyChanged(); } }

        private string? _bdFile = "";

        /// <summary>
        /// 邊界數據檔案名稱。
        /// </summary>
        public string? BdFile { get => _bdFile; set { _bdFile = value; OnPropertyChanged(); } }

        private bool _encrypt = true;

        /// <summary>
        /// 是否進行傳輸加密。
        /// </summary>
        public bool Encrypt { get => _encrypt; set { _encrypt = value; OnPropertyChanged(); } }

        private bool _randKey = true;

        /// <summary>
        /// 是否使用動態隨機金鑰。
        /// </summary>
        public bool RandKey { get => _randKey; set { _randKey = value; OnPropertyChanged(); } }
    }
}
