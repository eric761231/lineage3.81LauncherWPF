// CompatFlagsService.cs: 移植自 Rust dpi_override.rs 的 AppCompat Layers 合併寫入。
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Microsoft.Win32;

namespace LinLauncher.Services
{
    public static class CompatFlagsService
    {
        private const string SubKey = @"Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers";
        private const string DisableFullscreenOptimizations = "DISABLEDXMAXIMIZEDWINDOWEDMODE";

        // 對應「內容 → 相容性 → 變更高 DPI 設定」裡「使用舊版顯示器 ICC 色彩管理」+
        // 「停用高 DPI 縮放」這兩個勾選框（Windows 只會寫這一個旗標，不是兩個）。
        // 這個 client 是舊版 8-bit/16-bit 調色盤 DirectDraw 渲染，windowed 模式下
        // 沒有這個旗標，現代 DWM 合成器會把調色盤解讀錯誤，畫面整個變成紫綠色雜訊
        // （遊戲自己也會跳 STR_MESSAGE_WINDOW_MODE_ONLY_16BIT_COLOR 這個訊息）。
        // 旗標名稱直接從實測手動勾選後 Windows 自己寫入登錄檔的值取得，不是用猜的。
        private const string Dwm8And16BitMitigation = "DWM8And16BitMitigation";

        // 「內容 → 相容性 → 執行相容模式：Windows 7」——實驗性追加，使用者懷疑先前
        // 一直沒真正查出根因的「斷線畫面整片白」（DisconnectOverlay 那次診斷 log
        // 加了但沒測完）也是同一類「舊版 DirectDraw client 在現代 Windows 版本相容
        // 層下渲染異常」的問題，反映他自己過去用 Win7 相容模式測試時畫面一定正常。
        // 這是假設，還沒實測驗證，先跟 DWM8And16BitMitigation 一起無條件套用，觀察
        // 斷線白屏問題是否也一併消失。
        private const string Win7CompatMode = "WIN7RTM";

        /// <summary>
        /// DWM8And16BitMitigation、WIN7RTM 不分視窗/全螢幕都要套用（畫面調色盤/
        /// 舊版相容性修正）。DISABLEDXMAXIMIZEDWINDOWEDMODE 只在全螢幕模式套用
        /// （對齊 Rust）。
        /// </summary>
        public static void ApplyForLaunch(string gameExePath, bool windowed)
        {
            try
            {
                var flags = new List<string> { Dwm8And16BitMitigation, Win7CompatMode };
                if (!windowed)
                    flags.Add(DisableFullscreenOptimizations);
                else
                    LogService.Info("[compat] windowed launch; fullscreen optimization flag skipped");

                EnsureCompatFlags(gameExePath, flags);
            }
            catch (Exception ex)
            {
                LogService.Warn($"[compat] ApplyForLaunch failed: {ex.Message}");
            }
        }

        public static void EnsureCompatFlags(string exePath, IReadOnlyList<string> requiredFlags)
        {
            string pathStr = Path.GetFullPath(exePath);
            if (pathStr.StartsWith(@"\\?\", StringComparison.Ordinal))
                pathStr = pathStr.Substring(4);

            using RegistryKey? key = Registry.CurrentUser.CreateSubKey(SubKey, writable: true);
            if (key == null)
                throw new InvalidOperationException("無法開啟 AppCompatFlags\\Layers");

            string? existing = key.GetValue(pathStr) as string;
            string merged = MergeCompatFlags(existing, requiredFlags);
            if (string.Equals(existing?.Trim(), merged, StringComparison.Ordinal))
                return;

            key.SetValue(pathStr, merged, RegistryValueKind.String);
            LogService.Info($"[compat] set {pathStr} => {merged}");
        }

        internal static string MergeCompatFlags(string? existing, IReadOnlyList<string> requiredFlags)
        {
            var flags = (existing ?? "")
                .Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries)
                .Where(p => p != "~")
                .Select(p => p.Trim())
                .Where(p => p.Length > 0)
                .ToList();

            foreach (string required in requiredFlags)
            {
                if (!flags.Any(f => f.Equals(required, StringComparison.OrdinalIgnoreCase)))
                    flags.Add(required);
            }

            return flags.Count == 0 ? "~" : "~ " + string.Join(" ", flags);
        }
    }
}
