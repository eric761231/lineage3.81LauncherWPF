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

        /// <summary>
        /// 視窗模式：不強制加 DISABLEDXMAXIMIZEDWINDOWEDMODE（對齊 Rust）。
        /// 全螢幕：合併寫入該旗標。
        /// </summary>
        public static void ApplyForLaunch(string gameExePath, bool windowed)
        {
            try
            {
                if (windowed)
                {
                    LogService.Info("[compat] windowed launch; fullscreen optimization flag skipped");
                    return;
                }

                EnsureCompatFlags(gameExePath, new[] { DisableFullscreenOptimizations });
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
