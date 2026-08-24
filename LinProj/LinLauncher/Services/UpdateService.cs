// UpdateService.cs: 執行檔案更新檢查、MD5 比對與下載功能。
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using LinLauncher.Models;
using System.IO.Compression;

namespace LinLauncher.Services
{
    public class UpdateService
    {
        private readonly HttpClient _httpClient;

        public UpdateService()
        {
            _httpClient = new HttpClient();
            _httpClient.Timeout = TimeSpan.FromMinutes(15);
            _httpClient.DefaultRequestHeaders.UserAgent.ParseAdd("LinLauncher/1.0");
        }

        public event EventHandler<UpdateProgressEventArgs>? ProgressChanged;

        private void RaiseProgress(int overallPercent, string currentFile, int filePercent)
        {
            overallPercent = Math.Clamp(overallPercent, 0, 100);
            filePercent = Math.Clamp(filePercent, 0, 100);
            ProgressChanged?.Invoke(this, new UpdateProgressEventArgs
            {
                OverallPercent = overallPercent,
                CurrentFile = currentFile ?? "",
                FilePercent = filePercent
            });
        }

        /// <summary>
        /// 從本地檔案讀取更新列表，並提取下載網址與檔案資訊。
        /// </summary>
        public async Task<(List<UpdateInfo> files, string baseUrl)> LoadUpdateListAsync(string localPath)
        {
            var updateFiles = new List<UpdateInfo>();
            string baseUrl = "";
            if (!File.Exists(localPath)) return (updateFiles, baseUrl);

            try
            {
                string text = await File.ReadAllTextAsync(localPath, Encoding.UTF8).ConfigureAwait(false);
                if (text.Length > 0 && text[0] == '\uFEFF')
                    text = text.TrimStart('\uFEFF');

                string currentSection = "";
                int count = 0;
                var dict = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

                foreach (string line in text.Split(new[] { "\r\n", "\n", "\r" }, StringSplitOptions.None))
                {
                    string trimmed = line.Trim();
                    if (string.IsNullOrEmpty(trimmed) || trimmed.StartsWith(";")) continue;

                    if (trimmed.StartsWith("[") && trimmed.EndsWith("]"))
                    {
                        currentSection = trimmed.Substring(1, trimmed.Length - 2).Trim().ToLowerInvariant();
                        continue;
                    }

                    int eqIdx = trimmed.IndexOf('=');
                    if (eqIdx == -1) continue;

                    string key = trimmed.Substring(0, eqIdx).Trim().ToLowerInvariant();
                    string val = trimmed.Substring(eqIdx + 1).Trim();

                    if (currentSection == "main")
                    {
                        if (key == "count") int.TryParse(val, out count);
                        else if (key == "url") baseUrl = val;
                    }
                    else if (currentSection == "update")
                    {
                        dict[key] = val;
                    }
                }

                for (int i = 0; i < count; i++)
                {
                    if (dict.TryGetValue($"file_{i}", out string? file) && dict.TryGetValue($"md5_{i}", out string? md5))
                    {
                        updateFiles.Add(new UpdateInfo { Filename = file, Md5 = md5 });
                    }
                }
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"LoadUpdateListAsync: {ex.Message}");
            }

            return (updateFiles, baseUrl);
        }

        /// <summary>
        /// 比對本地檔案與遠端 MD5，篩選出需要更新的檔案列表。
        /// 根因（2026-08-24 查出）：清單裡若混進執行時才會產生、且會被其他行程鎖住
        /// 的檔案（例如 WebView2 執行時使用者資料目錄下的 CrashpadMetrics-active.pma——
        /// 這類檔案不該出現在 update.txt 裡，已經在 Encoder 端的
        /// GetPatchSourceWorkList 加上排除規則），單一檔案 MD5 計算失敗會讓整批檢查
        /// 直接丟例外中止，導致清單裡「這個問題檔案之後」的所有檔案永遠沒機會被
        /// 檢查到，每次啟動都在同一個地方失敗、看起來像更新完全跑不動。這裡改成
        /// 逐檔案吃例外、記 log 後跳過，不讓單一檔案拖垮整批檢查。
        /// </summary>
        public async Task<List<UpdateInfo>> CheckFilesAsync(List<UpdateInfo> allFiles, string baseDir, Action<string>? log = null)
        {
            var needUpdate = new List<UpdateInfo>();
            foreach (var info in allFiles)
            {
                string fullPath = Path.IsPathRooted(info.Filename) ? info.Filename : Path.Combine(baseDir, info.Filename);
                try
                {
                    if (!File.Exists(fullPath))
                    {
                        needUpdate.Add(info);
                        continue;
                    }

                    string localMd5 = await CalculateMd5Async(fullPath).ConfigureAwait(false);
                    if (!string.Equals(localMd5, info.Md5, StringComparison.OrdinalIgnoreCase))
                        needUpdate.Add(info);
                }
                catch (Exception ex)
                {
                    try { log?.Invoke($"CheckFiles: 略過 {info.Filename}（{ex.GetType().Name}: {ex.Message}）"); }
                    catch { }
                }
            }

            return needUpdate;
        }

        /// <summary>
        /// 下載更新檔（串流）、回報進度、解壓寫入；log 供除錯。
        /// </summary>
        public async Task<(bool ok, string? error, bool anyDeferred)> DownloadUpdatesAsync(
            List<UpdateInfo> needUpdate,
            string baseUrl,
            string baseDir,
            Action<string>? log = null)
        {
            void L(string m)
            {
                try
                {
                    log?.Invoke(m);
                }
                catch { }
            }

            if (needUpdate.Count == 0)
                return (true, null, false);

            baseUrl = (baseUrl ?? "").Trim();
            if (string.IsNullOrEmpty(baseUrl))
            {
                L("DownloadUpdates: baseUrl 空白（update.txt [main] url）");
                return (false, "更新清單未設定下載基底網址（[main] url）。", false);
            }

            int total = needUpdate.Count;
            int fileIndex = 0;
            bool anyDeferred = false;

            foreach (var info in needUpdate)
            {
                fileIndex++;
                string relativeUrl = info.Filename.Replace("\\", "/");
                string downloadUrl = baseUrl.TrimEnd('/') + "/" + relativeUrl + ".bin";
                string localPath = Path.IsPathRooted(info.Filename) ? info.Filename : Path.Combine(baseDir, info.Filename);
                string pendingPath = localPath + ".pending";

                string? dir = Path.GetDirectoryName(localPath);
                if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
                    Directory.CreateDirectory(dir);

                RaiseProgress(
                    (int)Math.Round((fileIndex - 1) * 100.0 / total),
                    Path.GetFileName(info.Filename) ?? info.Filename,
                    0);

                try
                {
                    if (File.Exists(pendingPath))
                    {
                        // 實測發現：正確解壓縮路徑要開新的 FileStream 寫 .pending 時，偶爾會遇到
                        // IOException「being used by another process」——這台機器上沒有殘留的
                        // LinLauncher/lineage380 行程、也沒有殘留的 .pending 檔案，最可能是防毒
                        // 軟體即時掃描剛下載/剛寫入的檔案時短暫鎖住。這種鎖通常幾十到幾百毫秒內
                        // 就會放開，原本這裡刪除失敗就直接吞掉、放行到下面開 FileStream 時撞上
                        // 同一個鎖直接失敗、一路 fallback 到注定 MD5 對不上的路徑。改成短暫重試
                        // 幾次，避免因為一次性的短暫鎖定就整個更新失敗；重試完還是失敗就維持
                        // 原本「best effort，吞掉」的語意（下面 FileMode.Create 本來就會覆蓋掉）。
                        try { await RetryOnIOExceptionAsync(() => File.Delete(pendingPath)).ConfigureAwait(false); }
                        catch (IOException) { }
                    }

                    byte[] data;
                    using (var resp = await _httpClient.GetAsync(downloadUrl, HttpCompletionOption.ResponseHeadersRead).ConfigureAwait(false))
                    {
                        if (!resp.IsSuccessStatusCode)
                        {
                            L($"DownloadUpdates: HTTP {(int)resp.StatusCode} {downloadUrl}");
                            return (false, $"下載失敗 ({(int)resp.StatusCode}): {relativeUrl}.bin", anyDeferred);
                        }

                        long? contentLen = resp.Content.Headers.ContentLength;
                        using Stream stream = await resp.Content.ReadAsStreamAsync().ConfigureAwait(false);
                        using var ms = new MemoryStream();
                        var buffer = new byte[81920];
                        long read = 0;
                        int n;
                        while ((n = await stream.ReadAsync(buffer.AsMemory(0, buffer.Length)).ConfigureAwait(false)) > 0)
                        {
                            await ms.WriteAsync(buffer.AsMemory(0, n)).ConfigureAwait(false);
                            read += n;
                            int filePct = contentLen.HasValue && contentLen.Value > 0
                                ? (int)Math.Min(99, read * 100 / contentLen.Value)
                                : 0;
                            int overall = (int)Math.Min(99,
                                Math.Round((fileIndex - 1) * 100.0 / total + filePct / (double)total));
                            RaiseProgress(overall, Path.GetFileName(info.Filename) ?? info.Filename, filePct);
                        }

                        data = ms.ToArray();
                    }

                    if (data.Length < 20)
                    {
                        L($"DownloadUpdates: 檔案過短 len={data.Length} {downloadUrl}");
                        return (false, $"下載內容異常（過短）：{relativeUrl}.bin", anyDeferred);
                    }

                    // 診斷用：之前三個 fallback 失敗時完全沒有 log，出錯了只看得到最後
                    // 一句「MD5 不符」，看不出實際下載到多少 bytes、內容跟預期差多少，
                    // 沒辦法判斷是下載本身不完整、還是伺服器內容真的有問題。這裡先記一行
                    // 「已下載」的原始資訊，下面每個 fallback 失敗也都補上實際算出來的 MD5。
                    L($"DownloadUpdates: 已下載 {downloadUrl} rawLen={data.Length} expectedMd5={info.Md5} rawMd5={ComputeMd5Hex(data)}");

                    byte[] decrypted = (byte[])data.Clone();
                    byte[] key = Encoding.ASCII.GetBytes(Constants.FileEncryptKey);
                    for (int i = 0; i < 16; i++)
                        decrypted[i + 4] ^= key[i % key.Length];

                    bool contentOk = false;
                    string? lastReason = null;

                    // 正常路徑：解密後走 zlib 解壓（對應 Encoder 端 EncoderService.BuildUpdatePackage
                    // 用的 ZLibStream 壓縮——這是使用者實際在用的打包工具，格式本來就正確對得起來；
                    // 另一個 tools/Generate-LauncherUpdatePackage.ps1 用 GZipStream 壓縮，
                    // 跟這裡不相容，是那支腳本本身的既有缺陷，不是這裡要改的方向）。
                    try
                    {
                        using var ms = new MemoryStream(decrypted, 4, decrypted.Length - 4);
                        using var zs = new ZLibStream(ms, CompressionMode.Decompress);
                        var fs = await OpenFileStreamWithRetryAsync(pendingPath, FileMode.Create, FileAccess.Write, FileShare.None).ConfigureAwait(false);
                        await zs.CopyToAsync(fs).ConfigureAwait(false);
                        // 這個 fs 是用 FileShare.None（獨占）開的，不能沿用 using var 讓它活到整個
                        // try 區塊結束才釋放——下面 VerifyPendingMd5Async 馬上要重新開同一個檔案讀取
                        // 算 MD5，寫入 handle 還沒關就去搶讀取 handle，一定會撞到「being used by
                        // another process」（不是外部程式鎖住，是自己前一個 handle 還沒放手）。寫完
                        // 立刻手動關閉，才能讓後面的驗證正常開檔。
                        await fs.DisposeAsync().ConfigureAwait(false);
                        contentOk = await VerifyPendingMd5Async(pendingPath, info.Md5).ConfigureAwait(false);
                        if (!contentOk)
                        {
                            lastReason = "zlib 解壓成功但 MD5 不符";
                            L($"DownloadUpdates: {lastReason}，實際MD5={await CalculateMd5Async(pendingPath).ConfigureAwait(false)} 解壓後長度={new FileInfo(pendingPath).Length}");
                        }
                    }
                    catch (Exception ex)
                    {
                        lastReason = $"zlib 解壓失敗：{ex.Message}";
                        L($"DownloadUpdates: {lastReason}");
                    }

                    // Fallback 1：部分來源只做了加密，未壓縮。
                    if (!contentOk)
                    {
                        try
                        {
                            await RetryOnIOExceptionAsync(() => File.WriteAllBytesAsync(pendingPath, decrypted.AsSpan(4).ToArray())).ConfigureAwait(false);
                            contentOk = await VerifyPendingMd5Async(pendingPath, info.Md5).ConfigureAwait(false);
                            if (contentOk)
                                L($"DownloadUpdates: fallback(raw decrypted payload) OK {downloadUrl}");
                            else
                            {
                                lastReason = "raw decrypted payload MD5 不符";
                                L($"DownloadUpdates: {lastReason}，實際MD5={await CalculateMd5Async(pendingPath).ConfigureAwait(false)}");
                            }
                        }
                        catch (Exception ex)
                        {
                            lastReason = $"raw decrypted payload 失敗：{ex.Message}";
                            L($"DownloadUpdates: {lastReason}");
                        }
                    }

                    // Fallback 2：極端情況直接為明文檔。
                    if (!contentOk)
                    {
                        try
                        {
                            await RetryOnIOExceptionAsync(() => File.WriteAllBytesAsync(pendingPath, data)).ConfigureAwait(false);
                            contentOk = await VerifyPendingMd5Async(pendingPath, info.Md5).ConfigureAwait(false);
                            if (contentOk)
                                L($"DownloadUpdates: fallback(raw full file) OK {downloadUrl}");
                            else
                            {
                                lastReason = "raw full file MD5 不符";
                                L($"DownloadUpdates: {lastReason}，實際MD5={await CalculateMd5Async(pendingPath).ConfigureAwait(false)}");
                            }
                        }
                        catch (Exception ex)
                        {
                            lastReason = $"raw full file 失敗：{ex.Message}";
                            L($"DownloadUpdates: {lastReason}");
                        }
                    }

                    if (!contentOk)
                    {
                        try
                        {
                            if (File.Exists(pendingPath))
                                File.Delete(pendingPath);
                        }
                        catch { }
                        return (false, $"下載失敗：{relativeUrl}\n{lastReason ?? "無法解包更新檔"}", anyDeferred);
                    }

                    // 內容已驗證正確，接著嘗試套用到正式檔名。若目標檔案被鎖住
                    // （最常見情況：正在更新登入器自己目前執行中的 Core\LinLauncher.exe/.dll），
                    // 不當成失敗，保留 .pending，交給 lineage381.exe（Proxy）下次啟動時的
                    // *.pending 掃描完成套用（見 LinLauncher.Proxy/Program.cs）。
                    if (TryPromotePending(pendingPath, localPath))
                    {
                        RaiseProgress((int)Math.Round(fileIndex * 100.0 / total), Path.GetFileName(info.Filename) ?? info.Filename, 100);
                    }
                    else
                    {
                        anyDeferred = true;
                        L($"DownloadUpdates: {relativeUrl} 內容已驗證正確但目標檔案使用中，保留為 .pending，將於下次啟動登入器時套用");
                        RaiseProgress((int)Math.Round(fileIndex * 100.0 / total), Path.GetFileName(info.Filename) ?? info.Filename, 100);
                    }
                }
                catch (Exception ex)
                {
                    L($"DownloadUpdates: {downloadUrl} — {ex.Message}");
                    try
                    {
                        if (File.Exists(pendingPath))
                            File.Delete(pendingPath);
                    }
                    catch { }

                    return (false, $"下載失敗：{relativeUrl}\n{ex.Message}", anyDeferred);
                }
            }

            RaiseProgress(100, "", 100);
            return (true, null, anyDeferred);
        }

        private async Task<bool> VerifyPendingMd5Async(string pendingPath, string expectedMd5)
        {
            if (!File.Exists(pendingPath))
                return false;

            string md5 = await CalculateMd5Async(pendingPath).ConfigureAwait(false);
            return string.Equals(md5, expectedMd5, StringComparison.OrdinalIgnoreCase);
        }

        /// <summary>
        /// 嘗試把已驗證正確的 .pending 檔案套用成正式檔名。
        /// 回傳 false 代表目標檔案目前被鎖住（例如登入器自己正在執行中的 exe/dll），
        /// 此時 .pending 會保留在原地，不視為錯誤——lineage381.exe 下次啟動時會自動掃描
        /// *.pending 並完成套用（見 LinLauncher.Proxy/Program.cs 的對應邏輯）。
        /// </summary>
        private static bool TryPromotePending(string pendingPath, string localPath)
        {
            try
            {
                if (File.Exists(localPath))
                    File.Delete(localPath);
                File.Move(pendingPath, localPath);
                return true;
            }
            catch (IOException)
            {
                return false;
            }
            catch (UnauthorizedAccessException)
            {
                return false;
            }
        }

        private static async Task<string> CalculateMd5Async(string filePath)
        {
            using var md5 = MD5.Create();
            await using var stream = new FileStream(filePath, FileMode.Open, FileAccess.Read, FileShare.Read, 4096, true);
            byte[] hash = await md5.ComputeHashAsync(stream).ConfigureAwait(false);
            return BitConverter.ToString(hash).Replace("-", "").ToLowerInvariant();
        }

        private static string ComputeMd5Hex(byte[] bytes)
        {
            using var md5 = MD5.Create();
            byte[] hash = md5.ComputeHash(bytes);
            return BitConverter.ToString(hash).Replace("-", "").ToLowerInvariant();
        }

        /// <summary>
        /// 對 .pending 檔案的動作（刪除／開檔寫入）偶爾會撞上 IOException「being used by
        /// another process」——這台機器上沒有殘留的 LinLauncher/lineage380 行程、也沒有殘留
        /// 的 .pending 檔案，最可能是防毒軟體即時掃描剛下載/剛寫入的檔案時短暫鎖住，通常
        /// 幾十到幾百毫秒內就會放開。重試幾次再放棄，最後一次還是失敗就讓例外照常往外拋
        /// （呼叫端原本的例外處理邏輯不用跟著改）。
        /// </summary>
        private static async Task RetryOnIOExceptionAsync(Action action, int maxAttempts = 4, int delayMs = 100)
        {
            for (int attempt = 1; ; attempt++)
            {
                try { action(); return; }
                catch (IOException) when (attempt < maxAttempts)
                {
                    await Task.Delay(delayMs).ConfigureAwait(false);
                }
            }
        }

        private static async Task RetryOnIOExceptionAsync(Func<Task> asyncAction, int maxAttempts = 4, int delayMs = 100)
        {
            for (int attempt = 1; ; attempt++)
            {
                try { await asyncAction().ConfigureAwait(false); return; }
                catch (IOException) when (attempt < maxAttempts)
                {
                    await Task.Delay(delayMs).ConfigureAwait(false);
                }
            }
        }

        private static async Task<FileStream> OpenFileStreamWithRetryAsync(
            string path, FileMode mode, FileAccess access, FileShare share, int maxAttempts = 4, int delayMs = 100)
        {
            for (int attempt = 1; ; attempt++)
            {
                try { return new FileStream(path, mode, access, share); }
                catch (IOException) when (attempt < maxAttempts)
                {
                    await Task.Delay(delayMs).ConfigureAwait(false);
                }
            }
        }
    }
}
