// VitalsPacketHook.cpp: see VitalsPacketHook.h.
//
// 背景：AutoPotionOverlay 的 BUFF 頁要畫紅/藍血魔條，資料來源本來規劃走「攔截
// 原生 S_HPUpdate/S_MPUpdate」（見 docs/AutoPotionOverlay_HPMP條後端快取計畫.md），
// 但那需要用 Cheat Engine 對正在跑的 client 下斷點才能安全定位「外層 opcode 可見」
// 的分派點——這個開發環境沒有這個條件，所以先做了一版「伺服器主動推送」的備援
// （PacketBox 借位子類型 39，見 C_PlaySupport.java 的 handleUiVisible/
// VitalsPushTimer）。
//
// 後來使用者指出 D:\天堂資料\RUST移植整理（對照過的 RUST 參考登入器整理）裡的
// src/hp_mp_patch.rs 已經逆向過真正的 S_HIT_POINT/S_MANA_POINT 封包解析位址——
// 那份文件本來是為了「伺服器已改成 32-bit HP/MP」這個情境做的擴充修補（Phase
// 1~3 把 ReadH 換成 ReadD、格式字串 h→d），我們的伺服器沒有做那個修改、還是標準
// 16-bit S_HIT_POINT/S_MANA_POINT，所以那整套 32-bit 擴充邏輯用不上；但裡面
// Phase 2 點出的「call ReadH 的位置」本身是「client 自己解析封包」的真實結構，
// 跟伺服器送的是 16-bit 還是 32-bit 無關——這才是我們要的東西。
//
// 手法：ReadH（0x5239F0）是 S_HIT_POINT/S_MANA_POINT 共用的欄位讀取函式，
// 這兩個封包各呼叫它兩次（先讀 cur 再讀 max）。4 個呼叫點都是標準 `E8 rel32`
// call 指令，直接改寫 rel32 讓它們改呼叫我們自己的小 trampoline——trampoline
// 內部照樣呼叫真正的 ReadH（原生行為完全不變），只是多把回傳值（EAX）順手記
// 下來，然後正常 ret。跟 SmoothRunPatch.cpp 的「先驗證原始 bytes、失敗就跳過
// 不硬幹」是同一套風格。
//
// 呼叫順序（同一個封包 handler 內）：curHP call 先、maxHP call 後（curMP/maxMP
// 同理）——所以 max 那個 capture 觸發時，cur 一定已經是這次封包剛解出來的新值，
// 直接在 max capture 裡一起呼叫 AutoPotionOverlay_OnHpUpdate/OnMpUpdate。
#include "stdafx.h"
#include "VitalsPacketHook.h"
#include "AutoPotionOverlay.h"
#include "LauncherDll.h"
#include <cstring>

namespace {

// S_HIT_POINT/S_MANA_POINT 共用的封包欄位讀取函式（讀 2 bytes，回傳 EAX）。
constexpr DWORD READ_H_ADDR = 0x005239F0;

// S_HIT_POINT：先讀 curHP 再讀 maxHP。
constexpr DWORD HP_CUR_CALL_ADDR = 0x00523990;
constexpr DWORD HP_MAX_CALL_ADDR = 0x005239AA;
// S_MANA_POINT：先讀 curMP 再讀 maxMP。
constexpr DWORD MP_CUR_CALL_ADDR = 0x00533800;
constexpr DWORD MP_MAX_CALL_ADDR = 0x0053381A;

constexpr size_t CAVE_SIZE = 128; // 4 個 trampoline，每個 12 bytes，留足夠空間

int g_lastCurHp = 0;
int g_lastCurMp = 0;

void PatchCode(void *addr, void *code, int len) {
  DWORD dwOldProtect;
  VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, PAGE_READWRITE, &dwOldProtect);
  memcpy(addr, code, len);
  VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, dwOldProtect, &dwOldProtect);
}

// 讀目前這個 call site 的目標位址（假設是標準 E8 rel32），格式不符回傳 0。
DWORD ReadCallTarget(DWORD callAddr) {
  BYTE buf[5];
  __try {
    memcpy(buf, (void *)(uintptr_t)callAddr, 5);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0;
  }
  if (buf[0] != 0xE8)
    return 0;
  int rel = 0;
  memcpy(&rel, buf + 1, 4);
  return (DWORD)((int)callAddr + 5 + rel);
}

void __stdcall OnCurHpCaptured(int v) { g_lastCurHp = v; }
void __stdcall OnMaxHpCaptured(int v) {
  AutoPotionOverlay_OnHpUpdate(g_lastCurHp, v);
}
void __stdcall OnCurMpCaptured(int v) { g_lastCurMp = v; }
void __stdcall OnMaxMpCaptured(int v) {
  AutoPotionOverlay_OnMpUpdate(g_lastCurMp, v);
}

// 產生單一 trampoline：call ReadH; push eax; call captureFunc(__stdcall); ret
// 回傳寫入的 byte 數（固定 12）。
int BuildTrampoline(BYTE *out, DWORD outAddr, void *captureFunc) {
  int n = 0;
  // call ReadH
  out[n++] = 0xE8;
  {
    int rel = (int)READ_H_ADDR - (int)(outAddr + n + 4);
    memcpy(out + n, &rel, 4);
    n += 4;
  }
  // push eax
  out[n++] = 0x50;
  // call captureFunc（__stdcall，被呼叫端自己清理堆疊，這裡不用再 add esp）
  out[n++] = 0xE8;
  {
    int rel = (int)(uintptr_t)captureFunc - (int)(outAddr + n + 4);
    memcpy(out + n, &rel, 4);
    n += 4;
  }
  // ret
  out[n++] = 0xC3;
  return n;
}

struct HookSite {
  const char *name;
  DWORD callAddr;
  void *captureFunc;
};

const HookSite kSites[4] = {
    {"HP_cur", HP_CUR_CALL_ADDR, (void *)OnCurHpCaptured},
    {"HP_max", HP_MAX_CALL_ADDR, (void *)OnMaxHpCaptured},
    {"MP_cur", MP_CUR_CALL_ADDR, (void *)OnCurMpCaptured},
    {"MP_max", MP_MAX_CALL_ADDR, (void *)OnMaxMpCaptured},
};

} // namespace

void InstallVitalsPacketHook() {
  BYTE *cave = (BYTE *)VirtualAlloc(NULL, CAVE_SIZE, MEM_COMMIT | MEM_RESERVE,
                                    PAGE_EXECUTE_READWRITE);
  if (!cave) {
    launcherdll_hook_log("[VitalsHook][WARN] codecave 配置失敗");
    return;
  }

  int installed = 0;
  DWORD offset = 0;
  for (const HookSite &site : kSites) {
    // 先驗證目前這個 call site 真的指向 ReadH，不符合就跳過（不要硬幹）——比照
    // SmoothRunPatch.cpp 的「先驗證原始 bytes 再動手」風格。
    DWORD curTarget = ReadCallTarget(site.callAddr);
    if (curTarget != READ_H_ADDR) {
      launcherdll_hook_log(
          "[VitalsHook][WARN] %s @0x%08X 目標不是 ReadH（現在是 0x%08X），跳過",
          site.name, (unsigned)site.callAddr, (unsigned)curTarget);
      continue;
    }

    BYTE *tramp = cave + offset;
    int len = BuildTrampoline(tramp, (DWORD)(uintptr_t)tramp, site.captureFunc);
    offset += (DWORD)len;
    if (offset > CAVE_SIZE) {
      launcherdll_hook_log("[VitalsHook][WARN] codecave 空間不足，中止於 %s",
                           site.name);
      break;
    }

    BYTE patch[5];
    patch[0] = 0xE8;
    int rel = (int)(uintptr_t)tramp - (int)(site.callAddr + 5);
    memcpy(patch + 1, &rel, 4);
    PatchCode((void *)(uintptr_t)site.callAddr, patch, 5);
    installed++;
    launcherdll_hook_log("[VitalsHook] %s @0x%08X -> trampoline 0x%p", site.name,
                         (unsigned)site.callAddr, tramp);
  }

  launcherdll_hook_log("[VitalsHook] 安裝完成：%d/4 個攔截點", installed);
}
