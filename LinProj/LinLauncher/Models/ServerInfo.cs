using System;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Windows.Media;

namespace LinLauncher.Models
{
    [StructLayout(LayoutKind.Sequential, Pack = 1, CharSet = CharSet.Unicode)]
    public class ServerInfo : INotifyPropertyChanged
    {
        public event PropertyChangedEventHandler? PropertyChanged;
        protected void OnPropertyChanged([CallerMemberName] string? name = null) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
        private string _name = "";
        public string Name { get => _name; set { _name = value; OnPropertyChanged(); } }

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)]
        private byte[] _ipBytes = new byte[32];

        private int _port;
        public int Port { get => _port; set { _port = value; OnPropertyChanged(); } }

        [MarshalAs(UnmanagedType.I1)]
        public bool Used;
        public bool IsUsed { get => Used; set { Used = value; OnPropertyChanged(); } }

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
        public byte[] Key = new byte[16];

        [MarshalAs(UnmanagedType.I1)]
        private bool _encrypt;
        public bool Encrypt { get => _encrypt; set { _encrypt = value; OnPropertyChanged(); } }

        [MarshalAs(UnmanagedType.I1)]
        private bool _useHelper;
        public bool UseHelper { get => _useHelper; set { _useHelper = value; OnPropertyChanged(); } }

        [MarshalAs(UnmanagedType.I1)]
        private bool _useBd;
        public bool UseBd { get => _useBd; set { _useBd = value; OnPropertyChanged(); } }

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
        private string _bdFile = "";
        public string BdFile { get => _bdFile; set { _bdFile = value; OnPropertyChanged(); } }

        [MarshalAs(UnmanagedType.I1)]
        private bool _randKey;
        public bool RandKey { get => _randKey; set { _randKey = value; OnPropertyChanged(); } }

        public uint E;
        public uint D;
        public uint N;

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
        public byte[] Fix = new byte[16];

        public string Ip
        {
            get => System.Text.Encoding.ASCII.GetString(_ipBytes).TrimEnd('\0');
            set
            {
                byte[] b = System.Text.Encoding.ASCII.GetBytes(value ?? "");
                Array.Clear(_ipBytes, 0, 32);
                Array.Copy(b, _ipBytes, Math.Min(b.Length, 32));
                OnPropertyChanged();
            }
        }

        // --- 以下為執行期才有的狀態查詢結果，不屬於 list.txt 的二進位格式，不參與 Marshal ---

        private int _onlineCount = -1;
        /// <summary>伺服器使用率百分比（0-100）；-1 代表尚未查詢或查詢失敗。
        /// 命名沿用 OnlineCount 是因為早期設計是實際人數，伺服器端後來改成只回傳
        /// 使用率（AcceptDispatcher.java），這裡沒有跟著改欄位名稱，避免牽動綁定範圍。</summary>
        public int OnlineCount
        {
            get => _onlineCount;
            set { _onlineCount = value; OnPropertyChanged(); OnPropertyChanged(nameof(StatusLightBrush)); OnPropertyChanged(nameof(StatusTooltip)); }
        }

        private int _maxOnline = -1;
        /// <summary>固定為 100（伺服器端回傳的是使用率百分比，非實際人數上限）；-1 代表尚未查詢或查詢失敗。</summary>
        public int MaxOnline
        {
            get => _maxOnline;
            set { _maxOnline = value; OnPropertyChanged(); OnPropertyChanged(nameof(StatusLightBrush)); OnPropertyChanged(nameof(StatusTooltip)); }
        }

        private bool _isMaintenance;
        /// <summary>伺服器目前是否處於維護中（維護期間只允許 GM 登入）。</summary>
        public bool IsMaintenance
        {
            get => _isMaintenance;
            set { _isMaintenance = value; OnPropertyChanged(); OnPropertyChanged(nameof(StatusLightBrush)); OnPropertyChanged(nameof(StatusTooltip)); }
        }

        /// <summary>維護預計結束時間（UTC）；查詢當下用剩餘秒數換算，供前端算即時倒數。非維護中為 null。</summary>
        public DateTime? MaintenanceEndAtUtc { get; set; }

        private static readonly SolidColorBrush GrayBrush = new SolidColorBrush(Color.FromRgb(0x88, 0x88, 0x88));
        private static readonly SolidColorBrush GreenBrush = new SolidColorBrush(Color.FromRgb(0x2E, 0xCC, 0x40));
        private static readonly SolidColorBrush BlueBrush = new SolidColorBrush(Color.FromRgb(0x2E, 0x8B, 0xE0));
        private static readonly SolidColorBrush OrangeBrush = new SolidColorBrush(Color.FromRgb(0xFF, 0x85, 0x1B));
        private static readonly SolidColorBrush RedBrush = new SolidColorBrush(Color.FromRgb(0xE0, 0x2A, 0x2A));
        private static readonly SolidColorBrush YellowBrush = new SolidColorBrush(Color.FromRgb(0xF0, 0xC0, 0x1E));

        /// <summary>燈號顏色：灰=離線/未知、黃=維護中、綠=順暢(&lt;50%)、藍=普通(50-69%)、橘=擁擠(70-89%)、紅=過載(&gt;=90%)。</summary>
        public SolidColorBrush StatusLightBrush
        {
            get
            {
                if (_isMaintenance) return YellowBrush;
                if (_onlineCount < 0 || _maxOnline <= 0) return GrayBrush;
                double ratio = (double)_onlineCount / _maxOnline;
                if (ratio >= 0.9) return RedBrush;
                if (ratio >= 0.7) return OrangeBrush;
                if (ratio >= 0.5) return BlueBrush;
                return GreenBrush;
            }
        }

        /// <summary>燈號是否為灰色（離線/查詢失敗/未知）——維護中不算，維護中是黃燈，可另外判斷是否為 GM 放行。</summary>
        public bool IsStatusUnknown => !_isMaintenance && (_onlineCount < 0 || _maxOnline <= 0);

        /// <summary>跟 StatusLightBrush 用同一套門檻，燈號跟文字才會一致：
        /// 灰=關閉、黃=維護中、綠=順暢(&lt;50%)、藍=普通(50-69%)、橘=擁擠(70-89%)、紅=過載(&gt;=90%)。</summary>
        public string StatusTooltip
        {
            get
            {
                if (_isMaintenance) return "維護中";
                if (_onlineCount < 0 || _maxOnline <= 0) return "關閉";
                double ratio = (double)_onlineCount / _maxOnline;
                if (ratio >= 0.9) return "過載";
                if (ratio >= 0.7) return "擁擠";
                if (ratio >= 0.5) return "普通";
                return "順暢";
            }
        }

        /// <summary>由與 Encoder 相同的 <see cref="ServerListEntryNative"/> 二進位還原。</summary>
        public static ServerInfo FromNative(ServerListEntryNative n)
        {
            var s = new ServerInfo();
            s.Name = (n.Name ?? "").Split('\0')[0];
            s.Port = n.Port;
            s.Used = n.Used;
            s.Encrypt = n.Encrypt;
            s.UseHelper = n.UseHelper;
            s.UseBd = n.UseBd;
            s.RandKey = n.RandKey;
            s.BdFile = (n.BdFile ?? "").Split('\0')[0];
            s.E = n.E;
            s.D = n.D;
            s.N = n.N;
            if (n.IpBytes != null && n.IpBytes.Length >= 32)
                Array.Copy(n.IpBytes, s._ipBytes, 32);
            if (n.Key != null && n.Key.Length >= 16)
                Array.Copy(n.Key, s.Key, 16);
            if (n.Fix != null && n.Fix.Length >= 16)
                Array.Copy(n.Fix, s.Fix, 16);
            return s;
        }
    }
}
