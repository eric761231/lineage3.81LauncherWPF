// ShareInfo.cs: 用於與 DLL 共享的資訊結構。
using System;
using System.Runtime.InteropServices;

namespace LinLauncher.Models
{
    [StructLayout(LayoutKind.Sequential, Pack = 1, CharSet = CharSet.Unicode)]
    public class ShareInfo
    {
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)]
        public byte[] Ip = new byte[32]; // ASCII

        public int Port;
        
        [MarshalAs(UnmanagedType.I1)]
        public bool Encrypt;
        
        [MarshalAs(UnmanagedType.I1)]
        public bool UseHelper;

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
        public byte[] Key = new byte[16];

        [MarshalAs(UnmanagedType.I1)]
        public bool UseBd;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
        public string BdFile = "";

        [MarshalAs(UnmanagedType.I1)]
        public bool Read;

        [MarshalAs(UnmanagedType.I1)]
        public bool RandEnc;

        public uint RsaN;
        public uint RsaD;
        public uint Magic;

        public void SetIp(string ip)
        {
            byte[] b = System.Text.Encoding.ASCII.GetBytes(ip);
            Array.Clear(Ip, 0, 32);
            Array.Copy(b, Ip, Math.Min(b.Length, 32));
        }

        public string GetIp()
        {
            return System.Text.Encoding.ASCII.GetString(Ip).TrimEnd('\0');
        }
    }
}
