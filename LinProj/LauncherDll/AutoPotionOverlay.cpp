// AutoPotionOverlay.cpp: see AutoPotionOverlay.h.
//
// 比照 MimirPowerOverlay：獨立長駐 UI 執行緒 + WS_EX_LAYERED + hWndOwner=g_hGameWnd，
// UpdateLayeredWindow 自繪。本階段無美術圖，GDI+ 幾何＋文字對齊 mockup。
// 儲存只排隊，真正 Save/Send 由 AutoPotionOverlay_PumpPendingSave（遊戲主執行緒）做。
#include <winsock2.h>
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <mutex>
#include <atomic>
#include "AutoPotionOverlay.h"
#include "AutoPotionConfig.h"
#include "OverlayAssets.h"
#include "AttackDamageHook.h"

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdiplus.lib")

extern HWND g_hGameWnd;
extern HINSTANCE hins;

namespace {

const wchar_t *kClassName = L"LauncherDllAutoPotionOverlay";
constexpr UINT WM_SHOW_AUTOPOTION = WM_USER + 300;
constexpr UINT WM_HIDE_AUTOPOTION = WM_USER + 301;
constexpr UINT WM_AUTOPOTION_RESOLVE_REPLY = WM_USER + 303;
constexpr UINT WM_AUTOPOTION_SLOT_COUNTS = WM_USER + 304;

// 設計座標系——集中在這裡調，之後可改讀 XML。
// 視窗與四宮格比例可改這些常數重新分配布局（不必改繪圖邏輯）。
constexpr int kBaseW = 720;
constexpr int kBaseH = 520;
constexpr int kRefW = 1153;
constexpr int kRefH = 798;

constexpr int kTitleH = 28;
constexpr int kTabH = 30;
constexpr int kFooterH = 44;
constexpr int kPad = 10;
constexpr int kGap = 8;
// 內容區左右／上下切分比例（0~1），對齊截圖四宮格
constexpr float kSplitX = 0.50f;
constexpr float kSplitY = 0.46f;

constexpr int kSlotSize = 40;
constexpr int kSlotGap = 6;

enum TabId { Tab_Buff = 0, Tab_Item = 1, Tab_Teleport = 2, Tab_Misc = 3, Tab_Count = 4 };
int g_activeTab = Tab_Buff;
int g_pressedTab = -1;
int g_pressedEnable = 0;
constexpr int TIMER_CARET = 2;
constexpr int TIMER_VITALS = 3; // 節流重繪（~200ms），非輪詢記憶體
bool g_caretOn = true;

struct PlayerVitalsCache {
  int curHp = 0;
  int maxHp = 0;
  int curMp = 0;
  int maxMp = 0;
  bool hasHp = false;
  bool hasMp = false;
};
PlayerVitalsCache g_vitals;
std::atomic<bool> g_vitalsDirty{false};
// -1=無；0=關閉通知；1=開啟通知（遊戲主執行緒 Pump 送出）
volatile LONG g_pendingUiNotify = -1;


const wchar_t *kTabNames[Tab_Count] = {L"BUFF", L"道具", L"傳送", L"其他"};

// 2026-09-10：「其他」分頁。「自動修理武器」「自動吃肉」→ cfg／伺服器；
// 「顯示傷害」→ AttackDamageHook（客戶端）；其餘暫 g_miscToggle 佔位。
constexpr int kMiscToggleCount = 6;
const wchar_t *kMiscToggleLabels[kMiscToggleCount] = {
    L"全白天", L"自動修理武器", L"海底抽水", L"自動吃肉", L"顯示傷害", L"待設定",
};
constexpr int kMiscIdx_Whetstone = 1;
constexpr int kMiscIdx_EatMeat = 3;
constexpr int kMiscIdx_ShowDamage = 4;
bool g_miscToggle[kMiscToggleCount] = {};


std::mutex g_lock;
HWND g_hwnd = NULL;
HWND g_threadHwnd = NULL;
HANDLE g_thread = NULL;
std::atomic<bool> g_visible{false};
bool g_userMoved = false;
double g_scaleX = 1.0, g_scaleY = 1.0;

AutoPotionConfig g_cfg;
ULONG_PTR g_gdiplusToken = 0;
bool g_gdiplusStarted = false;

// 2026-09-09：道具格子圖示。跟 MimirPowerOverlay.cpp 共用同一組 ui.pak/idx
// （OverlayAssets_Load 用 folderName+"|"+pakBaseName 當 cache key，兩邊傳
// "ui"/"ui" 會命中同一份已解密好的 pak，不會重複載入）。檔名規則見
// AutoPotionOverlay_現況與圖示交接.md：item_<gfxid>.png。找不到就退回
// DrawItemPlaceholderIcon 那個瓶子造型佔位圖，不會整個畫面空白。
OverlayAssetSet *g_assets = nullptr;

void EnsureAssetsLoaded() {
  if (g_assets)
    return;
  g_assets = OverlayAssets_Load("ui", "ui");
}

Gdiplus::Bitmap *GetItemIconBitmap(int gfxid) {
  if (gfxid <= 0)
    return nullptr;
  EnsureAssetsLoaded();
  if (!g_assets)
    return nullptr;
  char name[32];
  sprintf_s(name, "item_%d.png", gfxid);
  return OverlayAssets_GetBitmap(g_assets, name);
}

// 編輯狀態：-1=無；0=heal 槽；1=mana 槽；2=heal%；3=mana%
int g_editMode = -1;
int g_editSlot = -1;
wchar_t g_editBuf[16] = {};
int g_hoverClose = 0, g_pressedClose = 0;
int g_hoverSave = 0, g_pressedSave = 0;
int g_hoverCloseBtn = 0, g_pressedCloseBtn = 0;
int g_hoverDec = -1, g_hoverInc = -1; // 0=heal 1=mana

// 2026-09-08：「點格子→點背包道具」選道具狀態。跟 g_editMode/g_editSlot 同時
// 設起來（點任一槽都會同時進入這兩種狀態），玩家可以繼續打字用鍵盤輸入（既有
// 流程不變），也可以改成去點背包道具（新流程）。用 atomic 是因為
// AutoPotionOverlay_IsPicking 會被遊戲主執行緒（非 overlay 自己的執行緒）讀取。
// 2026-09-09：拿掉「確認中」中繼顯示（點道具後幾乎立刻有回覆＋自動存檔，不需要
// 額外的暫時名稱狀態），連帶拿掉 g_pickPendingName / PickCandidateMsg /
// AutoPotionOverlay_OnPickCandidate 這整條只服務那個顯示的機制。
std::atomic<int> g_pickSection{-1};
std::atomic<int> g_pickSlot{-1};

struct ResolveReplyMsg {
  bool success;
  int section;
  int slot;
  int templateItemId;
  int gfxid;
  int count;
  wchar_t name[64];
};

// 伺服器推送的槽數量（開面板多筆／用盡一筆）
struct SlotCountsBatchMsg {
  int n;
  struct {
    int section;
    int slot;
    int count;
  } items[20];
};

volatile LONG g_pendingSave = 0;
AutoPotionConfig g_pendingCfg;

// 2026-09-10：道具名稱/數量現在是 AutoPotionSlot 自己的欄位（見
// AutoPotionConfig.h），跟著設定檔一起存讀——進遊戲剛開視窗時直接顯示上次
// 存檔當下的名稱/數量（不是即時背包庫存），只有真的重新點選（伺服器回覆
// 成功）才會更新。不再需要獨立的 session-only 快取陣列。

int g_hoverSection = -1, g_hoverSlot = -1;

void ApLog(const char *fmt, ...) {
  // 只留主要 UI 訊息；resolve／slot-count／pump 等略過
  if (fmt == NULL)
    return;
  if (strstr(fmt, "resolve") || strstr(fmt, "slot-count") ||
      strstr(fmt, "PumpPending") || strstr(fmt, "save queued") ||
      strstr(fmt, "toggle enabled"))
    return;

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
  char msg[512] = {0};
  va_list args;
  va_start(args, fmt);
  vsprintf_s(msg, fmt, args);
  va_end(args);
  fprintf(fp, "[%04d-%02d-%02d %02d:%02d:%02d.%03d][PID=%u][TID=%u] [AutoPotionUI] %s\n",
          st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
          st.wMilliseconds, (unsigned)GetCurrentProcessId(),
          (unsigned)GetCurrentThreadId(), msg);
  fflush(fp);
  fclose(fp);
}

RECT ScaleRc(int x, int y, int w, int h) {
  RECT rc;
  rc.left = (int)(x * g_scaleX + 0.5);
  rc.top = (int)(y * g_scaleY + 0.5);
  rc.right = (int)((x + w) * g_scaleX + 0.5);
  rc.bottom = (int)((y + h) * g_scaleY + 0.5);
  return rc;
}

bool PtIn(const RECT &rc, int x, int y) {
  return x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom;
}

void UpdateScale() {
  g_scaleX = 1.0;
  g_scaleY = 1.0;
  if (!g_hGameWnd || !IsWindow(g_hGameWnd))
    return;
  RECT rc = {};
  GetClientRect(g_hGameWnd, &rc);
  int cw = rc.right - rc.left;
  int ch = rc.bottom - rc.top;
  if (cw > 0 && ch > 0 && kRefW > 0 && kRefH > 0) {
    g_scaleX = (double)cw / (double)kRefW;
    g_scaleY = (double)ch / (double)kRefH;
    if (g_scaleX < 0.5)
      g_scaleX = 0.5;
    if (g_scaleY < 0.5)
      g_scaleY = 0.5;
    if (g_scaleX > 1.5)
      g_scaleX = 1.5;
    if (g_scaleY > 1.5)
      g_scaleY = 1.5;
  }
}

void ComputeWinSize(int *outW, int *outH) {
  UpdateScale();
  *outW = (int)(kBaseW * g_scaleX + 0.5);
  *outH = (int)(kBaseH * g_scaleY + 0.5);
}

void PositionWindow(HWND hwnd, int winW, int winH) {
  if (g_userMoved)
    return;
  int x = 100, y = 100;
  if (g_hGameWnd && IsWindow(g_hGameWnd)) {
    RECT gr = {};
    GetClientRect(g_hGameWnd, &gr);
    POINT tl = {0, 0};
    ClientToScreen(g_hGameWnd, &tl);
    x = tl.x + (gr.right - winW) / 2;
    y = tl.y + (gr.bottom - winH) / 2;
  }
  SetWindowPos(hwnd, NULL, x, y, winW, winH, SWP_NOZORDER | SWP_NOACTIVATE);
}

// ---- layout in base coords（四宮格由 kSplitX/Y 決定）----
RECT TitleBarRc() { return ScaleRc(0, 0, kBaseW, kTitleH); }
RECT CloseXRc() { return ScaleRc(kBaseW - 28, 4, 22, 20); }

RECT ContentOuterRc() {
  return ScaleRc(kPad, kTitleH + kTabH + 4, kBaseW - kPad * 2,
                 kBaseH - kTitleH - kTabH - kFooterH - 8);
}

void ContentQuads(RECT *tl, RECT *tr, RECT *bl, RECT *br) {
  RECT o = ContentOuterRc();
  const int ow = o.right - o.left;
  const int oh = o.bottom - o.top;
  const int gap = (int)(kGap * g_scaleX);
  const int midX = o.left + (int)(ow * kSplitX);
  const int midY = o.top + (int)(oh * kSplitY);
  if (tl) {
    tl->left = o.left;
    tl->top = o.top;
    tl->right = midX - gap / 2;
    tl->bottom = midY - gap / 2;
  }
  if (tr) {
    tr->left = midX + gap / 2;
    tr->top = o.top;
    tr->right = o.right;
    tr->bottom = midY - gap / 2;
  }
  if (bl) {
    bl->left = o.left;
    bl->top = midY + gap / 2;
    bl->right = midX - gap / 2;
    bl->bottom = o.bottom;
  }
  if (br) {
    br->left = midX + gap / 2;
    br->top = midY + gap / 2;
    br->right = o.right;
    br->bottom = o.bottom;
  }
}

RECT TabRc(int index) {
  const int tabW = 72;
  const int x = kPad + index * (tabW + 4);
  return ScaleRc(x, kTitleH + 2, tabW, kTabH - 4);
}

// 「其他」分頁每一列打勾選項的位置（第 index 列）。
RECT MiscToggleRc(int index) {
  RECT o = ContentOuterRc();
  int rowH = (int)(34 * g_scaleY);
  int top = o.top + (int)(14 * g_scaleY) + index * rowH;
  RECT rc = {o.left + (int)(14 * g_scaleX), top, o.right - (int)(14 * g_scaleX),
             top + (int)(26 * g_scaleY)};
  return rc;
}

RECT FooterYBase() { return ScaleRc(0, kBaseH - kFooterH, kBaseW, kFooterH); }
RECT SaveBtnRc() { return ScaleRc(kPad, kBaseH - kFooterH + 8, 90, 28); }
RECT EnableBtnRc() { return ScaleRc(kPad + 100, kBaseH - kFooterH + 8, 90, 28); }
RECT CloseBtnRc() { return ScaleRc(kBaseW - kPad - 90, kBaseH - kFooterH + 8, 90, 28); }
RECT HintBarRc() {
  return ScaleRc(kPad + 200, kBaseH - kFooterH + 12, kBaseW - kPad * 2 - 300, 20);
}

// section 0=heal / 1=mana，落在左下「恢復道具設定」宮格內
RECT HealBoxRc() {
  RECT bl;
  ContentQuads(nullptr, nullptr, &bl, nullptr);
  const int hh = (bl.bottom - bl.top - (int)(4 * g_scaleY)) / 2;
  RECT rc = bl;
  rc.bottom = bl.top + hh;
  return rc;
}
RECT ManaBoxRc() {
  RECT bl;
  ContentQuads(nullptr, nullptr, &bl, nullptr);
  const int hh = (bl.bottom - bl.top - (int)(4 * g_scaleY)) / 2;
  RECT rc = bl;
  rc.top = bl.bottom - hh;
  return rc;
}

RECT SlotRc(int section /*0 heal 1 mana*/, int index) {
  RECT box = (section == 0) ? HealBoxRc() : ManaBoxRc();
  // 標題列約 20px，再留一點給 HP/MP 示意條，槽列往上靠
  const int titleH = (int)(18 * g_scaleY);
  const int barH = (int)(10 * g_scaleY);
  const int rowY = box.top + titleH + barH + (int)(4 * g_scaleY);
  const int slot = (int)(kSlotSize * ((g_scaleX < g_scaleY) ? g_scaleX : g_scaleY));
  const int gap = (int)(kSlotGap * g_scaleX);
  const int rowX = box.left + (int)(8 * g_scaleX);
  const int x = rowX + index * (slot + gap);
  RECT rc;
  rc.left = x;
  rc.top = rowY;
  rc.right = x + slot;
  rc.bottom = rowY + slot;
  return rc;
}

RECT SlotLabelRc(int section, int index) {
  RECT s = SlotRc(section, index);
  RECT rc;
  rc.left = s.left;
  rc.right = s.right;
  rc.top = s.bottom + (int)(1 * g_scaleY);
  rc.bottom = rc.top + (int)(14 * g_scaleY);
  return rc;
}

// 只取消「打字輸入」狀態，不動「正在等伺服器回覆的道具選擇」——存檔／關閉／
// ±這幾個按鈕只需要取消打字，不該連帶把還在飛行中的解析請求丟掉（見
// CancelPick 的說明）。
void CancelTextEdit() {
  g_editMode = -1;
  g_editSlot = -1;
  g_editBuf[0] = 0;
}

// 2026-09-09：取消「正在等伺服器回覆的道具選擇」。原本 ClearEdit 把打字輸入
// 跟道具選擇這兩件事綁在一起重置，結果存檔／關閉／±這些跟道具選擇完全無關的
// 按鈕，只要在「已經點了背包道具、伺服器回覆還沒送到」這個空檔被按下，就會把
// g_pickSection/g_pickSlot 重置成 -1；等真正的回覆送達時，比對發現「已經不是
// 我在等的那一格」就整包丟棄，玩家剛選好的藥水因此從沒真的寫進去，看起來像
// 「點任何按鈕都會移除剛選好的藥水」。現在拆開：只有真的要放棄這次選擇時
// （點別的格子、Esc、或伺服器回覆已經處理完畢）才呼叫這個。
void CancelPick() {
  g_pickSection.store(-1);
  g_pickSlot.store(-1);
}

void ClearEdit() {
  CancelTextEdit();
  CancelPick();
}

// 格子內容透過「點道具選擇」以外的方式改變時（手動清空/手動打字輸入）要呼叫，
// 避免 hover tooltip／格子內數量顯示舊道具留下來的名稱/數量，跟格子裡實際的
// 新內容對不上。呼叫端必須已經持有 g_lock（直接改 g_cfg，不自己上鎖）。
void ClearSlotCache(int section, int slot) {
  AutoPotionSection &sec = (section == 0) ? g_cfg.heal : g_cfg.mana;
  sec.slots[slot].name[0] = 0;
  sec.slots[slot].count = 0;
}

void QueueSave() {
  // enabled 只由「啟動／停止」按鈕決定，對應伺服器 C_PlaySupport 的
  // pc.autoPotionEnabled = readC() != 0；存檔／選道具不要擅自改這個旗標。
  AutoPotionConfig cfg;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    cfg = g_cfg;
    g_pendingCfg = cfg;
  }
  InterlockedExchange(&g_pendingSave, 1);
  ApLog("save queued enabled=%d", (int)cfg.enabled);
}

// 編輯中游標：閃爍的「｜」（全形較好認）；關掉時只顯示已打的字。
void FormatEditCaret(wchar_t *out, size_t outChars, const wchar_t *buf) {
  if (g_caretOn)
    swprintf_s(out, outChars, L"%s｜", buf && buf[0] ? buf : L"");
  else
    swprintf_s(out, outChars, L"%s", buf && buf[0] ? buf : L"");
}

void ClampVital(int *cur, int *maxv) {
  if (*cur < 0)
    *cur = 0;
  if (*maxv < 0)
    *maxv = 0;
  if (*maxv > 0 && *cur > *maxv)
    *cur = *maxv;
}

void MarkVitalsDirtyIfVisible() {
  if (g_visible.load())
    g_vitalsDirty.store(true);
}

void QueueUiNotify(bool visible) {
  InterlockedExchange(&g_pendingUiNotify, visible ? 1L : 0L);
}

void DrawRoundRect(Gdiplus::Graphics &g, const RECT &rc, Gdiplus::Color fill,
                   Gdiplus::Color stroke, float penW = 1.5f) {
  Gdiplus::SolidBrush br(fill);
  Gdiplus::Pen pen(stroke, penW);
  Gdiplus::RectF r((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
                   (Gdiplus::REAL)(rc.right - rc.left),
                   (Gdiplus::REAL)(rc.bottom - rc.top));
  g.FillRectangle(&br, r);
  g.DrawRectangle(&pen, r);
}

void DrawTextIn(Gdiplus::Graphics &g, const RECT &rc, const wchar_t *text,
                Gdiplus::Color color, int fontPt, bool bold, bool center) {
  Gdiplus::FontFamily fam(L"Microsoft JhengHei");
  Gdiplus::Font font(&fam, (Gdiplus::REAL)(fontPt * (g_scaleY < g_scaleX ? g_scaleY : g_scaleX)),
                     bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
                     Gdiplus::UnitPixel);
  Gdiplus::SolidBrush br(color);
  Gdiplus::StringFormat fmt;
  if (center) {
    fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  } else {
    fmt.SetAlignment(Gdiplus::StringAlignmentNear);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  }
  Gdiplus::RectF r((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
                   (Gdiplus::REAL)(rc.right - rc.left),
                   (Gdiplus::REAL)(rc.bottom - rc.top));
  g.DrawString(text, -1, &font, r, &fmt, &br);
}

// 沒有真的道具圖示可以畫（對照表要另外反組譯，這次不做，見計畫文件），先畫一個
// 簡單的瓶子形狀佔位，顏色照 kind 區分（道具=琥珀色／法術=藍色），至少看起來
// 不是純文字、有個「這裡放了東西」的視覺意象。
void DrawItemPlaceholderIcon(Gdiplus::Graphics &g, const RECT &rc, bool isSkill) {
  Gdiplus::Color body = isSkill ? Gdiplus::Color(255, 90, 130, 200)
                                : Gdiplus::Color(255, 200, 150, 60);
  Gdiplus::Color neck = isSkill ? Gdiplus::Color(255, 130, 170, 230)
                                : Gdiplus::Color(255, 230, 190, 110);
  int w = rc.right - rc.left, h = rc.bottom - rc.top;
  int cx = rc.left + w / 2;
  // 瓶頸
  Gdiplus::RectF neckRc((Gdiplus::REAL)(cx - w * 0.08f), (Gdiplus::REAL)(rc.top + h * 0.14f),
                        (Gdiplus::REAL)(w * 0.16f), (Gdiplus::REAL)(h * 0.16f));
  Gdiplus::SolidBrush neckBr(neck);
  g.FillRectangle(&neckBr, neckRc);
  // 瓶身（圓角矩形）
  Gdiplus::RectF bodyRc((Gdiplus::REAL)(rc.left + w * 0.22f), (Gdiplus::REAL)(rc.top + h * 0.32f),
                        (Gdiplus::REAL)(w * 0.56f), (Gdiplus::REAL)(h * 0.54f));
  Gdiplus::GraphicsPath path;
  Gdiplus::REAL r = bodyRc.Width * 0.3f;
  path.AddArc(bodyRc.X, bodyRc.Y, r, r, 180, 90);
  path.AddArc(bodyRc.X + bodyRc.Width - r, bodyRc.Y, r, r, 270, 90);
  path.AddArc(bodyRc.X + bodyRc.Width - r, bodyRc.Y + bodyRc.Height - r, r, r, 0, 90);
  path.AddArc(bodyRc.X, bodyRc.Y + bodyRc.Height - r, r, r, 90, 90);
  path.CloseFigure();
  Gdiplus::SolidBrush bodyBr(body);
  g.FillPath(&bodyBr, &path);
}

void DrawSlot(Gdiplus::Graphics &g, int section, int index, const AutoPotionSlot &slot) {
  RECT rc = SlotRc(section, index);
  // g_editMode: 0/1=heal/mana 道具編號輸入，2/3=heal/mana 百分比輸入（畫在
  // SlotLabelRc 那一行，見下面），都用同一個 g_editSlot 記是哪一槽。
  const bool editing =
      (g_editMode == section && g_editSlot == index);
  const bool editingPct =
      (g_editMode == section + 2 && g_editSlot == index);
  const bool picking =
      (g_pickSection.load() == section && g_pickSlot.load() == index);
  Gdiplus::Color fill(255, 40, 32, 28);
  Gdiplus::Color stroke(255, 160, 130, 70);
  if (editing || picking)
    stroke = Gdiplus::Color(255, 255, 220, 120);
  else if (slot.kind != AutoPotionSlot_None)
    stroke = Gdiplus::Color(255, 200, 170, 90);
  DrawRoundRect(g, rc, fill, stroke, (editing || picking) ? 2.5f : 1.5f);

  if (editing) {
    wchar_t line[64] = {};
    FormatEditCaret(line, _countof(line), g_editBuf);
    DrawTextIn(g, rc, line, Gdiplus::Color(255, 240, 230, 200), 12, false, true);
  } else if (slot.kind != AutoPotionSlot_None && slot.id > 0) {
    // 2026-09-09：優先畫真的道具圖示（item_<gfxid>.png，來自 Sprite.pak 離線
    // 轉出來的靜態圖，見交接文件第 6 節），找不到（法術槽 gfxid 目前一定是 0、
    // 或這個道具還沒轉圖）才退回瓶子造型佔位圖。
    // OverlayAssets 對 item_* 會把 tbt 匯出常見的紅底色鍵轉成透明。
    Gdiplus::Bitmap *icon = GetItemIconBitmap(slot.gfxid);
    if (icon) {
      // 2026-09-09：道具圖是從 Sprite.pak 轉出來的原生尺寸（多半 24~31px），
      // 遠比格子(48 設計px)小；先前整張拉滿格子畫，導致圖示看起來被放大
      // 1.5~2倍。改成照原生像素＋目前縮放比例畫，置中，且不超過格子扣掉邊距
      // 後的可用範圍（維持格子邊框跟外觀一致）。
      g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
      g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
      const int pad = (int)(4 * g_scaleX);
      const int maxW = (rc.right - rc.left) - pad * 2;
      const int maxH = (rc.bottom - rc.top) - pad * 2;
      // 2026-09-10：使用者反應原生尺寸偏小，整體放大 5%（超出可用範圍時下面
      // 的等比縮放還是會夾回格子邊界內，不會破格）。
      constexpr double kIconUpscale = 1.05;
      int iw = (int)(icon->GetWidth() * g_scaleX * kIconUpscale);
      int ih = (int)(icon->GetHeight() * g_scaleY * kIconUpscale);
      if (iw > maxW || ih > maxH) {
        double s = min((double)maxW / iw, (double)maxH / ih);
        iw = (int)(iw * s);
        ih = (int)(ih * s);
      }
      int dx = rc.left + ((rc.right - rc.left) - iw) / 2;
      int dy = rc.top + ((rc.bottom - rc.top) - ih) / 2;
      g.DrawImage(icon, dx, dy, iw, ih);
    } else {
      DrawItemPlaceholderIcon(g, rc, slot.kind == AutoPotionSlot_Skill);
    }

    // 數量：開面板／用盡由伺服器推送寫入 slot.count；平常不即時刷新
    int count = slot.count;
    // 用盡（count==0）：半透明灰遮罩；有數量才畫右上角數字
    if (count <= 0) {
      Gdiplus::SolidBrush mask(Gdiplus::Color(140, 40, 40, 40));
      g.FillRectangle(&mask, rc.left + 1, rc.top + 1, (rc.right - rc.left) - 2,
                      (rc.bottom - rc.top) - 2);
    } else {
      wchar_t cnt[16];
      swprintf_s(cnt, L"%d", count);
      int cw = (int)(22 * g_scaleX);
      int ch = (int)(14 * g_scaleY);
      RECT cntRc = {rc.right - cw, rc.top, rc.right, rc.top + ch};
      DrawTextIn(g, cntRc, cnt, Gdiplus::Color(255, 235, 225, 200), 8, false, true);
    }
  }

  // 2026-09-10：格子正下方同一行，依狀態顯示「輸入中」/百分比門檻徽章——
  // 百分比輸入框跟著移到這裡（原本在數量文字下方另一行，現在數量搬進格子
  // 內，這行往上移到緊貼格子下緣）。「請點背包道具」改成浮動 tooltip（見
  // DrawPickingTooltip），不佔用這行的空間——選擇中的格子邊框本身已經會
  // 高亮（見上面 stroke），這裡繼續正常顯示百分比徽章即可。
  RECT lab = SlotLabelRc(section, index);
  if (editing) {
    DrawTextIn(g, lab, L"輸入中", Gdiplus::Color(255, 255, 220, 120), 10, false, true);
  } else {
    DrawRoundRect(g, lab, Gdiplus::Color(255, 30, 24, 20),
                 editingPct ? Gdiplus::Color(255, 255, 220, 120)
                            : Gdiplus::Color(255, 130, 105, 60),
                 editingPct ? 2.0f : 1.0f);
    wchar_t pctBuf[16] = {};
    if (editingPct) {
      FormatEditCaret(pctBuf, _countof(pctBuf), g_editBuf);
    } else {
      swprintf_s(pctBuf, L"%d%%", slot.thresholdPercent);
    }
    DrawTextIn(g, lab, pctBuf,
              editingPct ? Gdiplus::Color(255, 255, 240, 200)
                         : Gdiplus::Color(255, 190, 170, 130),
              9, false, true);
  }
}

void DrawHoverTooltip(Gdiplus::Graphics &g, const AutoPotionConfig &cfg) {
  if (g_hoverSection < 0 || g_hoverSlot < 0)
    return;
  const AutoPotionSection &sec = (g_hoverSection == 0) ? cfg.heal : cfg.mana;
  const AutoPotionSlot &slot = sec.slots[g_hoverSlot];
  if (slot.kind == AutoPotionSlot_None || slot.id <= 0)
    return;

  wchar_t text[96] = {};
  if (slot.name[0]) {
    swprintf_s(text, L"%s", slot.name);
  } else if (slot.kind == AutoPotionSlot_Skill) {
    swprintf_s(text, L"法術 #%d", slot.id);
  } else {
    swprintf_s(text, L"道具 #%d", slot.id);
  }

  RECT slotRc = SlotRc(g_hoverSection, g_hoverSlot);
  int tipW = (int)(180 * g_scaleX);
  int tipH = (int)(24 * g_scaleY);
  int tipX = slotRc.left;
  int tipY = slotRc.top - tipH - (int)(4 * g_scaleY);
  if (tipY < (int)((kTitleH + kTabH) * g_scaleY))
    tipY = slotRc.bottom + (int)(4 * g_scaleY);
  RECT tipRc = {tipX, tipY, tipX + tipW, tipY + tipH};
  DrawRoundRect(g, tipRc, Gdiplus::Color(240, 20, 16, 14), Gdiplus::Color(255, 200, 170, 90),
                1.0f);
  DrawTextIn(g, tipRc, text, Gdiplus::Color(255, 240, 230, 200), 11, false, true);
}

// 2026-09-10：「請點背包道具」改成浮動 tooltip，跟 DrawHoverTooltip 同一套
// 版面邏輯，但觸發條件是「這格正在等待選擇」（g_pickSection/g_pickSlot），
// 不是滑鼠 hover——玩家點格子進入選擇模式後，就算滑鼠移開也要看得到提示，
// 不能只靠 hover 才顯示。
void DrawPickingTooltip(Gdiplus::Graphics &g) {
  int section = g_pickSection.load();
  int slot = g_pickSlot.load();
  if (section < 0 || slot < 0)
    return;

  RECT slotRc = SlotRc(section, slot);
  int tipW = (int)(140 * g_scaleX);
  int tipH = (int)(24 * g_scaleY);
  int tipX = slotRc.left;
  int tipY = slotRc.top - tipH - (int)(4 * g_scaleY);
  if (tipY < (int)((kTitleH + kTabH) * g_scaleY))
    tipY = slotRc.bottom + (int)(4 * g_scaleY);
  RECT tipRc = {tipX, tipY, tipX + tipW, tipY + tipH};
  DrawRoundRect(g, tipRc, Gdiplus::Color(240, 20, 16, 14), Gdiplus::Color(255, 255, 220, 120),
                1.5f);
  DrawTextIn(g, tipRc, L"請點背包道具", Gdiplus::Color(255, 255, 220, 120), 11, false, true);
}

void DrawSection(Gdiplus::Graphics &g, int section, const AutoPotionSection &sec,
                 const wchar_t *title, Gdiplus::Color barColor, int cur, int maxv) {
  RECT box = (section == 0) ? HealBoxRc() : ManaBoxRc();
  DrawRoundRect(g, box, Gdiplus::Color(255, 48, 36, 30),
                Gdiplus::Color(255, 120, 95, 55), 1.5f);

  // 標題靠左（對齊截圖「恢復道具設定」）
  RECT titleRc = box;
  titleRc.left += (int)(6 * g_scaleX);
  titleRc.top += (int)(2 * g_scaleY);
  titleRc.bottom = titleRc.top + (int)(16 * g_scaleY);
  titleRc.right = box.right - (int)(4 * g_scaleX);
  DrawTextIn(g, titleRc, title, Gdiplus::Color(255, 230, 210, 160), 12, true, false);

  // HP/MP 條 + 「當前/最大」文字（同步角色）
  RECT barRc = titleRc;
  barRc.top = titleRc.bottom + (int)(2 * g_scaleY);
  barRc.bottom = barRc.top + (int)(12 * g_scaleY);
  const int textW = (int)(90 * g_scaleX);
  barRc.right = box.right - (int)(8 * g_scaleX) - textW;
  if (barRc.right < barRc.left + (int)(40 * g_scaleX))
    barRc.right = barRc.left + (int)(40 * g_scaleX);

  DrawRoundRect(g, barRc, Gdiplus::Color(255, 25, 20, 18),
                Gdiplus::Color(255, 80, 70, 50), 1.0f);
  double ratio = 0.0;
  if (maxv > 0)
    ratio = (double)cur / (double)maxv;
  if (ratio < 0.0)
    ratio = 0.0;
  if (ratio > 1.0)
    ratio = 1.0;
  int fillW = (int)((barRc.right - barRc.left) * ratio);
  if (fillW > 0) {
    Gdiplus::SolidBrush barBr(barColor);
    g.FillRectangle(&barBr, (Gdiplus::REAL)barRc.left, (Gdiplus::REAL)barRc.top,
                    (Gdiplus::REAL)fillW, (Gdiplus::REAL)(barRc.bottom - barRc.top));
  }

  RECT numRc = barRc;
  numRc.left = barRc.right + (int)(4 * g_scaleX);
  numRc.right = box.right - (int)(4 * g_scaleX);
  wchar_t vit[32] = {};
  if (maxv > 0)
    swprintf_s(vit, L"%d/%d", cur, maxv);
  else
    wcscpy_s(vit, L"\u2014/\u2014"); // —/—
  DrawTextIn(g, numRc, vit, Gdiplus::Color(255, 230, 220, 200), 10, false, false);

  for (int i = 0; i < kAutoPotionSlotsPerSection; i++)
    DrawSlot(g, section, i, sec.slots[i]);
}

void DrawPlaceholderQuad(Gdiplus::Graphics &g, const RECT &box, const wchar_t *title,
                         int cols, int rows, bool crossLayout) {
  DrawRoundRect(g, box, Gdiplus::Color(255, 48, 36, 30),
                Gdiplus::Color(255, 120, 95, 55), 1.5f);
  RECT titleRc = box;
  titleRc.left += (int)(6 * g_scaleX);
  titleRc.top += (int)(4 * g_scaleY);
  titleRc.bottom = titleRc.top + (int)(18 * g_scaleY);
  DrawTextIn(g, titleRc, title, Gdiplus::Color(255, 230, 210, 160), 12, true, false);

  const int slot = (int)(36 * ((g_scaleX < g_scaleY) ? g_scaleX : g_scaleY));
  const int gap = (int)(6 * g_scaleX);
  if (crossLayout) {
    // 十字 5 槽（截圖 BUFF-固定）
    int cx = (box.left + box.right) / 2;
    int cy = (box.top + box.bottom) / 2 + (int)(6 * g_scaleY);
    POINT pts[5] = {
        {cx, cy - slot - gap},
        {cx - slot - gap, cy},
        {cx, cy},
        {cx + slot + gap, cy},
        {cx, cy + slot + gap},
    };
    for (int i = 0; i < 5; i++) {
      RECT rc = {pts[i].x - slot / 2, pts[i].y - slot / 2, pts[i].x + slot / 2,
                 pts[i].y + slot / 2};
      DrawRoundRect(g, rc, Gdiplus::Color(255, 40, 32, 28),
                    Gdiplus::Color(255, 160, 130, 70), 1.2f);
    }
  } else {
    int gridW = cols * slot + (cols - 1) * gap;
    int gridH = rows * slot + (rows - 1) * gap;
    int ox = (box.left + box.right - gridW) / 2;
    int oy = titleRc.bottom + (int)(8 * g_scaleY);
    if (oy + gridH > box.bottom - 4)
      oy = box.bottom - gridH - 4;
    for (int r = 0; r < rows; r++) {
      for (int c = 0; c < cols; c++) {
        RECT rc;
        rc.left = ox + c * (slot + gap);
        rc.top = oy + r * (slot + gap);
        rc.right = rc.left + slot;
        rc.bottom = rc.top + slot;
        DrawRoundRect(g, rc, Gdiplus::Color(255, 40, 32, 28),
                      Gdiplus::Color(255, 160, 130, 70), 1.2f);
      }
    }
  }
}

void DrawBuffPage(Gdiplus::Graphics &g, const AutoPotionConfig &cfg) {
  RECT tl, tr, br;
  ContentQuads(&tl, &tr, nullptr, &br);
  DrawPlaceholderQuad(g, tl, L"BUFF-固定", 0, 0, true);
  DrawPlaceholderQuad(g, tr, L"BUFF-自訂", 3, 3, false);

  // 左下：恢復道具——條上顯示伺服器權威 cur/max（g_vitals）
  int curHp = 0, maxHp = 0, curMp = 0, maxMp = 0;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_vitals.hasHp) {
      curHp = g_vitals.curHp;
      maxHp = g_vitals.maxHp;
    }
    if (g_vitals.hasMp) {
      curMp = g_vitals.curMp;
      maxMp = g_vitals.maxMp;
    }
  }
  DrawSection(g, 0, cfg.heal, L"治療設定", Gdiplus::Color(255, 180, 50, 50), curHp,
              maxHp);
  DrawSection(g, 1, cfg.mana, L"補魔設定", Gdiplus::Color(255, 50, 90, 180), curMp,
              maxMp);

  // 右下變身佔位
  DrawRoundRect(g, br, Gdiplus::Color(255, 48, 36, 30),
                Gdiplus::Color(255, 120, 95, 55), 1.5f);
  RECT morphTitle = br;
  morphTitle.left += (int)(6 * g_scaleX);
  morphTitle.top += (int)(4 * g_scaleY);
  morphTitle.bottom = morphTitle.top + (int)(18 * g_scaleY);
  DrawTextIn(g, morphTitle, L"設定變身", Gdiplus::Color(255, 230, 210, 160), 12, true,
             false);
  RECT preview = br;
  preview.left += (int)(10 * g_scaleX);
  preview.top = morphTitle.bottom + (int)(6 * g_scaleY);
  preview.right = br.left + (br.right - br.left) * 2 / 3;
  preview.bottom = br.bottom - (int)(10 * g_scaleY);
  DrawRoundRect(g, preview, Gdiplus::Color(255, 30, 24, 20),
                Gdiplus::Color(255, 100, 80, 50), 1.0f);
  DrawTextIn(g, preview, L"（預覽）", Gdiplus::Color(255, 120, 110, 90), 11, false,
             true);
  const int slot = (int)(36 * ((g_scaleX < g_scaleY) ? g_scaleX : g_scaleY));
  int sx = preview.right + (int)(8 * g_scaleX);
  int sy = preview.top;
  for (int i = 0; i < 3; i++) {
    RECT rc = {sx, sy + i * (slot + (int)(8 * g_scaleY)), sx + slot,
               sy + i * (slot + (int)(8 * g_scaleY)) + slot};
    DrawRoundRect(g, rc, Gdiplus::Color(255, 40, 32, 28),
                  Gdiplus::Color(255, 160, 130, 70), 1.2f);
  }
}

void DrawItemPage(Gdiplus::Graphics &g) {
  RECT o = ContentOuterRc();
  DrawRoundRect(g, o, Gdiplus::Color(255, 48, 36, 30),
                Gdiplus::Color(255, 120, 95, 55), 1.5f);
  DrawTextIn(g, o, L"道具頁面（規劃中）", Gdiplus::Color(255, 200, 180, 140), 16, true,
             true);
}

void DrawBackPage(Gdiplus::Graphics &g) {
  RECT o = ContentOuterRc();
  DrawRoundRect(g, o, Gdiplus::Color(255, 48, 36, 30),
                Gdiplus::Color(255, 120, 95, 55), 1.5f);
  DrawTextIn(g, o, L"返回頁面（規劃中）", Gdiplus::Color(255, 200, 180, 140), 16, true,
             true);
}

// 2026-09-10：「其他」分頁。「自動修理武器」「自動吃肉」讀 cfg；
// 「顯示傷害」讀 AttackDamageHook；其餘 g_miscToggle。
void DrawMiscPage(Gdiplus::Graphics &g, const AutoPotionConfig &cfg) {
  RECT o = ContentOuterRc();
  DrawRoundRect(g, o, Gdiplus::Color(255, 48, 36, 30),
                Gdiplus::Color(255, 120, 95, 55), 1.5f);
  for (int i = 0; i < kMiscToggleCount; i++) {
    RECT row = MiscToggleRc(i);
    int boxSize = (int)(18 * g_scaleY);
    RECT box;
    box.left = row.left;
    box.top = row.top + ((row.bottom - row.top) - boxSize) / 2;
    box.right = box.left + boxSize;
    box.bottom = box.top + boxSize;
    bool checked;
    if (i == kMiscIdx_Whetstone) {
      checked = cfg.whetstone;
    } else if (i == kMiscIdx_EatMeat) {
      checked = cfg.eatMeat;
    } else if (i == kMiscIdx_ShowDamage) {
      checked = AttackDamageHook_IsEnabled();
    } else {
      checked = g_miscToggle[i];
    }
    DrawRoundRect(g, box,
                 checked ? Gdiplus::Color(255, 90, 140, 70)
                         : Gdiplus::Color(255, 30, 24, 20),
                 Gdiplus::Color(255, 160, 130, 70), 1.5f);
    if (checked) {
      DrawTextIn(g, box, L"✓", Gdiplus::Color(255, 230, 255, 210), 12, true,
                true);
    }
    RECT label = row;
    label.left = box.right + (int)(10 * g_scaleX);
    DrawTextIn(g, label, kMiscToggleLabels[i], Gdiplus::Color(255, 220, 210, 190),
              13, false, false);
  }
}

void DrawTabs(Gdiplus::Graphics &g) {
  for (int i = 0; i < Tab_Count; i++) {
    RECT rc = TabRc(i);
    const bool active = (g_activeTab == i);
    const bool pressed = (g_pressedTab == i);
    Gdiplus::Color fill =
        active ? Gdiplus::Color(255, 70, 110, 50)
               : (pressed ? Gdiplus::Color(255, 60, 48, 40)
                          : Gdiplus::Color(255, 45, 36, 30));
    Gdiplus::Color stroke =
        active ? Gdiplus::Color(255, 140, 200, 90) : Gdiplus::Color(255, 120, 95, 55);
    DrawRoundRect(g, rc, fill, stroke, active ? 2.0f : 1.2f);
    DrawTextIn(g, rc, kTabNames[i], Gdiplus::Color(255, 240, 230, 200), 12, active,
               true);
  }
}

void DrawInto(HDC hdc, void * /*bits*/, int winW, int winH) {
  Gdiplus::Graphics g(hdc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

  Gdiplus::SolidBrush bg(Gdiplus::Color(255, 42, 32, 26));
  g.FillRectangle(&bg, 0, 0, winW, winH);
  Gdiplus::Pen border(Gdiplus::Color(255, 140, 110, 60), 2.0f);
  g.DrawRectangle(&border, 1, 1, winW - 3, winH - 3);

  RECT tb = TitleBarRc();
  DrawRoundRect(g, tb, Gdiplus::Color(255, 28, 22, 18),
                Gdiplus::Color(255, 100, 80, 45), 1.0f);
  DrawTextIn(g, tb, L"遊玩輔助", Gdiplus::Color(255, 240, 230, 200), 13, true, true);

  RECT cx = CloseXRc();
  DrawRoundRect(g, cx,
                g_pressedClose ? Gdiplus::Color(255, 120, 40, 40)
                               : Gdiplus::Color(255, 70, 50, 40),
                Gdiplus::Color(255, 180, 140, 80));
  DrawTextIn(g, cx, L"X", Gdiplus::Color(255, 255, 255, 255), 12, true, true);

  DrawTabs(g);

  AutoPotionConfig cfg;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    cfg = g_cfg;
  }

  if (g_activeTab == Tab_Buff) {
    DrawBuffPage(g, cfg);
    DrawHoverTooltip(g, cfg);
    DrawPickingTooltip(g);
  } else if (g_activeTab == Tab_Item) {
    DrawItemPage(g);
  } else if (g_activeTab == Tab_Teleport) {
    DrawBackPage(g); // 傳送頁內容（暫用既有 DrawBackPage）
  } else if (g_activeTab == Tab_Misc) {
    DrawMiscPage(g, cfg);
  }

  DrawTextIn(g, HintBarRc(),
             g_activeTab == Tab_Buff
                 ? L"點格子選背包道具；點格子下方 % 設門檻｜啟動對應 autoPotionEnabled"
                 : L"",
             Gdiplus::Color(255, 140, 130, 110), 10, false, false);

  RECT save = SaveBtnRc();
  RECT enable = EnableBtnRc();
  RECT closeB = CloseBtnRc();
  DrawRoundRect(g, save,
                g_pressedSave ? Gdiplus::Color(255, 90, 70, 40)
                              : Gdiplus::Color(255, 70, 55, 35),
                Gdiplus::Color(255, 180, 150, 80), 2.0f);
  DrawTextIn(g, save, L"儲存", Gdiplus::Color(255, 255, 240, 200), 13, true, true);

  // 啟動／停止：對應 C_PlaySupport 的 pc.autoPotionEnabled
  const bool running = cfg.enabled;
  DrawRoundRect(g, enable,
                g_pressedEnable
                    ? Gdiplus::Color(255, 90, 70, 40)
                    : (running ? Gdiplus::Color(255, 140, 70, 40)
                               : Gdiplus::Color(255, 60, 100, 50)),
                running ? Gdiplus::Color(255, 220, 140, 80)
                        : Gdiplus::Color(255, 140, 200, 90),
                2.0f);
  DrawTextIn(g, enable, running ? L"停止" : L"啟動",
             Gdiplus::Color(255, 255, 240, 200), 13, true, true);

  DrawRoundRect(g, closeB,
                g_pressedCloseBtn ? Gdiplus::Color(255, 200, 100, 40)
                                  : Gdiplus::Color(255, 160, 90, 35),
                Gdiplus::Color(255, 220, 150, 80), 2.0f);
  DrawTextIn(g, closeB, L"關閉", Gdiplus::Color(255, 255, 240, 200), 13, true, true);
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

  BYTE *bits = (BYTE *)bitsPtr;
  size_t total = (size_t)winW * winH * 4;
  for (size_t i = 3; i < total; i += 4)
    bits[i] = 0xFF;

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

void HideWindow(HWND hwnd) {
  // 只取消打字輸入，不取消還在飛行中的道具選擇——玩家點「關閉」本來就是「不存
  // 檔」的既有設計，這裡讓伺服器回覆自然抵達、寫進記憶體裡的 g_cfg 就好（視窗
  // 隱藏不影響資料本身），不要額外把它攔截丟棄，避免看起來像「選好的東西憑空
  // 消失」。反正沒按存檔，這次沒存到檔案/送到伺服器本來就是預期行為。
  CancelTextEdit();
  ShowWindow(hwnd, SW_HIDE);
  g_visible.store(false);
  g_vitalsDirty.store(false);
  QueueUiNotify(false); // 伺服器停止推 vitals
}

void CommitEdit(bool asSkill) {
  if (g_editMode < 0)
    return;
  // 2026-09-09：g_editBuf 是空的代表玩家沒有打字（例如改用「點背包道具」流程，
  // 伺服器回覆還沒送到），不是「打了 0 或空白想清空」——不要把這種情況也當成
  // 清空來處理，不然存檔按鈕會把一個「還在等回覆」的格子誤清空，而且下面的
  // ClearEdit() 還會把 g_pickSection/g_pickSlot 一起重置，導致稍後真正抵達的
  // 伺服器回覆對不上目標、被整包丟棄——玩家會看到「剛選好的藥水消失了」。
  if (g_editBuf[0] == 0) {
    CancelTextEdit(); // 只取消打字狀態，保留還在飛行中的道具選擇
    return;
  }
  int val = _wtoi(g_editBuf);
  {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_editMode == 0 || g_editMode == 1) {
      AutoPotionSection &sec = (g_editMode == 0) ? g_cfg.heal : g_cfg.mana;
      if (g_editSlot >= 0 && g_editSlot < kAutoPotionSlotsPerSection) {
        if (val <= 0) {
          sec.slots[g_editSlot].kind = AutoPotionSlot_None;
          sec.slots[g_editSlot].id = 0;
        } else {
          sec.slots[g_editSlot].kind =
              asSkill ? AutoPotionSlot_Skill : AutoPotionSlot_Item;
          sec.slots[g_editSlot].id = val;
        }
        // 手動打字輸入沒有名稱/數量可以快取，清掉這格舊的快取，避免 hover 顯示
        // 跟現在內容對不上的舊道具名稱。
        ClearSlotCache(g_editMode, g_editSlot);
      }
    } else if (g_editMode == 2 || g_editMode == 3) {
      // 2026-09-10：每槽各自的百分比門檻（g_editSlot 指是哪一槽），不再是整個
      // 分類共用一個值。
      AutoPotionSection &sec = (g_editMode == 2) ? g_cfg.heal : g_cfg.mana;
      if (val < 0)
        val = 0;
      if (val > 100)
        val = 100;
      if (g_editSlot >= 0 && g_editSlot < kAutoPotionSlotsPerSection)
        sec.slots[g_editSlot].thresholdPercent = val;
    }
  } // 釋放鎖，QueueSave() 內部自己也會上鎖，兩邊不能疊在一起
  // 這裡是「玩家真的手動打字提交」，跟道具選擇流程無關，兩邊狀態都清掉沒問題。
  ClearEdit();
  // 2026-09-10：跟點選道具流程一致——輸入編號/百分比後提交（Enter 或點別處）
  // 就是最終確認動作，直接自動存檔＋送伺服器，不用再另外按「儲存」。
  QueueSave();
}

void OnLButtonDown(HWND hwnd, int x, int y) {
  if (g_editMode >= 0 && g_editBuf[0] != 0) {
    bool asSkill = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    CommitEdit(asSkill);
  }
  if (PtIn(CloseXRc(), x, y)) {
    g_pressedClose = 1;
    PaintLayered(hwnd);
    return;
  }
  for (int i = 0; i < Tab_Count; i++) {
    if (PtIn(TabRc(i), x, y)) {
      g_pressedTab = i;
      PaintLayered(hwnd);
      return;
    }
  }
  if (PtIn(EnableBtnRc(), x, y)) {
    g_pressedEnable = 1;
    PaintLayered(hwnd);
    return;
  }
  if (PtIn(SaveBtnRc(), x, y)) {
    g_pressedSave = 1;
    PaintLayered(hwnd);
    return;
  }
  if (PtIn(CloseBtnRc(), x, y)) {
    g_pressedCloseBtn = 1;
    PaintLayered(hwnd);
    return;
  }

  if (g_activeTab == Tab_Misc) {
    for (int i = 0; i < kMiscToggleCount; i++) {
      if (PtIn(MiscToggleRc(i), x, y)) {
        if (i == kMiscIdx_Whetstone || i == kMiscIdx_EatMeat) {
          // 真後端：改 cfg 並直接存檔＋送 12-byte status 包（QueueSave 內部
          // 已經包含 SendStatusToServer，見 AutoPotionOverlay_PumpPendingSave）。
          {
            std::lock_guard<std::mutex> lock(g_lock);
            if (i == kMiscIdx_Whetstone)
              g_cfg.whetstone = !g_cfg.whetstone;
            else
              g_cfg.eatMeat = !g_cfg.eatMeat;
          }
          QueueSave();
        } else if (i == kMiscIdx_ShowDamage) {
          bool next = !AttackDamageHook_IsEnabled();
          AttackDamageHook_SetEnabled(next);
          {
            std::lock_guard<std::mutex> lock(g_lock);
            g_cfg.showDamage = next;
          }
          QueueSave(); // 只本機 cfg；封包忽略 showDamage
        } else {
          g_miscToggle[i] = !g_miscToggle[i];
        }
        PaintLayered(hwnd);
        return;
      }
    }
    return;
  }

  // 僅 BUFF 頁的恢復槽可互動
  if (g_activeTab != Tab_Buff)
    return;

  for (int s = 0; s < 2; s++) {
    for (int i = 0; i < kAutoPotionSlotsPerSection; i++) {
      if (PtIn(SlotLabelRc(s, i), x, y)) {
        CancelTextEdit();
        g_editMode = s + 2;
        g_editSlot = i;
        {
          std::lock_guard<std::mutex> lock(g_lock);
          const AutoPotionSlot &slot =
              (s == 0) ? g_cfg.heal.slots[i] : g_cfg.mana.slots[i];
          swprintf_s(g_editBuf, L"%d", slot.thresholdPercent);
        }
        PaintLayered(hwnd);
        return;
      }
    }
    for (int i = 0; i < kAutoPotionSlotsPerSection; i++) {
      if (!PtIn(SlotRc(s, i), x, y))
        continue;
      AutoPotionSlot slot;
      {
        std::lock_guard<std::mutex> lock(g_lock);
        slot = (s == 0) ? g_cfg.heal.slots[i] : g_cfg.mana.slots[i];
      }
      if (slot.kind != AutoPotionSlot_None && slot.id > 0) {
        ClearEdit();
        g_pickSection.store(s);
        g_pickSlot.store(i);
      } else {
        ClearEdit();
        g_editMode = s;
        g_editSlot = i;
        g_editBuf[0] = 0;
        g_pickSection.store(s);
        g_pickSlot.store(i);
      }
      PaintLayered(hwnd);
      return;
    }
  }
}

void OnLButtonUp(HWND hwnd, int x, int y) {
  if (g_pressedTab >= 0) {
    int t = g_pressedTab;
    g_pressedTab = -1;
    if (PtIn(TabRc(t), x, y)) {
      // 四個分頁一律切換；關閉只用 X／關閉鈕（舊 Tab_Back 點了會 HideWindow，
      // 但標籤文字已改成「傳送」，造成點傳送就關窗）。
      g_activeTab = t;
      CancelTextEdit();
    }
    PaintLayered(hwnd);
    return;
  }
  if (g_pressedClose) {
    g_pressedClose = 0;
    if (PtIn(CloseXRc(), x, y))
      HideWindow(hwnd);
    else
      PaintLayered(hwnd);
    return;
  }
  if (g_pressedEnable) {
    g_pressedEnable = 0;
    if (PtIn(EnableBtnRc(), x, y)) {
      bool nowEnabled = false;
      {
        std::lock_guard<std::mutex> lock(g_lock);
        g_cfg.enabled = !g_cfg.enabled;
        nowEnabled = g_cfg.enabled;
      }
      ApLog("toggle enabled -> %d (C_PlaySupport autoPotionEnabled)",
            (int)nowEnabled);
      QueueSave();
    }
    PaintLayered(hwnd);
    return;
  }
  if (g_pressedSave) {
    g_pressedSave = 0;
    if (PtIn(SaveBtnRc(), x, y)) {
      CommitEdit(false);
      QueueSave();
    }
    PaintLayered(hwnd);
    return;
  }
  if (g_pressedCloseBtn) {
    g_pressedCloseBtn = 0;
    if (PtIn(CloseBtnRc(), x, y))
      HideWindow(hwnd);
    else
      PaintLayered(hwnd);
    return;
  }
}

void OnMouseMove(HWND hwnd, int x, int y) {
  int newSection = -1, newSlot = -1;
  if (g_activeTab == Tab_Buff) {
    for (int s = 0; s < 2 && newSection < 0; s++) {
      for (int i = 0; i < kAutoPotionSlotsPerSection; i++) {
        if (PtIn(SlotRc(s, i), x, y)) {
          newSection = s;
          newSlot = i;
          break;
        }
      }
    }
  }
  if (newSection != g_hoverSection || newSlot != g_hoverSlot) {
    g_hoverSection = newSection;
    g_hoverSlot = newSlot;
    PaintLayered(hwnd);
  }
  TRACKMOUSEEVENT tme = {sizeof(tme)};
  tme.dwFlags = TME_LEAVE;
  tme.hwndTrack = hwnd;
  TrackMouseEvent(&tme);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
  case WM_SHOW_AUTOPOTION: {
    AutoPotionConfig loaded = AutoPotionConfig_Load();
    {
      std::lock_guard<std::mutex> lock(g_lock);
      g_cfg = loaded;
    }
    AttackDamageHook_SetEnabled(loaded.showDamage);
    // 2026-09-10：名稱/數量現在跟著設定檔一起讀（AutoPotionSlot.name/count），
    // 不用再清空重置——剛開視窗就能看到上次存檔當下的名稱/數量，不用等重新
    // 點選才有東西可顯示。
    ClearEdit();
    g_activeTab = Tab_Buff;
    g_caretOn = true;
    g_visible.store(true);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    // 不要 SetForegroundWindow：會把焦點從遊戲 UI thread 搶走，弄壞 LineageIme
    SetTimer(hwnd, TIMER_CARET, 500, NULL);
    SetTimer(hwnd, TIMER_VITALS, 200, NULL); // 節流重繪，非 DMA
    QueueUiNotify(true); // 伺服器 seed + 開著才推 vitals
    PaintLayered(hwnd);
    ApLog("shown");
    return 0;
  }
  case WM_HIDE_AUTOPOTION:
    KillTimer(hwnd, TIMER_CARET);
    KillTimer(hwnd, TIMER_VITALS);
    HideWindow(hwnd);
    return 0;
  case WM_TIMER:
    if (wp == TIMER_CARET) {
      if (g_editMode >= 0) {
        g_caretOn = !g_caretOn;
        PaintLayered(hwnd);
      } else {
        g_caretOn = true;
      }
      return 0;
    }
    if (wp == TIMER_VITALS) {
      // 僅可見且有新 vitals 時重繪（開著才同步畫面）
      if (g_visible.load() && g_vitalsDirty.exchange(false) &&
          g_activeTab == Tab_Buff)
        PaintLayered(hwnd);
      return 0;
    }
    return 0;
  case WM_AUTOPOTION_RESOLVE_REPLY: {
    ResolveReplyMsg *m = (ResolveReplyMsg *)lp;
    if (m) {
      if (g_pickSection.load() == m->section && g_pickSlot.load() == m->slot) {
        if (m->success) {
          bool changed = false;
          {
            std::lock_guard<std::mutex> lock(g_lock);
            AutoPotionSection &sec = (m->section == 0) ? g_cfg.heal : g_cfg.mana;
            AutoPotionSlot &dst = sec.slots[m->slot];
            // 2026-09-10：選到的道具（含數量）只要跟目前存檔內容有任何差異
            // 就要存檔——點同一瓶但數量因為玩家中途用掉/撿到而不同，也要更新
            // 存檔裡的數量快照，不然下次進遊戲顯示的還是舊數字。
            changed = (dst.kind != AutoPotionSlot_Item) ||
                      (dst.id != m->templateItemId) ||
                      (dst.gfxid != m->gfxid) ||
                      (dst.count != m->count) ||
                      (wcscmp(dst.name, m->name) != 0);
            if (changed) {
              dst.kind = AutoPotionSlot_Item;
              dst.id = m->templateItemId;
              dst.gfxid = m->gfxid;
              wcscpy_s(dst.name, m->name);
              dst.count = m->count;
            }
          } // 釋放鎖，QueueSave() 內部自己也會上鎖，兩邊不能疊在一起
          if (changed) {
            ApLog("resolve ok section=%d slot=%d templateId=%d gfxid=%d count=%d, "
                  "auto-saving",
                  m->section, m->slot, m->templateItemId, m->gfxid, m->count);
            QueueSave(); // 2026-09-09：點選道具就直接存檔＋送伺服器，不用再按存檔
          } else {
            ApLog("resolve ok section=%d slot=%d same as current, no-op",
                  m->section, m->slot);
          }
          ClearEdit();
        } else {
          ApLog("resolve failed section=%d slot=%d", m->section, m->slot);
          ClearEdit();
        }
        PaintLayered(hwnd);
      }
      delete m;
    }
    return 0;
  }
  case WM_AUTOPOTION_SLOT_COUNTS: {
    SlotCountsBatchMsg *m = (SlotCountsBatchMsg *)lp;
    if (m) {
      {
        std::lock_guard<std::mutex> lock(g_lock);
        for (int i = 0; i < m->n; i++) {
          int section = m->items[i].section;
          int slot = m->items[i].slot;
          if (section < 0 || section > 1 || slot < 0 || slot >= 5)
            continue;
          AutoPotionSection &sec = (section == 0) ? g_cfg.heal : g_cfg.mana;
          AutoPotionSlot &dst = sec.slots[slot];
          if (dst.kind == AutoPotionSlot_Item && dst.id > 0) {
            dst.count = m->items[i].count;
            ApLog("slot-count section=%d slot=%d count=%d", section, slot,
                  m->items[i].count);
          }
        }
      }
      delete m;
      if (g_visible.load())
        PaintLayered(hwnd);
    }
    return 0;
  }
  case WM_LBUTTONDOWN:
    OnLButtonDown(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
    return 0;
  case WM_LBUTTONUP:
    OnLButtonUp(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
    return 0;
  case WM_MOUSEMOVE:
    OnMouseMove(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
    return 0;
  case WM_MOUSELEAVE:
    if (g_hoverSection >= 0 || g_hoverSlot >= 0) {
      g_hoverSection = -1;
      g_hoverSlot = -1;
      PaintLayered(hwnd);
    }
    return 0;
  case WM_NCHITTEST: {
    POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    ScreenToClient(hwnd, &pt);
    if (PtIn(TitleBarRc(), pt.x, pt.y) && !PtIn(CloseXRc(), pt.x, pt.y))
      return HTCAPTION;
    return HTCLIENT;
  }
  case WM_EXITSIZEMOVE:
    g_userMoved = true;
    return 0;
  case WM_CHAR: {
    if (g_editMode < 0)
      return 0;
    wchar_t ch = (wchar_t)wp;
    if (ch == 13) { // Enter
      bool asSkill = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      CommitEdit(asSkill);
      PaintLayered(hwnd);
      return 0;
    }
    if (ch == 27) { // Esc
      ClearEdit();
      PaintLayered(hwnd);
      return 0;
    }
    if (ch == 8) { // Backspace
      size_t n = wcslen(g_editBuf);
      if (n > 0)
        g_editBuf[n - 1] = 0;
      PaintLayered(hwnd);
      return 0;
    }
    if (ch >= L'0' && ch <= L'9') {
      size_t n = wcslen(g_editBuf);
      if (n + 1 < _countof(g_editBuf)) {
        g_editBuf[n] = ch;
        g_editBuf[n + 1] = 0;
      }
      PaintLayered(hwnd);
    }
    return 0;
  }
  case WM_KEYDOWN:
    if (wp == VK_ESCAPE) {
      if (g_editMode >= 0) {
        ClearEdit();
        PaintLayered(hwnd);
      } else {
        HideWindow(hwnd);
      }
      return 0;
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
  if (!g_gdiplusStarted) {
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, NULL) == Gdiplus::Ok)
      g_gdiplusStarted = true;
    else
      ApLog("GdiplusStartup failed");
  }

  HINSTANCE hinst = hins ? hins : GetModuleHandleW(NULL);
  WNDCLASSEXW cls = {};
  cls.cbSize = sizeof(cls);
  cls.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
  cls.lpfnWndProc = WndProc;
  cls.hInstance = hinst;
  cls.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
  cls.hbrBackground = NULL;
  cls.lpszClassName = kClassName;
  RegisterClassExW(&cls);

  HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                              kClassName, L"遊玩輔助設定", WS_POPUP, 0, 0, kBaseW, kBaseH,
                              g_hGameWnd, NULL, hinst, NULL);
  if (!hwnd) {
    ApLog("CreateWindowExW failed gle=%lu", (unsigned long)GetLastError());
    return 1;
  }
  {
    std::lock_guard<std::mutex> lock(g_lock);
    g_hwnd = hwnd;
    g_threadHwnd = hwnd;
  }
  ApLog("overlay thread ready hwnd=0x%p", hwnd);

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
  g_thread = CreateThread(NULL, 0, OverlayThreadProc, NULL, 0, NULL);
  if (!g_thread) {
    ApLog("CreateThread failed");
    return false;
  }
  for (int i = 0; i < 100 && !g_threadHwnd; i++)
    Sleep(10);
  return g_threadHwnd != NULL;
}

} // namespace

void AutoPotionOverlay_Show() {
  HWND hwnd = NULL;
  bool visible = false;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    hwnd = g_threadHwnd;
    visible = g_visible.load();
  }
  if (!hwnd) {
    StartThread();
    std::lock_guard<std::mutex> lock(g_lock);
    hwnd = g_threadHwnd;
  }
  if (!hwnd) {
    ApLog("Show failed: no hwnd");
    return;
  }
  if (visible)
    PostMessageW(hwnd, WM_HIDE_AUTOPOTION, 0, 0);
  else
    PostMessageW(hwnd, WM_SHOW_AUTOPOTION, 0, 0);
}

bool AutoPotionOverlay_HitTestSlot(int screenX, int screenY, int *outSection,
                                   int *outIndex) {
  HWND hwnd = NULL;
  bool visible = false;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    hwnd = g_hwnd;
    visible = g_visible.load();
  }
  if (!hwnd || !visible || !IsWindow(hwnd))
    return false;
  if (g_activeTab != Tab_Buff)
    return false;

  POINT pt = {screenX, screenY};
  ScreenToClient(hwnd, &pt);

  for (int s = 0; s < 2; s++) {
    for (int i = 0; i < kAutoPotionSlotsPerSection; i++) {
      if (PtIn(SlotRc(s, i), pt.x, pt.y)) {
        if (outSection)
          *outSection = s;
        if (outIndex)
          *outIndex = i;
        return true;
      }
    }
  }
  return false;
}

bool AutoPotionOverlay_IsPicking(int *outSection, int *outSlot) {
  if (!g_visible.load())
    return false;
  int section = g_pickSection.load();
  int slot = g_pickSlot.load();
  if (section < 0 || slot < 0)
    return false;
  if (outSection)
    *outSection = section;
  if (outSlot)
    *outSlot = slot;
  return true;
}

void AutoPotionOverlay_OnResolveReply(bool success, int section, int slot,
                                      int templateItemId, int gfxid, int count,
                                      const wchar_t *name) {
  HWND hwnd = NULL;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    hwnd = g_threadHwnd;
  }
  if (!hwnd)
    return;
  ResolveReplyMsg *m = new ResolveReplyMsg();
  m->success = success;
  m->section = section;
  m->slot = slot;
  m->templateItemId = templateItemId;
  m->gfxid = gfxid;
  m->count = count;
  m->name[0] = 0;
  if (name)
    wcsncpy_s(m->name, name, _countof(m->name) - 1);
  PostMessageW(hwnd, WM_AUTOPOTION_RESOLVE_REPLY, 0, (LPARAM)m);
}

void AutoPotionOverlay_PumpPendingSave() {
  if (InterlockedExchange(&g_pendingSave, 0) == 0)
    return;
  AutoPotionConfig cfg;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    cfg = g_pendingCfg;
  }
  ApLog("PumpPendingSave flush");
  AutoPotionConfig_Save(cfg);
  // 兩包分開：62-byte 喝水 + 12-byte 吃肉／修武（伺服器用長度分流）
  AutoPotionConfig_SendToServer(cfg);
  AutoPotionConfig_SendStatusToServer(cfg);
}

void AutoPotionOverlay_PumpPendingUiNotify() {
  LONG v = InterlockedExchange(&g_pendingUiNotify, -1);
  if (v < 0)
    return;
  ApLog("PumpPendingUiNotify visible=%ld", v);
  // 開面板：先灌 cfg 再通知 UI，讓伺服器推數量時 State 已是最新
  if (v != 0) {
    AutoPotionConfig cfg;
    {
      std::lock_guard<std::mutex> lock(g_lock);
      cfg = g_cfg;
    }
    AutoPotionConfig_SendToServer(cfg);
    AutoPotionConfig_SendStatusToServer(cfg);
  }
  AutoPotionConfig_SendUiVisible(v != 0);
}

void AutoPotionOverlay_OnHpUpdate(int cur, int max) {
  ClampVital(&cur, &max);
  {
    std::lock_guard<std::mutex> lock(g_lock);
    g_vitals.curHp = cur;
    g_vitals.maxHp = max;
    g_vitals.hasHp = (max > 0);
  }
  MarkVitalsDirtyIfVisible();
}

void AutoPotionOverlay_OnMpUpdate(int cur, int max) {
  ClampVital(&cur, &max);
  {
    std::lock_guard<std::mutex> lock(g_lock);
    g_vitals.curMp = cur;
    g_vitals.maxMp = max;
    g_vitals.hasMp = (max > 0);
  }
  MarkVitalsDirtyIfVisible();
}

void AutoPotionOverlay_OnVitalsUpdate(int curHp, int maxHp, int curMp, int maxMp) {
  ClampVital(&curHp, &maxHp);
  ClampVital(&curMp, &maxMp);
  {
    std::lock_guard<std::mutex> lock(g_lock);
    g_vitals.curHp = curHp;
    g_vitals.maxHp = maxHp;
    g_vitals.curMp = curMp;
    g_vitals.maxMp = maxMp;
    g_vitals.hasHp = (maxHp > 0);
    g_vitals.hasMp = (maxMp > 0);
  }
  MarkVitalsDirtyIfVisible();
}

void AutoPotionOverlay_OnSlotCounts(int section, int slot, int count) {
  // 單筆入口；批次由 OnSlotCountsBatch 處理（MimirPowerHook 開面板）
  AutoPotionOverlay_OnSlotCountsBatch(1, &section, &slot, &count);
}

void AutoPotionOverlay_OnSlotCountsBatch(int n, const int *sections, const int *slots,
                                         const int *counts) {
  if (n <= 0 || !sections || !slots || !counts)
    return;
  HWND hwnd = NULL;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    hwnd = g_threadHwnd;
  }
  if (!hwnd)
    return;
  SlotCountsBatchMsg *m = new SlotCountsBatchMsg();
  m->n = 0;
  for (int i = 0; i < n && m->n < 20; i++) {
    m->items[m->n].section = sections[i];
    m->items[m->n].slot = slots[i];
    m->items[m->n].count = counts[i];
    m->n++;
  }
  PostMessageW(hwnd, WM_AUTOPOTION_SLOT_COUNTS, 0, (LPARAM)m);
}
