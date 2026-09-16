// AllDayPatch.cpp: see AllDayPatch.h.
//
// 全部位址／位元組抄自 RUST 參考專案
// C:\python_training\L1J3.8Launcher(RUST)參考\src\aux\toggle\all_day.rs
// （同一支 TW13081901.bin，含單元測試鎖定關鍵位址/位元組）。細節見
// docs/PSS_全白天_AllDayHook計畫.md。
//
// 跟 RUST 版本的差異：RUST 是外部行程用 CreateRemoteThread 注入 shellcode做
// 「停用時立即刷新調色盤」；我們的 DLL 本來就在遊戲行程內，等價邏輯直接寫成
// 普通函式呼叫（RefreshPaletteNow），不需要 remote thread/shellcode。
#include "stdafx.h"
#include "AllDayPatch.h"
#include "PatchUtil.h"
#include "LauncherDll.h"
#include <atomic>
#include <cstring>

namespace {

struct BytePatch {
  DWORD addr;
  const BYTE *orig;
  const BYTE *patched;
  int len;
};

// ---- 亮度計算函式：直接回傳 15，不吃遊戲時間 ----
constexpr DWORD kBrightnessCalcAddr = 0x00786D70;
const BYTE kBrightnessCalcOrig[16] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x24, 0x8B,
                                      0x45, 0x08, 0x50, 0xE8, 0x81, 0xA1, 0xE0,
                                      0xFF, 0x83};
const BYTE kBrightnessCalcOn[16] = {0xB8, 0x0F, 0x00, 0x00, 0x00, 0xC3,
                                    0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
                                    0x90, 0x90, 0x90, 0x90};

// ---- 天氣（雨/雪/霧）渲染函式：入口直接 ret，不繪製 ----
constexpr DWORD kWeatherRenderAddr = 0x004ED890;
const BYTE kWeatherRenderOrig[1] = {0x55};
const BYTE kWeatherRenderOn[1] = {0xC3};

// ---- 「是否為白天」判斷：永遠回傳是 ----
constexpr DWORD kDaylightCheckAddr = 0x00787040;
const BYTE kDaylightCheckOrig[3] = {0x55, 0x8B, 0xEC};
const BYTE kDaylightCheckOn[3] = {0xB0, 0x01, 0xC3};

// ---- 調色盤變暗參數：歸零，不吃呼叫端傳進來的暗度 ----
constexpr DWORD kPaletteDarkenLoadArgAddr = 0x0057E6F7;
const BYTE kPaletteDarkenLoadArgOrig[3] = {0x8B, 0x45, 0x08};
const BYTE kPaletteDarkenLoadArgOn[3] = {0x31, 0xC0, 0x90};
constexpr DWORD kPaletteDarkenCacheArgAddr = 0x0057E707;
const BYTE kPaletteDarkenCacheArgOrig[3] = {0x8B, 0x4D, 0x08};
const BYTE kPaletteDarkenCacheArgOn[3] = {0x31, 0xC9, 0x90};

// ---- cave_dark 兩處判斷立即數：01→00（不強制昏暗） ----
constexpr DWORD kCaveDarkHighMapSetImmAddr = 0x004EA514;
constexpr DWORD kCaveDarkTilesetSetImmAddr = 0x004EA551;
const BYTE kCaveDarkSetOrig[1] = {0x01};
const BYTE kCaveDarkSetOn[1] = {0x00};

// ---- cave 路徑強制昏暗立即數：01→0F（改成最大亮度） ----
constexpr DWORD kCaveLightForceImmAddr = 0x004EA6D4;
const BYTE kCaveLightForceOrig[1] = {0x01};
const BYTE kCaveLightForceOn[1] = {0x0F};

// ---- 「光源已最大就跳過重算」分支：NOP 掉，讓最大光源真的套用 ----
constexpr DWORD kLightRecomputeSkipBranchAddr = 0x004EAD19;
const BYTE kLightRecomputeSkipBranchOrig[6] = {0x0F, 0x8D, 0x2B, 0x04, 0x00, 0x00};
const BYTE kLightRecomputeSkipBranchOn[6] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};

// ---- 環境暗層 overlay 繪製分支：無條件跳過 ----
constexpr DWORD kEnvironmentOverlayBranchAddr = 0x004F0E92;
const BYTE kEnvironmentOverlayBranchOrig[7] = {0x83, 0x3D, 0xF0, 0xC9,
                                               0xBD, 0x00, 0x00};
const BYTE kEnvironmentOverlayBranchOn[7] = {0xE9, 0xA6, 0x00, 0x00,
                                             0x00, 0x90, 0x90};

// ---- 最終送進繪製流程的光源參數：直接塞 15 ----
constexpr DWORD kFinalLightArgAddr = 0x004F037C;
const BYTE kFinalLightArgOrig[3] = {0x8B, 0x55, 0xAC};
const BYTE kFinalLightArgOn[3] = {0x6A, 0x0F, 0x5A};

const BytePatch kPatches[] = {
    {kBrightnessCalcAddr, kBrightnessCalcOrig, kBrightnessCalcOn, 16},
    {kWeatherRenderAddr, kWeatherRenderOrig, kWeatherRenderOn, 1},
    {kDaylightCheckAddr, kDaylightCheckOrig, kDaylightCheckOn, 3},
    {kPaletteDarkenLoadArgAddr, kPaletteDarkenLoadArgOrig,
     kPaletteDarkenLoadArgOn, 3},
    {kPaletteDarkenCacheArgAddr, kPaletteDarkenCacheArgOrig,
     kPaletteDarkenCacheArgOn, 3},
    {kCaveDarkHighMapSetImmAddr, kCaveDarkSetOrig, kCaveDarkSetOn, 1},
    {kCaveDarkTilesetSetImmAddr, kCaveDarkSetOrig, kCaveDarkSetOn, 1},
    {kCaveLightForceImmAddr, kCaveLightForceOrig, kCaveLightForceOn, 1},
    {kLightRecomputeSkipBranchAddr, kLightRecomputeSkipBranchOrig,
     kLightRecomputeSkipBranchOn, 6},
    {kEnvironmentOverlayBranchAddr, kEnvironmentOverlayBranchOrig,
     kEnvironmentOverlayBranchOn, 7},
    {kFinalLightArgAddr, kFinalLightArgOrig, kFinalLightArgOn, 3},
};
constexpr int kPatchCount = sizeof(kPatches) / sizeof(kPatches[0]);

// ---- 天氣強度 / cave_dark 旗標：資料，不是程式碼，enable 時歸零 ----
constexpr DWORD kWeatherStateAddrs[3] = {0x00ABF324, 0x00ABF328, 0x00ABF8A4};
constexpr DWORD kCaveDarkFlagAddr = 0x009ABCEF;

// ---- 調色盤刷新用（RefreshPaletteNow）----
constexpr DWORD kPaletteObjThis = 0x00BDC7A4;
constexpr DWORD kPaletteObjRefresh = 0x00579E10;
constexpr DWORD kGameTimeGlobal = 0x00C31E7C;
constexpr DWORD kMapIdGlobal = 0x00965B60;
constexpr DWORD kTilesetIdGlobal = 0x00965B64;
constexpr DWORD kTilesetTableBase = 0x009655F0;
constexpr DWORD kTilesetTableLen = 0x15A;

typedef void(__thiscall *PaletteRefresh_t)(void *thisPtr, int brightness);
typedef int(__cdecl *BrightnessCalc_t)(int gameTime, int, int, int);

std::atomic<bool> g_enabled{false};
bool g_installed = false;

bool AllOriginal() {
  for (int i = 0; i < kPatchCount; i++) {
    if (memcmp((void *)kPatches[i].addr, kPatches[i].orig, kPatches[i].len) != 0) {
      return false;
    }
  }
  return true;
}

bool AllPatched() {
  for (int i = 0; i < kPatchCount; i++) {
    if (memcmp((void *)kPatches[i].addr, kPatches[i].patched, kPatches[i].len) != 0) {
      return false;
    }
  }
  return true;
}

void ApplyPatches() {
  for (int i = 0; i < kPatchCount; i++) {
    PatchCode((void *)kPatches[i].addr, kPatches[i].patched, kPatches[i].len);
  }
}

void RestorePatches() {
  for (int i = 0; i < kPatchCount; i++) {
    PatchCode((void *)kPatches[i].addr, kPatches[i].orig, kPatches[i].len);
  }
}

void ClearWeatherAndCaveState() {
  const DWORD zero = 0;
  for (int i = 0; i < 3; i++) {
    PatchCode((void *)kWeatherStateAddrs[i], &zero, sizeof(zero));
  }
  const BYTE zeroByte = 0;
  PatchCode((void *)kCaveDarkFlagAddr, &zeroByte, sizeof(zeroByte));
}

// 依「當下實際地圖/時間」重算 cave_dark 並立即刷新調色盤，等價於原生
// 0x004EA6C8 那段邏輯（見 all_day.rs 底部反組譯註解）。
// 注意：呼叫這個之前，BRIGHTNESS_CALC 的 patch 必須已經還原，否則算出來的
// 「亮度」只會是我們自己塞的 15，沒有意義（disable 流程要注意呼叫順序）。
void RefreshPaletteNow() {
  __try {
    void *paletteObj = reinterpret_cast<void *>(kPaletteObjThis);
    PaletteRefresh_t refresh =
        reinterpret_cast<PaletteRefresh_t>(kPaletteObjRefresh);

    const int mapId = *reinterpret_cast<int *>(kMapIdGlobal);
    BYTE cave = 0;
    if (mapId >= 0x4000) {
      cave = 1;
    } else {
      const int tileset = *reinterpret_cast<int *>(kTilesetIdGlobal);
      const int *table = reinterpret_cast<int *>(kTilesetTableBase);
      for (DWORD i = 0; i < kTilesetTableLen; i++) {
        if (table[i] == tileset) {
          cave = 1;
          break;
        }
      }
    }
    *reinterpret_cast<BYTE *>(kCaveDarkFlagAddr) = cave;

    if (cave) {
      refresh(paletteObj, 1);
    } else {
      const int gameTime = *reinterpret_cast<int *>(kGameTimeGlobal);
      BrightnessCalc_t brightnessCalc =
          reinterpret_cast<BrightnessCalc_t>(kBrightnessCalcAddr);
      const int brightness = brightnessCalc(gameTime, 0, 0, 0);
      refresh(paletteObj, brightness);
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    launcherdll_hook_log("[Pss][AllDay] RefreshPaletteNow exception, skip");
  }
}

} // namespace

void InstallAllDayHook() {
  if (g_installed) {
    return;
  }
  if (!AllOriginal() && !AllPatched()) {
    launcherdll_hook_log(
        "[Install] AllDay skip (bytes mismatch, version not matched)");
    return;
  }
  g_installed = true;
  // 只驗證特徵、記錄目前狀態，不在這裡（解密閘門剛過、世界都還沒進）主動套用
  // patch——此時 map/game-time 等全域狀態可能還沒填好，RefreshPaletteNow()
  // 讀了會不準。是否啟用交給 PssOverlay 的「進世界自動套用 cfg」流程決定，
  // 跟 UnderwaterPumpHook／AttackDamageHook 同一套時機。
  g_enabled.store(AllPatched(), std::memory_order_relaxed);
  launcherdll_hook_log("[Install] AllDay ok (already-on=%d)",
                       g_enabled.load(std::memory_order_relaxed) ? 1 : 0);
}

void AllDayHook_SetEnabled(bool enabled) {
  if (!g_installed) {
    return;
  }
  if (enabled == g_enabled.load(std::memory_order_relaxed)) {
    return;
  }
  if (enabled) {
    ApplyPatches();
    ClearWeatherAndCaveState();
    g_enabled.store(true, std::memory_order_relaxed);
    RefreshPaletteNow();
    launcherdll_hook_log("[Pss][AllDay] enabled");
  } else {
    // 順序不能顛倒：先還原全部 code patch（含 BRIGHTNESS_CALC 本體），
    // RefreshPaletteNow 才能呼叫到「正確」的亮度計算，而不是我們自己塞的 15。
    RestorePatches();
    g_enabled.store(false, std::memory_order_relaxed);
    RefreshPaletteNow();
    launcherdll_hook_log("[Pss][AllDay] disabled");
  }
}

bool AllDayHook_IsEnabled() {
  return g_enabled.load(std::memory_order_relaxed);
}
