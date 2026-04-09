using System; using System.Diagnostics; using System.IO; using System.Windows.Forms;
class Program { [STAThread] static void Main() {
    string d = AppDomain.CurrentDomain.BaseDirectory;
    string e = Path.Combine(d, "LinLauncher_Environment");
    string t = Path.Combine(e, "LinLauncher.exe");
    if (!File.Exists(t)) { MessageBox.Show("Proxy Error: Cannot find core at " + t); return; }
    // 套用等待中的更新檔案（DLL 或資源檔案在執行中被鎖定，儲存為 .pending，此處替換）
    // 掃描登入器目錄（LinLauncher_Environment）與遊戲根目錄
    try {
        string[] scanDirs = new[] { e, d };
        foreach (var scanDir in scanDirs) {
            foreach (var pending in Directory.GetFiles(scanDir, "*.pending")) {
                string target = pending.Substring(0, pending.Length - 8);
                try { if (File.Exists(target)) File.Delete(target); File.Move(pending, target); } catch { }
            }
        }
    } catch { }
    // 若有資源 .pending 被套用，執行 eat.exe 吃檔（將 .eat 資源匯入 .pak）
    try {
        string eatExe = Path.Combine(d, "eat.exe");
        if (File.Exists(eatExe)) {
            var ep = Process.Start(new ProcessStartInfo(eatExe) { WorkingDirectory = d, UseShellExecute = false, CreateNoWindow = true });
            if (ep != null) ep.WaitForExit(30000);
        }
    } catch { }
    try {
        byte[] s = File.ReadAllBytes(Process.GetCurrentProcess().MainModule.FileName);
        int idx = -1; ulong sign = 0x12345678FEDCBAFF;
        for (int i = s.Length - 8; i >= 0; i--) { if (BitConverter.ToUInt64(s, i) == sign) { idx = i; break; } }
        if (idx != -1) {
            int len = s.Length - idx;
            byte[] conf = new byte[len];
            Array.Copy(s, idx, conf, 0, len);
            File.WriteAllBytes(Path.Combine(e, "config.dat"), conf);
        }
    } catch (Exception ex) { MessageBox.Show("Proxy Config Error: " + ex.Message); }
    try { 
        Process.Start(new ProcessStartInfo(t) { WorkingDirectory = e, UseShellExecute = true }); 
    } catch (Exception ex) { 
        MessageBox.Show("Proxy Launch Error: " + ex.Message); 
    }
} }
