// MimirPowerOverlay.cpp: 密米爾之泉自繪卡片清單視窗。
// 跟 DisconnectOverlay 不同：這個視窗要真的能互動（點列表選項、按確認鈕），不是
// 純觀察用的 click-through 疊層，所以：
//   - hWndOwner 設成 g_hGameWnd：Windows 自動讓這個視窗永遠疊在遊戲視窗正上方、
//     隨遊戲視窗一起最小化/還原，不用比照 DisconnectOverlay 那套 200ms 計時器手動
//     判斷前景視窗、來回切換 HWND_TOPMOST/HWND_NOTOPMOST。
//   - 不加 WS_EX_TRANSPARENT/WS_EX_NOACTIVATE，直接在自己的 WndProc 收滑鼠訊息。
// 版面/素材（背景、卡片圖、hover/pressed 圖、圖示對照表）走 mimir_ui.pak/idx +
// mimir_ui.xml，改版面/圖片不用重編 DLL，見 ParseMimirXml。
#include <windows.h>
#include <windowsx.h> // GET_X_LPARAM/GET_Y_LPARAM
#include <gdiplus.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <string>
#include <map>
#include <mutex>
#include <atomic>
#include <vector>
#include <utility>
#include "MimirPowerOverlay.h"
#include "MimirPowerHook.h"
#include "OverlayAssets.h"

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdiplus.lib")

extern HWND g_hGameWnd;

namespace {

const wchar_t *kClassName = L"LauncherDllMimirPowerOverlay";
constexpr UINT WM_SHOW_MIMIR = WM_USER + 200;
constexpr UINT WM_HIDE_MIMIR = WM_USER + 201;
constexpr int TIMER_COUNTDOWN = 1;

struct RectCfg {
  bool has = false;
  RECT rc = {0, 0, 0, 0};
};

struct TripleImage {
  std::string normal, hover, pressed;
};

struct MimirUiConfig {
  bool loaded = false;
  int windowW = 380, windowH = 600;
  bool hasRefSize = false;
  int refW = 0, refH = 0;
  bool hasPos = false;
  int posX = 0, posY = 0;
  std::string bgImage;

  std::wstring titleText = L"日常活躍"; // 日常活躍
  RectCfg titleRect;
  int titleFontSize = 22;
  COLORREF titleColor = RGB(0xC9, 0xA6, 0xFF);
  std::wstring titleFontFamily = L"標楷體";
  bool titleCenter = true;

  RectCfg rowRect; // first row's rect; subsequent rows stack below by (height+spacing)
  int rowSpacing = 8;
  int rowIconSize = 48;
  int rowFontSize = 16;
  COLORREF rowColor = RGB(0xFF, 0xFF, 0xFF);
  TripleImage rowImages;

  RectCfg detailRect;
  int detailIconSize = 64;
  int detailNameFontSize = 18;
  int detailDescFontSize = 14;
  COLORREF detailColor = RGB(0xFF, 0xFF, 0xFF);
  std::string detailBg;

  // 詳情卡下方的 2 欄「效果類型/來源/持續時間...」統計格：純版面佔位，目前封包
  // 沒有帶這些資料，所以是靜態文字（xml 直接寫死 label+value），不是每個選項各
  // 自不同的動態資料。mimir_ui.xml 沒設定任何 <DetailStat> 就完全不畫，之後有
  // 真的欄位/資料來源要顯示，再回頭把這裡改成從封包帶資料。
  RectCfg detailStatsRect;
  int detailStatsFontSize = 12;
  COLORREF detailStatsColor = RGB(0x99, 0x99, 0x99);
  std::vector<std::pair<std::wstring, std::wstring>> detailStats; // (label, value)，最多 6 筆、2 欄 3 列

  // 「選擇將於 hh:mm:ss 後更新」：起始秒數是伺服器送的（見
  // g_countdownTotalSeconds），這裡只是畫面上的排版設定，不是時間來源。
  RectCfg countdownRect;
  int countdownFontSize = 13;
  COLORREF countdownColor = RGB(0xAA, 0xAA, 0xAA);
  std::wstring countdownPrefix = L"選擇將於 "; // 選擇將於
  std::wstring countdownSuffix = L" 後更新";    // 後更新

  RectCfg confirmRect;
  TripleImage confirmImages;
  int confirmFontSize = 18;
  COLORREF confirmColor = RGB(0xFF, 0xFF, 0xFF);
  std::wstring confirmText = L"選擇此能力"; // 選擇此能力

  // 右上角關閉鈕（X）：純粹關掉視窗，不送任何封包。
  RectCfg closeRect;
  TripleImage closeImages;

  std::string defaultIconFile;
  std::map<DWORD, std::string> iconById;
};

std::mutex g_lock;
OverlayAssetSet *g_assets = nullptr;
MimirUiConfig g_cfg;
HWND g_hwnd = NULL;
HWND g_threadHwnd = NULL;
HANDLE g_thread = NULL;
std::atomic<bool> g_visible{false};
double g_scaleX = 1.0, g_scaleY = 1.0;

DWORD g_objid = 0;
MimirOption g_options[MIMIR_OPTION_COUNT];
MimirOption g_defaultDetail;
int g_selectedIndex = 0; // 一開視窗就預選第 0 筆（畫面上第一列亮起），玩家點別列才換
bool g_usingDefaultDetail = true; // 詳情卡目前顯示的是 g_defaultDetail 還是 g_options[g_selectedIndex]
DWORD g_countdownTotalSeconds = 0;
ULONGLONG g_countdownReceivedTickMs = 0;
int g_hoverRow = -1;
bool g_hoverConfirm = false;
bool g_pressedConfirm = false;
bool g_hoverClose = false;
bool g_pressedClose = false;

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

// ---- mimir_ui.xml 解析：跟 OverlayAssets.cpp 的 strings.xml 同一種手工
// line-scan 風格，但 schema 完全不同（卡片清單，不是單一 title/body），所以
// 獨立在這裡解析，不動 OverlayAssets 既有的 disconnect 專用 schema。----

bool ExtractAttr(const char *line, const char *attrName, char *outBuf, size_t outSize) {
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

bool ExtractRect(const char *line, const char *attrName, RectCfg *out) {
  char buf[64];
  int x, y, w, h;
  if (!ExtractAttr(line, attrName, buf, sizeof(buf)))
    return false;
  if (sscanf_s(buf, "%d,%d,%d,%d", &x, &y, &w, &h) != 4)
    return false;
  out->has = true;
  out->rc.left = x;
  out->rc.top = y;
  out->rc.right = x + w;
  out->rc.bottom = y + h;
  return true;
}

COLORREF ExtractColor(const char *line, const char *attrName, COLORREF fallback) {
  char buf[16];
  if (!ExtractAttr(line, attrName, buf, sizeof(buf)))
    return fallback;
  unsigned int rgb = 0;
  if (sscanf_s(buf, "%x", &rgb) != 1)
    return fallback;
  return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
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

void ParseMimirXml(const BYTE *data, size_t len, MimirUiConfig *cfg) {
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

    char buf[256];
    if (strstr(line, "<Window")) {
      int w, h;
      if (ExtractAttr(line, "width", buf, sizeof(buf)))
        cfg->windowW = atoi(buf);
      if (ExtractAttr(line, "height", buf, sizeof(buf)))
        cfg->windowH = atoi(buf);
      if (ExtractAttr(line, "refW", buf, sizeof(buf)) && (w = atoi(buf), w > 0)) {
        cfg->refW = w;
        if (ExtractAttr(line, "refH", buf, sizeof(buf)) && (h = atoi(buf), h > 0)) {
          cfg->refH = h;
          cfg->hasRefSize = true;
        }
      }
      int px, py;
      if (ExtractAttr(line, "posX", buf, sizeof(buf)) && (px = atoi(buf), true)) {
        cfg->posX = px;
        if (ExtractAttr(line, "posY", buf, sizeof(buf)) && (py = atoi(buf), true)) {
          cfg->posY = py;
          cfg->hasPos = true;
        }
      }
      if (ExtractAttr(line, "bg", buf, sizeof(buf)))
        cfg->bgImage = buf;
    } else if (strstr(line, "<Title")) {
      char text[512];
      if (ExtractAttr(line, "text", text, sizeof(text)))
        cfg->titleText = Utf8ToWide(text);
      ExtractRect(line, "rect", &cfg->titleRect);
      if (ExtractAttr(line, "fontSize", buf, sizeof(buf)))
        cfg->titleFontSize = atoi(buf);
      cfg->titleColor = ExtractColor(line, "color", cfg->titleColor);
      if (ExtractAttr(line, "fontFamily", buf, sizeof(buf)))
        cfg->titleFontFamily = Utf8ToWide(buf);
      if (ExtractAttr(line, "align", buf, sizeof(buf)))
        cfg->titleCenter = _stricmp(buf, "center") == 0;
    } else if (strstr(line, "<RowLayout")) {
      ExtractRect(line, "rect", &cfg->rowRect);
      if (ExtractAttr(line, "spacing", buf, sizeof(buf)))
        cfg->rowSpacing = atoi(buf);
      if (ExtractAttr(line, "iconSize", buf, sizeof(buf)))
        cfg->rowIconSize = atoi(buf);
      if (ExtractAttr(line, "fontSize", buf, sizeof(buf)))
        cfg->rowFontSize = atoi(buf);
      cfg->rowColor = ExtractColor(line, "color", cfg->rowColor);
    } else if (strstr(line, "<RowImage")) {
      // normal: base card. hover: glow-border overlay drawn on top of normal
      // for both mouse-hover AND the pending (not-yet-confirmed) selection --
      // there's no separate "selected" art, confirming happens via the
      // bottom button, not by re-coloring the row itself.
      ExtractAttr(line, "normal", buf, sizeof(buf));
      cfg->rowImages.normal = buf;
      if (ExtractAttr(line, "hover", buf, sizeof(buf)))
        cfg->rowImages.hover = buf;
    } else if (strstr(line, "<DetailStats")) {
      // 順序要在 "<Detail" 判斷之前：strstr 是子字串比對，"<DetailStats"/
      // "<DetailStat " 都會命中 "<Detail"，先比對更明確的 tag 名稱。
      ExtractRect(line, "rect", &cfg->detailStatsRect);
      if (ExtractAttr(line, "fontSize", buf, sizeof(buf)))
        cfg->detailStatsFontSize = atoi(buf);
      cfg->detailStatsColor = ExtractColor(line, "color", cfg->detailStatsColor);
    } else if (strstr(line, "<DetailStat ") || strstr(line, "<DetailStat/")) {
      char label[128], value[128];
      if (ExtractAttr(line, "label", label, sizeof(label)) &&
          ExtractAttr(line, "value", value, sizeof(value)) &&
          cfg->detailStats.size() < 6) {
        cfg->detailStats.push_back({Utf8ToWide(label), Utf8ToWide(value)});
      }
    } else if (strstr(line, "<Detail")) {
      ExtractRect(line, "rect", &cfg->detailRect);
      if (ExtractAttr(line, "iconSize", buf, sizeof(buf)))
        cfg->detailIconSize = atoi(buf);
      if (ExtractAttr(line, "nameFontSize", buf, sizeof(buf)))
        cfg->detailNameFontSize = atoi(buf);
      if (ExtractAttr(line, "descFontSize", buf, sizeof(buf)))
        cfg->detailDescFontSize = atoi(buf);
      cfg->detailColor = ExtractColor(line, "color", cfg->detailColor);
      if (ExtractAttr(line, "bg", buf, sizeof(buf)))
        cfg->detailBg = buf;
    } else if (strstr(line, "<Countdown")) {
      ExtractRect(line, "rect", &cfg->countdownRect);
      if (ExtractAttr(line, "fontSize", buf, sizeof(buf)))
        cfg->countdownFontSize = atoi(buf);
      cfg->countdownColor = ExtractColor(line, "color", cfg->countdownColor);
      char text[128];
      if (ExtractAttr(line, "prefix", text, sizeof(text)))
        cfg->countdownPrefix = Utf8ToWide(text);
      if (ExtractAttr(line, "suffix", text, sizeof(text)))
        cfg->countdownSuffix = Utf8ToWide(text);
    } else if (strstr(line, "<ConfirmButton")) {
      ExtractRect(line, "rect", &cfg->confirmRect);
      ExtractAttr(line, "normal", buf, sizeof(buf));
      cfg->confirmImages.normal = buf;
      if (ExtractAttr(line, "hover", buf, sizeof(buf)))
        cfg->confirmImages.hover = buf;
      if (ExtractAttr(line, "pressed", buf, sizeof(buf)))
        cfg->confirmImages.pressed = buf;
      if (ExtractAttr(line, "fontSize", buf, sizeof(buf)))
        cfg->confirmFontSize = atoi(buf);
      cfg->confirmColor = ExtractColor(line, "color", cfg->confirmColor);
      char text[128];
      if (ExtractAttr(line, "text", text, sizeof(text)))
        cfg->confirmText = Utf8ToWide(text);
    } else if (strstr(line, "<CloseButton")) {
      ExtractRect(line, "rect", &cfg->closeRect);
      ExtractAttr(line, "normal", buf, sizeof(buf));
      cfg->closeImages.normal = buf;
      if (ExtractAttr(line, "hover", buf, sizeof(buf)))
        cfg->closeImages.hover = buf;
      if (ExtractAttr(line, "pressed", buf, sizeof(buf)))
        cfg->closeImages.pressed = buf;
    } else if (strstr(line, "<DefaultIcon")) {
      if (ExtractAttr(line, "file", buf, sizeof(buf)))
        cfg->defaultIconFile = buf;
    } else if (strstr(line, "<Icon")) {
      char idBuf[32];
      char file[256];
      if (ExtractAttr(line, "iconId", idBuf, sizeof(idBuf)) &&
          ExtractAttr(line, "file", file, sizeof(file))) {
        cfg->iconById[(DWORD)strtoul(idBuf, nullptr, 10)] = file;
      }
    }
  }
  cfg->loaded = true;
}

void EnsureAssetsLoaded() {
  if (g_assets)
    return;
  g_assets = OverlayAssets_Load("mimir_ui", "mimir_ui");
  if (!g_assets) {
    NetLog("[mimir-ui] no external assets (mimir_ui.pak not deployed?), using built-in layout");
    g_cfg.loaded = true; // fall back to the hardcoded defaults above
    return;
  }
  const BYTE *data = nullptr;
  size_t len = 0;
  if (OverlayAssets_GetRawBytes(g_assets, "mimir_ui.xml", &data, &len)) {
    ParseMimirXml(data, len, &g_cfg);
    NetLog("[mimir-ui] parsed mimir_ui.xml (%u bytes), %u icon mappings", (unsigned)len,
           (unsigned)g_cfg.iconById.size());
  } else {
    NetLog("[mimir-ui] mimir_ui.xml entry missing from idx, using built-in layout");
    g_cfg.loaded = true;
  }
}

Gdiplus::Bitmap *GetImg(const std::string &name) {
  if (name.empty() || !g_assets)
    return nullptr;
  return OverlayAssets_GetBitmap(g_assets, name.c_str());
}

const char *IconFileFor(DWORD iconId) {
  auto it = g_cfg.iconById.find(iconId);
  if (it != g_cfg.iconById.end())
    return it->second.c_str();
  return g_cfg.defaultIconFile.c_str();
}

RECT ScaledRect(const RectCfg &rc) {
  RECT out = {0, 0, 0, 0};
  if (!rc.has)
    return out;
  out.left = (int)(rc.rc.left * g_scaleX + 0.5);
  out.top = (int)(rc.rc.top * g_scaleY + 0.5);
  out.right = (int)(rc.rc.right * g_scaleX + 0.5);
  out.bottom = (int)(rc.rc.bottom * g_scaleY + 0.5);
  return out;
}

// index 從 0 開始，每列往下疊 (height+spacing)。回傳未縮放座標（相對視窗左上）。
RECT RowRectRaw(int index) {
  RECT rc = g_cfg.rowRect.rc;
  int step = (rc.bottom - rc.top) + g_cfg.rowSpacing;
  rc.top += step * index;
  rc.bottom += step * index;
  return rc;
}

RECT RowRectScaled(int index) {
  RECT rc = RowRectRaw(index);
  RECT out;
  out.left = (int)(rc.left * g_scaleX + 0.5);
  out.top = (int)(rc.top * g_scaleY + 0.5);
  out.right = (int)(rc.right * g_scaleX + 0.5);
  out.bottom = (int)(rc.bottom * g_scaleY + 0.5);
  return out;
}

HFONT MakeFont(int ptSize, const std::wstring &family, bool bold) {
  return CreateFontW(ptSize, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, 0, 0, 0,
                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                     PROOF_QUALITY, 0x02, family.c_str());
}

// 卡片列表用：圖示 + 單行名稱（不換行、不顯示 desc）。
void DrawRowContent(Gdiplus::Graphics &g, HDC memDc, const RECT &rc, const MimirOption &opt) {
  int iconSize = (int)(g_cfg.rowIconSize * g_scaleY + 0.5);
  Gdiplus::Bitmap *icon = GetImg(IconFileFor(opt.iconId));
  int textLeft = rc.left + 8;
  if (icon) {
    int iy = rc.top + ((rc.bottom - rc.top) - iconSize) / 2;
    g.DrawImage(icon, rc.left + 8, iy, iconSize, iconSize);
    textLeft = rc.left + 8 + iconSize + 10;
  }
  HFONT font = MakeFont((int)(g_cfg.rowFontSize * g_scaleY + 0.5), g_cfg.titleFontFamily, false);
  HGDIOBJ old = SelectObject(memDc, font);
  SetTextColor(memDc, g_cfg.rowColor);
  SetBkMode(memDc, TRANSPARENT);
  RECT textRc = {textLeft, rc.top, rc.right - 8, rc.bottom};
  DrawTextW(memDc, Utf8ToWide(opt.name).c_str(), -1, &textRc,
            DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
  SelectObject(memDc, old);
  DeleteObject(font);
}

// 詳情卡用：圖示 + 粗體名稱（標題）+ 換行敘述文字（本文）。
void DrawDetailContent(Gdiplus::Graphics &g, HDC memDc, const RECT &rc, const MimirOption &opt) {
  int iconSize = (int)(g_cfg.detailIconSize * g_scaleY + 0.5);
  Gdiplus::Bitmap *icon = GetImg(IconFileFor(opt.iconId));
  int textLeft = rc.left + 12;
  int iconTop = rc.top + 12;
  if (icon) {
    g.DrawImage(icon, rc.left + 12, iconTop, iconSize, iconSize);
    textLeft = rc.left + 12 + iconSize + 12;
  }

  int nameFontSize = (int)(g_cfg.detailNameFontSize * g_scaleY + 0.5);
  HFONT nameFont = MakeFont(nameFontSize, g_cfg.titleFontFamily, true);
  HGDIOBJ oldName = SelectObject(memDc, nameFont);
  SetTextColor(memDc, g_cfg.detailColor);
  SetBkMode(memDc, TRANSPARENT);
  RECT nameRc = {textLeft, rc.top + 12, rc.right - 12, rc.top + 12 + nameFontSize + 8};
  DrawTextW(memDc, Utf8ToWide(opt.name).c_str(), -1, &nameRc,
            DT_SINGLELINE | DT_LEFT | DT_NOPREFIX);
  SelectObject(memDc, oldName);
  DeleteObject(nameFont);

  int descFontSize = (int)(g_cfg.detailDescFontSize * g_scaleY + 0.5);
  HFONT descFont = MakeFont(descFontSize, g_cfg.titleFontFamily, false);
  HGDIOBJ oldDesc = SelectObject(memDc, descFont);
  RECT descRc = {textLeft, nameRc.bottom + 4, rc.right - 12, rc.bottom - 12};
  DrawTextW(memDc, Utf8ToWide(opt.desc).c_str(), -1, &descRc,
            DT_WORDBREAK | DT_LEFT | DT_NOPREFIX);
  SelectObject(memDc, oldDesc);
  DeleteObject(descFont);
}

// 詳情卡下方的 2 欄「◇label：value」統計格，純靜態文字（xml 設定，不是每個選項
//各自不同的動態資料——目前封包沒有帶這些欄位）。mimir_ui.xml 沒設定任何
// <DetailStat> 就完全不畫。
void DrawDetailStats(Gdiplus::Graphics &g, HDC memDc) {
  if (g_cfg.detailStats.empty())
    return;
  RECT rc = ScaledRect(g_cfg.detailStatsRect);
  if (rc.right <= rc.left)
    return;

  int fontSize = (int)(g_cfg.detailStatsFontSize * g_scaleY + 0.5);
  HFONT font = MakeFont(fontSize, g_cfg.titleFontFamily, false);
  HGDIOBJ old = SelectObject(memDc, font);
  SetTextColor(memDc, g_cfg.detailStatsColor);
  SetBkMode(memDc, TRANSPARENT);

  int colW = (rc.right - rc.left) / 2;
  int rowH = (fontSize + 8);
  for (size_t i = 0; i < g_cfg.detailStats.size(); i++) {
    int col = (int)(i % 2);
    int row = (int)(i / 2);
    RECT cellRc = {rc.left + col * colW, rc.top + row * rowH,
                   rc.left + (col + 1) * colW, rc.top + (row + 1) * rowH};
    std::wstring line = L"◇" + g_cfg.detailStats[i].first + L"：" +
                        g_cfg.detailStats[i].second; // ◇label：value
    DrawTextW(memDc, line.c_str(), -1, &cellRc, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
  }
  SelectObject(memDc, old);
  DeleteObject(font);
}

std::wstring CountdownText() {
  ULONGLONG elapsedMs = GetTickCount64() - g_countdownReceivedTickMs;
  ULONGLONG elapsedSec = elapsedMs / 1000;
  DWORD remaining = (elapsedSec < g_countdownTotalSeconds)
                        ? (DWORD)(g_countdownTotalSeconds - elapsedSec)
                        : 0;
  int hh = remaining / 3600;
  int mm = (remaining % 3600) / 60;
  int ss = remaining % 60;
  wchar_t buf[32];
  swprintf_s(buf, L"%02d:%02d:%02d", hh, mm, ss);
  return g_cfg.countdownPrefix + buf + g_cfg.countdownSuffix;
}

void DrawInto(HDC memDc, void *bits, int winW, int winH) {
  Gdiplus::Bitmap dest(winW, winH, winW * 4, PixelFormat32bppPARGB, (BYTE *)bits);
  Gdiplus::Graphics g(&dest);
  g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

  Gdiplus::Bitmap *bg = GetImg(g_cfg.bgImage);
  if (bg) {
    g.DrawImage(bg, 0, 0, winW, winH);
  } else {
    HBRUSH bgBrush = CreateSolidBrush(RGB(0x18, 0x14, 0x20));
    RECT full = {0, 0, winW, winH};
    FillRect(memDc, &full, bgBrush);
    DeleteObject(bgBrush);
  }

  // Title
  RECT titleRc = ScaledRect(g_cfg.titleRect);
  if (titleRc.right == 0)
    titleRc = {0, (int)(16 * g_scaleY), winW, (int)(50 * g_scaleY)};
  HFONT titleFont = MakeFont((int)(g_cfg.titleFontSize * g_scaleY + 0.5), g_cfg.titleFontFamily, true);
  HGDIOBJ oldFont = SelectObject(memDc, titleFont);
  SetTextColor(memDc, g_cfg.titleColor);
  SetBkMode(memDc, TRANSPARENT);
  DrawTextW(memDc, g_cfg.titleText.c_str(), -1, &titleRc,
            (g_cfg.titleCenter ? DT_CENTER : DT_LEFT) | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  SelectObject(memDc, oldFont);
  DeleteObject(titleFont);

  // Rows: base card always drawn first, then (if hovered OR the pending
  // selection) the glow-border image is overlaid on top of it -- not a full
  // swap, so the glow reads as "lit up", not a different card. The same
  // glow asset serves both hover and pending-selection; there's no separate
  // "selected" art (confirming happens via the bottom button, not per-row).
  for (int i = 0; i < MIMIR_OPTION_COUNT; i++) {
    RECT rc = RowRectScaled(i);
    Gdiplus::Bitmap *rowBg = GetImg(g_cfg.rowImages.normal);
    if (rowBg) {
      g.DrawImage(rowBg, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
    }
    bool lit = (i == g_hoverRow || i == g_selectedIndex);
    if (lit) {
      Gdiplus::Bitmap *glow = GetImg(g_cfg.rowImages.hover);
      if (glow)
        g.DrawImage(glow, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
      else if (!rowBg) {
        HBRUSH hl = CreateSolidBrush(RGB(0x3A, 0x2C, 0x4A));
        FillRect(memDc, &rc, hl);
        DeleteObject(hl);
      }
    } else if (!rowBg) {
      HBRUSH hl = CreateSolidBrush(RGB(0x24, 0x1E, 0x2E));
      FillRect(memDc, &rc, hl);
      DeleteObject(hl);
    }
    DrawRowContent(g, memDc, rc, g_options[i]);
  }

  // Detail: g_usingDefaultDetail 時顯示伺服器送的 defaultDetail（視窗一開就有，
  // 不用等玩家點），玩家點了某一筆卡片後改顯示那筆 options[] 的資料。
  {
    RECT rc = ScaledRect(g_cfg.detailRect);
    if (rc.right > rc.left) {
      Gdiplus::Bitmap *detailBg = GetImg(g_cfg.detailBg);
      if (detailBg)
        g.DrawImage(detailBg, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
      const MimirOption &shown =
          g_usingDefaultDetail ? g_defaultDetail : g_options[g_selectedIndex];
      DrawDetailContent(g, memDc, rc, shown);
    }
  }
  DrawDetailStats(g, memDc);

  // Countdown（起始秒數是伺服器送的，見 g_countdownTotalSeconds，client 只負責
  // 每秒往下扣顯示）
  RECT cdRc = ScaledRect(g_cfg.countdownRect);
  if (cdRc.right > cdRc.left) {
    HFONT cdFont = MakeFont((int)(g_cfg.countdownFontSize * g_scaleY + 0.5), g_cfg.titleFontFamily, false);
    HGDIOBJ oldCd = SelectObject(memDc, cdFont);
    SetTextColor(memDc, g_cfg.countdownColor);
    std::wstring cd = CountdownText();
    DrawTextW(memDc, cd.c_str(), -1, &cdRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(memDc, oldCd);
    DeleteObject(cdFont);
  }

  // Confirm button
  RECT btnRc = ScaledRect(g_cfg.confirmRect);
  if (btnRc.right > btnRc.left) {
    Gdiplus::Bitmap *btnImg = g_pressedConfirm ? GetImg(g_cfg.confirmImages.pressed)
                              : g_hoverConfirm ? GetImg(g_cfg.confirmImages.hover)
                                               : GetImg(g_cfg.confirmImages.normal);
    if (btnImg) {
      g.DrawImage(btnImg, btnRc.left, btnRc.top, btnRc.right - btnRc.left, btnRc.bottom - btnRc.top);
    } else {
      HBRUSH b = CreateSolidBrush(g_pressedConfirm ? RGB(0x4A, 0x2C, 0x72) : RGB(0x6A, 0x3C, 0x9A));
      FillRect(memDc, &btnRc, b);
      DeleteObject(b);
    }
    HFONT btnFont = MakeFont((int)(g_cfg.confirmFontSize * g_scaleY + 0.5), g_cfg.titleFontFamily, true);
    HGDIOBJ oldBtn = SelectObject(memDc, btnFont);
    SetTextColor(memDc, g_cfg.confirmColor);
    DrawTextW(memDc, g_cfg.confirmText.c_str(), -1, &btnRc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(memDc, oldBtn);
    DeleteObject(btnFont);
  }

  // Close button (top-right X): dismisses the window, sends nothing.
  RECT closeRc = ScaledRect(g_cfg.closeRect);
  if (closeRc.right > closeRc.left) {
    Gdiplus::Bitmap *closeImg = g_pressedClose ? GetImg(g_cfg.closeImages.pressed)
                                : g_hoverClose  ? GetImg(g_cfg.closeImages.hover)
                                                : GetImg(g_cfg.closeImages.normal);
    if (closeImg) {
      g.DrawImage(closeImg, closeRc.left, closeRc.top, closeRc.right - closeRc.left,
                  closeRc.bottom - closeRc.top);
    } else {
      HBRUSH b = CreateSolidBrush(g_pressedClose ? RGB(0x5A, 0x1A, 0x1A)
                                  : g_hoverClose  ? RGB(0x8A, 0x2A, 0x2A)
                                                  : RGB(0x50, 0x50, 0x50));
      FillRect(memDc, &closeRc, b);
      DeleteObject(b);
      HFONT xFont = MakeFont((int)(16 * g_scaleY + 0.5), g_cfg.titleFontFamily, true);
      HGDIOBJ oldX = SelectObject(memDc, xFont);
      SetTextColor(memDc, RGB(0xFF, 0xFF, 0xFF));
      DrawTextW(memDc, L"X", -1, &closeRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      SelectObject(memDc, oldX);
      DeleteObject(xFont);
    }
  }
}

void ComputeWinSize(int *outW, int *outH) {
  g_scaleX = 1.0;
  g_scaleY = 1.0;
  if (g_cfg.hasRefSize) {
    HWND game = g_hGameWnd;
    RECT rc;
    if (game && IsWindow(game) && GetClientRect(game, &rc) && rc.right > 0 && rc.bottom > 0) {
      g_scaleX = (double)rc.right / (double)g_cfg.refW;
      g_scaleY = (double)rc.bottom / (double)g_cfg.refH;
    }
  }
  *outW = (int)(g_cfg.windowW * g_scaleX + 0.5);
  *outH = (int)(g_cfg.windowH * g_scaleY + 0.5);
}

void PositionWindow(HWND hwnd, int winW, int winH) {
  HWND game = g_hGameWnd;
  int x, y;
  if (game && IsWindow(game)) {
    RECT rc;
    GetClientRect(game, &rc);
    POINT tl = {0, 0};
    ClientToScreen(game, &tl);
    if (g_cfg.hasPos) {
      x = tl.x + (int)(g_cfg.posX * g_scaleX + 0.5);
      y = tl.y + (int)(g_cfg.posY * g_scaleY + 0.5);
    } else {
      x = tl.x + (rc.right - winW) / 2;
      y = tl.y + (rc.bottom - winH) / 2;
    }
  } else {
    x = 100;
    y = 100;
  }
  SetWindowPos(hwnd, NULL, x, y, winW, winH, SWP_NOZORDER | SWP_NOACTIVATE);
}

void PaintLayered(HWND hwnd) {
  int winW, winH;
  ComputeWinSize(&winW, &winH);
  PositionWindow(hwnd, winW, winH);

  HDC screenDc = GetDC(NULL);
  if (!screenDc)
    return;
  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = winW;
  bmi.bmiHeader.biHeight = -winH;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;
  void *bitsPtr = nullptr;
  HBITMAP bmp = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bitsPtr, NULL, 0);
  if (!bmp) {
    ReleaseDC(NULL, screenDc);
    return;
  }
  HDC memDc = CreateCompatibleDC(screenDc);
  HGDIOBJ oldBmp = SelectObject(memDc, bmp);
  DrawInto(memDc, bitsPtr, winW, winH);

  // No real background art loaded -> force opaque so ULW_ALPHA doesn't show
  // the fallback fill as see-through.
  if (!GetImg(g_cfg.bgImage)) {
    BYTE *bits = (BYTE *)bitsPtr;
    size_t total = (size_t)winW * winH * 4;
    for (size_t i = 3; i < total; i += 4)
      bits[i] = 0xFF;
  }

  POINT ptSrc = {0, 0};
  RECT wndRc;
  GetWindowRect(hwnd, &wndRc);
  POINT ptDst = {wndRc.left, wndRc.top};
  SIZE sz = {winW, winH};
  BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  UpdateLayeredWindow(hwnd, screenDc, &ptDst, &sz, memDc, &ptSrc, 0, &blend, ULW_ALPHA);

  SelectObject(memDc, oldBmp);
  DeleteObject(bmp);
  DeleteDC(memDc);
  ReleaseDC(NULL, screenDc);
}

int HitTestRow(int x, int y) {
  for (int i = 0; i < MIMIR_OPTION_COUNT; i++) {
    RECT rc = RowRectScaled(i);
    if (x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom)
      return i;
  }
  return -1;
}

bool HitTestConfirm(int x, int y) {
  RECT rc = ScaledRect(g_cfg.confirmRect);
  return rc.right > rc.left && x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom;
}

bool HitTestClose(int x, int y) {
  RECT rc = ScaledRect(g_cfg.closeRect);
  return rc.right > rc.left && x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom;
}

void HideWindow(HWND hwnd) {
  g_visible.store(false);
  KillTimer(hwnd, TIMER_COUNTDOWN);
  ShowWindow(hwnd, SW_HIDE);
}

LRESULT CALLBACK MimirWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
  case WM_SHOW_MIMIR: {
    g_visible.store(true);
    g_selectedIndex = 0; // 預選第一項，跟 g_usingDefaultDetail 一起讓詳情卡一開就有東西
    g_usingDefaultDetail = true;
    g_hoverRow = -1;
    g_hoverConfirm = false;
    g_pressedConfirm = false;
    g_hoverClose = false;
    g_pressedClose = false;
    ShowWindow(hwnd, SW_SHOW);
    PaintLayered(hwnd);
    SetTimer(hwnd, TIMER_COUNTDOWN, 1000, NULL);
    NetLog("[mimir-ui] shown objid=0x%08X countdown=%us", g_objid,
           (unsigned)g_countdownTotalSeconds);
    return 0;
  }
  case WM_HIDE_MIMIR:
    if (g_visible.load())
      HideWindow(hwnd);
    return 0;
  case WM_MOUSEMOVE: {
    int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
    int row = HitTestRow(x, y);
    bool onConfirm = HitTestConfirm(x, y);
    bool onClose = HitTestClose(x, y);
    if (row != g_hoverRow || onConfirm != g_hoverConfirm || onClose != g_hoverClose) {
      g_hoverRow = row;
      g_hoverConfirm = onConfirm;
      g_hoverClose = onClose;
      PaintLayered(hwnd);
    }
    TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
    TrackMouseEvent(&tme);
    return 0;
  }
  case WM_MOUSELEAVE:
    g_hoverRow = -1;
    g_hoverConfirm = false;
    g_hoverClose = false;
    PaintLayered(hwnd);
    return 0;
  case WM_LBUTTONDOWN: {
    int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
    if (HitTestConfirm(x, y)) {
      g_pressedConfirm = true;
      SetCapture(hwnd);
      PaintLayered(hwnd);
    } else if (HitTestClose(x, y)) {
      g_pressedClose = true;
      SetCapture(hwnd);
      PaintLayered(hwnd);
    }
    return 0;
  }
  case WM_LBUTTONUP: {
    int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
    if (g_pressedConfirm) {
      g_pressedConfirm = false;
      ReleaseCapture();
      if (HitTestConfirm(x, y)) {
        DWORD objid = g_objid;
        BYTE idx = (BYTE)g_selectedIndex;
        NetLog("[mimir-ui] confirm clicked, selectedIndex=%d name=%s", g_selectedIndex,
               g_options[g_selectedIndex].name);
        HideWindow(hwnd);
        MimirPowerHook_SendChoice(objid, idx);
        return 0;
      }
      PaintLayered(hwnd);
      return 0;
    }
    if (g_pressedClose) {
      g_pressedClose = false;
      ReleaseCapture();
      if (HitTestClose(x, y)) {
        NetLog("[mimir-ui] close button clicked, dismiss without sending");
        HideWindow(hwnd);
        return 0;
      }
      PaintLayered(hwnd);
      return 0;
    }
    int row = HitTestRow(x, y);
    if (row >= 0) {
      g_selectedIndex = row;
      g_usingDefaultDetail = false;
      PaintLayered(hwnd);
    }
    return 0;
  }
  case WM_TIMER:
    if (wp == TIMER_COUNTDOWN && g_visible.load())
      PaintLayered(hwnd);
    return 0;
  case WM_PAINT:
    ValidateRect(hwnd, NULL);
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  default:
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

DWORD WINAPI OverlayThreadProc(void *) {
  HINSTANCE hinst = GetModuleHandleW(NULL);
  WNDCLASSEXW cls = {};
  cls.cbSize = sizeof(cls);
  cls.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
  cls.lpfnWndProc = MimirWndProc;
  cls.hInstance = hinst;
  cls.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512)); // IDC_ARROW
  cls.hbrBackground = NULL;
  cls.lpszClassName = kClassName;
  RegisterClassExW(&cls);

  // hWndOwner = 遊戲主視窗：owned popup 會自動疊在 owner 正上方、隨 owner 一起
  // 最小化/還原，不用像 DisconnectOverlay 那樣手動維護 Z 序。這個視窗要能被點
  // 擊/取得焦點，所以不加 WS_EX_TRANSPARENT/WS_EX_NOACTIVATE。
  HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, kClassName,
                              L"MimirPower", WS_POPUP, 0, 0, g_cfg.windowW,
                              g_cfg.windowH, g_hGameWnd, NULL, hinst, NULL);
  if (!hwnd) {
    NetLog("[mimir-ui] CreateWindowExW failed err=%u", GetLastError());
    return 1;
  }
  {
    std::lock_guard<std::mutex> lock(g_lock);
    g_hwnd = hwnd;
    g_threadHwnd = hwnd;
  }
  NetLog("[mimir-ui] overlay thread ready hwnd=0x%p", hwnd);

  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return 0;
}

bool StartThread() {
  if (g_thread)
    return true;
  EnsureAssetsLoaded();
  g_thread = CreateThread(NULL, 0, OverlayThreadProc, NULL, 0, NULL);
  if (!g_thread) {
    NetLog("[mimir-ui] CreateThread failed");
    return false;
  }
  for (int i = 0; i < 100 && !g_hwnd; i++)
    Sleep(10);
  return g_hwnd != NULL;
}

} // namespace

void MimirPowerOverlay_Show(DWORD objid, const MimirOption options[MIMIR_OPTION_COUNT],
                            const MimirOption &defaultDetail, DWORD countdownSeconds) {
  HWND hwnd = NULL;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    g_objid = objid;
    memcpy(g_options, options, sizeof(MimirOption) * MIMIR_OPTION_COUNT);
    g_defaultDetail = defaultDetail;
    g_countdownTotalSeconds = countdownSeconds;
    g_countdownReceivedTickMs = GetTickCount64();
    hwnd = g_threadHwnd;
  }
  if (!hwnd) {
    StartThread();
    std::lock_guard<std::mutex> lock(g_lock);
    hwnd = g_threadHwnd;
  }
  if (hwnd)
    PostMessageW(hwnd, WM_SHOW_MIMIR, 0, 0);
  else
    NetLog("[mimir-ui] Show failed: no hwnd objid=0x%08X", objid);
}
