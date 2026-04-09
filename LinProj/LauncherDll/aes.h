#pragma once
#include <windows.h>

// 提供與 C# CryptoService.cs 對峙的加密與解密函式
// (Provides encryption/decryption functions aligned with C# CryptoService.cs)

void aes_encrypt(unsigned char* key, unsigned char* buffer, int len);
void aes_decrypt(unsigned char* key, unsigned char* buffer, int len);

// 高層級組態解密 (AES + XOR)
void config_decrypt(unsigned char* key, unsigned char* buffer, int len);
void config_encrypt(unsigned char* key, unsigned char* buffer, int len);
