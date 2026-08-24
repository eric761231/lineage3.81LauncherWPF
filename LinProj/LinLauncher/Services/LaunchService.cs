// LaunchService.cs: 負責遊戲啟動、DLL 注入及共享記憶體設定。
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using LinLauncher.Models;
using LinLauncher;

namespace LinLauncher.Services
{
    public class LaunchService
    {
        // 與 LauncherDll/ShareMemory.h 的 SHARE_INFO (pack=1) 完全對齊
        private const int ShareInfoSize = 653;
        private const int OffsetIp = 0;            // BYTE[32]
        private const int OffsetPort = 32;         // int
        private const int OffsetEncrypt = 36;      // bool (1 byte)
        private const int OffsetUseHelper = 37;    // bool (1 byte)
        private const int OffsetKey = 38;          // BYTE[16]
        private const int OffsetUseBd = 54;        // bool (1 byte)
        private const int OffsetBdFile = 55;       // wchar_t[260] => 520 bytes (UTF-16LE)
        private const int OffsetRead = 575;        // bool (1 byte)
        private const int OffsetRandEnc = 576;     // bool (1 byte)
        private const int OffsetRsaN = 577;        // uint
        private const int OffsetRsaD = 581;        // uint
        private const int OffsetMagic = 585;       // uint
        private const int OffsetAccount = 589;     // char[32]
        private const int OffsetPassword = 621;    // char[32]
        private const uint ShareMagic = 0x12345678;

        /// <summary>Win32 ERROR_ELEVATION_REQUIRED：目標程式需要系統管理員權限，但呼叫端未以提升權限執行。</summary>
        private const int ErrorElevationRequired = 740;

        private const string ShmGuid = "{385FC524-96E3-4839-9909-1F2135D4F928}";
        private IntPtr _hShm = IntPtr.Zero;
        private IntPtr _pShm = IntPtr.Zero;
        public event EventHandler? GameExited;

        /// <summary>遊戲進程已建立、DLL 已注入、ResumeThread 已呼叫（引數是遊戲進程 PID）。
        /// 給 UI 用來啟動「等遊戲視窗出現」的進度模擬，跟 GameExited 是對稱的一對事件。</summary>
        public event EventHandler<int>? GameProcessStarted;

        /// <summary>
        /// 啟動遊戲核心流程：1. 顯示模式／相容性旗標 2. 掛起建立進程 3. 設定共享記憶體 4. 注入 DLL 5. 恢復執行。
        /// </summary>
        /// <param name="windowed">true=視窗模式（對齊 Rust 預設）；false=全螢幕。</param>
        /// <param name="windowMode">視窗解析度代碼 4..=7（預設 5=800x600）。</param>
        public async Task<bool> LaunchGameAsync(
            ServerInfo server,
            string gameExePath,
            string dllPath,
            string account,
            string password,
            bool windowed = true,
            uint windowMode = 5)
        {
            if (!File.Exists(gameExePath) || !File.Exists(dllPath)) return false;

            string? dir = Path.GetDirectoryName(gameExePath);
            if (!string.IsNullOrEmpty(dir))
            {
                uint mode = windowMode is >= 4 and <= 7 ? windowMode : 5u;
                LineageCfgService.ApplyDisplayMode(dir, windowed, mode);
                CompatFlagsService.ApplyForLaunch(gameExePath, windowed);
            }

            var si = new NativeMethods.STARTUPINFO();
            si.cb = (uint)Marshal.SizeOf(si);
            var pi = new NativeMethods.PROCESS_INFORMATION();

            bool success = NativeMethods.CreateProcess(gameExePath, null, IntPtr.Zero, IntPtr.Zero, false,
                NativeMethods.ProcessCreationFlags.Suspended, IntPtr.Zero, dir!, ref si, out pi);

            if (!success)
            {
                int err = Marshal.GetLastWin32Error();
                if (err == ErrorElevationRequired)
                {
                    string msg740 =
                        "無法建立遊戲進程（錯誤 740：需要系統管理員權限）。\n\n"
                        + "請確認：\n"
                        + "• 已使用「以系統管理員身分執行」啟動登入器；或\n"
                        + "• 專案已連結 app.manifest（requireAdministrator），並重新建置／發行後再試。\n\n"
                        + "若遊戲主程式本身要求提高權限，登入器也必須具備足夠權限才能掛起並注入 DLL。";
                    ErrorLog.LogMessageBox("啟動遊戲", msg740, "LaunchService.CreateProcess.740");
                    System.Windows.MessageBox.Show(
                        msg740,
                        "啟動遊戲",
                        System.Windows.MessageBoxButton.OK,
                        System.Windows.MessageBoxImage.Warning);
                }
                else
                {
                    string msgCp = $"無法建立遊戲進程：Win32 {err}（{new Win32Exception(err).Message}）";
                    ErrorLog.LogMessageBox("啟動遊戲", msgCp, "LaunchService.CreateProcess");
                    System.Windows.MessageBox.Show(
                        msgCp,
                        "啟動遊戲",
                        System.Windows.MessageBoxButton.OK,
                        System.Windows.MessageBoxImage.Error);
                }
                return false;
            }

            try
            {
                try
                {
                    string resolvedDllPath = Path.GetFullPath(dllPath);
                    StartupLog.Append($"LaunchGame: 注入 DLL 路徑={resolvedDllPath}");
                    if (!File.Exists(resolvedDllPath))
                        StartupLog.Append($"LaunchGame: [WARN] DLL 路徑不存在={resolvedDllPath}");
                }
                catch (Exception ex)
                {
                    StartupLog.Append("LaunchGame: 解析 DLL 路徑失敗", ex);
                }

                if (!SetupSharedMemory(pi.dwProcessId, server, account, password))
                {
                    System.Windows.MessageBox.Show("共享記憶體初始化失敗，無法傳遞啟動參數。");
                    Process.GetProcessById((int)pi.dwProcessId).Kill();
                    return false;
                }
                StartupLog.Append($"LaunchGame: SHM {server.Name} encrypt={server.Encrypt} randenc={server.RandKey} {server.Ip}:{server.Port}");
                if (!InjectDll(pi.dwProcessId, dllPath))
                {
                    System.Windows.MessageBox.Show("DLL Injection failed!");
                    Process.GetProcessById((int)pi.dwProcessId).Kill();
                    return false;
                }
                InjectOptionalImeDll(pi.dwProcessId, dllPath);
                NativeMethods.ResumeThread(pi.hThread);
                _ = MonitorProcessAsync((int)pi.dwProcessId);
                _ = GamePatchService.ApplyGapFillPatchesAsync(pi.dwProcessId);
                GameProcessStarted?.Invoke(this, (int)pi.dwProcessId);
                return true;
            }
            finally
            {
                NativeMethods.CloseHandle(pi.hProcess);
                NativeMethods.CloseHandle(pi.hThread);
            }
        }

        /// <summary>
        /// 建立具名共享記憶體，將選中的伺服器資訊傳遞給 DLL。
        /// (Creates named shared memory to pass selected server info to the DLL.)
        /// </summary>
        private bool SetupSharedMemory(uint pid, ServerInfo server, string account, string password)
        {
            string name = $"Local\\{ShmGuid}";
            int shmSize = ShareInfoSize;
            _hShm = NativeMethods.CreateFileMapping(new IntPtr(-1), IntPtr.Zero, NativeMethods.PAGE_READWRITE, 0, (uint)shmSize, name);
            if (_hShm == IntPtr.Zero) return false;
            _pShm = NativeMethods.MapViewOfFile(_hShm, NativeMethods.FILE_MAP_ALL_ACCESS, 0, 0, 0);
            if (_pShm == IntPtr.Zero) return false;

            // 先清空共享區，避免沿用前次啟動殘值。
            Marshal.Copy(new byte[shmSize], 0, _pShm, shmSize);

            string bdPath = GamePathHelper.TruncateForBdFileBuffer(GamePathHelper.ResolveBdFilePath(server.BdFile));
            StartupLog.Append($"LaunchGame: server.UseBd={server.UseBd} server.BdFile='{server.BdFile}' resolved bdPath='{bdPath}' exists={(string.IsNullOrEmpty(bdPath) ? "n/a" : File.Exists(bdPath).ToString())}");
            var payload = new byte[ShareInfoSize];

            // ip[32] (ASCII)
            byte[] ipBytes = Encoding.ASCII.GetBytes(server.Ip ?? "");
            Array.Copy(ipBytes, 0, payload, OffsetIp, Math.Min(ipBytes.Length, 32));

            // key[16]
            if (server.Key != null)
                Array.Copy(server.Key, 0, payload, OffsetKey, Math.Min(server.Key.Length, 16));

            // account/password char[32] (ASCII)
            byte[] accBytes = Encoding.ASCII.GetBytes(account ?? "");
            byte[] pwdBytes = Encoding.ASCII.GetBytes(password ?? "");
            Array.Copy(accBytes, 0, payload, OffsetAccount, Math.Min(accBytes.Length, 32));
            Array.Copy(pwdBytes, 0, payload, OffsetPassword, Math.Min(pwdBytes.Length, 32));

            // bdfile[260] (UTF-16LE, null-terminated)
            if (!string.IsNullOrEmpty(bdPath))
            {
                string trimmed = bdPath.Length > 259 ? bdPath.Substring(0, 259) : bdPath;
                byte[] bdBytes = Encoding.Unicode.GetBytes(trimmed + "\0");
                int maxBdBytes = 260 * 2;
                Array.Copy(bdBytes, 0, payload, OffsetBdFile, Math.Min(bdBytes.Length, maxBdBytes));
            }

            // scalar fields
            Buffer.BlockCopy(BitConverter.GetBytes(server.Port), 0, payload, OffsetPort, 4);
            payload[OffsetEncrypt] = server.Encrypt ? (byte)1 : (byte)0;
            payload[OffsetUseHelper] = server.UseHelper ? (byte)1 : (byte)0;
            payload[OffsetUseBd] = server.UseBd ? (byte)1 : (byte)0;
            payload[OffsetRead] = 0;
            payload[OffsetRandEnc] = server.RandKey ? (byte)1 : (byte)0;
            Buffer.BlockCopy(BitConverter.GetBytes(server.N), 0, payload, OffsetRsaN, 4);
            Buffer.BlockCopy(BitConverter.GetBytes(server.D), 0, payload, OffsetRsaD, 4);
            Buffer.BlockCopy(BitConverter.GetBytes(ShareMagic), 0, payload, OffsetMagic, 4);

            Marshal.Copy(payload, 0, _pShm, payload.Length);

            int magic = Marshal.ReadInt32(_pShm, OffsetMagic);
            return unchecked((uint)magic) == ShareMagic;
        }

        /// <summary>
        /// 選擇性注入 IME 候選字視窗 DLL（比照 Rust 版 ime_overlay，獨立 DLL、跟核心
        /// LauncherDll 注入分開）。跟主要 DLL 放同一目錄；找不到檔案或注入失敗都只記
        /// log、不彈窗、不影響遊戲繼續啟動 —— 這是可選的輔助功能，不是核心流程。
        /// </summary>
        private void InjectOptionalImeDll(uint pid, string mainDllPath)
        {
            try
            {
                string? dir = Path.GetDirectoryName(Path.GetFullPath(mainDllPath));
                if (string.IsNullOrEmpty(dir)) return;
                string imeDllPath = Path.Combine(dir, "LineageIme.dll");
                if (!File.Exists(imeDllPath))
                {
                    StartupLog.Append($"LaunchGame: LineageIme.dll 不存在，跳過候選字視窗注入 ({imeDllPath})");
                    return;
                }

                IntPtr hProcess = NativeMethods.OpenProcess(NativeMethods.ProcessAccessFlags.All, false, pid);
                if (hProcess == IntPtr.Zero)
                {
                    StartupLog.Append("LaunchGame: [WARN] OpenProcess 失敗，跳過 LineageIme.dll 注入");
                    return;
                }
                try
                {
                    byte[] pathBytes = Encoding.Unicode.GetBytes(imeDllPath + "\0");
                    uint size = (uint)pathBytes.Length;
                    IntPtr pLibPath = NativeMethods.VirtualAllocEx(hProcess, IntPtr.Zero, size, NativeMethods.MEM_COMMIT, NativeMethods.PAGE_READWRITE);
                    if (pLibPath == IntPtr.Zero)
                    {
                        StartupLog.Append("LaunchGame: [WARN] VirtualAllocEx 失敗，跳過 LineageIme.dll 注入");
                        return;
                    }
                    if (!NativeMethods.WriteProcessMemory(hProcess, pLibPath, pathBytes, size, out _))
                    {
                        StartupLog.Append("LaunchGame: [WARN] WriteProcessMemory 失敗，跳過 LineageIme.dll 注入");
                        return;
                    }
                    IntPtr hKernel32 = NativeMethods.GetModuleHandle("kernel32.dll");
                    IntPtr pLoadLibraryW = NativeMethods.GetProcAddress(hKernel32, "LoadLibraryW");
                    IntPtr hThread = NativeMethods.CreateRemoteThread(hProcess, IntPtr.Zero, 0, pLoadLibraryW, pLibPath, 0, out _);
                    if (hThread == IntPtr.Zero)
                    {
                        StartupLog.Append($"LaunchGame: [WARN] LineageIme.dll CreateRemoteThread 失敗，Win32={Marshal.GetLastWin32Error()}");
                        return;
                    }
                    NativeMethods.CloseHandle(hThread);
                    StartupLog.Append($"LaunchGame: LineageIme.dll 已注入 ({imeDllPath})");
                }
                finally { NativeMethods.CloseHandle(hProcess); }
            }
            catch (Exception ex)
            {
                StartupLog.Append("LaunchGame: LineageIme.dll 注入發生例外（不影響遊戲繼續啟動）", ex);
            }
        }

        /// <summary>
        /// 使用遠端執行緒技術將指定 DLL 注入到遊戲進程中。
        /// (Injects the specified DLL into the game process using remote thread technique.)
        /// </summary>
        private bool InjectDll(uint pid, string dllPath)
        {
            IntPtr hProcess = NativeMethods.OpenProcess(NativeMethods.ProcessAccessFlags.All, false, pid);
            if (hProcess == IntPtr.Zero) return false;
            try
            {
                byte[] pathBytes = Encoding.Unicode.GetBytes(dllPath + "\0");
                uint size = (uint)pathBytes.Length;
                IntPtr pLibPath = NativeMethods.VirtualAllocEx(hProcess, IntPtr.Zero, size, NativeMethods.MEM_COMMIT, NativeMethods.PAGE_READWRITE);
                if (pLibPath == IntPtr.Zero) return false;
                if (!NativeMethods.WriteProcessMemory(hProcess, pLibPath, pathBytes, size, out _)) return false;
                IntPtr hKernel32 = NativeMethods.GetModuleHandle("kernel32.dll");
                IntPtr pLoadLibraryW = NativeMethods.GetProcAddress(hKernel32, "LoadLibraryW");
                IntPtr hThread = NativeMethods.CreateRemoteThread(hProcess, IntPtr.Zero, 0, pLoadLibraryW, pLibPath, 0, out _);
                if (hThread == IntPtr.Zero) 
                {
                    System.Windows.MessageBox.Show($"CreateRemoteThread failed with error: {Marshal.GetLastWin32Error()}");
                    return false;
                }
                NativeMethods.CloseHandle(hThread);
                return true;
            }
            finally { NativeMethods.CloseHandle(hProcess); }
        }

        /// <summary>遊戲主視窗 class 名稱（跟 LauncherDll.cpp 判斷"Lineage"視窗的字串一致）。</summary>
        private const string GameWindowClassName = "Lineage";

        /// <summary>
        /// 從建立進程到遊戲主視窗（class="Lineage"）出現，實測要等保護殼解密＋
        /// LauncherDll 掛勾流程跑完，通常一台機器上要 10~20 秒不等，使用者在這段
        /// 空窗期看不到任何畫面變化，容易誤以為卡住了。DLL 端目前沒有跨行程主動
        /// 通知機制（只有寫 log），所以這裡改用輪詢 EnumWindows 找屬於這個 pid 的
        /// Lineage 視窗，配合 <paramref name="progress"/> 回報一個「模擬」進度百分比
        /// 讓 UI 有東西可以動，不是精確量測；<paramref name="estimatedSeconds"/> 只影響
        /// 模擬進度條跑的速度，實際完成時機還是以視窗真的出現為準。
        /// 逾時（預設 60 秒）還沒看到視窗不代表啟動失敗——保護殼在較慢的機器上可能
        /// 真的需要更久，這裡只是停止模擬進度、把主控權交還給 UI，不會踢掉遊戲進程。
        /// </summary>
        public async Task<bool> WaitForGameWindowAsync(
            int pid,
            IProgress<int>? progress,
            double estimatedSeconds = 14.5, // 實測視窗通常在 15 秒前後就出現，故意抓得比實測平均值略短，讓曲線在視窗真的出現時已經接近 99% 而不是 95~97%
            int timeoutSeconds = 60,
            CancellationToken ct = default)
        {
            var sw = Stopwatch.StartNew();
            var timeout = TimeSpan.FromSeconds(timeoutSeconds);
            double estimatedMs = Math.Max(1, estimatedSeconds) * 1000.0;
            while (sw.Elapsed < timeout && !ct.IsCancellationRequested)
            {
                if (FindWindowByClassAndPid(GameWindowClassName, pid) != IntPtr.Zero)
                {
                    progress?.Report(100);
                    return true;
                }
                // 實測視窗通常在模擬進度跑到 95% 前後那個時間點就出現了（見
                // estimatedSeconds 的說明），所以直接讓模擬曲線在 estimatedSeconds
                // 這個時間點跑到 100%（比原本「跑到 95% 封頂」快一點點），體感上
                // 跟視窗真的出現的時機更吻合。
                int pct = (int)Math.Min(100, sw.Elapsed.TotalMilliseconds / estimatedMs * 100);
                progress?.Report(pct);
                try { await Task.Delay(200, ct).ConfigureAwait(false); }
                catch (OperationCanceledException) { break; }
            }
            return false;
        }

        private static IntPtr FindWindowByClassAndPid(string className, int pid)
        {
            IntPtr found = IntPtr.Zero;
            NativeMethods.EnumWindows((hWnd, _) =>
            {
                NativeMethods.GetWindowThreadProcessId(hWnd, out uint windowPid);
                if (windowPid == (uint)pid)
                {
                    var sb = new StringBuilder(256);
                    NativeMethods.GetClassName(hWnd, sb, sb.Capacity);
                    if (sb.ToString() == className)
                    {
                        found = hWnd;
                        return false; // 找到了，停止列舉
                    }
                }
                return true;
            }, IntPtr.Zero);
            return found;
        }

        private async Task MonitorProcessAsync(int pid)
        {
            try
            {
                using var process = Process.GetProcessById(pid);
                while (!process.HasExited) await Task.Delay(1000);
            }
            catch { }
            finally
            {
                GameExited?.Invoke(this, EventArgs.Empty);
                CleanupSharedMemory();
            }
        }

        private void CleanupSharedMemory()
        {
            if (_pShm != IntPtr.Zero) { NativeMethods.UnmapViewOfFile(_pShm); _pShm = IntPtr.Zero; }
            if (_hShm != IntPtr.Zero) { NativeMethods.CloseHandle(_hShm); _hShm = IntPtr.Zero; }
        }
    }
}
