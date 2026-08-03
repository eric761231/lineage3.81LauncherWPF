using System;
using System.IO;

namespace LinLauncher.Services
{
    /// <summary>
    /// 遊戲根目錄與 BD 資源檔路徑（與 LauncherDll ShareMemory wchar_t bdfile[260] 一致）。
    /// </summary>
    public static class GamePathHelper
    {
        /// <summary>
        /// 遊戲根目錄（例如 C:\3.81Lineage）：登入器在 Core 內執行時為其父目錄，否則為 BaseDirectory。
        /// </summary>
        public static string GetGameRootDirectory()
        {
            string baseDir = AppDomain.CurrentDomain.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            string leaf = Path.GetFileName(baseDir);
            if (string.Equals(leaf, "Core", StringComparison.OrdinalIgnoreCase))
            {
                var parent = Directory.GetParent(baseDir);
                if (parent != null)
                    return parent.FullName.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            }

            return baseDir;
        }

        /// <summary>
        /// 將伺服器列表中的 BD 檔名（常僅為檔名）解析為完整路徑；已為絕對路徑則正規化。
        /// </summary>
        public static string ResolveBdFilePath(string? bdFileName)
        {
            if (string.IsNullOrWhiteSpace(bdFileName))
                return "";
            string t = bdFileName.Trim().Split('\0')[0];
            if (Path.IsPathRooted(t))
                return Path.GetFullPath(t);
            string root = GetGameRootDirectory();
            return Path.GetFullPath(Path.Combine(root, t));
        }

        /// <summary>與 native wchar_t bdfile[260] 相容：可存完整本機路徑（與 Windows MAX_PATH 同級）。</summary>
        public const int MaxBdFileCharCount = 259;

        public static string TruncateForBdFileBuffer(string fullPath)
        {
            if (string.IsNullOrEmpty(fullPath))
                return "";
            if (fullPath.Length <= MaxBdFileCharCount)
                return fullPath;
            return fullPath.Substring(0, MaxBdFileCharCount);
        }

        public const string DefaultGameExeFileName = "TW13081901.bin";

        /// <summary>
        /// 依序尋找遊戲主程式：登入器目錄 → 遊戲根目錄（與 Core 同層之上一層）→ 開發用 Client 目錄。
        /// </summary>
        public static bool TryResolveGameExecutablePath(out string fullPath)
        {
            string appDir = AppDomain.CurrentDomain.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            string name = DefaultGameExeFileName;

            string inLauncherDir = Path.Combine(appDir, name);
            if (File.Exists(inLauncherDir))
            {
                fullPath = Path.GetFullPath(inLauncherDir);
                return true;
            }

            string inGameRoot = Path.Combine(GetGameRootDirectory(), name);
            if (File.Exists(inGameRoot))
            {
                fullPath = Path.GetFullPath(inGameRoot);
                return true;
            }

            try
            {
                string devClient = Path.GetFullPath(Path.Combine(appDir, @"..\..\..\..\Client\TW13081901.bin"));
                if (File.Exists(devClient))
                {
                    fullPath = devClient;
                    return true;
                }
            }
            catch { /* 開發路徑無效時略過 */ }

            fullPath = Path.GetFullPath(inGameRoot);
            return false;
        }

        public const string LauncherDllFileName = "LauncherDll.dll";

        /// <summary>
        /// 依序尋找 LauncherDll.dll：登入器目錄 → 遊戲根目錄 → 開發用 LauncherDll\Debug 或 Release。
        /// </summary>
        public static bool TryResolveLauncherDllPath(out string fullPath)
        {
            string appDir = AppDomain.CurrentDomain.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            string name = LauncherDllFileName;

            string inLauncherDir = Path.Combine(appDir, name);
            if (File.Exists(inLauncherDir))
            {
                fullPath = Path.GetFullPath(inLauncherDir);
                return true;
            }

            string inGameRoot = Path.Combine(GetGameRootDirectory(), name);
            if (File.Exists(inGameRoot))
            {
                fullPath = Path.GetFullPath(inGameRoot);
                return true;
            }

            try
            {
                string devDebug = Path.GetFullPath(Path.Combine(appDir, @"..\..\..\..\LauncherDll\Debug\LauncherDll.dll"));
                if (File.Exists(devDebug))
                {
                    fullPath = devDebug;
                    return true;
                }
                string devRelease = Path.GetFullPath(Path.Combine(appDir, @"..\..\..\..\LauncherDll\Release\LauncherDll.dll"));
                if (File.Exists(devRelease))
                {
                    fullPath = devRelease;
                    return true;
                }
            }
            catch { /* 開發路徑無效時略過 */ }

            fullPath = Path.GetFullPath(inGameRoot);
            return false;
        }
    }
}
