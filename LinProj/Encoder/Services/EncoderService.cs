using System;
using System.IO;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using LinLauncher.Models;

namespace LinLauncher.Services
{
    public class EncoderService
    {
        private readonly ResourcePatcherService _patcher = new ResourcePatcherService();

        public bool CreateLauncher(LauncherConfig config, string templatePath, string outputPath)
        {
            if (!File.Exists(templatePath)) return false;

            int fullSize = Marshal.SizeOf(typeof(LauncherConfig)); // 4728 bytes (Modern C# struct)
            int legacySize = 4720; // Exact size of original C++ struct
            byte[] configDataFull = new byte[fullSize];
            
            IntPtr ptr = Marshal.AllocHGlobal(fullSize);
            try
            {
                Marshal.StructureToPtr(config, ptr, false);
                Marshal.Copy(ptr, configDataFull, 0, fullSize);
            }
            finally { Marshal.FreeHGlobal(ptr); }

            byte[] buffer = File.ReadAllBytes(templatePath);
            
            // --- First Try: Legacy C++ Binary Signature Patching ---
            byte[] configDataLegacy = new byte[legacySize];
            Array.Copy(configDataFull, 0, configDataLegacy, 0, legacySize);
            
            // Encrypt legacy payload (skipping first 26 bytes header)
            byte[] legacyPayload = new byte[legacySize - 26];
            Array.Copy(configDataLegacy, 26, legacyPayload, 0, legacySize - 26);
            CryptoService.ConfigEncrypt(config.Key, legacyPayload);
            Array.Copy(legacyPayload, 0, configDataLegacy, 26, legacySize - 26);

            bool foundSignature = false;
            for (int i = 0; i <= buffer.Length - legacySize; i++)
            {
                ulong sign = BitConverter.ToUInt64(buffer, i);
                if (sign == LauncherConfig.LAUNCHER_CONFIG_SIGN)
                {
                    Array.Copy(configDataLegacy, 0, buffer, i, legacySize);
                    foundSignature = true;
                    break;
                }
            }

            if (foundSignature)
            {
                File.WriteAllBytes(outputPath, buffer);
                return true; // Successfully patched legacy C++ Launcher
            }

            // --- Second Try: Modern C# Win32 Resource Patching ---
            // Encrypt full payload
            byte[] fullPayload = new byte[fullSize - 26];
            Array.Copy(configDataFull, 26, fullPayload, 0, fullSize - 26);
            CryptoService.ConfigEncrypt(config.Key, fullPayload);
            Array.Copy(fullPayload, 0, configDataFull, 26, fullSize - 26);

            File.Copy(templatePath, outputPath, true);
            return _patcher.PatchConfig(outputPath, configDataFull);
        }

        public (uint e, uint d, uint n) GenerateRSAKey()
        {
            // C++ usage is 32-bit RSA (very small). We'll simulate this with RSACryptoServiceProvider
            // but return small primes for compatibility if needed.
            // For now, let's just generate a standard small RSA key.
            using (var rsa = new RSACryptoServiceProvider(384)) // Smallest possible for compatibility
            {
                var paras = rsa.ExportParameters(true);

                // Helper to safely convert a possibly-null byte[] to uint (pads/truncates to 4 bytes)
                static uint ToUInt32Safe(byte[]? arr)
                {
                    if (arr == null || arr.Length == 0) return 0u;
                    byte[] tmp = new byte[4];
                    // Copy up to 4 bytes from the start of arr. If arr is shorter, remaining bytes stay 0.
                    Array.Copy(arr, 0, tmp, 0, Math.Min(arr.Length, 4));
                    return BitConverter.ToUInt32(tmp, 0);
                }

                uint e = ToUInt32Safe(paras.Exponent);
                uint d = ToUInt32Safe(paras.D); // Note: This might be larger than uint if 384 bit; we take a truncated/padded value
                uint n = ToUInt32Safe(paras.Modulus);
                return (e, d, n);
            }
        }

        public bool PackagePak(string inputPath, string outputPath)
        {
            if (!File.Exists(inputPath)) return false;
            byte[] raw = File.ReadAllBytes(inputPath);
            
            using (var ms = new MemoryStream())
            {
                // Header: 4 bytes original length
                ms.Write(BitConverter.GetBytes((uint)raw.Length), 0, 4);
                
                // Header: 16 bytes random salt
                byte[] salt = new byte[16];
                new Random().NextBytes(salt);
                ms.Write(salt, 0, 16);

                // Compressed data
                using (var zs = new ZLibStream(ms, CompressionLevel.Optimal, true))
                {
                    zs.Write(raw, 0, raw.Length);
                }

                byte[] result = ms.ToArray();
                // Encrypt from offset 20 (after salt)
                byte[] payload = new byte[result.Length - 20];
                Array.Copy(result, 20, payload, 0, payload.Length);
                CryptoService.ConfigEncrypt(salt, payload);
                Array.Copy(payload, 0, result, 20, payload.Length);

                File.WriteAllBytes(outputPath, result);
                return true;
            }
        }

        public void GeneratePatch(string fileName, byte[] raw, int level, string outDir, string baseUrl, List<string> lines)
        {
            string md5 = CalculateMd5(raw);
            
            using (var ms = new MemoryStream())
            {
                ms.Write(BitConverter.GetBytes((uint)raw.Length), 0, 4);
                using (var zs = new ZLibStream(ms, (CompressionLevel)level, true))
                {
                    zs.Write(raw, 0, raw.Length);
                }

                byte[] result = ms.ToArray();
                if (result.Length >= 20)
                {
                    byte[] key = Encoding.ASCII.GetBytes(Constants.FileEncryptKey);
                    byte[] head = new byte[16];
                    Array.Copy(result, 4, head, 0, 16);
                    CryptoService.ConfigEncrypt(key, head);
                    Array.Copy(head, 0, result, 4, 16);
                }

                string outPath = Path.Combine(outDir, fileName + ".bin");
                // Path.GetDirectoryName may return null (e.g. when outPath has no directory part), guard against that.
                var outDirPath = Path.GetDirectoryName(outPath);
                if (!string.IsNullOrEmpty(outDirPath)) Directory.CreateDirectory(outDirPath);
                File.WriteAllBytes(outPath, result);
                
                lines.Add(fileName);
                lines.Add(md5);
            }
        }

        private string CalculateMd5(byte[] data)
        {
            using (var md5 = MD5.Create())
            {
                byte[] hash = md5.ComputeHash(data);
                return BitConverter.ToString(hash).Replace("-", "").ToLowerInvariant();
            }
        }
    }
}
