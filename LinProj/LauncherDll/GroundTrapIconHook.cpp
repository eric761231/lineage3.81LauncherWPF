// GroundTrapIconHook.cpp: see GroundTrapIconHook.h.
//
// 封包格式（固定 10 bytes，opcode 算在內，跟 Java 端 S_GroundTrapIcon.java
// 完全對齊）：
//   opcode(C=254) objid(D) type(C) time(H) icon(H)
//
// 客戶端行為完全照抄 S_SkillHaste handler（0x52C410）反組譯結果，差別只有
// icon 一律從封包讀（不是寫死 0x17C），且這個 opcode 是我們自己新借的，
// 不需要處理「舊封包沒有這個欄位」的相容性問題——這個 opcode 從第一天就是
// 固定 10 bytes 格式，沒有「舊格式」。
#include "stdafx.h"
#include "GroundTrapIconHook.h"
#include "LauncherDll.h"
#include "detours.h"
#include <cstring>

#pragma comment(lib, "detours.lib")

namespace {

constexpr BYTE kOpcodeGroundTrapIcon = 254;
constexpr DWORD kDispatchAddr = 0x00544A20;

typedef void *(__cdecl *FindObjectById_t)(int objid);
FindObjectById_t FindObjectById = reinterpret_cast<FindObjectById_t>(0x005ADD70);

typedef void(__thiscall *VisualReset_t)(void *thisPtr, int type);
VisualReset_t VisualReset = reinterpret_cast<VisualReset_t>(0x00579E10);

typedef void(__cdecl *ApplyEffect_t)(int time, int mode, int unused0, int unused1);
ApplyEffect_t ApplyEffect = reinterpret_cast<ApplyEffect_t>(0x004EE400);

typedef void(__cdecl *ShowIcon_t)(int iconIndex, int flag);
ShowIcon_t ShowIcon = reinterpret_cast<ShowIcon_t>(0x004EE2D0);

constexpr DWORD kOwnPlayerPtrAddr = 0x00C2D2B8;
constexpr DWORD kVisualResetThis = 0x00BDC738;
constexpr DWORD kFlag645Addr = 0x009AB645;
constexpr DWORD kFlag648Addr = 0x009AB648;
constexpr DWORD kFlag644Addr = 0x009AB644;
// 2026-09-16 重大更正：這個 icon 參數其實不是任意圖案編號，是
// effectlist2.xml 的 <effect id="N" icon=".." graphic=".." .../> 的
// 「id」——ApplyEffect/ShowIcon 的索引參數會拿它去查 0xAC4CD8 那張表
// （列寬 0x3C，跟 effectlist2.xml 的 id 一一對應，count=512）。
// id=0 = HASTE_S（icon 5397，原生 type==1 用）；id=1 = SLOW_S（icon 480，
// 一直顯示的那顆就是它）；id=380 = ENTANGLE_S（icon=766, graphic=2250,
// kind="slow"）——這正是原本規劃書給的 766/2250 兩個數字的真正來源，兩個
// 都是同一列的欄位，不是任意挑的圖案編號。地面陷阱功能語意上就是「纏繞/
// 減速」，直接借用這個既有 id 最貼切，不用改 effectlist2.xml。
constexpr int kDefaultAccelId = 0;    // HASTE_S
constexpr int kDefaultDecelId = 380;  // ENTANGLE_S（icon 766 / graphic 2250）

typedef void(__cdecl *Dispatch_t)(void *pkt, int len);
Dispatch_t real_Dispatch = reinterpret_cast<Dispatch_t>(kDispatchAddr);

// 照抄 S_SkillHaste handler（0x52C410）的行為，唯一差異是 icon 一律從封包
// 讀（這個 opcode 固定 10 bytes，不像 S_SkillHaste 要判斷長度相容舊格式）。
void HandleGroundTrapIcon(const BYTE *pkt) {
  __try {
    const int objid = *reinterpret_cast<const int *>(pkt + 1);
    const BYTE type = pkt[5];
    const short time = *reinterpret_cast<const short *>(pkt + 6);
    const short icon = *reinterpret_cast<const short *>(pkt + 8);

    void *target = FindObjectById(objid);
    launcherdll_hook_log(
        "[Pss][diag] GroundTrapIcon parsed objid=%d type=%d time=%d icon=%d "
        "target=%p",
        objid, (int)type, (int)time, (int)icon, target);
    if (!target) {
      launcherdll_hook_log("[Pss][diag] GroundTrapIcon target not found, stop");
      return;
    }

    void *ownPlayer = *reinterpret_cast<void **>(kOwnPlayerPtrAddr);
    launcherdll_hook_log(
        "[Pss][diag] GroundTrapIcon ownPlayer=%p is_own=%d", ownPlayer,
        target == ownPlayer ? 1 : 0);
    if (target == ownPlayer) {
      // 原樣照抄 0x52C410 這段旗標處理，語意未知但保留跟原生一致的行為。
      const BYTE flag645 = *reinterpret_cast<BYTE *>(kFlag645Addr);
      *reinterpret_cast<DWORD *>(kFlag648Addr) = flag645 ? 1 : 0;
      if (*reinterpret_cast<BYTE *>(kFlag644Addr)) {
        *reinterpret_cast<BYTE *>(kFlag644Addr) = 0;
      }
      VisualReset(reinterpret_cast<void *>(kVisualResetThis), type);

      if (type == 1) {
        // icon 欄位＝effectlist2.xml 的 effect id；0 代表用原生預設的
        // HASTE_S（id 0）。
        const int effectId = icon != 0 ? icon : kDefaultAccelId;
        ApplyEffect(time, effectId, 0, -1);
        launcherdll_hook_log(
            "[Pss][diag] GroundTrapIcon type=1 ApplyEffect(time=%d,id=%d,0,-1) "
            "ShowIcon(%d,1)",
            (int)time, effectId, effectId);
        ShowIcon(effectId, 1);
      } else if (type == 2) {
        // 0 代表用預設的 ENTANGLE_S（id 380，icon 766／graphic 2250）。
        const int effectId = icon != 0 ? icon : kDefaultDecelId;
        ApplyEffect(time, effectId, 0, -1);
        launcherdll_hook_log(
            "[Pss][diag] GroundTrapIcon type=2 ApplyEffect(time=%d,id=%d,0,-1) "
            "ShowIcon(%d,1)",
            (int)time, effectId, effectId);
        ShowIcon(effectId, 1);
      } else {
        launcherdll_hook_log("[Pss][diag] GroundTrapIcon type=%d (clear branch)",
                             (int)type);
        ShowIcon(kDefaultAccelId, 0);
        ShowIcon(kDefaultDecelId, 0);
        ShowIcon(icon != 0 ? icon : kDefaultAccelId, 0);
      }
    }

    *(reinterpret_cast<BYTE *>(target) + 0x24) = type;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    launcherdll_hook_log("[Pss][GroundTrapIcon] handle exception, ignored");
  }
}

void __cdecl Hook_Dispatch(void *pkt, int len) {
  __try {
    if (pkt && *reinterpret_cast<BYTE *>(pkt) == kOpcodeGroundTrapIcon) {
      launcherdll_hook_log("[Pss][diag] GroundTrapIcon dispatch hit len=%d", len);
      if (len < 10) {
        launcherdll_hook_log(
            "[Pss][diag] GroundTrapIcon len<10, falling back to native dispatch");
      } else {
        HandleGroundTrapIcon(reinterpret_cast<const BYTE *>(pkt));
        return; // 原生對這個 opcode 沒有 case，不轉呼叫原生 dispatch
      }
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    launcherdll_hook_log("[Pss][GroundTrapIcon] pre-check exception, fall back to native dispatch");
  }
  real_Dispatch(pkt, len);
}

} // namespace

void InstallGroundTrapIconHook() {
  BYTE *p = reinterpret_cast<BYTE *>(kDispatchAddr);
  if (p[0] != 0x55 || p[1] != 0x8B || p[2] != 0xEC) {
    launcherdll_hook_log("[Install] GroundTrapIcon skip (prologue mismatch)");
    return;
  }

  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  DetourAttach(&(PVOID &)real_Dispatch, reinterpret_cast<PVOID>(Hook_Dispatch));
  const LONG result = DetourTransactionCommit();
  if (result != NO_ERROR) {
    launcherdll_hook_log("[Install] GroundTrapIcon fail (detour=%ld)", result);
    return;
  }
  launcherdll_hook_log("[Install] GroundTrapIcon ok");
}
