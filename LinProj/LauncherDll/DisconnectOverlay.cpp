// DisconnectOverlay.cpp: WS_EX_LAYERED + UpdateLayeredWindow disconnect dialog.
// Align with LineageIme Overlay: do NOT use SetLayeredWindowAttributes.
#include <windows.h>
#include <gdiplus.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <atomic>
#include <mutex>
#include "DisconnectOverlay.h"
#include "OverlayAssets.h"

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdiplus.lib")

extern HWND g_hGameWnd;

namespace {

const wchar_t *kClassName = L"LauncherDllDisconnectOverlay";
constexpr UINT WM_SHOW_DISCONNECT = WM_USER + 100;
constexpr UINT WM_UPDATE_TEXT = WM_USER + 101;
constexpr UINT WM_HIDE_OVERLAY = WM_USER + 102;
const char *kScreen = "disconnect";
const char *kBgPng = "bg_disconnect.png";
// Used only when disconnect_ui.pak isn't deployed / an entry is missing.
constexpr int FALLBACK_WIN_W = 360;
constexpr int FALLBACK_WIN_H = 160;
constexpr int FONT_SIZE = 20;
constexpr COLORREF BG_COLOR = 0x00202020;
constexpr COLORREF BORDER_COLOR = 0x003C3C3C;
constexpr COLORREF TEXT_COLOR = 0x00FFFFFF;

std::mutex g_lock;
OverlayAssetSet *g_assets = nullptr;
HWND g_hwnd = NULL;
HWND g_threadHwnd = NULL; // message-only / overlay hwnd for PostMessage
HANDLE g_thread = NULL;
std::atomic<bool> g_visible{false};
std::atomic<bool> g_swallow{false};
std::atomic<unsigned short> g_reason{500};
int g_posX = 0, g_posY = 0;
// How much strings.xml's width/height/btnX/Y/W/H got scaled by to track the
// game window's current client size vs. the refW/refH they were designed
// at. 1.0/1.0 when no refW/refH is set (fixed pixel size, no scaling).
double g_scaleX = 1.0, g_scaleY = 1.0;
// Screen-space rect of the native window's own confirm button (from
// strings.xml's btnX/Y/W/H, scaled + offset by the overlay's on-screen
// position). Watched by a low-level mouse hook -- guarded by g_lock since
// the hook callback runs on this same overlay thread but PaintLayered can
// be re-entered via posted messages.
RECT g_btnScreenRc = {0, 0, 0, 0};
std::atomic<bool> g_nativeConfirmFired{false};
HHOOK g_mouseHook = NULL;
// Captured from native MessageBoxA/W (already resolved from string-c.tbl by
// the game client itself); guarded by g_lock. Empty -> fall back to the
// hardcoded tables below.
wchar_t g_liveText[512] = {0};

void NetLog(const char *fmt, ...) {
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

const wchar_t *ReasonToText(unsigned short reason) {
  switch (reason) {
  case 22:
    return L"\u6709\u4eba\u4ee5\u540c\u6a23\u7684\u5e33\u865f\u767b\u5165\uff0c\u8acb\u6ce8\u610f\uff0c\u60a8\u7684\u5bc6\u78bc\u53ef\u80fd\u5df2\u7d93\u5916\u6d29";
  case 31:
    return L"\u5225\u7684\u4eba\u4ee5\u540c\u6a23\u7684\u5e33\u865f\u4f86\u767b\u5165\u3002";
  case 38:
    return L"\u5728\u5225\u7684 PC \u4e0a\u767b\u5165\u76f8\u540c\u7684 IP \u4f4d\u5740\u3002";
  case 44:
    return L"\u9023\u7dda\u4e2d\u65b7\u3002";
  case 45:
    return L"\u7db2\u9801\u4e0a\u7684\u5bc6\u78bc\u6709\u8b8a\u66f4\uff0c\u56e0\u6b64\u5f37\u5236\u7d50\u675f\u3002";
  default:
    return L"\u9023\u7dda\u4e2d\u65b7\u3002";
  }
}

// 斷線畫面「整片變白」除錯用：確認素材到底有沒有真的載入成功、格式是不是
// 預期的。查完可以拿掉。
Gdiplus::Bitmap *CurrentBackgroundBitmap() {
  Gdiplus::Bitmap *bmp = OverlayAssets_GetBitmap(g_assets, kBgPng);
  if (bmp && bmp->GetWidth() > 0 && bmp->GetHeight() > 0) {
    NetLog("[disconnect-ui-dbg] GetBitmap OK w=%u h=%u pixelFormat=0x%08X",
           bmp->GetWidth(), bmp->GetHeight(), (unsigned)bmp->GetPixelFormat());
    return bmp;
  }
  NetLog("[disconnect-ui-dbg] GetBitmap FAILED bmp=0x%p", bmp);
  return nullptr;
}

bool GetGameClientSize(int *outW, int *outH) {
  HWND game = g_hGameWnd;
  if (!game || !IsWindow(game))
    return false;
  RECT rc;
  if (!GetClientRect(game, &rc) || rc.right <= 0 || rc.bottom <= 0)
    return false;
  *outW = rc.right;
  *outH = rc.bottom;
  return true;
}

void ComputeWinSize(int *outW, int *outH) {
  g_scaleX = 1.0;
  g_scaleY = 1.0;

  int baseW, baseH;
  bool haveBase;
  // strings.xml's Screen width/height (if set) wins over the source PNG's
  // native pixel size, so art can be stretched without re-exporting it.
  if (OverlayAssets_GetSize(g_assets, kScreen, &baseW, &baseH)) {
    haveBase = true;
  } else {
    Gdiplus::Bitmap *bmp = CurrentBackgroundBitmap();
    if (bmp) {
      baseW = (int)bmp->GetWidth();
      baseH = (int)bmp->GetHeight();
      haveBase = true;
    } else {
      baseW = FALLBACK_WIN_W;
      baseH = FALLBACK_WIN_H;
      haveBase = false; // no real art loaded; don't scale the fallback box
    }
  }

  int refW, refH, gameW, gameH;
  if (haveBase && OverlayAssets_GetRefSize(g_assets, kScreen, &refW, &refH) &&
      GetGameClientSize(&gameW, &gameH)) {
    g_scaleX = (double)gameW / (double)refW;
    g_scaleY = (double)gameH / (double)refH;
  }

  *outW = (int)(baseW * g_scaleX + 0.5);
  *outH = (int)(baseH * g_scaleY + 0.5);
  // 斷線畫面「整片變白」除錯用：確認縮放邏輯算出來的視窗尺寸合理。查完可以
  // 拿掉。
  NetLog("[disconnect-ui-dbg] ComputeWinSize baseW=%d baseH=%d haveBase=%d "
         "scaleX=%.3f scaleY=%.3f outW=%d outH=%d",
         baseW, baseH, (int)haveBase, g_scaleX, g_scaleY, *outW, *outH);
}

const wchar_t *TitleText() {
  static wchar_t buf[256];
  if (OverlayAssets_GetText(g_assets, kScreen, "title", buf, 256))
    return buf;
  return L"\u9023\u7dda\u4e2d\u65b7";
}

const wchar_t *BodyText() {
  {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_liveText[0])
      return g_liveText;
  }
  static wchar_t buf[512];
  char key[32];
  sprintf_s(key, "body_%u", (unsigned)g_reason.load());
  if (OverlayAssets_GetText(g_assets, kScreen, key, buf, 512))
    return buf;
  if (OverlayAssets_GetText(g_assets, kScreen, "body_default", buf, 512))
    return buf;
  return ReasonToText(g_reason.load());
}

void PositionCenter(int winW, int winH) {
  RECT rc = {0, 0, winW, winH};
  HWND game = g_hGameWnd;
  int rawPosX, rawPosY;
  bool hasExplicitPos = OverlayAssets_GetPosition(g_assets, kScreen, &rawPosX, &rawPosY);
  if (game && IsWindow(game)) {
    GetClientRect(game, &rc);
    POINT tl = {0, 0};
    ClientToScreen(game, &tl);
    if (hasExplicitPos) {
      // strings.xml's posX/posY are in the same refW/refH coordinate space
      // as everything else, offset from the game window's client top-left.
      g_posX = tl.x + (int)(rawPosX * g_scaleX + 0.5);
      g_posY = tl.y + (int)(rawPosY * g_scaleY + 0.5);
    } else {
      g_posX = tl.x + (rc.right - winW) / 2;
      g_posY = tl.y + (rc.bottom - winH) / 2;
    }
  } else if (hasExplicitPos) {
    g_posX = rawPosX;
    g_posY = rawPosY;
  } else {
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    g_posX = (sw - winW) / 2;
    g_posY = (sh - winH) / 2;
  }
  if (g_posX < 0)
    g_posX = 0;
  if (g_posY < 0)
    g_posY = 0;
}

// bits: the same 32bpp top-down buffer memDc's selected HBITMAP owns, so
// GDI+ (background image, real per-pixel alpha) and GDI (text, via memDc)
// can both write into it. Text is expected to sit on the image's opaque
// area, so GDI leaving alpha untouched there is a no-op (already 255).
void DrawInto(HDC memDc, void *bits, int winW, int winH) {
  Gdiplus::Bitmap *bg = CurrentBackgroundBitmap();
  bool usingAsset = bg != nullptr;

  if (usingAsset) {
    Gdiplus::Bitmap dest(winW, winH, winW * 4, PixelFormat32bppPARGB,
                         (BYTE *)bits);
    Gdiplus::Graphics g(&dest);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    Gdiplus::Status st = g.DrawImage(bg, 0, 0, winW, winH);
    // 斷線畫面「整片變白」除錯用：DrawImage 回傳值原本完全沒檢查，非
    // Gdiplus::Ok（0）就是真的畫失敗了。查完可以拿掉。
    NetLog("[disconnect-ui-dbg] DrawImage status=%d winW=%d winH=%d", (int)st,
           winW, winH);
  } else {
    HBRUSH bgBrush = CreateSolidBrush(BG_COLOR);
    RECT full = {0, 0, winW, winH};
    FillRect(memDc, &full, bgBrush);
    DeleteObject(bgBrush);

    HPEN pen = CreatePen(PS_SOLID, 1, BORDER_COLOR);
    HGDIOBJ oldPen = SelectObject(memDc, pen);
    HGDIOBJ oldBrush = SelectObject(memDc, GetStockObject(NULL_BRUSH));
    Rectangle(memDc, 0, 0, winW, winH);
    SelectObject(memDc, oldBrush);
    SelectObject(memDc, oldPen);
    DeleteObject(pen);
  }

  const char *screenName = kScreen;

  int titleFontSize = FONT_SIZE + 2;
  int bodyFontSize = FONT_SIZE;
  OverlayAssets_GetFontSize(g_assets, screenName, "title", &titleFontSize);
  OverlayAssets_GetFontSize(g_assets, screenName, "body", &bodyFontSize);
  // strings.xml font sizes are defined at refW/refH like everything else;
  // scale by the vertical factor (font height is a vertical metric).
  titleFontSize = (int)(titleFontSize * g_scaleY + 0.5);
  bodyFontSize = (int)(bodyFontSize * g_scaleY + 0.5);

  wchar_t titleFontFamily[64] = L"\u6A19\u6977\u9AD4"; // DFKai-SB
  wchar_t bodyFontFamily[64] = L"\u6A19\u6977\u9AD4";  // DFKai-SB
  OverlayAssets_GetFontFamily(g_assets, screenName, "title", titleFontFamily, 64);
  OverlayAssets_GetFontFamily(g_assets, screenName, "body", bodyFontFamily, 64);

  HFONT font = CreateFontW(bodyFontSize, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, PROOF_QUALITY, 0x02,
                           bodyFontFamily);
  HFONT titleFont = CreateFontW(titleFontSize, 0, 0, 0, FW_BOLD, 0, 0, 0,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, PROOF_QUALITY, 0x02,
                                titleFontFamily);
  SetBkMode(memDc, TRANSPARENT);

  HGDIOBJ oldFont = SelectObject(memDc, titleFont);
  SetTextColor(memDc, TEXT_COLOR);
  bool showTitle = OverlayAssets_GetShowTitle(g_assets, screenName);
  if (showTitle) {
    RECT assetTitleRc;
    RECT titleRc;
    if (OverlayAssets_GetTextRect(g_assets, screenName, "title", &assetTitleRc)) {
      titleRc.left = (int)(assetTitleRc.left * g_scaleX + 0.5);
      titleRc.top = (int)(assetTitleRc.top * g_scaleY + 0.5);
      titleRc.right = (int)(assetTitleRc.right * g_scaleX + 0.5);
      titleRc.bottom = (int)(assetTitleRc.bottom * g_scaleY + 0.5);
    } else {
      titleRc = {16, 16, winW - 16, 44};
    }
    bool titleCenter = OverlayAssets_GetCenterAlign(g_assets, screenName, "title");
    DrawTextW(memDc, TitleText(), -1, &titleRc,
              (titleCenter ? DT_CENTER : DT_LEFT) | DT_VCENTER |
                  DT_SINGLELINE | DT_NOPREFIX);
  }

  RECT assetBodyRc;
  RECT bodyRc;
  if (OverlayAssets_GetTextRect(g_assets, screenName, "body", &assetBodyRc)) {
    bodyRc.left = (int)(assetBodyRc.left * g_scaleX + 0.5);
    bodyRc.top = (int)(assetBodyRc.top * g_scaleY + 0.5);
    bodyRc.right = (int)(assetBodyRc.right * g_scaleX + 0.5);
    bodyRc.bottom = (int)(assetBodyRc.bottom * g_scaleY + 0.5);
  } else if (showTitle) {
    bodyRc = {16, 48, winW - 16, winH - 52};
  } else {
    // No title reserving the top strip: body can use the full box.
    bodyRc = {16, 16, winW - 16, winH - 16};
  }
  SelectObject(memDc, font);
  bool bodyCenter = OverlayAssets_GetCenterAlign(g_assets, screenName, "body");
  if (bodyCenter) {
    // DT_VCENTER is only honored by Win32 with DT_SINGLELINE, not with
    // word-wrapped multi-line text -- measure the wrapped height first via
    // DT_CALCRECT, then center that block manually within bodyRc.
    RECT calcRc = bodyRc;
    DrawTextW(memDc, BodyText(), -1, &calcRc,
              DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    int textH = calcRc.bottom - calcRc.top;
    int boxH = bodyRc.bottom - bodyRc.top;
    RECT drawRc = bodyRc;
    drawRc.top += (boxH - textH) / 2;
    DrawTextW(memDc, BodyText(), -1, &drawRc,
              DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
  } else {
    DrawTextW(memDc, BodyText(), -1, &bodyRc,
              DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
  }

  // No confirm button of our own -- the native window's own button (and its
  // click handling) is left untouched underneath; this overlay is a pure
  // visual patch (WS_EX_TRANSPARENT, see OverlayThreadProc) and never
  // intercepts input.

  SelectObject(memDc, oldFont);
  DeleteObject(font);
  DeleteObject(titleFont);
}

// Tracks where the native window's own confirm button is on screen (from
// strings.xml's btnX/Y/W/H), so the mouse hook knows what to watch for even
// though we no longer draw or hit-test a button ourselves.
void UpdateBtnScreenRect(int posX, int posY) {
  RECT assetBtnRc;
  RECT screenRc = {0, 0, 0, 0};
  if (OverlayAssets_GetButtonRect(g_assets, kScreen, &assetBtnRc)) {
    screenRc.left = posX + (int)(assetBtnRc.left * g_scaleX + 0.5);
    screenRc.top = posY + (int)(assetBtnRc.top * g_scaleY + 0.5);
    screenRc.right = posX + (int)(assetBtnRc.right * g_scaleX + 0.5);
    screenRc.bottom = posY + (int)(assetBtnRc.bottom * g_scaleY + 0.5);
  }
  std::lock_guard<std::mutex> lock(g_lock);
  g_btnScreenRc = screenRc;
}

void PaintLayered(HWND hwnd) {
  int winW, winH;
  ComputeWinSize(&winW, &winH);
  PositionCenter(winW, winH);
  int posX = g_posX, posY = g_posY;
  UpdateBtnScreenRect(posX, posY);

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
  HBITMAP bmp =
      CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bitsPtr, NULL, 0);
  if (!bmp) {
    ReleaseDC(NULL, screenDc);
    return;
  }

  HDC memDc = CreateCompatibleDC(screenDc);
  HGDIOBJ oldBmp = SelectObject(memDc, bmp);
  bool usingAsset = CurrentBackgroundBitmap() != nullptr;
  DrawInto(memDc, bitsPtr, winW, winH);

  if (!usingAsset) {
    // Fallback dark box has no real transparency: force fully opaque so
    // ULW_ALPHA doesn't show it as (incorrectly) see-through.
    BYTE *bits = (BYTE *)bitsPtr;
    size_t total = (size_t)winW * winH * 4;
    for (size_t i = 3; i < total; i += 4)
      bits[i] = 0xFF;
  }

  POINT ptDst = {posX, posY};
  SIZE sz = {winW, winH};
  POINT ptSrc = {0, 0};
  BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  BOOL ulwOk = UpdateLayeredWindow(hwnd, screenDc, &ptDst, &sz, memDc, &ptSrc,
                                   0, &blend, ULW_ALPHA);
  // 斷線畫面「整片變白」除錯用：回傳值原本完全沒檢查，失敗的話畫面會維持
  // 上一次的內容或呈現未定義狀態。查完可以拿掉。
  if (!ulwOk)
    NetLog("[disconnect-ui-dbg] UpdateLayeredWindow FAILED err=%u posX=%d "
           "posY=%d winW=%d winH=%d",
           (unsigned)GetLastError(), posX, posY, winW, winH);

  SelectObject(memDc, oldBmp);
  DeleteObject(bmp);
  DeleteDC(memDc);
  ReleaseDC(NULL, screenDc);
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);

// WH_MOUSE_LL 只在斷線對話框真的顯示的這段期間才裝上/卸載，見上面
// OverlayThreadProc 裡的說明。必須在 overlay 執行緒自己身上呼叫（安裝時的
// 慣例，雖然 dwThreadId=0 是系統範圍，回呼還是固定跑在安裝它的執行緒），這裡
// 兩個呼叫點（WM_SHOW_DISCONNECT / HideDialog）本來就都在 OverlayWndProc
// 裡，天生就是這個執行緒。
void EnsureMouseHookInstalled() {
  if (g_mouseHook)
    return;
  g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, NULL, 0);
  if (!g_mouseHook)
    NetLog("[disconnect-ui] SetWindowsHookEx(WH_MOUSE_LL) failed err=%u",
           GetLastError());
}

void RemoveMouseHookIfInstalled() {
  if (!g_mouseHook)
    return;
  UnhookWindowsHookEx(g_mouseHook);
  g_mouseHook = NULL;
}

void HideDialog(HWND hwnd) {
  g_visible.store(false);
  ShowWindow(hwnd, SW_HIDE);
  KillTimer(hwnd, 2);
  SetTimer(hwnd, 1, 3000, NULL);
  RemoveMouseHookIfInstalled();
}

// Toggle topmost dynamically instead of leaving it off entirely: this is a
// layered GDI popup over what's likely an exclusive-mode game surface, and
// plain relative z-order (SetWindowPos against g_hGameWnd, not
// HWND_TOPMOST) was not enough to actually composite above it -- the
// overlay stopped rendering at all. Only stay HWND_TOPMOST while the game
// is the foreground window (guarantees it still shows, same as the
// original always-topmost behavior); drop to HWND_NOTOPMOST otherwise so
// switching to another window covers the overlay along with the game.
void ReassertZOrder(HWND hwnd) {
  HWND insertAfter =
      (GetForegroundWindow() == g_hGameWnd) ? HWND_TOPMOST : HWND_NOTOPMOST;
  SetWindowPos(hwnd, insertAfter, 0, 0, 0, 0,
              SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

// Pure observer: never blocks/consumes the click (always CallNextHookEx),
// just watches for a real click landing on the native confirm button's
// on-screen rect while our overlay is up, and reacts to it. This is a
// WH_MOUSE_LL hook, which Windows requires to be installed system-wide
// (dwThreadId=0) even though the callback only ever runs on the thread that
// installed it (this overlay thread's message loop).
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION && wParam == WM_LBUTTONUP &&
      g_visible.load() && !g_nativeConfirmFired.load()) {
    MSLLHOOKSTRUCT *info = (MSLLHOOKSTRUCT *)lParam;
    RECT rc;
    {
      std::lock_guard<std::mutex> lock(g_lock);
      rc = g_btnScreenRc;
    }
    if (rc.right > rc.left && rc.bottom > rc.top && info->pt.x >= rc.left &&
        info->pt.x < rc.right && info->pt.y >= rc.top &&
        info->pt.y < rc.bottom) {
      g_nativeConfirmFired.store(true);
      NetLog("[disconnect-ui] native confirm button clicked (observed) -> "
             "close game");
      HWND game = g_hGameWnd;
      if (game && IsWindow(game))
        PostMessageW(game, WM_CLOSE, 0, 0);
      else
        ExitProcess(0);
    }
  }
  return CallNextHookEx(NULL, nCode, wParam, lParam);
}

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
  case WM_SHOW_DISCONNECT:
    KillTimer(hwnd, 1);
    g_reason.store((unsigned short)wp);
    g_swallow.store(true);
    g_visible.store(true);
    EnsureMouseHookInstalled();
    ShowWindow(hwnd, SW_SHOW);
    ReassertZOrder(hwnd);
    PaintLayered(hwnd);
    SetTimer(hwnd, 2, 200, NULL); // keep tracking the game window's z-order
    NetLog("[disconnect-ui] shown disconnect reason=%u", (unsigned)wp);
    return 0;
  case WM_TIMER:
    if (wp == 1) {
      KillTimer(hwnd, 1);
      if (!g_visible.load())
        g_swallow.store(false);
    } else if (wp == 2) {
      ReassertZOrder(hwnd);
    }
    return 0;
  case WM_UPDATE_TEXT:
    if (g_visible.load())
      PaintLayered(hwnd);
    return 0;
  case WM_HIDE_OVERLAY:
    if (g_visible.load()) {
      NetLog("[disconnect-ui] hiding (next login cycle)");
      HideDialog(hwnd);
    }
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
  cls.lpfnWndProc = OverlayWndProc;
  cls.hInstance = hinst;
  cls.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512)); // IDC_ARROW
  cls.hbrBackground = NULL;
  cls.lpszClassName = kClassName;
  RegisterClassExW(&cls);

  // Pure visual patch: click-through (WS_EX_TRANSPARENT) so the native
  // window underneath (including its own confirm button) still receives
  // all input; WS_EX_NOACTIVATE so we never steal focus from the game.
  // WS_EX_TOPMOST starts on (needed to actually composite over the game's
  // surface) -- ReassertZOrder() drops it to HWND_NOTOPMOST whenever the
  // game isn't the foreground window, so it doesn't float above other apps.
  HWND hwnd = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT |
          WS_EX_NOACTIVATE,
      kClassName, L"Disconnect", WS_POPUP, 0, 0, FALLBACK_WIN_W,
      FALLBACK_WIN_H, NULL, NULL, hinst, NULL);
  if (!hwnd) {
    NetLog("[disconnect-ui] CreateWindowExW failed err=%u", GetLastError());
    return 1;
  }
  // 不呼叫 SetLayeredWindowAttributes
  {
    std::lock_guard<std::mutex> lock(g_lock);
    g_hwnd = hwnd;
    g_threadHwnd = hwnd;
  }
  NetLog("[disconnect-ui] overlay thread ready hwnd=0x%p", hwnd);

  // WH_MOUSE_LL 不在這裡預先裝：它是系統範圍的低階滑鼠鉤子，只要裝著，全系統
  // 每一次滑鼠移動都要多繞去這個 hook chain 一趟。這個 overlay 執行緒從殼解密
  // 完成就啟動、活到整個 session 結束，但斷線對話框平常根本沒顯示（只有真的
  // 斷線那幾秒才用得到這個 hook 去偵測原生確認鈕的點擊），所以改成只在對話框
  // 真的顯示的時候才裝（見 WM_SHOW_DISCONNECT），隱藏/關閉就立刻卸載（見
  // HideDialog），避免整個遊戲過程中平白多一個系統級 hook 拖慢滑鼠。

  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (g_mouseHook)
    UnhookWindowsHookEx(g_mouseHook);
  return 0;
}

} // namespace

bool DisconnectOverlay_Start() {
  if (g_thread)
    return true;
  // 自製 UI 統一放同一組 ui.pak/idx（跟 MimirPowerOverlay.cpp 共用同一個
  // folderName/pakBaseName："ui"），OverlayAssets_Load 內部用
  // folderName+"|"+pakBaseName 當 cache key，兩邊傳一樣的字串會命中同一份已
  // 解密好的 pak，不會重複載入/解密兩次。
  g_assets = OverlayAssets_Load("ui", "ui");
  if (g_assets)
    NetLog("[disconnect-ui] external assets loaded (ui.pak)");
  else
    NetLog("[disconnect-ui] no external assets, using built-in appearance");
  g_thread = CreateThread(NULL, 0, OverlayThreadProc, NULL, 0, NULL);
  if (!g_thread) {
    NetLog("[disconnect-ui] CreateThread failed");
    return false;
  }
  // 等窗建立
  for (int i = 0; i < 100 && !g_hwnd; i++)
    Sleep(10);
  return g_hwnd != NULL;
}

void DisconnectOverlay_Notify(unsigned short reason) {
  g_swallow.store(true);
  g_reason.store(reason);
  g_nativeConfirmFired.store(false);
  HWND hwnd = NULL;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    g_liveText[0] = 0; // new cycle: forget previous native text until captured again
    hwnd = g_threadHwnd;
  }
  if (!hwnd) {
    DisconnectOverlay_Start();
    {
      std::lock_guard<std::mutex> lock(g_lock);
      hwnd = g_threadHwnd;
    }
  }
  if (hwnd)
    PostMessageW(hwnd, WM_SHOW_DISCONNECT, (WPARAM)reason, 0);
  else
    NetLog("[disconnect-ui] Notify failed: no hwnd reason=%u",
           (unsigned)reason);
}

bool DisconnectOverlay_ShouldSwallowNative() {
  return g_swallow.load() || g_visible.load();
}

void DisconnectOverlay_Hide() {
  HWND hwnd = NULL;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    hwnd = g_threadHwnd;
  }
  if (hwnd)
    PostMessageW(hwnd, WM_HIDE_OVERLAY, 0, 0);
}

void DisconnectOverlay_ArmSwallow() { g_swallow.store(true); }

void DisconnectOverlay_Shutdown() {
  HWND hwnd = NULL;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    hwnd = g_threadHwnd;
  }
  if (hwnd) {
    NetLog("[disconnect-ui] shutdown: closing overlay window (game exiting)");
    // WM_CLOSE has no explicit case in OverlayWndProc, so DefWindowProcW's
    // default handling runs: DestroyWindow -> WM_DESTROY -> PostQuitMessage,
    // which ends OverlayThreadProc's message loop and runs
    // UnhookWindowsHookEx(g_mouseHook) right after, on the same thread that
    // installed it.
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
  }
}

void DisconnectOverlay_SetLiveText(const wchar_t *text) {
  if (!text || !text[0])
    return;
  HWND hwnd = NULL;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    wcsncpy_s(g_liveText, text, _TRUNCATE);
    hwnd = g_threadHwnd;
  }
  NetLog("[disconnect-ui] live text captured from native dialog: '%ls'", text);
  if (hwnd && g_visible.load())
    PostMessageW(hwnd, WM_UPDATE_TEXT, 0, 0);
}
