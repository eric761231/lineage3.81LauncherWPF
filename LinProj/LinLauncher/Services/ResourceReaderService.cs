using System;
using System.Runtime.InteropServices;

namespace LinLauncher.Services
{
    public class ResourceReaderService
    {
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern IntPtr FindResource(IntPtr hModule, string lpName, string lpType);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern IntPtr LoadResource(IntPtr hModule, IntPtr hResInfo);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern uint SizeofResource(IntPtr hModule, IntPtr hResInfo);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern IntPtr LockResource(IntPtr hResData);
        public byte[]? ReadConfig()
        {
            try
            {
                string path = System.Diagnostics.Process.GetCurrentProcess().MainModule.FileName;
                byte[] buffer = System.IO.File.ReadAllBytes(path);
                ulong signature = LinLauncher.Models.LauncherConfig.LAUNCHER_CONFIG_SIGN;
                int structSize = Marshal.SizeOf(typeof(LinLauncher.Models.LauncherConfig));

                for (int i = 0; i <= buffer.Length - structSize; i++)
                {
                    if (BitConverter.ToUInt64(buffer, i) == signature)
                    {
                        byte[] configData = new byte[structSize];
                        Array.Copy(buffer, i, configData, 0, structSize);
                        return configData;
                    }
                }
            }
            catch { }
            return null;
        }
    }
}
