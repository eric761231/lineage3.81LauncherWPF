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
  std::wstring titleFontFamily = L"微軟正黑體";
  bool titleCenter = true;

  RectCfg rowRect; // first row's rect; subsequent rows stack below by (height+spacing)
  int rowSpacing = 8;
  int rowIconSize = 48;
  int rowFontSize = 16;
  int rowTextLeftMin = 110; // 選項列文字離左邊界至少多遠，XML 可調，不用重編 DLL
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
// 玩家拖曳過視窗之後設 true：PositionWindow 之後就不再把視窗拉回置中/設定位置，
// 不然 PaintLayered 幾乎每次滑鼠移動、每秒倒數都會重畫，若每次都重新置中，玩家
// 剛拖完手一放視窗就彈回去，等於永遠拖不動。這個 flag 故意不會在 WM_SHOW_MIMIR
// 時重置——視窗只是 SW_HIDE，位置本來就還在，讓玩家下次重新打開時停在上次拖過
// 的地方（同一次遊戲 session 內），不用每次都重新閃開背包等原生視窗一次。
bool g_userMoved = false;

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
  // 密米爾之泉功能已經確認穩定運作，暫時關掉這個檔案的 log，要恢復就把這行 return
  // 拿掉。
  return;
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

// Java 端 writeS() 送出的字串是 Big5（碼頁 950）雙位元組編碼，不是 UTF-8（這點
// 是從真實封包 dump 逐 byte 比對確認的）。密米爾 name/desc 這兩個欄位是直接從
// wire 讀出來的原始文字，要用這個轉換，不能用上面的 Utf8ToWide；mimir_ui.xml
// 本身是我們自己準備的檔案，維持存 UTF-8，繼續用 Utf8ToWide。
std::wstring Big5ToWide(const char *big5) {
  if (!big5 || !big5[0])
    return std::wstring();
  int chars = MultiByteToWideChar(950, 0, big5, -1, NULL, 0);
  if (chars <= 0)
    return std::wstring();
  std::wstring w;
  w.resize((size_t)chars - 1);
  MultiByteToWideChar(950, 0, big5, -1, &w[0], chars);
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
      if (ExtractAttr(line, "textLeftMin", buf, sizeof(buf)))
        cfg->rowTextLeftMin = atoi(buf);
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

// 沒有點陣字/描邊字型可用（原生介面的字型檔是遊戲自己的舊版點陣字格式，測過
// AddFontResourceEx 載不進來），改用系統粗體字＋經典 GDI 描邊技巧模擬：先用外框
// 色在上下左右四個方向各偏移 1px 畫一次，最後再用填色在正中央畫一次蓋上去，
// 讓文字在淺色背景/深色背景下都比純色文字更好辨識。字型選取/呼叫端（HFONT 已
// SelectObject 進 memDc）不變，這裡只是把單次 DrawTextW 換成 5 次。
void DrawOutlinedText(HDC memDc, const RECT &rc, const std::wstring &text,
                      COLORREF fillColor, COLORREF outlineColor, UINT dtFlags) {
  static const int kOffsets[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
  SetTextColor(memDc, outlineColor);
  for (auto &off : kOffsets) {
    RECT r = rc;
    OffsetRect(&r, off[0], off[1]);
    DrawTextW(memDc, text.c_str(), -1, &r, dtFlags);
  }
  SetTextColor(memDc, fillColor);
  RECT r = rc;
  DrawTextW(memDc, text.c_str(), -1, &r, dtFlags);
}

// 卡片列表用：圖示 + 單行名稱（不換行、不顯示 desc）。
void DrawRowContent(Gdiplus::Graphics &g, HDC memDc, const RECT &rc, const MimirOption &opt) {
  int iconSize = (int)(g_cfg.rowIconSize * g_scaleY + 0.5);
  Gdiplus::Bitmap *icon = GetImg(IconFileFor(opt.iconId));
  // 文字至少要離列左邊界 g_cfg.rowTextLeftMin px（xml <RowLayout textLeftMin="..">
  // 可調，不用重編 DLL），不管圖示有沒有載入成功都一樣（圖示本身較寬時用圖示
  // 實際寬度往右推，避免壓到圖示）。
  int minTextLeft = rc.left + g_cfg.rowTextLeftMin;
  int textLeft = minTextLeft;
  if (icon) {
    int iy = rc.top + ((rc.bottom - rc.top) - iconSize) / 2;
    g.DrawImage(icon, rc.left + 8, iy, iconSize, iconSize);
    textLeft = max(rc.left + 8 + iconSize + 10, minTextLeft);
  }
  HFONT font = MakeFont((int)(g_cfg.rowFontSize * g_scaleY + 0.5), g_cfg.titleFontFamily, false);
  HGDIOBJ old = SelectObject(memDc, font);
  SetBkMode(memDc, TRANSPARENT);
  RECT textRc = {textLeft, rc.top, rc.right - 8, rc.bottom};
  DrawOutlinedText(memDc, textRc, Big5ToWide(opt.name), g_cfg.rowColor, RGB(0, 0, 0),
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
  SetBkMode(memDc, TRANSPARENT);
  RECT nameRc = {textLeft, rc.top + 12, rc.right - 12, rc.top + 12 + nameFontSize + 8};
  DrawOutlinedText(memDc, nameRc, Big5ToWide(opt.name), g_cfg.detailColor, RGB(0, 0, 0),
                   DT_SINGLELINE | DT_LEFT | DT_NOPREFIX);
  SelectObject(memDc, oldName);
  DeleteObject(nameFont);

  int descFontSize = (int)(g_cfg.detailDescFontSize * g_scaleY + 0.5);
  HFONT descFont = MakeFont(descFontSize, g_cfg.titleFontFamily, false);
  HGDIOBJ oldDesc = SelectObject(memDc, descFont);
  RECT descRc = {textLeft, nameRc.bottom + 4, rc.right - 12, rc.bottom - 12};
  DrawOutlinedText(memDc, descRc, Big5ToWide(opt.desc), g_cfg.detailColor, RGB(0, 0, 0),
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
    DrawOutlinedText(memDc, cellRc, line, g_cfg.detailStatsColor, RGB(0, 0, 0),
                     DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
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

  // Title：背景圖（bg）本身已經把標題文字畫進去了，只有背景圖沒載入成功時才
  // 用程式碼畫的文字當備援，不然會跟圖裡的字疊成兩份。
  SetBkMode(memDc, TRANSPARENT);
  if (!bg) {
    RECT titleRc = ScaledRect(g_cfg.titleRect);
    if (titleRc.right == 0)
      titleRc = {0, (int)(16 * g_scaleY), winW, (int)(50 * g_scaleY)};
    HFONT titleFont = MakeFont((int)(g_cfg.titleFontSize * g_scaleY + 0.5), g_cfg.titleFontFamily, true);
    HGDIOBJ oldFont = SelectObject(memDc, titleFont);
    DrawOutlinedText(memDc, titleRc, g_cfg.titleText, g_cfg.titleColor, RGB(0, 0, 0),
                     (g_cfg.titleCenter ? DT_CENTER : DT_LEFT) | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(memDc, oldFont);
    DeleteObject(titleFont);
  }

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
    std::wstring cd = CountdownText();
    DrawOutlinedText(memDc, cdRc, cd, g_cfg.countdownColor, RGB(0, 0, 0),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(memDc, oldCd);
    DeleteObject(cdFont);
  }

  // Confirm button
  RECT btnRc = ScaledRect(g_cfg.confirmRect);
  if (btnRc.right > btnRc.left) {
    // hover/pressed 沒對應圖（例如只準備了 normal+pressed 兩張，沒有另外做 hover
    // 圖）就退回 normal 圖，不要直接跳去下面純色方塊 fallback——純色方塊只在連
    // normal 圖都沒載入成功時才該出現。
    Gdiplus::Bitmap *normalImg = GetImg(g_cfg.confirmImages.normal);
    Gdiplus::Bitmap *btnImg =
        g_pressedConfirm
            ? (GetImg(g_cfg.confirmImages.pressed) ? GetImg(g_cfg.confirmImages.pressed) : normalImg)
        : g_hoverConfirm ? (GetImg(g_cfg.confirmImages.hover) ? GetImg(g_cfg.confirmImages.hover) : normalImg)
                         : normalImg;
    if (btnImg) {
      // 素材圖本身已經把「選擇此能力」畫進去了，圖有載入成功就不用再疊一次文字
      // 上去，不然會變成兩份字疊在一起。
      g.DrawImage(btnImg, btnRc.left, btnRc.top, btnRc.right - btnRc.left, btnRc.bottom - btnRc.top);
    } else {
      HBRUSH b = CreateSolidBrush(g_pressedConfirm ? RGB(0x4A, 0x2C, 0x72) : RGB(0x6A, 0x3C, 0x9A));
      FillRect(memDc, &btnRc, b);
      DeleteObject(b);
      HFONT btnFont = MakeFont((int)(g_cfg.confirmFontSize * g_scaleY + 0.5), g_cfg.titleFontFamily, true);
      HGDIOBJ oldBtn = SelectObject(memDc, btnFont);
      DrawOutlinedText(memDc, btnRc, g_cfg.confirmText, g_cfg.confirmColor, RGB(0, 0, 0),
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      SelectObject(memDc, oldBtn);
      DeleteObject(btnFont);
    }
  }

  // Close button (top-right X): dismisses the window, sends nothing. 只有一張
  // 素材圖（沒有另外的 hover/pressed 圖），按下時直接把同一張圖往下移 1-2px 當
  // 互動回饋，不需要額外素材。
  RECT closeRc = ScaledRect(g_cfg.closeRect);
  if (closeRc.right > closeRc.left) {
    Gdiplus::Bitmap *closeImg = GetImg(g_cfg.closeImages.normal);
    if (closeImg) {
      int pressOffset = g_pressedClose ? max(1, (int)(2 * g_scaleY + 0.5)) : 0;
      // 素材圖只有 33x32，跟顯示的方框幾乎 1:1，但整體 UI 又會依遊戲視窗大小再
      // 縮放一次（g_scaleX/Y），縮放比例不是整數倍時，全域設定的
      // HighQualityBicubic 對這種小圖示還是會糊。小圖示邊緣（X 字、金色外框）都
      // 是硬邊，改用最近鄰內插比較銳利，畫完再切回原本的內插模式，不影響背景圖
      // 等其他元素。
      g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
      g.DrawImage(closeImg, closeRc.left, closeRc.top + pressOffset, closeRc.right - closeRc.left,
                  closeRc.bottom - closeRc.top);
      g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    } else {
      HBRUSH b = CreateSolidBrush(g_pressedClose ? RGB(0x5A, 0x1A, 0x1A)
                                  : g_hoverClose  ? RGB(0x8A, 0x2A, 0x2A)
                                                  : RGB(0x50, 0x50, 0x50));
      FillRect(memDc, &closeRc, b);
      DeleteObject(b);
      HFONT xFont = MakeFont((int)(16 * g_scaleY + 0.5), g_cfg.titleFontFamily, true);
      HGDIOBJ oldX = SelectObject(memDc, xFont);
      DrawOutlinedText(memDc, closeRc, L"X", RGB(0xFF, 0xFF, 0xFF), RGB(0, 0, 0),
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
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
  if (g_userMoved) {
    // 玩家已經手動拖過，尊重目前位置，只更新尺寸（正常情況尺寸在同一次顯示中
    // 不會變，但遊戲視窗被拉伸縮放時 winW/winH 可能跟著變，還是要跟上）。
    RECT cur;
    GetWindowRect(hwnd, &cur);
    SetWindowPos(hwnd, NULL, cur.left, cur.top, winW, winH, SWP_NOZORDER | SWP_NOACTIVATE);
    return;
  }
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

  // 視窗本來就是矩形面板，不需要圓角/羽化透明邊緣。不管背景圖有沒有載入成功、
  // 不管列高亮/按鈕文字這些 GDI fallback 有沒有補上 alpha，這裡一律把整個
  // buffer 的 alpha 都設成 255，避免任何區域因為漏設 alpha 而穿幫透出遊戲畫面。
  {
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

// 標題列當拖曳把手：滑鼠在這個區域按住拖曳可以移動整個視窗（見 WM_NCHITTEST）。
// 排除掉右上角關閉鈕那塊，不然關閉鈕會被拖曳搶走點擊。標題文字現在畫進背景圖
// 裡了，xml 不一定會設 <Title> 標籤（g_cfg.titleRect 可能沒設），拖曳熱區不能
// 跟著消失，沒設就跟 DrawInto 同一套 fallback（視窗最上面那一條）。
bool HitTestTitleDragZone(int x, int y) {
  RECT rc = ScaledRect(g_cfg.titleRect);
  if (rc.right <= rc.left) {
    RECT client;
    GetClientRect(g_hwnd, &client);
    rc = {0, (int)(16 * g_scaleY), client.right, (int)(50 * g_scaleY)};
  }
  if (!(x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom))
    return false;
  return !HitTestClose(x, y);
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
    // 不重置 g_userMoved：視窗只是 SW_HIDE 不是銷毀，GetWindowRect 本來就還留著
    // 玩家上次拖曳到的位置，PositionWindow 看到 g_userMoved==true 就會沿用那個
    // 位置（只補尺寸），這樣同一次遊戲 session 內重新打開會停在玩家上次拖過的
    // 地方，不會每次都彈回預設位置去擋到背包之類的原生視窗。
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
    NetLog("[mimir-ui] shown countdown=%us", (unsigned)g_countdownTotalSeconds);
    return 0;
  }
  case WM_HIDE_MIMIR:
    if (g_visible.load())
      HideWindow(hwnd);
    return 0;
  case WM_NCHITTEST: {
    // 標題列區域回報 HTCAPTION，讓 Windows 用內建的視窗拖曳機制處理移動（不用
    // 自己手動追蹤滑鼠位移），其他區域照正常方式判斷（按鈕/清單才收得到滑鼠
    // 訊息）。lp 在 WM_NCHITTEST 是螢幕座標，要先轉成這個視窗的 client 座標。
    POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    ScreenToClient(hwnd, &pt);
    if (HitTestTitleDragZone(pt.x, pt.y))
      return HTCAPTION;
    return HTCLIENT;
  }
  case WM_WINDOWPOSCHANGING: {
    // 標題列拖曳走的是 Windows 內建的 HTCAPTION 移動，中途每一步都會先送這個
    // 訊息，在真正移動生效「之前」把座標夾在遊戲視窗範圍內，玩家會感覺拖到邊界
    // 就卡住，而不是拖出去之後才彈回來（用 WM_MOVE 事後修正會抖動）。只在視窗
    // 真的要移動（沒設 SWP_NOMOVE）且已經顯示時夾範圍，避免影響 PositionWindow
    // 自己算好座標的初始定位（那時視窗還沒 SW_SHOW，或者是程式主動置中/回復
    // 拖曳位置，不是玩家正在拖）。
    WINDOWPOS *wp = (WINDOWPOS *)lp;
    if (g_visible.load() && !(wp->flags & SWP_NOMOVE) && IsWindowVisible(hwnd)) {
      HWND game = g_hGameWnd;
      if (game && IsWindow(game)) {
        RECT grc;
        GetClientRect(game, &grc);
        POINT tl = {0, 0};
        ClientToScreen(game, &tl);
        int minX = tl.x, minY = tl.y;
        int maxX = tl.x + grc.right - wp->cx;
        int maxY = tl.y + grc.bottom - wp->cy;
        if (maxX < minX)
          maxX = minX;
        if (maxY < minY)
          maxY = minY;
        if (wp->x < minX)
          wp->x = minX;
        if (wp->x > maxX)
          wp->x = maxX;
        if (wp->y < minY)
          wp->y = minY;
        if (wp->y > maxY)
          wp->y = maxY;
      }
    }
    return 0;
  }
  case WM_MOVE:
    // 只要視窗移動過（不管是不是使用者手動拖的，SetWindowPos 也會觸發，但那些
    // 情況下座標本來就沒變，多設一次 g_userMoved 也無害），之後 PositionWindow
    // 就不再把視窗拉回預設位置。
    if (g_visible.load())
      g_userMoved = true;
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
        BYTE idx = (BYTE)g_selectedIndex;
        NetLog("[mimir-ui] confirm clicked, selectedIndex=%d name=%s", g_selectedIndex,
               g_options[g_selectedIndex].name);
        HideWindow(hwnd);
        MimirPowerHook_SendChoice(idx);
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

void MimirPowerOverlay_Show(const MimirOption options[MIMIR_OPTION_COUNT],
                            const MimirOption &defaultDetail, DWORD countdownSeconds) {
  HWND hwnd = NULL;
  {
    std::lock_guard<std::mutex> lock(g_lock);
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
    NetLog("[mimir-ui] Show failed: no hwnd");
}
