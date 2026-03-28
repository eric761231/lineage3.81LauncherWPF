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

        public async Task<List<ServerInfo>> LoadServerListAsync(string url)
        {
            var servers = new List<ServerInfo>();
            try
            {
                string content = await _httpClient.GetStringAsync(url);
                using (var reader = new StringReader(content))
                {
                    bool inList = false;
                    string line;
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

                             if (key.StartsWith("ServerData"))
                             {
                                 try {
                                     byte[] encryptedData = Convert.FromBase64String(b64);
                                     byte[] listKey = Encoding.ASCII.GetBytes(Constants.ServerListKey);
                                     CryptoService.ConfigDecrypt(listKey, encryptedData);

                                     ServerInfo info = ByteToStruct<ServerInfo>(encryptedData);
                                     if (info != null && !string.IsNullOrEmpty(info.Name))
                                     {
                                         servers.Add(info);
                                         System.Diagnostics.Debug.WriteLine($"Loaded server: {info.Name}");
                                     }
                                 } catch (Exception ex) {
                                     System.Diagnostics.Debug.WriteLine($"Error parsing server line: {line}. {ex.Message}");
                                 }
                             }
                    }
                }
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"Error loading server list from {url}: {ex.Message}");
                if (ex.InnerException != null) System.Diagnostics.Debug.WriteLine($"Inner: {ex.InnerException.Message}");
            }
            return servers;
        }

        private T ByteToStruct<T>(byte[] bytes) where T : class
        {
            int size = Marshal.SizeOf(typeof(T));
            IntPtr ptr = Marshal.AllocHGlobal(size);
            try
            {
                int copySize = Math.Min(size, bytes.Length);
                Marshal.Copy(bytes, 0, ptr, copySize);
                return (T)Marshal.PtrToStructure(ptr, typeof(T))!;
            }
            finally
            {
                Marshal.FreeHGlobal(ptr);
            }
        }
    }
}
