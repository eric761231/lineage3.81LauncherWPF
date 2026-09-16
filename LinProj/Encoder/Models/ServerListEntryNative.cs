using System.Runtime.InteropServices;

namespace LinEncoder.Models
{
    /// <summary>
    /// 伺服器列表原生項目資料結構。
    /// 與 LinLauncher.Models.ServerListEntryNative 位元組佈局必須完全一致（Encoder 產生、登入器解析）。
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1, CharSet = CharSet.Unicode)]
    public struct ServerListEntryNative
    {
        /// <summary>
        /// 伺服器名稱。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
        public string Name;

        /// <summary>
        /// 伺服器 IP 位元組陣列。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)]
        public byte[] IpBytes;

        /// <summary>
        /// 伺服器通訊埠 (Port)。
        /// </summary>
        public int Port;

        /// <summary>
        /// 是否已啟用。
        /// </summary>
        [MarshalAs(UnmanagedType.I1)]
        public bool Used;

        /// <summary>
        /// 傳輸金鑰位元組陣列。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
        public byte[] Key;

        /// <summary>
        /// 是否開啟加密。
        /// </summary>
        [MarshalAs(UnmanagedType.I1)]
        public bool Encrypt;

        /// <summary>
        /// 是否使用小幫手。
        /// </summary>
        [MarshalAs(UnmanagedType.I1)]
        public bool UseHelper;

        /// <summary>
        /// 是否使用邊界/變身檔。
        /// </summary>
        [MarshalAs(UnmanagedType.I1)]
        public bool UseBd;

        /// <summary>
        /// 變身檔名稱。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
        public string BdFile;

        /// <summary>
        /// 是否使用動態隨機金鑰。
        /// </summary>
        [MarshalAs(UnmanagedType.I1)]
        public bool RandKey;

        /// <summary>
        /// RSA 金鑰 E 參數。
        /// </summary>
        public uint E;

        /// <summary>
        /// RSA 金鑰 D 參數。
        /// </summary>
        public uint D;

        /// <summary>
        /// RSA 金鑰 N 參數。
        /// </summary>
        public uint N;

        /// <summary>
        /// 修正補丁保留位元組陣列。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
        public byte[] Fix;
    }
}
