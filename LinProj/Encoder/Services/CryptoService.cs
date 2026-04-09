using System;
using System.Security.Cryptography;
using LinEncoder.Models;

namespace LinEncoder.Services
{
    public static class CryptoService
    {
        public static void ConfigEncrypt(byte[] key, byte[] data)
        {
            if (key == null || key.Length == 0) return;
            for (int i = 0; i < data.Length; i++)
            {
                data[i] ^= key[i % key.Length];
            }
        }
    }
}