using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using LinEncoder.Models;

namespace LinEncoder.Services
{
    public class EncoderService
    {
        private const int MinBinTotalBytes = 20; // 與 LinLauncher UpdateService 下載端一致

        /// <summary>
        /// 產生登入器 exe：範本位元組 + 附加加密後的 <see cref="LauncherConfig"/> 區塊。
        /// LinLauncher.Proxy 啟動時會從自身 exe 尾端反向搜尋 Sign 魔術數
        /// (0x12345678FEDCBAFF) 抽出這段區塊寫成 config.dat —— 舊版這裡只單純複製範本檔案，
        /// 完全沒有寫入設定，等於「產生登入器」這個核心功能沒有真的生效。
        /// </summary>
        public bool CreateLauncher(LauncherConfig config, string templatePath, string outputPath)
        {
            if (!File.Exists(templatePath)) return false;
            try
            {
                byte[] template = File.ReadAllBytes(templatePath);
                byte[] configBlock = ConfigDatWriter.BuildEncryptedFile(config);

                using (var fs = new FileStream(outputPath, FileMode.Create, FileAccess.Write))
                {
                    fs.Write(template, 0, template.Length);
                    fs.Write(configBlock, 0, configBlock.Length);
                }
                return true;
            }
            catch (Exception)
            {
                return false;
            }
        }

        /// <summary>
        /// 打包「變身檔」（ShareInfo.bdfile/usebd）。這個檔案只會被我們自己的
        /// LauncherDll 讀（GetFileBuffer() 解密後，透過 GetFileData() 那段
        /// shellcode 直接把內容塞進遊戲的變身檔資料結構，完全略過遊戲原生讀取
        /// 根目錄 TW13081901.pak 用的那個未知加密演算法——不需要、也不用去猜
        /// 那個演算法），所以檔案格式我們自己說了算：[orig_len:4 LE][key:16]
        /// [zlib 壓縮 + AES-128-ECB+XOR table 加密過的內容]，對齊
        /// L1J3.8Launcher(RUST)參考 inject.rs::load_inject_file 的格式（只是
        /// LauncherDll 端的 GetFileData 用 buffer 本身、不像 Rust 版還要 +1 跳過
        /// 'S' 前綴，所以這裡刻意不加那個前綴 byte）。加解密邏輯沿用
        /// CryptoService（跟 list.txt ServerData 用同一套 XOR table +
        /// AES-128-ECB，已經跟 C++ 的 aes.cpp 驗證過相容）。
        /// </summary>
        public bool PackagePak(string input, string output)
        {
            if (!File.Exists(input)) return false;
            try
            {
                byte[] raw = File.ReadAllBytes(input);
                int origLen = raw.Length;

                byte[] compressed;
                using (var ms = new MemoryStream())
                {
                    using (var zs = new ZLibStream(ms, CompressionLevel.Optimal, leaveOpen: true))
                        zs.Write(raw, 0, raw.Length);
                    compressed = ms.ToArray();
                }

                byte[] key = RandomNumberGenerator.GetBytes(16);
                CryptoService.ConfigEncrypt(key, compressed);

                using (var fs = new FileStream(output, FileMode.Create, FileAccess.Write))
                {
                    fs.Write(BitConverter.GetBytes(origLen), 0, 4);
                    fs.Write(key, 0, key.Length);
                    fs.Write(compressed, 0, compressed.Length);
                }
                return true;
            }
            catch (Exception)
            {
                return false;
            }
        }

        /// <summary>
        /// 產生與 LinLauncher UpdateService 下載端相容的更新資源：
        /// 寫入「輸出目錄\update.txt」與「輸出目錄\{相對路徑}.bin」（含子目錄時會建立對應資料夾）。
        /// </summary>
        public PatchPackageResult BuildUpdatePackage(
            string sourceDir,
            string outputDir,
            string baseUrl,
            int compressionLevel,
            IProgress<(int current, int total, string relativePath)>? progress)
        {
            if (string.IsNullOrWhiteSpace(sourceDir) || !Directory.Exists(sourceDir))
                return new PatchPackageResult { Success = false, ErrorMessage = "請選擇有效的來源目錄。" };
            if (string.IsNullOrWhiteSpace(outputDir))
                return new PatchPackageResult { Success = false, ErrorMessage = "請選擇輸出目錄。" };

            try
            {
                Directory.CreateDirectory(outputDir);
            }
            catch (Exception ex)
            {
                return new PatchPackageResult { Success = false, ErrorMessage = "無法建立輸出目錄：" + ex.Message };
            }

            string baseUrlTrim = (baseUrl ?? "").Trim();
            if (string.IsNullOrEmpty(baseUrlTrim))
                return new PatchPackageResult { Success = false, ErrorMessage = "請填寫下載基底網址（對應 update.txt 的 [main] url）。" };

            CompressionLevel zLevel = MapCompressionLevel(compressionLevel);
            byte[] xorKey = Encoding.ASCII.GetBytes(Constants.FileEncryptKey);

            List<string> workList;
            try
            {
                workList = GetPatchSourceWorkList(sourceDir);
            }
            catch (Exception ex)
            {
                return new PatchPackageResult { Success = false, ErrorMessage = "掃描來源目錄失敗：" + ex.Message };
            }

            if (workList.Count == 0)
                return new PatchPackageResult { Success = false, ErrorMessage = "來源目錄內沒有可封裝的檔案。" };

            string sourceRoot = Path.GetFullPath(sourceDir);
            var rows = new List<PatchFileRow>();
            var lines = new List<string>();
            int idx = 0;

            progress?.Report((0, workList.Count, ""));

            for (int n = 0; n < workList.Count; n++)
            {
                string fullPath = workList[n];
                string rel = Path.GetRelativePath(sourceRoot, fullPath);
                rel = rel.Replace('\\', '/');
                progress?.Report((n + 1, workList.Count, rel));

                byte[] raw;
                try
                {
                    raw = File.ReadAllBytes(fullPath);
                }
                catch (Exception ex)
                {
                    return new PatchPackageResult
                    {
                        Success = false,
                        ErrorMessage = $"讀取失敗：{rel}\n{ex.Message}"
                    };
                }

                string md5Hex;
                using (var md5 = MD5.Create())
                {
                    md5Hex = BitConverter.ToString(md5.ComputeHash(raw)).Replace("-", "").ToLowerInvariant();
                }

                byte[] compressed;
                try
                {
                    compressed = ZLibCompress(raw, zLevel);
                }
                catch (Exception ex)
                {
                    return new PatchPackageResult
                    {
                        Success = false,
                        ErrorMessage = $"壓縮失敗：{rel}\n{ex.Message}"
                    };
                }

                // 小檔案壓縮後可能不足 MinBinTotalBytes（解密端需要至少 16 bytes 可供 XOR）。
                // zlib 壓縮串流本身有結束標記（DEFLATE final block + Adler-32），解壓縮只會讀到
                // 真正資料結束的地方，尾端補的 0 bytes 不會被讀到，也不影響 XOR（逐 byte 對稱操作）——
                // 補 0 到最小長度即可，不用因為極小檔案就整個拒絕打包。
                int compressedLen = Math.Max(compressed.Length, MinBinTotalBytes - 4);
                int totalLen = 4 + compressedLen;

                var packet = new byte[totalLen];
                // 前 4 bytes 保留；登入器自 offset 4 做 XOR 與 ZLib 解壓
                Buffer.BlockCopy(compressed, 0, packet, 4, compressed.Length);
                for (int i = 0; i < 16; i++)
                    packet[4 + i] ^= xorKey[i % xorKey.Length];

                string relOs = rel.Replace('/', Path.DirectorySeparatorChar);
                string outPath = Path.Combine(outputDir, relOs) + ".bin";
                string? outDir = Path.GetDirectoryName(outPath);
                if (!string.IsNullOrEmpty(outDir))
                    Directory.CreateDirectory(outDir);

                try
                {
                    File.WriteAllBytes(outPath, packet);
                }
                catch (Exception ex)
                {
                    return new PatchPackageResult
                    {
                        Success = false,
                        ErrorMessage = $"寫入失敗：{outPath}\n{ex.Message}"
                    };
                }

                idx++;
                rows.Add(new PatchFileRow
                {
                    Index = idx,
                    RelativePath = rel,
                    SizeBytes = raw.Length
                });

                lines.Add($"file_{idx - 1}={rel}");
                lines.Add($"md5_{idx - 1}={md5Hex}");
            }

            string listPath = Path.Combine(outputDir, "update.txt");
            try
            {
                var sb = new StringBuilder();
                sb.AppendLine("[main]");
                sb.AppendLine("count=" + idx);
                sb.AppendLine("url=" + baseUrlTrim.TrimEnd('/'));
                sb.AppendLine();
                sb.AppendLine("[update]");
                foreach (var line in lines)
                    sb.AppendLine(line);
                File.WriteAllText(listPath, sb.ToString(), new UTF8Encoding(false));
            }
            catch (Exception ex)
            {
                return new PatchPackageResult
                {
                    Success = false,
                    ErrorMessage = "寫入 update.txt 失敗：" + ex.Message
                };
            }

            return new PatchPackageResult
            {
                Success = true,
                UpdateListPath = listPath,
                Files = rows
            };
        }

        /// <summary>與打包相同的規則列舉來源檔（供預覽清單）。</summary>
        public static List<PatchFileRow> BuildPatchFilePreview(string sourceDir)
        {
            var rows = new List<PatchFileRow>();
            if (string.IsNullOrWhiteSpace(sourceDir) || !Directory.Exists(sourceDir))
                return rows;

            List<string> paths;
            try
            {
                paths = GetPatchSourceWorkList(sourceDir);
            }
            catch
            {
                return rows;
            }

            string root = Path.GetFullPath(sourceDir);
            int i = 0;
            foreach (string fullPath in paths)
            {
                string rel = Path.GetRelativePath(root, fullPath).Replace('\\', '/');
                long len = 0;
                try
                {
                    len = new FileInfo(fullPath).Length;
                }
                catch
                {
                    /* ignore */
                }

                rows.Add(new PatchFileRow { Index = ++i, RelativePath = rel, SizeBytes = len });
            }

            return rows;
        }

        private static List<string> GetPatchSourceWorkList(string sourceDir)
        {
            string[] allFiles = Directory.GetFiles(sourceDir, "*", SearchOption.AllDirectories);
            var skipNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
            {
                "Thumbs.db", ".DS_Store"
            };

            return allFiles
                .Where(f => !skipNames.Contains(Path.GetFileName(f)))
                .Where(f => !IsRuntimeJunkPath(f))
                .OrderBy(f => f, StringComparer.OrdinalIgnoreCase)
                .ToList();
        }

        /// <summary>
        /// 排除「執行時才會產生、不該發佈給玩家」的路徑。根因案例（2026-08-24）：
        /// 如果打包來源目錄選到已經跑過 LinLauncher.exe 的 Core 目錄，
        /// LinLauncher.exe.WebView2\ 這個 WebView2 執行時使用者資料目錄（GPU 快取、
        /// Crashpad、IndexedDB、Session 等）會被 Directory.GetFiles 一併掃進去，混進
        /// update.txt 當成靜態客戶端檔案。這些檔案內容一直在變、還常常被 WebView2
        /// 子行程鎖住，登入器端算 MD5 時會直接丟例外，導致清單裡這個檔案之後的所有
        /// 檔案永遠沒機會被檢查到——玩家端會看到「每次更新都跑不完」。
        /// </summary>
        private static bool IsRuntimeJunkPath(string fullPath)
        {
            string normalized = fullPath.Replace('\\', '/');
            return normalized.IndexOf(".WebView2/", StringComparison.OrdinalIgnoreCase) >= 0
                || normalized.EndsWith(".pending", StringComparison.OrdinalIgnoreCase)
                || normalized.EndsWith("update.tmp", StringComparison.OrdinalIgnoreCase)
                || Path.GetFileName(fullPath).Equals("launcher.log", StringComparison.OrdinalIgnoreCase);
        }

        private static CompressionLevel MapCompressionLevel(int level) => level switch
        {
            1 => CompressionLevel.Fastest,
            2 => CompressionLevel.SmallestSize,
            _ => CompressionLevel.Optimal
        };

        private static byte[] ZLibCompress(byte[] raw, CompressionLevel level)
        {
            using var ms = new MemoryStream();
            using (var zs = new ZLibStream(ms, level, leaveOpen: true))
            {
                zs.Write(raw, 0, raw.Length);
            }
            return ms.ToArray();
        }
    }
}
