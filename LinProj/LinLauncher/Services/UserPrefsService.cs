// UserPrefsService.cs: 本機視窗／全螢幕偏好（對齊 Rust config.rs windowed / window_mode）。
using System;
using System.IO;

namespace LinLauncher.Services
{
    public sealed class UserDisplayPrefs
    {
        public bool Windowed { get; set; } = true;
        public uint WindowMode { get; set; } = 5;
    }

    public static class UserPrefsService
    {
        private const string FileName = "user_prefs.ini";

        public static string PrefsPath =>
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, FileName);

        public static UserDisplayPrefs Load()
        {
            var prefs = new UserDisplayPrefs();
            try
            {
                string path = PrefsPath;
                if (!File.Exists(path))
                    return prefs;

                foreach (string raw in File.ReadAllLines(path))
                {
                    string line = raw.Trim();
                    if (line.Length == 0 || line.StartsWith(';') || line.StartsWith('#'))
                        continue;
                    int eq = line.IndexOf('=');
                    if (eq <= 0) continue;
                    string key = line[..eq].Trim();
                    string val = line[(eq + 1)..].Trim();
                    if (key.Equals("windowed", StringComparison.OrdinalIgnoreCase))
                        prefs.Windowed = val is "1" or "true" or "yes" or "True";
                    else if (key.Equals("window_mode", StringComparison.OrdinalIgnoreCase)
                             && uint.TryParse(val, out uint mode) && mode is >= 4 and <= 7)
                        prefs.WindowMode = mode;
                }
            }
            catch (Exception ex)
            {
                LogService.Warn($"[prefs] Load failed: {ex.Message}");
            }
            return prefs;
        }

        public static void Save(UserDisplayPrefs prefs)
        {
            try
            {
                uint mode = prefs.WindowMode is >= 4 and <= 7 ? prefs.WindowMode : 5u;
                string content =
                    "[Settings]\n" +
                    $"windowed={(prefs.Windowed ? "true" : "false")}\n" +
                    $"window_mode={mode}\n";
                File.WriteAllText(PrefsPath, content);
            }
            catch (Exception ex)
            {
                LogService.Warn($"[prefs] Save failed: {ex.Message}");
            }
        }
    }
}
