// SkillHasteHook.cpp: see SkillHasteHook.h.
//
// Hook 點：S_SkillHaste（opcode 149）handler（0x540D4A）內呼叫
// 0x4AEF30（套用狀態＋格式化顯示文字）那行 call 指令（0x540E34，
// `E8 F7 E0 F6 FF`，call rel32 到 0x4AEF30）。這裡不改行為，只在呼叫前後
// 各記錄一次 target 物件已知欄位，兩者一起貼出來方便比對封包內容。
//
// 反組譯已確認 handler 本身只讀 objid(D) + type(C) 共 5 bytes，沒有讀
// 伺服器已經在送的 time(H)——留意這不代表壞掉，這個引擎的封包是長度前綴
// 框架，沒讀完的尾巴下一包會自動跳過，不會造成真正的錯位（待確認）。
#include "stdafx.h"
#include "SkillHasteHook.h"
#include "LauncherDll.h"
#include "PatchUtil.h"
#include <cstring>

namespace {

BYTE *const kCallSiteAddr = reinterpret_cast<BYTE *>(0x00540E34);
// call rel32 0x4AEF30
const BYTE kCallSiteSig[5] = {0xE8, 0xF7, 0xE0, 0xF6, 0xFF};

typedef void(__thiscall *ApplyStatus_t)(void *target);
// MASM 內嵌組語不接受 `call <固定位址常數>`，改用函式指標變數間接呼叫，
// 效果跟直接呼叫該位址相同。
ApplyStatus_t const real_ApplyStatus =
    reinterpret_cast<ApplyStatus_t>(0x004AEF30);

} // namespace

extern "C" void __cdecl SkillHasteLogBefore(void *target, void *framePtr) {
  static int s_count = 0;
  if (s_count >= 60) {
    return;
  }
  s_count++;
  __try {
    const DWORD objid =
        *reinterpret_cast<DWORD *>(static_cast<BYTE *>(framePtr) - 0x5fd8);
    const BYTE type =
        *reinterpret_cast<BYTE *>(static_cast<BYTE *>(framePtr) - 0x5fd9);
    const BYTE *t = static_cast<const BYTE *>(target);
    const short f9A = *reinterpret_cast<const short *>(t + 0x9A);
    const BYTE fA4 = *reinterpret_cast<const BYTE *>(t + 0xA4);
    const WORD fB2 = *reinterpret_cast<const WORD *>(t + 0xB2);
    const DWORD fB8 = *reinterpret_cast<const DWORD *>(t + 0xB8);
    const void *fA8 = *reinterpret_cast<void *const *>(t + 0xA8);
    launcherdll_hook_log(
        "[Pss][diag] SkillHaste BEFORE n=%d objid=%u type=%d target=%p "
        "+0x9A=%d +0xA4=%d +0xB2=%u +0xB8=%u +0xA8=%p",
        s_count, objid, type, target, f9A, fA4, fB2, fB8, fA8);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    launcherdll_hook_log("[Pss][diag] SkillHaste BEFORE exception n=%d", s_count);
  }
}

extern "C" void __cdecl SkillHasteLogAfter(void *target, void *framePtr) {
  (void)framePtr;
  __try {
    const BYTE *t = static_cast<const BYTE *>(target);
    const short f9A = *reinterpret_cast<const short *>(t + 0x9A);
    const BYTE fA4 = *reinterpret_cast<const BYTE *>(t + 0xA4);
    const WORD fB2 = *reinterpret_cast<const WORD *>(t + 0xB2);
    const DWORD fB8 = *reinterpret_cast<const DWORD *>(t + 0xB8);
    const void *fA8 = *reinterpret_cast<void *const *>(t + 0xA8);
    launcherdll_hook_log(
        "[Pss][diag] SkillHaste AFTER  target=%p +0x9A=%d +0xA4=%d +0xB2=%u "
        "+0xB8=%u +0xA8=%p",
        target, f9A, fA4, fB2, fB8, fA8);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    launcherdll_hook_log("[Pss][diag] SkillHaste AFTER exception");
  }
}

__declspec(naked) void Tramp_SkillHasteApply() {
  __asm {
    push ecx          // 保存 target（thiscall 參數），供呼叫後還原
    push ebp
    push ecx
    call SkillHasteLogBefore
    add esp, 8
    mov ecx, dword ptr [esp]   // 偷看堆疊上保存的 target，不動堆疊
    call real_ApplyStatus      // 原生：套用狀態＋格式化顯示文字（不變）
    pop ecx                    // 取回先前保存的 target
    push ebp
    push ecx
    call SkillHasteLogAfter
    add esp, 8
    push 0x00540E39
    ret
  }
}

void InstallSkillHasteHook() {
  if (memcmp(kCallSiteAddr, kCallSiteSig, sizeof(kCallSiteSig)) != 0) {
    launcherdll_hook_log("[Install] SkillHaste sig mismatch, skip");
    return;
  }
  HookCode(kCallSiteAddr, reinterpret_cast<void *>(Tramp_SkillHasteApply),
           sizeof(kCallSiteSig));
  FlushInstructionCache(GetCurrentProcess(), kCallSiteAddr,
                        sizeof(kCallSiteSig));
  launcherdll_hook_log("[Install] SkillHaste ok");
}
