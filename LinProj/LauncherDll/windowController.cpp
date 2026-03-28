// windowController.cpp: 視窗控制與樣式管理。
#include "stdafx.h"
#include "windowController.h"

// 原始視窗程序指標
WNDPROC g_OldWndProc = NULL;

LRESULT CALLBACK MyWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCHITTEST) {
        // 直接將用戶區點擊 (HTCLIENT) 修改為標題列 (HTCAPTION) 使視窗全時可拖拉
        LRESULT hit = CallWindowProc(g_OldWndProc, hWnd, message, wParam, lParam);
        if (hit == HTCLIENT) return HTCAPTION;
        return hit;
    }
    // 其餘訊息與正常點擊，全部交還給原始視窗程序處理
    return CallWindowProc(g_OldWndProc, hWnd, message, wParam, lParam);
}

void UnlockGameWindow(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) return;

    // 使用 Subclassing 子類化技術，不更動 WS_CAPTION 樣式以避免 DirectDraw 閃退
    if (g_OldWndProc == NULL) {
        g_OldWndProc = (WNDPROC)SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)MyWndProc);
    }
}
