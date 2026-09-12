// SmoothRunPatch.cpp: see SmoothRunPatch.h.
// 查表決策走 C：怪物有 98 即切腳；人物需三段加速齊備才切腳。
#include "stdafx.h"
#include "SmoothRunPatch.h"
#include "LauncherDll.h"
#include <cstring>

namespace {

// Hook 的記憶體位址與比對用原始 Code
constexpr uintptr_t HOOK_ADDR = 0x00449776;
const BYTE EXPECTED_BYTES[5] = {0x8B, 0x44, 0xC2, 0x04, 0x5D};

constexpr DWORD RUNL_SLOT_OFF = 0x0314; // 98*8+4 (左腳跑步動作 Slot)
constexpr DWORD RUNR_SLOT_OFF = 0x031C; // 99*8+4 (右腳跑步動作 Slot)
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

/**
 * @brief 判斷給定的實體指標是否為玩家本人。
 * @param entity 實體物件指標
 * @return true 代表為玩家本人，否則為 false
 */
bool IsLocalEntity(BYTE *entity) {
  __try {
    BYTE *self = *(BYTE **)G_PLAYER_PTR;
    if (self != nullptr && self == entity) {
      return true;
    }
    DWORD entId = *(DWORD *)(entity + OFF_OBJ_ID);
    DWORD selfId = *(DWORD *)G_LOCAL_OBJ_ID;
    return selfId != 0 && entId == selfId;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

/**
 * @brief 檢查全域加速 Buff 狀態表中指定 ID 的狀態是否開啟。
 * @param stateId 狀態/Buff ID
 * @return true 代表 Buff 生效中，否則為 false
 */
bool BuffTableOn(int stateId) {
  __try {
    return ((BYTE *)G_HASTE_BUFF_TABLE)[stateId] != 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

/**
 * @brief 讀取實體的三段加速 Flag 數值。
 * @param entity 實體物件指標
 * @param move 輸出一段移動速度旗標
 * @param brave 輸出二段勇敢/藥水旗標
 * @param third 輸出三段加速旗標
 * @return 讀取成功回傳 true，異常回傳 false
 */
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

/**
 * @brief 檢查玩家實體是否已同時具備三段加速效果（一段 + 二段 + 三段）。
 * @param entity 玩家實體指標
 * @return true 代表三段加速齊備，否則為 false
 */
bool IsPcTripleHaste(BYTE *entity) {
  BYTE moveSpeed = 0, braveOrLiquor = 0, thirdFlag = 0;
  if (!ReadTripleFlags(entity, &moveSpeed, &braveOrLiquor, &thirdFlag)) {
    return false;
  }

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
  if (!hasMove && local) {
    hasMove = BuffTableOn(0);
  }

  bool hasBrave = (braveOrLiquor != 0 && braveOrLiquor != LIQUOR_THIRD);
  if (!hasBrave && local) {
    hasBrave = BuffTableOn(2);
  }

  bool hasThird = (thirdFlag != 0) || (braveOrLiquor == LIQUOR_THIRD);
  if (!hasThird && local) {
    hasThird = BuffTableOn(0x13) || BuffTableOn(73);
  }

  return hasMove && hasBrave && hasThird;
}

#pragma pack(push, 1)
/**
 * @struct EntState
 * @brief 紀錄實體動畫狀態與切腳交替狀態。
 */
struct EntState {
  WORD entLo;       // 實體位址低位 word (用於雜湊比對)
  BYTE lastFrame;   // 上一 Frame 的動畫幀號
  BYTE toggle;      // 切腳開關 (0=左腳 98, 1=右腳 99)
};
#pragma pack(pop)
static EntState g_ht[HASH_TABLE_SIZE];

/**
 * @brief 修改指定記憶體位址的 Code (修正記憶體保護屬性後寫入)。
 * @param addr 目標記憶體位址
 * @param code 欲寫入的指令資料
 * @param len 資料長度
 */
void PatchCode(void *addr, void *code, int len) {
  DWORD dwOldProtect;
  VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, PAGE_READWRITE, &dwOldProtect);
  memcpy(addr, code, len);
  VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, dwOldProtect, &dwOldProtect);
}

/**
 * @brief 判斷指定動作 ID 是否為移動/行走動作。
 * @param actionId 動作 ID
 * @return true 代表為移動動作，否則為 false
 */
bool IsWalkAction(DWORD actionId) {
  if (actionId == 0) {
    return true;
  }
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

/**
 * @brief 嘗試從呼叫堆疊 Frame 中取得當前的實體物件指標。
 * @param frame EBP / 堆疊 Frame 指標
 * @return 實體物件指標，失敗回傳 nullptr
 */
BYTE *TryGetEntity(DWORD frame) {
  __try {
    DWORD savedEbp = *(DWORD *)frame;
    return *(BYTE **)(savedEbp - 0x5C);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return nullptr;
  }
}

/**
 * @brief 切腳（左右腳動作交替）決策邏輯核心函式。
 * @param table 動作 Slot 表指標
 * @param frame 呼叫堆疊 Frame
 * @return 最終決定的動作位址 (Slot 98 或 Slot 99)
 */
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

  if (slot98 < 0x10000u) {
    return orig;
  }
  if (!IsWalkAction(actionId)) {
    return orig;
  }
  if (retAddr < MOVEMENT_FUNC_LO || retAddr > MOVEMENT_FUNC_HI) {
    return orig;
  }

  BYTE *entity = TryGetEntity(frame);
  if (!entity) {
    return orig;
  }

  bool isPc = false;
  __try {
    isPc = (entity[OFF_PC_FLAG] != 0);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return orig;
  }

  // 人物：一段＋二段＋三段皆備才切；怪物／NPC 不檢查加速。
  if (isPc && !IsPcTripleHaste(entity)) {
    return orig;
  }

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
    if (animFrame < st.lastFrame) {
      st.toggle ^= 1;
    }
    st.lastFrame = animFrame;
  }

  if (st.toggle != 0 && slot99 >= 0x10000u) {
    return slot99;
  }
  return slot98;
}

/**
 * @brief SmoothRun 匯編跳板 (Stub) 函式。
 */
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

/**
 * @brief 安裝 SmoothRun 流暢跑步/切腳 Patch。
 */
void InstallSmoothRunPatch() {
  BYTE *addr = (BYTE *)HOOK_ADDR;
  if (addr[0] == 0xE9) {
    return;
  }
  if (memcmp(addr, EXPECTED_BYTES, 5) != 0) {
    return;
  }

  memset(g_ht, 0, sizeof(g_ht));

  BYTE jmp5[5];
  jmp5[0] = 0xE9;
  *(int *)&jmp5[1] =
      (int)((intptr_t)SmoothRun_Stub - (intptr_t)addr - 5);
  PatchCode(addr, jmp5, 5);

  launcherdll_hook_log("[SmoothRun][install] OK");
}
