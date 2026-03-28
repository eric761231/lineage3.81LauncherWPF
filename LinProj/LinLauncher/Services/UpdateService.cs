// UpdateService.cs: 執行檔案更新檢查、MD5 比對與下載功能。
using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using LinLauncher.Models;

namespace LinLauncher.Services
{
    public class UpdateService
    {
        private readonly HttpClient _httpClient = new HttpClient();
        public event EventHandler<UpdateProgressEventArgs>? ProgressChanged;

        /// <summary>
        /// 從本地檔案讀取更新列表 (通常是下載後的臨時檔案)。
        /// (Loads update list from a local file, usually a downloaded temp file.)
        /// </summary>
        public async Task<List<UpdateInfo>> LoadUpdateListAsync(string localPath)
        {
            var updateFiles = new List<UpdateInfo>();
            if (!File.Exists(localPath)) return updateFiles;

            try
            {
                var lines = await File.ReadAllLinesAsync(localPath, Encoding.Default);
                string currentSection = "";
                int count = 0;
                var dict = new Dictionary<string, string>();

                foreach (var line in lines)
                {
                    string trimmed = line.Trim();
                    if (string.IsNullOrEmpty(trimmed) || trimmed.StartsWith(";")) continue;

                    if (trimmed.StartsWith("[") && trimmed.EndsWith("]"))
                    {
                        currentSection = trimmed.Substring(1, trimmed.Length - 2).ToLower();
                        continue;
                    }

                    int eqIdx = trimmed.IndexOf('=');
                    if (eqIdx == -1) continue;

                    string key = trimmed.Substring(0, eqIdx).Trim().ToLower();
                    string val = trimmed.Substring(eqIdx + 1).Trim();

                    if (currentSection == "main" && key == "count")
                    {
                        int.TryParse(val, out count);
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
                Console.WriteLine($"Error loading update list: {ex.Message}");
            }
            return updateFiles;
        }

        /// <summary>
        /// 比對本地檔案與遠端 MD5，篩選出需要更新的檔案列表。
        /// (Compares local files with remote MD5s to find files needing updates.)
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
                string localMd5 = await CalculateMd5Async(fullPath);
                if (!string.Equals(localMd5, info.Md5, StringComparison.OrdinalIgnoreCase))
                {
                    needUpdate.Add(info);
                }
            }
            return needUpdate;
        }

        /// <summary>
        /// 執行資源下載進度回報與資料流寫入
        /// (Downloads updates with progress reporting and stream writing.)
        /// </summary>
        public async Task<bool> DownloadUpdatesAsync(List<UpdateInfo> needUpdate, string baseUrl, string baseDir)
        {
            if (needUpdate.Count == 0) return true;
            int completedFiles = 0;
            foreach (var info in needUpdate)
            {
                string relativeUrl = info.Filename.Replace("\\", "/");
                string downloadUrl = baseUrl.TrimEnd('/') + "/" + relativeUrl;
                string localPath = Path.IsPathRooted(info.Filename) ? info.Filename : Path.Combine(baseDir, info.Filename);

                string? dir = Path.GetDirectoryName(localPath);
                if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir)) Directory.CreateDirectory(dir);

                try
                {
                    using (var response = await _httpClient.GetAsync(downloadUrl, HttpCompletionOption.ResponseHeadersRead))
                    {
                        response.EnsureSuccessStatusCode();
                        long? totalBytes = response.Content.Headers.ContentLength;
                        using (var contentStream = await response.Content.ReadAsStreamAsync())
                        using (var fileStream = new FileStream(localPath, FileMode.Create, FileAccess.Write, FileShare.None, 8192, true))
                        {
                            var buffer = new byte[8192];
                            long totalRead = 0;
                            int read;
                            while ((read = await contentStream.ReadAsync(buffer, 0, buffer.Length)) > 0)
                            {
                                await fileStream.WriteAsync(buffer, 0, read);
                                totalRead += read;
                                if (totalBytes.HasValue)
                                {
                                    int filePercent = (int)((totalRead * 100) / totalBytes.Value);
                                    int overallPercent = (int)(((completedFiles * 100) + filePercent) / needUpdate.Count);
                                    ProgressChanged?.Invoke(this, new UpdateProgressEventArgs 
                                    { 
                                        OverallPercent = overallPercent, 
                                        CurrentFile = info.Filename,
                                        FilePercent = filePercent 
                                    });
                                }
                            }
                        }
                    }
                }
                catch { return false; }
                completedFiles++;
            }
            return true;
        }

        private async Task<string> CalculateMd5Async(string filePath)
        {
            using (var md5 = MD5.Create())
            using (var stream = new FileStream(filePath, FileMode.Open, FileAccess.Read, FileShare.Read, 4096, true))
            {
                byte[] hash = await md5.ComputeHashAsync(stream);
                return BitConverter.ToString(hash).Replace("-", "").ToLowerInvariant();
            }
        }
    }
}
