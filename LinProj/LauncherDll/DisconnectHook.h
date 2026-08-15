// DisconnectHook.h: ProcessPacket high-opcode + recv peek + MessageBox swallow.
#pragma once
#include <windows.h>

void InstallDisconnectHooks();

void DisconnectHook_OnClientLoginSent();
bool DisconnectHook_IsPostLogin();
void DisconnectHook_ResetSession();

void DisconnectHook_InspectRecvPlain(const unsigned char *data, int len);
void DisconnectHook_InitRecvCipher(int seed);
void DisconnectHook_DecodeRandenc(unsigned char *dst, const unsigned char *src,
                                  int len);

// recv() returned <= 0 (connection closed/error). Diagnostic-only for now:
// logs so we can see whether a real disconnect ever actually surfaces this
// way, without yet triggering the overlay (still needs a way to tell a real
// disconnect apart from the user closing the game normally).
void DisconnectHook_OnRecvClosed();

extern int(WINAPI *real_MessageBoxA)(HWND, LPCSTR, LPCSTR, UINT);
extern int(WINAPI *real_MessageBoxW)(HWND, LPCWSTR, LPCWSTR, UINT);
int WINAPI my_MessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType);
int WINAPI my_MessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType);

extern int(WINAPI *real_closesocket)(SOCKET s);
int WINAPI my_closesocket(SOCKET s);
