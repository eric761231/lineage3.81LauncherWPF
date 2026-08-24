// =============================================================================
// LinLauncher.Proxy（組譯後檔名 LinLauncher.exe）— Encoder 外層「殼」啟動器
// -----------------------------------------------------------------------------
// 在整體流程中的位置：
//   • LinEncoder 會以本專案建置產出為「模板位元組」，再嵌入夥伴設定的組態，
//     最後產生給玩家下載的單一執行檔（舊稱／流程上常與 LinLauncher.dat 相關）。
//   • 玩家實際執行的是這個外殼；它負責環境準備後，再啟動同目錄下
//     Core\LinLauncher.exe（真正的 WPF 登入器本體）。
//
// 主要功能（依 Main 執行順序）：
//   1) 定位核心：必須存在「當前目錄\Core\LinLauncher.exe」。
//   2) 原子替換：在 Core 與外殼目錄掃描 *.pending，
//      將「xxx.bin.pending」更名為「xxx.bin」（先刪目標再 Move），用於更新時避免半套檔案。
//   3) eat.exe（選用）：若外殼目錄有 eat.exe，先以無視窗方式執行並最多等待 30 秒
//      （常見用途：清理／掛載／前置工具，失敗不阻擋後續）。
//   4) 從自身 PE 尾端抽出內嵌組態：在執行檔位元組中搜尋魔術數 0x12345678FEDCBAFF，
//      其後若緊接長度為 LauncherConfigSerializedSize 的區塊，則寫入
//      Core\config.dat，供內層 LinLauncher 讀取（與 Encoder 嵌入流程對齊）。
//   5) 啟動內層登入器：Process.Start，WorkingDirectory 設為 Core。
//
// 維護注意：
//   • LauncherConfigSerializedSize 必須與 LinLauncher.Models.LauncherConfig 序列化大小一致，
//     若結構變更請同步修改並重新跑 Encoder／測試嵌入組態。
// =============================================================================

using System;
using System.Diagnostics;
using System.IO;
using System.Windows.Forms;

namespace LinLauncher.Proxy;

internal static class Program
{
    /// <summary>
    /// 內嵌組態區塊位元組長度；須與 <c>LauncherConfig</c> 之封送／序列化總長度一致。
    /// Encoder 將組態附加於此外殼 exe 尾端時，此長度用於辨識有效載荷。
    /// </summary>
    private const int LauncherConfigSerializedSize = 4728;

    [STAThread]
    private static void Main()
    {
        string d = AppDomain.CurrentDomain.BaseDirectory;
        string e = Path.Combine(d, "Core");
        string t = Path.Combine(e, "LinLauncher.exe");

        // --- 核心本體必須與外殼同層之 Core 內 ---
        if (!File.Exists(t))
        {
            MessageBox.Show("Proxy Error: Cannot find core at " + t);
            return;
        }

        // --- 更新原子性：將 *.pending 套用為正式檔名（兩層目錄都掃） ---
        try
        {
            string[] scanDirs = new[] { e, d };
            foreach (string scanDir in scanDirs)
            {
                foreach (string pending in Directory.GetFiles(scanDir, "*.pending"))
                {
                    string target = pending.Substring(0, pending.Length - 8);
                    try
                    {
                        if (File.Exists(target))
                            File.Delete(target);
                        File.Move(pending, target);
                    }
                    catch { /* best effort：單檔失敗不影響其餘 */ }
                }
            }
        }
        catch { /* best effort */ }

        // --- 選用舊版 eat.exe（內建吃檔已改由 Core\LinLauncher 執行）；無檔案則略過 ---
        try
        {
            string eatExe = Path.Combine(d, "eat.exe");
            if (File.Exists(eatExe))
            {
                using (Process? ep = Process.Start(new ProcessStartInfo(eatExe)
                {
                    WorkingDirectory = d,
                    UseShellExecute = false,
                    CreateNoWindow = true
                }))
                {
                    ep?.WaitForExit(30000);
                }
            }
        }
        catch { /* best effort */ }

        // --- 從本進程 exe 尾端抽出 Encoder 嵌入的 LauncherConfig，寫成 config.dat ---
        try
        {
            string? exePath = Process.GetCurrentProcess().MainModule?.FileName;
            if (string.IsNullOrEmpty(exePath))
                return;
            byte[] s = File.ReadAllBytes(exePath);
            int idx = -1;
            const ulong sign = 0x12345678FEDCBAFF;
            for (int i = s.Length - 8; i >= 0; i--)
            {
                if (BitConverter.ToUInt64(s, i) == sign)
                {
                    idx = i;
                    break;
                }
            }

            if (idx != -1)
            {
                int len = s.Length - idx;
                if (len == LauncherConfigSerializedSize)
                {
                    byte[] conf = new byte[len];
                    Array.Copy(s, idx, conf, 0, len);
                    File.WriteAllBytes(Path.Combine(e, "config.dat"), conf);
                }
            }
        }
        catch (Exception ex)
        {
            MessageBox.Show("Proxy Config Error: " + ex.Message);
        }

        // --- 啟動真正的 WPF 登入器；工作目錄設在 Environment 以利相對路徑與資源 ---
        try
        {
            Process.Start(new ProcessStartInfo(t) { WorkingDirectory = e, UseShellExecute = true });
        }
        catch (Exception ex)
        {
            // 常見：UAC／防毒取消啟動（Win32「操作被使用者取消」）。不彈窗，只寫 Core\launcher.log。
            AppendLog(e, "Proxy Launch Error: " + ex.Message);
        }
    }

    private static void AppendLog(string coreDir, string message)
    {
        try
        {
            string path = Path.Combine(coreDir, "launcher.log");
            File.AppendAllText(path, $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] [Proxy] {message}{Environment.NewLine}");
        }
        catch
        {
            /* 寫 log 失敗也不要再彈窗 */
        }
    }
}
