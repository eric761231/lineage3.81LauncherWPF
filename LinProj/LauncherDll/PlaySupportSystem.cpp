// PlaySupportSystem.cpp: see PlaySupportSystem.h.
#include "stdafx.h"
#include "PlaySupportSystem.h"
#include "PssOverlay.h"
#include "PssConfig.h"
#include "LauncherDll.h"

#include <string.h>

namespace {

constexpr int kMaxScanLen = 4096; // PacketBox payload 防禦上限，配 SEH

/** 寫入 launcher.log，前置 [Pss]。 */
void PssLog(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  char body[512] = {0};
  vsprintf_s(body, fmt, args);
  va_end(args);
  launcherdll_hook_log("%s", body);
}

/** PacketBox 32：解析道具回覆。pktData 從子類型之後算。回傳 1=吃掉。 */
DWORD OnResolveDispatch(const BYTE *pktData) {
  __try {
    if (kMaxScanLen < 15) {
      return 0;
    }
    int pos = 0;
    BYTE success = pktData[pos++];
    BYTE section = pktData[pos++];
    BYTE slotIndex = pktData[pos++];
    DWORD templateItemId = 0;
    memcpy(&templateItemId, pktData + pos, sizeof(DWORD));
    pos += sizeof(DWORD);
    DWORD gfxid = 0;
    memcpy(&gfxid, pktData + pos, sizeof(DWORD));
    pos += sizeof(DWORD);
    DWORD count = 0;
    memcpy(&count, pktData + pos, sizeof(DWORD));
    pos += sizeof(DWORD);

    char nameBig5[128] = {0};
    int nameLen = 0;
    while (pos < kMaxScanLen && nameLen < (int)sizeof(nameBig5) - 1 &&
           pktData[pos] != 0) {
      nameBig5[nameLen++] = (char)pktData[pos++];
    }
    nameBig5[nameLen] = 0;

    wchar_t wideName[128] = {0};
    MultiByteToWideChar(950, 0, nameBig5, -1, wideName, _countof(wideName));

    PssLog("[Pss] resolve success=%d section=%d slotIndex=%d "
           "templateItemId=%u gfxid=%u count=%u name=%s",
           (int)success, (int)section, (int)slotIndex,
           (unsigned)templateItemId, (unsigned)gfxid, (unsigned)count, nameBig5);
    PssOverlay_OnResolveReply(success != 0, section, slotIndex,
                              (int)templateItemId, (int)gfxid, (int)count,
                              wideName);
    return 1;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    PssLog("[Pss] resolve: exception, passing through");
    return 0;
  }
}

/** PacketBox 39：HP/MP 四個 writeH（小端 WORD）。 */
DWORD OnVitalsDispatch(const BYTE *pktData) {
  __try {
    if (kMaxScanLen < 8) {
      return 0;
    }
    int pos = 0;
    WORD curHp = 0, maxHp = 0, curMp = 0, maxMp = 0;
    memcpy(&curHp, pktData + pos, sizeof(WORD));
    pos += sizeof(WORD);
    memcpy(&maxHp, pktData + pos, sizeof(WORD));
    pos += sizeof(WORD);
    memcpy(&curMp, pktData + pos, sizeof(WORD));
    pos += sizeof(WORD);
    memcpy(&maxMp, pktData + pos, sizeof(WORD));
    pos += sizeof(WORD);

    PssOverlay_OnVitalsUpdate((int)curHp, (int)maxHp, (int)curMp, (int)maxMp);
    return 1;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    PssLog("[Pss] vitals: exception, passing through");
    return 0;
  }
}

/** PacketBox 46：n + n×(section,slot,count d)，n 最大 20。 */
DWORD OnSlotCountsDispatch(const BYTE *pktData) {
  __try {
    if (kMaxScanLen < 1) {
      return 0;
    }
    int pos = 0;
    BYTE n = pktData[pos++];
    if (n > 20 || (1 + (int)n * 6) > kMaxScanLen) {
      return 0;
    }
    int sections[20];
    int slots[20];
    int counts[20];
    for (BYTE i = 0; i < n; i++) {
      sections[i] = (int)pktData[pos++];
      slots[i] = (int)pktData[pos++];
      DWORD count = 0;
      memcpy(&count, pktData + pos, sizeof(DWORD));
      pos += sizeof(DWORD);
      counts[i] = (int)count;
    }
    PssOverlay_OnSlotCountsBatch((int)n, sections, slots, counts);
    return 1;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    PssLog("[Pss] slot-counts: exception, passing through");
    return 0;
  }
}

/** PacketBox 47：listType + n + n×(itemId d, gfxid d, name Big5)。 */
DWORD OnItemFilterDispatch(const BYTE *pktData) {
  __try {
    if (kMaxScanLen < 2) {
      return 0;
    }
    int pos = 0;
    BYTE listType = pktData[pos++];
    BYTE n = pktData[pos++];
    if (n > (BYTE)kItemFilterMax) {
      return 0;
    }
    int itemIds[kItemFilterMax];
    int gfxids[kItemFilterMax];
    wchar_t names[kItemFilterMax][64];
    for (BYTE i = 0; i < n; i++) {
      if (pos + 8 > kMaxScanLen) {
        return 0;
      }
      DWORD id = 0, gfx = 0;
      memcpy(&id, pktData + pos, sizeof(DWORD));
      pos += sizeof(DWORD);
      memcpy(&gfx, pktData + pos, sizeof(DWORD));
      pos += sizeof(DWORD);
      itemIds[i] = (int)id;
      gfxids[i] = (int)gfx;
      char nameBig5[128] = {0};
      int nameLen = 0;
      while (pos < kMaxScanLen && nameLen < (int)sizeof(nameBig5) - 1 &&
             pktData[pos] != 0) {
        nameBig5[nameLen++] = (char)pktData[pos++];
      }
      if (pos < kMaxScanLen && pktData[pos] == 0) {
        pos++;
      }
      nameBig5[nameLen] = 0;
      names[i][0] = 0;
      MultiByteToWideChar(950, 0, nameBig5, -1, names[i], 64);
    }
    PssOverlay_OnItemFilterList((int)listType, (int)n, itemIds, gfxids, names);
    return 1;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0;
  }
}

} // namespace

extern "C" DWORD __cdecl PlaySupportSystem_OnPacketBox(DWORD subtype,
                                                       const BYTE *pktData) {
  if (!pktData) {
    return 0;
  }
  switch (subtype) {
  case kPssPacketBoxResolve:
    return OnResolveDispatch(pktData);
  case kPssPacketBoxVitals:
    return OnVitalsDispatch(pktData);
  case kPssPacketBoxSlotCounts:
    return OnSlotCountsDispatch(pktData);
  case kPssPacketBoxItemFilter:
    return OnItemFilterDispatch(pktData);
  case kPacketBoxKarma:
    // 原生 KARMA 包：不要吃掉。只當「人物已進世界」訊號，把 cfg 灌進 State。
    PssOverlay_OnWorldEnter();
    return 0;
  default:
    return 0;
  }
}
