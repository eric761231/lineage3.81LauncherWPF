// Overlay.cpp: 對照 Rust 版 ime_overlay/src/overlay.rs。
//
// 為什麼用 UpdateLayeredWindow：遊戲畫面（無論是 legacy DirectDraw 還是
// dgVoodoo 轉譯後的輸出）會蓋掉一般 top-most 視窗；唯一能穩定疊在最上層的
// 方法是讓 DWM 在最後一刻合成我們的 bitmap —— 也就是 UpdateLayeredWindow +
// WS_EX_LAYERED。這條路 OBS/疊加工具都用，理論上能壓過任何畫面輸出方式。
//
// 不用 InvalidateRect/BeginPaint/EndPaint —— 自己建 32bpp ARGB DIB，用 GDI
// 畫完後手動把 alpha 補成 0xFF（GDI 繪圖不會自動填 alpha），再一次性
// UpdateLayeredWindow 提交給 DWM。
#include "Common.h"
#include "Overlay.h"
#include "Dbg.h"
#include <dwmapi.h>
#include <string>
#include <mutex>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace {

const wchar_t *kClassName = L"LineageImeOverlay";
const wchar_t *kGameClass = L"Lineage";
const wchar_t *kGameTitle = L"Lineage Windows Client (13081901)";

// ===== 尺寸常數（垂直佈局）=====
constexpr int PADDING_X = 8;
constexpr int PADDING_Y = 6;
constexpr int COMP_H = 22;   // 組字字串列高
constexpr int ITEM_H = 26;   // 每個候選字列高
constexpr int NUM_W = 22;    // 數字 prefix 寬度
constexpr int ACCENT_W = 3;  // 左邊 accent 條寬度
constexpr int WIN_W = 220;   // 整個視窗寬度
constexpr int FONT_SIZE = 16;
constexpr int COMP_FONT_SIZE = 14;
constexpr size_t MAX_PAGE_SIZE = 9;

// ===== 配色（COLORREF 是 0x00BBGGRR）=====
constexpr COLORREF BG_COLOR = 0x00202020;
constexpr COLORREF BORDER_COLOR = 0x003C3C3C;
constexpr COLORREF TEXT_COLOR = 0x00FFFFFF;
constexpr COLORREF NUM_COLOR = 0x00B4B4B4;
constexpr COLORREF COMP_COLOR = 0x00AAAAAA;
constexpr COLORREF SEL_BG = 0x003C3C3C;
constexpr COLORREF ACCENT_COLOR = 0x00D77800;

// ===== 全域狀態 =====
std::mutex g_stateLock;
HWND g_overlayHwnd = NULL;
ImeState g_currentState;
HWND g_attachedTo = NULL;
bool g_visible = false;
int g_posX = 0, g_posY = 0, g_winW = WIN_W, g_winH = ITEM_H * 9 + PADDING_Y * 2;

int ComputeHeight(const ImeState &state) {
  int h = PADDING_Y * 2;
  if (!state.composition.empty()) h += COMP_H + 2;
  size_t n = state.PageItems().size();
  if (n > MAX_PAGE_SIZE) n = MAX_PAGE_SIZE;
  h += (int)n * ITEM_H;
  int minH = PADDING_Y * 2 + ITEM_H;
  return h > minH ? h : minH;
}

// 把 overlay 擺在輸入框下方 4px，自動避開螢幕邊界。
// 不呼叫 SetWindowPos —— UpdateLayeredWindow 用 pptDst+psize 原子改位置+大小；
// SetWindowPos 在 layered window 上會多走一次 NCCALC + WM_PAINT，造成閃爍。
void PositionNear(HWND inputHwnd, int winH) {
  RECT rect;
  if (!GetWindowRect(inputHwnd, &rect)) return;
  int x = rect.left;
  int y = rect.bottom + 4;

  int screenW = GetSystemMetrics(SM_CXSCREEN);
  int screenH = GetSystemMetrics(SM_CYSCREEN);
  int finalX = x;
  if (finalX > screenW - WIN_W) finalX = screenW - WIN_W;
  if (finalX < 0) finalX = 0;
  if (y + winH > screenH) y = rect.top - winH - 4;
  int finalY = y < 0 ? 0 : y;

  std::lock_guard<std::mutex> lock(g_stateLock);
  g_posX = finalX;
  g_posY = finalY;
  g_winW = WIN_W;
  g_winH = winH;
}

void DrawInto(HDC memDc, int winW, int winH) {
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

  HFONT font = CreateFontW(FONT_SIZE, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, PROOF_QUALITY,
                           0x02, L"Microsoft JhengHei");
  HFONT compFont = CreateFontW(COMP_FONT_SIZE, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, PROOF_QUALITY,
                               0x02, L"Microsoft JhengHei");

  SetBkMode(memDc, TRANSPARENT);

  int y = PADDING_Y;
  ImeState state;
  {
    std::lock_guard<std::mutex> lock(g_stateLock);
    state = g_currentState;
  }

  if (!state.composition.empty()) {
    HGDIOBJ oldFont = SelectObject(memDc, compFont);
    SetTextColor(memDc, COMP_COLOR);
    RECT rc = {PADDING_X, y, winW - PADDING_X, y + COMP_H};
    std::wstring comp = state.composition;
    DrawTextW(memDc, comp.data(), (int)comp.size(), &rc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(memDc, oldFont);
    y += COMP_H + 2;
  }

  HGDIOBJ oldFont = SelectObject(memDc, font);
  std::vector<std::wstring> page = state.PageItems();
  int selInPage = state.PageSelection();

  for (size_t i = 0; i < page.size() && i < MAX_PAGE_SIZE; i++) {
    int rowTop = y + (int)i * ITEM_H;
    int rowBot = rowTop + ITEM_H;
    bool isSel = (selInPage == (int)i);

    if (isSel) {
      RECT selRc = {1, rowTop, winW - 1, rowBot};
      HBRUSH selBrush = CreateSolidBrush(SEL_BG);
      FillRect(memDc, &selRc, selBrush);
      DeleteObject(selBrush);

      RECT accentRc = {1, rowTop + 4, 1 + ACCENT_W, rowBot - 4};
      HBRUSH accentBrush = CreateSolidBrush(ACCENT_COLOR);
      FillRect(memDc, &accentRc, accentBrush);
      DeleteObject(accentBrush);
    }

    SetTextColor(memDc, NUM_COLOR);
    std::wstring numLabel = std::to_wstring(i + 1);
    RECT numRc = {PADDING_X + ACCENT_W, rowTop, PADDING_X + ACCENT_W + NUM_W, rowBot};
    DrawTextW(memDc, numLabel.data(), (int)numLabel.size(), &numRc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SetTextColor(memDc, TEXT_COLOR);
    std::wstring cand = page[i];
    RECT candRc = {PADDING_X + ACCENT_W + NUM_W, rowTop, winW - PADDING_X, rowBot};
    DrawTextW(memDc, cand.data(), (int)cand.size(), &candRc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }
  SelectObject(memDc, oldFont);

  DeleteObject(font);
  DeleteObject(compFont);
}

void PaintLayered(HWND hwnd) {
  static int paintCount = 0;
  int n = ++paintCount;
  if (n <= 5) IME_LOG("[ime-overlay] paint_layered #%d hwnd=0x%p", n, hwnd);

  int posX, posY, winW, winH;
  {
    std::lock_guard<std::mutex> lock(g_stateLock);
    posX = g_posX;
    posY = g_posY;
    winW = g_winW;
    winH = g_winH;
  }
  if (winW <= 0 || winH <= 0) return;

  HDC screenDc = GetDC(NULL);
  if (!screenDc) {
    if (n <= 5) IME_LOG("[ime-overlay] GetDC NULL");
    return;
  }

  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = winW;
  bmi.bmiHeader.biHeight = -winH; // top-down
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void *bitsPtr = nullptr;
  HBITMAP bmp = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bitsPtr, NULL, 0);
  if (!bmp) {
    ReleaseDC(NULL, screenDc);
    if (n <= 5) IME_LOG("[ime-overlay] CreateDIBSection FAIL");
    return;
  }

  HDC memDc = CreateCompatibleDC(screenDc);
  HGDIOBJ oldBmp = SelectObject(memDc, bmp);

  DrawInto(memDc, winW, winH);

  // 每個 pixel 的 alpha 補成 0xFF —— GDI FillRect/DrawTextW 不會碰 alpha byte，
  // UpdateLayeredWindow + AC_SRC_ALPHA 看到 alpha=0 = 完全透明。
  BYTE *bits = (BYTE *)bitsPtr;
  size_t total = (size_t)winW * winH * 4;
  for (size_t i = 3; i < total; i += 4) bits[i] = 0xFF;

  POINT ptDst = {posX, posY};
  SIZE sz = {winW, winH};
  POINT ptSrc = {0, 0};
  BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

  BOOL ok = UpdateLayeredWindow(hwnd, screenDc, &ptDst, &sz, memDc, &ptSrc, 0, &blend, ULW_ALPHA);
  if (n <= 5) {
    IME_LOG("[ime-overlay] UpdateLayeredWindow #%d pos=(%d,%d) size=%dx%d ok=%d", n, posX, posY,
            winW, winH, ok ? 1 : 0);
  }

  SelectObject(memDc, oldBmp);
  DeleteObject(bmp);
  DeleteDC(memDc);
  ReleaseDC(NULL, screenDc);
}

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_PAINT:
      // 不靠 WM_PAINT 畫——所有繪製從 ShowOverlayFor/UpdateOverlay 主動觸發，
      // 這裡只把 dirty 區域消掉避免重複觸發。
      ValidateRect(hwnd, NULL);
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

// FindWindowW 是系統範圍搜尋，不會限定在呼叫端自己這個行程——雙開兩份遊戲時
// 系統上同時有兩個 "Lineage" class 的視窗，FindWindowW 只會回傳其中一個（不保
// 證是自己這個行程開的那個），導致這個行程的 SubclassGameWindow/TSF init 全部
// 作用在別的遊戲行程的視窗上，候選字視窗就再也不會出現。改成逐一列舉所有同
// class/title 的頂層視窗，用 GetWindowThreadProcessId 過濾出真正屬於自己這個
// 行程的那一個。
HWND FindWindowInThisProcess(LPCWSTR className, LPCWSTR windowTitle) {
  DWORD myPid = GetCurrentProcessId();
  HWND h = NULL;
  while ((h = FindWindowExW(NULL, h, className, windowTitle)) != NULL) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == myPid) return h;
  }
  return NULL;
}

} // namespace

HWND WaitForGameWindow(DWORD timeoutMs) {
  DWORD start = GetTickCount();
  while (true) {
    HWND h = FindWindowInThisProcess(kGameClass, NULL);
    if (h) return h;
    h = FindWindowInThisProcess(NULL, kGameTitle);
    if (h) return h;
    if (GetTickCount() - start >= timeoutMs) return NULL;
    Sleep(200);
  }
}

HWND CreateOverlayWindow() {
  HINSTANCE hinst = GetModuleHandleW(NULL);

  WNDCLASSEXW cls = {};
  cls.cbSize = sizeof(WNDCLASSEXW);
  cls.style = CS_HREDRAW | CS_VREDRAW;
  cls.lpfnWndProc = OverlayWndProc;
  cls.hInstance = hinst;
  cls.hCursor = LoadCursorW(NULL, IDC_ARROW);
  cls.hbrBackground = NULL; // UpdateLayeredWindow 不靠 WM_ERASEBKGND
  cls.lpszClassName = kClassName;
  RegisterClassExW(&cls);

  HWND hwnd = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClassName,
      L"LineageIME", WS_POPUP, 0, 0, WIN_W, ITEM_H * 9 + PADDING_Y * 2, NULL, NULL, hinst, NULL);
  if (!hwnd) return NULL;

  // 不呼叫 SetLayeredWindowAttributes —— 跟 UpdateLayeredWindow 路徑互斥。
  DWORD corner = 2; // DWMWCP_ROUND
  DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

  IME_LOG("[ime-overlay] created hwnd=0x%p", hwnd);
  return hwnd;
}

void SetOverlayHwnd(HWND h) { g_overlayHwnd = h; }
bool IsOverlayVisible() { return g_visible; }

void ShowOverlayFor(HWND inputHwnd, const ImeState &state) {
  g_attachedTo = inputHwnd;
  int winH = ComputeHeight(state);
  {
    std::lock_guard<std::mutex> lock(g_stateLock);
    g_currentState = state;
  }
  g_visible = true;

  if (!g_overlayHwnd) return;
  PositionNear(inputHwnd, winH);
  ShowWindow(g_overlayHwnd, SW_SHOWNOACTIVATE);
  PaintLayered(g_overlayHwnd);
}

void UpdateOverlay(const ImeState &state) {
  int winH = ComputeHeight(state);
  HWND attached = g_attachedTo;
  {
    std::lock_guard<std::mutex> lock(g_stateLock);
    g_currentState = state;
  }
  if (!g_overlayHwnd) return;
  if (attached) PositionNear(attached, winH);
  PaintLayered(g_overlayHwnd);
}

void HideOverlay() {
  g_visible = false;
  if (!g_overlayHwnd) return;
  ShowWindow(g_overlayHwnd, SW_HIDE);
}
