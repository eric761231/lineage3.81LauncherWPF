// OverlayAssets.cpp: loads <folderName>\<pakBaseName>.pak/.idx (background/
// icon PNGs + strings.xml) so an overlay's appearance/text can be swapped by
// re-packing assets, no DLL rebuild. Generic: any number of independent
// overlays can each load their own pak set via OverlayAssets_Load and get
// back an opaque OverlayAssetSet* handle; state never crosses between sets.
//
// This DLL runs injected into the GAME process, so GetModuleFileNameA(NULL)
// resolves to the game's own exe directory (game root), not the Core\
// folder LinLauncher.exe/LauncherDll.dll ship from -- same convention
// LauncherDll.cpp's LoadCombatConfig() already relies on for xml\. So the
// pak/idx must live at <game root>\<folderName>\, not Core\<folderName>\.
//
// pak format: entries' raw bytes concatenated, whole-file XOR'd with the
// same fixed key LauncherDll's GetFileBuffer() uses for TW13081901.pak.
// idx format: plain text, one "name=offset,length" per line.
// strings.xml: line-based tags, parsed the same hand-rolled fgets+strstr
// style LauncherDll.cpp's LoadCombatConfig() already uses for bloodeffect.xml
// (see <Screen mode="..." title="..." btnX=".." ...><Body reason="..">text
// </Body></Screen>). File must be UTF-8.
#include <windows.h>
#include <gdiplus.h>
#include <objbase.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <string>
#include <map>
#include <vector>
#include "OverlayAssets.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")

namespace {

const char kFileEncryptKey[] = "PAt82IqEvNBmERYl"; // same as GetFileBuffer()

struct IdxEntry {
  size_t offset;
  size_t length;
};

struct ScreenStrings {
  std::wstring title;
  std::map<std::string, std::wstring> bodyByReason;
  bool hasBtnRect = false;
  RECT btnRect = {0, 0, 0, 0};
  bool hasSize = false;
  int width = 0;
  int height = 0;
  bool hasRefSize = false;
  int refW = 0;
  int refH = 0;
  bool hasTitleRect = false;
  RECT titleRect = {0, 0, 0, 0};
  int titleFontSize = 0;
  bool hasBodyRect = false;
  RECT bodyRect = {0, 0, 0, 0};
  int bodyFontSize = 0;
  std::wstring titleFontFamily;
  std::wstring bodyFontFamily;
  bool showTitle = true;
  bool titleCenter = false;
  bool bodyCenter = false;
  bool hasPos = false;
  int posX = 0;
  int posY = 0;
};

bool g_gdiplusStarted = false;
ULONG_PTR g_gdiplusToken = 0;
std::map<std::string, OverlayAssetSet *> g_instances; // key: folderName+"|"+pakBaseName

void NetLog(const char *fmt, ...) {
  char exePath[MAX_PATH] = {0};
  char logPath[MAX_PATH] = "./launcherdll_net.log";
  if (GetModuleFileNameA(NULL, exePath, MAX_PATH) > 0) {
    for (int i = (int)strlen(exePath) - 1; i >= 0; i--) {
      if (exePath[i] == '\\' || exePath[i] == '/') {
        exePath[i] = '\0';
        break;
      }
    }
    sprintf_s(logPath, "%s\\launcherdll_net.log", exePath);
  }
  FILE *fp = NULL;
  if (fopen_s(&fp, logPath, "a+") != 0 || fp == NULL)
    return;
  SYSTEMTIME st;
  GetLocalTime(&st);
  char msg[1024] = {0};
  va_list args;
  va_start(args, fmt);
  vsprintf_s(msg, fmt, args);
  va_end(args);
  fprintf(fp, "[%04d-%02d-%02d %02d:%02d:%02d.%03d][PID=%u][TID=%u] %s\n",
          st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
          st.wMilliseconds, (unsigned)GetCurrentProcessId(),
          (unsigned)GetCurrentThreadId(), msg);
  fflush(fp);
  fclose(fp);
}

bool ResolveExeRelativePath(const char *relPath, char *outPath, size_t outSize) {
  char exePath[MAX_PATH] = {0};
  if (GetModuleFileNameA(NULL, exePath, MAX_PATH) <= 0)
    return false;
  for (int i = (int)strlen(exePath) - 1; i >= 0; i--) {
    if (exePath[i] == '\\' || exePath[i] == '/') {
      exePath[i] = '\0';
      break;
    }
  }
  sprintf_s(outPath, outSize, "%s\\%s", exePath, relPath);
  return true;
}

bool ReadWholeFile(const char *path, std::vector<BYTE> *outData) {
  FILE *fp = NULL;
  if (fopen_s(&fp, path, "rb") != 0 || !fp)
    return false;
  fseek(fp, 0, SEEK_END);
  long len = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (len <= 0) {
    fclose(fp);
    return false;
  }
  outData->resize((size_t)len);
  size_t readCount = fread(outData->data(), 1, (size_t)len, fp);
  fclose(fp);
  return readCount == (size_t)len;
}

void XorDecrypt(std::vector<BYTE> *data) {
  size_t keyLen = sizeof(kFileEncryptKey) - 1; // exclude trailing NUL
  for (size_t i = 0; i < data->size(); i++)
    (*data)[i] ^= (BYTE)kFileEncryptKey[i % keyLen];
}

// Extracts attrName="value" from a line into outBuf (UTF-8 bytes, unescaped
// as-is). Returns false if the attribute isn't present on this line.
bool ExtractAttr(const char *line, const char *attrName, char *outBuf,
                 size_t outSize) {
  char needle[64];
  sprintf_s(needle, "%s=\"", attrName);
  const char *p = strstr(line, needle);
  if (!p)
    return false;
  p += strlen(needle);
  const char *end = strchr(p, '"');
  if (!end)
    return false;
  size_t len = (size_t)(end - p);
  if (len >= outSize)
    len = outSize - 1;
  memcpy(outBuf, p, len);
  outBuf[len] = 0;
  return true;
}

std::wstring Utf8ToWide(const char *utf8) {
  if (!utf8 || !utf8[0])
    return std::wstring();
  int chars = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
  if (chars <= 0)
    return std::wstring();
  std::wstring w;
  w.resize((size_t)chars - 1);
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &w[0], chars);
  return w;
}

} // namespace

// Per-loaded-pak state. Opaque to callers (OverlayAssets.h only forward-
// declares this struct); each OverlayAssets_Load() call gets its own
// instance, cached by (folderName, pakBaseName) so repeated Load calls for
// the same pak set are cheap and idempotent.
struct OverlayAssetSet {
  bool loaded = false;
  std::vector<BYTE> pakData;
  std::map<std::string, IdxEntry> idx;
  std::map<std::string, ScreenStrings> screens;
  std::map<std::string, Gdiplus::Bitmap *> bitmapCache;
};

namespace {

bool ParseIdx(OverlayAssetSet *set, const char *path) {
  FILE *fp = NULL;
  if (fopen_s(&fp, path, "r") != 0 || !fp) {
    NetLog("[overlay-assets] idx not found: %s", path);
    return false;
  }
  char line[512];
  bool firstLine = true;
  while (fgets(line, sizeof(line), fp)) {
    char *p = line;
    if (firstLine) {
      // Skip a UTF-8 BOM if present (e.g. from a different tool/encoding)
      // so it doesn't get glued onto the first entry's name.
      if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB &&
          (unsigned char)p[2] == 0xBF)
        p += 3;
      firstLine = false;
    }
    char name[128] = {0};
    size_t offset = 0, length = 0;
    // name=offset,length
    char *eq = strchr(p, '=');
    if (!eq)
      continue;
    size_t nameLen = (size_t)(eq - p);
    if (nameLen == 0 || nameLen >= sizeof(name))
      continue;
    memcpy(name, p, nameLen);
    name[nameLen] = 0;
    if (sscanf_s(eq + 1, "%zu,%zu", &offset, &length) != 2)
      continue;
    IdxEntry entry{offset, length};
    set->idx[name] = entry;
  }
  fclose(fp);
  return !set->idx.empty();
}

void ParseStringsXml(OverlayAssetSet *set, const BYTE *data, size_t len) {
  // Line-scan over the in-memory buffer (no FILE*, this came from the
  // decrypted pak, not disk) mirroring LoadCombatConfig()'s fgets style.
  std::string currentScreen;
  size_t pos = 0;
  while (pos < len) {
    size_t lineEnd = pos;
    while (lineEnd < len && data[lineEnd] != '\n')
      lineEnd++;
    size_t lineLen = lineEnd - pos;
    if (lineLen > 2000)
      lineLen = 2000;
    char line[2048] = {0};
    memcpy(line, data + pos, lineLen);
    line[lineLen] = 0;
    pos = lineEnd + 1;

    if (strstr(line, "<Screen")) {
      char mode[64] = {0};
      if (ExtractAttr(line, "mode", mode, sizeof(mode))) {
        currentScreen = mode;
        ScreenStrings &s = set->screens[currentScreen];
        char titleUtf8[512] = {0};
        if (ExtractAttr(line, "title", titleUtf8, sizeof(titleUtf8)))
          s.title = Utf8ToWide(titleUtf8);
        char num[32];
        int x, y, w, h;
        bool gotAll = ExtractAttr(line, "btnX", num, sizeof(num)) &&
                      (x = atoi(num), true) &&
                      ExtractAttr(line, "btnY", num, sizeof(num)) &&
                      (y = atoi(num), true) &&
                      ExtractAttr(line, "btnW", num, sizeof(num)) &&
                      (w = atoi(num), true) &&
                      ExtractAttr(line, "btnH", num, sizeof(num)) &&
                      (h = atoi(num), true);
        if (gotAll) {
          s.hasBtnRect = true;
          s.btnRect.left = x;
          s.btnRect.top = y;
          s.btnRect.right = x + w;
          s.btnRect.bottom = y + h;
        }
        int width, height;
        bool gotSize = ExtractAttr(line, "width", num, sizeof(num)) &&
                       (width = atoi(num), true) &&
                       ExtractAttr(line, "height", num, sizeof(num)) &&
                       (height = atoi(num), true);
        if (gotSize && width > 0 && height > 0) {
          s.hasSize = true;
          s.width = width;
          s.height = height;
        }
        int refW, refH;
        bool gotRef = ExtractAttr(line, "refW", num, sizeof(num)) &&
                     (refW = atoi(num), true) &&
                     ExtractAttr(line, "refH", num, sizeof(num)) &&
                     (refH = atoi(num), true);
        if (gotRef && refW > 0 && refH > 0) {
          s.hasRefSize = true;
          s.refW = refW;
          s.refH = refH;
        }
        char rectBuf[64];
        int rx, ry, rw, rh;
        if (ExtractAttr(line, "titleRect", rectBuf, sizeof(rectBuf)) &&
            sscanf_s(rectBuf, "%d,%d,%d,%d", &rx, &ry, &rw, &rh) == 4) {
          s.hasTitleRect = true;
          s.titleRect.left = rx;
          s.titleRect.top = ry;
          s.titleRect.right = rx + rw;
          s.titleRect.bottom = ry + rh;
        }
        if (ExtractAttr(line, "bodyRect", rectBuf, sizeof(rectBuf)) &&
            sscanf_s(rectBuf, "%d,%d,%d,%d", &rx, &ry, &rw, &rh) == 4) {
          s.hasBodyRect = true;
          s.bodyRect.left = rx;
          s.bodyRect.top = ry;
          s.bodyRect.right = rx + rw;
          s.bodyRect.bottom = ry + rh;
        }
        if (ExtractAttr(line, "titleFontSize", num, sizeof(num)))
          s.titleFontSize = atoi(num);
        if (ExtractAttr(line, "bodyFontSize", num, sizeof(num)))
          s.bodyFontSize = atoi(num);
        char fontUtf8[128];
        if (ExtractAttr(line, "titleFontFamily", fontUtf8, sizeof(fontUtf8)))
          s.titleFontFamily = Utf8ToWide(fontUtf8);
        if (ExtractAttr(line, "bodyFontFamily", fontUtf8, sizeof(fontUtf8)))
          s.bodyFontFamily = Utf8ToWide(fontUtf8);
        char boolBuf[16];
        if (ExtractAttr(line, "showTitle", boolBuf, sizeof(boolBuf)))
          s.showTitle = _stricmp(boolBuf, "false") != 0;
        if (ExtractAttr(line, "titleAlign", boolBuf, sizeof(boolBuf)))
          s.titleCenter = _stricmp(boolBuf, "center") == 0;
        if (ExtractAttr(line, "bodyAlign", boolBuf, sizeof(boolBuf)))
          s.bodyCenter = _stricmp(boolBuf, "center") == 0;
        int posX, posY;
        bool gotPos = ExtractAttr(line, "posX", num, sizeof(num)) &&
                     (posX = atoi(num), true) &&
                     ExtractAttr(line, "posY", num, sizeof(num)) &&
                     (posY = atoi(num), true);
        if (gotPos) {
          s.hasPos = true;
          s.posX = posX;
          s.posY = posY;
        }
      }
    } else if (strstr(line, "</Screen")) {
      currentScreen.clear();
    } else if (strstr(line, "<Body") && !currentScreen.empty()) {
      char reason[32] = "default";
      ExtractAttr(line, "reason", reason, sizeof(reason));
      const char *gt = strchr(line, '>');
      const char *closeTag = strstr(line, "</Body");
      if (gt && closeTag && closeTag > gt + 1) {
        std::string bodyUtf8(gt + 1, closeTag);
        std::string key = std::string("body_") + reason;
        set->screens[currentScreen].bodyByReason[key] = Utf8ToWide(bodyUtf8.c_str());
      }
    }
  }
}

} // namespace

OverlayAssetSet *OverlayAssets_Load(const char *folderName,
                                    const char *pakBaseName) {
  std::string cacheKey = std::string(folderName) + "|" + pakBaseName;
  auto existing = g_instances.find(cacheKey);
  if (existing != g_instances.end())
    return existing->second->loaded ? existing->second : nullptr;

  OverlayAssetSet *set = new OverlayAssetSet();
  g_instances[cacheKey] = set; // cache even on failure, so we don't retry every call

  char pakRel[MAX_PATH], idxRel[MAX_PATH];
  sprintf_s(pakRel, "%s\\%s.pak", folderName, pakBaseName);
  sprintf_s(idxRel, "%s\\%s.idx", folderName, pakBaseName);

  char pakPath[MAX_PATH], idxPath[MAX_PATH];
  if (!ResolveExeRelativePath(pakRel, pakPath, sizeof(pakPath)) ||
      !ResolveExeRelativePath(idxRel, idxPath, sizeof(idxPath))) {
    NetLog("[overlay-assets] could not resolve exe-relative paths for %s", folderName);
    return nullptr;
  }

  if (!ParseIdx(set, idxPath))
    return nullptr;

  if (!ReadWholeFile(pakPath, &set->pakData)) {
    NetLog("[overlay-assets] pak not found: %s", pakPath);
    set->idx.clear();
    return nullptr;
  }
  XorDecrypt(&set->pakData);

  auto it = set->idx.find("strings.xml");
  if (it != set->idx.end() && it->second.offset + it->second.length <= set->pakData.size()) {
    ParseStringsXml(set, set->pakData.data() + it->second.offset, it->second.length);
  } else {
    NetLog("[overlay-assets] strings.xml entry missing from idx (%s)", pakPath);
  }

  if (!g_gdiplusStarted) {
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, NULL) == Gdiplus::Ok)
      g_gdiplusStarted = true;
    else
      NetLog("[overlay-assets] GdiplusStartup failed");
  }

  set->loaded = !set->pakData.empty();
  NetLog("[overlay-assets] loaded pak=%s (%u bytes), %u idx entries, %u screens",
         pakPath, (unsigned)set->pakData.size(), (unsigned)set->idx.size(),
         (unsigned)set->screens.size());
  return set->loaded ? set : nullptr;
}

bool OverlayAssets_IsLoaded(OverlayAssetSet *set) { return set && set->loaded; }

bool OverlayAssets_GetRawBytes(OverlayAssetSet *set, const char *name,
                               const BYTE **outData, size_t *outLen) {
  if (!set || !set->loaded)
    return false;
  auto idxIt = set->idx.find(name);
  if (idxIt == set->idx.end())
    return false;
  const IdxEntry &e = idxIt->second;
  if (e.offset + e.length > set->pakData.size())
    return false;
  *outData = set->pakData.data() + e.offset;
  *outLen = e.length;
  return true;
}

Gdiplus::Bitmap *OverlayAssets_GetBitmap(OverlayAssetSet *set, const char *name) {
  if (!set || !set->loaded)
    return nullptr;
  auto cacheIt = set->bitmapCache.find(name);
  if (cacheIt != set->bitmapCache.end())
    return cacheIt->second;

  auto idxIt = set->idx.find(name);
  if (idxIt == set->idx.end()) {
    NetLog("[overlay-assets] idx has no entry named '%s' (not packed?)", name);
    return nullptr;
  }
  const IdxEntry &e = idxIt->second;
  if (e.offset + e.length > set->pakData.size()) {
    NetLog("[overlay-assets] idx entry '%s' out of bounds in pak", name);
    return nullptr;
  }

  HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, e.length);
  if (!hMem)
    return nullptr;
  void *pMem = GlobalLock(hMem);
  memcpy(pMem, set->pakData.data() + e.offset, e.length);
  GlobalUnlock(hMem);

  IStream *stream = nullptr;
  if (CreateStreamOnHGlobal(hMem, TRUE /*free on release*/, &stream) != S_OK) {
    GlobalFree(hMem);
    return nullptr;
  }

  Gdiplus::Bitmap *bmp = Gdiplus::Bitmap::FromStream(stream);
  stream->Release();
  if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) {
    NetLog("[overlay-assets] failed to decode bitmap: %s", name);
    delete bmp;
    return nullptr;
  }
  set->bitmapCache[name] = bmp;
  NetLog("[overlay-assets] decoded bitmap %s (%ux%u)", name,
         (unsigned)bmp->GetWidth(), (unsigned)bmp->GetHeight());
  return bmp;
}

bool OverlayAssets_GetText(OverlayAssetSet *set, const char *screen,
                           const char *key, wchar_t *outBuf, size_t outChars) {
  if (!set || !set->loaded || outChars == 0)
    return false;
  auto sIt = set->screens.find(screen);
  if (sIt == set->screens.end())
    return false;
  if (strcmp(key, "title") == 0) {
    if (sIt->second.title.empty())
      return false;
    wcsncpy_s(outBuf, outChars, sIt->second.title.c_str(), _TRUNCATE);
    return true;
  }
  auto bIt = sIt->second.bodyByReason.find(key);
  if (bIt == sIt->second.bodyByReason.end() || bIt->second.empty())
    return false;
  wcsncpy_s(outBuf, outChars, bIt->second.c_str(), _TRUNCATE);
  return true;
}

bool OverlayAssets_GetButtonRect(OverlayAssetSet *set, const char *screen,
                                 RECT *outRc) {
  if (!set || !set->loaded)
    return false;
  auto sIt = set->screens.find(screen);
  if (sIt == set->screens.end() || !sIt->second.hasBtnRect)
    return false;
  *outRc = sIt->second.btnRect;
  return true;
}

bool OverlayAssets_GetSize(OverlayAssetSet *set, const char *screen, int *outW,
                           int *outH) {
  if (!set || !set->loaded)
    return false;
  auto sIt = set->screens.find(screen);
  if (sIt == set->screens.end() || !sIt->second.hasSize)
    return false;
  *outW = sIt->second.width;
  *outH = sIt->second.height;
  return true;
}

bool OverlayAssets_GetRefSize(OverlayAssetSet *set, const char *screen,
                              int *outW, int *outH) {
  if (!set || !set->loaded)
    return false;
  auto sIt = set->screens.find(screen);
  if (sIt == set->screens.end() || !sIt->second.hasRefSize)
    return false;
  *outW = sIt->second.refW;
  *outH = sIt->second.refH;
  return true;
}

bool OverlayAssets_GetTextRect(OverlayAssetSet *set, const char *screen,
                               const char *which, RECT *outRc) {
  if (!set || !set->loaded)
    return false;
  auto sIt = set->screens.find(screen);
  if (sIt == set->screens.end())
    return false;
  if (strcmp(which, "title") == 0) {
    if (!sIt->second.hasTitleRect)
      return false;
    *outRc = sIt->second.titleRect;
    return true;
  }
  if (!sIt->second.hasBodyRect)
    return false;
  *outRc = sIt->second.bodyRect;
  return true;
}

bool OverlayAssets_GetFontSize(OverlayAssetSet *set, const char *screen,
                               const char *which, int *outSize) {
  if (!set || !set->loaded)
    return false;
  auto sIt = set->screens.find(screen);
  if (sIt == set->screens.end())
    return false;
  int size = strcmp(which, "title") == 0 ? sIt->second.titleFontSize
                                         : sIt->second.bodyFontSize;
  if (size <= 0)
    return false;
  *outSize = size;
  return true;
}

bool OverlayAssets_GetFontFamily(OverlayAssetSet *set, const char *screen,
                                 const char *which, wchar_t *outBuf,
                                 size_t outChars) {
  if (!set || !set->loaded || outChars == 0)
    return false;
  auto sIt = set->screens.find(screen);
  if (sIt == set->screens.end())
    return false;
  const std::wstring &family = strcmp(which, "title") == 0
                                   ? sIt->second.titleFontFamily
                                   : sIt->second.bodyFontFamily;
  if (family.empty())
    return false;
  wcsncpy_s(outBuf, outChars, family.c_str(), _TRUNCATE);
  return true;
}

bool OverlayAssets_GetShowTitle(OverlayAssetSet *set, const char *screen) {
  if (!set || !set->loaded)
    return true;
  auto sIt = set->screens.find(screen);
  if (sIt == set->screens.end())
    return true;
  return sIt->second.showTitle;
}

bool OverlayAssets_GetCenterAlign(OverlayAssetSet *set, const char *screen,
                                  const char *which) {
  if (!set || !set->loaded)
    return false;
  auto sIt = set->screens.find(screen);
  if (sIt == set->screens.end())
    return false;
  return strcmp(which, "title") == 0 ? sIt->second.titleCenter
                                     : sIt->second.bodyCenter;
}

bool OverlayAssets_GetPosition(OverlayAssetSet *set, const char *screen,
                               int *outX, int *outY) {
  if (!set || !set->loaded)
    return false;
  auto sIt = set->screens.find(screen);
  if (sIt == set->screens.end() || !sIt->second.hasPos)
    return false;
  *outX = sIt->second.posX;
  *outY = sIt->second.posY;
  return true;
}
