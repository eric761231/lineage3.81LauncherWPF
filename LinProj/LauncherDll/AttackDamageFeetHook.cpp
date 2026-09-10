// AttackDamageFeetHook.cpp: 紅色傷害氣泡改錨點到腳下。
// 對照 RUST attack_damage_feet_hook.rs（四段 codecave）。
#include "stdafx.h"
#include "AttackDamageFeetHook.h"
#include "LauncherDll.h"
#include <cstring>

namespace {

constexpr DWORD kLocalAddr = 0x0042B9D2;
constexpr DWORD kRemoteAddr = 0x0042B9FB;
constexpr DWORD kPostAc80Addr = 0x0042BAFB;
constexpr DWORD kAc80ResetAddr = 0x0042AE0C;
constexpr DWORD kReturnAfterPos = 0x0042BA01;
constexpr DWORD kReturnAfterPost = 0x0042BB00;
constexpr DWORD kReturnAfterReset = 0x0042AE13;

constexpr int kLocalLen = 6;
constexpr int kRemoteLen = 6;
constexpr int kPostLen = 5;
constexpr int kResetLen = 7;

const BYTE kLocalOrig[kLocalLen] = {0x89, 0x8A, 0x80, 0x03, 0x00, 0x00};
const BYTE kRemoteOrig[kRemoteLen] = {0x89, 0x81, 0x80, 0x03, 0x00, 0x00};
const BYTE kPostOrig[kPostLen] = {0x0F, 0xB6, 0xC0, 0x85, 0xC0};
const BYTE kResetOrig[kResetLen] = {0xC6, 0x81, 0x6A, 0x03, 0x00, 0x00, 0x00};

constexpr BYTE kColorLo = 0x00;
constexpr BYTE kColorHi = 0xF8;
constexpr BYTE kFeetPad = 0x20;
constexpr size_t kCaveSize = 0x400;

bool g_installed = false;

void PatchCode(void *addr, const void *code, int len) {
  DWORD oldProt = 0;
  VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, PAGE_READWRITE, &oldProt);
  memcpy(addr, code, len);
  VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, oldProt, &oldProt);
}

void EmitJmp32(BYTE *sc, int &n, DWORD here, DWORD target) {
  sc[n++] = 0xE9;
  *(int *)&sc[n] = (int)((intptr_t)target - (intptr_t)(here + 5));
  n += 4;
}

void BuildJmpPatch(BYTE *patch, int len, DWORD hookAddr, DWORD target) {
  memset(patch, 0x90, len);
  patch[0] = 0xE9;
  *(int *)&patch[1] = (int)((intptr_t)target - (intptr_t)hookAddr - 5);
}

// remote: mov [ecx+0x380],eax ; if color==red adjust Y to feet
int EmitRemote(BYTE *sc, DWORD segStart) {
  int n = 0;
  const BYTE body[] = {
      0x89, 0x81, 0x80, 0x03, 0x00, 0x00, 0x66, 0x81, 0x7D, 0x10, kColorLo, kColorHi,
      0x75, 0x2A, 0x8B, 0x91, 0x98, 0x03, 0x00, 0x00, 0x85, 0xD2, 0x74, 0x20,
      0x0F, 0xBE, 0x42, 0x1D, 0x6B, 0xC0, 0x18, 0x8B, 0x92, 0x8C, 0x00, 0x00, 0x00,
      0x85, 0xD2, 0x74, 0x0F, 0x8B, 0x54, 0x02, 0x14, 0xF7, 0xDA, 0x83, 0xC2, kFeetPad,
      0x01, 0x91, 0x80, 0x03, 0x00, 0x00};
  memcpy(sc, body, sizeof(body));
  n = (int)sizeof(body);
  EmitJmp32(sc, n, segStart + n, kReturnAfterPos);
  return n;
}

int EmitLocal(BYTE *sc, DWORD segStart) {
  int n = 0;
  const BYTE body[] = {
      0x89, 0x8A, 0x80, 0x03, 0x00, 0x00, 0x66, 0x81, 0x7D, 0x10, kColorLo, kColorHi,
      0x75, 0x2A, 0x8B, 0x8A, 0x98, 0x03, 0x00, 0x00, 0x85, 0xC9, 0x74, 0x20,
      0x0F, 0xBE, 0x41, 0x1D, 0x6B, 0xC0, 0x18, 0x8B, 0x89, 0x8C, 0x00, 0x00, 0x00,
      0x85, 0xC9, 0x74, 0x0F, 0x8B, 0x4C, 0x01, 0x14, 0xF7, 0xD9, 0x83, 0xC1, kFeetPad,
      0x01, 0x8A, 0x80, 0x03, 0x00, 0x00};
  memcpy(sc, body, sizeof(body));
  n = (int)sizeof(body);
  EmitJmp32(sc, n, segStart + n, kReturnAfterPos);
  return n;
}

int EmitPostAc80(BYTE *sc, DWORD segStart) {
  int n = 0;
  const BYTE body[] = {0x8B, 0x55, 0xE0, 0x66, 0x81, 0xBA, 0x9C, 0x03, 0x00, 0x00,
                       kColorLo, kColorHi, 0x75, 0x07, 0xC6, 0x82, 0x6A, 0x03,
                       0x00, 0x00, 0x04, 0x0F, 0xB6, 0xC0, 0x85, 0xC0};
  memcpy(sc, body, sizeof(body));
  n = (int)sizeof(body);
  EmitJmp32(sc, n, segStart + n, kReturnAfterPost);
  return n;
}

int EmitAc80Reset(BYTE *sc, DWORD segStart) {
  int n = 0;
  const BYTE body[] = {0x66, 0x81, 0xB9, 0x9C, 0x03, 0x00, 0x00, kColorLo, kColorHi,
                       0x74, 0x07, 0xC6, 0x81, 0x6A, 0x03, 0x00, 0x00, 0x00};
  memcpy(sc, body, sizeof(body));
  n = (int)sizeof(body);
  EmitJmp32(sc, n, segStart + n, kReturnAfterReset);
  return n;
}

} // namespace

void InstallAttackDamageFeetHook() {
  if (g_installed)
    return;

  if (memcmp((void *)kLocalAddr, kLocalOrig, kLocalLen) != 0 ||
      memcmp((void *)kRemoteAddr, kRemoteOrig, kRemoteLen) != 0 ||
      memcmp((void *)kPostAc80Addr, kPostOrig, kPostLen) != 0 ||
      memcmp((void *)kAc80ResetAddr, kResetOrig, kResetLen) != 0) {
    launcherdll_hook_log("[AttackDmgFeet][WARN] bytes mismatch, skip");
    return;
  }

  BYTE *cave = (BYTE *)VirtualAlloc(NULL, kCaveSize, MEM_COMMIT | MEM_RESERVE,
                                    PAGE_EXECUTE_READWRITE);
  if (!cave) {
    launcherdll_hook_log("[AttackDmgFeet][WARN] VirtualAlloc failed");
    return;
  }

  DWORD base = (DWORD)(uintptr_t)cave;
  int off = 0;
  int remoteOff = off;
  off += EmitRemote(cave + off, base + remoteOff);
  int localOff = off;
  off += EmitLocal(cave + off, base + localOff);
  int postOff = off;
  off += EmitPostAc80(cave + off, base + postOff);
  int resetOff = off;
  off += EmitAc80Reset(cave + off, base + resetOff);

  BYTE patch[8];
  BuildJmpPatch(patch, kRemoteLen, kRemoteAddr, base + remoteOff);
  PatchCode((void *)kRemoteAddr, patch, kRemoteLen);
  BuildJmpPatch(patch, kLocalLen, kLocalAddr, base + localOff);
  PatchCode((void *)kLocalAddr, patch, kLocalLen);
  BuildJmpPatch(patch, kPostLen, kPostAc80Addr, base + postOff);
  PatchCode((void *)kPostAc80Addr, patch, kPostLen);
  BuildJmpPatch(patch, kResetLen, kAc80ResetAddr, base + resetOff);
  PatchCode((void *)kAc80ResetAddr, patch, kResetLen);

  g_installed = true;
  launcherdll_hook_log(
      "[AttackDmgFeet] installed cave=%p (remote=+0x%X local=+0x%X post=+0x%X reset=+0x%X)",
      cave, remoteOff, localOff, postOff, resetOff);
}
