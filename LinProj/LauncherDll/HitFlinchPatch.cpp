// HitFlinchPatch.cpp: see HitFlinchPatch.h.
//
// Root cause: SHOULD_SKIP_FLINCH (0x5ABE70) decides whether a hit reaction
// takes the "just show the blood-splat effect (1248), skip the real flinch/
// stagger animation" shortcut, purely by checking whether the target's
// sprite ID falls in a narrow whitelisted range (0x216D-0x217A) meant for a
// handful of specific monster types. Every other monster sprite falls
// through to the "real flinch" path (action=2), which monsters don't have a
// working animation for, so they visually get stuck in a stagger pose
// forever instead of just showing the blood effect. Player characters (any
// PC, including PK opponents - not just the local player) are supposed to
// always get the real flinch reaction, but only happened to get it because
// their sprite IDs don't fall in that monster range - fragile, not an
// intentional check.
//
// v1 (kept only in history/commit log, not here) fixed this with a same-size
// in-place byte overwrite: NPC (UserObject+0x27 == 0) always skips flinch,
// PC keeps the original sprite-range check. Confirmed working, but treats
// every monster identically. This version replaces that with a Detours hook
// so it can consult a per-sprite-ID table (g_SpriteConfigs, loaded from
// NpcFlinch.xml inside the shared ui.pak) and only skip flinch for sprites
// the table says to - same PC/NPC split, but NPC behavior is now
// data-driven instead of blanket.
#include "stdafx.h"
#include "HitFlinchPatch.h"
#include <cstring>
#include "detours.h"
// stdafx.h defines WIN32_LEAN_AND_MEAN, which strips the RPC/OLE headers
// gdiplus.h (pulled in by OverlayAssets.h) needs for PROPID/byte - pull them
// back in explicitly before OverlayAssets.h (DisconnectOverlay.cpp/
// MimirPowerOverlay.cpp don't hit this since they never include stdafx.h).
#include <objbase.h>
#include "OverlayAssets.h"

#pragma comment(lib, "detours.lib")

std::map<int, SpriteConfig> g_SpriteConfigs;

namespace {

// Each translation unit in this project owns its own tiny logger (see
// NetLog in DisconnectHook.cpp, ImGuiLog in ImGuiHook.cpp) - the
// launcherdll_net_log in LauncherDll.cpp is `static` (internal linkage) and
// isn't linkable from here.
void HfLog(const char *fmt, ...) {
  char exePath[MAX_PATH] = {0};
  char logPath[MAX_PATH] = "./Core/launcher.log";
  if (GetModuleFileNameA(NULL, exePath, MAX_PATH) > 0) {
    for (int i = (int)strlen(exePath) - 1; i >= 0; i--) {
      if (exePath[i] == '\\' || exePath[i] == '/') {
        exePath[i] = '\0';
        break;
      }
    }
    sprintf_s(logPath, "%s\\Core\\launcher.log", exePath);
  }
  FILE *fp = NULL;
  if (fopen_s(&fp, logPath, "a+") != 0 || fp == NULL) return;
  SYSTEMTIME st;
  GetLocalTime(&st);
  char msg[512] = {0};
  va_list args;
  va_start(args, fmt);
  vsprintf_s(msg, fmt, args);
  va_end(args);
  fprintf(fp, "[%04d-%02d-%02d %02d:%02d:%02d.%03d][PID=%u][TID=%u] [HitFlinch] %s\n",
          st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
          st.wMilliseconds, (unsigned)GetCurrentProcessId(), (unsigned)GetCurrentThreadId(), msg);
  fflush(fp);
  fclose(fp);
}

typedef bool(__thiscall *ShouldSkipFlinch_t)(void *obj);
ShouldSkipFlinch_t real_ShouldSkipFlinch = (ShouldSkipFlinch_t)0x5ABE70;

// MSVC won't let a free function be defined with __thiscall directly (see
// MatchMakingHook.cpp's Hook_SetTypeLabel for the same trick already used
// twice this session) - __fastcall's first two params land in ECX/EDX,
// binary-compatible with how a thiscall caller puts `this` in ECX; the
// unused edx parameter just absorbs whatever thiscall didn't put there.
bool __fastcall Hook_ShouldSkipFlinch(void *obj, void * /*edx*/) {
  BYTE isPc = *((BYTE *)obj + 0x27);
  if (isPc != 0) return false; // 玩家（含PK對手）一律維持原本受身，不查表

  // 2026-09-02: 拿掉 suppressFlinch 布林值，presence 本身就是旗標 - 有列在
  // NpcFlinch.xml 裡（不管 no_damage 是多少，寫進去的都代表「這隻要跳過受身」）
  // 就跳過受身；沒列到的怪物維持原本的受身/後仰動畫（預設行為已反過來）。
  short sprite = *(short *)((BYTE *)obj + 0x18);
  return g_SpriteConfigs.find(sprite) != g_SpriteConfigs.end();
}

} // namespace

void LoadCombatConfig() {
  // NpcFlinch.xml is packed into the shared ui.pak (see Pack-UiAssets.ps1 /
  // tools\ui_sample\NpcFlinch.xml) rather than a loose disk file - same
  // mechanism MimirPowerOverlay.cpp/DisconnectOverlay.cpp already use via
  // OverlayAssets_Load("ui","ui") (cached, so this call is free if either of
  // those has already loaded it, and vice versa).
  OverlayAssetSet *set = OverlayAssets_Load("ui", "ui");
  if (!set) {
    HfLog("[CombatFix] ui.pak not loaded (not deployed?), skipping NPC flinch config");
    return;
  }
  const BYTE *data = nullptr;
  size_t len = 0;
  if (!OverlayAssets_GetRawBytes(set, "NpcFlinch.xml", &data, &len)) {
    HfLog("[CombatFix] NpcFlinch.xml not found in ui.pak");
    return;
  }

  // Line-scan over the in-memory decrypted buffer (no FILE*), mirroring
  // OverlayAssets.cpp's ParseStringsXml() - same per-line attribute parsing
  // as the old disk-file version, just fed from pak bytes instead of fgets.
  int count = 0;
  size_t pos = 0;
  while (pos < len) {
    size_t lineEnd = pos;
    while (lineEnd < len && data[lineEnd] != '\n')
      lineEnd++;
    size_t lineLen = lineEnd - pos;
    if (lineLen > 500)
      lineLen = 500;
    char line[512] = {0};
    memcpy(line, data + pos, lineLen);
    line[lineLen] = 0;
    pos = lineEnd + 1;

    // 逐行解析 XML 標籤（例如 <Sprite ）。id 有出現在這個表裡就代表跳過受身，
    // 不再讀 suppressFlinch 屬性（就算舊格式的 XML 還帶著這個屬性，這裡也不理
    // 會它的值，只看 id 存不存在）。
    if (strstr(line, "<Sprite")) {
      int spriteId = -1;
      int bloodEffectID = 10770; // 預設血液特效 ID
      // 解析 id 屬性
      char *pId = strstr(line, "id=\"");
      if (pId)
        sscanf_s(pId + 4, "%d", &spriteId);
      // 解析 bloodEffect 屬性
      char *pBlood = strstr(line, "bloodEffect=\"");
      if (pBlood)
        sscanf_s(pBlood + 13, "%d", &bloodEffectID);
      if (spriteId != -1) {
        SpriteConfig cfg{};
        cfg.bloodEffect = bloodEffectID;
        g_SpriteConfigs[spriteId] = cfg;
        count++;
      }
    }
  }
  HfLog("[CombatFix] Loaded %d monster configs from ui.pak(NpcFlinch.xml)", count);
}

void InstallHitFlinchPatch() {
  LoadCombatConfig();

  const BYTE expected[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08};
  if (memcmp((void *)0x5ABE70, expected, 6) != 0) {
    HfLog("0x5ABE70 prologue mismatch, skipping install");
    return;
  }

  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  DetourAttach(&(PVOID &)real_ShouldSkipFlinch, reinterpret_cast<PVOID>(Hook_ShouldSkipFlinch));
  LONG result = DetourTransactionCommit();
  HfLog("SHOULD_SKIP_FLINCH hook install result=%ld", result);
}
