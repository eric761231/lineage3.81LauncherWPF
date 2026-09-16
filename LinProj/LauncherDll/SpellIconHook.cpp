// SpellIconHook.cpp: 點技能欄 hit-test 0x743D20。只記 packed／名稱給 PSS 對技能。
#include "stdafx.h"
#include "SpellIconHook.h"
#include "LauncherDll.h"
#include "PatchUtil.h"
#include <cstring>

namespace {

constexpr DWORD kHitAddr = 0x00743D20;
constexpr int kHookLen = 6;
const BYTE kHitOrig[kHookLen] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10};

DWORD g_lastTick = 0;
DWORD g_lastPacked = 0xFFFFFFFF;
bool g_installed = false;

int ComputeHitSlot(const BYTE *self, int a1, int a2) {
  const int *p = reinterpret_cast<const int *>(self);
  const int cellH = p[0x24 / 4];
  if (cellH == 0) {
    return -2;
  }
  const int row = (a2 - p[0x08 / 4]) / cellH;
  if (row > 8) {
    return -1;
  }
  int idx;
  if (p[0x54 / 4] == 0) {
    idx = p[0x28 / 4] + row;
  } else {
    const int cellW = p[0x20 / 4];
    if (cellW == 0) {
      return -2;
    }
    const int col = (a1 - p[0x04 / 4]) / cellW;
    if (col >= 4) {
      return -1;
    }
    idx = p[0x28 / 4] + row * 4 + col;
  }
  if (idx >= p[0x2C / 4]) {
    return -1;
  }
  return idx;
}

} // namespace

extern "C" void __cdecl SpellGridHitLog(void *self, int a1, int a2) {
  __try {
    if (!self) {
      return;
    }
    const BYTE *b = reinterpret_cast<const BYTE *>(self);
    const int slot = ComputeHitSlot(b, a1, a2);
    if (slot < 0) {
      return;
    }
    const int *pi = reinterpret_cast<const int *>(self);
    const DWORD arr = (DWORD)pi[0x58 / 4];
    if (!arr) {
      return;
    }
    const DWORD ep = *reinterpret_cast<DWORD *>(arr + (DWORD)slot * 4);
    if (!ep) {
      return;
    }
    const DWORD packed = *reinterpret_cast<const DWORD *>(ep + 4);
    const DWORD now = GetTickCount();
    if (packed == g_lastPacked && now - g_lastTick < 400) {
      return;
    }
    g_lastTick = now;
    g_lastPacked = packed;
    const char *name = *reinterpret_cast<char *const *>(ep + 0xC);
    launcherdll_hook_log("[Pss] skill slot=%d packed=%u name=%s", slot,
                         (unsigned)packed, name ? name : "");
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

__declspec(naked) void Tramp_SpellGridHit() {
  __asm {
    pushad
    push dword ptr [esp + 0x28]
    push dword ptr [esp + 0x28]
    push dword ptr [esp + 0x20]
    call SpellGridHitLog
    add esp, 12
    popad
    push ebp
    mov ebp, esp
    sub esp, 0x10
    push 0x00743D26
    ret
  }
}

void InstallSpellListParseHook() {
  if (g_installed) {
    return;
  }
  if (memcmp((void *)kHitAddr, kHitOrig, kHookLen) != 0) {
    launcherdll_hook_log("[Pss][Install] SpellGridHit 743D20 sig mismatch, skip");
    return;
  }
  HookCode((void *)kHitAddr, reinterpret_cast<void *>(Tramp_SpellGridHit),
           kHookLen);
  FlushInstructionCache(GetCurrentProcess(), (void *)kHitAddr, kHookLen);
  g_installed = true;
  launcherdll_hook_log("[Pss][Install] SpellGridHit 743D20 ok");
}
