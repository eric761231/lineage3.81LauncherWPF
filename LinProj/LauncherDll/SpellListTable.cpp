// SpellListTable.cpp: see SpellListTable.h.
#include "stdafx.h"
#include "SpellListTable.h"
#include <objbase.h>
#include "OverlayAssets.h"
#include "LauncherDll.h"
#include <cstring>
#include <map>

namespace {

struct SpellRec {
  int icon = 0;
  wchar_t name[64] = {};
};

bool g_tried = false;
std::map<int, SpellRec> g_spells;

bool CopyAttr(const char *line, const char *key, char *out, int outn) {
  if (!line || !key || !out || outn <= 1) {
    return false;
  }
  char pat[32];
  sprintf_s(pat, "%s=\"", key);
  const char *p = strstr(line, pat);
  if (!p) {
    return false;
  }
  p += strlen(pat);
  int i = 0;
  while (*p && *p != '"' && i < outn - 1) {
    out[i++] = *p++;
  }
  out[i] = 0;
  return i > 0;
}

void ParseXml(const BYTE *data, size_t len) {
  g_spells.clear();
  size_t pos = 0;
  while (pos < len) {
    size_t lineEnd = pos;
    while (lineEnd < len && data[lineEnd] != '\n') {
      lineEnd++;
    }
    size_t lineLen = lineEnd - pos;
    if (lineLen > 1023) {
      lineLen = 1023;
    }
    char line[1024] = {};
    memcpy(line, data + pos, lineLen);
    pos = lineEnd + 1;
    if (!strstr(line, "<spell")) {
      continue;
    }
    char idBuf[16] = {};
    char iconBuf[16] = {};
    char descBuf[192] = {};
    if (!CopyAttr(line, "id", idBuf, sizeof(idBuf))) {
      continue;
    }
    CopyAttr(line, "icon", iconBuf, sizeof(iconBuf));
    CopyAttr(line, "desc", descBuf, sizeof(descBuf));
    SpellRec rec;
    rec.icon = atoi(iconBuf);
    if (descBuf[0]) {
      MultiByteToWideChar(CP_UTF8, 0, descBuf, -1, rec.name,
                          (int)_countof(rec.name));
      rec.name[_countof(rec.name) - 1] = 0;
    }
    g_spells[atoi(idBuf)] = rec;
  }
}

} // namespace

void SpellList_EnsureLoaded() {
  if (g_tried) {
    return;
  }
  g_tried = true;
  OverlayAssetSet *set = OverlayAssets_Load("ui", "ui");
  if (!set) {
    launcherdll_hook_log("[Pss] SpellList ui.pak missing");
    return;
  }
  const BYTE *data = nullptr;
  size_t len = 0;
  if (!OverlayAssets_GetRawBytes(set, "CSpellList.xml", &data, &len) || !data ||
      len == 0) {
    launcherdll_hook_log("[Pss] CSpellList.xml not in ui.pak");
    return;
  }
  ParseXml(data, len);
  launcherdll_hook_log("[Pss] CSpellList.xml loaded n=%d", (int)g_spells.size());
}

bool SpellList_Find(int packedId, int *outIcon, wchar_t *outName, int nameChars) {
  SpellList_EnsureLoaded();
  auto it = g_spells.find(packedId);
  if (it == g_spells.end()) {
    return false;
  }
  if (outIcon) {
    *outIcon = it->second.icon;
  }
  if (outName && nameChars > 0) {
    wcsncpy_s(outName, nameChars, it->second.name, _TRUNCATE);
  }
  return true;
}

Gdiplus::Bitmap *SpellList_GetIconBitmap(int packedId) {
  int icon = 0;
  if (!SpellList_Find(packedId, &icon, nullptr, 0) || icon <= 0) {
    return nullptr;
  }
  OverlayAssetSet *set = OverlayAssets_Load("ui", "ui");
  if (!set) {
    return nullptr;
  }
  char name[40];
  sprintf_s(name, "skill_%d.png", icon);
  Gdiplus::Bitmap *bmp = OverlayAssets_GetBitmap(set, name);
  if (bmp) {
    return bmp;
  }
  sprintf_s(name, "item_%d.png", icon);
  return OverlayAssets_GetBitmap(set, name);
}
