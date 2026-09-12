#include "stdafx.h"
#include "PatchUtil.h"
#include <cstring>

bool IsCodeDecrypt() {
  __try {
    DWORD val = *(volatile DWORD *)0x0058788B;
    return val == 0x85C0B60F || val == 0x4D8D016A;
  } __except (1) {
  }
  return false;
}

void PatchCode(void *addr, const void *code, int len) {
  DWORD dwOldProtect;
  VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, PAGE_READWRITE,
                   &dwOldProtect);
  memcpy(addr, code, len);
  VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, dwOldProtect, &dwOldProtect);
}

void HookCode(void *addr, void *func, int len) {
  if (len < 5)
    return;
  DWORD dwOldProtect;
  BYTE *patch = new BYTE[len];
  memset(patch, 0x90, len);
  patch[0] = 0xE9;
  *(DWORD *)&patch[1] = (DWORD)((uintptr_t)func - (uintptr_t)addr - 5);
  VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, PAGE_READWRITE,
                   &dwOldProtect);
  memcpy(addr, patch, len);
  VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, dwOldProtect, &dwOldProtect);
  delete[] patch;
}
