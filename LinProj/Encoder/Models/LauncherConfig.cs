using System.Runtime.InteropServices;

namespace LinEncoder.Models
{
    /// <summary>
    /// 登入器組態資料結構。
    /// 必須與 LinLauncher.Models.LauncherConfig 逐欄位位元組對齊（Sign/欄位順序/大小全部一致）。
    /// </summary>
    // 必須跟 LinLauncher.Models.LauncherConfig 逐欄位位元組對齊（Sign/欄位順序/大小全部一致），
    // 因為 LinLauncher.Proxy 是從輸出 exe 尾端找 Sign 魔術數(0x12345678FEDCBAFF)、比對區塊長度
    // 4728 bytes 來抽出 config —— 這裡以前是另一份不相容的舊定義（Sign 是 "PROXYCFG"、欄位順序
    // 也不同），Encoder 產生的登入器實際上從沒能被 Proxy 正確讀到組態。
    [StructLayout(LayoutKind.Sequential, Pack = 1, CharSet = CharSet.Unicode)]
    public class LauncherConfig
    {
        /// <summary>
        /// 登入器組態簽章魔術數。
        /// </summary>
        public const ulong LAUNCHER_CONFIG_SIGN = 0x12345678FEDCBAFF;

        /// <summary>
        /// 組態簽章標頭。
        /// </summary>
        public ulong Sign = LAUNCHER_CONFIG_SIGN;

        /// <summary>
        /// 是否加密。
        /// </summary>
        [MarshalAs(UnmanagedType.I1)] public bool Encrypted = true;

        /// <summary>
        /// 是否已設定。
        /// </summary>
        [MarshalAs(UnmanagedType.I1)] public bool Configed = false;

        /// <summary>
        /// 金鑰資料。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
        public byte[] Key = new byte[16];

        /// <summary>
        /// 登入器視窗標題。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string Title = "Lineage Launcher";

        /// <summary>
        /// 版本號碼。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 16)]
        public string Ver = "1001";

        /// <summary>
        /// 官方網站網址。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string Web = "http://www.google.com/";

        /// <summary>
        /// 伺服器列表下載網址。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string List = "";

        /// <summary>
        /// 是否使用自動更新。
        /// </summary>
        [MarshalAs(UnmanagedType.I1)] public bool UseUpdate = false;

        /// <summary>
        /// 自動更新檔下載網址。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string Update = "";

        /// <summary>
        /// 快速連結按鈕是否啟用陣列 (共 5 個)。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValArray, ArraySubType = UnmanagedType.I1, SizeConst = 5)]
        public bool[] UseLink = new bool[5];

        /// <summary>
        /// 快速連結按鈕名稱原始位元組資料 (5 * 16 * 2 Unicode TCHAR)。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 160)] // 5 * 16 * 2 (Unicode TCHAR)
        public byte[] LinkNamesRaw = new byte[160];

        /// <summary>
        /// 快速連結按鈕網址原始位元組資料 (5 * 256 * 2 Unicode TCHAR)。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2560)] // 5 * 256 * 2 (Unicode TCHAR)
        public byte[] LinkUrlsRaw = new byte[2560];

        /// <summary>
        /// 輔助說明網址。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
        public string Helper = "";

        /// <summary>
        /// 修補專用欄位。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
        public byte[] Fix = new byte[16];

        /// <summary>
        /// 登入器視窗寬度。
        /// </summary>
        public int Width = 1000;

        /// <summary>
        /// 登入器視窗高度。
        /// </summary>
        public int Height = 600;
    }

    /// <summary>
    /// 快速連結項目資料模型。
    /// </summary>
    public class LinkItem
    {
        /// <summary>
        /// 顯示順序索引。
        /// </summary>
        public int DisplayIndex { get; set; }

        /// <summary>
        /// 是否啟用。
        /// </summary>
        public bool Enabled { get; set; }

        /// <summary>
        /// 連結名稱。
        /// </summary>
        public string Name { get; set; } = "";

        /// <summary>
        /// 連結網址。
        /// </summary>
        public string Url { get; set; } = "";
    }
}
