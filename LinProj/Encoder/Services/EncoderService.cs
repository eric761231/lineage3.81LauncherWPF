using System;
using System.IO;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using LinEncoder.Models;

namespace LinEncoder.Services
{
    public class EncoderService
    {
        public bool CreateLauncher(LauncherConfig config, string templatePath, string outputPath)
        {
            if (!File.Exists(templatePath)) return false;
            File.Copy(templatePath, outputPath, true);
            return true;
        }

        public bool PackagePak(string input, string output)
        {
            if (!File.Exists(input)) return false;
            try
            {
                byte[] data = File.ReadAllBytes(input);
                byte[] key = Encoding.ASCII.GetBytes(Constants.FileEncryptKey);
                for (int i = 0; i < data.Length; i++)
                {
                    data[i] ^= key[i % key.Length];
                }
                File.WriteAllBytes(output, data);
                return true;
            }
            catch (Exception)
            {
                return false;
            }
        }
        public void GeneratePatch(string rel, byte[] raw, int level, string outDir, string baseUrl, List<string> lines) { }
    }
}