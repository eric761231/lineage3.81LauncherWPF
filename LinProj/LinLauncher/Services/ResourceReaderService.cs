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

                    if (buffer.Length >= structSize)
                    {
                        if (BitConverter.ToUInt64(buffer, 0) == signature)
                        {
                            byte[] configData = new byte[structSize];
                            Array.Copy(buffer, 0, configData, 0, structSize);
                            return configData;
                        }
                    }
                }
            }
            catch { }
            return null;
        }
    }
}
