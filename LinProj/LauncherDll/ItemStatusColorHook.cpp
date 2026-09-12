#include "stdafx.h"
#include "ItemStatusColorHook.h"
#include "LauncherDll.h"
#include "detours.h"
#include <cstring>

typedef void(__cdecl *ItemUiDraw6Fn)(void *font, const char *str, int x, int y,
                                    DWORD color, DWORD flag);
static ItemUiDraw6Fn real_ItemUiDraw6 = (ItemUiDraw6Fn)0x46E0F0;

typedef void(__cdecl *ItemUiDrawFecFn)(void *font, const char *str, int len, int x,
                                      int y, DWORD color);
static ItemUiDrawFecFn real_ItemUiDrawFec = (ItemUiDrawFecFn)0x46FEC0;

typedef int(__cdecl *ItemUiGlyphFn)(void *font, const char *start, int len, int unk,
                                   int x, int y, DWORD color);
static ItemUiGlyphFn real_ItemUiGlyph = (ItemUiGlyphFn)0x46D420;

static bool ItemBufHasFColor(const char *s, int len) {
  if (!s)
    return false;
  const int n = (len > 0) ? len : 256;
  for (int i = 0; i + 1 < n && s[i]; ++i) {
    if (static_cast<unsigned char>(s[i]) == 0x5C &&
        static_cast<unsigned char>(s[i + 1]) == 0x66)
      return true;
  }
  return false;
}

static void __cdecl Hook_ItemUiDraw6(void *font, const char *str, int x, int y,
                                    DWORD color, DWORD flag) {
  const bool hasF = ItemBufHasFColor(str, 0);
  real_ItemUiDraw6(font, str, x, y, color, hasF ? 0 : flag);
}

static void __cdecl Hook_ItemUiDrawFec(void *font, const char *str, int len, int x,
                                      int y, DWORD color) {
  if (!str || len <= 0 || !ItemBufHasFColor(str, len)) {
    real_ItemUiDrawFec(font, str, len, x, y, color);
    return;
  }
  char tmp[512];
  int out = 0;
  DWORD col = color;
  const int n = (len < 511) ? len : 511;
  for (int i = 0; i < n && str[i] && out < 511;) {
    if (i + 2 < n && static_cast<unsigned char>(str[i]) == 0x5C &&
        static_cast<unsigned char>(str[i + 1]) == 0x66) {
      const unsigned char ch = static_cast<unsigned char>(str[i + 2]);
      if (ch >= 0x30 && ch < 0x7D) {
        col = *reinterpret_cast<DWORD *>(0x95FA78 + ch * 4);
        i += 3;
        continue;
      }
    }
    tmp[out++] = str[i++];
  }
  tmp[out] = 0;
  real_ItemUiDrawFec(font, tmp, out, x, y, col);
}

static int __cdecl Hook_ItemUiGlyph(void *font, const char *start, int len, int unk,
                                   int x, int y, DWORD color) {
  if (start && len >= 3 && static_cast<unsigned char>(start[0]) == 0x5C &&
      static_cast<unsigned char>(start[1]) == 0x66) {
    const unsigned char ch = static_cast<unsigned char>(start[2]);
    if (ch >= 0x30 && ch < 0x7D) {
      color = *reinterpret_cast<DWORD *>(0x95FA78 + ch * 4);
      start += 3;
      len -= 3;
    }
  }
  return real_ItemUiGlyph(font, start, len, unk, x, y, color);
}

void InstallItemStatusColorHook() {
  static const BYTE expF0[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10};
  static const BYTE expFec[5] = {0x55, 0x8B, 0xEC, 0x81, 0x3D};
  static const BYTE expGlyph[5] = {0x55, 0x8B, 0xEC, 0x51, 0x83};
  if (memcmp(reinterpret_cast<void *>(0x46E0F0), expF0, sizeof(expF0)) != 0 ||
      memcmp(reinterpret_cast<void *>(0x46FEC0), expFec, sizeof(expFec)) != 0 ||
      memcmp(reinterpret_cast<void *>(0x46D420), expGlyph, sizeof(expGlyph)) != 0) {
    launcherdll_hook_log("[Install] ItemStatusColor skip");
    return;
  }
  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  DetourAttach(&(PVOID &)real_ItemUiDraw6, reinterpret_cast<PVOID>(Hook_ItemUiDraw6));
  DetourAttach(&(PVOID &)real_ItemUiDrawFec,
               reinterpret_cast<PVOID>(Hook_ItemUiDrawFec));
  DetourAttach(&(PVOID &)real_ItemUiGlyph, reinterpret_cast<PVOID>(Hook_ItemUiGlyph));
  const LONG result = DetourTransactionCommit();
  launcherdll_hook_log("[Install] ItemStatusColor %s", result == 0 ? "ok" : "fail");
}
