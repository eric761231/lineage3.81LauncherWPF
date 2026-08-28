// ImGuiHook.h: 對遊戲實際使用的 IDirect3DDevice9 掛 EndScene/Reset hook，
// 讓 Dear ImGui 疊層真正畫在遊戲畫面上（跟 Overlay.cpp 那種另開 layered
// 視窗用 GDI+ 疊上去的候選字做法是兩條獨立路徑）。
#pragma once
#include <windows.h>

// 安裝 D3D9 hook 並延遲初始化 ImGui（第一次 EndScene 呼叫時才真正建立
// context/backend）。gameHwnd 是 dllmain.cpp 的 WaitForGameWindow() 已經
// 拿到的遊戲主視窗。
void InstallImGuiHook(HWND gameHwnd);

// 提供給既有 WndProc（Subclass.cpp 的 GameWndProc）呼叫：若 ImGui 想要吃掉
// 這個輸入訊息，回傳 true 並填好 outResult，呼叫端應直接 return
// outResult，不要再轉給遊戲原本的 WndProc；回傳 false 代表訊息應照常轉發。
bool ImGuiHook_HandleWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                              LRESULT *outResult);
