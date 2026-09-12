// Equipment slot expansion (14->31). Ported from Rust src/equip_ui.rs.
#include "stdafx.h"
#include "EquipUiPatch.h"
#include "PatchUtil.h"
#include "LauncherDll.h"
#include <cstring>

namespace EquipUiPatch {

static void EquipUiLog(const char *, ...) {}

constexpr uintptr_t SCAN_START_ADDR = 0x00790000;
constexpr uintptr_t SCAN_END_ADDR = 0x007A0000;
constexpr uintptr_t SURF_BOUNDS_CHECK = 0x004387DB;

static const BYTE EQUIP_LOOKUP_TABLE[32] = {
    0,  2,  5,  4,  6,  11, 7,  9,  14, 16, 3,  8,  1,  0,  0,  0,
    0,  0,  10, 12, 13, 15, 17, 18, 19, 16, 46, 47, 48, 49, 50, 51,
};

static BYTE *FindPattern(BYTE *start, BYTE *end, const int *pattern, int patLen) {
  for (BYTE *p = start; p + patLen <= end; ++p) {
    bool match = true;
    for (int i = 0; i < patLen; ++i) {
      if (pattern[i] != -1 && p[i] != (BYTE)pattern[i]) {
        match = false;
        break;
      }
    }
    if (match)
      return p;
  }
  return nullptr;
}

static BYTE *FindFuncEntryBackward(BYTE *from, size_t maxBack) {
  BYTE *start = from - maxBack;
  for (BYTE *p = from - 3; p >= start; --p) {
    if (p[0] == 0x55 && p[1] == 0x8B && p[2] == 0xEC)
      return p;
  }
  return nullptr;
}

static void PatchServerIndexToUiSlot() {
  static const int AOB[] = {0x83, 0xE9, 0x01, 0x89, 0x4D, 0xF4, 0x83, 0x7D,
                            0xF4, 0x15, 0x0F, 0x87, -1,   -1,   -1,   -1,
                            0x8B, 0x55, 0xF4, 0xFF, 0x24, 0x95};
  BYTE *hit = FindPattern((BYTE *)SCAN_START_ADDR, (BYTE *)SCAN_END_ADDR, AOB, 22);
  if (!hit) {
    EquipUiLog("[EquipUI][WARN] Patch A: ServerIndex_to_UISlot AOB not found, skipping");
    return;
  }
  BYTE *funcEntry = FindFuncEntryBackward(hit, 0x30);
  if (!funcEntry) {
    EquipUiLog("[EquipUI][WARN] Patch A: function entry not found, skipping");
    return;
  }
  if (funcEntry[0] == 0xE9) {
    EquipUiLog("[EquipUI] Patch A: already hooked, skipping");
    return;
  }

  BYTE *cave = (BYTE *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE,
                                    PAGE_EXECUTE_READWRITE);
  if (!cave) {
    EquipUiLog("[EquipUI][WARN] Patch A: codecave allocation failed");
    return;
  }
  BYTE *tableAddr = cave + 32;

  BYTE sc[32];
  int i = 0;
  sc[i++] = 0x55;
  sc[i++] = 0x8B;
  sc[i++] = 0xEC;
  sc[i++] = 0x8B;
  sc[i++] = 0x45;
  sc[i++] = 0x08;
  sc[i++] = 0x83;
  sc[i++] = 0xF8;
  sc[i++] = 0x1F;
  sc[i++] = 0x77;
  sc[i++] = 0x0F;
  sc[i++] = 0x85;
  sc[i++] = 0xC0;
  sc[i++] = 0x74;
  sc[i++] = 0x0B;
  sc[i++] = 0x0F;
  sc[i++] = 0xB6;
  sc[i++] = 0x80;
  *(DWORD *)&sc[i] = (DWORD)(uintptr_t)tableAddr;
  i += 4;
  sc[i++] = 0x5D;
  sc[i++] = 0xC2;
  sc[i++] = 0x04;
  sc[i++] = 0x00;
  sc[i++] = 0x31;
  sc[i++] = 0xC0;
  sc[i++] = 0x5D;
  sc[i++] = 0xC2;
  sc[i++] = 0x04;
  sc[i++] = 0x00;
  if (i != 32) {
    EquipUiLog("[EquipUI][WARN] Patch A: codecave length mismatch (%d != 32), aborting", i);
    VirtualFree(cave, 0, MEM_RELEASE);
    return;
  }
  memcpy(cave + 32, EQUIP_LOOKUP_TABLE, 32);
  PatchCode(cave, sc, 32);

  BYTE jmp5[5];
  jmp5[0] = 0xE9;
  *(int *)&jmp5[1] = (int)((intptr_t)cave - (intptr_t)funcEntry - 5);
  PatchCode(funcEntry, jmp5, 5);

  EquipUiLog("[EquipUI] Patch A OK: ServerIndex_to_UISlot @0x%p -> codecave 0x%p",
             funcEntry, cave);
}

static void PatchSetupSlotsHooks() {
  static const int AOB[] = {0xC7, 0x45, 0xF8, 0x01, 0x00, 0x00, 0x00, 0xEB, 0x09,
                            0x8B, 0x4D, 0xF8, 0x83, 0xC1, 0x01, 0x89, 0x4D, 0xF8,
                            0x83, 0x7D, 0xF8, -1};
  BYTE *hit = FindPattern((BYTE *)SCAN_START_ADDR, (BYTE *)SCAN_END_ADDR, AOB, 22);
  if (!hit) {
    EquipUiLog("[EquipUI][WARN] Patch B: SetupSlots AOB not found, skipping");
    return;
  }

  BYTE *exitAddr = hit + 0x2E;
  BYTE *callAddr = hit + 0x27;
  BYTE *bgCalcAddr = hit + 0xBB8;

  if (exitAddr[0] == 0xE9) {
    EquipUiLog("[EquipUI] Patch B: already hooked, skipping");
    return;
  }
  static const BYTE expectedExit[5] = {0x8B, 0xE5, 0x5D, 0xC3, 0xCC};
  if (memcmp(exitAddr, expectedExit, 5) != 0) {
    EquipUiLog("[EquipUI][WARN] Patch B1: exit bytes mismatch @0x%p, skipping",
               exitAddr);
    return;
  }
  if (callAddr[0] != 0xE8) {
    EquipUiLog("[EquipUI][WARN] Patch B: call instruction mismatch (0x%02X), skipping",
               callAddr[0]);
    return;
  }
  int rel32 = *(int *)&callAddr[1];
  BYTE *helperAddr = callAddr + 5 + rel32;

  static const BYTE expectedBg[7] = {0x8B, 0x4D, 0x0C, 0x83, 0xC1, 0x1A, 0x51};
  if (memcmp(bgCalcAddr, expectedBg, 7) != 0) {
    EquipUiLog("[EquipUI][WARN] Patch B2: bg bytes mismatch @0x%p, skipping",
               bgCalcAddr);
    return;
  }

  BYTE *cave = (BYTE *)VirtualAlloc(NULL, 128, MEM_COMMIT | MEM_RESERVE,
                                    PAGE_EXECUTE_READWRITE);
  if (!cave) {
    EquipUiLog("[EquipUI][WARN] Patch B: codecave allocation failed");
    return;
  }
  BYTE *caveB1 = cave;
  BYTE *caveB2 = cave + 80;

  BYTE b1[80];
  int n = 0;
  b1[n++] = 0xC7;
  b1[n++] = 0x45;
  b1[n++] = 0xF8;
  b1[n++] = 0x00;
  b1[n++] = 0x00;
  b1[n++] = 0x00;
  b1[n++] = 0x00;
  int loopTop = n;
  b1[n++] = 0x83;
  b1[n++] = 0x7D;
  b1[n++] = 0xF8;
  b1[n++] = 0x06;
  b1[n++] = 0x7D;
  int jgeRel8Pos = n;
  b1[n++] = 0x00;
  b1[n++] = 0x6A;
  b1[n++] = 0x00;
  b1[n++] = 0x6A;
  b1[n++] = 0x00;
  b1[n++] = 0x8B;
  b1[n++] = 0x55;
  b1[n++] = 0xF8;
  b1[n++] = 0x83;
  b1[n++] = 0xC2;
  b1[n++] = 0x2E;
  b1[n++] = 0x52;
  b1[n++] = 0x8B;
  b1[n++] = 0x45;
  b1[n++] = 0xFC;
  b1[n++] = 0x50;
  b1[n++] = 0x8B;
  b1[n++] = 0x4D;
  b1[n++] = 0xF4;
  b1[n++] = 0xE8;
  {
    BYTE *callSite = caveB1 + n;
    int helperRel = (int)((intptr_t)helperAddr - (intptr_t)(callSite + 4));
    *(int *)&b1[n] = helperRel;
    n += 4;
  }
  b1[n++] = 0xFF;
  b1[n++] = 0x45;
  b1[n++] = 0xF8;
  {
    int jmpRel = loopTop - (n + 2);
    b1[n++] = 0xEB;
    b1[n++] = (BYTE)jmpRel;
  }
  int donePos = n;
  b1[jgeRel8Pos] = (BYTE)(donePos - jgeRel8Pos - 1);
  b1[n++] = 0x8B;
  b1[n++] = 0xE5;
  b1[n++] = 0x5D;
  b1[n++] = 0xC3;

  BYTE b2[16];
  int m = 0;
  b2[m++] = 0x8B;
  b2[m++] = 0x4D;
  b2[m++] = 0x0C;
  b2[m++] = 0x83;
  b2[m++] = 0xF9;
  b2[m++] = 0x2E;
  b2[m++] = 0x7C;
  b2[m++] = 0x04;
  b2[m++] = 0x83;
  b2[m++] = 0xC1;
  b2[m++] = 0x06;
  b2[m++] = 0xC3;
  b2[m++] = 0x83;
  b2[m++] = 0xC1;
  b2[m++] = 0x1A;
  b2[m++] = 0xC3;

  PatchCode(caveB1, b1, n);
  PatchCode(caveB2, b2, m);

  BYTE hookB1[5];
  hookB1[0] = 0xE9;
  *(int *)&hookB1[1] = (int)((intptr_t)caveB1 - (intptr_t)(exitAddr + 5));
  PatchCode(exitAddr, hookB1, 5);

  BYTE hookB2[7];
  hookB2[0] = 0xE8;
  *(int *)&hookB2[1] = (int)((intptr_t)caveB2 - (intptr_t)(bgCalcAddr + 5));
  hookB2[5] = 0x51;
  hookB2[6] = 0x90;
  PatchCode(bgCalcAddr, hookB2, 7);

  EquipUiLog("[EquipUI] Patch B OK: helper@0x%p, exit@0x%p->0x%p, bg@0x%p->0x%p",
             helperAddr, exitAddr, caveB1, bgCalcAddr, caveB2);
}

static void PatchSurfBoundsCheck() {
  BYTE *addr = (BYTE *)SURF_BOUNDS_CHECK;
  if (addr[0] == 0x81 && addr[1] == 0xFA) {
    EquipUiLog("[EquipUI] Patch D: already applied, skipping");
    return;
  }
  static const BYTE expected[6] = {0x3B, 0x15, 0xB0, 0xD0, 0xC2, 0x00};
  if (memcmp(addr, expected, 6) != 0) {
    EquipUiLog("[EquipUI][WARN] Patch D: instruction mismatch @0x%p, skipping", addr);
    return;
  }
  BYTE patched[6] = {0x81, 0xFA, 0x33, 0x75, 0x00, 0x00};
  PatchCode(addr, patched, 6);
  EquipUiLog("[EquipUI] Patch D OK: Surf bounds check -> cmp edx,30003");
}

void InstallAll() {
  EquipUiLog("[EquipUI] Installing equip slot expansion patch (A+B+D, 14->31)");
  PatchServerIndexToUiSlot();
  PatchSetupSlotsHooks();
  PatchSurfBoundsCheck();
  launcherdll_hook_log("[Install] EquipUI ok");
}

} // namespace EquipUiPatch
