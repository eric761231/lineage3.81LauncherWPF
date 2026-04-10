#include "stdafx.h"
#include "LauncherDll.h"
#include "L1Offsets.h"

#include "VMProtectSDK.h"
#include "timeController.h"
#include <map>
#include <string>
#include <sstream>
#include <vector>

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
constexpr int SERVER_LIST_RSA_XOR_N = 22345678;
constexpr int SERVER_LIST_RSA_XOR_D = 32345678;

// SHARE_INFO struct is now in ShareMemory.h
HHOOK hhk = NULL;
HHOOK h_hook = NULL;
HINSTANCE hins;
HANDLE g_hInitEvent = NULL;
SHARE_INFO ShareInfo;
BYTE *buffer = NULL;
DWORD buffer_len = 0;
char szTitle[32];
HWND g_hGameWnd = NULL;
bool g_dpiFixed = false;
static bool g_hooked = false; // 是否已完成首次 Hook 安裝
int g_editCount = 0;           // 用於識別帳號/密碼編輯框
BYTE g_id[32];
BYTE g_pwd[32];
int g_pwd_pos = 0;

// RSA 金鑰（由共享記憶體讀入，類型均在 DWORD 範圍內）


// RSA 金鑰（由共享記憶體讀入，類型均在 DWORD 範圍內）


int _seed = 0;
int _xorByte = 0;

// RSA 金鑰（由共享記憶體讀入，類型均在 DWORD 範圍內）
static DWORD _rsaD = 0;
static DWORD _rsaN = 0;
// 小數模冪：計算 base^exp mod mod（適用於 authdata ^ D mod N，皆為 DWORD）
static DWORD modpow(unsigned long base, unsigned long exp, unsigned long mod) {
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
// 精靈戰鬥組態設定（Combat Config）
struct SpriteConfig {
  bool suppressFlinch;
  int bloodEffect;
};

std::map<int, SpriteConfig> g_SpriteConfigs;

// =============================================================================
// 前向宣告
// =============================================================================
static void launcherdll_net_log(const char *fmt, ...);
void __dbg_print(const char *fmt, ...);
bool __stdcall __fn1(DWORD tid);

// =============================================================================
// 亂數與封包加密
// =============================================================================
static int nextRand() {
  _seed = (214013 * _seed + 2531011) & 0x7FFFFFFF;
  return (int)(_seed >> 16) & 0xFF;
}

// __dbg_print: 跨檔案使用的除錯輸出函式 (不可設為 static)
void __dbg_print(const char *fmt, ...) {
  char buffer[8192] = {0};
  va_list args;
  va_start(args, fmt);
  vsprintf_s(buffer, fmt, args);
  va_end(args);
  OutputDebugStringA(buffer);
}

static void launcherdll_net_log(const char *fmt, ...) {
  char exePath[MAX_PATH] = {0};
  char logPath[MAX_PATH] = "./launcherdll_net.log";
  if (GetModuleFileNameA(NULL, exePath, MAX_PATH) > 0) {
    for (int i = (int)strlen(exePath) - 1; i >= 0; i--) {
      if (exePath[i] == '\\' || exePath[i] == '/') {
        exePath[i] = '\0';
        break;
      }
    }
    sprintf_s(logPath, "%s\\launcherdll_net.log", exePath);
  }
  FILE *fp = NULL;
  if (fopen_s(&fp, logPath, "a+") != 0 || fp == NULL)
    return;
  SYSTEMTIME st;
  GetLocalTime(&st);
  char msg[2048] = {0};
  va_list args;
  va_start(args, fmt);
  vsprintf_s(msg, fmt, args);
  va_end(args);
  fprintf(fp, "[%04d-%02d-%02d %02d:%02d:%02d.%03d][PID=%u][TID=%u] %s\n",
          st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
          st.wMilliseconds, (unsigned int)GetCurrentProcessId(),
          (unsigned int)GetCurrentThreadId(), msg);
  fflush(fp);
  fclose(fp);
}

static void LoadCombatConfig() {
  char exePath[MAX_PATH] = {0};
  if (GetModuleFileNameA(NULL, exePath, MAX_PATH) <= 0)
    return;
  // 截取執行檔目錄路徑
  for (int i = (int)strlen(exePath) - 1; i >= 0; i--) {
    if (exePath[i] == '\\' || exePath[i] == '/') {
      exePath[i] = '\0';
      break;
    }
  }
  char xmlPath[MAX_PATH];
  sprintf_s(xmlPath, "%s\\xml\\bloodeffect.xml", exePath);
  FILE *fp = NULL;
  if (fopen_s(&fp, xmlPath, "r") != 0 || !fp) {
    launcherdll_net_log("[CombatFix] XML not found: %s", xmlPath);
    return;
  }
  char line[512];
  int count = 0;
  while (fgets(line, sizeof(line), fp)) {
    // 逐行解析 XML 標籤（例如 <Sprite ）
    if (strstr(line, "<Sprite")) {
      int spriteId = -1;
      char flinchStr[32] = "false";
      int bloodEffectID = 10770; // 預設血液特效 ID
      // 解析 id 屬性
      char *pId = strstr(line, "id=\"");
      if (pId)
        sscanf_s(pId + 4, "%d", &spriteId);
      // 解析 suppressFlinch 屬性
      char *pFlinch = strstr(line, "suppressFlinch=\"");
      if (pFlinch)
        sscanf_s(pFlinch + 16, "%[^\"]", flinchStr,
                 (unsigned int)sizeof(flinchStr));
      // 解析 bloodEffect 屬性
      char *pBlood = strstr(line, "bloodEffect=\"");
      if (pBlood)
        sscanf_s(pBlood + 13, "%d", &bloodEffectID);
      if (spriteId != -1) {
          SpriteConfig cfg{};
        cfg.suppressFlinch = (_stricmp(flinchStr, "true") == 0);
        cfg.bloodEffect = bloodEffectID;
        g_SpriteConfigs[spriteId] = cfg;
        count++;
      }
    }
  }
  fclose(fp);
  launcherdll_net_log("[CombatFix] Loaded %d monster configs from %s", count,
                      xmlPath);
}

static void bytes_to_hex_preview(const BYTE *data, int len, char *out,
                                 size_t outSize, int maxBytes) {
  if (out == NULL || outSize == 0)
    return;
  out[0] = '\0';
  if (data == NULL || len <= 0)
    return;
  if (maxBytes <= 0)
    maxBytes = len;
  int n = (len < maxBytes) ? len : maxBytes;
  size_t pos = 0;
  for (int i = 0; i < n; i++) {
    int w = sprintf_s(out + pos, outSize - pos, "%02X%s", data[i],
                      (i == n - 1) ? "" : " ");
    if (w <= 0 || (size_t)w >= outSize - pos)
      break;
    pos += (size_t)w;
  }
  if (len > n && pos + 5 < outSize)
    strcat_s(out, outSize, " ...");
}

static void bytes_to_ascii_preview(const BYTE *data, int len, char *out,
                                   size_t outSize, int maxBytes) {
  if (out == NULL || outSize == 0)
    return;
  out[0] = '\0';
  if (data == NULL || len <= 0)
    return;
  if (maxBytes <= 0)
    maxBytes = len;
  int n = (len < maxBytes) ? len : maxBytes;
  int i = 0;
  for (i = 0; i < n && i < (int)outSize - 1; i++) {
    unsigned char c = data[i];
    out[i] = (c >= 32 && c <= 126) ? (char)c : '.';
  }
  out[i] = '\0';
  if (len > n && i < (int)outSize - 5)
    strcat_s(out, outSize, "...");
}

static int find_subseq(const BYTE *haystack, int hayLen, const BYTE *needle,
                       int needleLen) {
  if (haystack == NULL || needle == NULL || hayLen <= 0 || needleLen <= 0 ||
      needleLen > hayLen)
    return -1;
  for (int i = 0; i <= hayLen - needleLen; i++) {
    bool match = true;
    for (int j = 0; j < needleLen; j++) {
      if (haystack[i + j] != needle[j]) {
        match = false;
        break;
      }
    }
    if (match)
      return i;
  }
  return -1;
}

// =============================================================================
// Advanced Combat Helpers (C++ Lookup)
// =============================================================================
// 查詢指定精靈是否抑制受擊硬直
extern "C" bool __stdcall GetSuppressFlinch(int spriteId) {
  auto it = g_SpriteConfigs.find(spriteId);
  if (it != g_SpriteConfigs.end()) {
    return it->second.suppressFlinch;
  }
  return false;
}

// 查詢指定精靈的血液特效 ID
extern "C" int __stdcall GetBloodEffect(int spriteId) {
  auto it = g_SpriteConfigs.find(spriteId);
  if (it != g_SpriteConfigs.end()) {
    return it->second.bloodEffect;
  }
  return 10770; // 預設血液特效
}

// =============================================================================
// Advanced Combat Hooks (Naked Jumpers)
// =============================================================================
// Advanced hooks have been moved to NakedFlinchHook.cpp, NakedBloodHook.cpp,
// NakedLocomotionHook.cpp

// =============================================================================
// 記憶體補丁與 Hook 安裝輔助函式（Patch / Hook）
// =============================================================================
static bool IsCodeDecrypt() {
  __try {
    DWORD val = *(volatile DWORD *)0x0058788B;
    return val == 0x85C0B60F || val == 0x4D8D016A;
  } __except (1) {
  }
  return false;
}

static void PatchCode(void *addr, void *code, int len) {
  DWORD dwOldProtect;
  VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, PAGE_READWRITE,
                   &dwOldProtect);
  memcpy(addr, code, len);
  VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, dwOldProtect,
                   &dwOldProtect);
}

static void HookCode(void *addr, void *func, int len) {
  if (len < 5)
    return;
  DWORD dwOldProtect;
  BYTE *patch = new BYTE[len];
  memset(patch, 0x90, len);
  patch[0] = 0xE9;
  *(DWORD *)&patch[1] = (DWORD)((uintptr_t)func - (uintptr_t)addr - 5);
  VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, PAGE_READWRITE,
                   &dwOldProtect);
  memcpy(addr, patch, len);
  VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, dwOldProtect,
                   &dwOldProtect);
  delete[] patch;
}

// =============================================================================
// Helper 輔助對話框鍵盤攔截
// =============================================================================
static LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode >= 0) {
    MSG *pMsg = (MSG *)lParam;
    if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_HOME) {
      if (ShareInfo.usehelper) {
        ShowOrHideHelperDialog();
      }
    }
  }
  return CallNextHookEx(h_hook ? h_hook : hhk, nCode, wParam, lParam);
}

// =============================================================================
// File Replacement Bare Hook
// =============================================================================
const DWORD USER_HOOK_ADDR = 0x0077317D;
const DWORD USER_RETN_ADDR = 0x00773183;

static void __stdcall UserNameHandler(void *p) {
  memcpy(g_id, p, 32);
  g_id[31] = 0; // ensure C-string termination
}

__declspec(naked) void GetUsername(void) {
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

static void __stdcall PasswordHandler(BYTE PassByte) {
  if (g_pwd_pos == 0)
    memset(g_pwd, 0, 32);
  if (g_pwd_pos < 31) {
    g_pwd[g_pwd_pos++] = PassByte;
    g_pwd[g_pwd_pos] = 0; // keep zero-terminated
  }
}

__declspec(naked) void GetPassword(void) {
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

const DWORD SETID_HOOK_ADDR = 0x00772BA3;
const DWORD SETID_RETN_ADDR = 0x00772BAD;

// 把攔截到的帳密資料回填至遊戲原流程。
__declspec(naked) void SetIdPass(void) {
  __asm
  {
		mov g_pwd_pos, 0
		lea eax, g_pwd
		push eax
		lea eax, g_id
		push eax
		jmp SETID_RETN_ADDR
  }
}

const BYTE path_code[] = {0x60, 0x6A, 0x00, 0x68, 0xC8, 0xAB, 0x9A, 0x00,
                          0x68, 0x48, 0xAC, 0x9A, 0x00, 0x6A, 0x06, 0x68,
                          0xD2, 0x00, 0x00, 0x00, 0x68, 0x14, 0x15, 0x8D,
                          0x00, 0xE8, 0x92, 0xE2, 0xE0, 0xFF, 0x83, 0xC4,
                          0x18, 0x61, 0xC3, 0x90, 0x90};

__declspec(naked) void GetFileData(void) {
  __asm {
		mov eax, buffer_len
		mov dword ptr ss:[ebp - 0x14], eax
		mov eax, buffer
		mov edx, dword ptr ss:[ebp - 0x23C]
		mov dword ptr ds:[edx + 0x08], eax
		mov eax, 0x0058794F
		jmp eax
  }
}
// =============================================================================
// API Hook 區（Network, Window, Time, Credential）
// =============================================================================
int(WINAPI *real_connect)(SOCKET s, const struct sockaddr *name,
                          int namelen) = connect;
int(WINAPI *real_send)(SOCKET s, const char *buf, int len, int flag) = send;
int(WINAPI *real_recv)(SOCKET s, char *buf, int len, int flag) = recv;

static int WINAPI my_connect(SOCKET s, const struct sockaddr *name, int namelen) {
  if (name == NULL || namelen < (int)sizeof(sockaddr_in))
    return real_connect(s, name, namelen);
  VMProtectBegin;
  const sockaddr_in *sa = (const sockaddr_in *)name;
  launcherdll_net_log(
      "[connect] original dst=%u.%u.%u.%u:%d", sa->sin_addr.S_un.S_un_b.s_b1,
      sa->sin_addr.S_un.S_un_b.s_b2, sa->sin_addr.S_un.S_un_b.s_b3,
      sa->sin_addr.S_un.S_un_b.s_b4, ntohs(sa->sin_port));
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
    VMProtectEnd;
    inited = false;
    g_editCount = 0; // 重新連線時重置編輯框計數器
    return real_connect(s, (const sockaddr *)&mappedAddr, sizeof(mappedAddr));
  }
  VMProtectEnd;
  inited = false;
  g_editCount = 0; // 重新連線時重置編輯框計數器
  return real_connect(s, name, namelen);
}

static int my_send(SOCKET s, const char *buf, int len, int flag) {
  if (buf == NULL || len <= 0)
    return real_send(s, buf, len, flag);
  BYTE stackBuffer[4096];
  BYTE *buffer_ptr = stackBuffer;
  bool useHeap = false;
  if (len > (int)sizeof(stackBuffer)) {
    buffer_ptr = new BYTE[len];
    useHeap = true;
  }
  memcpy(buffer_ptr, buf, len);
  if (len > 0) {
    char preview[64] = {0};
    char hex[128] = {0};
    bytes_to_ascii_preview((const BYTE *)buffer_ptr, len, preview,
                           sizeof(preview), 32);
    bytes_to_hex_preview((const BYTE *)buffer_ptr, len, hex, sizeof(hex), 32);
    launcherdll_net_log(
        "[my_send] socket=%u len=%u opcode=0x%02X msg=[%s] hex=[%s]",
        (unsigned)s, (unsigned)len, (unsigned)buffer_ptr[0], preview, hex);
  }
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
  return ret;
}

static int my_recv(SOCKET s, char *buf, int len, int flag) {
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
    _xorByte = (unsigned char)modpow(*(unsigned long *)buffer, _rsaD, _rsaN);
    inited = true;
  }
  int ret = real_recv(s, buf, len, flag);
  if (ret > 0) {
    unsigned char opcode = (unsigned char)buf[0];
    char hex[512] = {0};
    char ascii[128] = {0};
    bytes_to_hex_preview((const BYTE *)buf, ret, hex, sizeof(hex), 64);
    bytes_to_ascii_preview((const BYTE *)buf, ret, ascii, sizeof(ascii), 32);
    if (opcode == 0x0A && ret > 10) {
      launcherdll_net_log(
          "[my_recv] *** SERVER MSG 0x0A (len=%d) hex=[%s] ascii=[%s]", ret,
          hex, ascii);
    } else {
      launcherdll_net_log(
          "[my_recv] socket=%u ret=%d opcode=0x%02X inited=%d encrypt=%d",
          (unsigned)s, ret, (unsigned)opcode, (int)inited,
          (int)ShareInfo.encrypt);
    }
  }
  return ret;
}

// =============================================================================
// Window Hook（視窗建立 / 標題隨機化 / MessageBox）
// =============================================================================
HWND(WINAPI *real_CreateWindowEx)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int,
                                  int, HWND, HMENU, HINSTANCE,
                                  LPVOID) = CreateWindowExA;
HWND(WINAPI *real_CreateWindowExW)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int,
                                   int, int, HWND, HMENU, HINSTANCE,
                                   LPVOID) = CreateWindowExW;
static HWND WINAPI my_CreateWindowEx(DWORD dwExStyle, LPCSTR lpClassName,
                              LPCSTR lpWindowName, DWORD dwStyle, int x, int y,
                              int nWidth, int nHeight, HWND hWndParent,
                              HMENU hMenu, HINSTANCE hInstance,
                              LPVOID lpParam) {
  HWND hWndRet;
  bool bCreate = false;
  launcherdll_net_log(
      "[CreateWindowExA] class='%s' title='%s'",
      (lpClassName && HIWORD(lpClassName) != 0) ? lpClassName : "(atom)",
      (lpWindowName && HIWORD(lpWindowName) != 0) ? lpWindowName : "(null)");
  if (lpClassName && HIWORD(lpClassName) != 0 &&
      _stricmp(lpClassName, "Lineage") == 0) {
    if (!g_hooked) {
      g_hooked = true;
      launcherdll_net_log(
          "[Hook] CreateWindowEx triggered, installing patches");
      // 恢復原始 LinProject3.8 的補丁：0x859001B0 (強制遊戲核心使用純文字連線)
      DWORD forceDisableEnc = 0x859001B0;
      PatchCode((void *)0x00722761, &forceDisableEnc, sizeof(DWORD));
      // 恢復原始 LinProject3.8 的補丁與全掛鉤功能
      PatchCode((void *)0x00772BA0, (void *)path_code, sizeof(path_code));

      HookCode((void *)USER_HOOK_ADDR, (void *)GetUsername,
               USER_RETN_ADDR - USER_HOOK_ADDR);
      HookCode((void *)PASS_HOOK_ADDR, (void *)GetPassword,
               PASS_RETN_ADDR - PASS_HOOK_ADDR);
      HookCode((void *)SETID_HOOK_ADDR, (void *)SetIdPass,
               SETID_RETN_ADDR - SETID_HOOK_ADDR);

      if (buffer != NULL) {
        HookCode((void *)0x0058788B, (void *)GetFileData, 5);
      }
      
      launcherdll_net_log("[Hook] Core Pak loading (GetFileData) RESTORED.");
      
      launcherdll_net_log("[Hook] ALL hooks and patches RESTORED for full functionality.");

      // --- Helper 輔助對話框安裝 ---
      if (!h_hook) {
        h_hook = SetWindowsHookEx(WH_GETMESSAGE, (HOOKPROC)HookProc, hins,
                                  GetCurrentThreadId());
      }
    }
    srand(GetTickCount());
    char randomStr[16]{};
    for (int i = 0; i < 8; i++) {
      if (i < 4)
        randomStr[i] = 'A' + (rand() % 26);
      else
        randomStr[i] = '0' + (rand() % 10);
    }
    randomStr[8] = '\0';
    sprintf_s(szTitle, "%s", randomStr);
    lpWindowName = szTitle;
    bCreate = true;
  }
  hWndRet = real_CreateWindowEx(dwExStyle, lpClassName, lpWindowName, dwStyle,
                                x, y, nWidth, nHeight, hWndParent, hMenu,
                                hInstance, lpParam);
  if (bCreate && hWndRet != NULL) {
    g_hGameWnd = hWndRet;
  }

  // --- 穩定版帳密自動回填：訊息回填法 (ANSI) ---
  if (hWndRet != NULL && lpClassName != NULL && HIWORD(lpClassName) != 0 && _stricmp(lpClassName, "LUnicodeEdit") == 0) {
      g_editCount++;
      if (g_editCount == 1) { // 帳號
          SendMessageA(hWndRet, WM_SETTEXT, 0, (LPARAM)g_id);
          launcherdll_net_log("[AutoFill-A] Username filled into HWND=%p", hWndRet);
      } else if (g_editCount == 2) { // 密碼
          SendMessageA(hWndRet, WM_SETTEXT, 0, (LPARAM)g_pwd);
          launcherdll_net_log("[AutoFill-A] Password filled into HWND=%p", hWndRet);
      }
  }

  return hWndRet;
}

static HWND WINAPI my_CreateWindowExW(DWORD dwExStyle, LPCWSTR lpClassName,
                               LPCWSTR lpWindowName, DWORD dwStyle, int x,
                               int y, int nWidth, int nHeight, HWND hWndParent,
                               HMENU hMenu, HINSTANCE hInstance,
                               LPVOID lpParam) {
  if (lpClassName && HIWORD(lpClassName) != 0 &&
      _wcsicmp(lpClassName, L"Lineage") == 0) {
    // (W) 寬字元版本處理邏輯與 ANSI 版本類似，以 A 版為主
    if (!g_hooked) {
      // 同 my_CreateWindowEx 流程，僅安裝鍵盤 Hook
      g_hooked = true;
      if (!h_hook)
        h_hook = SetWindowsHookEx(WH_GETMESSAGE, (HOOKPROC)HookProc, hins,
                                  GetCurrentThreadId());
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
  }
  HWND hWnd = real_CreateWindowExW(dwExStyle, lpClassName, lpWindowName,
                                   dwStyle, x, y, nWidth, nHeight, hWndParent,
                                   hMenu, hInstance, lpParam);
  // --- 穩定版帳密自動回填：訊息回填法 (Wide) ---
  if (hWnd != NULL && lpClassName != NULL && HIWORD(lpClassName) != 0 && _wcsicmp(lpClassName, L"LUnicodeEdit") == 0) {
      g_editCount++;
      if (g_editCount == 1) { // 帳號
          SendMessageW(hWnd, WM_SETTEXT, 0, (LPARAM)ShareInfo.Account); // 這裡 ShareInfo 內是 Unicode? 視情況調整
          // 如果 g_id 是 ANSI，應使用 SendMessageA
          SendMessageA(hWnd, WM_SETTEXT, 0, (LPARAM)g_id);
          launcherdll_net_log("[AutoFill-W] Username filled into HWND=%p", hWnd);
      } else if (g_editCount == 2) { // 密碼
          SendMessageA(hWnd, WM_SETTEXT, 0, (LPARAM)g_pwd);
          launcherdll_net_log("[AutoFill-W] Password filled into HWND=%p", hWnd);
      }
  }

  return hWnd;
}

// MessageBox
int(WINAPI *real_MessageBoxA)(HWND, LPCSTR, LPCSTR, UINT) = MessageBoxA;
int(WINAPI *real_MessageBoxW)(HWND, LPCWSTR, LPCWSTR, UINT) = MessageBoxW;
static int WINAPI my_MessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption,
                          UINT uType) {
  launcherdll_net_log("[MessageBoxA] Caption='%s', Text='%s'", 
      lpCaption ? lpCaption : "(null)", 
      lpText ? lpText : "(null)");
  return real_MessageBoxA(hWnd, lpText, lpCaption, uType);
}

static int WINAPI my_MessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption,
                          UINT uType) {
  // 注意：日誌使用 %ls 來處理寬字元
  launcherdll_net_log("[MessageBoxW] Caption='%ls', Text='%ls'", 
      lpCaption ? lpCaption : L"(null)", 
      lpText ? lpText : L"(null)");
  return real_MessageBoxW(hWnd, lpText, lpCaption, uType);
}

// 設定檔解密 / 檔案讀取（純 XOR 還原 + 虛擬編譯模式）
// =============================================================================

/* [暫時停用] ConvertTxtToBinary 函式實體
BYTE* ConvertTxtToBinary(BYTE* txtData, unsigned int txtLen, DWORD& outLen) {
    ... (內容已註解)
    return NULL;
}
*/

// -----------------------------------------------------------------------------
// NakedLoaderHook (0x58228A)
// 目的：修正遊戲引擎在處理 Action 4 (走路) 時的索引計算偏移。
//       原始引擎可能只預留了 4 個 Action 的空間，以此 Hook 強制平衡堆疊與暫存器。
// -----------------------------------------------------------------------------
/* [暫時停用] NakedLoaderHook 函式實體
void __declspec(naked) NakedLoaderHook() {
    __asm {
        // 原始碼大約是：mov eax, [esi+ebx*4+0Ch]
        // 我們在這裡確保 ebx (ActionIndex) 如果是 4，能正確對應到我們虛擬編譯出的 20 bytes 空間
        mov eax, [esp + 0x10] // 取得目前的處理物件
        push ecx
        mov ecx, [eax + 0x08] // 檢查 ActionCount
        pop ecx
        
        // 恢復原始指令並跳回
        mov eax, [edi + 0x438] 
        push 0x00582290 // 跳回原始指令下一行
        ret
    }
}
*/

static BYTE *GetFileBuffer() {
  FILE *fp = NULL;
  unsigned int len = 0;
  buffer_len = 0;
  launcherdll_net_log("[GetFileBuffer] bdfile='%S'", ShareInfo.bdfile);

  if (_wfopen_s(&fp, ShareInfo.bdfile, L"rb") == 0 && fp != NULL) {
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    launcherdll_net_log("[GetFileBuffer] file opened, len=%u", len);

    if (len < 4) { fclose(fp); return NULL; }

    BYTE *file_data = new BYTE[len];
    fread(file_data, 1, len, fp);
    fclose(fp);

    VMProtectBegin;

    const char *fixedKey = "PAt82IqEvNBmERYl";
    launcherdll_net_log("[GetFileBuffer] Decrypting %u bytes with fixed XOR key...", len);
    for (unsigned int i = 0; i < len; i++) {
      file_data[i] ^= (BYTE)fixedKey[i % 16];
    }

    /* [暫時停用轉譯] 修正 C3861 錯誤：目前不使用 ConvertTxtToBinary，回歸純 XOR 模式
    if (len > 4 && (file_data[0] == '#' || (file_data[0] == '2' && file_data[1] == '0'))) {
        launcherdll_net_log("[GetFileBuffer] Detected Text Configuration. Invoking Virtual Packer...");
        DWORD binLen = 0;
        BYTE* binData = ConvertTxtToBinary(file_data, len, binLen);
        delete[] file_data;
        buffer_len = binLen;
        VMProtectEnd;
        return binData;
    }
    */

    buffer_len = len;
    VMProtectEnd;
    return file_data;
  }
  return NULL;
}

// =============================================================================
// PatchThread：遊戲記憶體補丁執行緒
// 目的：將地址 0x004E204E 的條件跳轉指令 JNZ (0F 85) 改為無條件跳轉 JMP (90 E9)
//       藉此繞過遊戲內部的某項檢查邏輯（例如版本驗證或功能限制）
//
// 時序：由 DelayedDetourThread 在保護殼解密完成後啟動
//       此時遊戲程式碼已還原為原始指令，可以安全地讀取與修改
//
// x86 指令對照：
//   修改前 (記憶體中的位元組): 0F 85 97 00 → JNZ rel32（條件跳轉：ZF=0 才跳）
//   修改後 (記憶體中的位元組): 90 E9 97 00 → NOP + JMP
//   rel32（無條件跳轉：永遠跳） 效果：原本有條件才執行的分支，變成永遠執行
// =============================================================================
static DWORD WINAPI PatchThread(void *p) {
  __try {
    while (true) {
      if (*(DWORD *)0x004E204E == 0x0097850F) {
        launcherdll_net_log("[Patch] 核心解密完成，開始執行記憶體補丁程序... ");
        launcherdll_net_log("[Patch] 目前基準位址: 0x%p ", (void*)L1Offsets::BASE_ADDRESS);

        DWORD kernelPatch = 0x0097E990;
        PatchCode((void *)0x004E204E, &kernelPatch, sizeof(DWORD));
        launcherdll_net_log("[Patch] 1. 核心診斷補丁已套用 @0x004E204E ");

        BYTE expandPattern[] = { 0x83, 0xF8, 0x12 };
        
        uintptr_t addrA = L1Offsets::BASE_ADDRESS + L1Offsets::PatchTargets::UI_SLOT_LIMIT_CHECK_1;
        PatchCode((void *)addrA, expandPattern, sizeof(expandPattern));
        launcherdll_net_log("[Patch] 2A. UI 渲染邊界補丁 1 套用成功 @0x%p ", (void*)addrA);

        uintptr_t addrB = L1Offsets::BASE_ADDRESS + L1Offsets::PatchTargets::UI_SLOT_LIMIT_CHECK_2;
        PatchCode((void *)addrB, expandPattern, sizeof(expandPattern));
        launcherdll_net_log("[Patch] 2B. UI 渲染邊界補丁 2 套用成功 @0x%p ", (void*)addrB);

        uintptr_t addrC = L1Offsets::BASE_ADDRESS + L1Offsets::PatchTargets::SLOT_SPECIAL_HANDLING_1;
        PatchCode((void *)addrC, expandPattern, sizeof(expandPattern));
        launcherdll_net_log("[Patch] 2C. 特殊 Slot 邏輯補丁 1 套用成功 @0x%p ", (void*)addrC);

        uintptr_t addrD = L1Offsets::BASE_ADDRESS + L1Offsets::PatchTargets::SLOT_SPECIAL_HANDLING_2;
        PatchCode((void *)addrD, expandPattern, sizeof(expandPattern));
        launcherdll_net_log("[Patch] 2D. 特殊 Slot 邏輯補丁 2 套用成功 @0x%p ", (void*)addrD);

        launcherdll_net_log("[Patch] 裝備欄位擴充完成 (目前支援至 18 欄位)。 ");
        break;
      }
      Sleep(1);
    }
  } __except (1) {
    launcherdll_net_log("[Patch] *** CRITICAL *** 補丁執行例外。 ");
  }
  return 0;
}

// 全域：保存真實系統時間，解殼後恢復
static SYSTEMTIME g_realLocalTime;
static bool g_timeChanged = false;

// 暫時更改系統時間（騙 Themida 授權）；不修改任何 API 入口程式碼
static bool SetFakeSystemTime() {
  GetLocalTime(&g_realLocalTime); // 此時尚未 hook，取得真實時間
  // 提升 SE_SYSTEMTIME_NAME 權限
  HANDLE hToken = NULL;
  if (OpenProcessToken(GetCurrentProcess(),
                       TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
    TOKEN_PRIVILEGES tp = {};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    LookupPrivilegeValue(NULL, SE_SYSTEMTIME_NAME, &tp.Privileges[0].Luid);
    AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
    CloseHandle(hToken);
  }
  SYSTEMTIME fakeTime = g_realLocalTime;
  fakeTime.wYear = 2013;
  fakeTime.wMonth = 8;
  fakeTime.wDay = 1;
  g_timeChanged = (SetLocalTime(&fakeTime) != 0);
  return g_timeChanged;
}

static void RestoreSystemTime() {
  if (g_timeChanged) {
    SetLocalTime(&g_realLocalTime);
    g_timeChanged = false;
  }
}

// 延遲安裝 Detours 的執行緒：等保護殼解密完成後才安裝所有 hook
static DWORD WINAPI DelayedDetourThread(void *p) {
  launcherdll_net_log("[DelayedDetour] waiting for code decryption...");
  int waitCount = 0;
  DWORD lastVal = 0xFFFFFFFF;
  while (!IsCodeDecrypt() && waitCount < 12000) {
    Sleep(10);
    waitCount++;
    if (waitCount % 500 == 0) {
      DWORD curVal = 0xDEADDEAD;
      __try {
        curVal = *(volatile DWORD *)0x0058788B;
      } __except (1) {
        curVal = 0xDEADDEAD;
      }
      MEMORY_BASIC_INFORMATION mbi = {};
      VirtualQuery((PVOID)0x0058788B, &mbi, sizeof(mbi));
      launcherdll_net_log(
          "[DelayedDetour] poll %ds: code@58788B=0x%08X %s (protect=0x%X)",
          waitCount / 100, curVal,
          (curVal != lastVal) ? "(CHANGED!)" : "(unchanged)",
          (unsigned)mbi.Protect);
      lastVal = curVal;
    }
  }
  if (!IsCodeDecrypt()) {
    DWORD finalVal = 0xDEADDEAD;
    __try {
      finalVal = *(volatile DWORD *)0x0058788B;
    } __except (1) {
    }
    launcherdll_net_log(
        "[DelayedDetour] TIMEOUT 120s, code@58788B=0x%08X (expect 0x4D8D016A)",
        finalVal);
    RestoreSystemTime();
    launcherdll_net_log("[DelayedDetour] system time restored (timeout path)");
    return 1;
  }
  launcherdll_net_log(
      "[DelayedDetour] code decrypted (waited %d ms), installing ALL hooks...",
      waitCount * 10);

  // 解殼完成：安裝所有 Detours hook（時間 + API）
  SetupTimeController();
  DetourRestoreAfterWith();
  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  // 時間 hook
  DetourAttach(&(PVOID &)real_GetLocalTime,
               reinterpret_cast<PVOID>(my_GetLocalTime));
  DetourAttach(&(PVOID &)real_GetSystemTime,
               reinterpret_cast<PVOID>(my_GetSystemTime));
  DetourAttach(&(PVOID &)real_GetSystemTimeAsFileTime,
               reinterpret_cast<PVOID>(my_GetSystemTimeAsFileTime));
  DetourAttach(&(PVOID &)real_GetTickCount,
               reinterpret_cast<PVOID>(my_GetTickCount));
  if (real_timeGetTime) {
    DetourAttach(&(PVOID &)real_timeGetTime,
                 reinterpret_cast<PVOID>(my_timeGetTime));
  }
  // API hook
  DetourAttach(&(PVOID &)real_connect, reinterpret_cast<PVOID>(my_connect));
  DetourAttach(&(PVOID &)real_send, reinterpret_cast<PVOID>(my_send));
  DetourAttach(&(PVOID &)real_recv, reinterpret_cast<PVOID>(my_recv));
  DetourAttach(&(PVOID &)real_CreateWindowEx,
               reinterpret_cast<PVOID>(my_CreateWindowEx));
  DetourAttach(&(PVOID &)real_CreateWindowExW,
               reinterpret_cast<PVOID>(my_CreateWindowExW));
  DetourAttach(&(PVOID &)real_MessageBoxA,
               reinterpret_cast<PVOID>(my_MessageBoxA));
  DetourAttach(&(PVOID &)real_MessageBoxW,
               reinterpret_cast<PVOID>(my_MessageBoxW));
  LONG detourResult = DetourTransactionCommit();
  launcherdll_net_log("[DelayedDetour] DetourTransactionCommit result=%ld",
                      detourResult);

  // 恢復系統時間（時間 hook 已接管，遊戲繼續看到 2013）
  RestoreSystemTime();
  launcherdll_net_log("[DelayedDetour] system time restored");

  // [恢復] 啟動 PatchThread 對遊戲核心進行診斷性補丁
  CloseHandle(CreateThread(NULL, 0, PatchThread, NULL, 0, NULL));
  // [暫停中] 實驗性 Action 4 偏移修正 Hook，根據要求暫不啟動
  // HookCode((void *)0x58228A, (void *)NakedLoaderHook, 6);

  LoadCombatConfig();
  launcherdll_net_log("[DelayedDetour] all hooks installed successfully");
  return 0;
}

// init: DLL 初始化進入點，供外部 Launcher 呼叫 (不可設為 static)
void init() {
  VMProtectBegin;
  launcherdll_net_log("[init] DLL init started, PID=%u",
                      (unsigned)GetCurrentProcessId());
  SHARE_INFO *pShareInfo = get_shm(GetCurrentProcessId(), false);
  if (pShareInfo == NULL) {
    launcherdll_net_log("[init] get_shm failed, calling ExitProcess");
    ExitProcess(0);
    return;
  }
  launcherdll_net_log("[init] get_shm OK, pShareInfo=%p, waiting for magic...",
                      pShareInfo);
  int timeout = 0;
  while (*(volatile DWORD *)&pShareInfo->magic != 0x12345678 && timeout < 50) {
    Sleep(100);
    timeout++;
  }
  launcherdll_net_log("[init] magic wait done, timeout=%d, magic=0x%08X",
                      timeout, (unsigned)pShareInfo->magic);
  memcpy(&ShareInfo, pShareInfo, sizeof(SHARE_INFO));
  pShareInfo->read = true;

  // 安全拷貝帳密並確保 null-terminated
  memset(g_id, 0, 32);
  memset(g_pwd, 0, 32);
  memcpy(g_id, ShareInfo.Account, 32);
  memcpy(g_pwd, ShareInfo.Password, 32);
  g_id[31] = '\0';
  g_pwd[31] = '\0';
  g_pwd_pos = (int)strlen((const char*)g_pwd);

  launcherdll_net_log("[init] ShareInfo size=%u (expected 197)", (unsigned)sizeof(SHARE_INFO));
  
  // 強制關閉加密 (User Requested: No Encryption)
  ShareInfo.encrypt = 0;
  ShareInfo.randenc = 0;
  
  launcherdll_net_log("[init] ShareInfo copied: ip=%.31s, port=%d, encrypt=%d (FORCED OFF)",
                      (const char *)ShareInfo.ip, ShareInfo.port, (int)ShareInfo.encrypt);

  if (0) { // 強制跳過 RSA 初始化
    unsigned long rsaD = pShareInfo->RSA_D ^ SERVER_LIST_RSA_XOR_D;
    unsigned long rsaN = pShareInfo->RSA_N ^ SERVER_LIST_RSA_XOR_N;
    _rsaD = (DWORD)rsaD;
    _rsaN = (DWORD)rsaN;
    launcherdll_net_log("[init] RSA keys loaded: N=0x%08X, D=0x%08X", _rsaN,
                        _rsaD);
  }
  free_shm();
  launcherdll_net_log("[init] free_shm done");
  if (ShareInfo.usebd) {
    launcherdll_net_log("[init] Loading BD file...");
    buffer = GetFileBuffer();
    launcherdll_net_log("[init] GetFileBuffer result: buffer=%p, len=%u",
                        buffer, (unsigned)buffer_len);
  }
  encdec_init_key(ShareInfo.key);
  launcherdll_net_log("[init] encdec_init_key done");

  // 暫時更改系統時間為 2013/8/1（騙過 Themida 授權時間檢查）
  // 不使用 Detours hook，避免 Themida 偵測 API 入口被改寫而拒絕解殼
  bool timeOk = SetFakeSystemTime();
  launcherdll_net_log(
      "[init] SetFakeSystemTime result=%d (1=OK, 0=FAIL needs admin)",
      (int)timeOk);

  // 所有 hook（時間+API）延遲到保護殼解密後安裝
  CreateThread(NULL, 0, DelayedDetourThread, NULL, 0, NULL);

  launcherdll_net_log("[init] init completed, DelayedDetourThread started");
  // 通知 Launcher init() 已完成，可以 ResumeThread
  if (g_hInitEvent) {
    SetEvent(g_hInitEvent);
    CloseHandle(g_hInitEvent);
    g_hInitEvent = NULL;
  }
  VMProtectEnd;
}

// __fn1: 外部 Hook 安裝進入點 (不可設為 static)
bool __stdcall __fn1(DWORD tid) {
  VMProtectBegin;
  h_hook = SetWindowsHookEx(WH_GETMESSAGE, HookProc, hins, tid);
  VMProtectEnd;
  return h_hook != NULL;
}

// DLLGetVersion: 提供 Launcher 辨識版本 (不可設為 static)
int __stdcall DLLGetVersion() { return 0x1002; }

// DLLGetInformation: 提供 Launcher 辨識 DLL 資訊 (不可設為 static)
const char *__stdcall DLLGetInformation() { return "LauncherDll"; }
