// ServerService.cs: 負責從遠端載入並解析伺服器列表。
using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Http;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using LinLauncher.Models;

namespace LinLauncher.Services
{
    public class ServerService
    {
        private readonly HttpClient _httpClient = new HttpClient();

        public async Task<List<ServerInfo>> LoadServerListAsync(string urlOrPath)
        {
            var servers = new List<ServerInfo>();
            try
            {
                string content;
                if (TryReadLocalListFile(urlOrPath, out string? localPath))
                    content = await File.ReadAllTextAsync(localPath!);
                else
                    content = await _httpClient.GetStringAsync(urlOrPath);
                content = content.TrimStart('\uFEFF');
                using (var reader = new StringReader(content))
                {
                    bool inList = false;
                    string? line;
                    while ((line = reader.ReadLine()) != null)
                    {
                        line = line.Trim();
                        if (string.IsNullOrEmpty(line)) continue;

                        if (line.StartsWith("[") && line.EndsWith("]"))
                        {
                            inList = line.Equals("[list]", StringComparison.OrdinalIgnoreCase);
                            continue;
                        }

                        if (!inList) continue;

                        int eqIdx = line.IndexOf('=');
                        if (eqIdx == -1) continue;

                        string key = line.Substring(0, eqIdx);
                        string b64 = line.Substring(eqIdx + 1);

                             if (key.StartsWith("ServerData", StringComparison.OrdinalIgnoreCase))
                             {
                                 try
                                 {
                                     byte[] encryptedData = Convert.FromBase64String(b64);
                                     byte[] listKey = Encoding.ASCII.GetBytes(Constants.ServerListKey);
                                     if (listKey.Length != 16)
                                     {
                                         System.Diagnostics.Debug.WriteLine("ServerListKey 必須為 16 位元組（ASCII）。");
                                         continue;
                                     }
                                     if (encryptedData.Length % 16 != 0)
                                     {
                                         System.Diagnostics.Debug.WriteLine($"ServerData 長度非 16 的倍數: {encryptedData.Length}");
                                         continue;
                                     }

                                     CryptoService.ConfigDecrypt(listKey, encryptedData);

                                     int nativeSize = Marshal.SizeOf<ServerListEntryNative>();
                                     if (encryptedData.Length < nativeSize)
                                     {
                                         System.Diagnostics.Debug.WriteLine($"解密後資料過短: {encryptedData.Length} < {nativeSize}");
                                         continue;
                                     }

                                     byte[] raw = new byte[nativeSize];
                                     Array.Copy(encryptedData, raw, nativeSize);
                                     ServerListEntryNative native = ByteToStruct<ServerListEntryNative>(raw);
                                     ServerInfo info = ServerInfo.FromNative(native);
                                     if (!string.IsNullOrEmpty(info.Name))
                                     {
                                         servers.Add(info);
                                         System.Diagnostics.Debug.WriteLine($"Loaded server: {info.Name}");
                                     }
                                 }
                                 catch (Exception ex)
                                 {
                                     System.Diagnostics.Debug.WriteLine($"Error parsing server line: {line}. {ex.Message}");
                                 }
                             }
                    }
                }
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"Error loading server list from {urlOrPath}: {ex.Message}");
                if (ex.InnerException != null) System.Diagnostics.Debug.WriteLine($"Inner: {ex.InnerException.Message}");
            }
            return servers;
        }

        private static bool TryReadLocalListFile(string urlOrPath, out string? path)
        {
            path = null;
            if (string.IsNullOrWhiteSpace(urlOrPath)) return false;
            string t = urlOrPath.Trim();
            if (File.Exists(t))
            {
                path = t;
                return true;
            }
            if (Uri.TryCreate(t, UriKind.Absolute, out var u) && u.IsFile)
            {
                try
                {
                    string local = u.LocalPath;
                    if (File.Exists(local))
                    {
                        path = local;
                        return true;
                    }
                }
                catch { }
            }
            return false;
        }

        private static T ByteToStruct<T>(byte[] bytes) where T : struct
        {
            int size = Marshal.SizeOf<T>();
            if (bytes.Length < size)
                throw new ArgumentException($"byte[] 長度 {bytes.Length} 小於 {typeof(T).Name} ({size})");
            IntPtr ptr = Marshal.AllocHGlobal(size);
            try
            {
                Marshal.Copy(bytes, 0, ptr, size);
                return Marshal.PtrToStructure<T>(ptr);
            }
            finally
            {
                Marshal.FreeHGlobal(ptr);
            }
        }
    }
}
