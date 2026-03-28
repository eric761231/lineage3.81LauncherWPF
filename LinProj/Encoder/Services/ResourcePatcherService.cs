using System;
using System.Runtime.InteropServices;
using System.IO;

namespace LinLauncher.Services
{
    public class ResourcePatcherService
    {
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern IntPtr BeginUpdateResource(string pFileName, [MarshalAs(UnmanagedType.Bool)] bool bDeleteExistingResources);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool UpdateResource(IntPtr hUpdate, string lpType, string lpName, ushort wLanguage, IntPtr lpData, uint cbData);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool EndUpdateResource(IntPtr hUpdate, bool fDiscard);

        public bool PatchConfig(string targetExe, byte[] configData)
        {
            if (!File.Exists(targetExe)) return false;

            IntPtr hUpdate = BeginUpdateResource(targetExe, false);
            if (hUpdate == IntPtr.Zero) return false;

            IntPtr pData = Marshal.AllocHGlobal(configData.Length);
            try
            {
                Marshal.Copy(configData, 0, pData, configData.Length);
                // "CONFIG" is the type, "101" is the name (arbitrary)
                if (!UpdateResource(hUpdate, "CONFIG", "101", 1033, pData, (uint)configData.Length))
                {
                    EndUpdateResource(hUpdate, true);
                    return false;
                }
                return EndUpdateResource(hUpdate, false);
            }
            finally
            {
                Marshal.FreeHGlobal(pData);
            }
        }
    }
}
