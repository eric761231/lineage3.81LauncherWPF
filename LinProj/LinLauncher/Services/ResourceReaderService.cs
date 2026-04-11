using System;
using System.Runtime.InteropServices;

namespace LinLauncher.Services
{
    public class ResourceReaderService
    {

        public byte[]? ReadConfig()
        {
            try
            {
                string path = System.IO.Path.Combine(System.AppDomain.CurrentDomain.BaseDirectory, "config.dat");
                if (System.IO.File.Exists(path))
                {
                    byte[] buffer = System.IO.File.ReadAllBytes(path);
                    ulong signature = LinLauncher.Models.LauncherConfig.LAUNCHER_CONFIG_SIGN;
                    int structSize = Marshal.SizeOf(typeof(LinLauncher.Models.LauncherConfig));

                    // 必須與 Marshal.SizeOf(LauncherConfig) 完全一致；過長常見於 LinLauncher.dat 覆寫（尾端切出之變長位元組），硬截前段會導致解密錯亂。
                    if (buffer.Length == structSize && BitConverter.ToUInt64(buffer, 0) == signature)
                        return buffer;
                }
            }
            catch { }
            return null;
        }
    }
}
