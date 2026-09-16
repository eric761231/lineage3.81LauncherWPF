// LightStampHook.cpp: S_Light → obj+0x16 與光罩尺寸表測試。
//
// 封包路徑（已 CE 驗證）：
//   0x544B4F → 0x524A10 ("dc") → 0x4F4D00 → mov [obj+0x16], type
// 畫面半徑不是 type 格數，而是查：
//   0xABF8E8 / 0xABF8EC = 每 level 寬高（level*8）
//   0xABF8A8 = stamp 指標（level*4）
//   貼上：0x4EB129 → 0x556F00
//
// 本 hook：
//   1) Detour 0x4F4D00：每次 S_Light 寫 log（typeIn / 寫入後 +0x16 / 對應 stamp wh）
//   2) 安裝時 dump 0..15 尺寸表；若 kTestScale!=1 則放大 level>=kScaleFromLevel 的 wh
//      （只改表、不換圖；圖可能被拉糊，用來確認「變大是否有效」）
#include "stdafx.h"
#include "LightStampHook.h"
#include "LauncherDll.h"
#include "detours.h"
#include <cstring>

#pragma comment(lib, "detours.lib")

namespace {

constexpr DWORD kApplyLightAddr = 0x004F4D00;
constexpr DWORD kFindObjAddr = 0x005ADD70;
constexpr DWORD kStampWTable = 0x00ABF8E8; // [level*8] = width
constexpr DWORD kStampHTable = 0x00ABF8EC; // [level*8] = height
constexpr DWORD kStampPtrTable = 0x00ABF8A8;

// 測試用：1.0 = 只 log 不改表。
// 實測 ×2 只改 wh、不換 stamp 圖會貼光讀爆 → 畫面花掉；不要再開 scale。
constexpr float kTestScale = 1.0f;
constexpr int kScaleFromLevel = 10;
constexpr int kMaxLevel = 15;
constexpr int kMaxDim = 640; // 若再實驗 scale 用；目前關閉

typedef void(__cdecl *ApplyLight_t)(int objId, int type);
typedef void *(__cdecl *FindObj_t)(int objId);

ApplyLight_t real_ApplyLight = (ApplyLight_t)kApplyLightAddr;
FindObj_t FindObj = (FindObj_t)kFindObjAddr;

struct StampSize {
  int w;
  int h;
};
StampSize g_orig[kMaxLevel + 1] = {};
bool g_scaled = false;
bool g_tableDumped = false;

void PatchMem(void *addr, const void *data, size_t len) {
  DWORD oldProt = 0;
  VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &oldProt);
  memcpy(addr, data, len);
  VirtualProtect(addr, len, oldProt, &oldProt);
}

StampSize ReadStamp(int level) {
  StampSize s = {0, 0};
  if (level < 0 || level > kMaxLevel)
    return s;
  s.w = *(int *)(kStampWTable + level * 8);
  s.h = *(int *)(kStampHTable + level * 8);
  return s;
}

void WriteStamp(int level, int w, int h) {
  if (level < 0 || level > kMaxLevel)
    return;
  PatchMem((void *)(kStampWTable + level * 8), &w, sizeof(w));
  PatchMem((void *)(kStampHTable + level * 8), &h, sizeof(h));
}

void DumpTable(const char *tag) {
  for (int lv = 0; lv <= kMaxLevel; lv++) {
    StampSize s = ReadStamp(lv);
    DWORD ptr = *(DWORD *)(kStampPtrTable + lv * 4);
    launcherdll_hook_log("[LightStamp] %s lv=%d w=%d h=%d ptr=0x%08X", tag, lv, s.w,
                         s.h, (unsigned)ptr);
  }
}

bool TableLooksReady() {
  // level 14 正常約 312×156；登入前可能還是 0
  StampSize s14 = ReadStamp(14);
  StampSize s5 = ReadStamp(5);
  return s14.w > 0 && s14.h > 0 && s5.w > 0;
}

void ApplyScaleForTestOnce() {
  if (g_scaled || kTestScale == 1.0f)
    return;
  if (!TableLooksReady())
    return;

  if (!g_tableDumped) {
    DumpTable("table-before");
    g_tableDumped = true;
  }

  for (int lv = 0; lv <= kMaxLevel; lv++) {
    g_orig[lv] = ReadStamp(lv);
    if (lv < kScaleFromLevel)
      continue;
    if (g_orig[lv].w <= 0 || g_orig[lv].h <= 0)
      continue;
    int nw = (int)(g_orig[lv].w * kTestScale);
    int nh = (int)(g_orig[lv].h * kTestScale);
    if (nw > kMaxDim)
      nw = kMaxDim;
    if (nh > kMaxDim)
      nh = kMaxDim;
    nw = (nw + 3) & ~3;
    nh = (nh + 1) & ~1;
    WriteStamp(lv, nw, nh);
    launcherdll_hook_log("[LightStamp] scale lv=%d %dx%d -> %dx%d (x%.2f)", lv,
                         g_orig[lv].w, g_orig[lv].h, nw, nh, (double)kTestScale);
  }
  g_scaled = true;
  DumpTable("table-after-scale");
}

void __cdecl Hook_ApplyLight(int objId, int type) {
  // 進世界後表才會灌好；第一次有效 apply 時再放大
  ApplyScaleForTestOnce();

  real_ApplyLight(objId, type);

  int stored = -1;
  void *obj = NULL;
  __try {
    obj = FindObj(objId);
    if (obj)
      stored = (int)(*(unsigned char *)((char *)obj + 0x16));
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    stored = -2;
  }

  int useLv = stored >= 0 ? stored : type;
  if (useLv < 0)
    useLv = 0;
  if (useLv > kMaxLevel)
    useLv = kMaxLevel;
  StampSize s = ReadStamp(useLv);

  launcherdll_hook_log(
      "[LightStamp] apply objId=%d typeIn=%d stored=+0x16=%d stamp[lv%d]=%dx%d "
      "scaled=%d",
      objId, type, stored, useLv, s.w, s.h, g_scaled ? 1 : 0);
}

} // namespace

void InstallLightStampHook() {
  BYTE *p = (BYTE *)kApplyLightAddr;
  if (p[0] != 0x55 || p[1] != 0x8B || p[2] != 0xEC) {
    launcherdll_hook_log(
        "[LightStamp][WARN] 0x%08X prologue mismatch (%02X %02X %02X)，跳過",
        (unsigned)kApplyLightAddr, p[0], p[1], p[2]);
    return;
  }

  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  LONG r = DetourAttach(&(PVOID &)real_ApplyLight, (PVOID)Hook_ApplyLight);
  LONG c = DetourTransactionCommit();
  launcherdll_hook_log(
      "[LightStamp] Detour 0x%08X attach=%ld commit=%ld scale=%.2f fromLv=%d "
      "(表會在第一次 apply 且已灌好時再放大)",
      (unsigned)kApplyLightAddr, r, c, (double)kTestScale, kScaleFromLevel);

  if (kTestScale == 1.0f)
    launcherdll_hook_log("[LightStamp] kTestScale=1.0（只 log，不改表）");
  else if (TableLooksReady())
    ApplyScaleForTestOnce();
  else
    launcherdll_hook_log("[LightStamp] 尺寸表尚未就緒，延後到第一次 S_Light");
}
