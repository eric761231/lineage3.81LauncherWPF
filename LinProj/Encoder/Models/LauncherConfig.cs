using System;
using System.Runtime.InteropServices;

namespace LinLauncher.Models
{
    /// <summary>
    /// 表示啟動器設定的序列化結構 (用於與本機程式或檔案做固定大小的讀寫/序列化)。
    ///
    /// 初學說明：這個類別使用 <see cref="StructLayout"/> 來固定記憶體版面（Layout），
    /// 所有欄位會依宣告順序排放且有特定大小（SizeConst）。這常用於與非託管程式
    /// 溝通或把資料寫入固定長度的二進位檔時。
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1, CharSet = CharSet.Auto)]
    public class LauncherConfig
    {
        /// <summary>
        /// 結構的簽章值，用來驗證資料是否為預期格式。
        /// </summary>
        public const ulong LAUNCHER_CONFIG_SIGN = 0x12345678FEDCBAFF;

        /// <summary>
        /// 儲存簽章，預設為 <see cref="LAUNCHER_CONFIG_SIGN"/>。
        /// </summary>
        public ulong Sign = LAUNCHER_CONFIG_SIGN;

        /// <summary>
        /// 是否已加密（用 MarshalAs 指定為 1 byte 的布林表示法）。
        /// </summary>
        [MarshalAs(UnmanagedType.I1)] public bool Encrypted = true;

        /// <summary>
        /// 是否已被設定過（同樣使用 1 byte 的布林表示法）。
        /// </summary>
        [MarshalAs(UnmanagedType.I1)] public bool Configed = false;

        /// <summary>
        /// 對稱金鑰（固定 16 bytes）。宣告為 byte 陣列方便直接對應到原始位元組。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
        public byte[] Key = new byte[16];

        /// <summary>
        /// 視窗標題，固定長度為 64 個字元（注意是固定空間，超過會截斷，未滿會以 null 結尾）。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string Title = "Lineage Launcher";

        /// <summary>
        /// 版本字串，固定長度 16。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 16)]
        public string Ver = "1001";

        /// <summary>
        /// 主要網站或首頁 URL，固定長度 256。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string Web = "http://www.google.com/";

        /// <summary>
        /// 列表或其他 URL，固定長度 256。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string List = "http://www.google.com/";

        /// <summary>
        /// 是否啟用更新檢查（1 byte 布林）。
        /// </summary>
        [MarshalAs(UnmanagedType.I1)] public bool UseUpdate = false;

        /// <summary>
        /// 更新 URL，固定長度 256。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string Update = "";

        /// <summary>
        /// 是否啟用各項連結（5 個選項）。ArraySubType 指定內部元素類型。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValArray, ArraySubType = UnmanagedType.I1, SizeConst = 5)]
        public bool[] UseLink = new bool[5];

        /// <summary>
        /// 原始的連結名稱資料，以固定長度的位元組陣列儲存（搭配字元寬度計算）。
        /// 範例註記：5 * 16 * 2 (Unicode TCHAR) = 160 bytes。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 160)] // 5 * 16 * 2 (Unicode TCHAR)
        public byte[] LinkNamesRaw = new byte[160];

        /// <summary>
        /// 原始的連結 URL 資料，以固定長度的位元組陣列儲存。
        /// 範例註記：5 * 256 * 2 (Unicode TCHAR) = 2560 bytes。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2560)] // 5 * 256 * 2 (Unicode TCHAR)
        public byte[] LinkUrlsRaw = new byte[2560];

        /// <summary>
        /// 協助工具或說明字串，固定長度 128。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
        public string Helper = "";

        /// <summary>
        /// 保留欄位（可用於未來擴充），固定 16 bytes。
        /// </summary>
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
        public byte[] Fix = new byte[16];

        /// <summary>
        /// 視窗寬度（像素），預設 1000。
        /// </summary>
        public int Width = 1000;

        /// <summary>
        /// 視窗高度（像素），預設 600。
        /// </summary>
        public int Height = 600;
    }
}
