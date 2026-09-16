// WindShackleTimePatch.cpp: see WindShackleTimePatch.h.
#include "stdafx.h"
#include "WindShackleTimePatch.h"
#include "PatchUtil.h"
#include "LauncherDll.h"
#include <cstring>

namespace {

constexpr DWORD kShlAddr = 0x0053D02B;
const BYTE kShlEcx2[3] = {0xC1, 0xE1, 0x02};
const BYTE kNop3[3] = {0x90, 0x90, 0x90};

} // namespace

void InstallWindShackleTimePatch() {
  BYTE got[3] = {};
  memcpy(got, (const void *)kShlAddr, 3);
  if (memcmp(got, kShlEcx2, 3) != 0) {
    launcherdll_hook_log("[WindShackle] skip: %02X %02X %02X @ %08X", got[0],
                         got[1], got[2], (unsigned)kShlAddr);
    return;
  }
  PatchCode((void *)kShlAddr, kNop3, 3);
  launcherdll_hook_log("[WindShackle] nop shl ecx,2 @ %08X", (unsigned)kShlAddr);
}
