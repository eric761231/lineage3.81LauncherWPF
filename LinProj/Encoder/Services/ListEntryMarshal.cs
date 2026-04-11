using System;
using System.Runtime.InteropServices;

namespace LinEncoder.Services
{
    /// <summary>將 ServerListEntryNative 序列化為與 LinLauncher 相同長度的位元組（供 AES+XOR 加密）。</summary>
    public static class ListEntryMarshal
    {
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
    }
}
