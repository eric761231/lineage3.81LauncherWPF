// SmoothRunPatch.cpp: see SmoothRunPatch.h.
// 查表決策走 C：怪物有 98 即切腳；人物需三段加速齊備才切腳。
#include "stdafx.h"
#include "SmoothRunPatch.h"
#include "LauncherDll.h"
#include <cstring>

namespace {

constexpr uintptr_t HOOK_ADDR = 0x00449776;
const BYTE EXPECTED_BYTES[5] = {0x8B, 0x44, 0xC2, 0x04, 0x5D};

constexpr DWORD RUNL_SLOT_OFF = 0x0314; // 98*8+4
constexpr DWORD RUNR_SLOT_OFF = 0x031C; // 99*8+4
constexpr DWORD MOVEMENT_FUNC_LO = 0x005AA000;
constexpr DWORD MOVEMENT_FUNC_HI = 0x005AAA00;

constexpr int HASH_TABLE_SIZE = 64;
constexpr BYTE HASH_TABLE_MASK = 0x3F;

// 客戶端欄位（CE／反組譯已驗證）：
//   +0x24 ← 一段 MoveSpeed（S_SkillHaste／ObjectPack）
//   +0x29 ← 二段 Brave（S_SkillBrave；Liquor 也可能暫寫 8）
//   +0x12B ← 三段（type==8 時置 1；+0x5A 是等級不是三段）
constexpr BYTE OFF_PC_FLAG = 0x27;
constexpr BYTE OFF_MOVE_SPEED = 0x24;
constexpr BYTE OFF_BRAVE_OR_LIQUOR = 0x29;
constexpr DWORD OFF_THIRD_SPEED = 0x12B; // 不可用 BYTE（會截成 0x2B）
constexpr BYTE LIQUOR_THIRD = 0x08;

constexpr uintptr_t G_HASTE_BUFF_TABLE = 0x00ABF4C8;
constexpr uintptr_t G_PLAYER_PTR = 0x00C2D2B8;
constexpr uintptr_t G_LOCAL_OBJ_ID = 0x00ABF4B4;
constexpr DWORD OFF_OBJ_ID = 0x0C;

bool IsLocalEntity(BYTE *entity) {
  __try {
    BYTE *self = *(BYTE **)G_PLAYER_PTR;
    if (self != nullptr && self == entity)
      return true;
    DWORD entId = *(DWORD *)(entity + OFF_OBJ_ID);
    DWORD selfId = *(DWORD *)G_LOCAL_OBJ_ID;
    return selfId != 0 && entId == selfId;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool BuffTableOn(int stateId) {
  __try {
    return ((BYTE *)G_HASTE_BUFF_TABLE)[stateId] != 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool ReadTripleFlags(BYTE *entity, BYTE *move, BYTE *brave, BYTE *third) {
  __try {
    *move = entity[OFF_MOVE_SPEED];
    *brave = entity[OFF_BRAVE_OR_LIQUOR];
    *third = entity[OFF_THIRD_SPEED];
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool IsPcTripleHaste(BYTE *entity) {
  BYTE moveSpeed = 0, braveOrLiquor = 0, thirdFlag = 0;
  if (!ReadTripleFlags(entity, &moveSpeed, &braveOrLiquor, &thirdFlag))
    return false;

  const bool local = IsLocalEntity(entity);
  if (local) {
    BYTE *self = nullptr;
    __try {
      self = *(BYTE **)G_PLAYER_PTR;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      self = nullptr;
    }
    if (self != nullptr) {
      BYTE sm = 0, sb = 0, st = 0;
      if (ReadTripleFlags(self, &sm, &sb, &st)) {
        moveSpeed = sm;
        braveOrLiquor = sb;
        thirdFlag = st;
      }
    }
  }

  bool hasMove = (moveSpeed == 1);
  if (!hasMove && local)
    hasMove = BuffTableOn(0);

  bool hasBrave = (braveOrLiquor != 0 && braveOrLiquor != LIQUOR_THIRD);
  if (!hasBrave && local)
    hasBrave = BuffTableOn(2);

  bool hasThird = (thirdFlag != 0) || (braveOrLiquor == LIQUOR_THIRD);
  if (!hasThird && local)
    hasThird = BuffTableOn(0x13) || BuffTableOn(73);

  return hasMove && hasBrave && hasThird;
}

#pragma pack(push, 1)
struct EntState {
  WORD entLo;
  BYTE lastFrame;
  BYTE toggle; // 0=L(98) 1=R(99)
};
#pragma pack(pop)
static EntState g_ht[HASH_TABLE_SIZE];

void PatchCode(void *addr, void *code, int len) {
  DWORD dwOldProtect;
  VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, PAGE_READWRITE, &dwOldProtect);
  memcpy(addr, code, len);
  VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, dwOldProtect, &dwOldProtect);
}

bool IsWalkAction(DWORD actionId) {
  if (actionId == 0)
    return true;
  switch (actionId) {
  case 4:
  case 11:
  case 20:
  case 24:
  case 40:
  case 46:
  case 50:
  case 54:
  case 58:
  case 62:
  case 83:
  case 88:
  case 119:
    return true;
  default:
    return false;
  }
}

BYTE *TryGetEntity(DWORD frame) {
  __try {
    DWORD savedEbp = *(DWORD *)frame;
    return *(BYTE **)(savedEbp - 0x5C);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return nullptr;
  }
}

extern "C" DWORD __stdcall SmoothRun_Decide(DWORD table, DWORD frame) {
  DWORD actionId = 0;
  DWORD retAddr = 0;
  DWORD orig = 0;
  DWORD slot98 = 0;
  DWORD slot99 = 0;

  __try {
    actionId = *(DWORD *)(frame + 0x0C);
    retAddr = *(DWORD *)(frame + 0x04);
    orig = *(DWORD *)(table + actionId * 8 + 4);
    slot98 = *(DWORD *)(table + RUNL_SLOT_OFF);
    slot99 = *(DWORD *)(table + RUNR_SLOT_OFF);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0;
  }

  if (slot98 < 0x10000u)
    return orig;
  if (!IsWalkAction(actionId))
    return orig;
  if (retAddr < MOVEMENT_FUNC_LO || retAddr > MOVEMENT_FUNC_HI)
    return orig;

  BYTE *entity = TryGetEntity(frame);
  if (!entity)
    return orig;

  bool isPc = false;
  __try {
    isPc = (entity[OFF_PC_FLAG] != 0);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return orig;
  }

  // 人物：一段＋二段＋三段皆備才切；怪物／NPC 不檢查加速。
  if (isPc && !IsPcTripleHaste(entity))
    return orig;

  BYTE animFrame = 0;
  __try {
    animFrame = entity[0x17];
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    animFrame = 0;
  }

  WORD entLo = (WORD)((uintptr_t)entity & 0xFFFF);
  int idx = ((int)((uintptr_t)entity >> 3)) & HASH_TABLE_MASK;
  EntState &st = g_ht[idx];

  if (st.entLo != entLo) {
    st.entLo = entLo;
    st.lastFrame = animFrame;
    st.toggle = (actionId == 0) ? 0 : 1;
  } else {
    if (animFrame < st.lastFrame)
      st.toggle ^= 1;
    st.lastFrame = animFrame;
  }

  if (st.toggle != 0 && slot99 >= 0x10000u)
    return slot99;
  return slot98;
}

__declspec(naked) void SmoothRun_Stub() {
  __asm {
    push ebp
    push edx
    call SmoothRun_Decide
    pop ebp
    ret
  }
}

} // namespace

void InstallSmoothRunPatch() {
  BYTE *addr = (BYTE *)HOOK_ADDR;
  if (addr[0] == 0xE9)
    return;
  if (memcmp(addr, EXPECTED_BYTES, 5) != 0)
    return;

  memset(g_ht, 0, sizeof(g_ht));

  BYTE jmp5[5];
  jmp5[0] = 0xE9;
  *(int *)&jmp5[1] =
      (int)((intptr_t)SmoothRun_Stub - (intptr_t)addr - 5);
  PatchCode(addr, jmp5, 5);

  launcherdll_hook_log("[SmoothRun][install] OK");
}
