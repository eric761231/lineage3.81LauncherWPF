using System;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

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
