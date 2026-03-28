// LauncherDll.cpp: 核心 DLL 實作，包含遊戲鉤子、IME 修復與視窗管理。
// LauncherDll.cpp — 與 0318/LinProj/LauncherDll/LauncherDll.cpp 整支對齊（含 IME/DPI/MM 等區塊）。
// 若需還原舊版，請自版本控管或 0318 備份取回。

#include "stdafx.h"
#include "LauncherDll.h"
#include <imm.h>
#include "detours.h"
#include "windowController.h"
#include "HelperDlg.h"

#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")

#ifndef DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED
#define DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED ((HANDLE)-5)
#endif
#ifndef DPI_AWARENESS_CONTEXT_SYSTEM_AWARE
#define DPI_AWARENESS_CONTEXT_SYSTEM_AWARE ((HANDLE)-2)
#endif

HHOOK hhk;
HINSTANCE hins;
SHARE_INFO ShareInfo;
BYTE* buffer = NULL;
DWORD buffer_len = 0;
char szTitle[32];
HWND g_hGameWnd = NULL;
<<<<<<< HEAD
HHOOK h_hook = NULL;
=======
HANDLE g_hWindowGuardThread = NULL;
HANDLE g_hImeGuardThread = NULL;
bool g_dpiFixed = false;
static const bool kEnableWin10UiFixes = false;
static const bool kEnableImeCandidateFix = true;
static const bool kEnableImeWndProcHook = false;

// 前置宣告：供 TSF/IME 早期輔助函式使用。
static void launcherdll_net_log(const char* fmt, ...);

typedef BOOL (WINAPI* PFN_ImmDisableTextFrameService)(DWORD);

static BOOL CALLBACK EnumAttachImeContextProc(HWND hWnd, LPARAM lParam)
{
	HIMC hImc = (HIMC)lParam;
	if(hImc == NULL || hWnd == NULL || !IsWindow(hWnd))
		return TRUE;

	ImmAssociateContextEx(hWnd, hImc, IACE_DEFAULT);
	ImmAssociateContext(hWnd, hImc);
	return TRUE;
}

static void DisableTsfForThread(HWND hWnd, const char* reason)
{
	if(!kEnableImeCandidateFix)
		return;

	DWORD tid = GetCurrentThreadId();
	if(hWnd != NULL && IsWindow(hWnd))
	{
		DWORD wndTid = GetWindowThreadProcessId(hWnd, NULL);
		if(wndTid != 0)
			tid = wndTid;
	}

	HMODULE imm32 = GetModuleHandleA("imm32.dll");
	if(imm32 == NULL)
		imm32 = LoadLibraryA("imm32.dll");
	if(imm32 == NULL)
		return;

	PFN_ImmDisableTextFrameService pImmDisableTextFrameService =
		(PFN_ImmDisableTextFrameService)GetProcAddress(imm32, "ImmDisableTextFrameService");
	if(pImmDisableTextFrameService == NULL)
		return;

	BOOL ok = pImmDisableTextFrameService(tid);
	launcherdll_net_log("ime: disable-tsf reason=%s tid=%u ok=%d", reason, (unsigned int)tid, (int)ok);
}
static const bool kEnableWindowStyleGuard = false;
WNDPROC g_realGameWndProc = NULL;
DWORD g_lastImeLogTick = 0;
DWORD g_lastImeForceLogTick = 0;
HIMC g_hImeContext = NULL;
DWORD g_lastImeUiLogTick = 0;
HWND g_hStickyImeTarget = NULL;
DWORD g_stickyImeTargetTick = 0;

static const char* ImeMsgName(UINT msg)
{
	switch(msg)
	{
	case WM_SETFOCUS: return "WM_SETFOCUS";
	case WM_IME_SETCONTEXT: return "WM_IME_SETCONTEXT";
	case WM_IME_STARTCOMPOSITION: return "WM_IME_STARTCOMPOSITION";
	case WM_IME_COMPOSITION: return "WM_IME_COMPOSITION";
	case WM_IME_NOTIFY: return "WM_IME_NOTIFY";
	case WM_INPUTLANGCHANGE: return "WM_INPUTLANGCHANGE";
	default: return "WM_UNKNOWN";
	}
}

static bool IsSameOrChildWindow(HWND hParent, HWND hWnd)
{
	if(hParent == NULL || hWnd == NULL)
		return false;

	HWND cur = hWnd;
	while(cur != NULL)
	{
		if(cur == hParent)
			return true;
		cur = GetParent(cur);
	}

	return false;
}

static bool IsLikelyImeEditWindow(HWND hWnd)
{
	if(hWnd == NULL || !IsWindow(hWnd))
		return false;

	char cls[128] = {0};
	GetClassNameA(hWnd, cls, (int)sizeof(cls));
	if(cls[0] == '\0')
		return false;

	if(_stricmp(cls, "LUnicodeEdit") == 0)
		return true;
	if(_stricmp(cls, "Edit") == 0)
		return true;

	return false;
}

static HWND ResolveImeTargetWindow(HWND hGameWnd)
{
	if(hGameWnd == NULL || !IsWindow(hGameWnd))
		return NULL;

	DWORD now = GetTickCount();
	if(g_hStickyImeTarget != NULL && IsWindow(g_hStickyImeTarget) && IsSameOrChildWindow(hGameWnd, g_hStickyImeTarget))
	{
		if(now - g_stickyImeTargetTick <= 15000)
			return g_hStickyImeTarget;
	}

	DWORD tid = GetWindowThreadProcessId(hGameWnd, NULL);
	if(tid == 0)
		return hGameWnd;

	GUITHREADINFO gti = {};
	gti.cbSize = sizeof(gti);
	if(GetGUIThreadInfo(tid, &gti))
	{
		if(gti.hwndFocus != NULL && IsWindow(gti.hwndFocus))
		{
			if(IsLikelyImeEditWindow(gti.hwndFocus))
			{
				g_hStickyImeTarget = gti.hwndFocus;
				g_stickyImeTargetTick = now;
				return gti.hwndFocus;
			}
		}

		if(gti.hwndCaret != NULL && IsWindow(gti.hwndCaret))
		{
			if(IsLikelyImeEditWindow(gti.hwndCaret))
			{
				g_hStickyImeTarget = gti.hwndCaret;
				g_stickyImeTargetTick = now;
				return gti.hwndCaret;
			}
		}

		if(g_hStickyImeTarget != NULL && IsWindow(g_hStickyImeTarget) && IsSameOrChildWindow(hGameWnd, g_hStickyImeTarget))
		{
			if(now - g_stickyImeTargetTick <= 15000)
				return g_hStickyImeTarget;
		}

		if(gti.hwndFocus != NULL && IsWindow(gti.hwndFocus))
			return gti.hwndFocus;
		if(gti.hwndCaret != NULL && IsWindow(gti.hwndCaret))
			return gti.hwndCaret;
	}

	if(g_hStickyImeTarget != NULL && IsWindow(g_hStickyImeTarget) && IsSameOrChildWindow(hGameWnd, g_hStickyImeTarget))
		return g_hStickyImeTarget;

	return hGameWnd;
}

static bool TryGetImeCaretScreenPoint(HWND hWnd, POINT* outPt)
{
	if(hWnd == NULL || !IsWindow(hWnd) || outPt == NULL)
		return false;

	DWORD tid = GetWindowThreadProcessId(hWnd, NULL);
	if(tid == 0)
		return false;

	GUITHREADINFO gti = {};
	gti.cbSize = sizeof(gti);
	if(!GetGUIThreadInfo(tid, &gti))
		return false;

	HWND hCaretOwner = gti.hwndCaret;
	if(hCaretOwner == NULL || !IsWindow(hCaretOwner))
		hCaretOwner = gti.hwndFocus;
	if(hCaretOwner == NULL || !IsWindow(hCaretOwner))
		return false;

	if(!IsSameOrChildWindow(hWnd, hCaretOwner) && !IsSameOrChildWindow(g_hGameWnd, hCaretOwner))
		return false;

	if(gti.rcCaret.left == gti.rcCaret.right && gti.rcCaret.top == gti.rcCaret.bottom)
		return false;

	POINT pt = {gti.rcCaret.left, gti.rcCaret.bottom + 2};
	if(!ClientToScreen(hCaretOwner, &pt))
		return false;

	*outPt = pt;
	return true;
}

static bool EnsureImeContext(HWND hWnd)
{
	if(!kEnableImeCandidateFix)
		return false;

	if(hWnd == NULL || !IsWindow(hWnd))
		return false;

	HIMC hImc = ImmGetContext(hWnd);
	if(hImc != NULL)
	{
		ImmReleaseContext(hWnd, hImc);
		return true;
	}

	if(g_hImeContext == NULL)
	{
		g_hImeContext = ImmCreateContext();
		launcherdll_net_log("ime: create context result=0x%p", g_hImeContext);
	}

	if(g_hImeContext == NULL)
		return false;

	BOOL okExChildren = ImmAssociateContextEx(hWnd, g_hImeContext, IACE_CHILDREN);
	BOOL okEx = ImmAssociateContextEx(hWnd, g_hImeContext, IACE_DEFAULT);
	HIMC old = ImmAssociateContext(hWnd, g_hImeContext);
	EnumChildWindows(hWnd, EnumAttachImeContextProc, (LPARAM)g_hImeContext);
	launcherdll_net_log(
		"ime: associate context hwnd=0x%p okExChildren=%d okEx=%d old=0x%p new=0x%p",
		hWnd,
		(int)okExChildren,
		(int)okEx,
		old,
		g_hImeContext);

	hImc = ImmGetContext(hWnd);
	if(hImc == NULL)
		return false;

	ImmReleaseContext(hWnd, hImc);
	return true;
}

static void ForceEnableIme(HWND hWnd, const char* reason)
{
	if(!kEnableImeCandidateFix)
		return;

	if(hWnd == NULL || !IsWindow(hWnd))
		return;

	if(!EnsureImeContext(hWnd))
	{
		launcherdll_net_log("ime: force-enable skipped, no context reason=%s hwnd=0x%p", reason, hWnd);
		return;
	}

	HIMC hImc = ImmGetContext(hWnd);
	if(hImc == NULL)
	{
		launcherdll_net_log("ime: force-enable failed, ImmGetContext=NULL reason=%s hwnd=0x%p", reason, hWnd);
		return;
	}

	BOOL before = ImmGetOpenStatus(hImc);
	ImmSetOpenStatus(hImc, TRUE);
	BOOL after = ImmGetOpenStatus(hImc);

	// 部分 Win10/Win11 輸入法會在焦點切回時再次關閉，補一層喚醒流程。
	if(after == FALSE)
	{
		ImmSetConversionStatus(hImc, IME_CMODE_NATIVE | IME_CMODE_FULLSHAPE, 0);
		HKL hkl = GetKeyboardLayout(0);
		PostMessage(hWnd, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)hkl);
		ImmSetOpenStatus(hImc, TRUE);
		after = ImmGetOpenStatus(hImc);
	}

	DWORD now = GetTickCount();
	if(before == FALSE || after == FALSE || now - g_lastImeForceLogTick >= 3000)
	{
		g_lastImeForceLogTick = now;
		launcherdll_net_log(
			"ime: force-enable reason=%s hwnd=0x%p open_before=%d open_after=%d",
			reason,
			hWnd,
			(int)before,
			(int)after);
	}

	ImmReleaseContext(hWnd, hImc);
}

static void ForceShowImeUi(HWND hWnd, const char* reason)
{
	if(!kEnableImeCandidateFix)
		return;

	if(hWnd == NULL || !IsWindow(hWnd))
		return;

	LPARAM imeUiFlags =
		ISC_SHOWUICOMPOSITIONWINDOW |
		ISC_SHOWUICANDIDATEWINDOW |
		(ISC_SHOWUICANDIDATEWINDOW << 1) |
		(ISC_SHOWUICANDIDATEWINDOW << 2) |
		(ISC_SHOWUICANDIDATEWINDOW << 3);

	SendMessage(hWnd, WM_IME_SETCONTEXT, TRUE, imeUiFlags);

	DWORD now = GetTickCount();
	if(now - g_lastImeUiLogTick >= 3000)
	{
		g_lastImeUiLogTick = now;
		launcherdll_net_log(
			"ime: force-show-ui reason=%s hwnd=0x%p flags=0x%IX",
			reason,
			hWnd,
			(size_t)imeUiFlags);
	}
}

int _seed = 0;

int _xorByte = 0;

BN_CTX* bn_ctx;
BIGNUM* d;
BIGNUM* n;

typedef BOOL (WINAPI* PFN_SetProcessDpiAwarenessContext)(HANDLE);

// 寫入 AppCompat 旗標，停用 DWM 全螢幕最佳化，避免 DirectDraw 表面蓋住 IME 候選字窗。
static void SetGameCompatibilityFlags()
{
	char exePath[MAX_PATH] = {0};
	if(GetModuleFileNameA(NULL, exePath, MAX_PATH) == 0)
		return;

	const char* keyPath =
		"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";
	HKEY hKey = NULL;
	LONG ret = RegOpenKeyExA(HKEY_CURRENT_USER, keyPath, 0, KEY_SET_VALUE, &hKey);
	if(ret != ERROR_SUCCESS)
		ret = RegCreateKeyExA(HKEY_CURRENT_USER, keyPath, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL);
	if(ret != ERROR_SUCCESS || hKey == NULL)
	{
		launcherdll_net_log("compat: failed to open/create AppCompatFlags key err=%ld", ret);
		return;
	}

	// DISABLEDXMAXIMIZEDWINDOWEDMODE：停用 DWM 視窗化全螢幕模式，讓 IME UI 不被渲染層蓋住。
	const char* flags = "~ DISABLEDXMAXIMIZEDWINDOWEDMODE";
	ret = RegSetValueExA(hKey, exePath, 0, REG_SZ,
		(const BYTE*)flags, (DWORD)(strlen(flags) + 1));
	RegCloseKey(hKey);
	launcherdll_net_log("compat: DISABLEDXMAXIMIZEDWINDOWEDMODE set for %s ret=%ld", exePath, ret);
}

static void FixProcessDpiAwareness()
{
	if(!kEnableWin10UiFixes)
		return;

	if(g_dpiFixed)
		return;
	g_dpiFixed = true;

	// 優先固定為 GDI 縮放非感知模式，降低老遊戲在多螢幕/DPI 混合下的輸入與重繪異常。
	HMODULE user32 = GetModuleHandleA("user32.dll");
	if(user32 != NULL)
	{
		PFN_SetProcessDpiAwarenessContext pSetProcessDpiAwarenessContext =
			(PFN_SetProcessDpiAwarenessContext)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
		if(pSetProcessDpiAwarenessContext != NULL)
		{
			if(pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED))
				return;
			if(pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE))
				return;
		}
	}

	SetProcessDPIAware();
}

static void RefreshImeComposition(HWND hWnd)
{
	if(!kEnableImeCandidateFix)
		return;

	if(hWnd == NULL || !IsWindow(hWnd))
	{
		launcherdll_net_log("ime: refresh skipped, invalid hwnd");
		return;
	}

	HIMC hImc = ImmGetContext(hWnd);
	if(hImc == NULL)
	{
		if(!EnsureImeContext(hWnd))
		{
			launcherdll_net_log("ime: refresh failed, ImmGetContext=NULL hwnd=0x%p", hWnd);
			return;
		}

		hImc = ImmGetContext(hWnd);
		if(hImc == NULL)
		{
			launcherdll_net_log("ime: refresh failed after ensure, hwnd=0x%p", hWnd);
			return;
		}
	}

	POINT pt = {8, 8};
	bool usedCaretPoint = TryGetImeCaretScreenPoint(hWnd, &pt);
	if(!usedCaretPoint)
		ClientToScreen(hWnd, &pt);

	COMPOSITIONFORM cf = {};
	cf.dwStyle = CFS_FORCE_POSITION;
	cf.ptCurrentPos = pt;
	ImmSetCompositionWindow(hImc, &cf);

	CANDIDATEFORM cdf = {};
	cdf.dwStyle = CFS_EXCLUDE;
	cdf.ptCurrentPos = pt;
	cdf.rcArea.left = pt.x;
	cdf.rcArea.top = pt.y;
	cdf.rcArea.right = pt.x + 1;
	cdf.rcArea.bottom = pt.y + 20;
	for(DWORD i = 0; i < 4; i++)
	{
		cdf.dwIndex = i;
		ImmSetCandidateWindow(hImc, &cdf);
	}

	// 只在 IME 視窗尚未設置 WS_EX_TOPMOST 時才設一次，避免重複 SetWindowPos 導致閃爍。
	HWND hImeWnd = ImmGetDefaultIMEWnd(hWnd);
	if(hImeWnd != NULL)
	{
		LONG exStyle = GetWindowLong(hImeWnd, GWL_EXSTYLE);
		if(!(exStyle & WS_EX_TOPMOST))
		{
			SetWindowPos(
				hImeWnd,
				HWND_TOPMOST,
				0,
				0,
				0,
				0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
		}
	}
	else
	{
		launcherdll_net_log("ime: default ime window not found hwnd=0x%p", hWnd);
	}

	ImmReleaseContext(hWnd, hImc);

	DWORD now = GetTickCount();
	if(now - g_lastImeLogTick >= 1000)
	{
		g_lastImeLogTick = now;
		launcherdll_net_log(
			"ime: refreshed hwnd=0x%p imewnd=0x%p comp=(%ld,%ld) source=%s",
			hWnd,
			hImeWnd,
			(long)pt.x,
			(long)pt.y,
			usedCaretPoint ? "caret" : "fallback");
	}
}

// 攔截主視窗 IME/焦點訊息，持續刷新候選字位置與層級。
LRESULT CALLBACK GameWndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch(msg)
	{
	case WM_SETFOCUS:
		ForceEnableIme(hWnd, "WM_SETFOCUS");
		break;
	case WM_IME_SETCONTEXT:
		if(wParam)
		{
			ForceEnableIme(hWnd, "WM_IME_SETCONTEXT_ON");
		}
		break;
	case WM_IME_STARTCOMPOSITION:
		ForceEnableIme(hWnd, "WM_IME_STARTCOMPOSITION");
		break;
	case WM_IME_COMPOSITION:
	case WM_IME_NOTIFY:
	case WM_INPUTLANGCHANGE:
		EnsureImeContext(hWnd);
		launcherdll_net_log(
			"ime: msg=%s(0x%X) wParam=0x%IX lParam=0x%IX hwnd=0x%p",
			ImeMsgName(msg),
			(unsigned int)msg,
			(size_t)wParam,
			(size_t)lParam,
			hWnd);
		RefreshImeComposition(hWnd);
		break;
	default:
		break;
	}

	if(g_realGameWndProc != NULL)
		return CallWindowProc(g_realGameWndProc, hWnd, msg, wParam, lParam);

	return DefWindowProc(hWnd, msg, wParam, lParam);
}

// 記錄 LauncherDll 網路握手關鍵訊息，方便排查加密模式不一致問題。
static void launcherdll_net_log(const char* fmt, ...)
{
	char exePath[MAX_PATH] = {0};
	char logPath[MAX_PATH] = "./launcherdll_net.log";
	if(GetModuleFileNameA(NULL, exePath, MAX_PATH) > 0)
	{
		for(int i = (int)strlen(exePath) - 1; i >= 0; i--)
		{
			if(exePath[i] == '\\' || exePath[i] == '/')
			{
				exePath[i] = '\0';
				break;
			}
		}
		sprintf_s(logPath, "%s\\launcherdll_net.log", exePath);
	}

	FILE* fp = NULL;
	if(fopen_s(&fp, logPath, "a+") != 0 || fp == NULL)
		return;

	SYSTEMTIME st;
	GetLocalTime(&st);

	char msg[2048] = {0};
	va_list args;
	va_start(args, fmt);
	vsprintf_s(msg, fmt, args);
	va_end(args);

	fprintf(
		fp,
		"[%04d-%02d-%02d %02d:%02d:%02d.%03d][PID=%u][TID=%u] %s\n",
		st.wYear, st.wMonth, st.wDay,
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
		(unsigned int)GetCurrentProcessId(),
		(unsigned int)GetCurrentThreadId(),
		msg);

	fflush(fp);
	fclose(fp);
}

// 將 bytes 轉為可讀 hex 字串（僅輸出前 maxBytes）。
static void bytes_to_hex_preview(const BYTE* data, int len, char* out, size_t outSize, int maxBytes)
{
	if(out == NULL || outSize == 0)
		return;
	out[0] = '\0';
	if(data == NULL || len <= 0)
		return;

	if(maxBytes <= 0)
		maxBytes = len;
	int n = (len < maxBytes) ? len : maxBytes;

	size_t pos = 0;
	for(int i = 0; i < n; i++)
	{
		int w = sprintf_s(out + pos, outSize - pos, "%02X%s", data[i], (i == n - 1) ? "" : " ");
		if(w <= 0 || (size_t)w >= outSize - pos)
			break;
		pos += (size_t)w;
	}

	if(len > n && pos + 5 < outSize)
		strcat_s(out, outSize, " ...");
}

// 將 bytes 轉為可見字元預覽，不可見字元以 '.' 取代。
static void bytes_to_ascii_preview(const BYTE* data, int len, char* out, size_t outSize, int maxBytes)
{
	if(out == NULL || outSize == 0)
		return;
	out[0] = '\0';
	if(data == NULL || len <= 0)
		return;

	if(maxBytes <= 0)
		maxBytes = len;
	int n = (len < maxBytes) ? len : maxBytes;
	int i = 0;
	for(i = 0; i < n && i < (int)outSize - 1; i++)
	{
		unsigned char c = data[i];
		out[i] = (c >= 32 && c <= 126) ? (char)c : '.';
	}
	out[i] = '\0';
	if(len > n && i < (int)outSize - 5)
		strcat_s(out, outSize, "...");
}

// 回傳 needle 在 haystack 中第一次出現的位置；找不到回傳 -1。
static int find_subseq(const BYTE* haystack, int hayLen, const BYTE* needle, int needleLen)
{
	if(haystack == NULL || needle == NULL || hayLen <= 0 || needleLen <= 0 || needleLen > hayLen)
		return -1;
	for(int i = 0; i <= hayLen - needleLen; i++)
	{
		bool match = true;
		for(int j = 0; j < needleLen; j++)
		{
			if(haystack[i + j] != needle[j])
			{
				match = false;
				break;
			}
		}
		if(match)
			return i;
	}
	return -1;
}

// 週期性補回可移動視窗樣式，避免遊戲流程中途覆寫成不可拖動狀態。
DWORD WINAPI WindowGuardThread(void* p)
{
	UNREFERENCED_PARAMETER(p);
	HMONITOR lastMonitor = NULL;
	while(true)
	{
		HWND hWnd = g_hGameWnd;
		if(hWnd == NULL || !IsWindow(hWnd))
			break;

		if(kEnableWindowStyleGuard)
		{
			if(!IsWindowEnabled(hWnd))
				EnableWindow(hWnd, TRUE);

			LONG style = GetWindowLong(hWnd, GWL_STYLE);
			LONG wanted = (style & ~WS_POPUP) | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
			if(style != wanted)
			{
				SetWindowLong(hWnd, GWL_STYLE, wanted);
				SetWindowPos(hWnd, NULL, 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
			}
		}

		if(kEnableWin10UiFixes)
		{
			HMONITOR monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
			if(lastMonitor == NULL)
				lastMonitor = monitor;
			else if(monitor != lastMonitor)
			{
				lastMonitor = monitor;

				RECT rc = {};
				GetWindowRect(hWnd, &rc);
				SetWindowPos(
					hWnd,
					NULL,
					rc.left,
					rc.top,
					rc.right - rc.left,
					rc.bottom - rc.top,
					SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

				RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_FRAME);
			}

			if(GetForegroundWindow() == hWnd)
				RefreshImeComposition(hWnd);
		}

		Sleep(300);
	}
	return 0;
}

// 低侵入 IME 保活：只在前景是遊戲主窗時補回 IME 開啟與候選窗位置。
DWORD WINAPI ImeGuardThread(void* p)
{
	UNREFERENCED_PARAMETER(p);
	DWORD lastTargetLogTick = 0;
	while(true)
	{
		HWND hGameWnd = g_hGameWnd;
		if(hGameWnd == NULL || !IsWindow(hGameWnd))
			break;

		if(GetForegroundWindow() == hGameWnd)
		{
			HWND hTarget = ResolveImeTargetWindow(hGameWnd);
			if(hTarget == NULL)
				hTarget = hGameWnd;

			EnsureImeContext(hGameWnd);
			if(hTarget != hGameWnd)
				EnsureImeContext(hTarget);

			ForceEnableIme(hTarget, "ImeGuardThread");
			ForceShowImeUi(hTarget, "ImeGuardThread");
			if(hTarget != hGameWnd)
				ForceShowImeUi(hGameWnd, "ImeGuardThreadGame");

			// 只在有組字字串時刷新位置，避免週期性 SetWindowPos 在無輸入時造成閃爍。
			{
				HIMC hTargetImc = ImmGetContext(hTarget);
				bool hasComposition = false;
				if(hTargetImc != NULL)
				{
					hasComposition = ImmGetCompositionStringW(hTargetImc, GCS_COMPSTR, NULL, 0) > 0;
					ImmReleaseContext(hTarget, hTargetImc);
				}
				if(hasComposition)
					RefreshImeComposition(hTarget);
			}

			DWORD now = GetTickCount();
			if(now - lastTargetLogTick >= 3000)
			{
				lastTargetLogTick = now;
				char cls[128] = {0};
				GetClassNameA(hTarget, cls, (int)sizeof(cls));
				launcherdll_net_log(
					"ime: guard target game=0x%p target=0x%p class=%s",
					hGameWnd,
					hTarget,
					cls[0] ? cls : "<unknown>");
			}
		}

		Sleep(400);
	}

	return 0;
}

void __dbg_print(const char* fmt, ...)
{
	char buffer[8192] = {0};
	va_list args;
	va_start(args, fmt);
	vsprintf_s(buffer, fmt, args);
	va_end(args);
	OutputDebugStringA(buffer);
}

int nextRand()
{
	_seed = (214013 * _seed + 2531011) & 0x7FFFFFFF;
	return (int)(_seed >> 16) & 0xFF;
}
>>>>>>> b1bb3f5 (Refactor UI: Hardcoded layout, Server Status checks, Game Monitoring, and Build fixes)

bool inited = false;

bool __stdcall __fn1(DWORD tid);

// 全域訊息鉤子：按 HOME 可切換輔助視窗。
LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	MSG* pMsg = (MSG*)lParam;
	if(nCode == HC_ACTION)
	{
		if(pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_HOME)
		{
			if(ShareInfo.usehelper)
				ShowOrHideHelperDialog();
		}
	}
	return CallNextHookEx(hhk, nCode, wParam, lParam);
}

int (WINAPI* real_connect)(SOCKET s, const struct sockaddr* name, int namelen) = connect;

// 攔截 connect：把目標位址改為共享設定中的代理端點。
int WINAPI my_connect(SOCKET s, const struct sockaddr* name, int namelen)
{
	if(name == NULL || namelen < (int)sizeof(sockaddr_in))
		return real_connect(s, name, namelen);

	VMProtectBegin
	sockaddr_in mappedAddr = *(const sockaddr_in*)name;
	bool hasMappedHost = false;
	char host[64] = {0};
	strncpy_s(host, ShareInfo.ip, _TRUNCATE);

	// 清理前後空白，避免伺服器列表字串帶空白導致解析失敗（10049）。
	char* begin = host;
	while(*begin == ' ' || *begin == '\t' || *begin == '\r' || *begin == '\n')
		begin++;
	char* end = begin + strlen(begin);
	while(end > begin && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
		*--end = '\0';

	// 若輸入是 host:port 格式，僅取 host；實際 port 以 ShareInfo.port 為準。
	char* colon = strchr(begin, ':');
	if(colon != NULL)
		*colon = '\0';

	if(begin[0] != '\0')
	{
		IN_ADDR parsedAddr = {};
		if(InetPtonA(AF_INET, begin, &parsedAddr) == 1)
		{
			mappedAddr.sin_addr = parsedAddr;
			hasMappedHost = true;
		}
		else
		{
			ADDRINFOA hints = {};
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			ADDRINFOA* result = NULL;
			if(getaddrinfo(begin, NULL, &hints, &result) == 0 && result != NULL)
			{
				mappedAddr.sin_addr = ((sockaddr_in*)result->ai_addr)->sin_addr;
				hasMappedHost = true;
				freeaddrinfo(result);
			}
		}
	}

	if(hasMappedHost)
	{
		mappedAddr.sin_port = htons(ShareInfo.port);
		VMProtectEnd
		inited = false;
		return real_connect(s, (const sockaddr*)&mappedAddr, sizeof(mappedAddr));
	}

	// 映射位址無效時回退原始 connect，避免把位址強制改成 INADDR_NONE 造成 10049。
	VMProtectEnd
	inited = false;
	//__dbg_print("my_connect: invalid mapping host '%s', fallback original target", ShareInfo.ip);
	return real_connect(s, name, namelen);
}

int (WINAPI* real_send)(SOCKET s, const char* buf, int len, int flag) = send;
int (WINAPI* real_recv)(SOCKET s, char* buf, int len, int flag) = recv;

// 伺服器初始化封包 opcode (S_OPCODE_INITPACKET)
// 根據日誌 "96 0B DD 43..." 第一字節 0x96 是 opcode
#define S_OPCODE_INITPACKET 0x96
// 標記是否已從伺服器收到 KEY 並初始化 encdec
static bool g_encdec_inited = false;

extern BYTE g_id[32];
extern int g_pwd_pos;
extern BYTE g_pwd[32];


int my_send(SOCKET s, const char* buf, int len, int flag)
{
	if(buf == NULL || len <= 0)
		return real_send(s, buf, len, flag);

	int ret;
	BYTE stackBuffer[4096];
	BYTE* buffer = stackBuffer;
	bool useHeap = false;
	if(len > (int)sizeof(stackBuffer))
	{
		buffer = new BYTE[len];
		useHeap = true;
	}
	memcpy(buffer, buf, len);
	char plainHex[512] = {0};
	char plainAsc[128] = {0};
	bytes_to_hex_preview(buffer, len, plainHex, sizeof(plainHex), 64);
	bytes_to_ascii_preview(buffer, len, plainAsc, sizeof(plainAsc), 64);
	int pktLenField = (len >= 2) ? ((int)(BYTE)buffer[0] | ((int)(BYTE)buffer[1] << 8)) : -1;
	unsigned int pktOpcode = (len >= 3) ? (unsigned int)(BYTE)buffer[2] : 0xFFFFFFFF;
	launcherdll_net_log(
		"net: send-plain len=%d pktLen=%d op=0x%02X enc=%d rand=%d ascii='%s' hex=%s",
		len,
		pktLenField,
		pktOpcode,
		ShareInfo.encrypt ? 1 : 0,
		ShareInfo.randenc ? 1 : 0,
		plainAsc,
		plainHex);
	// 額外檢查登入封包是否直接包含目前擷取到的帳密 bytes。
	// 若位置為 -1，表示帳密可能在送出前已被另外轉碼/加密/雜湊處理。
	int idLen = 0;
	while(idLen < 31 && g_id[idLen] != 0)
		idLen++;
	int pwdLen = 0;
	while(pwdLen < 31 && g_pwd[pwdLen] != 0)
		pwdLen++;
	if(idLen > 0 || pwdLen > 0)
	{
		int idPos = (idLen > 0) ? find_subseq(buffer, len, g_id, idLen) : -1;
		int pwdPos = (pwdLen > 0) ? find_subseq(buffer, len, g_pwd, pwdLen) : -1;
		launcherdll_net_log(
			"cred: packet-scan op=0x%02X idLen=%d idPos=%d pwdLen=%d pwdPos=%d pwdCaptureLen=%d",
			pktOpcode,
			idLen,
			idPos,
			pwdLen,
			pwdPos,
			g_pwd_pos);
	}
	// 送出前依模式做 XOR：固定位元組或偽隨機序列（需先完成 RSA 握手）。
	if(ShareInfo.encrypt && inited)
	{
		if(ShareInfo.randenc)
		{
			for(int i = 0; i < len; i++)
				buffer[i] ^= (unsigned char)nextRand();
			launcherdll_net_log("rsa: randenc applied seed=%u len=%d", (unsigned int)_seed, len);
		}
		else
		{
			for(int i = 0; i < len; i++)
				buffer[i] ^= (unsigned char)_xorByte;
			launcherdll_net_log("rsa: fixedxor applied xorByte=%u len=%d", (unsigned int)_xorByte, len);
		}
	}
	char cipherHex[512] = {0};
	char cipherAsc[128] = {0};
	bytes_to_hex_preview(buffer, len, cipherHex, sizeof(cipherHex), 64);
	bytes_to_ascii_preview(buffer, len, cipherAsc, sizeof(cipherAsc), 64);
	int pktLenField2 = (len >= 2) ? ((int)(BYTE)buffer[0] | ((int)(BYTE)buffer[1] << 8)) : -1;
	unsigned int pktOpcode2 = (len >= 3) ? (unsigned int)(BYTE)buffer[2] : 0xFFFFFFFF;
	launcherdll_net_log(
		"net: send-cipher len=%d pktLen=%d op=0x%02X enc=%d inited=%d mode=%s key=%u ascii='%s' hex=%s",
		len,
		pktLenField2,
		pktOpcode2,
		ShareInfo.encrypt ? 1 : 0,
		inited ? 1 : 0,
		ShareInfo.randenc ? "randenc" : "fixedxor",
		ShareInfo.randenc ? (unsigned int)_seed : (unsigned int)_xorByte,
		cipherAsc,
		cipherHex);
	ret = real_send(s, (const char*)buffer, len, flag);
	if(useHeap)
		delete[] buffer;

	//__dbg_print("real_send len: %d, ret: %d", len, ret);

	return ret;
}

int my_recv(SOCKET s, char* buf, int len, int flag)
{
	int ret;
	char buffer[32];
	// 首包握手流程：
	// - 加密模式下，先強制讀 4 bytes authdata
	// - 透過 RSA(d, n) 還原出本次會話參數
	// - randenc=true 時還原 seed（每 byte 變動）
	// - randenc=false 時還原固定 xor byte（每 byte 相同）
	// 完成後才進入一般 real_recv 路徑。
	if(ShareInfo.encrypt && !inited)
	{
		memset(buffer, 0, sizeof(buffer));
		int len = 0;
		while(len < 4)
		{
			ret = real_recv(s, &buffer[len], 4 - len, 0);
			if(ret > 0)
				len += ret;
			else
			{
				if(WSAGetLastError() == WSAEWOULDBLOCK)
				{
					//__dbg_print("=======WSAEWOULDBLOCK=======");
					continue;
				}
				else
				{
					//__dbg_print("ret: %d, err: %d", ret, WSAGetLastError());
					return ret;
				}
			}
		}
		
		unsigned long _authdata = *(unsigned long*)buffer;
		launcherdll_net_log("handshake: recv authdata=%lu", _authdata);

		BIGNUM* c = BN_new();
		BIGNUM* m = BN_new();
		BN_set_word(c, _authdata);
		
		BN_mod_exp(m, c, d, n, bn_ctx);
		BN_ULONG mword = BN_get_word(m);
		launcherdll_net_log("handshake: decrypted authdata=%lu", (unsigned long)mword);

		// 把握手結果轉成 send/recv 共同使用的加解參數。
		if(ShareInfo.randenc)
		{
			_seed = (int)mword;
			launcherdll_net_log("handshake: mode=randenc, seed=%d", _seed);
		}
		else
		{
			_xorByte = (unsigned char)mword;
			launcherdll_net_log("handshake: mode=fixedxor, xorByte=%u", (unsigned int)_xorByte);
		}

		BN_free(c);
		BN_free(m);

		inited = true;
	}
	ret = real_recv(s, buf, len, flag);
	if(ret > 0)
	{
		char recvHex[512] = {0};
		char recvAsc[128] = {0};
		bytes_to_hex_preview((const BYTE*)buf, ret, recvHex, sizeof(recvHex), 64);
		bytes_to_ascii_preview((const BYTE*)buf, ret, recvAsc, sizeof(recvAsc), 64);
		int pktLenField = (ret >= 2) ? ((int)(BYTE)buf[0] | ((int)(BYTE)buf[1] << 8)) : -1;
		unsigned int pktOpcode = (ret >= 3) ? (unsigned int)(BYTE)buf[2] : 0xFFFFFFFF;
		launcherdll_net_log(
			"net: recv-raw len=%d pktLen=%d op=0x%02X enc=%d inited=%d ascii='%s' hex=%s",
			ret,
			pktLenField,
			pktOpcode,
			ShareInfo.encrypt ? 1 : 0,
			inited ? 1 : 0,
			recvAsc,
			recvHex);

		if(ShareInfo.encrypt && !ShareInfo.randenc && inited)
		{
			BYTE decPreview[128] = {0};
			int n = (ret < (int)sizeof(decPreview)) ? ret : (int)sizeof(decPreview);
			for(int i = 0; i < n; i++)
				decPreview[i] = ((BYTE)buf[i]) ^ (BYTE)_xorByte;
			char decHex[512] = {0};
			char decAsc[128] = {0};
			bytes_to_hex_preview(decPreview, n, decHex, sizeof(decHex), 64);
			bytes_to_ascii_preview(decPreview, n, decAsc, sizeof(decAsc), 64);
			unsigned int decOp = (n >= 3) ? (unsigned int)decPreview[2] : 0xFFFFFFFF;
			launcherdll_net_log(
				"net: recv-decoded-preview len=%d op=0x%02X xor=%u ascii='%s' hex=%s",
				n,
				decOp,
				(unsigned int)_xorByte,
				decAsc,
				decHex);
		}
	}
	//__dbg_print("real_recv len: %d ret: %d, %d", len, ret, ret == SOCKET_ERROR ? WSAGetLastError() : 0);
	return ret;
}

const DWORD FILE_HOOK_ADDR = 0x0058788B;
const DWORD FILE_RETN_ADDR = 0x0058794F;

// 裸函式跳板：改寫遊戲內部資料來源，導向自訂檔案緩衝。
__declspec(naked) void GetFileData(void)
{
	__asm
	{
		mov eax, buffer_len
		mov dword ptr ss:[ebp - 0x14], eax
		mov eax, buffer
		add eax, 1
		mov edx, dword ptr ss:[ebp - 0x23C]
		mov dword ptr ds:[edx + 0x08], eax
		jmp FILE_RETN_ADDR
	}
}

const DWORD USER_HOOK_ADDR = 0x0077317D;
const DWORD USER_RETN_ADDR = 0x00773183;
BYTE g_id[32];
int g_pwd_pos = 0;
BYTE g_pwd[32];

void __stdcall UserNameHandler(void* p)
{
	memcpy(g_id, p, 32);
	g_id[31] = 0; // ensure C-string termination
	char hex[256] = {0};
	char asc[64] = {0};
	bytes_to_hex_preview(g_id, 32, hex, sizeof(hex), 32);
	bytes_to_ascii_preview(g_id, 32, asc, sizeof(asc), 32);
	launcherdll_net_log("cred: username-capture text='%s' ascii='%s' hex=%s", (char*)g_id, asc, hex);
}

__declspec(naked) void GetUsername(void)
{
	__asm
	{
		lea eax, dword ptr ss:[ebp-0x98]
		pushad
		push eax
		call UserNameHandler
		popad
		jmp USER_RETN_ADDR
	}
}

const DWORD PASS_HOOK_ADDR = 0x004AA38E;
const DWORD PASS_RETN_ADDR = 0x004AA395;
const DWORD PASS_CALL_ADDR = 0x00402800;

BYTE GetPassByte(DWORD dwValue)
{
	BYTE result;
	__asm
	{
		mov ecx, dwValue
		mov eax, PASS_CALL_ADDR
		call eax
		mov result, al
	}
	return result;
}

void __stdcall PasswordHandler(BYTE PassByte)
{
	if(g_pwd_pos == 0)
		memset(g_pwd, 0, 32);
	if(g_pwd_pos < 31)
	{
		g_pwd[g_pwd_pos++] = PassByte;
		g_pwd[g_pwd_pos] = 0; // keep zero-terminated
		char hex[256] = {0};
		char asc[64] = {0};
		bytes_to_hex_preview(g_pwd, g_pwd_pos, hex, sizeof(hex), 32);
		bytes_to_ascii_preview(g_pwd, g_pwd_pos, asc, sizeof(asc), 32);
		launcherdll_net_log(
			"cred: password-capture len=%d last=0x%02X ascii='%s' hex=%s",
			g_pwd_pos,
			(unsigned int)PassByte,
			asc,
			hex);
	}
}

__declspec(naked) void GetPassword(void)
{
	__asm
	{
		mov edx, dword ptr ss:[ebp - 0x0C]
		mov ecx, dword ptr ds:[edx + ecx * 4 + 0x3C]
		pushad
		mov eax, 0x00402800
		call eax
		push eax
		call PasswordHandler
		popad
		jmp PASS_RETN_ADDR
	}
}

void __stdcall TestIdPass()
{
	char msg[1024];
	sprintf_s(msg, "%s->%s", (char*)g_id, (char*)g_pwd);
	MessageBoxA(NULL, msg, "IDPWD:", MB_OK);
}

const DWORD SETID_HOOK_ADDR = 0x00772BA3;
const DWORD SETID_RETN_ADDR = 0x00772BAD;
// 把攔截到的帳密資料回填至遊戲原流程。
__declspec(naked) void SetIdPass(void)
{
	__asm
	{
		//pushad
		//call TestIdPass
		//popad
		mov g_pwd_pos, 0
		lea eax, g_pwd
		push eax
		lea eax, g_id
		push eax
		jmp SETID_RETN_ADDR
	}
}

// --- Helper 訊息掛鉤程序 ---
LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode >= 0) {
    MSG *pMsg = (MSG *)lParam;
    if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_HOME) {
      if (ShareInfo.usehelper) {
        ShowOrHideHelperDialog();
      }
    }
  }
  return CallNextHookEx(h_hook, nCode, wParam, lParam);
}

// --- 核心工具函式與舊版加密邏輯移除 (改由 encdec.h 提供) ---
void UpdateEKey(unsigned long mask) {
  // 此版 Lineage 3.81 不使用 UpdateEKey
}

// --- MessageBox Hook ---
int(WINAPI *real_MessageBoxA)(HWND, LPCSTR, LPCSTR, UINT) = MessageBoxA;
int(WINAPI *real_MessageBoxW)(HWND, LPCWSTR, LPCWSTR, UINT) = MessageBoxW;

int WINAPI my_MessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption,
                          UINT uType) {
  launcherdll_net_log("MessageBoxA: [%s] %s", lpCaption ? lpCaption : "NULL",
                      lpText ? lpText : "NULL");
  return real_MessageBoxA(hWnd, lpText, lpCaption, uType);
}

int WINAPI my_MessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption,
                          UINT uType) {
  launcherdll_net_log("MessageBoxW: [%ls] %ls", lpCaption ? lpCaption : L"NULL",
                      lpText ? lpText : L"NULL");
  return real_MessageBoxW(hWnd, lpText, lpCaption, uType);
}

// --- 5. 時間欺騙 Hook (防止 RSA 亂碼與用戶端過期) ---

void(WINAPI *real_GetLocalTime)(LPSYSTEMTIME) = GetLocalTime;
void WINAPI my_GetLocalTime(LPSYSTEMTIME lpSystemTime) {
  real_GetLocalTime(lpSystemTime);
  lpSystemTime->wYear = 2013;
  lpSystemTime->wMonth = 8;
  lpSystemTime->wDay = 1;
}

void(WINAPI *real_GetSystemTime)(LPSYSTEMTIME) = GetSystemTime;
void WINAPI my_GetSystemTime(LPSYSTEMTIME lpSystemTime) {
  real_GetSystemTime(lpSystemTime);
  lpSystemTime->wYear = 2013;
  lpSystemTime->wMonth = 8;
  lpSystemTime->wDay = 1;
}

void(WINAPI *real_GetSystemTimeAsFileTime)(LPFILETIME) = GetSystemTimeAsFileTime;
void WINAPI my_GetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime) {
  // 將原本的時間回歸到 2013-08-01 的 FileTime (避免 time() 故障)
  SYSTEMTIME st = { 2013, 8, 4, 1, 12, 0, 0, 0 };
  SystemTimeToFileTime(&st, lpSystemTimeAsFileTime);
}

DWORD(WINAPI *real_GetTickCount)() = GetTickCount;
DWORD WINAPI my_GetTickCount() {
  // 固定回傳一個合理的開機存活時間 (例如 1 小時 = 3,600,000 毫秒) 避免溢位
  return 3600000;
}

DWORD(WINAPI *real_timeGetTime)() = NULL; // timeGetTime 在 winmm.dll
DWORD WINAPI my_timeGetTime() {
  return 3600000;
}

// --- 帳密攔截裸跳板函式 (從 0318 原樣移植) ---
void __stdcall UserNameHandler(void *p) {
  memcpy(g_id, p, 32);
  g_id[31] = 0;
  launcherdll_net_log("cred: username-capture text='%s'", (char *)g_id);
}

__declspec(naked) void GetUsername(void) {
  __asm {
    lea eax, dword ptr ss:[ebp-0x98]
    pushad
    push eax
    call UserNameHandler
    popad
    jmp USER_RETN_ADDR
  }
}

void __stdcall PasswordHandler(BYTE PassByte) {
  if (g_pwd_pos == 0) memset(g_pwd, 0, 32);
  if (g_pwd_pos < 31) {
    g_pwd[g_pwd_pos++] = PassByte;
    g_pwd[g_pwd_pos] = 0;
    launcherdll_net_log("cred: password-capture len=%d", g_pwd_pos);
  }
}

__declspec(naked) void GetPassword(void) {
  __asm {
    mov edx, dword ptr ss:[ebp - 0x0C]
    mov ecx, dword ptr ds:[edx + ecx * 4 + 0x3C]
    pushad
    mov eax, 0x00402800
    call eax
    push eax
    call PasswordHandler
    popad
    jmp PASS_RETN_ADDR
  }
}

__declspec(naked) void SetIdPass(void) {
  __asm {
    mov g_pwd_pos, 0
    lea eax, g_pwd
    push eax
    lea eax, g_id
    push eax
    jmp SETID_RETN_ADDR
  }
}
const BYTE path_code[] = {
		0x60, 0x6A, 0x00, 0x68, 0xC8, 0xAB, 0x9A, 0x00, 0x68, 0x48, 
		0xAC, 0x9A, 0x00, 0x6A, 0x06, 0x68, 0xD2, 0x00, 0x00, 0x00, 
		0x68, 0x14, 0x15, 0x8D, 0x00, 0xE8, 0x92, 0xE2, 0xE0, 0xFF, 
		0x83, 0xC4, 0x18, 0x61, 0xC3, 0x90, 0x90
	};

bool IsCodeDecrypt()
{
	__try{
		return *(DWORD*)FILE_HOOK_ADDR == 0x85C0B60F;
	}__except(1){
		//
	}
	return false;
}

void PatchCode(void* addr, void* code, int len)
{
	// 直接改寫目標程序記憶體，務必只在已確認位址版本一致時使用。
	DWORD dwOldProtect;
	VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, PAGE_READWRITE, &dwOldProtect);
	memcpy(addr, code, len);
	VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, dwOldProtect, &dwOldProtect);
}

void HookCode(void* addr, void* func, int len)
{
	// 以 E9 相對跳轉覆寫目標指令，導到自訂處理函式。
	if(len < 5)
		return;
	DWORD dwOldProtect;
	BYTE* patch = new BYTE[len];
	memset(patch, 0x90, len);
	patch[0] = 0xE9;
	*(DWORD*)&patch[1] = (DWORD)func - (DWORD)addr - 5;
	VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, PAGE_READWRITE, &dwOldProtect);
	memcpy(addr, patch, len);
	VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, dwOldProtect, &dwOldProtect);
	delete[] patch;
}

// ── Tab 欄位座標持久化與熱鍵教學（WH_GETMESSAGE）；Tab 鍵切換帳號/密碼已暫時移除 ──
struct EditEnumCtx
{
	HWND hwnds[16];
	int  count;
};

static BOOL CALLBACK EnumLUnicodeEditProc(HWND hWnd, LPARAM lParam)
{
	EditEnumCtx* ctx = (EditEnumCtx*)lParam;
	char cls[64] = {0};
	GetClassNameA(hWnd, cls, (int)sizeof(cls));
	if(_stricmp(cls, "LUnicodeEdit") == 0 && ctx->count < 16)
		ctx->hwnds[ctx->count++] = hWnd;
	return TRUE;
}


// =============================================================================
// MatchMaking 功能區塊（全部程式碼，含常數/全域/INI/SEH/熱鍵/監控執行緒）
// =============================================================================

static const DWORD MM_GLOBAL_OBJ_PTR   = 0xC2FDC0;
static const DWORD MM_CHANGELABEL_CORE = 0x64CCD0;
static const bool kEnableMmChangeLabelHook = false;
static const bool kEnableMmRegisterHook    = false;
static int g_mmStateOffset = 0x30;
static float g_fieldRatioX[5] = { -1.f, -1.f, -1.f, -1.f, -1.f };
static float g_fieldRatioY[5] = { -1.f, -1.f, -1.f, -1.f, -1.f };
static float g_mmBtnRatioX[3] = { -1.f, -1.f, -1.f };
static float g_mmBtnRatioY[3] = { -1.f, -1.f, -1.f };
static volatile bool g_autoRegisterEnabled = true;
static HANDLE g_hAutoRegisterThread = NULL;
static HHOOK  g_hTabHook = NULL;
static DWORD g_lastMmHookStatusLogTick = 0;
struct MMClickCtx { HWND hWnd; POINT clientPt; DWORD delayMs; };

static bool SafeReadDWORD(DWORD addr, DWORD* outVal);
static bool SafeWriteDWORD(DWORD addr, DWORD val);
static bool SafeReadBytes(DWORD addr, BYTE* outBuf, int len);
static void SaveFieldPositions();
static void SaveMMStateOffset();
static void GetFieldsCfgPath(char* buf, int bufLen);
static void LogMmHookStatus(const char* reason);
static DWORD WINAPI MMDelayedClickThread(LPVOID pv);
static DWORD WINAPI AutoRegisterMonitorThread(LPVOID pv);

static bool SafeReadDWORD(DWORD addr, DWORD* outVal)
{
	__try { *outVal = *(DWORD*)addr; return true; }
	__except(1) { return false; }
}
static bool SafeWriteDWORD(DWORD addr, DWORD val)
{
	__try { *(DWORD*)addr = val; return true; }
	__except(1) { return false; }
}
static bool SafeReadBytes(DWORD addr, BYTE* outBuf, int len)
{
	if(outBuf == NULL || len <= 0) return false;
	__try { memcpy(outBuf, (const void*)addr, len); return true; }
	__except(1) { return false; }
}

<<<<<<< HEAD
// --- CreateWindowEx Hook ---
HWND(WINAPI *real_CreateWindowEx)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int,
                                  int, HWND, HMENU, HINSTANCE,
                                  LPVOID) = CreateWindowExA;

HWND WINAPI my_CreateWindowEx(DWORD dwExStyle, LPCSTR lpClassName,
                              LPCSTR lpWindowName, DWORD dwStyle, int x, int y,
                              int nWidth, int nHeight, HWND hWndParent,
                              HMENU hMenu, HINSTANCE hInstance,
                              LPVOID lpParam) {
  // 避免 lpClassName 是 ATOM 導致存取違規
  if (lpClassName && HIWORD(lpClassName) != 0) {
    launcherdll_net_log("my_CreateWindowEx intercepted! ClassName: %s",
                        lpClassName);
    if (_stricmp(lpClassName, "Lineage") == 0) {
      if (!g_hooked) {
        g_hooked = true;
        // 安裝帳密攔截補丁：繞過客戶端 RSA 加密，改以明文送出帳密
        PatchCode((void *)0x00772BA0, (void *)path_code, sizeof(path_code));
        HookCode((void *)USER_HOOK_ADDR, GetUsername,
                 USER_RETN_ADDR - USER_HOOK_ADDR);
        HookCode((void *)PASS_HOOK_ADDR, GetPassword,
                 PASS_RETN_ADDR - PASS_HOOK_ADDR);
        HookCode((void *)SETID_HOOK_ADDR, SetIdPass,
                 SETID_RETN_ADDR - SETID_HOOK_ADDR);
        launcherdll_net_log("cred: path_code + credential hooks installed");
      }
      srand(GetTickCount());
      // 隨機英文加數字 (隨機大寫字母 + 隨機數字)
      char randomStr[16];
      for (int i = 0; i < 8; i++) {
        if (i < 4)
          randomStr[i] = 'A' + (rand() % 26);
        else
          randomStr[i] = '0' + (rand() % 10);
      }
      randomStr[8] = '\0';
      sprintf_s(szTitle, "%s", randomStr);
      lpWindowName = szTitle;

      // --- Helper 訊息掛鉤修復 ---
      // 在遊戲主執行緒建立視窗時安裝掛鉤，確保能攔截到 Home 鍵。
      if (!h_hook) {
        h_hook = SetWindowsHookEx(WH_GETMESSAGE, (HOOKPROC)HookProc, hins,
                                  GetCurrentThreadId());
        if (h_hook) {
          launcherdll_net_log("hook: WH_GETMESSAGE (Helper) installed "
                              "successfully on UI thread");
        } else {
          launcherdll_net_log("hook: WH_GETMESSAGE (Helper) FAILED, err=%u",
                              GetLastError());
        }
      }
    }
  }
  HWND hWnd = real_CreateWindowEx(dwExStyle, lpClassName, lpWindowName, dwStyle,
                                  x, y, nWidth, nHeight, hWndParent, hMenu,
                                  hInstance, lpParam);
  // 解鎖主視窗使其可透過 Ctrl 拖動
  if (lpClassName && HIWORD(lpClassName) != 0 &&
      _stricmp(lpClassName, "Lineage") == 0) {
    if (hWnd && IsWindow(hWnd)) {
      UnlockGameWindow(hWnd);
    }
  }
  return hWnd;
}

HWND(WINAPI *real_CreateWindowExW)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int,
                                   int, int, HWND, HMENU, HINSTANCE,
                                   LPVOID) = CreateWindowExW;

HWND WINAPI my_CreateWindowExW(DWORD dwExStyle, LPCWSTR lpClassName,
                               LPCWSTR lpWindowName, DWORD dwStyle, int x,
                               int y, int nWidth, int nHeight, HWND hWndParent,
                               HMENU hMenu, HINSTANCE hInstance,
                               LPVOID lpParam) {
  if (lpClassName && HIWORD(lpClassName) != 0) {
    if (_wcsicmp(lpClassName, L"Lineage") == 0) {
      if (!g_hooked) {
        g_hooked = true;
        // 安裝帳密攔截補丁：繞過客戶端 RSA 加密，改以明文送出帳密
        PatchCode((void *)0x00772BA0, (void *)path_code, sizeof(path_code));
        HookCode((void *)USER_HOOK_ADDR, GetUsername,
                 USER_RETN_ADDR - USER_HOOK_ADDR);
        HookCode((void *)PASS_HOOK_ADDR, GetPassword,
                 PASS_RETN_ADDR - PASS_HOOK_ADDR);
        HookCode((void *)SETID_HOOK_ADDR, SetIdPass,
                 SETID_RETN_ADDR - SETID_HOOK_ADDR);
        launcherdll_net_log("cred: path_code + credential hooks installed (W)");
      }
      srand(GetTickCount());
      static wchar_t szTitleW[32];
      char randomStr[16];
      for (int i = 0; i < 8; i++) {
        if (i < 4)
          randomStr[i] = 'A' + (rand() % 26);
        else
          randomStr[i] = '0' + (rand() % 10);
      }
      randomStr[8] = '\0';
      swprintf_s(szTitleW, 32, L"%hs", randomStr);
      lpWindowName = szTitleW;

      // --- Helper 訊息掛鉤修復 (W) ---
      if (!h_hook) {
        h_hook = SetWindowsHookEx(WH_GETMESSAGE, (HOOKPROC)HookProc, hins,
                                  GetCurrentThreadId());
        if (h_hook) {
          launcherdll_net_log("hook: WH_GETMESSAGE (Helper/W) installed "
                              "successfully on UI thread");
        }
      }
    }
  }
  HWND hWnd = real_CreateWindowExW(dwExStyle, lpClassName, lpWindowName,
                                   dwStyle, x, y, nWidth, nHeight, hWndParent,
                                   hMenu, hInstance, lpParam);
  // 解鎖主視窗使其可透過 Ctrl 拖動
  if (lpClassName && HIWORD(lpClassName) != 0 &&
      _wcsicmp(lpClassName, L"Lineage") == 0) {
    if (hWnd && IsWindow(hWnd)) {
      UnlockGameWindow(hWnd);
    }
  }
  return hWnd;
=======
void __stdcall ChangeLabelLogger(DWORD retAddr, DWORD pObj)
{
	DWORD stateVal = 0;
	bool hasState = false;
	if(g_mmStateOffset >= 0 && (g_mmStateOffset & 0x3) == 0 && g_mmStateOffset <= 0x200 && pObj != 0)
		hasState = SafeReadDWORD(pObj + g_mmStateOffset, &stateVal);
	launcherdll_net_log(
		"mm: ChangeLabel caller=0x%08X this=0x%08X stateOff=0x%X state=%s0x%08X",
		retAddr, pObj, g_mmStateOffset, hasState ? "" : "<err>", stateVal);
}
__declspec(naked) static void ChangeLabelHook(void)
{
	__asm
	{
		mov eax, dword ptr [esp]
		push ecx
		push eax
		call ChangeLabelLogger
		add esp, 8
		mov eax, 0x0064CCD0
		add eax, 5
		jmp eax
	}
}

static void GetFieldsCfgPath(char* buf, int bufLen)
{
	GetModuleFileNameA((HMODULE)hins, buf, bufLen);
	char* dot = strrchr(buf, '.');
	if(dot) strcpy_s(dot, bufLen - (int)(dot - buf), ".fields.ini");
	else     strncat_s(buf, bufLen, ".fields.ini", _TRUNCATE);
}
static void SaveFieldPositions()
{
	char path[MAX_PATH]; GetFieldsCfgPath(path, MAX_PATH);
	for(int i = 0; i < 5; i++)
	{
		char section[16], val[64];
		sprintf_s(section, "field%d", i);
		sprintf_s(val, "%.6f,%.6f", g_fieldRatioX[i], g_fieldRatioY[i]);
		WritePrivateProfileStringA(section, "ratio", val, path);
	}
	for(int i = 0; i < 3; i++)
	{
		char section[16], val[64];
		sprintf_s(section, "mmbtn%d", i);
		sprintf_s(val, "%.6f,%.6f", g_mmBtnRatioX[i], g_mmBtnRatioY[i]);
		WritePrivateProfileStringA(section, "ratio", val, path);
	}
}
static void LoadFieldPositions()
{
	char path[MAX_PATH]; GetFieldsCfgPath(path, MAX_PATH);
	if(GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return;
	for(int i = 0; i < 5; i++)
	{
		char section[16], val[64];
		sprintf_s(section, "field%d", i);
		GetPrivateProfileStringA(section, "ratio", "-1.0,-1.0", val, sizeof(val), path);
		float rx = -1.f, ry = -1.f;
		sscanf_s(val, "%f,%f", &rx, &ry);
		g_fieldRatioX[i] = rx; g_fieldRatioY[i] = ry;
	}
	for(int i = 0; i < 3; i++)
	{
		char section[16], val[64];
		sprintf_s(section, "mmbtn%d", i);
		GetPrivateProfileStringA(section, "ratio", "-1.0,-1.0", val, sizeof(val), path);
		float rx = -1.f, ry = -1.f;
		sscanf_s(val, "%f,%f", &rx, &ry);
		g_mmBtnRatioX[i] = rx; g_mmBtnRatioY[i] = ry;
	}
}
static void SaveMMStateOffset()
{
	char path[MAX_PATH]; GetFieldsCfgPath(path, MAX_PATH);
	char val[32] = {0};
	sprintf_s(val, "%d", g_mmStateOffset);
	WritePrivateProfileStringA("mm", "state_offset", val, path);
}
static void LoadMMStateOffset()
{
	char path[MAX_PATH]; GetFieldsCfgPath(path, MAX_PATH);
	if(GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return;
	char val[32] = {0};
	GetPrivateProfileStringA("mm", "state_offset", "", val, (DWORD)sizeof(val), path);
	if(val[0] == '\0') return;
	int parsed = g_mmStateOffset;
	if(sscanf_s(val, "%d", &parsed) == 1) g_mmStateOffset = parsed;
>>>>>>> b1bb3f5 (Refactor UI: Hardcoded layout, Server Status checks, Game Monitoring, and Build fixes)
}

static void LogMmHookStatus(const char* reason)
{
	if(reason == NULL) reason = "unknown";
	BYTE cc[8] = {0}; BYTE rg[8] = {0};
	bool okCC = SafeReadBytes(MM_CHANGELABEL_CORE, cc, 8);
	bool okRG = SafeReadBytes(0x6506E0, rg, 8);
	launcherdll_net_log(
		"mm: hook-status reason=%s changeLabel@0x%08X=%s %02X %02X %02X %02X %02X %02X %02X %02X register@0x6506E0=%s %02X %02X %02X %02X %02X %02X %02X %02X",
		reason, MM_CHANGELABEL_CORE, okCC ? "" : "<err>",
		cc[0], cc[1], cc[2], cc[3], cc[4], cc[5], cc[6], cc[7],
		okRG ? "" : "<err>",
		rg[0], rg[1], rg[2], rg[3], rg[4], rg[5], rg[6], rg[7]);
}
void ForceMMStateIfOpened()
{
	DWORD pObj = 0;
	if(SafeReadDWORD(MM_GLOBAL_OBJ_PTR, &pObj) && pObj)
	{
		if(g_mmStateOffset < 0 || g_mmStateOffset > 0x200 || (g_mmStateOffset & 0x3) != 0) return;
		DWORD sval = 0;
		if(SafeReadDWORD(pObj + g_mmStateOffset, &sval))
			if(sval > 0 && sval <= 0x33) SafeWriteDWORD(pObj + g_mmStateOffset, 0x34);
	}
}
static void WINAPI ChangeLabelObserveHook(DWORD pObj) {}
static void WINAPI MM_RegisterButtonHook(DWORD pObj)
{
	DWORD stateVal = 0; bool hasState = false;
	if(pObj != 0 && g_mmStateOffset >= 0 && (g_mmStateOffset & 0x3) == 0 && g_mmStateOffset <= 0x200)
		hasState = SafeReadDWORD(pObj + g_mmStateOffset, &stateVal);
	launcherdll_net_log("mm: RegisterHandler this=0x%08X stateOff=0x%X state=%s0x%08X",
		pObj, g_mmStateOffset, hasState ? "" : "<err>", stateVal);
}
static void LogMmObjSnapshot(const char* reason, DWORD pObj)
{
	if(reason == NULL) reason = "unknown";
	if(pObj == 0) { launcherdll_net_log("mm: snapshot reason=%s obj=<null>", reason); return; }
	const int offs[] = { 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x40 };
	for(int i = 0; i < (int)(sizeof(offs) / sizeof(offs[0])); i++)
	{
		DWORD v = 0;
		if(SafeReadDWORD(pObj + offs[i], &v))
			launcherdll_net_log("mm: snapshot reason=%s obj=0x%08X +0x%02X = 0x%08X", reason, pObj, offs[i], v);
		else
			launcherdll_net_log("mm: snapshot reason=%s obj=0x%08X +0x%02X = <err>", reason, pObj, offs[i]);
	}
}
static bool MM_IsSameOrChildWindow(HWND hParent, HWND hWnd)
{
	if(hParent == NULL || hWnd == NULL) return false;
	HWND cur = hWnd;
	while(cur != NULL) { if(cur == hParent) return true; cur = GetParent(cur); }
	return false;
}
static void LogMmClickMessage(MSG* pMsg)
{
	if(pMsg == NULL || g_hGameWnd == NULL || !IsWindow(g_hGameWnd)) return;
	if(pMsg->message != WM_LBUTTONDOWN && pMsg->message != WM_LBUTTONUP &&
	   pMsg->message != WM_RBUTTONDOWN && pMsg->message != WM_RBUTTONUP) return;
	if(!MM_IsSameOrChildWindow(g_hGameWnd, pMsg->hwnd)) return;
	POINT gamePt = {};
	gamePt.x = (int)(short)LOWORD(pMsg->lParam);
	gamePt.y = (int)(short)HIWORD(pMsg->lParam);
	if(pMsg->hwnd != g_hGameWnd && IsWindow(pMsg->hwnd))
		MapWindowPoints(pMsg->hwnd, g_hGameWnd, &gamePt, 1);
	char cls[96] = {0};
	if(pMsg->hwnd != NULL && IsWindow(pMsg->hwnd)) GetClassNameA(pMsg->hwnd, cls, (int)sizeof(cls));
	DWORD pObj = 0; DWORD stateVal = 0;
	bool hasObj = SafeReadDWORD(MM_GLOBAL_OBJ_PTR, &pObj) && pObj != 0;
	bool hasState = false;
	if(hasObj && g_mmStateOffset >= 0 && (g_mmStateOffset & 0x3) == 0 && g_mmStateOffset <= 0x200)
		hasState = SafeReadDWORD(pObj + g_mmStateOffset, &stateVal);
	const char* msgName = "WM_UNKNOWN";
	if(pMsg->message == WM_LBUTTONDOWN)      msgName = "WM_LBUTTONDOWN";
	else if(pMsg->message == WM_LBUTTONUP)   msgName = "WM_LBUTTONUP";
	else if(pMsg->message == WM_RBUTTONDOWN) msgName = "WM_RBUTTONDOWN";
	else if(pMsg->message == WM_RBUTTONUP)   msgName = "WM_RBUTTONUP";
	const char* btnTag = "Unknown";
	const char* btnNames[3] = { "Killer", "Hunter", "Talker" };
	RECT rc = {}; int cw = 0, ch = 0;
	if(GetClientRect(g_hGameWnd, &rc)) { cw = rc.right - rc.left; ch = rc.bottom - rc.top; }
	if(cw > 0 && ch > 0)
	{
		int best = -1; double bestDist2 = 0.0;
		for(int i = 0; i < 3; i++)
		{
			if(g_mmBtnRatioX[i] < 0.f || g_mmBtnRatioY[i] < 0.f) continue;
			double tx = g_mmBtnRatioX[i] * (double)cw;
			double ty = g_mmBtnRatioY[i] * (double)ch;
			double dx = (double)gamePt.x - tx, dy = (double)gamePt.y - ty;
			double d2 = dx*dx + dy*dy;
			if(best < 0 || d2 < bestDist2) { best = i; bestDist2 = d2; }
		}
		if(best >= 0)
		{
			double radius = (double)((cw < ch ? cw : ch) / 12);
			if(radius < 24.0) radius = 24.0;
			if(bestDist2 <= radius * radius) btnTag = btnNames[best];
		}
	}
	launcherdll_net_log(
		"mm: click %s tag=%s srcHwnd=0x%p class=%s gamePt=(%ld,%ld) obj=%s0x%08X stateOff=0x%X state=%s0x%08X",
		msgName, btnTag, pMsg->hwnd, cls[0] ? cls : "<unknown>",
		(long)gamePt.x, (long)gamePt.y,
		hasObj ? "" : "<err>", pObj, g_mmStateOffset, hasState ? "" : "<err>", stateVal);
	if(pMsg->message == WM_LBUTTONUP && btnTag[0] != 'U' && hasObj)
	{
		char reason[64] = {0};
		sprintf_s(reason, "after_%s", btnTag);
		LogMmObjSnapshot(reason, pObj);
		DWORD now = GetTickCount();
		if(now - g_lastMmHookStatusLogTick >= 1200)
		{
			g_lastMmHookStatusLogTick = now;
			LogMmHookStatus(reason);
		}
	}
}
static DWORD WINAPI MMDelayedClickThread(LPVOID pv)
{
	MMClickCtx* ctx = (MMClickCtx*)pv;
	Sleep(ctx->delayMs);
	POINT sp = ctx->clientPt;
	ClientToScreen(ctx->hWnd, &sp);
	SetCursorPos(sp.x, sp.y);
	LPARAM lp = MAKELPARAM((WORD)ctx->clientPt.x, (WORD)ctx->clientPt.y);
	PostMessage(ctx->hWnd, WM_LBUTTONDOWN, MK_LBUTTON, lp);
	PostMessage(ctx->hWnd, WM_LBUTTONUP,   0,          lp);
	delete ctx;
	return 0;
}
LRESULT CALLBACK MM_HookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	MSG* pMsg = (MSG*)lParam;
	if(nCode == HC_ACTION && pMsg != NULL)
	{
		LogMmClickMessage(pMsg);
		if(pMsg->message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000))
		{
			int tagIdx = -1; const char* tagName = "";
			if(pMsg->wParam == VK_F8)       { tagIdx = 0; tagName = "Killer"; }
			else if(pMsg->wParam == VK_F9)  { tagIdx = 1; tagName = "Hunter"; }
			else if(pMsg->wParam == VK_F10) { tagIdx = 2; tagName = "Talker"; }
			if(tagIdx >= 0 && g_hGameWnd != NULL && IsWindow(g_hGameWnd))
			{
				POINT pt; GetCursorPos(&pt);
				ScreenToClient(g_hGameWnd, &pt);
				RECT rc = {}; GetClientRect(g_hGameWnd, &rc);
				int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
				if(cw > 0 && ch > 0)
				{
					g_mmBtnRatioX[tagIdx] = (float)pt.x / cw;
					g_mmBtnRatioY[tagIdx] = (float)pt.y / ch;
					SaveFieldPositions();
					launcherdll_net_log("mm: teach tag=%s idx=%d ratio=(%.6f,%.6f) client=(%ld,%ld)",
						tagName, tagIdx, g_mmBtnRatioX[tagIdx], g_mmBtnRatioY[tagIdx],
						(long)pt.x, (long)pt.y);
				}
				pMsg->message = WM_NULL;
			}
		}
		if(pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_F6 && (GetKeyState(VK_CONTROL) & 0x8000))
		{
			POINT pt; GetCursorPos(&pt);
			if(g_hGameWnd) ScreenToClient(g_hGameWnd, &pt);
			RECT rc = {}; GetClientRect(g_hGameWnd, &rc);
			int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
			if(cw > 0 && ch > 0)
			{
				g_fieldRatioX[4] = (float)pt.x / cw;
				g_fieldRatioY[4] = (float)pt.y / ch;
				SaveFieldPositions();
			}
			pMsg->message = WM_NULL;
		}
		if(pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_F11)
		{
			if(g_fieldRatioX[4] >= 0.f && g_hGameWnd)
			{
				RECT rc = {}; GetClientRect(g_hGameWnd, &rc);
				int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
				if(cw > 0 && ch > 0)
				{
					MMClickCtx* ctx = new MMClickCtx();
					ctx->hWnd       = g_hGameWnd;
					ctx->clientPt.x = (LONG)(g_fieldRatioX[4] * cw + 0.5f);
					ctx->clientPt.y = (LONG)(g_fieldRatioY[4] * ch + 0.5f);
					ctx->delayMs    = 0;
					HANDLE hT = CreateThread(NULL, 0, MMDelayedClickThread, ctx, 0, NULL);
					if(hT) CloseHandle(hT); else delete ctx;
				}
			}
			pMsg->message = WM_NULL;
		}
	}
	return CallNextHookEx(g_hTabHook, nCode, wParam, lParam);
}
static DWORD WINAPI AutoRegisterMonitorThread(LPVOID pv)
{
	BOOL lastSeen = FALSE;
	while(TRUE)
	{
		if(!g_autoRegisterEnabled) { Sleep(1000); continue; }
		if(!g_hGameWnd) { Sleep(1000); continue; }
		DWORD obj = 0;
		if(SafeReadDWORD(MM_GLOBAL_OBJ_PTR, &obj))
		{
			BOOL seen = FALSE;
			if(obj && obj >= 0x01000000u && obj < 0x7F000000u)
			{
				DWORD vtbl = 0; if(SafeReadDWORD(obj, &vtbl))
					if(vtbl >= 0x400000u && vtbl < 0x960000u) seen = TRUE;
			}
			if(seen && !lastSeen)
			{
				if(g_fieldRatioX[4] >= 0.f)
				{
					RECT rc = {}; GetClientRect(g_hGameWnd, &rc);
					int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
					if(cw > 0 && ch > 0)
					{
						DWORD pObj = 0;
						if(SafeReadDWORD(MM_GLOBAL_OBJ_PTR, &pObj) && pObj)
						{
							DWORD sval = 0;
							bool hasState = (g_mmStateOffset >= 0 && (g_mmStateOffset & 0x3) == 0 && g_mmStateOffset <= 0x200)
								&& SafeReadDWORD(pObj + g_mmStateOffset, &sval);
							launcherdll_net_log("mm:auto: UI opened stateOff=0x%X state=%s0x%X",
								g_mmStateOffset, hasState ? "" : "<err>", sval);
							if(!hasState || sval > 0x33)
							{
								MMClickCtx* ctx = new MMClickCtx();
								ctx->hWnd       = g_hGameWnd;
								ctx->clientPt.x = (LONG)(g_fieldRatioX[4] * cw + 0.5f);
								ctx->clientPt.y = (LONG)(g_fieldRatioY[4] * ch + 0.5f);
								ctx->delayMs    = 0;
								HANDLE hT = CreateThread(NULL, 0, MMDelayedClickThread, ctx, 0, NULL);
								if(hT) CloseHandle(hT); else delete ctx;
								launcherdll_net_log("mm:auto: triggered click on Register btn at client=(%ld,%ld)",
									(long)ctx->clientPt.x, (long)ctx->clientPt.y);
							}
							else
							{
								launcherdll_net_log("mm:auto: skipped, UI is in Edit mode (stateVal=0x%X <= 0x33)", sval);
							}
						}
					}
				}
			}
			lastSeen = seen;
		}
		Sleep(1000);
	}
	return 0;
}
void MM_ApplyCodeHooks()
{
	if(kEnableMmChangeLabelHook)
		HookCode((void*)MM_CHANGELABEL_CORE, (void*)ChangeLabelHook, 5);
	if(kEnableMmRegisterHook)
		HookCode((void*)0x6506E0, (void*)MM_RegisterButtonHook, 5);
	LogMmHookStatus("hook-installed");
}
void MM_OnGameWindowCreated(HWND hwnd)
{
	LoadFieldPositions();
	LoadMMStateOffset();
	if(g_hAutoRegisterThread == NULL)
	{
		g_hAutoRegisterThread = CreateThread(NULL, 0, AutoRegisterMonitorThread, NULL, 0, NULL);
		launcherdll_net_log("mm:auto: AutoRegisterMonitorThread started handle=0x%p", g_hAutoRegisterThread);
	}
	if(g_hTabHook == NULL)
	{
		DWORD gameTid = GetWindowThreadProcessId(hwnd, NULL);
		g_hTabHook = SetWindowsHookEx(WH_GETMESSAGE, MM_HookProc, NULL, gameTid);
		launcherdll_net_log("hook: WH_GETMESSAGE installed tid=%u hook=0x%p", gameTid, g_hTabHook);
	}
}
// ── MatchMaking 功能區塊結束 ──────────────────────────────────────────────

HWND (WINAPI* real_CreateWindowEx)(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, 
								   int x, int y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, 
								   HINSTANCE hInstance, LPVOID lpParam) = CreateWindowExA;

HWND WINAPI my_CreateWindowEx(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, 
							  int x, int y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, 
							  HINSTANCE hInstance, LPVOID lpParam)
{
	HWND hWndRet;
	bool bCreate = false;
	
	if(_stricmp(lpClassName, "Lineage") == 0)
	{
		FixProcessDpiAwareness();

		// 僅在遊戲主窗建立時注入記憶體補丁與登入資料相關 hook。
		//MessageBox(NULL, _T("Start Patch Code..."), _T("OK"), MB_OK);

		// 先保證主窗可拖動：補回標題列與框線，避免錯誤視窗期間無法移動。
		if(kEnableWindowStyleGuard)
		{
			dwStyle &= ~WS_POPUP;
			dwStyle |= (WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);
		}

		DWORD code = 0x859001B0;
		PatchCode((void*)0x00722761, &code, sizeof(DWORD));
		PatchCode((void*)0x00772BA0, (void*)path_code, sizeof(path_code));

		//MessageBox(NULL, _T("开始创建游戏主窗口...."), _T("创建窗口"), MB_OK);
		if(buffer != NULL)
			HookCode((void*)FILE_HOOK_ADDR, GetFileData, 5);
		HookCode((void*)USER_HOOK_ADDR, GetUsername, USER_RETN_ADDR - USER_HOOK_ADDR);
		HookCode((void*)PASS_HOOK_ADDR, GetPassword, PASS_RETN_ADDR - PASS_HOOK_ADDR);
		HookCode((void*)SETID_HOOK_ADDR, SetIdPass, SETID_RETN_ADDR - SETID_HOOK_ADDR);
		MM_ApplyCodeHooks();

		srand(GetTickCount());
		const char* str = "abcdefghigklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

		int cnt = 8 + (rand() % 8);
		for(int i = 0; i < cnt; i++)
		{
			szTitle[i] = str[rand() % strlen(str)];
		}
		szTitle[cnt] = 0;

		lpWindowName = szTitle;
		
		bCreate = true;
	}

	hWndRet = real_CreateWindowEx(dwExStyle, lpClassName, lpWindowName, dwStyle, x, y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
	if(bCreate && hWndRet != NULL)
	{
		g_hGameWnd = hWndRet;
		SetGameCompatibilityFlags();
		DisableTsfForThread(hWndRet, "CreateWindowEx");
		EnsureImeContext(hWndRet);
		RefreshImeComposition(hWndRet);
		launcherdll_net_log("ime: game window created hwnd=0x%p class=%s", hWndRet, lpClassName);
		MM_OnGameWindowCreated(hWndRet);
		if(kEnableImeCandidateFix && kEnableImeWndProcHook && g_realGameWndProc == NULL)
		{
			g_realGameWndProc = (WNDPROC)SetWindowLongPtr(hWndRet, GWLP_WNDPROC, (LONG_PTR)GameWndProcHook);
			launcherdll_net_log("ime: wndproc hook set hwnd=0x%p oldproc=0x%p", hWndRet, g_realGameWndProc);
		}
		if(kEnableWindowStyleGuard)
		{
			if(!IsWindowEnabled(hWndRet))
				EnableWindow(hWndRet, TRUE);
			SetWindowPos(hWndRet, NULL, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
		}
		if((kEnableWindowStyleGuard || kEnableWin10UiFixes) && g_hWindowGuardThread == NULL)
			g_hWindowGuardThread = CreateThread(NULL, 0, WindowGuardThread, NULL, 0, NULL);
		if(kEnableImeCandidateFix && g_hImeGuardThread == NULL)
		{
			g_hImeGuardThread = CreateThread(NULL, 0, ImeGuardThread, NULL, 0, NULL);
			launcherdll_net_log("ime: guard thread started handle=0x%p", g_hImeGuardThread);
		}
	}
// 	if(ShareInfo.usehelper && bCreate)
// 	{
// 		CreateHelperDialog();
// 		DWORD tid = GetWindowThreadProcessId(hWndRet, NULL);
// 		__fn1(tid);
// 	}

	return hWndRet;
}

BYTE* GetFileBuffer()
{
	FILE* fp;
	DWORD len = 0;
	buffer_len = 0;
	//MessageBox(NULL, _T("开始加载变身档案...."), _T("变身档"), MB_OK);
	if(_tfopen_s(&fp, ShareInfo.bdfile, _T("rb")) == 0)
	{
		fseek(fp, 0, SEEK_END);
		len = ftell(fp);
		fseek(fp, 0, SEEK_SET);

		// 讀取加密壓縮檔後：先解密，再解壓成遊戲可直接使用的緩衝。
		BYTE* file_data = new BYTE[len];
		fread(file_data, 1, len, fp);
		fclose(fp);
		VMProtectBegin
		//解密
		config_decrypt(&file_data[4], &file_data[20], len - 20);
		
		DWORD un_len = *(DWORD*)file_data;
		//解压
		BYTE* un_buffer = new BYTE[un_len + 1];
		int ret = uncompress(un_buffer, &un_len, &file_data[20], len - 20);
		un_buffer[un_len] = 0;
		VMProtectEnd
		delete []file_data;
		if(ret == Z_OK)
		{
			//MessageBox(NULL, _T("加载ok..."), _T("ok"), MB_OK);
			buffer_len = un_len;
			return un_buffer;
		}

<<<<<<< HEAD
  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  LONG err1 = DetourAttach(&(PVOID &)real_connect, my_connect);
  LONG err2 = DetourAttach(&(PVOID &)real_send, my_send);
  LONG err_recv = DetourAttach(&(PVOID &)real_recv, my_recv);
  LONG err4 = DetourAttach(&(PVOID &)real_CreateWindowEx, my_CreateWindowEx);
  LONG err8 = DetourAttach(&(PVOID &)real_CreateWindowExW, my_CreateWindowExW);
  LONG err9 = DetourAttach(&(PVOID &)real_MessageBoxA, my_MessageBoxA);
  LONG err10 = DetourAttach(&(PVOID &)real_MessageBoxW, my_MessageBoxW);
  LONG errA = DetourAttach(&(PVOID &)real_SetWindowPos, my_SetWindowPos);
  
  // 注入時間欺騙函式，修正主程式 2038 Bug 所導致的 RSA 加密亂碼以及逾期警告
  DetourAttach(&(PVOID &)real_GetLocalTime, my_GetLocalTime);
  DetourAttach(&(PVOID &)real_GetSystemTime, my_GetSystemTime);
  DetourAttach(&(PVOID &)real_GetSystemTimeAsFileTime, my_GetSystemTimeAsFileTime);
  DetourAttach(&(PVOID &)real_GetTickCount, my_GetTickCount);
  
  // timeGetTime 來自 winmm.dll，需動態取得地址
  HMODULE hWinmm = GetModuleHandleA("winmm.dll");
  if (!hWinmm) hWinmm = LoadLibraryA("winmm.dll");
  if (hWinmm) {
      real_timeGetTime = (DWORD(WINAPI *)())GetProcAddress(hWinmm, "timeGetTime");
      if (real_timeGetTime) {
          DetourAttach(&(PVOID &)real_timeGetTime, my_timeGetTime);
      }
  }

  LONG commitErr = DetourTransactionCommit();

  fopen_s(&f, "C:\\3.81Lineage\\hook_debug.txt", "a+");
  if (f) {
    fprintf(f,
            "[%u] Commit=%d, conn=%d, send=%d, cwnd=%d, cwndW=%d, pos=%d, "
            "msgA=%d, msgW=%d\n",
            GetCurrentProcessId(), commitErr, err1, err2, err4, err8, errA,
            err9, err10);
    fclose(f);
  }
    fclose(f);
  }
=======
		delete []un_buffer;
	}
	return NULL;
>>>>>>> b1bb3f5 (Refactor UI: Hardcoded layout, Server Status checks, Game Monitoring, and Build fixes)
}

DWORD WINAPI PatchThread(void* p)
{
	// 等待目標位址進入可 patch 狀態後再寫入，降低啟動時序造成的失敗。
	// __try/__except：遊戲受 VMProtect/Oreans 保護，反覆存取受保護位址會觸發 VEH
	// 拋出 C++ 例外（0xE06D7363）使進程崩潰；SEH 包覆後安靜退出，不影響遊戲。
	__try
	{
		while(true)
		{
			if(*(DWORD*)0x004E204E == 0x0097850F)
			{
				DWORD code = 0x0097E990;
				PatchCode((void*)0x004E204E, &code, sizeof(DWORD));
				break;
			}
			Sleep(1);
		}
	}
	__except(1) { /* 位址受保護或無效，安靜退出 */ }
	return 0;
}

void init()
{
	// 初始化流程（注入後第一個關鍵節點）：
	// 1) 讀取共享記憶體中的啟動參數
	// 2) 回寫 read=true 告知 Launcher 可退出
	// 3) 初始化加密上下文（含 RSA 還原參數）
	// 4) 視設定載入附加資源（如變檔）
	// 5) 註冊 Detours Hook，正式接管網路/視窗關鍵 API
	VMProtectBegin
	SHARE_INFO* pShareInfo = get_shm(GetCurrentProcessId(), false);
	if(pShareInfo == NULL)
	{
		DWORD err = GetLastError();
		_TCHAR msg[512] = {0};
		_stprintf_s(msg, _T("无法初始化共享信息, 程序退出!\nPID=%u\nGetLastError=%u"), GetCurrentProcessId(), err);
		MessageBox(NULL, msg, _T("错误"), MB_ICONERROR);
		ExitProcess(0);
		return;
	}

	// 0318 compatibility:
	// Wait until Launcher writes the SHM magic marker before consuming data.
	int timeout = 0;
	while(*(volatile DWORD*)&pShareInfo->magic != 0x12345678 && timeout < 50)
	{
		Sleep(100);
		timeout++;
	}

	launcherdll_net_log(
		"init: magicWait finished waited=%d curMagic=0x%08X",
		timeout, (unsigned int)pShareInfo->magic);
	memcpy(&ShareInfo, pShareInfo, sizeof(SHARE_INFO));
	launcherdll_net_log(
		"init: encrypt=%d randenc=%d port=%d rsa_d(raw)=%lu rsa_n(raw)=%lu",
		ShareInfo.encrypt ? 1 : 0,
		ShareInfo.randenc ? 1 : 0,
		ShareInfo.port,
		(unsigned long)ShareInfo.RSA_D,
		(unsigned long)ShareInfo.RSA_N);
	// 回寫握手完成旗標，讓 Launcher 解除等待。
	pShareInfo->read = true;

	// 0318 compatibility:
	// server AutoAuthentication uses native Blowfish; DLL should not add encdec/AES layer.
	ShareInfo.encrypt = false;
	launcherdll_net_log("init: forced ShareInfo.encrypt=false (server uses native Blowfish only)");

	if(ShareInfo.encrypt)
	{
		// 建立 RSA 相關參數，用於首次握手資料還原（XOR 常數與 Encoder/struct.h 一致）。
		bn_ctx = BN_CTX_new();
		d = BN_new();
		n = BN_new();

		unsigned long rsaD = pShareInfo->RSA_D ^ SERVER_LIST_RSA_XOR_D;
		unsigned long rsaN = pShareInfo->RSA_N ^ SERVER_LIST_RSA_XOR_N;
		BN_set_word(d, rsaD);
		BN_set_word(n, rsaN);
		if(rsaD == 0 || rsaN == 0)
			launcherdll_net_log("init: WARNING encrypt=1 but RSA d/n is zero after XOR restore (check pack.properties / list)");
		launcherdll_net_log("init: rsa_d(restored)=%lu rsa_n(restored)=%lu", rsaD, rsaN);
		//__dbg_print("=======Need Crypt=======");

		//__dbg_print("d: %s, n: %s, randenc: %d", BN_bn2dec(d), BN_bn2dec(n), pShareInfo->randenc);
	}

	free_shm();
	
	//MessageBox(NULL, _T("开始加载变身档文件!!!!"), 0, 0);
	if(ShareInfo.usebd)
		// 變檔屬於可選功能，失敗不直接中止主流程。
		buffer = GetFileBuffer();

	// 同步加解密金鑰，供封包攔截流程使用。
	encdec_init_key(ShareInfo.key);

// 	if(ShareInfo.usehelper)
// 		LoadLibrary(_T("Ada.dll"));

	// Detours 事務：全部 attach 成功才一次提交，避免半套 hook 狀態。
	DetourRestoreAfterWith();
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourAttach(&(PVOID&)real_connect, my_connect);
	DetourAttach(&(PVOID&)real_send, my_send);
	DetourAttach(&(PVOID&)real_recv, my_recv);
	DetourAttach(&(PVOID&)real_CreateWindowEx, my_CreateWindowEx);
	DetourAttach(&(PVOID&)real_CreateWindowExW, my_CreateWindowExW);
	DetourAttach(&(PVOID&)real_MessageBoxA, my_MessageBoxA);
	DetourAttach(&(PVOID&)real_MessageBoxW, my_MessageBoxW);
	DetourAttach(&(PVOID&)real_SetWindowPos, my_SetWindowPos);
	
	// 注入時間欺騙函式，修正主程式 2038 Bug 所導致的 RSA 加密亂碼以及逾期警告
	DetourAttach(&(PVOID&)real_GetLocalTime, my_GetLocalTime);
	DetourAttach(&(PVOID&)real_GetSystemTime, my_GetSystemTime);
	DetourAttach(&(PVOID&)real_GetSystemTimeAsFileTime, my_GetSystemTimeAsFileTime);
	DetourAttach(&(PVOID&)real_GetTickCount, my_GetTickCount);

	// timeGetTime 來自 winmm.dll，需動態取得地址
	HMODULE hWinmm = GetModuleHandleA("winmm.dll");
	if(!hWinmm) hWinmm = LoadLibraryA("winmm.dll");
	if(hWinmm) {
		real_timeGetTime = (DWORD(WINAPI*)())GetProcAddress(hWinmm, "timeGetTime");
		if(real_timeGetTime) {
			DetourAttach(&(PVOID&)real_timeGetTime, my_timeGetTime);
		}
	}

	DetourTransactionCommit();

	CloseHandle(CreateThread(NULL, 0, PatchThread, NULL, 0, NULL));

	// 啟動 Win10/Win11 輔助修正執行緒
	g_hWindowGuardThread = CreateThread(NULL, 0, WindowGuardThread, NULL, 0, NULL);
	g_hImeGuardThread = CreateThread(NULL, 0, ImeGuardThread, NULL, 0, NULL);
	
	VMProtectEnd
}

bool __stdcall __fn1(DWORD tid)
{
	VMProtectBegin
	// 對指定執行緒安裝訊息鉤子。
	h_hook = SetWindowsHookEx(WH_GETMESSAGE, HookProc, hins, tid);
	VMProtectEnd
	return h_hook != NULL;
}

int __stdcall DLLGetVersion()
{
	return 0x1001;
}

char* __stdcall DLLGetInformation()
{
	return "Lin LauncherDll";
}
