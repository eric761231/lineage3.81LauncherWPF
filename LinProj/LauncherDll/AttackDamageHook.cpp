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
#include "AutoPotionConfig.h"
#include "LauncherDll.h"
#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

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

std::atomic<bool> g_enabled{false};
bool g_installed = false;

struct AccumState {
  DWORD targetId;
  DWORD total;
  DWORD tick;
};
AccumState g_accum = {};

void PatchCode(void *addr, const void *code, int len) {
  DWORD oldProt = 0;
  VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, PAGE_READWRITE, &oldProt);
  memcpy(addr, code, len);
  VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, oldProt, &oldProt);
}

void BuildJmpPatch(BYTE *patch, int len, DWORD hookAddr, DWORD target) {
  memset(patch, 0x90, len);
  patch[0] = 0xE9;
  *(int *)&patch[1] = (int)((intptr_t)target - (intptr_t)hookAddr - 5);
}

typedef void(__cdecl *OverheadTextFn)(DWORD targetId, const char *text, DWORD color, int a,
                                      int b, int c);

void ShowDamageBubble(DWORD targetId, DWORD damage, DWORD total) {
  char buf[96];
  // \\fR 色碼：'0' 是暗藍看不清；'3'＝0x95FB44／0xF800 純紅（與強化 +9 同色）。
  // 括號用 '>'（偏白）區隔；當下傷害與累計都用紅。
  _snprintf_s(buf, _TRUNCATE, "\\\\fRf>( \\\\fRf3%u\\\\fRf> ) \\\\fRf3%u", (unsigned)damage,
              (unsigned)total);
  OverheadTextFn fn = (OverheadTextFn)kOverheadTextFn;
  fn(targetId, buf, kDamageColor, 1, 0, 0);
}

extern "C" void __cdecl AttackDamage_OnHit(DWORD targetId, DWORD damage) {
  if (!g_enabled.load(std::memory_order_relaxed))
    return;
  if (targetId == 0 || damage == 0)
    return;

  const DWORD now = GetTickCount();
  if (g_accum.targetId == targetId && (now - g_accum.tick) <= kAccumTimeoutMs) {
    g_accum.total += damage;
  } else {
    g_accum.targetId = targetId;
    g_accum.total = damage;
  }
  g_accum.tick = now;
  ShowDamageBubble(targetId, damage, g_accum.total);
}

// container+4 = DWORD* targets；+8 = WORD* damages（本服＝真傷）
extern "C" void __cdecl AttackDamage_OnAoeBatch(void *container, int count) {
  if (!g_enabled.load(std::memory_order_relaxed))
    return;
  if (!container || count <= 0 || count > 256)
    return;
  DWORD *targets = *(DWORD **)((BYTE *)container + 4);
  WORD *dmgs = *(WORD **)((BYTE *)container + 8);
  if (!targets || !dmgs)
    return;

  for (int i = 0; i < count; i++) {
    const DWORD tid = targets[i];
    if (!tid)
      continue;
    bool seen = false;
    for (int j = 0; j < i; j++) {
      if (targets[j] == tid) {
        seen = true;
        break;
      }
    }
    if (seen)
      continue;
    DWORD sum = 0;
    for (int j = 0; j < count; j++) {
      if (targets[j] == tid)
        sum += dmgs[j];
    }
    if (sum)
      AttackDamage_OnHit(tid, sum);
  }
}

// 共用：pushfd/pushad → 過濾攻擊者(edx) → 成功跳 .ok
// 回傳 sc 寫入長度；*out_ok / *out_skip 為相對 fixup 位置（填 int32 disp）
struct FilterFixups {
  int je_ok1;
  int jz_skip1;
  int je_ok2;
  int jne_skip2;
};

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

void PatchJcc(BYTE *sc, int relOff, int targetOff) {
  *(int *)&sc[relOff] = targetOff - (relOff + 4);
}

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

// attackerEbpOff: byte offset from ebp (as signed 8-bit for [ebp+disp8])
// containerEbpOff, countEbpOff similarly; count is word.
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

void InstallAttackDamageHook() {
  if (g_installed)
    return;

  if (memcmp((void *)kAttackAddr, kAttackOrig, kAttackLen) != 0 ||
      memcmp((void *)kAoeAddr, kAoeOrig, kAoeLen) != 0 ||
      memcmp((void *)kMagicAoeAddr, kMagicAoeOrig, kMagicAoeLen) != 0) {
    launcherdll_hook_log("[AttackDmg][WARN] site bytes mismatch, skip");
    return;
  }

  BYTE *cave = (BYTE *)VirtualAlloc(NULL, kCaveSize, MEM_COMMIT | MEM_RESERVE,
                                    PAGE_EXECUTE_READWRITE);
  if (!cave) {
    launcherdll_hook_log("[AttackDmg][WARN] VirtualAlloc failed");
    return;
  }

  const DWORD base = (DWORD)(uintptr_t)cave;
  const DWORD onHit = (DWORD)(uintptr_t)&AttackDamage_OnHit;
  const DWORD onBatch = (DWORD)(uintptr_t)&AttackDamage_OnAoeBatch;

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
    AutoPotionConfig cfg = AutoPotionConfig_Load();
    g_enabled.store(cfg.showDamage, std::memory_order_relaxed);
    launcherdll_hook_log(
        "[AttackDmg] installed attack/aoe/magic cave=%p size=%d showDamage=%d",
        cave, off, (int)cfg.showDamage);
  }
}

void AttackDamageHook_SetEnabled(bool enabled) {
  g_enabled.store(enabled, std::memory_order_relaxed);
  if (enabled)
    g_accum = {};
  launcherdll_hook_log("[AttackDmg] enabled=%d", enabled ? 1 : 0);
}

bool AttackDamageHook_IsEnabled() {
  return g_enabled.load(std::memory_order_relaxed);
}
