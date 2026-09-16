// UnderwaterPumpHook.cpp: S_MapID 水下藍特效當陸地處理。
// 驗證：CE 0x52C0EF；EAX=1 出藍，EAX=0 跳 0x52C132 藍消失。
#include "stdafx.h"
#include "UnderwaterPumpHook.h"
#include "PatchUtil.h"
#include "PssConfig.h"
#include "LauncherDll.h"
#include <atomic>
#include <cstring>

namespace {

constexpr DWORD kHandlerAddr = 0x0052C0C0;
constexpr DWORD kHookAddr = 0x0052C0EF; // movsx eax, [ebp-5]
constexpr DWORD kResumeAddr = 0x0052C0F5; // je 0x52C132（test 已在 cave）
constexpr int kHookLen = 6; // 蓋掉 movsx+test
const BYTE kHandlerProlog[3] = {0x55, 0x8B, 0xEC};
const BYTE kHookOrig[kHookLen] = {0x0F, 0xBE, 0x45, 0xFB, 0x85, 0xC0};

constexpr DWORD kFlagAddr = 0x009AB646;
constexpr DWORD kOverlayTick = 0x004EE150;
constexpr DWORD kStartOverlay = 0x00590BA0;
constexpr DWORD kStopOverlay = 0x00590D90;
constexpr DWORD kStartDelay = 0x32;

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_lastPacketUnderwater{false};
volatile LONG g_pendingApply = 0; // 0=無 1=停藍 2=開藍
bool g_installed = false;

extern "C" void __cdecl UnderwaterPump_FilterFlag(BYTE *pUnderwater) {
  if (!pUnderwater) {
    return;
  }
  const bool uw = (*pUnderwater != 0);
  g_lastPacketUnderwater.store(uw, std::memory_order_relaxed);
  if (g_enabled.load(std::memory_order_relaxed)) {
    *pUnderwater = 0;
  }
}

typedef void(__cdecl *StartOverlayFn)(DWORD delay, DWORD tickFn, DWORD unused);
typedef void(__cdecl *StopOverlayFn)(DWORD tickFn, DWORD unused);

void ApplyStopOverlay() {
  __try {
    *(volatile BYTE *)kFlagAddr = 0;
    StopOverlayFn stop = (StopOverlayFn)kStopOverlay;
    stop(kOverlayTick, 0);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

void ApplyStartOverlay() {
  __try {
    *(volatile BYTE *)kFlagAddr = 1;
    StartOverlayFn start = (StartOverlayFn)kStartOverlay;
    start(kStartDelay, kOverlayTick, 0);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

int BuildFilterCave(DWORD caveVa, BYTE *out) {
  // lea eax, [ebp-5]
  // push eax
  // call FilterFlag
  // add esp, 4
  // movsx eax, byte [ebp-5]
  // test eax, eax
  // jmp kResumeAddr (je)
  int n = 0;
  out[n++] = 0x8D;
  out[n++] = 0x45;
  out[n++] = 0xFB;
  out[n++] = 0x50;
  out[n++] = 0xE8;
  *(int *)&out[n] =
      (int)((intptr_t)&UnderwaterPump_FilterFlag - (intptr_t)(caveVa + n + 4));
  n += 4;
  out[n++] = 0x83;
  out[n++] = 0xC4;
  out[n++] = 0x04;
  out[n++] = 0x0F;
  out[n++] = 0xBE;
  out[n++] = 0x45;
  out[n++] = 0xFB;
  out[n++] = 0x85;
  out[n++] = 0xC0;
  out[n++] = 0xE9;
  *(int *)&out[n] = (int)((intptr_t)kResumeAddr - (intptr_t)(caveVa + n + 4));
  n += 4;
  return n;
}

} // namespace

void InstallUnderwaterPumpHook() {
  if (g_installed) {
    return;
  }
  if (memcmp((void *)kHandlerAddr, kHandlerProlog, 3) != 0 ||
      memcmp((void *)kHookAddr, kHookOrig, kHookLen) != 0) {
    launcherdll_hook_log("[Install] UnderwaterPump skip (bytes)");
    return;
  }

  BYTE *cave = (BYTE *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE,
                                    PAGE_EXECUTE_READWRITE);
  if (!cave) {
    launcherdll_hook_log("[Install] UnderwaterPump skip (alloc)");
    return;
  }

  const DWORD caveVa = (DWORD)(uintptr_t)cave;
  BYTE sc[64];
  const int n = BuildFilterCave(caveVa, sc);
  memcpy(cave, sc, (size_t)n);

  BYTE patch[16];
  memset(patch, 0x90, kHookLen);
  patch[0] = 0xE9;
  *(int *)&patch[1] = (int)((intptr_t)caveVa - (intptr_t)kHookAddr - 5);
  PatchCode((void *)kHookAddr, patch, kHookLen);

  g_installed = true;
  PssConfig cfg = PssConfig_Load();
  g_enabled.store(cfg.underwaterPump, std::memory_order_relaxed);
  if (cfg.underwaterPump) {
    InterlockedExchange(&g_pendingApply, 1);
  }
  launcherdll_hook_log("[Install] UnderwaterPump ok");
}

void UnderwaterPumpHook_SetEnabled(bool enabled) {
  g_enabled.store(enabled, std::memory_order_relaxed);
  if (enabled) {
    InterlockedExchange(&g_pendingApply, 1);
  } else if (g_lastPacketUnderwater.load(std::memory_order_relaxed) ||
             *(volatile BYTE *)kFlagAddr != 0) {
    // 關抽水且仍在／曾在水下：下一幀遊戲執行緒把藍加回來。
    InterlockedExchange(&g_pendingApply, 2);
  }
}

bool UnderwaterPumpHook_IsEnabled() {
  return g_enabled.load(std::memory_order_relaxed);
}

void UnderwaterPumpHook_PumpPending() {
  const LONG want = InterlockedExchange(&g_pendingApply, 0);
  if (want == 1) {
    ApplyStopOverlay();
  } else if (want == 2) {
    ApplyStartOverlay();
  }
}
