using System.Runtime.InteropServices;

namespace LinEncoder.Models
{
    [StructLayout(LayoutKind.Sequential, Pack = 1, CharSet = CharSet.Ansi)]
    public class ServerInfo
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string Name = "";
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string Ip = "";
        public int Port;
        public uint D;
        public uint N;
        public bool IsUsed;
        public bool UseBd;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string? BdFile;
    }
}