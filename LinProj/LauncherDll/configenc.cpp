// configenc.cpp : 設定資料（.cfg）加解密實作
//
// 演算法：先 XOR 查表處理，再以 AES-128-ECB 加密（加密）；
//         解密順序相反：先 AES 還原，再 XOR 還原。
//
// 注意：本檔案與 aes.cpp 提供相同的 config_encrypt / config_decrypt 實作，
// 為避免連結時重複符號（duplicate symbol），本檔案已從 LauncherDll.vcxproj 移除。
// 實際建置使用 aes.cpp（BCrypt 版本，不依賴 OpenSSL）。

// LauncherDll.h 提供 VMProtectBegin/VMProtectEnd 巨集定義
#include "stdafx.h"
#include "LauncherDll.h"
#include "configenc.h"

// config_encrypt: 先做 XOR、再做 AES-128-ECB 加密
// 需與 C# CryptoService.Encrypt() 順序相同
void config_encrypt(const unsigned char* key, unsigned char* buffer, int len)
{
    VMProtectBegin;
    // 調整為非 const 指標以符合 aes.h 介面（key 內容不會被修改）
    config_encrypt(const_cast<unsigned char*>(key), buffer, len);
    VMProtectEnd;
}

// config_decrypt: 先做 AES-128-ECB 解密、再做 XOR 還原
// 需與 C# CryptoService.Decrypt() 順序相同
void config_decrypt(const unsigned char* key, unsigned char* buffer, int len)
{
    VMProtectBegin;
    config_decrypt(const_cast<unsigned char*>(key), buffer, len);
    VMProtectEnd;
}