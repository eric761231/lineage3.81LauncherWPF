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
            IProgress<(int current, int total, string relativePath)>? progress)
        {
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

            string baseRemoteDir = "/" + (remoteDir ?? "").Trim('/', '\\').Replace('\\', '/');
            var madeDirs = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

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
                    string? err = EnsureRemoteDirectoryExists(host, port, username, password, remoteFileDir);
                    if (err != null)
                        return new FtpUploadResult { Success = false, ErrorMessage = $"建立遠端目錄失敗：{remoteFileDir}\n{err}", UploadedCount = i };
                }

                try
                {
                    UploadFile(fullPath, host, port, username, password, remotePath);
                }
                catch (Exception ex)
                {
                    return new FtpUploadResult
                    {
                        Success = false,
                        ErrorMessage = $"上傳失敗：{rel}\n{ex.Message}",
                        UploadedCount = i
                    };
                }
            }

            return new FtpUploadResult { Success = true, UploadedCount = files.Count };
        }

        private static string? GetRemoteDirectory(string remotePath)
        {
            int idx = remotePath.LastIndexOf('/');
            if (idx <= 0) return null;
            return remotePath.Substring(0, idx);
        }

        /// <summary>FTP 沒有原生 mkdir -p，逐層建立；目錄已存在時 FTP server 會回錯誤，一律忽略。</summary>
        private static string? EnsureRemoteDirectoryExists(string host, int port, string username, string password, string remoteDir)
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
                }
                catch (WebException)
                {
                    // 目錄已存在或其他非致命錯誤：忽略，繼續往下一層建立。
                }
            }
            return null;
        }

        private static void UploadFile(string localFile, string host, int port, string username, string password, string remotePath)
        {
            var req = CreateRequest(host, port, username, password, remotePath, WebRequestMethods.Ftp.UploadFile);
            using var fileStream = new FileStream(localFile, FileMode.Open, FileAccess.Read, FileShare.Read);
            using var reqStream = req.GetRequestStream();
            fileStream.CopyTo(reqStream);
            using var resp = (FtpWebResponse)req.GetResponse();
        }

        private static FtpWebRequest CreateRequest(string host, int port, string username, string password, string remotePath, string method)
        {
            string url = $"ftp://{host}:{port}{remotePath}";
            var req = (FtpWebRequest)WebRequest.Create(url);
            req.Method = method;
            req.Credentials = new NetworkCredential(username ?? "", password ?? "");
            req.UsePassive = true;
            req.UseBinary = true;
            req.KeepAlive = false;
            return req;
        }
    }
}
