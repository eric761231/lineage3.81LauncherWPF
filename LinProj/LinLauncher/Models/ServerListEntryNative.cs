using System.Runtime.InteropServices;

namespace LinLauncher.Models
{
    /// <summary>
    /// 與 Encoder 產生之 list.txt（ServerData*=base64）二進位格式一致，可安全 Marshal，不含 event。
    /// 必須與 LinEncoder.Models.ServerListEntryNative 欄位順序與 Pack 完全一致。
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1, CharSet = CharSet.Unicode)]
    public struct ServerListEntryNative
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
        public string Name;

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)]
        public byte[] IpBytes;

        public int Port;

        [MarshalAs(UnmanagedType.I1)]
        public bool Used;

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
        public byte[] Key;

        [MarshalAs(UnmanagedType.I1)]
        public bool Encrypt;

        [MarshalAs(UnmanagedType.I1)]
        public bool UseHelper;

        [MarshalAs(UnmanagedType.I1)]
        public bool UseBd;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
        public string BdFile;

        [MarshalAs(UnmanagedType.I1)]
        public bool RandKey;

        public uint E;
        public uint D;
        public uint N;

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
        public byte[] Fix;
    }
}
