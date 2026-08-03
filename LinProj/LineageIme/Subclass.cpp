// Subclass.cpp: 對照 Rust 版 ime_overlay/src/subclass.rs（Tier 1 範圍，見 Subclass.h）。
//
// 不能完全吃掉 IME 訊息 —— 遊戲自己有 polling 邏輯讀組字字串（ImmGetCompositionStringA）
// 跟最終結果（GCS_RESULTSTR），所以原 wndproc 還是要 call 一遍，讓遊戲拿到該拿的。
// WM_IME_NOTIFY 是例外：全部攔住不轉給原 wndproc（比照 Rust 版目前的「實驗 A 擴大版」
// 策略），因為 Lineage 對 IME 私有通知會強制 commit，可能偷偷把 IMM32 default IME
// window 隱藏；我們已經提前 DefWindowProcW 通知了，IMM32 state 會正確。
#include "Common.h"
#include "Subclass.h"
#include "Overlay.h"
#include "Candidates.h"
#include "TsfSink.h"
#include "Dbg.h"
#include <imm.h>
#include <unordered_map>
#include <mutex>
#include <string>

#pragma comment(lib, "imm32.lib")

const UINT WM_TSF_INIT_ON_UI_THREAD = WM_APP + 1;

namespace {

const wchar_t *TARGET_CLASS = L"LUnicodeEdit";
// IMN_OPENCANDIDATE / IMN_CHANGECANDIDATE / IMN_CLOSECANDIDATE 已由 <imm.h> 定義
// （值分別是 0x05/0x03/0x04，跟 Rust 版一致），這裡不重複宣告。
constexpr UINT GCS_COMPSTR_FLAG = 0x0008;
constexpr COLORREF EDIT_BG_COLOR = 0x00FFFFFF;
constexpr COLORREF EDIT_TEXT_COLOR = 0x00000000;

std::mutex g_subclassLock;
std::unordered_map<HWND, WNDPROC> g_subclassed;
WNDPROC g_gameWndProc = nullptr;
HBRUSH g_editBgBrush = nullptr;

bool ClassIsTarget(HWND hwnd) {
  wchar_t buf[64];
  int n = GetClassNameW(hwnd, buf, 64);
  if (n <= 0) return false;
  return wcscmp(buf, TARGET_CLASS) == 0;
}

std::wstring DescribeWindow(HWND hwnd) {
  HWND parent = GetParent(hwnd);
  BOOL visible = IsWindowVisible(hwnd);
  LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

  RECT win = {};
  wchar_t winText[64];
  if (GetWindowRect(hwnd, &win)) {
    swprintf_s(winText, L"win=(%ld,%ld %ldx%ld)", win.left, win.top, win.right - win.left,
               win.bottom - win.top);
  } else {
    wcscpy_s(winText, L"win=<err>");
  }

  RECT client = {};
  wchar_t clientText[32];
  if (GetClientRect(hwnd, &client)) {
    swprintf_s(clientText, L"client=%ldx%ld", client.right - client.left,
               client.bottom - client.top);
  } else {
    wcscpy_s(clientText, L"client=<err>");
  }

  wchar_t buf[256];
  swprintf_s(buf, L"parent=0x%p visible=%d style=0x%08X ex=0x%08X %s %s", parent, visible ? 1 : 0,
             (unsigned)style, (unsigned)exStyle, winText, clientText);
  return buf;
}

HBRUSH EditBgBrush() {
  if (!g_editBgBrush) g_editBgBrush = CreateSolidBrush(EDIT_BG_COLOR);
  return g_editBgBrush;
}

bool IsImeNotifyMessage(UINT msg) {
  return msg == WM_IME_STARTCOMPOSITION || msg == WM_IME_ENDCOMPOSITION ||
         msg == WM_IME_NOTIFY || msg == WM_IME_SETCONTEXT;
}

bool ShouldRefreshEdit(UINT msg) {
  return msg == WM_CHAR || msg == WM_KEYUP || msg == WM_SETTEXT || msg == WM_SETFOCUS ||
         msg == WM_IME_COMPOSITION;
}

// IMN_OPENCANDIDATE 時：設候選位置 + 列舉 thread/process 內的視窗，找出真正的
// 候選 UI window（診斷用，只在前 3 次做完整 enumeration）。
void RescueImm32CandidateWindow(HWND editHwnd) {
  static std::atomic<size_t> count{0};
  size_t n = ++count;
  if (n > 3) return;

  HIMC himc = ImmGetContext(editHwnd);
  if (himc) {
    RECT rc = {};
    GetClientRect(editHwnd, &rc);
    POINT origin = {0, rc.bottom};
    ClientToScreen(editHwnd, &origin);

    COMPOSITIONFORM comp = {};
    comp.dwStyle = CFS_POINT;
    ImmSetCompositionWindow(himc, &comp);

    CANDIDATEFORM cand = {};
    cand.dwIndex = 0;
    cand.dwStyle = CFS_CANDIDATEPOS;
    cand.ptCurrentPos = {0, rc.bottom};
    ImmSetCandidateWindow(himc, &cand);
    ImmReleaseContext(editHwnd, himc);

    IME_LOG("[ime-enum] #%zu OPENCANDIDATE edit=0x%p screen_origin=(%ld,%ld) ime_proxy=0x%p", n,
            editHwnd, origin.x, origin.y, ImmGetDefaultIMEWnd(editHwnd));
  }

  DWORD pid = 0;
  DWORD tid = GetWindowThreadProcessId(editHwnd, &pid);
  IME_LOG("[ime-enum] #%zu thread tid=%lu pid=%lu", n, (unsigned long)tid, (unsigned long)pid);

  EnumThreadWindows(
      tid,
      [](HWND hwnd, LPARAM lp) -> BOOL {
        size_t n = (size_t)lp;
        wchar_t buf[128] = {0};
        int len = GetClassNameW(hwnd, buf, 128);
        BOOL visible = IsWindowVisible(hwnd);
        RECT rect = {};
        GetWindowRect(hwnd, &rect);
        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        HWND parent = GetParent(hwnd);
        IME_LOG(
            "[ime-enum] #%zu thread-wnd hwnd=0x%p class='%ls' visible=%d ex=0x%08X parent=0x%p "
            "rect=(%ld,%ld %ldx%ld)",
            n, hwnd, len > 0 ? buf : L"<?>", visible ? 1 : 0, (unsigned)ex, parent, rect.left,
            rect.top, rect.right - rect.left, rect.bottom - rect.top);
        return TRUE;
      },
      (LPARAM)n);

  EnumWindows(
      [](HWND hwnd, LPARAM lp) -> BOOL {
        DWORD targetPid = (DWORD)lp;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != targetPid) return TRUE;
        wchar_t buf[128] = {0};
        int len = GetClassNameW(hwnd, buf, 128);
        if (len <= 0) return TRUE;
        std::wstring cl = buf;
        for (auto &c : cl) c = towlower(c);
        bool interesting = cl.find(L"ime") != std::wstring::npos ||
                           cl.find(L"candidate") != std::wstring::npos ||
                           cl.find(L"ctf") != std::wstring::npos ||
                           cl.find(L"tsf") != std::wstring::npos ||
                           cl.find(L"bopomofo") != std::wstring::npos ||
                           cl.find(L"pinyin") != std::wstring::npos ||
                           cl.find(L"reading") != std::wstring::npos ||
                           cl.find(L"composition") != std::wstring::npos ||
                           cl.find(L"chewing") != std::wstring::npos ||
                           cl.find(L"editcomposition") != std::wstring::npos ||
                           cl.find(L"phonetic") != std::wstring::npos ||
                           cl.find(L"hanyin") != std::wstring::npos ||
                           cl.find(L"natural") != std::wstring::npos;
        if (!interesting) return TRUE;
        BOOL visible = IsWindowVisible(hwnd);
        RECT rect = {};
        GetWindowRect(hwnd, &rect);
        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        HWND parent = GetParent(hwnd);
        IME_LOG(
            "[ime-enum] global IME-like hwnd=0x%p class='%ls' visible=%d ex=0x%08X parent=0x%p "
            "rect=(%ld,%ld %ldx%ld)",
            hwnd, buf, visible ? 1 : 0, (unsigned)ex, parent, rect.left, rect.top,
            rect.right - rect.left, rect.bottom - rect.top);
        return TRUE;
      },
      (LPARAM)pid);
}

LRESULT CALLBACK SubclassWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_SETFOCUS) {
    TsfSetLastFocusEdit(hwnd);
  }

  // === IMM32 default IME UI 路由補洞 ===
  // LUnicodeEdit 自己吃掉 IME 訊息不呼叫 DefWindowProc，IMM32 default IME
  // window 永遠收不到 IMN_OPENCANDIDATE。我們在原 wndproc 之前先手動
  // DefWindowProcW 一次補通知（只轉純通知類，避免 WM_CHAR 重複插入）。
  if (IsImeNotifyMessage(msg)) {
    DefWindowProcW(hwnd, msg, wParam, lParam);
  }

  bool blockOriginalForImeNotify = (msg == WM_IME_NOTIFY);

  if (msg == WM_IME_NOTIFY) {
    switch (wParam) {
      case IMN_OPENCANDIDATE: {
        ImeState state;
        if (FetchImeState(hwnd, state)) ShowOverlayFor(hwnd, state);
        break;
      }
      case IMN_CHANGECANDIDATE: {
        ImeState state;
        if (FetchImeState(hwnd, state)) UpdateOverlay(state);
        break;
      }
      case IMN_CLOSECANDIDATE:
        HideOverlay();
        break;
      default:
        break;
    }
  }
  if (msg == WM_IME_COMPOSITION && ((UINT)lParam & GCS_COMPSTR_FLAG) != 0 && IsOverlayVisible()) {
    ImeState state;
    if (FetchImeState(hwnd, state)) UpdateOverlay(state);
  }
  if (msg == WM_KILLFOCUS || msg == WM_DESTROY) {
    HideOverlay();
  }
  if (msg == WM_IME_NOTIFY && wParam == IMN_OPENCANDIDATE) {
    RescueImm32CandidateWindow(hwnd);
  }

  WNDPROC orig = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_subclassLock);
    auto it = g_subclassed.find(hwnd);
    if (it != g_subclassed.end()) orig = it->second;
  }

  LRESULT result;
  if (blockOriginalForImeNotify) {
    result = 0;
  } else if (orig) {
    result = CallWindowProcW(orig, hwnd, msg, wParam, lParam);
  } else {
    result = 0;
  }

  if (ShouldRefreshEdit(msg)) {
    InvalidateRect(hwnd, NULL, FALSE);
  }

  return result;
}

void SubclassWindow(HWND hwnd) {
  {
    std::lock_guard<std::mutex> lock(g_subclassLock);
    if (g_subclassed.find(hwnd) != g_subclassed.end()) return;
  }

  WNDPROC orig = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
  if (!orig) {
    IME_LOG("[ime] subclass_window FAIL hwnd=0x%p (GetWindowLongPtrW=0)", hwnd);
    return;
  }
  SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)SubclassWndProc);
  {
    std::lock_guard<std::mutex> lock(g_subclassLock);
    g_subclassed[hwnd] = orig;
  }
  std::wstring desc = DescribeWindow(hwnd);
  IME_LOG("[ime] subclassed LUnicodeEdit hwnd=0x%p orig_wndproc=0x%p %ls", hwnd, orig,
          desc.c_str());
}

LRESULT CALLBACK GameWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_TSF_INIT_ON_UI_THREAD) {
    DWORD tid = GetCurrentThreadId();
    HRESULT hr = TsfInit();
    if (SUCCEEDED(hr)) {
      IME_LOG("[tsf] init on UI thread OK (tid=%lu)", (unsigned long)tid);
    } else {
      IME_LOG("[tsf] init on UI thread FAIL: hr=0x%08X", (unsigned)hr);
    }
    return 0;
  }

  if (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORSTATIC) {
    HWND child = (HWND)lParam;
    if (child && ClassIsTarget(child)) {
      HDC hdc = (HDC)wParam;
      SetBkColor(hdc, EDIT_BG_COLOR);
      SetTextColor(hdc, EDIT_TEXT_COLOR);
      return (LRESULT)EditBgBrush();
    }
  }

  if (g_gameWndProc) {
    return CallWindowProcW(g_gameWndProc, hwnd, msg, wParam, lParam);
  }
  return 0;
}

HWINEVENTHOOK g_winEventHook = NULL;

void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject,
                           LONG idChild, DWORD idEventThread, DWORD dwmsEventTime) {
  bool interesting = (event == EVENT_OBJECT_CREATE || event == EVENT_OBJECT_FOCUS);
  if (!interesting || idObject != 0 /* OBJID_WINDOW */ || !hwnd) return;
  if (ClassIsTarget(hwnd)) SubclassWindow(hwnd);
}

} // namespace

bool IsSubclassedLunicodeEdit(HWND hwnd) {
  std::lock_guard<std::mutex> lock(g_subclassLock);
  return g_subclassed.find(hwnd) != g_subclassed.end();
}

void SubclassGameWindow(HWND hwnd) {
  if (g_gameWndProc) return;
  WNDPROC orig = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
  if (!orig) {
    IME_LOG("[ime] subclass_game_window FAIL hwnd=0x%p (GetWindowLongPtrW=0)", hwnd);
    return;
  }
  SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)GameWndProc);
  g_gameWndProc = orig;
  std::wstring desc = DescribeWindow(hwnd);
  IME_LOG("[ime] subclassed game hwnd=0x%p orig_wndproc=0x%p %ls", hwnd, orig, desc.c_str());
}

void InstallCreateWatcher() {
  // 一次掛 OBJECT_CREATE..OBJECT_FOCUS 整個 range（0x8000~0x8005），涵蓋
  // CREATE/DESTROY/SHOW/HIDE/REORDER/FOCUS。我們真正在意 CREATE（新建
  // LUnicodeEdit）跟 FOCUS（玩家點聊天框→既有 LUnicodeEdit 拿到 focus，這是
  // 主要抓到 init 階段就建好的輸入框的手段）。
  //
  // 不能用 WINEVENT_SKIPOWNPROCESS —— 我們注入到目標行程，「自己的行程」就是
  // 遊戲行程，skip own 等於全部 skip。
  g_winEventHook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_FOCUS, NULL, WinEventProc,
                                   GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
}
