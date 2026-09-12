// AttackDamageHook.cpp: 普攻／單體 + 範圍技真實傷害 → 頭上紅字。
//
// 對照 RUST attack_damage_hook.rs：
//   普攻 @ 0x5295D9
//   範圍 @ 0x52A4F2 / 魔法範圍 @ 0x52A8F1（彙總 hit 容器的 H）
// 本服 S_RangeSkill：writeD(id)+writeH(SkillDamageTemp) → H 已是真傷，
// 故不裝 0x52A821（那是「H=hitFlag + 額外 D」的 fork）。
#include "stdafx.h"
#include "AttackDamageHook.h"
#include "AttackDamageFeetHook.h"
#include "PssConfig.h"
#include "LauncherDll.h"
#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

// Hook 的記憶體位址與比對用原始 Code
constexpr DWORD kAttackAddr = 0x005295D9;
constexpr DWORD kAttackFall = 0x005295E3;
constexpr DWORD kAttackSkip = 0x00529BCD;
constexpr int kAttackLen = 10;
const BYTE kAttackOrig[kAttackLen] = {0x83, 0x7D, 0xE0, 0x00, 0x0F, 0x8E, 0xEA, 0x05, 0x00, 0x00};

constexpr DWORD kAoeAddr = 0x0052A4F2;
constexpr DWORD kAoeFall = 0x0052A4F9;
constexpr int kAoeLen = 7;
const BYTE kAoeOrig[kAoeLen] = {0x83, 0x3D, 0xB8, 0xD2, 0xC2, 0x00, 0x00};

constexpr DWORD kMagicAoeAddr = 0x0052A8F1;
constexpr DWORD kMagicAoeFall = 0x0052A8F8;
constexpr int kMagicAoeLen = 7;
const BYTE kMagicAoeOrig[kMagicAoeLen] = {0x83, 0x3D, 0xB8, 0xD2, 0xC2, 0x00, 0x00};

constexpr DWORD kSelfCharIdAddr = 0x00ABF4B4;
constexpr DWORD kLocalPlayerPtrAddr = 0x00C2D2B8;
constexpr DWORD kOverheadTextFn = 0x0042B7B0;
constexpr DWORD kDamageColor = 0x0000F800;
constexpr DWORD kAccumTimeoutMs = 8000;
constexpr size_t kCaveSize = 0x400;

// 全域狀態變數
std::atomic<bool> g_enabled{false};
bool g_installed = false;

/**
 * @struct AccumState
 * @brief 傷害累計狀態結構，用於記錄特定目標在超時時間內的傷害加總。
 */
struct AccumState {
  DWORD targetId; // 目標角色/怪物 ID
  DWORD total;    // 累計總傷害量
  DWORD tick;     // 上次受到傷害的時間戳記（毫秒）
};
AccumState g_accum = {};

/**
 * @brief 修改指定記憶體位址的 Code (修正記憶體保護屬性後寫入)。
 * @param addr 目標記憶體位址
 * @param code 欲寫入的指令資料
 * @param len 資料長度
 */
void PatchCode(void *addr, const void *code, int len) {
  DWORD oldProt = 0;
  VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, PAGE_READWRITE, &oldProt);
  memcpy(addr, code, len);
  VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, oldProt, &oldProt);
}

/**
 * @brief 建構 JMP 轉址指令（0xE9 + 相對位址）。
 * @param patch 存放修補指令的緩衝區
 * @param len 緩衝區長度（會先以 0x90 NOP 填滿）
 * @param hookAddr Hook 起始位址
 * @param target 轉址目標位址
 */
void BuildJmpPatch(BYTE *patch, int len, DWORD hookAddr, DWORD target) {
  memset(patch, 0x90, len);
  patch[0] = 0xE9;
  *(int *)&patch[1] = (int)((intptr_t)target - (intptr_t)hookAddr - 5);
}

// 頭頂浮動文字函式指標型態定義
typedef void(__cdecl *OverheadTextFn)(DWORD targetId, const char *text, DWORD color, int a,
                                      int b, int c);

/**
 * @brief 在目標頭頂顯示傷害泡泡文字（當前傷害與累計傷害）。
 * @param targetId 目標 ID
 * @param damage 當次傷害值
 * @param total 累計傷害值
 */
void ShowDamageBubble(DWORD targetId, DWORD damage, DWORD total) {
  char buf[96];
  // \\fR 色碼：'0' 是暗藍看不清；'3'＝0x95FB44／0xF800 純批紅（與強化 +9 同色）。
  // 括號用 '>'（偏白）區隔；當下傷害與累計都用紅。
  _snprintf_s(buf, _TRUNCATE, "\\\\fRf>( \\\\fRf3%u\\\\fRf> ) \\\\fRf3%u", (unsigned)damage,
              (unsigned)total);
  OverheadTextFn fn = (OverheadTextFn)kOverheadTextFn;
  fn(targetId, buf, kDamageColor, 1, 0, 0);
}

/**
 * @brief 命中傷害觸發回呼函式（單體／普攻）。
 * @param targetId 受擊目標 ID
 * @param damage 傷害數值
 */
extern "C" void __cdecl AttackDamage_OnHit(DWORD targetId, DWORD damage) {
  if (!g_enabled.load(std::memory_order_relaxed)) {
    return;
  }
  if (targetId == 0 || damage == 0) {
    return;
  }

  // 計算並累加傷害
  const DWORD now = GetTickCount();
  if (g_accum.targetId == targetId && (now - g_accum.tick) <= kAccumTimeoutMs) {
    g_accum.total += damage;
  } else {
    g_accum.targetId = targetId;
    g_accum.total = damage;
  }
  g_accum.tick = now;
  // 顯示傷害浮動泡泡
  ShowDamageBubble(targetId, damage, g_accum.total);
}

/**
 * @brief 範圍傷害（AOE / 魔法 AOE）批次處理回呼函式。
 * @param container 包含目標與傷害陣列的容器指標 (container+4: targets, container+8: damages)
 * @param count 命中目標數量
 */
extern "C" void __cdecl AttackDamage_OnAoeBatch(void *container, int count) {
  if (!g_enabled.load(std::memory_order_relaxed)) {
    return;
  }
  if (!container || count <= 0 || count > 256) {
    return;
  }
  DWORD *targets = *(DWORD **)((BYTE *)container + 4);
  WORD *dmgs = *(WORD **)((BYTE *)container + 8);
  if (!targets || !dmgs) {
    return;
  }

  // 走訪目標列表並合併相同目標的傷害金額
  for (int i = 0; i < count; i++) {
    const DWORD tid = targets[i];
    if (!tid) {
      continue;
    }
    bool seen = false;
    for (int j = 0; j < i; j++) {
      if (targets[j] == tid) {
        seen = true;
        break;
      }
    }
    if (seen) {
      continue;
    }
    DWORD sum = 0;
    for (int j = 0; j < count; j++) {
      if (targets[j] == tid) {
        sum += dmgs[j];
      }
    }
    if (sum) {
      AttackDamage_OnHit(tid, sum);
    }
  }
}

/**
 * @struct FilterFixups
 * @brief 記錄 EmitAttackerFilter 所產生的 JCC 轉址修補位址。
 */
struct FilterFixups {
  int je_ok1;
  int jz_skip1;
  int je_ok2;
  int jne_skip2;
};

/**
 * @brief 寫入過濾攻擊者（驗證 edx 是否為玩家本人或寵物/召喚獸）的機器碼。
 * @param sc 機器碼緩衝區
 * @param n 當前寫入偏移量
 * @param fx 記錄修補位址的結構指標
 * @return 更新後的緩衝區偏移量
 */
int EmitAttackerFilter(BYTE *sc, int n, FilterFixups *fx) {
  // cmp edx,[self]; je .ok
  sc[n++] = 0x3B;
  sc[n++] = 0x15;
  *(DWORD *)&sc[n] = kSelfCharIdAddr;
  n += 4;
  sc[n++] = 0x0F;
  sc[n++] = 0x84;
  fx->je_ok1 = n;
  n += 4;
  // mov eax,[local]; test; jz .skip
  sc[n++] = 0xA1;
  *(DWORD *)&sc[n] = kLocalPlayerPtrAddr;
  n += 4;
  sc[n++] = 0x85;
  sc[n++] = 0xC0;
  sc[n++] = 0x0F;
  sc[n++] = 0x84;
  fx->jz_skip1 = n;
  n += 4;
  // cmp edx,[eax+0xC]; je .ok
  sc[n++] = 0x3B;
  sc[n++] = 0x50;
  sc[n++] = 0x0C;
  sc[n++] = 0x0F;
  sc[n++] = 0x84;
  fx->je_ok2 = n;
  n += 4;
  // cmp edx,[eax+0x14]; jne .skip
  sc[n++] = 0x3B;
  sc[n++] = 0x50;
  sc[n++] = 0x14;
  sc[n++] = 0x0F;
  sc[n++] = 0x85;
  fx->jne_skip2 = n;
  n += 4;
  return n;
}

/**
 * @brief 修補條件跳轉指令（Jcc）的相對偏移量。
 * @param sc 機器碼緩衝區
 * @param relOff Jcc 指令相對偏移欄位的位址
 * @param targetOff 跳轉目標位址偏移量
 */
void PatchJcc(BYTE *sc, int relOff, int targetOff) {
  *(int *)&sc[relOff] = targetOff - (relOff + 4);
}

/**
 * @brief 建造普攻 Code Cave 機器碼。
 * @param caveAddr Cave 的基礎虛擬位址
 * @param sc 緩衝區指標
 * @param onHit 回呼函式位址
 * @return 產生的機器碼總位元組長度
 */
int BuildAttackCave(DWORD caveAddr, BYTE *sc, DWORD onHit) {
  int n = 0;
  sc[n++] = 0x9C;
  sc[n++] = 0x60;
  // mov edx,[ebp-0x14]
  sc[n++] = 0x8B;
  sc[n++] = 0x55;
  sc[n++] = 0xEC;
  FilterFixups fx = {};
  n = EmitAttackerFilter(sc, n, &fx);
  int ok = n;
  PatchJcc(sc, fx.je_ok1, ok);
  PatchJcc(sc, fx.je_ok2, ok);
  // mov ecx,[ebp-0x10]; movzx edx,word[ebp-0x34]
  sc[n++] = 0x8B;
  sc[n++] = 0x4D;
  sc[n++] = 0xF0;
  sc[n++] = 0x0F;
  sc[n++] = 0xB7;
  sc[n++] = 0x55;
  sc[n++] = 0xCC;
  sc[n++] = 0x85;
  sc[n++] = 0xC9;
  sc[n++] = 0x0F;
  sc[n++] = 0x84;
  int jz3 = n;
  n += 4;
  sc[n++] = 0x85;
  sc[n++] = 0xD2;
  sc[n++] = 0x0F;
  sc[n++] = 0x84;
  int jz4 = n;
  n += 4;
  sc[n++] = 0x52;
  sc[n++] = 0x51;
  sc[n++] = 0xB8;
  *(DWORD *)&sc[n] = onHit;
  n += 4;
  sc[n++] = 0xFF;
  sc[n++] = 0xD0;
  sc[n++] = 0x83;
  sc[n++] = 0xC4;
  sc[n++] = 0x08;
  int skip = n;
  PatchJcc(sc, fx.jz_skip1, skip);
  PatchJcc(sc, fx.jne_skip2, skip);
  PatchJcc(sc, jz3, skip);
  PatchJcc(sc, jz4, skip);
  sc[n++] = 0x61;
  sc[n++] = 0x9D;
  sc[n++] = 0x83;
  sc[n++] = 0x7D;
  sc[n++] = 0xE0;
  sc[n++] = 0x00;
  sc[n++] = 0x0F;
  sc[n++] = 0x8E;
  *(int *)&sc[n] = (int)((intptr_t)kAttackSkip - (intptr_t)(caveAddr + n + 4));
  n += 4;
  sc[n++] = 0xE9;
  *(int *)&sc[n] = (int)((intptr_t)kAttackFall - (intptr_t)(caveAddr + n + 4));
  n += 4;
  return n;
}

/**
 * @brief 建造範圍傷害（AOE / 魔法 AOE）Code Cave 機器碼。
 * @param caveAddr Cave 的基礎虛擬位址
 * @param sc 緩衝區指標
 * @param onBatch 批次回呼函式位址
 * @param attackerDisp 攻擊者相對於 ebp 的偏移量
 * @param containerDisp 容器指標相對於 ebp 的偏移量
 * @param countDisp 命中數量相對於 ebp 的偏移量
 * @param fallthroughHook 原程式執行流寫回位址
 * @return 產生的機器碼總位元組長度
 */
int BuildAoeCave(DWORD caveAddr, BYTE *sc, DWORD onBatch, BYTE attackerDisp,
                 BYTE containerDisp, BYTE countDisp, DWORD fallthrough) {
  int n = 0;
  sc[n++] = 0x9C;
  sc[n++] = 0x60;
  // mov edx,[ebp+attackerDisp]
  sc[n++] = 0x8B;
  sc[n++] = 0x55;
  sc[n++] = attackerDisp;
  FilterFixups fx = {};
  n = EmitAttackerFilter(sc, n, &fx);
  int ok = n;
  PatchJcc(sc, fx.je_ok1, ok);
  PatchJcc(sc, fx.je_ok2, ok);
  // mov esi,[ebp+container]; movzx eax,word[ebp+count]
  sc[n++] = 0x8B;
  sc[n++] = 0x75;
  sc[n++] = containerDisp;
  sc[n++] = 0x0F;
  sc[n++] = 0xB7;
  sc[n++] = 0x45;
  sc[n++] = countDisp;
  sc[n++] = 0x85;
  sc[n++] = 0xF6;
  sc[n++] = 0x0F;
  sc[n++] = 0x84;
  int jz_c = n;
  n += 4;
  sc[n++] = 0x85;
  sc[n++] = 0xC0;
  sc[n++] = 0x0F;
  sc[n++] = 0x84;
  int jz_n = n;
  n += 4;
  sc[n++] = 0x50; // push count
  sc[n++] = 0x56; // push container
  sc[n++] = 0xB8;
  *(DWORD *)&sc[n] = onBatch;
  n += 4;
  sc[n++] = 0xFF;
  sc[n++] = 0xD0;
  sc[n++] = 0x83;
  sc[n++] = 0xC4;
  sc[n++] = 0x08;
  int skip = n;
  PatchJcc(sc, fx.jz_skip1, skip);
  PatchJcc(sc, fx.jne_skip2, skip);
  PatchJcc(sc, jz_c, skip);
  PatchJcc(sc, jz_n, skip);
  sc[n++] = 0x61;
  sc[n++] = 0x9D;
  // original cmp [0xC2D2B8],0
  memcpy(sc + n, kAoeOrig, kAoeLen);
  n += kAoeLen;
  sc[n++] = 0xE9;
  *(int *)&sc[n] = (int)((intptr_t)fallthrough - (intptr_t)(caveAddr + n + 4));
  n += 4;
  return n;
}

} // namespace

/**
 * @brief 安裝攻擊傷害顯示的 Hook（包括普攻、物理 AOE 與魔法 AOE）。
 */
void InstallAttackDamageHook() {
  if (g_installed) {
    return;
  }

  // 比對位址特徵碼，確保未經預期修改
  if (memcmp((void *)kAttackAddr, kAttackOrig, kAttackLen) != 0 ||
      memcmp((void *)kAoeAddr, kAoeOrig, kAoeLen) != 0 ||
      memcmp((void *)kMagicAoeAddr, kMagicAoeOrig, kMagicAoeLen) != 0) {
    launcherdll_hook_log("[Install] AttackDmg skip");
    return;
  }

  // 配置動態 Cave 記憶體
  BYTE *cave = (BYTE *)VirtualAlloc(NULL, kCaveSize, MEM_COMMIT | MEM_RESERVE,
                                    PAGE_EXECUTE_READWRITE);
  if (!cave) {
    launcherdll_hook_log("[Install] AttackDmg skip");
    return;
  }

  const DWORD base = (DWORD)(uintptr_t)cave;
  const DWORD onHit = (DWORD)(uintptr_t)&AttackDamage_OnHit;
  const DWORD onBatch = (DWORD)(uintptr_t)&AttackDamage_OnAoeBatch;

  // 構建普攻、物理 AOE 與魔法 AOE 的 Cave 機器碼
  int off = 0;
  int attackOff = off;
  off += BuildAttackCave(base + attackOff, cave + attackOff, onHit);
  int aoeOff = off;
  // aoe: attacker [ebp-0x10]=0xF0, container [ebp-0x1C]=0xE4, count [ebp-0x20]=0xE0
  off += BuildAoeCave(base + aoeOff, cave + aoeOff, onBatch, 0xF0, 0xE4, 0xE0, kAoeFall);
  int magicOff = off;
  // magic: attacker [ebp-0x1C]=0xE4, container [ebp-0x44]=0xBC, count [ebp-0x4C]=0xB4
  off +=
      BuildAoeCave(base + magicOff, cave + magicOff, onBatch, 0xE4, 0xBC, 0xB4, kMagicAoeFall);

  // 對原 Code 進行修補，寫入 JMP 轉位指令至 Cave
  BYTE patch[16];
  BuildJmpPatch(patch, kAttackLen, kAttackAddr, base + attackOff);
  PatchCode((void *)kAttackAddr, patch, kAttackLen);
  BuildJmpPatch(patch, kAoeLen, kAoeAddr, base + aoeOff);
  PatchCode((void *)kAoeAddr, patch, kAoeLen);
  BuildJmpPatch(patch, kMagicAoeLen, kMagicAoeAddr, base + magicOff);
  PatchCode((void *)kMagicAoeAddr, patch, kMagicAoeLen);

  InstallAttackDamageFeetHook();

  g_installed = true;
  // 從本機 cfg 還原開關（不開 Overlay 也生效）
  {
    PssConfig cfg = PssConfig_Load();
    g_enabled.store(cfg.showDamage, std::memory_order_relaxed);
    launcherdll_hook_log("[Install] AttackDmg ok");
  }
}

/**
 * @brief 設定是否啟用傷害頭頂泡泡顯示。
 * @param enabled true 為啟用，false 為停用
 */
void AttackDamageHook_SetEnabled(bool enabled) {
  g_enabled.store(enabled, std::memory_order_relaxed);
  if (enabled) {
    g_accum = {};
  }
}

/**
 * @brief 取得當前傷害頭頂泡泡顯示是否已啟用。
 * @return true 代表已啟用，false 代表未啟用
 */
bool AttackDamageHook_IsEnabled() {
  return g_enabled.load(std::memory_order_relaxed);
}
