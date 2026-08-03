// 對齊 LinLauncher.Services.ConfigDatWriter 的邏輯，供 Encoder 把 LauncherConfig
// 編碼成可以附加到輸出登入器 exe 尾端、被 LinLauncher.Proxy 正確抽出的位元組區塊。
using System;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using LinEncoder.Models;

namespace LinEncoder.Services
{
    public static class ConfigDatWriter
    {
        public const int HeaderSize = 26;

        /// <summary>
        /// 建立加密後的 config 區塊。每次呼叫會產生新的隨機 16-byte 金鑰（寫入結構體 Key 欄位）。
        /// </summary>
        public static byte[] BuildEncryptedFile(LauncherConfig cfg)
        {
            cfg.Sign = LauncherConfig.LAUNCHER_CONFIG_SIGN;
            cfg.Encrypted = true;
            cfg.Configed = true;
            cfg.Key ??= new byte[16];
            if (cfg.Key.Length != 16)
                cfg.Key = new byte[16];
            RandomNumberGenerator.Fill(cfg.Key);

            int size = Marshal.SizeOf(typeof(LauncherConfig));
            int payloadLen = size - HeaderSize;
            if (payloadLen <= 0)
                throw new InvalidOperationException($"LauncherConfig 序列化長度 {size} 無效。");

            IntPtr ptr = Marshal.AllocHGlobal(size);
            byte[] data;
            try
            {
                Marshal.StructureToPtr(cfg, ptr, false);
                data = new byte[size];
                Marshal.Copy(ptr, data, 0, size);
                Marshal.DestroyStructure(ptr, typeof(LauncherConfig));
            }
            finally
            {
                Marshal.FreeHGlobal(ptr);
            }

            var keyCopy = new byte[16];
            Array.Copy(data, 10, keyCopy, 0, 16);
            var payload = new byte[payloadLen];
            Array.Copy(data, HeaderSize, payload, 0, payloadLen);
            CryptoService.ConfigEncrypt(keyCopy, payload);
            Array.Copy(payload, 0, data, HeaderSize, payloadLen);
            return data;
        }
    }
}
