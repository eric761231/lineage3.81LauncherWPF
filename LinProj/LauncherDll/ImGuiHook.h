// ImGuiHook.h: hooks the game's real IDirect3DDevice9 (EndScene/Reset) so a
// Dear ImGui overlay actually renders into the game's own D3D9 frame,
// instead of a separate layered GDI+ popup window like DisconnectOverlay /
// MimirPowerOverlay do.
#pragma once
#include <windows.h>

// Installs the D3D9 hook and lazily initializes ImGui on the first EndScene
// call. Call from a background thread (see DelayedDetourThread in
// LauncherDll.cpp).
void InstallImGuiHook();

// Meant to be called from the game window's WndProc: if ImGui wants to
// consume this input message (mouse over/clicking the ImGui window,
// keyboard input, etc.), returns true and fills outResult - the
// caller should return outResult immediately instead of forwarding to the
// game's original WndProc. Returns false if the message should be forwarded
// as usual.
bool ImGuiHook_HandleWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                              LRESULT *outResult);
