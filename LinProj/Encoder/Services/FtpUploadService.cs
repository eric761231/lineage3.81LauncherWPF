using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;

namespace LinEncoder.Services
{
    public class FtpUploadResult
    {
        public bool Success { get; set; }
        public string? ErrorMessage { get; set; }
        public int UploadedCount { get; set; }
    }

    /// <summary>
    /// 把補丁產生工具的輸出目錄整包上傳到遠端 FTP 伺服器，保留相對子目錄結構。
    /// 用 .NET 內建 FtpWebRequest（標示 obsolete 但功能仍完整），不額外加 NuGet 依賴。
    /// </summary>
    public static class FtpUploadService
    {
        public static FtpUploadResult UploadDirectory(
            string localDir,
            string host,
            int port,
            string username,
            string password,
            string remoteDir,
            IProgress<(int current, int total, string relativePath)>? progress,
            Action<string>? log = null)
        {
            void L(string m)
            {
                try { log?.Invoke(m); } catch { }
            }

            if (string.IsNullOrWhiteSpace(localDir) || !Directory.Exists(localDir))
                return new FtpUploadResult { Success = false, ErrorMessage = "本機來源目錄不存在。" };
            if (string.IsNullOrWhiteSpace(host))
                return new FtpUploadResult { Success = false, ErrorMessage = "請填寫 FTP 主機。" };

            string root = Path.GetFullPath(localDir);
            var skipNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { "Thumbs.db", ".DS_Store" };
            List<string> files = Directory.GetFiles(root, "*", SearchOption.AllDirectories)
                .Where(f => !skipNames.Contains(Path.GetFileName(f)))
                .OrderBy(f => f, StringComparer.OrdinalIgnoreCase)
                .ToList();

            if (files.Count == 0)
                return new FtpUploadResult { Success = false, ErrorMessage = "來源目錄內沒有可上傳的檔案。" };

            string baseRemoteDir = NormalizeRemoteDir(remoteDir);
            var madeDirs = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

            L($"開始上傳：{files.Count} 個檔案 → ftp://{host}:{port}{baseRemoteDir}");
            progress?.Report((0, files.Count, ""));

            for (int i = 0; i < files.Count; i++)
            {
                string fullPath = files[i];
                string rel = Path.GetRelativePath(root, fullPath).Replace('\\', '/');
                progress?.Report((i + 1, files.Count, rel));

                string remotePath = baseRemoteDir.TrimEnd('/') + "/" + rel;
                string? remoteFileDir = GetRemoteDirectory(remotePath);
                if (remoteFileDir != null && madeDirs.Add(remoteFileDir))
                {
                    L($"建立目錄 {remoteFileDir}");
                    string? err = EnsureRemoteDirectoryExists(host, port, username, password, remoteFileDir, log);
                    if (err != null)
                    {
                        L($"[失敗] 建立目錄 {remoteFileDir}：{err}");
                        return new FtpUploadResult { Success = false, ErrorMessage = $"建立遠端目錄失敗：{remoteFileDir}\n{err}", UploadedCount = i };
                    }
                }

                try
                {
                    UploadFile(fullPath, host, port, username, password, remotePath, log);
                    L($"[{i + 1}/{files.Count}] {rel}");
                }
                catch (WebException ex)
                {
                    L($"[失敗] {rel}：{DescribeFtpError(ex)}");
                    return new FtpUploadResult
                    {
                        Success = false,
                        ErrorMessage = $"上傳失敗：{rel}\n{DescribeFtpError(ex)}",
                        UploadedCount = i
                    };
                }
                catch (Exception ex)
                {
                    L($"[失敗] {rel}：{ex.Message}");
                    return new FtpUploadResult
                    {
                        Success = false,
                        ErrorMessage = $"上傳失敗：{rel}\n{ex.Message}",
                        UploadedCount = i
                    };
                }
            }

            L($"完成，共 {files.Count} 個檔案上傳成功。");
            return new FtpUploadResult { Success = true, UploadedCount = files.Count };
        }

        private static string NormalizeRemoteDir(string? remoteDir) =>
            "/" + (remoteDir ?? "").Trim('/', '\\').Replace('\\', '/');

        /// <summary>
        /// 只驗證連線/帳密/遠端目錄是否可用，不搬動任何檔案。「上傳補丁」按鈕要等這個
        /// 成功過一次才能按，避免像這次一樣填錯遠端目錄（誤填本機磁碟路徑）才在批次
        /// 上傳中途才發現。
        /// </summary>
        public static bool TestConnection(
            string host, int port, string username, string password, string remoteDir,
            Action<string>? log, out string? error)
        {
            void L(string m)
            {
                try { log?.Invoke(m); } catch { }
            }

            if (string.IsNullOrWhiteSpace(host))
            {
                error = "請填寫 FTP 主機。";
                L($"[失敗] {error}");
                return false;
            }

            string baseRemoteDir = NormalizeRemoteDir(remoteDir);
            L($"開始測試連線 → ftp://{host}:{port}{baseRemoteDir}");
            try
            {
                var req = CreateRequest(host, port, username, password, baseRemoteDir, WebRequestMethods.Ftp.ListDirectory);
                using var resp = (FtpWebResponse)req.GetResponse();
                error = null;
                L("連線成功，目錄可正常存取。");
                return true;
            }
            catch (WebException ex)
            {
                error = DescribeFtpError(ex);
                L($"[失敗] 連線失敗：{error}");
                return false;
            }
            catch (Exception ex)
            {
                error = ex.Message;
                L($"[失敗] 連線失敗：{error}");
                return false;
            }
        }

        private static string? GetRemoteDirectory(string remotePath)
        {
            int idx = remotePath.LastIndexOf('/');
            if (idx <= 0) return null;
            return remotePath.Substring(0, idx);
        }

        /// <summary>
        /// FTP 沒有原生 mkdir -p，逐層建立。MKD 對「目錄已存在」跟「真的建立失敗」
        /// （權限不足、磁碟配額爆掉、路徑打錯等）很多伺服器回的都是同一個 550，
        /// 單看 MKD 的回應沒辦法可靠分辨兩者。舊寫法直接吞掉所有 WebException，
        /// 導致真正的建立失敗被無聲忽略，接著上傳檔案到根本不存在的目錄，才在
        /// STOR 階段冒出一個看起來像是單一檔案問題、其實根因在目錄的
        /// 「550 File unavailable」，讓人誤以為是某個檔案本身壞掉。
        /// 這裡改成：MKD 失敗後用 LIST 該目錄做一次「目錄是否真的存在」的驗證——
        /// 如果 LIST 也失敗，才是真正的問題，直接回傳明確錯誤訊息（含 FTP 原始
        /// 回應），不要繼續往下走到上傳階段。
        /// </summary>
        private static string? EnsureRemoteDirectoryExists(string host, int port, string username, string password, string remoteDir, Action<string>? log = null)
        {
            string[] segments = remoteDir.Trim('/').Split('/', StringSplitOptions.RemoveEmptyEntries);
            string current = "";
            foreach (string seg in segments)
            {
                current += "/" + seg;
                try
                {
                    var req = CreateRequest(host, port, username, password, current, WebRequestMethods.Ftp.MakeDirectory);
                    using var resp = (FtpWebResponse)req.GetResponse();
                    try { log?.Invoke($"  MKD {current} → 新建立"); } catch { }
                }
                catch (WebException mkdEx)
                {
                    string mkdDetail = DescribeFtpError(mkdEx);
                    // 這台伺服器（跟很多 FTP 伺服器一樣）對「目錄已存在」的 MKD 失敗，
                    // 訊息穩定包含 "already exists"——遇到這種明確語意就直接放行，省掉
                    // 一次 LIST 往返；訊息不是這種明確「已存在」的情況，才照舊送 LIST
                    // 驗證（保守判斷，沒把握就不省，避免真正的失敗被誤判成已存在）。
                    if (mkdDetail.Contains("already exists", StringComparison.OrdinalIgnoreCase))
                    {
                        try { log?.Invoke($"  MKD {current} → 已存在（{mkdDetail}），略過 LIST 驗證"); } catch { }
                        continue;
                    }

                    try
                    {
                        var listReq = CreateRequest(host, port, username, password, current, WebRequestMethods.Ftp.ListDirectory);
                        using var listResp = (FtpWebResponse)listReq.GetResponse();
                        // LIST 成功：目錄確實存在（MKD 的失敗只是「已存在」），繼續下一層。
                        try { log?.Invoke($"  MKD {current} → 已存在（{mkdDetail}），LIST 驗證通過"); } catch { }
                    }
                    catch (WebException listEx)
                    {
                        string listDetail = DescribeFtpError(listEx);
                        return $"目錄 {current} 不存在也建立不了。\nMKD：{mkdDetail}\nLIST：{listDetail}\n請確認 FTP 帳號對這個路徑有建立目錄的權限、路徑是否正確、以及伺服器空間是否足夠。";
                    }
                }
            }
            return null;
        }

        private static string DescribeFtpError(WebException ex)
        {
            if (ex.Response is FtpWebResponse ftpResp)
                return $"{(int)ftpResp.StatusCode} {ftpResp.StatusDescription?.Trim()}";
            return ex.Message;
        }

        /// <summary>
        /// 這裡每一步都打診斷 log（送出前/完成後）：實測發現獨立腳本用完全相同的邏輯
        /// 上傳完全正常（20 個檔案 0.7 秒），但這支 App 實際執行時會卡在第一個檔案、
        /// 遠端出現 0 bytes 的檔案（代表資料連線開了、但沒有真正寫完/正常關閉）。兩邊
        /// 邏輯一樣，唯一差異是執行環境（Task.Run + async/await + WPF Dispatcher），
        /// 用逐行 log 才能抓到究竟卡在 GetRequestStream／CopyTo／GetResponse 的哪一步。
        /// </summary>
        private static void UploadFile(string localFile, string host, int port, string username, string password, string remotePath, Action<string>? log)
        {
            void L(string m) { try { log?.Invoke(m); } catch { } }

            long fileLen = new FileInfo(localFile).Length;
            L($"  → STOR {remotePath} 準備中（本機檔案 {fileLen} bytes）...");

            var req = CreateRequest(host, port, username, password, remotePath, WebRequestMethods.Ftp.UploadFile);

            L($"  → STOR {remotePath} 開啟本機檔案...");
            using var fileStream = new FileStream(localFile, FileMode.Open, FileAccess.Read, FileShare.Read);

            L($"  → STOR {remotePath} 開啟資料連線（GetRequestStream）...");
            using var reqStream = req.GetRequestStream();
            L($"  → STOR {remotePath} 資料連線已開啟，開始寫入...");

            fileStream.CopyTo(reqStream);
            L($"  → STOR {remotePath} 寫入完成（{fileLen} bytes），關閉資料連線並等待伺服器回應...");

            reqStream.Close();
            using var resp = (FtpWebResponse)req.GetResponse();
            L($"  → STOR {remotePath} 伺服器回應：{(int)resp.StatusCode} {resp.StatusDescription?.Trim()}");
        }

        private static FtpWebRequest CreateRequest(string host, int port, string username, string password, string remotePath, string method)
        {
            string url = $"ftp://{host}:{port}{remotePath}";
            var req = (FtpWebRequest)WebRequest.Create(url);
            req.Method = method;
            req.Credentials = new NetworkCredential(username ?? "", password ?? "");
            req.UsePassive = true;
            req.UseBinary = true;
            // KeepAlive=true 讓 .NET 對同一台主機/同一組帳密重用底層已登入的控制連線，
            // 不用每個 MKD/LIST/STOR 都重新 TCP 交握＋FTP 登入＋PASV 協商一輪。舊寫法
            // 設 false，等於把「1446 個檔案」放大成數千次完整連線/登入，是「上傳一個
            // 檔案就幾乎不動」的根因，不是網路真的斷線或邏輯死鎖。
            req.KeepAlive = true;
            // .NET 預設 Timeout=100 秒、ReadWriteTimeout=5 分鐘：單一檔案/目錄操作卡住時，
            // 整批上傳（2000+ 檔案，每個檔案都是獨立連線）很容易看起來像「卡死沒反應」，
            // 其實是在默默等一個過長的預設逾時。改成 20 秒，卡住能更快浮現、失敗訊息
            // 也更快顯示，不用讓使用者猜是不是真的當機了。
            req.Timeout = 20000;
            req.ReadWriteTimeout = 20000;
            return req;
        }
    }
}
