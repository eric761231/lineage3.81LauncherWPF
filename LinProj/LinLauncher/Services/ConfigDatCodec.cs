using System;
using System.Runtime.InteropServices;
using LinLauncher.Models;

namespace LinLauncher.Services
{
    /// <summary>
    /// config.dat 加解密與 <see cref="LauncherConfig"/> 序列化（與 MainViewModel 載入邏輯一致）。
    /// </summary>
    public static class ConfigDatCodec
    {
        public const int HeaderSize = ConfigDatWriter.HeaderSize;

        /// <summary>
        /// 將完整檔案位元組（含簽名與加密 payload）解密並還原為 <see cref="LauncherConfig"/>（失敗則回傳 null）。
        /// </summary>
        public static LauncherConfig? TryDecrypt(byte[] data)
        {
            try
            {
                int structSize = Marshal.SizeOf(typeof(LauncherConfig));
                if (data == null || data.Length < structSize)
                    return null;
                if (BitConverter.ToUInt64(data, 0) != LauncherConfig.LAUNCHER_CONFIG_SIGN)
                    return null;
                if (data.Length != structSize)
                    return null;

                byte[] key = new byte[16];
                Array.Copy(data, 10, key, 0, 16);
                int payloadSize = data.Length - HeaderSize;
                byte[] payload = new byte[payloadSize];
                Array.Copy(data, HeaderSize, payload, 0, payloadSize);
                CryptoService.ConfigDecrypt(key, payload);
                byte[] copy = new byte[data.Length];
                Array.Copy(data, 0, copy, 0, data.Length);
                Array.Copy(payload, 0, copy, HeaderSize, payloadSize);

                IntPtr ptr = Marshal.AllocHGlobal(copy.Length);
                try
                {
                    Marshal.Copy(copy, 0, ptr, copy.Length);
                    return Marshal.PtrToStructure(ptr, typeof(LauncherConfig)) as LauncherConfig;
                }
                finally
                {
                    Marshal.FreeHGlobal(ptr);
                }
            }
            catch
            {
                return null;
            }
        }
    }
}
