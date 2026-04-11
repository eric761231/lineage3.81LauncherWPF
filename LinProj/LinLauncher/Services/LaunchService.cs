// LaunchService.cs: 負責遊戲啟動、DLL 注入及共享記憶體設定。
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using LinLauncher.Models;

namespace LinLauncher.Services
{
    public class LaunchService
    {
        /// <summary>Win32 ERROR_ELEVATION_REQUIRED：目標程式需要系統管理員權限，但呼叫端未以提升權限執行。</summary>
        private const int ErrorElevationRequired = 740;

        private const string ShmGuid = "{385FC524-96E3-4839-9909-1F2135D4F928}";
        private IntPtr _hShm = IntPtr.Zero;
        private IntPtr _pShm = IntPtr.Zero;
        public event EventHandler? GameExited;

        /// <summary>
        /// 啟動遊戲核心流程：1. 檢查檔案 2. 掛起建立進程 3. 設定共享記憶體 4. 注入 DLL 5. 恢復執行。
        /// (Core game launch flow: 1. Check files 2. Create suspended process 3. Setup SHM 4. Inject DLL 5. Resume.)
        /// </summary>
        public async Task<bool> LaunchGameAsync(ServerInfo server, string gameExePath, string dllPath, string account, string password)
        {
            if (!File.Exists(gameExePath) || !File.Exists(dllPath)) return false;

            var si = new NativeMethods.STARTUPINFO();
            si.cb = (uint)Marshal.SizeOf(si);
            var pi = new NativeMethods.PROCESS_INFORMATION();

            string? dir = Path.GetDirectoryName(gameExePath);
            bool success = NativeMethods.CreateProcess(gameExePath, null, IntPtr.Zero, IntPtr.Zero, false,
                NativeMethods.ProcessCreationFlags.Suspended, IntPtr.Zero, dir!, ref si, out pi);

            if (!success)
            {
                int err = Marshal.GetLastWin32Error();
                if (err == ErrorElevationRequired)
                {
                    System.Windows.MessageBox.Show(
                        "無法建立遊戲進程（錯誤 740：需要系統管理員權限）。\n\n"
                        + "請確認：\n"
                        + "• 已使用「以系統管理員身分執行」啟動登入器；或\n"
                        + "• 專案已連結 app.manifest（requireAdministrator），並重新建置／發行後再試。\n\n"
                        + "若遊戲主程式本身要求提高權限，登入器也必須具備足夠權限才能掛起並注入 DLL。",
                        "啟動遊戲",
                        System.Windows.MessageBoxButton.OK,
                        System.Windows.MessageBoxImage.Warning);
                }
                else
                {
                    System.Windows.MessageBox.Show(
                        $"無法建立遊戲進程：Win32 {err}（{new Win32Exception(err).Message}）",
                        "啟動遊戲",
                        System.Windows.MessageBoxButton.OK,
                        System.Windows.MessageBoxImage.Error);
                }
                return false;
            }

            try
            {
                SetupSharedMemory(pi.dwProcessId, server, account, password);
                if (!InjectDll(pi.dwProcessId, dllPath))
                {
                    System.Windows.MessageBox.Show("DLL Injection failed!");
                    Process.GetProcessById((int)pi.dwProcessId).Kill();
                    return false;
                }
                NativeMethods.ResumeThread(pi.hThread);
                _ = MonitorProcessAsync((int)pi.dwProcessId);
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
        private void SetupSharedMemory(uint pid, ServerInfo server, string account, string password)
        {
            string name = $"Local\\{ShmGuid}";
            _hShm = NativeMethods.CreateFileMapping(new IntPtr(-1), IntPtr.Zero, NativeMethods.PAGE_READWRITE, 0, 8192, name);
            if (_hShm == IntPtr.Zero) return;
            _pShm = NativeMethods.MapViewOfFile(_hShm, NativeMethods.FILE_MAP_ALL_ACCESS, 0, 0, 0);
            if (_pShm == IntPtr.Zero) return;

            string bdPath = GamePathHelper.TruncateForBdFileBuffer(GamePathHelper.ResolveBdFilePath(server.BdFile));
            var share = new ShareInfo
            {
                Port = server.Port,
                Encrypt = server.Encrypt,
                UseHelper = server.UseHelper,
                UseBd = server.UseBd,
                BdFile = bdPath,
                RandEnc = server.RandKey,
                RsaN = server.N,
                RsaD = server.D,
                Magic = 0x12345678
            };
            share.SetIp(server.Ip);
            Array.Copy(server.Key, share.Key, 16);

            // 填入帳號密碼 (Fill Account and Password)
            byte[] accBytes = Encoding.ASCII.GetBytes(account ?? "");
            byte[] pwdBytes = Encoding.ASCII.GetBytes(password ?? "");
            Array.Copy(accBytes, 0, share.Account, 0, Math.Min(accBytes.Length, 32));
            Array.Copy(pwdBytes, 0, share.Password, 0, Math.Min(pwdBytes.Length, 32));

            Marshal.StructureToPtr(share, _pShm, false);
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
