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
        /// </summary>
        public async Task<List<UpdateInfo>> CheckFilesAsync(List<UpdateInfo> allFiles, string baseDir)
        {
            var needUpdate = new List<UpdateInfo>();
            foreach (var info in allFiles)
            {
                string fullPath = Path.IsPathRooted(info.Filename) ? info.Filename : Path.Combine(baseDir, info.Filename);
                if (!File.Exists(fullPath))
                {
                    needUpdate.Add(info);
                    continue;
                }

                string localMd5 = await CalculateMd5Async(fullPath).ConfigureAwait(false);
                if (!string.Equals(localMd5, info.Md5, StringComparison.OrdinalIgnoreCase))
                    needUpdate.Add(info);
            }

            return needUpdate;
        }

        /// <summary>
        /// 下載更新檔（串流）、回報進度、解壓寫入；log 供除錯。
        /// </summary>
        public async Task<(bool ok, string? error)> DownloadUpdatesAsync(
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
                return (true, null);

            baseUrl = (baseUrl ?? "").Trim();
            if (string.IsNullOrEmpty(baseUrl))
            {
                L("DownloadUpdates: baseUrl 空白（update.txt [main] url）");
                return (false, "更新清單未設定下載基底網址（[main] url）。");
            }

            int total = needUpdate.Count;
            int fileIndex = 0;

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
                        try
                        {
                            File.Delete(pendingPath);
                        }
                        catch { }
                    }

                    byte[] data;
                    using (var resp = await _httpClient.GetAsync(downloadUrl, HttpCompletionOption.ResponseHeadersRead).ConfigureAwait(false))
                    {
                        if (!resp.IsSuccessStatusCode)
                        {
                            L($"DownloadUpdates: HTTP {(int)resp.StatusCode} {downloadUrl}");
                            return (false, $"下載失敗 ({(int)resp.StatusCode}): {relativeUrl}.bin");
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
                        return (false, $"下載內容異常（過短）：{relativeUrl}.bin");
                    }

                    byte[] decrypted = (byte[])data.Clone();
                    byte[] key = Encoding.ASCII.GetBytes(Constants.FileEncryptKey);
                    for (int i = 0; i < 16; i++)
                        decrypted[i + 4] ^= key[i % key.Length];

                    bool applied = false;
                    string? lastReason = null;

                    // 正常路徑：解密後走 zlib 解壓。
                    try
                    {
                        using var ms = new MemoryStream(decrypted, 4, decrypted.Length - 4);
                        using var zs = new ZLibStream(ms, CompressionMode.Decompress);
                        using var fs = new FileStream(pendingPath, FileMode.Create, FileAccess.Write, FileShare.None);
                        await zs.CopyToAsync(fs).ConfigureAwait(false);
                        applied = await VerifyAndPromotePendingAsync(pendingPath, localPath, info.Md5).ConfigureAwait(false);
                        if (!applied)
                            lastReason = "zlib 解壓成功但 MD5 不符";
                    }
                    catch (Exception ex)
                    {
                        lastReason = $"zlib 解壓失敗：{ex.Message}";
                    }

                    // Fallback 1：部分來源只做了加密，未壓縮。
                    if (!applied)
                    {
                        try
                        {
                            await File.WriteAllBytesAsync(pendingPath, decrypted.AsSpan(4).ToArray()).ConfigureAwait(false);
                            applied = await VerifyAndPromotePendingAsync(pendingPath, localPath, info.Md5).ConfigureAwait(false);
                            if (applied)
                                L($"DownloadUpdates: fallback(raw decrypted payload) OK {downloadUrl}");
                            else
                                lastReason = "raw decrypted payload MD5 不符";
                        }
                        catch (Exception ex)
                        {
                            lastReason = $"raw decrypted payload 失敗：{ex.Message}";
                        }
                    }

                    // Fallback 2：極端情況直接為明文檔。
                    if (!applied)
                    {
                        try
                        {
                            await File.WriteAllBytesAsync(pendingPath, data).ConfigureAwait(false);
                            applied = await VerifyAndPromotePendingAsync(pendingPath, localPath, info.Md5).ConfigureAwait(false);
                            if (applied)
                                L($"DownloadUpdates: fallback(raw full file) OK {downloadUrl}");
                            else
                                lastReason = "raw full file MD5 不符";
                        }
                        catch (Exception ex)
                        {
                            lastReason = $"raw full file 失敗：{ex.Message}";
                        }
                    }

                    if (!applied)
                    {
                        try
                        {
                            if (File.Exists(pendingPath))
                                File.Delete(pendingPath);
                        }
                        catch { }
                        return (false, $"下載失敗：{relativeUrl}\n{lastReason ?? "無法解包更新檔"}");
                    }

                    RaiseProgress((int)Math.Round(fileIndex * 100.0 / total), Path.GetFileName(info.Filename) ?? info.Filename, 100);
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

                    return (false, $"下載失敗：{relativeUrl}\n{ex.Message}");
                }
            }

            RaiseProgress(100, "", 100);
            return (true, null);
        }

        private async Task<bool> VerifyAndPromotePendingAsync(string pendingPath, string localPath, string expectedMd5)
        {
            if (!File.Exists(pendingPath))
                return false;

            string md5 = await CalculateMd5Async(pendingPath).ConfigureAwait(false);
            if (!string.Equals(md5, expectedMd5, StringComparison.OrdinalIgnoreCase))
                return false;

            if (File.Exists(localPath))
                File.Delete(localPath);
            File.Move(pendingPath, localPath);
            return true;
        }

        private static async Task<string> CalculateMd5Async(string filePath)
        {
            using var md5 = MD5.Create();
            await using var stream = new FileStream(filePath, FileMode.Open, FileAccess.Read, FileShare.Read, 4096, true);
            byte[] hash = await md5.ComputeHashAsync(stream).ConfigureAwait(false);
            return BitConverter.ToString(hash).Replace("-", "").ToLowerInvariant();
        }
    }
}
