using System;
using System.Runtime.InteropServices;

namespace LinEncoder.Services
{
    /// <summary>
    /// 將 ServerListEntryNative 序列化為與 LinLauncher 相同長度的位元組（供 AES+XOR 加密）。
    /// </summary>
    public static class ListEntryMarshal
    {
        /// <summary>
        /// 將 Struct 結構體轉為 Native 位元組陣列。
        /// </summary>
        /// <typeparam name="T">結構體型別</typeparam>
        /// <param name="structure">結構體實例</param>
        /// <return>序列化後的位元組陣列</return>
        public static byte[] StructureToBytes<T>(T structure) where T : struct
        {
            int size = Marshal.SizeOf<T>();
            IntPtr ptr = Marshal.AllocHGlobal(size);
            try
            {
                Marshal.StructureToPtr(structure, ptr, false);
                byte[] arr = new byte[size];
                Marshal.Copy(ptr, arr, 0, size);
                return arr;
            }
            finally
            {
                Marshal.DestroyStructure(ptr, typeof(T));
                Marshal.FreeHGlobal(ptr);
            }
        }

        /// <summary>
        /// StructureToBytes 的反向操作，供「驗證金鑰」讀回 list.txt 解密後的 ServerListEntryNative 用。
        /// </summary>
        /// <typeparam name="T">結構體型別</typeparam>
        /// <param name="bytes">位元組陣列</param>
        /// <return>還原之結構體實例</return>
        public static T BytesToStructure<T>(byte[] bytes) where T : struct
        {
            int size = Marshal.SizeOf<T>();
            IntPtr ptr = Marshal.AllocHGlobal(size);
            try
            {
                Marshal.Copy(bytes, 0, ptr, size);
                return Marshal.PtrToStructure<T>(ptr);
            }
            finally
            {
                Marshal.FreeHGlobal(ptr);
            }
        }

        // 對齊 RUST server_list.rs Server_Info（213 bytes, pack=1）
        public const int OffsetEncrypt = 117; // +0x75
        public const int OffsetRandKey = 184; // +0xB8
        public const int OffsetRsaE = 185;    // +0xB9
        public const int OffsetRsaD = 189;    // +0xBD
        public const int OffsetRsaN = 193;    // +0xC1

        /// <summary>
        /// 用 UI／ini 的開關與 RSA 覆蓋 Marshal 可能寫偏的旗標與金鑰。
        /// </summary>
        /// <param name="buf">伺服器資料位元組緩衝區</param>
        /// <param name="encrypt">加密旗標</param>
        /// <param name="randKey">動態隨機金鑰旗標</param>
        /// <param name="e">RSA E 參數</param>
        /// <param name="d">RSA D 參數</param>
        /// <param name="n">RSA N 參數</param>
        public static void WriteCryptoFields(byte[] buf, bool encrypt, bool randKey, uint e, uint d, uint n)
        {
            if (buf.Length < OffsetRsaN + 4)
            {
                return;
            }
            buf[OffsetEncrypt] = encrypt ? (byte)1 : (byte)0;
            buf[OffsetRandKey] = randKey ? (byte)1 : (byte)0;
            Buffer.BlockCopy(BitConverter.GetBytes(e), 0, buf, OffsetRsaE, 4);
            Buffer.BlockCopy(BitConverter.GetBytes(d), 0, buf, OffsetRsaD, 4);
            Buffer.BlockCopy(BitConverter.GetBytes(n), 0, buf, OffsetRsaN, 4);
        }

        /// <summary>
        /// 從位元組緩衝區中讀取加密與 RSA 相關欄位資訊。
        /// </summary>
        /// <param name="buf">伺服器資料位元組緩衝區</param>
        /// <return>包含 E, D, N 參數與加密/隨機金鑰旗標之元組</return>
        public static (uint E, uint D, uint N, bool Encrypt, bool RandKey) ReadCryptoFields(byte[] buf)
        {
            bool enc = buf.Length > OffsetEncrypt && buf[OffsetEncrypt] != 0;
            bool rand = buf.Length > OffsetRandKey && buf[OffsetRandKey] != 0;
            uint e = buf.Length >= OffsetRsaE + 4 ? BitConverter.ToUInt32(buf, OffsetRsaE) : 0;
            uint d = buf.Length >= OffsetRsaD + 4 ? BitConverter.ToUInt32(buf, OffsetRsaD) : 0;
            uint n = buf.Length >= OffsetRsaN + 4 ? BitConverter.ToUInt32(buf, OffsetRsaN) : 0;
            return (e, d, n, enc, rand);
        }
    }
}
