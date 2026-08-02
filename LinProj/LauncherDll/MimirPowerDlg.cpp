// MimirPowerDlg.cpp: 祕米爾之泉能力選擇視窗實作。
// 比照 HelperDlg.cpp 的做法：獨立頂層 CreateDialog，疊在遊戲畫面最上層，不用處理
// dgVoodoo/DirectDraw 疊層問題。按鈕文字為預設顯示；美術圖準備好後，把對應檔名的
// .bmp 放到遊戲主程式同一層資料夾，會自動改用圖片按鈕（見 ApplyBitmapButtonIfExists）。
#include "stdafx.h"
#include "MimirPowerDlg.h"
#include "resource.h"

static HWND g_hMimirDlg = NULL;
static MimirOption g_mimirOptions[3];
static DWORD g_mimirObjId = 0;

// 若遊戲主程式同一層資料夾有對應檔名的 bmp，改用圖片按鈕；沒有就維持預設文字按鈕，
// 不當作錯誤處理（美術圖本來就可能還沒準備好）。
static void ApplyBitmapButtonIfExists(HWND hWnd, int buttonId,
                                      const wchar_t *bitmapFileName) {
  wchar_t exeDir[MAX_PATH] = {0};
  if (GetModuleFileNameW(NULL, exeDir, MAX_PATH) <= 0)
    return;
  for (int i = (int)wcslen(exeDir) - 1; i >= 0; i--) {
    if (exeDir[i] == L'\\' || exeDir[i] == L'/') {
      exeDir[i] = L'\0';
      break;
    }
  }
  wchar_t path[MAX_PATH];
  swprintf_s(path, L"%s\\%s", exeDir, bitmapFileName);
  if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
    return;

  HBITMAP hBmp = (HBITMAP)LoadImageW(NULL, path, IMAGE_BITMAP, 0, 0,
                                     LR_LOADFROMFILE);
  if (!hBmp)
    return;
  HWND hBtn = GetDlgItem(hWnd, buttonId);
  if (!hBtn) {
    DeleteObject(hBmp);
    return;
  }
  LONG_PTR style = GetWindowLongPtr(hBtn, GWL_STYLE);
  SetWindowLongPtr(hBtn, GWL_STYLE, style | BS_BITMAP);
  SendMessage(hBtn, BM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hBmp);
}

static void SendMimirChoice(BYTE index) {
  BYTE payload[5];
  memcpy(payload, &g_mimirObjId, 4);
  payload[4] = index;
  // C_OPCODE_MIMIRPOWER = 195 (0xC3)，須跟 Java 端 OpcodesClient 一致。
  SendCustomPacket(0xC3, payload, sizeof(payload));
}

static INT_PTR CALLBACK MimirDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam,
                                     LPARAM lParam) {
  switch (uMsg) {
  case WM_INITDIALOG: {
    static const int buttonIds[3] = {IDC_MIMIR_BTN1, IDC_MIMIR_BTN2,
                                     IDC_MIMIR_BTN3};
    static const wchar_t *bitmapNames[3] = {L"mimir_btn1.bmp", L"mimir_btn2.bmp",
                                            L"mimir_btn3.bmp"};
    for (int i = 0; i < 3; i++) {
      // name 是 Java 端已經組好的完整顯示文字（例如「短距離傷害+5,魔法命中+2」），
      // 不用再另外拼數值。
      SetDlgItemTextA(hWnd, buttonIds[i], g_mimirOptions[i].name);
      ApplyBitmapButtonIfExists(hWnd, buttonIds[i], bitmapNames[i]);
    }
    return TRUE;
  }
  case WM_COMMAND: {
    switch (LOWORD(wParam)) {
    case IDC_MIMIR_BTN1:
      SendMimirChoice(0);
      DestroyWindow(hWnd);
      return TRUE;
    case IDC_MIMIR_BTN2:
      SendMimirChoice(1);
      DestroyWindow(hWnd);
      return TRUE;
    case IDC_MIMIR_BTN3:
      SendMimirChoice(2);
      DestroyWindow(hWnd);
      return TRUE;
    }
    break;
  }
  case WM_SYSCOMMAND: {
    if (wParam == SC_CLOSE) {
      DestroyWindow(hWnd);
      return TRUE;
    }
    break;
  }
  case WM_DESTROY:
    g_hMimirDlg = NULL;
    break;
  }
  return FALSE;
}

void ShowMimirPowerDialogFromCache() {
  if (g_mimirObjId == 0) {
    return; // 從沒收過伺服器資料，沒東西可以顯示
  }
  ShowMimirPowerDialog(g_mimirObjId, g_mimirOptions);
}

void ShowMimirPowerDialog(DWORD objid, const MimirOption options[3]) {
  g_mimirObjId = objid;
  memcpy(g_mimirOptions, options, sizeof(g_mimirOptions));

  if (IsWindow(g_hMimirDlg)) {
    DestroyWindow(g_hMimirDlg);
    g_hMimirDlg = NULL;
  }
  g_hMimirDlg =
      CreateDialog(hins, MAKEINTRESOURCE(IDD_DIALOG_MIMIR), NULL, MimirDlgProc);
  if (g_hMimirDlg) {
    SetWindowPos(g_hMimirDlg, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE);
    ShowWindow(g_hMimirDlg, SW_SHOW);
  }
}
