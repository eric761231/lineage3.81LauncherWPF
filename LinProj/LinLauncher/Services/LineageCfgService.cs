// LineageCfgService.cs: 移植自 Rust lineage_cfg.rs — 啟動前寫入 lineage.cfg 顯示模式。
// Win11 + 舊 DirectDraw 全螢幕路徑易鎖死；啟動前把 FullScreen/WindowMode 寫好，避免遊戲內切模式。
using System;
using System.IO;
using System.Text;

namespace LinLauncher.Services
{
    public static class LineageCfgService
    {
        private const string CfgFile = "lineage.cfg";
        private const int HeaderLen = 0x1C;
        private static readonly byte[] Header = Encoding.ASCII.GetBytes("lineage configuration file\x1A\0");
        private const uint TlvTerminator = 0xFFFFFFFF;
        private const uint StringKeyThreshold = 0x2710;
        private const uint KeyFullscreen = 0x12;
        private const uint KeyWindowMode = 0x1A;
        private const uint KeyPrevWindowMode = 0x1B;
        private const uint DefaultWindowMode = 5;

        /// <summary>套用顯示模式（失敗只記 log，不拋出）。</summary>
        public static void ApplyDisplayMode(string gameDir, bool windowed, uint windowMode)
        {
            try
            {
                byte fullscreen = (byte)(windowed ? 0 : 1);
                SetFullscreen(gameDir, fullscreen);
                if (windowed)
                    SetWindowMode(gameDir, windowMode);
                LogService.Info(
                    $"[cfg] display mode applied: {(windowed ? "windowed" : "fullscreen")} WindowMode={windowMode}");
            }
            catch (Exception ex)
            {
                LogService.Warn($"[cfg] ApplyDisplayMode failed: {ex.Message}");
            }
        }

        public static void SetFullscreen(string gameDir, byte value)
        {
            UpdateValue(gameDir, KeyFullscreen, new[] { value }, "FullScreen");
        }

        public static void SetWindowMode(string gameDir, uint mode)
        {
            if (mode < 4 || mode > 7)
                throw new ArgumentOutOfRangeException(nameof(mode), "WindowMode 只接受 4..=7");
            UpdateValue(gameDir, KeyWindowMode, BitConverter.GetBytes(mode), "WindowMode");
        }

        private static void UpdateValue(string gameDir, uint targetKey, byte[] newBytes, string label)
        {
            string path = Path.Combine(gameDir, CfgFile);
            if (!File.Exists(path))
                CreateMinimalCfg(path, targetKey, newBytes);

            byte[] data = File.ReadAllBytes(path);
            if (data.Length < HeaderLen + 4)
                throw new InvalidDataException($"lineage.cfg 太短({data.Length} bytes)");

            int off = HeaderLen;
            while (off + 4 <= data.Length)
            {
                uint key = BitConverter.ToUInt32(data, off);
                if (key == TlvTerminator)
                    break;

                if (key >= StringKeyThreshold)
                {
                    int nul = Array.IndexOf(data, (byte)0, off + 4);
                    if (nul < 0)
                        throw new InvalidDataException("cfg 字串值缺結尾 0");
                    off = nul + 1;
                    continue;
                }

                if (off + 8 > data.Length)
                    throw new InvalidDataException($"cfg 截斷在 size 欄位 @ 0x{off:X}");

                int sz = (int)BitConverter.ToUInt32(data, off + 4);
                if (sz > 1024 || off + 8 + sz > data.Length)
                    throw new InvalidDataException($"cfg 內容毀損 @ 0x{off:X}(key=0x{key:X} sz={sz})");

                if (key == targetKey)
                {
                    if (sz != newBytes.Length)
                        throw new InvalidDataException($"{label} cfg size 不符(預期 {newBytes.Length} 但實際 {sz})");

                    int valueOff = off + 8;
                    bool same = true;
                    for (int i = 0; i < sz; i++)
                    {
                        if (data[valueOff + i] != newBytes[i]) { same = false; break; }
                    }
                    if (!same)
                    {
                        Array.Copy(newBytes, 0, data, valueOff, sz);
                        File.WriteAllBytes(path, data);
                        LogService.Info($"[cfg] {label} → {DisplayValue(newBytes)}");
                    }
                    return;
                }

                off += 8 + sz;
            }
            // key 找不到：對齊 Rust — no-op（既有 cfg 可能缺欄位）
        }

        private static void CreateMinimalCfg(string path, uint targetKey, byte[] newBytes)
        {
            byte fullscreen = (targetKey == KeyFullscreen && newBytes.Length == 1) ? newBytes[0] : (byte)0;
            uint windowMode = (targetKey == KeyWindowMode && newBytes.Length == 4)
                ? BitConverter.ToUInt32(newBytes, 0)
                : DefaultWindowMode;

            using var ms = new MemoryStream();
            ms.Write(Header, 0, Header.Length);
            PushTlv(ms, KeyFullscreen, new[] { fullscreen });
            PushTlv(ms, KeyWindowMode, BitConverter.GetBytes(windowMode));
            PushTlv(ms, KeyPrevWindowMode, BitConverter.GetBytes(windowMode));
            ms.Write(BitConverter.GetBytes(TlvTerminator), 0, 4);
            File.WriteAllBytes(path, ms.ToArray());
            LogService.Info("[cfg] created missing lineage.cfg");
        }

        private static void PushTlv(Stream s, uint key, byte[] value)
        {
            s.Write(BitConverter.GetBytes(key), 0, 4);
            s.Write(BitConverter.GetBytes((uint)value.Length), 0, 4);
            s.Write(value, 0, value.Length);
        }

        private static string DisplayValue(byte[] bytes) => bytes.Length switch
        {
            1 => bytes[0].ToString(),
            4 => BitConverter.ToUInt32(bytes, 0).ToString(),
            _ => BitConverter.ToString(bytes)
        };
    }
}
