#include "stdafx.h"
#include "LauncherDll.h"
#include "MimirPowerHook.h"
#include "LineageEncryption.h"
#include "MatchMakingHook.h"
#include "WebNavigateHook.h"
#include "HitFlinchPatch.h"
#include "SmoothRunPatch.h"
#include "VitalsPacketHook.h"
#include "ShowClockPatch.h"
#include "AttackDamageHook.h"
#include "PssOverlay.h"
#include "PssConfig.h"
#include "InventoryDebugHook.h"
#include "WarehouseStatusHook.h"
#include "TradeStatusHook.h"
#include "PrivateShopStatus.h"
#include "PatchUtil.h"
#include "EquipUiPatch.h"
#include "Login77Hook.h"
#include "ItemStatusColorHook.h"
#include "MorphPakInject.h"

#include "VMProtectSDK.h"
#include <stdarg.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")
#ifndef DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED
#define DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED ((HANDLE) - 5)
#endif
#ifndef DPI_AWARENESS_CONTEXT_SYSTEM_AWARE
#define DPI_AWARENESS_CONTEXT_SYSTEM_AWARE ((HANDLE) - 2)
#endif

// =============================================================================
// 全域變數宣告
// =============================================================================
// SHARE_INFO struct is now in ShareMemory.h
HHOOK hhk = NULL;
HHOOK h_hook = NULL;
HINSTANCE hins;
HANDLE g_hInitEvent = NULL;
SHARE_INFO ShareInfo;
char szTitle[32];
HWND g_hGameWnd = NULL;
bool g_dpiFixed = false;
static bool g_hooked = false;

int _seed = 0;
int _xorByte = 0;

static DWORD _rsaD = 0;
static DWORD _rsaN = 0;
// 小數模冪：計算 base^exp mod mod（適用於 authdata ^ D mod N，皆為 DWORD）
static DWORD modpow(unsigned long base, unsigned long exp, unsigned long mod) {
  // 登入握手收到的 4-byte authdata 需要用 RSA 私鑰還原；這裡只處理協定使用的
  // DWORD 範圍，不能把欄位誤當成任意精度資料。
  if (mod == 0)
    return 0;
  DWORDLONG result = 1;
  DWORDLONG b = base % mod;
  while (exp > 0) {
    if (exp & 1)
      result = result * b % mod;
    b = b * b % mod;
    exp >>= 1;
  }
  return (DWORD)result;
}

bool inited = false;

static int nextRand() {
  // 與伺服器 RandomEnc 使用相同的 LCG；呼叫端必須持有送包鎖，避免金鑰流錯位。
  _seed = (214013 * _seed + 2531011) & 0x7FFFFFFF;
  return (int)(_seed >> 16) & 0xFF;
}

static void launcherdll_vlog(const char *fmt, va_list args) {
  // 只允許 PSS UI 診斷與安裝結果進入檔案，避免高頻封包／繪製路徑刷爆 Log。
  char msg[2048] = {0};
  vsprintf_s(msg, fmt, args);
  if (strstr(msg, "[Pss]") == NULL && strstr(msg, "[PssUI]") == NULL &&
      strstr(msg, "[Install]") == NULL)
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
  fprintf(fp, "[%04d-%02d-%02d %02d:%02d:%02d.%03d][PID=%u][TID=%u] %s\n",
          st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
          st.wMilliseconds, (unsigned int)GetCurrentProcessId(),
          (unsigned int)GetCurrentThreadId(), msg);
  fflush(fp);
  fclose(fp);
}

void launcherdll_hook_log(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  launcherdll_vlog(fmt, args);
  va_end(args);
}

// WH_GETMESSAGE：主執行緒沖待送封包，並處理 PSS 熱鍵／點選道具。
static LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam) {
  // 此 Hook 在遊戲主執行緒處理待送工作與 PSS UI；滑鼠事件必須等原生處理完成
  // 後才讀取目前格子的道具。
  if (nCode >= 0) {
    MimirPowerHook_PumpPendingChoice();
    PssOverlay_PumpPendingSave();
    PssOverlay_PumpPendingUiNotify();

    MSG *pMsg = (MSG *)lParam;
    if (pMsg->message == WM_KEYDOWN &&
        (pMsg->wParam == VK_HOME || pMsg->wParam == VK_INSERT)) {
      PssOverlay_Show();
    }
    // WM_LBUTTONUP：遊戲已處理完這次點擊，才能讀到當格道具。
    if (pMsg->message == WM_LBUTTONUP) {
      int pickSection = -1, pickSlot = -1;
      if (PssOverlay_IsPicking(&pickSection, &pickSlot)) {
        ClickedItemInfo clicked;
        if (InventoryDebug_FindJustClickedItem(&clicked)) {
          PssConfig_SendResolveItemRequest(pickSection, pickSlot,
                                                  clicked.objId);
        }
      }
    }
  }
  return CallNextHookEx(h_hook ? h_hook : hhk, nCode, wParam, lParam);
}

// =============================================================================
// API Hook 區（Network, Window）
// =============================================================================
int(WINAPI *real_connect)(SOCKET s, const struct sockaddr *name,
                          int namelen) = connect;
int(WINAPI *real_send)(SOCKET s, const char *buf, int len, int flag) = send;
int(WINAPI *real_recv)(SOCKET s, char *buf, int len, int flag) = recv;

static int WINAPI my_connect(SOCKET s, const struct sockaddr *name, int namelen) {
  // 將共享記憶體的伺服器位址套到原生 connect；解析失敗時保留原始 sockaddr。
  if (name == NULL || namelen < (int)sizeof(sockaddr_in))
    return real_connect(s, name, namelen);
  VMProtectBegin;
  sockaddr_in mappedAddr = *(const sockaddr_in *)name;
  bool hasMappedHost = false;
  char host[64] = {0};
  strncpy_s(host, sizeof(host), (const char *)ShareInfo.ip, _TRUNCATE);
  char *begin = host;
  while (*begin == ' ' || *begin == '\t' || *begin == '\r' || *begin == '\n')
    begin++;
  char *end = begin + strlen(begin);
  while (end > begin && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' ||
                         end[-1] == '\n'))
    *--end = '\0';
  char *colon = strchr(begin, ':');
  if (colon != NULL)
    *colon = '\0';
  if (begin[0] != '\0') {
    IN_ADDR parsedAddr = {};
    if (InetPtonA(AF_INET, begin, &parsedAddr) == 1) {
      mappedAddr.sin_addr = parsedAddr;
      hasMappedHost = true;
    } else {
      ADDRINFOA hints = {0};
      hints.ai_family = AF_INET;
      hints.ai_socktype = SOCK_STREAM;
      ADDRINFOA *result = NULL;
      if (getaddrinfo(begin, NULL, &hints, &result) == 0 && result != NULL) {
        mappedAddr.sin_addr = ((sockaddr_in *)result->ai_addr)->sin_addr;
        hasMappedHost = true;
        freeaddrinfo(result);
      }
    }
  }
  if (hasMappedHost) {
    mappedAddr.sin_port = htons(ShareInfo.port);
    MimirPowerHook_SetSocket(s);
    VMProtectEnd;
    inited = false;
    return real_connect(s, (const sockaddr *)&mappedAddr, sizeof(mappedAddr));
  }
  MimirPowerHook_SetSocket(s);
  VMProtectEnd;
  inited = false;
  return real_connect(s, name, namelen);
}

// 編碼與送出共用 _seed/_xorByte，必須同一把鎖串行，否則與 server 金鑰流錯位。
struct SendLock {
  CRITICAL_SECTION cs;
  SendLock() { InitializeCriticalSection(&cs); }
};
static SendLock &GetSendLock() {
  static SendLock lock;
  return lock;
}
struct SendLockGuard {
  CRITICAL_SECTION &cs;
  SendLockGuard(CRITICAL_SECTION &c) : cs(c) { EnterCriticalSection(&cs); }
  ~SendLockGuard() { LeaveCriticalSection(&cs); }
};

static int my_send(SOCKET s, const char *buf, int len, int flag) {
  // send 與 MimirSendEncoded 共用金鑰流，複製、編碼與 real_send 必須串行化。
  if (buf == NULL || len <= 0)
    return real_send(s, buf, len, flag);
  SendLockGuard lockGuard(GetSendLock().cs);
  BYTE stackBuffer[4096];
  BYTE *buffer_ptr = stackBuffer;
  bool useHeap = false;
  if (len > (int)sizeof(stackBuffer)) {
    buffer_ptr = new BYTE[len];
    useHeap = true;
  }
  memcpy(buffer_ptr, buf, len);
  // randenc=0：固定 XOR；randenc=1：逐 byte nextRand()。須與 server RandomEnc 一致。
  if (ShareInfo.encrypt && inited) {
    if (ShareInfo.randenc) {
      for (int i = 0; i < len; i++)
        buffer_ptr[i] ^= (unsigned char)nextRand();
    } else {
      for (int i = 0; i < len; i++)
        buffer_ptr[i] ^= (unsigned char)_xorByte;
    }
  }
  int ret = real_send(s, (const char *)buffer_ptr, len, flag);
  if (useHeap)
    delete[] buffer_ptr;
  MimirPowerHook_PumpPendingChoice();
  PssOverlay_PumpPendingSave();
  PssOverlay_PumpPendingUiNotify();
  return ret;
}

// 密米爾待送封包：與 my_send 同編碼，直接 real_send，避免再進 my_send。
int MimirSendEncoded(SOCKET s, const BYTE *body, int len) {
  // Mimir 封包不能再次進入 my_send，否則會被重複編碼；此處直接呼叫 real_send。
  if (body == NULL || len <= 0)
    return real_send(s, (const char *)body, len, 0);
  SendLockGuard lockGuard(GetSendLock().cs);
  BYTE stackBuffer[64];
  BYTE *buffer_ptr = stackBuffer;
  bool useHeap = false;
  if (len > (int)sizeof(stackBuffer)) {
    buffer_ptr = new BYTE[len];
    useHeap = true;
  }
  memcpy(buffer_ptr, body, len);
  if (ShareInfo.encrypt && inited) {
    if (ShareInfo.randenc) {
      for (int i = 0; i < len; i++)
        buffer_ptr[i] ^= (unsigned char)nextRand();
    } else {
      for (int i = 0; i < len; i++)
        buffer_ptr[i] ^= (unsigned char)_xorByte;
    }
  }
  int ret = real_send(s, (const char *)buffer_ptr, len, 0);
  if (useHeap)
    delete[] buffer_ptr;
  return ret;
}

static int my_recv(SOCKET s, char *buf, int len, int flag) {
  // 首次加密接收先消耗 RSA authdata，建立 XOR 或 RandomEnc 狀態後再透傳資料。
  if (ShareInfo.encrypt && !inited) {
    char buffer[32];
    memset(buffer, 0, sizeof(buffer));
    int read_len = 0;
    while (read_len < 4) {
      int ret = real_recv(s, &buffer[read_len], 4 - read_len, 0);
      if (ret > 0)
        read_len += ret;
      else {
        if (WSAGetLastError() == WSAEWOULDBLOCK)
          continue;
        else
          return ret;
      }
    }
    // 4-byte RSA authdata：用 D,N 還原明文後，依 randenc 開關選路徑（對齊伺服器 RandomEnc）。
    {
      unsigned long plain = modpow(*(unsigned long *)buffer, _rsaD, _rsaN);
      if (ShareInfo.randenc) {
        _seed = (int)plain;
      } else {
        _xorByte = (unsigned char)((plain % 255) + 1);
      }
    }
    inited = true;
  }
  int ret = real_recv(s, buf, len, flag);
  if (ret <= 0) {
    int err = (ret < 0) ? WSAGetLastError() : 0;
    if (ret < 0 && err == WSAEWOULDBLOCK) {
      return ret;
    }
    return ret;
  }
  // 逐包 hex dump 已拿掉：每個 recv 寫 launcher.log 會讓關窗卡死。
  return ret;
}

// =============================================================================
// Window Hook（視窗建立 / 標題隨機化）
//
// 對照 L1J3.8Launcher(RUST)參考：
//   - 時間保護／PATCHCODE1 → PatchThread（對齊 patch.rs::wait_and_patch）
//   - 帳密／Login77        → InstallLogin77Hooks（對齊 login.rs）
//   - CreateWindowEx 只負責 UI：標題隨機化、g_hGameWnd、Helper、可選 GetFileData
// =============================================================================
HWND(WINAPI *real_CreateWindowEx)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int,
                                  int, HWND, HMENU, HINSTANCE,
                                  LPVOID) = CreateWindowExA;
HWND(WINAPI *real_CreateWindowExW)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int,
                                   int, int, HWND, HMENU, HINSTANCE,
                                   LPVOID) = CreateWindowExW;

// 遊戲主窗 class "Lineage" 首次建立：只裝 UI 側（登入／PATCHCODE1 已移出）。
static void OnLineageWindowCreating() {
  // 主窗只安裝一次 WH_GETMESSAGE；登入、解密與程式碼 Patch 由延遲執行緒負責。
  if (g_hooked)
    return;
  g_hooked = true;

  if (!h_hook) {
    h_hook = SetWindowsHookEx(WH_GETMESSAGE, (HOOKPROC)HookProc, hins,
                              GetCurrentThreadId());
  }
}

static void MakeRandomTitleA(char *out, size_t outLen) {
  // 產生 4 個英文字母加 4 個數字的標題，避免多開客戶端使用相同視窗標題。
  srand(GetTickCount());
  char randomStr[16]{};
  for (int i = 0; i < 8; i++) {
    if (i < 4)
      randomStr[i] = 'A' + (rand() % 26);
    else
      randomStr[i] = '0' + (rand() % 10);
  }
  randomStr[8] = '\0';
  sprintf_s(out, outLen, "%s", randomStr);
}

static HWND WINAPI my_CreateWindowEx(DWORD dwExStyle, LPCSTR lpClassName,
                              LPCSTR lpWindowName, DWORD dwStyle, int x, int y,
                              int nWidth, int nHeight, HWND hWndParent,
                              HMENU hMenu, HINSTANCE hInstance,
                              LPVOID lpParam) {
  // 只攔截 class="Lineage" 的主窗；其他視窗完整透傳原生參數與回傳值。
  bool isLineage = false;
  if (lpClassName && HIWORD(lpClassName) != 0 &&
      _stricmp(lpClassName, "Lineage") == 0) {
    OnLineageWindowCreating();
    MakeRandomTitleA(szTitle, sizeof(szTitle));
    lpWindowName = szTitle;
    isLineage = true;
  }
  HWND hWndRet = real_CreateWindowEx(dwExStyle, lpClassName, lpWindowName,
                                     dwStyle, x, y, nWidth, nHeight, hWndParent,
                                     hMenu, hInstance, lpParam);
  if (isLineage && hWndRet != NULL) {
    g_hGameWnd = hWndRet;
  }
  return hWndRet;
}

static HWND WINAPI my_CreateWindowExW(DWORD dwExStyle, LPCWSTR lpClassName,
                               LPCWSTR lpWindowName, DWORD dwStyle, int x,
                               int y, int nWidth, int nHeight, HWND hWndParent,
                               HMENU hMenu, HINSTANCE hInstance,
                               LPVOID lpParam) {
  // Unicode 版本採相同策略，標題先以 ANSI 產生再轉成寬字元。
  bool isLineage = false;
  static wchar_t szTitleW[32];
  if (lpClassName && HIWORD(lpClassName) != 0 &&
      _wcsicmp(lpClassName, L"Lineage") == 0) {
    OnLineageWindowCreating();
    MakeRandomTitleA(szTitle, sizeof(szTitle));
    swprintf_s(szTitleW, 32, L"%hs", szTitle);
    lpWindowName = szTitleW;
    isLineage = true;
  }
  HWND hWnd = real_CreateWindowExW(dwExStyle, lpClassName, lpWindowName,
                                   dwStyle, x, y, nWidth, nHeight, hWndParent,
                                   hMenu, hInstance, lpParam);
  if (isLineage && hWnd != NULL) {
    g_hGameWnd = hWnd;
  }
  return hWnd;
}

// 等 0x004E204E 變 Ready 或已補丁後寫 ConditionalPatch / PATCHCODE1，
// 再裝裝備欄、順跑、時鐘、傷害顯示。逾時 120 秒放棄。
static DWORD WINAPI PatchThread(void *p) {
  // 等待解密標記進入可修補狀態，再套用 ConditionalPatch、PATCHCODE1 與其餘
  // UI／戰鬥 Patch；保護殼尚未完成時寫入會被覆蓋。
  constexpr DWORD kDecryptAddr = 0x004E204E;
  constexpr DWORD kDecryptReady = 0x0097850F;
  constexpr DWORD kDecryptPatched = 0x0097E990;
  constexpr int kTimeoutMs = 120000;

  __try {
    const DWORD t0 = GetTickCount();
    bool alreadyPatched = false;

    while (true) {
      const DWORD elapsed = GetTickCount() - t0;
      if (elapsed >= (DWORD)kTimeoutMs) {
        launcherdll_hook_log("[Install] PatchThread timeout");
        return 0;
      }

      DWORD marker = 0;
      __try {
        marker = *(volatile DWORD *)kDecryptAddr;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        marker = 0;
      }

      if (marker == kDecryptReady) {
        alreadyPatched = false;
        break;
      }
      if (marker == kDecryptPatched) {
        alreadyPatched = true;
        break;
      }
      Sleep(1);
    }

    if (!alreadyPatched) {
      DWORD kernelPatch = kDecryptPatched;
      PatchCode((void *)kDecryptAddr, &kernelPatch, sizeof(DWORD));
    }

    DWORD patchCode1 = 0x859001B0;
    PatchCode((void *)0x00722761, &patchCode1, sizeof(DWORD));
    EquipUiPatch::InstallAll();
    InstallSmoothRunPatch();
    // VitalsPacketHook 進世界會斷線，不安裝。
    InstallShowClockPatch();
    InstallAttackDamageHook();
  } __except (1) {
    launcherdll_hook_log("[Install] PatchThread exception");
  }
  return 0;
}

// 等保護殼解密後再裝 hook（解殼前 patch 會被蓋掉）。
static DWORD WINAPI DelayedDetourThread(void *p) {
  // 所有需要解密後程式碼的 Hook 統一由此執行緒安裝；這裡只保留整體 Detours
  // 失敗與必要的初始化錯誤，細節由各模組自行回報成功結果。
  int waitCount = 0;
  while (!IsCodeDecrypt() && waitCount < 12000) {
    Sleep(10);
    waitCount++;
  }
  if (!IsCodeDecrypt()) {
    launcherdll_hook_log("[Install] decrypt timeout, hooks not installed");
    return 1;
  }

  MorphPak_InstallHook();

  DetourRestoreAfterWith();
  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  DetourAttach(&(PVOID &)real_connect, reinterpret_cast<PVOID>(my_connect));
  DetourAttach(&(PVOID &)real_send, reinterpret_cast<PVOID>(my_send));
  DetourAttach(&(PVOID &)real_recv, reinterpret_cast<PVOID>(my_recv));
  DetourAttach(&(PVOID &)real_CreateWindowEx,
               reinterpret_cast<PVOID>(my_CreateWindowEx));
  DetourAttach(&(PVOID &)real_CreateWindowExW,
               reinterpret_cast<PVOID>(my_CreateWindowExW));
  LONG detourResult = DetourTransactionCommit();
  if (detourResult != NO_ERROR) {
    launcherdll_hook_log("[Install] Winsock/CreateWindow detours fail=%ld",
                         detourResult);
  }

  InstallLogin77Hooks();
  InstallMimirPowerHook();
  InstallMatchMakingHook();
  InstallWebNavigateHook();
  InstallHitFlinchPatch();
  InstallItemStatusColorHook();
  InstallWarehouseStatusHook();
  InstallTradeStatusHook();
  InstallPrivateShopStatusHook();
  PatchThread(NULL);

  return 0;
}

// init: DLL 初始化進入點，供外部 Launcher 呼叫 (不可設為 static)
void init() {
  // 外部 Launcher 的初始化入口：讀共享設定、準備加密狀態，再啟動延遲安裝執行緒。
  VMProtectBegin;
  SHARE_INFO *pShareInfo = get_shm(GetCurrentProcessId(), false);
  if (pShareInfo == NULL) {
    launcherdll_hook_log("[Install] init get_shm failed");
    ExitProcess(0);
    return;
  }
  int timeout = 0;
  while (*(volatile DWORD *)&pShareInfo->magic != 0x12345678 && timeout < 50) {
    Sleep(100);
    timeout++;
  }
  memcpy(&ShareInfo, pShareInfo, sizeof(SHARE_INFO));
  pShareInfo->read = true;

  Login77Hook_SetAccount(ShareInfo.Account, ShareInfo.Password);

  _rsaD = ShareInfo.RSA_D;
  _rsaN = ShareInfo.RSA_N;
  free_shm();
  if (ShareInfo.usebd)
    MorphPak_Load();
  encdec_init_key(ShareInfo.key);

  CreateThread(NULL, 0, DelayedDetourThread, NULL, 0, NULL);

  if (g_hInitEvent) {
    SetEvent(g_hInitEvent);
    CloseHandle(g_hInitEvent);
    g_hInitEvent = NULL;
  }
  VMProtectEnd;
}

// __fn1: 外部 Hook 安裝進入點 (不可設為 static)
bool __stdcall __fn1(DWORD tid) {
  // 外部注入流程提供的訊息 Hook 入口；回傳值只表示 SetWindowsHookEx 是否成功。
  VMProtectBegin;
  h_hook = SetWindowsHookEx(WH_GETMESSAGE, HookProc, hins, tid);
  VMProtectEnd;
  return h_hook != NULL;
}

// DLLGetVersion: 提供 Launcher 辨識版本 (不可設為 static)
int __stdcall DLLGetVersion() { return 0x1002; }

// DLLGetInformation: 提供 Launcher 辨識 DLL 資訊 (不可設為 static)
const char *__stdcall DLLGetInformation() { return "LauncherDll"; }
