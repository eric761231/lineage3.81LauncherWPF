using System;
using System.Runtime.InteropServices;
using System.IO;

namespace LinEncoder.Services
{
    public class ResourcePatcherService
    {
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern IntPtr BeginUpdateResource(string pFileName, [MarshalAs(UnmanagedType.Bool)] bool bDeleteExistingResources);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool UpdateResource(IntPtr hUpdate, IntPtr lpType, IntPtr lpName, ushort wLanguage, IntPtr lpData, uint cbData);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool UpdateResource(IntPtr hUpdate, string lpType, string lpName, ushort wLanguage, IntPtr lpData, uint cbData);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool EndUpdateResource(IntPtr hUpdate, bool fDiscard);

        // RT_ICON = 3, RT_GROUP_ICON = 14
        private const int RT_ICON = 3;
        private const int RT_GROUP_ICON = 14;

        public bool PatchConfig(string targetExe, byte[] configData)
        {
            if (!File.Exists(targetExe)) return false;

            IntPtr hUpdate = BeginUpdateResource(targetExe, false);
            if (hUpdate == IntPtr.Zero) return false;

            IntPtr pData = Marshal.AllocHGlobal(configData.Length);
            try
            {
                Marshal.Copy(configData, 0, pData, configData.Length);
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

        /// <summary>
        /// Replaces the icon in a native Win32 EXE using the provided .ico file bytes.
        /// Only works on native (non-.NET Single-File) EXEs like our proxy launcher.
        /// </summary>
        public bool PatchIcon(string targetExe, string icoPath)
        {
            if (!File.Exists(targetExe) || !File.Exists(icoPath)) return false;
            byte[] icoData = File.ReadAllBytes(icoPath);
            if (icoData.Length < 22) return false; // Too small to be a valid ICO

            // ICO file layout:
            // 0-5:  ICONDIR header (6 bytes)
            // 6+:   ICONDIRENTRY per image (16 bytes each)
            // Each entry points to the raw image data
            int imageCount = BitConverter.ToInt16(icoData, 4);
            if (imageCount <= 0) return false;

            IntPtr hUpdate = BeginUpdateResource(targetExe, false);
            if (hUpdate == IntPtr.Zero) return false;

            try
            {
                // Build GRPICONDIR header (same layout as ICO ICONDIR but offsets replaced with resource IDs)
                // GRPICONDIR = 6 bytes header + imageCount * 14 bytes per entry
                byte[] grpData = new byte[6 + imageCount * 14];
                // idReserved=0, idType=1 (icon), idCount
                grpData[2] = 1; // idType = 1
                BitConverter.GetBytes((short)imageCount).CopyTo(grpData, 4);

                for (int i = 0; i < imageCount; i++)
                {
                    int entryOffset = 6 + i * 16;  // offset in original ICO file
                    int grpEntryOffset = 6 + i * 14; // offset in GRPICONDIR

                    if (entryOffset + 16 > icoData.Length) break;

                    int imageOffset = BitConverter.ToInt32(icoData, entryOffset + 12);
                    int imageSize   = BitConverter.ToInt32(icoData, entryOffset + 8);

                    if (imageOffset + imageSize > icoData.Length) break;

                    byte[] imageData = new byte[imageSize];
                    Array.Copy(icoData, imageOffset, imageData, 0, imageSize);

                    // Write individual RT_ICON resource (ID = i+1)
                    IntPtr pImg = Marshal.AllocHGlobal(imageSize);
                    try
                    {
                        Marshal.Copy(imageData, 0, pImg, imageSize);
                        UpdateResource(hUpdate, (IntPtr)RT_ICON, (IntPtr)(i + 1), 0, pImg, (uint)imageSize);
                    }
                    finally { Marshal.FreeHGlobal(pImg); }

                    // Copy ICONDIRENTRY (first 12 bytes) into GRPICONDIR entry, append ID (2 bytes)
                    Array.Copy(icoData, entryOffset, grpData, grpEntryOffset, 12);
                    BitConverter.GetBytes((short)(i + 1)).CopyTo(grpData, grpEntryOffset + 12);
                }

                // Write RT_GROUP_ICON resource (ID = 1)
                IntPtr pGrp = Marshal.AllocHGlobal(grpData.Length);
                try
                {
                    Marshal.Copy(grpData, 0, pGrp, grpData.Length);
                    UpdateResource(hUpdate, (IntPtr)RT_GROUP_ICON, (IntPtr)1, 0, pGrp, (uint)grpData.Length);
                }
                finally { Marshal.FreeHGlobal(pGrp); }

                return EndUpdateResource(hUpdate, false);
            }
            catch
            {
                EndUpdateResource(hUpdate, true);
                return false;
            }
        }
    }
}
