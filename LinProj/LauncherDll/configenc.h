#pragma once

// 設定資料加密：先 XOR 再對齊區塊做 AES-128。
void config_encrypt(const unsigned char* key, unsigned char* buffer, int len);

// 設定資料解密：先 AES-128 還原，再做 XOR 還原。
void config_decrypt(const unsigned char* key, unsigned char* buffer, int len);