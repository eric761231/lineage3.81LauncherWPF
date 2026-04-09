#pragma once

void encdec_init();
void encdec_init_key(unsigned char* key);
void encdec_encrypt(const unsigned char* src, unsigned char* dst, int len);
void encdec_decrypt(const unsigned char* src, unsigned char* dst, int len);