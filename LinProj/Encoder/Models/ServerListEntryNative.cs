using System.Runtime.InteropServices;

namespace LinEncoder.Models
{
    /// <summary>
    /// 與 LinLauncher.Models.ServerListEntryNative 位元組佈局必須完全一致（Encoder 產生、登入器解析）。
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
