// dllmain.cpp: 對照 Rust 版 ime_overlay/src/lib.rs。
//
// 流程：
//   1. launcher 注入此 DLL → DllMain spawn worker thread（避免 loader lock）
//   2. worker thread：
//      - 裝 CreateWindowEx hook（試 ImmDisableTextFrameService）
//      - 等遊戲主視窗（class "Lineage"）
//      - 裝 SetWinEventHook 監看 LUnicodeEdit 新建/focus
//      - 建自繪 overlay window（隱藏狀態）
//      - subclass 遊戲主視窗 + PostMessage 觸發 TSF init（要在遊戲 UI thread 上）
//      - GetMessageW 訊息迴圈（派發 WinEvent callback + TSF STA COM 訊息）
#include "Common.h"
#include "Dbg.h"
#include "CreateHook.h"
#include "Overlay.h"
#include "Subclass.h"
#include "ImGuiHook.h"

namespace {

DWORD WINAPI Worker(LPVOID) {
  IME_LOG("[ime] worker thread started, pid=%lu", (unsigned long)GetCurrentProcessId());

  // (1) 裝 CreateWindowEx hook —— 確保遊戲 UI thread 第一次建窗時嘗試
  //     ImmDisableTextFrameService。
  std::string hookErr = InstallCreateWindowHook();
  if (!hookErr.empty()) {
    IME_LOG("[ime] FAIL: hook install: %s", hookErr.c_str());
    return 0;
  }
  IME_LOG("[ime] hooks installed (CreateWindowEx)");

  // (2) 等遊戲主視窗出現
  HWND hwnd = WaitForGameWindow(180000);
  if (!hwnd) {
    IME_LOG("[ime] FAIL: game window not found in 180s");
    return 0;
  }
  IME_LOG("[ime] game hwnd = 0x%p", hwnd);

  // (3) 延遲 5 秒讓遊戲 init 走完 —— 太早 subclass + SetWinEventHook 可能干擾
  //     視窗化 init（對齊 Rust 版的保留態度：不對既有 LUnicodeEdit 做
  //     subclass_all_existing，只靠後續 watcher 抓）。
  Sleep(5000);
  IME_LOG("[ime] post-init delay done - installing OBJECT_CREATE watcher");

  // (4) SetWinEventHook 抓未來新建/取得焦點的 LUnicodeEdit。callback 在這條
  //     thread 跑（WINEVENT_OUTOFCONTEXT）。
  InstallCreateWatcher();
  IME_LOG("[ime] WinEventHook installed");

  // (4.5) 建自繪 overlay window —— 這條 thread 跑 GetMessageW loop 順便
  //       dispatch overlay 的 WM_PAINT。
  HWND overlayHwnd = CreateOverlayWindow();
  if (overlayHwnd) {
    SetOverlayHwnd(overlayHwnd);
    IME_LOG("[ime] overlay window ready");
  } else {
    IME_LOG("[ime] FAIL overlay create");
  }

  // (4.6) subclass 遊戲主視窗 + 觸發 TSF init 在遊戲 UI thread 上。
  // TSF 是 per-thread：sink 必須註冊在實際發生打字/候選事件的那條 thread，
  // 我們 worker 註冊只會收到 worker thread 的事件（我們不打字所以永遠 0 次）。
  // 解法：subclass 遊戲主視窗加 WM_TSF_INIT_ON_UI_THREAD handler，從 worker
  // PostMessage 觸發，handler 在遊戲 UI thread 跑 → 那條 thread 的
  // ITfThreadMgr 才會 advise 我們的 sink。
  SubclassGameWindow(hwnd);
  PostMessageW(hwnd, WM_TSF_INIT_ON_UI_THREAD, 0, 0);
  IME_LOG("[ime] posted WM_TSF_INIT to game UI thread");

  // Dear ImGui 測試疊層：先停用。ImGuiHook.cpp 用「暫時 dummy device 讀
  // vtable」的手法在 dgvoodoo2 底下實測沒有真的攔到遊戲的渲染呼叫（log
  // 顯示 hook 安裝回報成功，但 Hooked_EndScene 從沒被叫過），而啟動時多
  // 建立一個 CreateDevice 仍是有風險的動作（候選字視窗這次測試也回報不會
  // 出現，兩者順序上是巧合還是真的有關聯還沒排除）。之後要改用 hook
  // Direct3DCreate9/IDirect3D9::CreateDevice 拿到遊戲真正的 device，而不是
  // 另外自己造一個，才有機會真的行得通。
  // InstallImGuiHook(hwnd);

  // (5) thread message loop —— SetWinEventHook OUTOFCONTEXT callback 派發，
  //     同時 pump TSF ThreadMgr 需要的 STA COM 訊息（tsf_sink 未在這條 thread
  //     init，但保留迴圈以防未來需要）。
  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(hModule);
    // 避開 loader lock —— 把所有 init 工作丟到 worker thread。
    HANDLE h = CreateThread(NULL, 0, Worker, NULL, 0, NULL);
    if (h) CloseHandle(h);
  }
  return TRUE;
}
