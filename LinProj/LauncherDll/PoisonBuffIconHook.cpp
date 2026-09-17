// PoisonBuffIconHook.cpp: PacketBox 161（S_PoisonIcon）可選 effectId。
//
// 舊包（無尾端 H）：len < 8 → 不攔截，由原生 0x52DA80 處理（固定 391/386/381）。
// 新包：C(250)+C(161)+C(type)+H+C + H(effectId)
//   effectId＝effectlist2.xml 的 <effect id>；0＝該 type 預設圖。
// 掛載點：併入 GroundTrapIconHook 的 0x544A20 分派（見 TryHandlePoisonBuffIconPacket）。
#include "stdafx.h"
#include "PoisonBuffIconHook.h"
#include "LauncherDll.h"

namespace {

constexpr BYTE kOpcodePacketBox = 250;
constexpr BYTE kSubPoisonBuffIcon = 161;

constexpr int kIconPoison = 0x187;   // 391
constexpr int kIconParalyze = 0x182; // 386
constexpr int kIconSilence = 0x17D;  // 381

typedef void(__cdecl *ApplyEffect_t)(int time, int mode, int unused0, int unused1);
ApplyEffect_t ApplyEffect = reinterpret_cast<ApplyEffect_t>(0x004EE400);

typedef void(__cdecl *ShowIcon_t)(int iconIndex, int flag);
ShowIcon_t ShowIcon = reinterpret_cast<ShowIcon_t>(0x004EE2D0);

void ClearDefaultPoisonIcons() {
  ShowIcon(kIconPoison, 0);
  ShowIcon(kIconParalyze, 0);
  ShowIcon(kIconSilence, 0);
}

void HandlePoisonBuffIcon(const BYTE *pkt, int len) {
  __try {
    const BYTE type = pkt[2];
    const short fieldH = *reinterpret_cast<const short *>(pkt + 3);
    const BYTE extra = pkt[5];
    const short effectId =
        (len >= 8) ? *reinterpret_cast<const short *>(pkt + 6) : static_cast<short>(0);

    launcherdll_hook_log(
        "[PoisonIcon] type=%d fieldH=%d extra=%d effectId=%d len=%d", (int)type,
        (int)fieldH, (int)extra, (int)effectId, len);

    if (type == 0) {
      ClearDefaultPoisonIcons();
      if (effectId != 0) {
        ShowIcon(effectId, 0);
      }
      return;
    }

    int seconds = 0;
    int defaultId = kIconPoison;
    if (type == 1) {
      seconds = fieldH;
      defaultId = kIconPoison;
    } else if (type == 2) {
      seconds = static_cast<signed char>(extra);
      defaultId = kIconParalyze;
    } else if (type == 6) {
      seconds = fieldH;
      defaultId = kIconSilence;
    } else {
      ClearDefaultPoisonIcons();
      return;
    }

    const int id = (effectId != 0) ? static_cast<int>(effectId) : defaultId;
    ClearDefaultPoisonIcons();
    // 與原生 0x52DA80 相同：ApplyEffect(time, effectId, 0, -1)
    ApplyEffect(seconds, id, 0, -1);
    launcherdll_hook_log("[PoisonIcon] ApplyEffect(time=%d, id=%d, 0, -1)",
                         seconds, id);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    launcherdll_hook_log("[PoisonIcon] handle exception, ignored");
  }
}

} // namespace

bool TryHandlePoisonBuffIconPacket(void *pkt, int len) {
  __try {
    if (!pkt || len < 8) {
      return false;
    }
    const BYTE *p = reinterpret_cast<const BYTE *>(pkt);
    if (p[0] != kOpcodePacketBox || p[1] != kSubPoisonBuffIcon) {
      return false;
    }
    HandlePoisonBuffIcon(p, len);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    launcherdll_hook_log("[PoisonIcon] pre-check exception, fall back");
    return false;
  }
}
