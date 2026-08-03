// GameMemoryService.cs: 外部行程記憶體讀寫 + AOB 掃描 + 執行緒暫停/恢復。
// 對齊 Rust 版 L1J3.8Launcher(RUST)參考 src/memory.rs + src/process.rs 的語意，
// 純 C#／不透過 LauncherDll，供 GamePatchService 做 Stage2 記憶體修補用。
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace LinLauncher.Services
{
    /// <summary>AOB pattern 的一個位元組；null 代表萬用字元（對齊 Rust 的 Option&lt;u8&gt;）。</summary>
    public readonly struct PatternByte
    {
        public readonly byte? Value;
        private PatternByte(byte? value) => Value = value;
        public static PatternByte B(byte value) => new PatternByte(value);
        public static readonly PatternByte Wildcard = new PatternByte(null);
        public static implicit operator PatternByte(byte value) => B(value);
    }

    public class GameMemoryService
    {
        private readonly IntPtr _hProcess;

        public GameMemoryService(IntPtr hProcess)
        {
            _hProcess = hProcess;
        }

        public bool TryReadUInt32(uint addr, out uint value)
        {
            if (TryReadBytes(addr, 4, out var buf))
            {
                value = BitConverter.ToUInt32(buf, 0);
                return true;
            }
            value = 0;
            return false;
        }

        public bool TryReadBytes(uint addr, int size, out byte[] data)
        {
            var buf = new byte[size];
            bool ok = NativeMethods.ReadProcessMemory(_hProcess, new IntPtr((long)addr), buf, (uint)size, out var read);
            if (ok && read.ToInt64() == size)
            {
                data = buf;
                return true;
            }
            data = Array.Empty<byte>();
            return false;
        }

        /// <summary>安全寫入程式碼：VirtualProtectEx → Write → 恢復保護 → FlushInstructionCache（對齊 memory::write_code）。</summary>
        public bool WriteCode(uint addr, byte[] data)
        {
            var address = new IntPtr((long)addr);
            var size = new UIntPtr((uint)data.Length);

            if (!NativeMethods.VirtualProtectEx(_hProcess, address, size, NativeMethods.PAGE_EXECUTE_READWRITE, out uint oldProtect))
            {
                LogService.Warn($"[GameMemory] VirtualProtectEx 失敗 @ 0x{addr:X8}: {Marshal.GetLastWin32Error()}");
                return false;
            }

            bool wrote = NativeMethods.WriteProcessMemory(_hProcess, address, data, (uint)data.Length, out var written);
            NativeMethods.VirtualProtectEx(_hProcess, address, size, oldProtect, out _);

            if (!wrote || written.ToInt64() != data.Length)
            {
                LogService.Warn($"[GameMemory] WriteProcessMemory 失敗 @ 0x{addr:X8}: {Marshal.GetLastWin32Error()}");
                return false;
            }

            NativeMethods.FlushInstructionCache(_hProcess, address, size);
            return true;
        }

        /// <summary>AOB 掃描，回傳第一個命中位址（對齊 memory::scan_pattern）。</summary>
        public uint? ScanPattern(uint start, uint end, PatternByte[] pattern)
        {
            const int chunk = 0x10000; // 64KB
            int patLen = pattern.Length;
            uint addr = start;

            while (addr < end)
            {
                int size = (int)Math.Min(chunk + patLen, (long)(end - addr));
                if (!TryReadBytes(addr, size, out var data))
                {
                    addr += chunk;
                    continue;
                }

                for (int i = 0; i <= data.Length - patLen; i++)
                {
                    bool match = true;
                    for (int j = 0; j < patLen; j++)
                    {
                        var pb = pattern[j];
                        if (pb.Value.HasValue && data[i + j] != pb.Value.Value)
                        {
                            match = false;
                            break;
                        }
                    }
                    if (match) return addr + (uint)i;
                }

                addr += chunk;
            }

            return null;
        }

        /// <summary>AOB 掃描，回傳所有命中位址（對齊 memory::scan_pattern_all）。</summary>
        public List<uint> ScanPatternAll(uint start, uint end, PatternByte[] pattern)
        {
            const int chunk = 0x10000;
            int patLen = pattern.Length;
            uint addr = start;
            var results = new List<uint>();

            while (addr < end)
            {
                int size = (int)Math.Min(chunk + patLen, (long)(end - addr));
                if (!TryReadBytes(addr, size, out var data))
                {
                    addr += chunk;
                    continue;
                }

                for (int i = 0; i <= data.Length - patLen; i++)
                {
                    bool match = true;
                    for (int j = 0; j < patLen; j++)
                    {
                        var pb = pattern[j];
                        if (pb.Value.HasValue && data[i + j] != pb.Value.Value)
                        {
                            match = false;
                            break;
                        }
                    }
                    if (match) results.Add(addr + (uint)i);
                }

                addr += chunk;
            }

            return results;
        }

        /// <summary>暫停目標行程所有執行緒，回傳 thread handle 清單（對齊 process::suspend_threads）。</summary>
        public static List<IntPtr> SuspendThreads(uint pid)
        {
            var handles = new List<IntPtr>();
            IntPtr snapshot = NativeMethods.CreateToolhelp32Snapshot(NativeMethods.TH32CS_SNAPTHREAD, 0);
            if (snapshot == IntPtr.Zero || snapshot.ToInt64() == -1) return handles;

            try
            {
                var te = new NativeMethods.THREADENTRY32();
                te.dwSize = (uint)Marshal.SizeOf(te);
                if (NativeMethods.Thread32First(snapshot, ref te))
                {
                    do
                    {
                        if (te.th32OwnerProcessID == pid)
                        {
                            IntPtr h = NativeMethods.OpenThread(NativeMethods.ThreadAccessFlags.SuspendResume, false, te.th32ThreadID);
                            if (h != IntPtr.Zero)
                            {
                                NativeMethods.SuspendThread(h);
                                handles.Add(h);
                            }
                        }
                        te.dwSize = (uint)Marshal.SizeOf(te);
                    } while (NativeMethods.Thread32Next(snapshot, ref te));
                }
            }
            finally
            {
                NativeMethods.CloseHandle(snapshot);
            }

            return handles;
        }

        /// <summary>恢復先前暫停的所有執行緒（對齊 process::resume_threads）。</summary>
        public static void ResumeThreads(List<IntPtr> handles)
        {
            foreach (var h in handles)
            {
                NativeMethods.ResumeThread(h);
                NativeMethods.CloseHandle(h);
            }
        }
    }
}
