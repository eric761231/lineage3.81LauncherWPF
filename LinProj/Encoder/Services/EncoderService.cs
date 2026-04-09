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

        public bool PackagePak(string input, string output) => true;
        public void GeneratePatch(string rel, byte[] raw, int level, string outDir, string baseUrl, List<string> lines) { }
    }
}