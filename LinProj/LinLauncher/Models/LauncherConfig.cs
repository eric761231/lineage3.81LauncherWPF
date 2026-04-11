using System;
using System.Runtime.InteropServices;

namespace LinLauncher.Models
{
    // 序列化位元組大小變更時請同步 LinLauncher.Proxy\Program.cs 之 LauncherConfigSerializedSize
    [StructLayout(LayoutKind.Sequential, Pack = 1, CharSet = CharSet.Unicode)]
    public class LauncherConfig
    {
        public const ulong LAUNCHER_CONFIG_SIGN = 0x12345678FEDCBAFF;

        public ulong Sign = LAUNCHER_CONFIG_SIGN;
        [MarshalAs(UnmanagedType.I1)] public bool Encrypted = true;
        [MarshalAs(UnmanagedType.I1)] public bool Configed = false;

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
        public byte[] Key = new byte[16];

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string Title = "Lineage Launcher";

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 16)]
        public string Ver = "1001";

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string Web = "http://www.google.com/";

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string List = "";

        [MarshalAs(UnmanagedType.I1)] public bool UseUpdate = false;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string Update = "";

        [MarshalAs(UnmanagedType.ByValArray, ArraySubType = UnmanagedType.I1, SizeConst = 5)]
        public bool[] UseLink = new bool[5];

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 160)] // 5 * 16 * 2 (Unicode TCHAR)
        public byte[] LinkNamesRaw = new byte[160];

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2560)] // 5 * 256 * 2 (Unicode TCHAR)
        public byte[] LinkUrlsRaw = new byte[2560];

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
        public string Helper = "";

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
        public byte[] Fix = new byte[16];

        public int Width = 1000;
        public int Height = 600;
    }
}
