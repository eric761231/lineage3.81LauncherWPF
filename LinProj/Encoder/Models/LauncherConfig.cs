using System.Runtime.InteropServices;

namespace LinEncoder.Models
{
    [StructLayout(LayoutKind.Sequential, Pack = 1, CharSet = CharSet.Unicode)]
    public class LauncherConfig
    {
        public const ulong LAUNCHER_CONFIG_SIGN = 0x50524F5859434647; // "PROXYCFG"

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 26)]
        public byte[] Key = new byte[26];
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string Title = "";
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
        public string Ver = "";
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string Web = "";
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string List = "";
        public bool UseUpdate;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string Update = "";
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 5)]
        public bool[] UseLink = new bool[5];
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 160)] // 5 * 32
        public byte[] LinkNamesRaw = new byte[160];
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2560)] // 5 * 512
        public byte[] LinkUrlsRaw = new byte[2560];
        public int Width;
        public int Height;
        public bool Configed;
    }

    public class LinkItem
    {
        public int DisplayIndex { get; set; }
        public bool Enabled { get; set; }
        public string Name { get; set; } = "";
        public string Url { get; set; } = "";
    }
}