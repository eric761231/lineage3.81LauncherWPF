#include "stdafx.h"
#include "LauncherDll.h"
#include "L1Offsets.h"

#include "VMProtectSDK.h"
#include <string>
#include <sstream>
#include <vector>
#include <cstdlib>

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
BYTE g_id[32];
BYTE g_pwd[32];
int g_pwd_pos = 0;
int _seed = 0;
int _xorByte = 0;

// RSA 金鑰（由共享記憶體讀入，類型均在 DWORD 範圍內）
static DWORD _rsaD = 0;
static DWORD _rsaN = 0;
// 小數模冪：計算 base^exp mod modulus（適用於 authdata ^ D mod N，皆為 DWORD）
// 參數避免使用名稱 mod，以免與 Windows/CRT 巨集衝突。
static DWORD modpow(unsigned long baseVal, unsigned long exp,
                    unsigned long modulus) {
  if (modulus == 0)
    return 0;
  DWORDLONG result = 1;
  DWORDLONG b = baseVal % modulus;
  while (exp > 0) {
    if (exp & 1)
      result = result * b % modulus;
    b = b * b % modulus;
    exp >>= 1;
  }
  return (DWORD)result;
}

bool inited = false;

// 密米爾之泉功能總開關：懷疑登入斷線跟這次新增的東西有關，先整體關閉排查，
// 排除後再改回 1。設 0 時，以下所有 Mimir 相關程式碼都不會被編譯進執行路徑
// （不是只在 runtime 判斷跳過，是編譯期就整段拿掉），盡量貼近改動前的行為。
#define MIMIR_FEATURE_ENABLED 0

// 目前遊戲行程實際在用的連線 socket，供 SendCustomPacket 主動送出自訂封包時使用
// （my_connect 每次連線都會更新，button click 等 UI 事件觸發時不是在 hook 呼叫鏈裡，
// 沒有現成的 socket 可用，所以另外存一份）。
static SOCKET g_gameSocket = INVALID_SOCKET;

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
  // 統一寫進跟 WPF 端 LogService.cs 同一個 launcher.log（同一層目錄），
  // 不要另外開一個 launcherdll_net.log。
  char logPath[MAX_PATH] = "./launcher.log";
  if (GetModuleFileNameA(NULL, exePath, MAX_PATH) > 0) {
    for (int i = (int)strlen(exePath) - 1; i >= 0; i--) {
      if (exePath[i] == '\\' || exePath[i] == '/') {
        exePath[i] = '\0';
        break;
      }
    }
    // 對齊 C# 端 GamePathHelper.GetGameRootDirectory()：如果遊戲主程式所在的
    // 資料夾本身叫 LinLauncher_Environment，實際的 launcher.log 要寫在它的
    // 上一層（遊戲根目錄），這樣兩邊才會真的寫進同一個檔案，不會各寫各的。
    const char *leaf = exePath;
    for (const char *p = exePath; *p; p++) {
      if (*p == '\\' || *p == '/')
        leaf = p + 1;
    }
    if (_stricmp(leaf, "LinLauncher_Environment") == 0) {
      for (int i = (int)strlen(exePath) - 1; i >= 0; i--) {
        if (exePath[i] == '\\' || exePath[i] == '/') {
          exePath[i] = '\0';
          break;
        }
      }
    }
    sprintf_s(logPath, "%s\\launcher.log", exePath);
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

// -----------------------------------------------------------------------------
// 遊戲主程式透過 MessageBox 顯示的錯誤／提示：另寫專用檔，避免與封包日誌混在一起、
// 或視窗被擋、一瞬間關閉時無法對照。（仍會同時寫入 launcherdll_net.log）
// 檔案路徑：<遊戲主程式目錄>\GameClient_MessageBox.log
// -----------------------------------------------------------------------------
static void game_client_messagebox_log_file_w(LPCWSTR caption, LPCWSTR text) {
  wchar_t dir[MAX_PATH] = {0};
  if (GetModuleFileNameW(NULL, dir, MAX_PATH) <= 0)
    return;
  for (int i = (int)wcslen(dir) - 1; i >= 0; i--) {
    if (dir[i] == L'\\' || dir[i] == L'/') {
      dir[i] = L'\0';
      break;
    }
  }
  wchar_t logPath[MAX_PATH];
  swprintf_s(logPath, L"%s\\GameClient_MessageBox.log", dir);

  FILE *fp = nullptr;
  if (_wfopen_s(&fp, logPath, L"a, ccs=UTF-8") != 0 || fp == nullptr) {
    if (_wfopen_s(&fp, logPath, L"a") != 0 || fp == nullptr)
      return;
  }

  SYSTEMTIME st;
  GetLocalTime(&st);
  fwprintf(fp,
           L"[%04u-%02u-%02u %02u:%02u:%02u.%03u][PID=%u][TID=%u][Game MessageBox]\r\n"
           L"  Caption: %s\r\n"
           L"  Text: %s\r\n"
           L"--------------------------------------------------------------------------------\r\n",
           st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
           st.wMilliseconds, (unsigned)GetCurrentProcessId(),
           (unsigned)GetCurrentThreadId(),
           caption ? caption : L"(null)",
           text ? text : L"(null)");
  fflush(fp);
  fclose(fp);
}

static void game_client_messagebox_log_file_a(LPCSTR caption, LPCSTR text) {
  wchar_t wcap[512] = {0};
  wchar_t wtxt[8192] = {0};
  if (caption) {
    MultiByteToWideChar(CP_ACP, 0, caption, -1, wcap, 511);
  } else {
    wcscpy_s(wcap, L"(null)");
  }
  if (text) {
    MultiByteToWideChar(CP_ACP, 0, text, -1, wtxt, 8191);
  } else {
    wcscpy_s(wtxt, L"(null)");
  }
  game_client_messagebox_log_file_w(wcap, wtxt);
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

static bool should_log_packet_opcode(unsigned opcode) {
  switch (opcode) {
  case 0x95:
  case 0x56:
  case 0x0F:
  case 0x33:
  case 0xA1:
  case 0xC1:
  case 0xA7:
  case 0x83:
  case 0xEE:
    return true;
  default:
    return false;
  }
}

static unsigned extract_packet_opcode(const BYTE *data, int len) {
  if (data == NULL || len < 3)
    return 0xFFFFFFFFu;
  return (unsigned)data[2];
}

static void log_slot_trace(const char *direction, SOCKET s, unsigned opcode,
                           const BYTE *packet, int packetLen) {
  if (direction == NULL || packet == NULL || packetLen <= 2)
    return;
  const BYTE *payload = packet + 2;
  int payloadLen = packetLen - 2;
  if (payloadLen <= 0)
    return;

  unsigned b0 = (payloadLen >= 1) ? (unsigned)payload[0] : 0xFF;
  unsigned b1 = (payloadLen >= 2) ? (unsigned)payload[1] : 0xFF;
  unsigned b2 = (payloadLen >= 3) ? (unsigned)payload[2] : 0xFF;
  unsigned w01 = (payloadLen >= 2)
                     ? ((unsigned)payload[0] | ((unsigned)payload[1] << 8))
                     : 0xFFFF;
  unsigned w12 = (payloadLen >= 3)
                     ? ((unsigned)payload[1] | ((unsigned)payload[2] << 8))
                     : 0xFFFF;

  char payloadHex[128] = {0};
  bytes_to_hex_preview(payload, payloadLen, payloadHex, sizeof(payloadHex), 16);
  launcherdll_net_log(
      "[slot-trace][%s] socket=%u op=0x%02X b0=0x%02X b1=0x%02X b2=0x%02X "
      "w01=0x%04X w12=0x%04X payloadHex=[%s]",
      direction, (unsigned)s, opcode, b0, b1, b2, w01, w12, payloadHex);
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
  return CallNextHookEx(h_hook, nCode, wParam, lParam);
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
  // 退格／刪除：否則只會一直 append，畫面上刪除也無法同步到 g_pwd，登入仍用舊密碼
  if (PassByte == '\b' || PassByte == 0x7F) {
    if (g_pwd_pos > 0) {
      g_pwd_pos--;
      g_pwd[g_pwd_pos] = 0;
    }
    return;
  }
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
#if MIMIR_FEATURE_ENABLED
  g_gameSocket = s;
#endif
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
    return real_connect(s, (const sockaddr *)&mappedAddr, sizeof(mappedAddr));
  }
  VMProtectEnd;
  inited = false;
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
  unsigned sendOpcode = extract_packet_opcode(buffer_ptr, len);
  bool allowSendLog = should_log_packet_opcode(sendOpcode);
  if (len > 0 && allowSendLog) {
    char preview[64] = {0};
    char hex[128] = {0};
    bytes_to_ascii_preview((const BYTE *)buffer_ptr, len, preview,
                           sizeof(preview), 24);
    bytes_to_hex_preview((const BYTE *)buffer_ptr, len, hex, sizeof(hex), 24);
    unsigned headLen = 0;
    unsigned pay0 = 0;
    if (len >= 2)
      headLen = (unsigned)buffer_ptr[0] | ((unsigned)buffer_ptr[1] << 8);
    if (len >= 3)
      pay0 = (unsigned)buffer_ptr[2];
    launcherdll_net_log(
        "[net: send-plain] socket=%u len=%u head_len=%u opcode=0x%02X "
        "pay0=0x%02X ascii=[%s] hex=[%s]",
        (unsigned)s, (unsigned)len, headLen, sendOpcode, pay0, preview, hex);
    log_slot_trace("send", s, sendOpcode, buffer_ptr, len);
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
  if (len > 0 && allowSendLog) {
    char cipherPreview[64] = {0};
    char cipherHex[128] = {0};
    bytes_to_ascii_preview((const BYTE *)buffer_ptr, len, cipherPreview,
                           sizeof(cipherPreview), 24);
    bytes_to_hex_preview((const BYTE *)buffer_ptr, len, cipherHex,
                         sizeof(cipherHex), 24);
    launcherdll_net_log(
        "[net: send-cipher] socket=%u len=%u opcode=0x%02X ascii=[%s] hex=[%s]",
        (unsigned)s, (unsigned)len, sendOpcode, cipherPreview, cipherHex);
  }
  int ret = real_send(s, (const char *)buffer_ptr, len, flag);
  if (useHeap)
    delete[] buffer_ptr;
  return ret;
}

// 送出自訂（非原生協定）C 封包，宣告見 LauncherDll.h。走 my_send 而非直接呼叫
// real_send，這樣才會套用跟其他封包一樣的加密，Java 端 DecryptExecutor 才能正常解密。
void SendCustomPacket(BYTE opcode, const BYTE *payload, int payloadLen) {
  if (g_gameSocket == INVALID_SOCKET) {
    launcherdll_net_log(
        "[SendCustomPacket] 尚未取得遊戲 socket，略過送出 opcode=0x%02X", opcode);
    return;
  }
  int totalLen = 2 + 1 + payloadLen;
  if (payloadLen < 0 || totalLen > 512) {
    launcherdll_net_log(
        "[SendCustomPacket] payload 長度異常 (%d bytes)，略過 opcode=0x%02X",
        payloadLen, opcode);
    return;
  }
  BYTE buf[512];
  buf[0] = (BYTE)(totalLen & 0xFF);
  buf[1] = (BYTE)((totalLen >> 8) & 0xFF);
  buf[2] = opcode;
  if (payloadLen > 0)
    memcpy(buf + 3, payload, payloadLen);
  launcherdll_net_log("[SendCustomPacket] socket=%u opcode=0x%02X len=%d",
                      (unsigned)g_gameSocket, opcode, totalLen);
  my_send(g_gameSocket, (const char *)buf, totalLen, 0);
}

// =============================================================================
// 自訂封包：祕米爾之泉（Mimir Power Set），取代原生 S_Html 版本。
// opcode 值須跟 Java 端 OpcodeServer.S_OPCODE_MIMIRPOWER /
// OpcodesClient.C_OPCODE_MIMIRPOWER 保持一致。
// =============================================================================
static const BYTE S_OPCODE_MIMIRPOWER = 189; // 0xBD
static const BYTE C_OPCODE_MIMIRPOWER = 195; // 0xC3（送出端見 MimirPowerDlg.cpp）

// 封包格式（跟原生協定一樣有 XOR 加密，見 TryHandleMimirPowerPacket 的解密處理）：
//   [len:2][opcode:1][objid:4][optionCount:1]
//   repeat optionCount: [index:1][desc: 以 0x00 結尾的字串][mimirId:4]
// desc 是 Java 端 L1MimirPowerSet.buildMimirDesc() 已經組好的完整顯示文字
// （例如「短距離傷害+5,魔法命中+2」），跟 writeS()/readS() 的 null-terminated
// 字串格式一致，不是固定長度前綴。
static void HandleMimirPowerPayload(const BYTE *p, int len) {
  if (len < 8)
    return;
  int pos = 3;
  DWORD objid;
  memcpy(&objid, p + pos, 4);
  pos += 4;
  BYTE count = p[pos++];
  int n = (count > 3) ? 3 : (int)count;

  MimirOption options[3] = {};
  for (int i = 0; i < n; i++) {
    if (pos + 1 > len)
      break;
    BYTE index = p[pos++];
    // 找 0x00 結尾（writeS() 的格式），不能超過封包剩餘長度
    int strStart = pos;
    while (pos < len && p[pos] != 0x00)
      pos++;
    if (pos >= len)
      break; // 沒找到結尾，封包不完整
    int strLen = pos - strStart;
    pos++; // 跳過 0x00
    if (pos + 4 > len)
      break;
    int copyLen = (strLen < (int)(sizeof(options[i].name) - 1))
                     ? strLen
                     : (int)(sizeof(options[i].name) - 1);
    memcpy(options[i].name, p + strStart, copyLen);
    options[i].name[copyLen] = '\0';
    options[i].index = index;
    memcpy(&options[i].value, p + pos, 4);
    pos += 4;
  }

  launcherdll_net_log(
      "[MimirPower] 收到選項 objid=%u: [0]%s(id=%u) [1]%s(id=%u) [2]%s(id=%u)",
      objid, options[0].name, options[0].value, options[1].name,
      options[1].value, options[2].name, options[2].value);
  ShowMimirPowerDialog(objid, options);
}

// 在原生 opcode 判斷/記錄之前，先檢查緩衝區最前面是不是我們自訂的 S_MimirPower
// 封包；是的話完整解密、處理、並把這段位元組從緩衝區移除，不轉發給原生 client
// 解析器（原生 client 不認得這個 opcode，一定不能讓它流過去，否則會解析錯位）。
// 只支援 ShareInfo.randenc==false 的靜態 XOR 模式；randenc（逐 byte LCG）模式
// 沒有對應的接收端解密狀態，遇到就放棄攔截，直接跳過不處理。
static void TryHandleMimirPowerPacket(char *buf, int &ret) {
  while (ret >= 3) {
    if (ShareInfo.encrypt && ShareInfo.randenc)
      return; // randenc 模式暫不支援攔截自訂封包
    BYTE peek2 =
        ShareInfo.encrypt ? ((BYTE)buf[2] ^ (BYTE)_xorByte) : (BYTE)buf[2];
    if (peek2 != S_OPCODE_MIMIRPOWER)
      return;
    BYTE peek0 =
        ShareInfo.encrypt ? ((BYTE)buf[0] ^ (BYTE)_xorByte) : (BYTE)buf[0];
    BYTE peek1 =
        ShareInfo.encrypt ? ((BYTE)buf[1] ^ (BYTE)_xorByte) : (BYTE)buf[1];
    unsigned headLen = (unsigned)peek0 | ((unsigned)peek1 << 8);
    if (headLen < 3 || (int)headLen > ret)
      return; // 長度異常，或封包還沒收完整，交給下次 recv 再處理

    BYTE decoded[700]; // desc 是變動長度字串，可能比固定欄位版本大，留足空間
    if (headLen > sizeof(decoded))
      return;
    for (unsigned i = 0; i < headLen; i++)
      decoded[i] =
          ShareInfo.encrypt ? ((BYTE)buf[i] ^ (BYTE)_xorByte) : (BYTE)buf[i];
    HandleMimirPowerPayload(decoded, (int)headLen);

    int remaining = ret - (int)headLen;
    if (remaining > 0)
      memmove(buf, buf + headLen, remaining);
    ret = remaining;
  }
}

// =============================================================================
// Action_SocialAction1 劫持：把行動視窗（ActionUI.xml）裡的「打招呼1」按鈕改成
// 觸發密米爾之泉視窗。使用者已明確確認接受這個取捨：SocialAction1 原本的問候
// 動作會停止播放，按鈕圖示/說明文字之後另外在 ActionUI.xml 換成密米爾之泉專用的，
// 不影響 SocialAction2~4。
//
// 反組譯確認的事實（TW1308190-3.DMP，函式起點 0x0062be10）：
// - client 啟動時的動作登記表是編譯期就決定大小的固定陣列，剛好 26 格，每格
//   0x20 bytes（offset 0 起 28 bytes 的 std::string 名稱 + offset 0x1c 的 4
//   bytes 處理函式指標），陣列位於 0xC2FA48~0xC2FD68，緊接在後面的 0xC2FD88
//   是「這段初始化只跑一次」的旗標（bit0）。沒有多餘空位可以新增一個全新動作
//   名稱，只能覆寫既有格子的處理函式指標。Action_SocialAction1 的槽位是
//   0xC2FC48，處理函式指標在 0xC2FC48+0x1c = 0xC2FC64。
// - 呼叫慣例已反組譯 Action_PotionHelper（0x62ec60）與 Action_SocialAction1
//   本身（0x62e7d0）兩個確認：處理函式不吃參數、不靠 ecx 傳 this，單純
//   void ()，直接被 `call [slot+0x1c]` 呼叫；我們自己的處理函式維持同樣的
//   簽名即可，不用擔心呼叫慣例不合造成堆疊錯位。
// =============================================================================
static const DWORD kActionSocialAction1HandlerAddr = 0xC2FC48 + 0x1c;
static const DWORD kActionArrayInitFlag = 0xC2FD88; // bit0=這個固定陣列是否已初始化完成

static void __cdecl MimirActionHandler() {
  launcherdll_net_log("[MimirAction] Action_SocialAction1 槽位被觸發，開啟密米爾之泉視窗");
  ShowMimirPowerDialogFromCache();
}

static DWORD WINAPI MimirActionPatchThread(void *p) {
  __try {
    int waitCount = 0;
    const int maxWaitMs = 120000;
    while (waitCount < maxWaitMs) {
      DWORD flag = 0;
      __try {
        flag = *(volatile DWORD *)kActionArrayInitFlag;
      } __except (1) {
        flag = 0;
      }
      if (flag & 1) {
        DWORD handlerAddr = (DWORD)&MimirActionHandler;
        PatchCode((void *)kActionSocialAction1HandlerAddr, &handlerAddr,
                  sizeof(DWORD));
        launcherdll_net_log(
            "[MimirAction] 已劫持 Action_SocialAction1 handler slot=0x%08X -> "
            "0x%08X（等待 %d ms）",
            kActionSocialAction1HandlerAddr, handlerAddr, waitCount);
        return 0;
      }
      Sleep(10);
      waitCount += 10;
    }
    launcherdll_net_log(
        "[MimirAction] TIMEOUT %dms，動作登記表初始化旗標(0x%08X)一直沒等到，"
        "放棄劫持",
        maxWaitMs, kActionArrayInitFlag);
  } __except (1) {
    launcherdll_net_log("[MimirAction] *** CRITICAL *** 劫持時發生例外");
  }
  return 1;
}

// 隔離測試開關：懷疑 TryHandleMimirPowerPacket 誤吃到真正封包的中段資料，
// 導致角色登入途中斷線（buf 不保證每次 recv 都切在封包邊界上，TCP 是位元組
// 流，可能收到上一個封包還沒收完的中段，這時 buf[2] 恰好等於 189 就會誤判）。
// 先關掉這段攔截確認是不是這裡的問題，之後要修就要改成自己維護一個跨多次
// recv 的重組緩衝區，不能只看單次 recv 的開頭 3 bytes。
static const bool ENABLE_MIMIR_PACKET_INTERCEPT = false;
// 隔離測試開關：懷疑 0xC2FD88 旗標只代表「client 開始跑這段初始化」而非
// 「26 筆全部註冊完成」，我們的補丁執行緒可能在 client 自己還沒建構
// Action_SocialAction1 那個 slot 之前就搶先寫入，導致跟 client 的初始化互相
// 搶寫同一塊記憶體。先關掉確認是不是這裡造成登入斷線。
static const bool ENABLE_MIMIR_ACTION_HIJACK = false;

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
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
          Sleep(1); // 避免非阻塞 socket 下忙等吃滿 CPU、拖慢其他執行緒時序
          continue;
        } else
          return ret;
      }
    }
    // xor_byte = (plain % 255) + 1 —— 對齊 Rust src/rsa32.rs 文件註解與
    // src/packet_proxy.rs::auth_xor_from_cipher（對齊 TGG EP6 Java server 演算法）。
    // 舊版直接把 modpow 結果截斷成最低 1 byte，公式錯誤，會導致封包全部亂碼。
    unsigned long plain = modpow(*(unsigned long *)buffer, _rsaD, _rsaN);
    _xorByte = (unsigned char)((plain % 255) + 1);
    inited = true;
  }
  int ret = real_recv(s, buf, len, flag);
  if (ret > 0) {
#if MIMIR_FEATURE_ENABLED
    if (ENABLE_MIMIR_PACKET_INTERCEPT) {
      TryHandleMimirPowerPacket(buf, ret);
    }
#endif
    if (ret == 0) {
      // 這次 recv 收到的整段資料剛好只有自訂封包、已經被完整吃掉了。回傳 0 會被
      // 原生 client 誤判成對端正常關閉連線，改用 WSAEWOULDBLOCK 讓它下次再讀，
      // 這是非阻塞 socket 下「目前沒資料」的正常訊號。
      WSASetLastError(WSAEWOULDBLOCK);
      return -1;
    }
    unsigned recvOpcode = extract_packet_opcode((const BYTE *)buf, ret);
    bool allowRecvLog = should_log_packet_opcode(recvOpcode);
    if (allowRecvLog) {
      char rawHex[512] = {0};
      char rawAscii[128] = {0};
      bytes_to_hex_preview((const BYTE *)buf, ret, rawHex, sizeof(rawHex), 24);
      bytes_to_ascii_preview((const BYTE *)buf, ret, rawAscii, sizeof(rawAscii),
                             24);
      unsigned headLen = 0;
      if (ret >= 2)
        headLen = (unsigned)(unsigned char)buf[0] |
                  ((unsigned)(unsigned char)buf[1] << 8);
      launcherdll_net_log(
          "[net: recv-raw] socket=%u ret=%d head_len=%u opcode=0x%02X "
          "inited=%d encrypt=%d ascii=[%s] hex=[%s]",
          (unsigned)s, ret, headLen, recvOpcode, (int)inited,
          (int)ShareInfo.encrypt, rawAscii, rawHex);
      launcherdll_net_log(
          "[net: recv-decoded-preview] socket=%u ret=%d opcode=0x%02X "
          "ascii=[%s] hex=[%s]",
          (unsigned)s, ret, recvOpcode, rawAscii, rawHex);
      log_slot_trace("recv", s, recvOpcode, (const BYTE *)buf, ret);
    }
  } else if (ret == 0) {
    // 對端正常關閉連線。
    launcherdll_net_log(
        "[my_recv][CLOSE] socket=%u ret=0 len=%d flag=%d inited=%d encrypt=%d",
        (unsigned)s, len, flag, (int)inited, (int)ShareInfo.encrypt);
  } else {
    int lastErr = WSAGetLastError();
    // WSAEWOULDBLOCK(10035) 在 non-blocking socket 下屬正常情況，忽略避免誤判。
    if (lastErr != WSAEWOULDBLOCK) {
      launcherdll_net_log(
          "[my_recv][ERR] socket=%u ret=%d wsa=%d len=%d flag=%d inited=%d "
          "encrypt=%d",
          (unsigned)s, ret, lastErr, len, flag, (int)inited,
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
  game_client_messagebox_log_file_a(lpCaption, lpText);
  return real_MessageBoxA(hWnd, lpText, lpCaption, uType);
}

static int WINAPI my_MessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption,
                          UINT uType) {
  // 注意：日誌使用 %ls 來處理寬字元
  launcherdll_net_log("[MessageBoxW] Caption='%ls', Text='%ls'",
      lpCaption ? lpCaption : L"(null)",
      lpText ? lpText : L"(null)");
  game_client_messagebox_log_file_w(lpCaption, lpText);
  return real_MessageBoxW(hWnd, lpText, lpCaption, uType);
}

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
    size_t nread = fread(file_data, 1, len, fp);
    fclose(fp);

    if (nread != len) {
      launcherdll_net_log(
          "[GetFileBuffer] fread INCOMPLETE: got %u bytes, expected %u — aborting "
          "(partial read would corrupt decrypt)",
          (unsigned)nread, len);
      delete[] file_data;
      return NULL;
    }
    launcherdll_net_log("[GetFileBuffer] fread OK: read %u bytes (matches file size)",
                        (unsigned)nread);

    VMProtectBegin;

    const char *fixedKey = "PAt82IqEvNBmERYl";
    launcherdll_net_log("[GetFileBuffer] Decrypting %u bytes with fixed XOR key...", len);
    for (unsigned int i = 0; i < len; i++) {
      file_data[i] ^= (BYTE)fixedKey[i % 16];
    }

    {
      char hexPrev[160] = {0};
      bytes_to_hex_preview(file_data, (int)len, hexPrev, sizeof(hexPrev), 32);
      launcherdll_net_log(
          "[GetFileBuffer] After XOR, first 32 bytes (hex) = [%s] "
          "(compare with Verify-Tw13081901Pak.ps1 on same file)",
          hexPrev);
    }

    buffer_len = len;
    VMProtectEnd;
    return file_data;
  }
  return NULL;
}

// =============================================================================
// 裝備欄擴展（14->31，附加式佈局）— 逐項移植自 Rust 版 src/equip_ui.rs
//
// 背景：原本這裡用 L1Offsets::PatchTargets 寫死 4 個位址直接改
// CMP reg,0x0E -> CMP reg,0x12。稽核時拿正式執行期 dump 比對，
// 這 4 個位址實際內容跟預期的 CMP 指令完全對不上（讀到的是不相關的
// LEA/PUSH 指令位元組），代表這組寫死位址對這個編譯版本是錯的 ——
// 不只沒效果，還會把不相關指令改壞，有造成當機/異常行為的風險。
// 改用特徵碼（AOB）掃描動態定位，跟 Rust 版技術路線一致；同一份 dump
// 用特徵碼可以正確命中 Patch A/B/D 三個目標，已驗證。
// =============================================================================
namespace EquipUiPatch {
  constexpr uintptr_t SCAN_START_ADDR = 0x00790000;
  constexpr uintptr_t SCAN_END_ADDR = 0x007A0000;
  constexpr uintptr_t SURF_BOUNDS_CHECK = 0x004387DB;

  // server_index(0-31) -> UI slot / child index（0=無效），對齊 Rust EQUIP_LOOKUP_TABLE
  static const BYTE EQUIP_LOOKUP_TABLE[32] = {
      0,
      2, 5, 4, 6, 11, 7, 9, 14, 16, 3, 8, 1,
      0, 0, 0, 0, 0,
      10, 12, 13, 15, 17, 18, 19, 16,
      46, 47, 48, 49, 50, 51,
  };

  // pattern 用 -1 代表萬用字元（對齊 Rust memory::scan_pattern 的 Option<u8>）
  static BYTE *FindPattern(BYTE *start, BYTE *end, const int *pattern, int patLen) {
    for (BYTE *p = start; p + patLen <= end; ++p) {
      bool match = true;
      for (int i = 0; i < patLen; ++i) {
        if (pattern[i] != -1 && p[i] != (BYTE)pattern[i]) { match = false; break; }
      }
      if (match) return p;
    }
    return nullptr;
  }

  // 從指定位址向前搜尋函數入口（55 8B EC prologue），對齊 Rust find_func_entry
  static BYTE *FindFuncEntryBackward(BYTE *from, size_t maxBack) {
    BYTE *start = from - maxBack;
    for (BYTE *p = from - 3; p >= start; --p) {
      if (p[0] == 0x55 && p[1] == 0x8B && p[2] == 0xEC) return p;
    }
    return nullptr;
  }

  // Patch A: ServerIndex_to_UISlot codecave 替換（AOB定位 + 14->31 動態映射表）
  static void PatchServerIndexToUiSlot() {
    static const int AOB[] = {
        0x83, 0xE9, 0x01, 0x89, 0x4D, 0xF4, 0x83, 0x7D, 0xF4, 0x15,
        0x0F, 0x87, -1, -1, -1, -1,
        0x8B, 0x55, 0xF4, 0xFF, 0x24, 0x95};
    BYTE *hit = FindPattern((BYTE *)SCAN_START_ADDR, (BYTE *)SCAN_END_ADDR, AOB, 22);
    if (!hit) {
      launcherdll_net_log("[EquipUI][WARN] Patch A: 找不到 ServerIndex_to_UISlot AOB，跳過");
      return;
    }
    BYTE *funcEntry = FindFuncEntryBackward(hit, 0x30);
    if (!funcEntry) {
      launcherdll_net_log("[EquipUI][WARN] Patch A: 找不到函數入口，跳過");
      return;
    }
    if (funcEntry[0] == 0xE9) {
      launcherdll_net_log("[EquipUI] Patch A: 已被 hook，跳過");
      return;
    }

    BYTE *cave = (BYTE *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!cave) {
      launcherdll_net_log("[EquipUI][WARN] Patch A: codecave 配置失敗");
      return;
    }
    BYTE *tableAddr = cave + 32;

    BYTE sc[32];
    int i = 0;
    sc[i++] = 0x55; sc[i++] = 0x8B; sc[i++] = 0xEC;             // push ebp; mov ebp, esp
    sc[i++] = 0x8B; sc[i++] = 0x45; sc[i++] = 0x08;             // mov eax, [ebp+8]
    sc[i++] = 0x83; sc[i++] = 0xF8; sc[i++] = 0x1F;             // cmp eax, 31
    sc[i++] = 0x77; sc[i++] = 0x0F;                             // ja .ret_zero
    sc[i++] = 0x85; sc[i++] = 0xC0;                             // test eax, eax
    sc[i++] = 0x74; sc[i++] = 0x0B;                             // jz .ret_zero
    sc[i++] = 0x0F; sc[i++] = 0xB6; sc[i++] = 0x80;             // movzx eax, byte [eax + table]
    *(DWORD *)&sc[i] = (DWORD)(uintptr_t)tableAddr; i += 4;
    sc[i++] = 0x5D; sc[i++] = 0xC2; sc[i++] = 0x04; sc[i++] = 0x00; // pop ebp; ret 4
    sc[i++] = 0x31; sc[i++] = 0xC0; sc[i++] = 0x5D; sc[i++] = 0xC2; sc[i++] = 0x04; sc[i++] = 0x00; // .ret_zero
    if (i != 32) {
      launcherdll_net_log("[EquipUI][WARN] Patch A: codecave 組裝長度不符 (%d != 32)，中止", i);
      VirtualFree(cave, 0, MEM_RELEASE);
      return;
    }
    memcpy(cave + 32, EQUIP_LOOKUP_TABLE, 32);
    PatchCode(cave, sc, 32);

    BYTE jmp5[5];
    jmp5[0] = 0xE9;
    *(int *)&jmp5[1] = (int)((intptr_t)cave - (intptr_t)funcEntry - 5);
    PatchCode(funcEntry, jmp5, 5);

    launcherdll_net_log("[EquipUI] Patch A OK: ServerIndex_to_UISlot @0x%p -> codecave 0x%p", funcEntry, cave);
  }

  // Patch B: SetupSlots 雙 Hook — 附加式佈局（不改迴圈上限，新 slot 在 child 46-51）
  static void PatchSetupSlotsHooks() {
    static const int AOB[] = {
        0xC7, 0x45, 0xF8, 0x01, 0x00, 0x00, 0x00,
        0xEB, 0x09,
        0x8B, 0x4D, 0xF8,
        0x83, 0xC1, 0x01,
        0x89, 0x4D, 0xF8,
        0x83, 0x7D, 0xF8, -1};
    BYTE *hit = FindPattern((BYTE *)SCAN_START_ADDR, (BYTE *)SCAN_END_ADDR, AOB, 22);
    if (!hit) {
      launcherdll_net_log("[EquipUI][WARN] Patch B: 找不到 SetupSlots AOB，跳過");
      return;
    }

    BYTE *exitAddr = hit + 0x2E;
    BYTE *callAddr = hit + 0x27;
    BYTE *bgCalcAddr = hit + 0xBB8;

    if (exitAddr[0] == 0xE9) {
      launcherdll_net_log("[EquipUI] Patch B: 已被 hook，跳過");
      return;
    }
    static const BYTE expectedExit[5] = {0x8B, 0xE5, 0x5D, 0xC3, 0xCC};
    if (memcmp(exitAddr, expectedExit, 5) != 0) {
      launcherdll_net_log("[EquipUI][WARN] Patch B1: exit bytes 不符 @0x%p，跳過", exitAddr);
      return;
    }
    if (callAddr[0] != 0xE8) {
      launcherdll_net_log("[EquipUI][WARN] Patch B: call 指令不符 (0x%02X)，跳過", callAddr[0]);
      return;
    }
    int rel32 = *(int *)&callAddr[1];
    BYTE *helperAddr = callAddr + 5 + rel32;

    static const BYTE expectedBg[7] = {0x8B, 0x4D, 0x0C, 0x83, 0xC1, 0x1A, 0x51};
    if (memcmp(bgCalcAddr, expectedBg, 7) != 0) {
      launcherdll_net_log("[EquipUI][WARN] Patch B2: bg bytes 不符 @0x%p，跳過", bgCalcAddr);
      return;
    }

    BYTE *cave = (BYTE *)VirtualAlloc(NULL, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!cave) {
      launcherdll_net_log("[EquipUI][WARN] Patch B: codecave 配置失敗");
      return;
    }
    BYTE *caveB1 = cave;
    BYTE *caveB2 = cave + 80;

    // --- 組裝 Hook B1（函數出口追加 6 次 helper 呼叫，child 46-51）---
    BYTE b1[80];
    int n = 0;
    b1[n++] = 0xC7; b1[n++] = 0x45; b1[n++] = 0xF8; b1[n++] = 0x00; b1[n++] = 0x00; b1[n++] = 0x00; b1[n++] = 0x00; // mov [ebp-8],0
    int loopTop = n;
    b1[n++] = 0x83; b1[n++] = 0x7D; b1[n++] = 0xF8; b1[n++] = 0x06; // cmp [ebp-8],6
    b1[n++] = 0x7D; int jgeRel8Pos = n; b1[n++] = 0x00;             // jge .done (placeholder)
    b1[n++] = 0x6A; b1[n++] = 0x00;                                 // push 0 (visible)
    b1[n++] = 0x6A; b1[n++] = 0x00;                                 // push 0 (equip_data)
    b1[n++] = 0x8B; b1[n++] = 0x55; b1[n++] = 0xF8;                 // mov edx,[ebp-8]
    b1[n++] = 0x83; b1[n++] = 0xC2; b1[n++] = 0x2E;                 // add edx,46
    b1[n++] = 0x52;                                                 // push edx
    b1[n++] = 0x8B; b1[n++] = 0x45; b1[n++] = 0xFC;                 // mov eax,[ebp-4]
    b1[n++] = 0x50;                                                 // push eax
    b1[n++] = 0x8B; b1[n++] = 0x4D; b1[n++] = 0xF4;                 // mov ecx,[ebp-0xC]
    b1[n++] = 0xE8;                                                 // call helper
    {
      BYTE *callSite = caveB1 + n;
      int helperRel = (int)((intptr_t)helperAddr - (intptr_t)(callSite + 4));
      *(int *)&b1[n] = helperRel;
      n += 4;
    }
    b1[n++] = 0xFF; b1[n++] = 0x45; b1[n++] = 0xF8; // inc [ebp-8]
    {
      int jmpRel = loopTop - (n + 2);
      b1[n++] = 0xEB; b1[n++] = (BYTE)jmpRel; // jmp .loop
    }
    int donePos = n;
    b1[jgeRel8Pos] = (BYTE)(donePos - jgeRel8Pos - 1);
    b1[n++] = 0x8B; b1[n++] = 0xE5; b1[n++] = 0x5D; b1[n++] = 0xC3; // mov esp,ebp; pop ebp; ret

    // --- 組裝 Hook B2（bg index 條件修正：>=46 時 +6，否則沿用原本 +0x1A）---
    BYTE b2[16];
    int m = 0;
    b2[m++] = 0x8B; b2[m++] = 0x4D; b2[m++] = 0x0C;               // mov ecx,[ebp+0xC]
    b2[m++] = 0x83; b2[m++] = 0xF9; b2[m++] = 0x2E;               // cmp ecx,46
    b2[m++] = 0x7C; b2[m++] = 0x04;                               // jl .normal
    b2[m++] = 0x83; b2[m++] = 0xC1; b2[m++] = 0x06; b2[m++] = 0xC3; // add ecx,6; ret
    b2[m++] = 0x83; b2[m++] = 0xC1; b2[m++] = 0x1A; b2[m++] = 0xC3; // .normal: add ecx,0x1A; ret

    PatchCode(caveB1, b1, n);
    PatchCode(caveB2, b2, m);

    BYTE hookB1[5];
    hookB1[0] = 0xE9;
    *(int *)&hookB1[1] = (int)((intptr_t)caveB1 - (intptr_t)(exitAddr + 5));
    PatchCode(exitAddr, hookB1, 5);

    BYTE hookB2[7];
    hookB2[0] = 0xE8;
    *(int *)&hookB2[1] = (int)((intptr_t)caveB2 - (intptr_t)(bgCalcAddr + 5));
    hookB2[5] = 0x51; // push ecx
    hookB2[6] = 0x90; // nop
    PatchCode(bgCalcAddr, hookB2, 7);

    launcherdll_net_log("[EquipUI] Patch B OK: helper@0x%p, exit@0x%p->0x%p, bg@0x%p->0x%p",
                        helperAddr, exitAddr, caveB1, bgCalcAddr, caveB2);
  }

  // Patch D: Surf ID bounds check — 固定位址（已用正式執行期 dump 逐 byte 驗證過）
  static void PatchSurfBoundsCheck() {
    BYTE *addr = (BYTE *)SURF_BOUNDS_CHECK;
    if (addr[0] == 0x81 && addr[1] == 0xFA) {
      launcherdll_net_log("[EquipUI] Patch D: 已套用，跳過");
      return;
    }
    static const BYTE expected[6] = {0x3B, 0x15, 0xB0, 0xD0, 0xC2, 0x00};
    if (memcmp(addr, expected, 6) != 0) {
      launcherdll_net_log("[EquipUI][WARN] Patch D: 指令不符 @0x%p，跳過", addr);
      return;
    }
    BYTE patched[6] = {0x81, 0xFA, 0x33, 0x75, 0x00, 0x00}; // cmp edx, 30003
    PatchCode(addr, patched, 6);
    launcherdll_net_log("[EquipUI] Patch D OK: Surf bounds check -> cmp edx,30003");
  }

  static void InstallAll() {
    launcherdll_net_log("[EquipUI] 開始安裝裝備欄擴展 patch（A+B+D，AOB動態定位，14->31）");
    PatchServerIndexToUiSlot();
    PatchSetupSlotsHooks();
    PatchSurfBoundsCheck();
    launcherdll_net_log("[EquipUI] 裝備欄擴展 A+B+D 流程結束");
  }
} // namespace EquipUiPatch

// =============================================================================
// 變身跑步（順跑）— 逐項移植自 Rust 版 src/smooth_run_hook.rs 的 per-entity 中段 hook。
// 位址 0x00449776 已用正式執行期 dump 驗證，原始 5 bytes 確認為 8B 44 C2 04 5D，跟 Rust
// EXPECTED_BYTES 完全吻合。這裡的 shellcode 組譯邏輯是逐 byte 翻譯 Rust 已驗證的版本，
// 不是重新設計；DLL 跟遊戲同行程，codecave 直接用 VirtualAlloc 配置（比 Rust 版
// VirtualAllocEx 對外部行程操作簡單）。
// =============================================================================
namespace SmoothRunPatch {
  constexpr uintptr_t HOOK_ADDR = 0x00449776;
  static const BYTE EXPECTED_BYTES[5] = {0x8B, 0x44, 0xC2, 0x04, 0x5D};

  constexpr DWORD RUNL_SLOT_OFF = 0x0314;
  constexpr DWORD RUNR_SLOT_OFF = 0x031C;
  constexpr BYTE HASTE_HIGH_OFF = 0x29;
  constexpr BYTE ENTITY_EBP_OFF = 0xA4; // -0x5C as u8
  constexpr DWORD MOVEMENT_FUNC_LO = 0x005AA000;
  constexpr DWORD MOVEMENT_FUNC_HI = 0x005AAA00;

  constexpr DWORD HASH_TABLE_OFF = 0x200;
  constexpr int HASH_TABLE_SIZE = 64;
  constexpr BYTE HASH_TABLE_MASK = 0x3F;
  constexpr size_t CAVE_SIZE = HASH_TABLE_OFF + HASH_TABLE_SIZE * 4 + 64;

  // 組譯 shellcode，逐行對照 smooth_run_hook.rs::build_shellcode 翻譯。
  // sc 緩衝區至少要 HASH_TABLE_OFF bytes（hash table 緊接在 shellcode 後面）。
  static int BuildShellcode(DWORD caveAddr, BYTE *sc) {
    int n = 0;
    DWORD hashTableAddr = caveAddr + HASH_TABLE_OFF;

    // === 1. 原始查表（被覆蓋的指令）===
    sc[n++] = 0x8B; sc[n++] = 0x45; sc[n++] = 0x0C;               // mov eax,[ebp+0xC]
    sc[n++] = 0x8B; sc[n++] = 0x44; sc[n++] = 0xC2; sc[n++] = 0x04; // mov eax,[edx+eax*8+4]

    // === 2. slot 98 有效性 ===
    sc[n++] = 0x81; sc[n++] = 0xBA;
    *(DWORD *)&sc[n] = RUNL_SLOT_OFF; n += 4;
    *(DWORD *)&sc[n] = 0x00010000u; n += 4;
    sc[n++] = 0x0F; sc[n++] = 0x82;
    int jb_done_slot = n; n += 4;

    // === 3. 走路動作檢查 ===
    sc[n++] = 0x8B; sc[n++] = 0x4D; sc[n++] = 0x0C;               // mov ecx,[ebp+0xC]
    sc[n++] = 0x85; sc[n++] = 0xC9;                               // test ecx,ecx
    sc[n++] = 0x74; int jz_walk = n; sc[n++] = 0x00;

    auto cmpJe = [&](BYTE val) -> int {
      sc[n++] = 0x83; sc[n++] = 0xF9; sc[n++] = val;
      sc[n++] = 0x74; int pos = n; sc[n++] = 0x00;
      return pos;
    };
    int je_w1 = cmpJe(4), je_w2 = cmpJe(11), je_w3 = cmpJe(20), je_w4 = cmpJe(24);
    int je_w5 = cmpJe(40), je_w6 = cmpJe(46), je_w7 = cmpJe(50), je_w8 = cmpJe(54);
    int je_w9 = cmpJe(58), je_w10 = cmpJe(62), je_w11 = cmpJe(83), je_w12 = cmpJe(88);
    int je_w13 = cmpJe(119);

    sc[n++] = 0xE9; int jmp_done_notwalk = n; n += 4;

    // .is_walk
    int walk_off = n;
    for (int pos : {jz_walk, je_w1, je_w2, je_w3, je_w4, je_w5, je_w6, je_w7, je_w8, je_w9,
                    je_w10, je_w11, je_w12, je_w13}) {
      sc[pos] = (BYTE)(walk_off - pos - 1);
    }

    // === 4. return address guard ===
    sc[n++] = 0x8B; sc[n++] = 0x4D; sc[n++] = 0x04;               // mov ecx,[ebp+4]
    sc[n++] = 0x81; sc[n++] = 0xF9;
    *(DWORD *)&sc[n] = MOVEMENT_FUNC_LO; n += 4;
    sc[n++] = 0x0F; sc[n++] = 0x82; int jb_done_guard1 = n; n += 4;
    sc[n++] = 0x81; sc[n++] = 0xF9;
    *(DWORD *)&sc[n] = MOVEMENT_FUNC_HI; n += 4;
    sc[n++] = 0x0F; sc[n++] = 0x87; int ja_done_guard2 = n; n += 4;

    // === 5. 取 entity 指標 ===
    sc[n++] = 0x8B; sc[n++] = 0x4D; sc[n++] = 0x00;               // mov ecx,[ebp]
    sc[n++] = 0x8B; sc[n++] = 0x49; sc[n++] = ENTITY_EBP_OFF;     // mov ecx,[ecx-0x5C]
    sc[n++] = 0x85; sc[n++] = 0xC9;                               // test ecx,ecx
    sc[n++] = 0x0F; sc[n++] = 0x84; int jz_done_null = n; n += 4;

    // === 6. per-entity 加速檢查 ===
    sc[n++] = 0x50;                                               // push eax
    sc[n++] = 0x80; sc[n++] = 0x79; sc[n++] = HASTE_HIGH_OFF; sc[n++] = 0x00; // cmp byte[ecx+0x29],0
    sc[n++] = 0x74; int jz_no_haste = n; sc[n++] = 0x00;

    // === 7. Stateful foot selection ===
    sc[n++] = 0x57;                                               // push edi
    sc[n++] = 0x89; sc[n++] = 0xC8;                               // mov eax,ecx
    sc[n++] = 0xC1; sc[n++] = 0xE8; sc[n++] = 0x03;               // shr eax,3
    sc[n++] = 0x83; sc[n++] = 0xE0; sc[n++] = HASH_TABLE_MASK;    // and eax,0x3F
    sc[n++] = 0xC1; sc[n++] = 0xE0; sc[n++] = 0x02;               // shl eax,2
    sc[n++] = 0x05; *(DWORD *)&sc[n] = hashTableAddr; n += 4;     // add eax,hash_table_addr
    sc[n++] = 0x89; sc[n++] = 0xC7;                               // mov edi,eax

    sc[n++] = 0x66; sc[n++] = 0x39; sc[n++] = 0x0F;               // cmp word[edi],cx
    sc[n++] = 0x75; int jne_new_entry = n; sc[n++] = 0x00;

    sc[n++] = 0x0F; sc[n++] = 0xB6; sc[n++] = 0x41; sc[n++] = 0x17; // movzx eax,byte[ecx+0x17]
    sc[n++] = 0x3A; sc[n++] = 0x47; sc[n++] = 0x02;               // cmp al,[edi+2]
    sc[n++] = 0x73; int jae_no_wrap = n; sc[n++] = 0x00;

    sc[n++] = 0x80; sc[n++] = 0x77; sc[n++] = 0x03; sc[n++] = 0x01; // xor byte[edi+3],1

    int no_wrap_off = n;
    sc[jae_no_wrap] = (BYTE)(no_wrap_off - jae_no_wrap - 1);
    sc[n++] = 0x88; sc[n++] = 0x47; sc[n++] = 0x02;               // mov[edi+2],al
    sc[n++] = 0xEB; int jmp_apply_toggle = n; sc[n++] = 0x00;

    int new_entry_off = n;
    sc[jne_new_entry] = (BYTE)(new_entry_off - jne_new_entry - 1);
    sc[n++] = 0x66; sc[n++] = 0x89; sc[n++] = 0x0F;               // mov word[edi],cx
    sc[n++] = 0x0F; sc[n++] = 0xB6; sc[n++] = 0x41; sc[n++] = 0x17; // movzx eax,byte[ecx+0x17]
    sc[n++] = 0x88; sc[n++] = 0x47; sc[n++] = 0x02;               // mov[edi+2],al
    sc[n++] = 0xC6; sc[n++] = 0x47; sc[n++] = 0x03; sc[n++] = 0x00; // mov byte[edi+3],0

    sc[n++] = 0x8B; sc[n++] = 0x45; sc[n++] = 0x0C;               // mov eax,[ebp+0xC]
    sc[n++] = 0x85; sc[n++] = 0xC0;                               // test eax,eax
    sc[n++] = 0x74; int jz_apply_toggle = n; sc[n++] = 0x00;
    sc[n++] = 0xC6; sc[n++] = 0x47; sc[n++] = 0x03; sc[n++] = 0x01; // mov byte[edi+3],1

    int apply_toggle_off = n;
    sc[jmp_apply_toggle] = (BYTE)(apply_toggle_off - jmp_apply_toggle - 1);
    sc[jz_apply_toggle] = (BYTE)(apply_toggle_off - jz_apply_toggle - 1);
    sc[n++] = 0x0F; sc[n++] = 0xB6; sc[n++] = 0x47; sc[n++] = 0x03; // movzx eax,byte[edi+3]
    sc[n++] = 0x5F;                                               // pop edi
    sc[n++] = 0x85; sc[n++] = 0xC0;                               // test eax,eax
    sc[n++] = 0x59;                                               // pop ecx
    sc[n++] = 0x75; int jnz_runr = n; sc[n++] = 0x00;

    sc[n++] = 0x8B; sc[n++] = 0x82; *(DWORD *)&sc[n] = RUNL_SLOT_OFF; n += 4; // mov eax,[edx+0x314]
    sc[n++] = 0xE9; int jmp_done_runl = n; n += 4;

    int runr_off = n;
    sc[jnz_runr] = (BYTE)(runr_off - jnz_runr - 1);
    sc[n++] = 0x81; sc[n++] = 0xBA; *(DWORD *)&sc[n] = RUNR_SLOT_OFF; n += 4;
    *(DWORD *)&sc[n] = 0x00010000u; n += 4;
    sc[n++] = 0x72; int jb_fb = n; sc[n++] = 0x00;
    sc[n++] = 0x8B; sc[n++] = 0x82; *(DWORD *)&sc[n] = RUNR_SLOT_OFF; n += 4;
    sc[n++] = 0xEB; int jmp_done_runr = n; sc[n++] = 0x00;

    int fb_off = n;
    sc[jb_fb] = (BYTE)(fb_off - jb_fb - 1);
    sc[n++] = 0x8B; sc[n++] = 0x82; *(DWORD *)&sc[n] = RUNL_SLOT_OFF; n += 4;
    sc[n++] = 0xEB; int jmp_done_fb = n; sc[n++] = 0x00;

    // === .no_haste_pop ===
    int no_haste_off = n;
    sc[jz_no_haste] = (BYTE)(no_haste_off - jz_no_haste - 1);
    sc[n++] = 0x58;                                               // pop eax

    // === .done ===
    int done_off = n;
    for (int fixup : {jb_done_slot, jmp_done_notwalk, jb_done_guard1, ja_done_guard2,
                      jz_done_null, jmp_done_runl}) {
      *(int *)&sc[fixup] = done_off - fixup - 4;
    }
    sc[jmp_done_runr] = (BYTE)(done_off - jmp_done_runr - 1);
    sc[jmp_done_fb] = (BYTE)(done_off - jmp_done_fb - 1);

    sc[n++] = 0x5D;                                               // pop ebp
    sc[n++] = 0xC3;                                               // ret

    return n;
  }

  static void InstallHook() {
    BYTE *addr = (BYTE *)HOOK_ADDR;
    if (addr[0] == 0xE9) {
      launcherdll_net_log("[SmoothRun] 已被 hook，跳過");
      return;
    }
    if (memcmp(addr, EXPECTED_BYTES, 5) != 0) {
      launcherdll_net_log(
          "[SmoothRun][WARN] 0x%08X 位元組不符（%02X %02X %02X %02X %02X，預期 8B 44 C2 04 5D），跳過",
          (unsigned)HOOK_ADDR, addr[0], addr[1], addr[2], addr[3], addr[4]);
      return;
    }

    BYTE *cave = (BYTE *)VirtualAlloc(NULL, CAVE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!cave) {
      launcherdll_net_log("[SmoothRun][WARN] codecave 配置失敗");
      return;
    }

    BYTE sc[HASH_TABLE_OFF];
    int scLen = BuildShellcode((DWORD)(uintptr_t)cave, sc);
    if (scLen > (int)HASH_TABLE_OFF) {
      launcherdll_net_log("[SmoothRun][WARN] shellcode 長度 %d 超過 hash table 偏移 0x%X，中止",
                          scLen, HASH_TABLE_OFF);
      VirtualFree(cave, 0, MEM_RELEASE);
      return;
    }
    PatchCode(cave, sc, scLen);

    BYTE jmp5[5];
    jmp5[0] = 0xE9;
    *(int *)&jmp5[1] = (int)((intptr_t)cave - (intptr_t)addr - 5);
    PatchCode(addr, jmp5, 5);

    launcherdll_net_log(
        "[SmoothRun] hook 已安裝 @0x%08X -> codecave 0x%p（per-entity，anim_frame wrap L/R）",
        (unsigned)HOOK_ADDR, cave);
  }
} // namespace SmoothRunPatch

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

        // 裝備欄擴展 A+B+D（AOB 動態定位，14->31，對照 Rust src/equip_ui.rs）。
        // 舊版寫死位址（L1Offsets::PatchTargets）稽核時比對正式 dump 發現完全對不上，
        // 已移除，改用這裡的特徵碼掃描版本，見上方 EquipUiPatch 命名空間。
        EquipUiPatch::InstallAll();

        // 變身跑步（順跑）hook，逐項移植自 Rust src/smooth_run_hook.rs，位址已用
        // 正式執行期 dump 驗證，見上方 SmoothRunPatch 命名空間。
        SmoothRunPatch::InstallHook();
        break;
      }
      Sleep(1);
    }
  } __except (1) {
    launcherdll_net_log("[Patch] *** CRITICAL *** 補丁執行例外。 ");
  }
  return 0;
}

// 稽核發現：Rust 參考版（src/patch.rs::apply_time_guard_patches）處理 Themida/版本檢查
// 完全不動系統時鐘，只靠 0x004E204E（PatchThread）+ 0x00722761（my_CreateWindowEx）兩個
// 一次性 byte patch 就足夠——這兩個 patch C++ 這邊本來就都有做。先前疊加的系統時鐘偽造機制
// （SetFakeSystemTime/RestoreSystemTime + timeController.cpp 的 5 個時間 API hook）
// 在 Rust 裡找不到對應，屬於多餘且風險較高的舊機制（會動到整台機器的真實時鐘，且
// GetTickCount/timeGetTime 被寫死回傳常數、永遠不遞增，可能是「過一陣子當機」的根因），
// 已確認移除。

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
    return 1;
  }
  launcherdll_net_log(
      "[DelayedDetour] code decrypted (waited %d ms), installing ALL hooks...",
      waitCount * 10);

  // 解殼完成：安裝所有 Detours hook（API，不再含時間 hook——見上方稽核註記）
  DetourRestoreAfterWith();
  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
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

  // [恢復] 啟動 PatchThread 對遊戲核心進行診斷性補丁
  CloseHandle(CreateThread(NULL, 0, PatchThread, NULL, 0, NULL));
  // 密米爾之泉：等動作登記表初始化完成後，劫持 Action_SocialAction1 的處理函式
  // 隔離測試：懷疑 0xC2FD88 旗標代表「開始初始化」而非「初始化完成」，我們的
  // patch 執行緒可能跟 client 自己對同一個 slot 的建構動作互相搶寫，暫時關掉
  // 確認是不是這裡造成斷線。
#if MIMIR_FEATURE_ENABLED
  if (ENABLE_MIMIR_ACTION_HIJACK) {
    CloseHandle(CreateThread(NULL, 0, MimirActionPatchThread, NULL, 0, NULL));
  }
#endif
  // [暫停中] 實驗性 Action 4 偏移修正 Hook，根據要求暫不啟動
  // HookCode((void *)0x58228A, (void *)NakedLoaderHook, 6);

  launcherdll_net_log("[DelayedDetour] all hooks installed successfully");
  return 0;
}

// =============================================================================
// CRT 崩潰防護 — 對齊 Rust 版 src/patch.rs::patch_crt_watson 的用意（稽核時發現
// 這個 DLL 完全沒有對應防護：大量使用 sprintf_s/vsprintf_s 系列的地方，一旦
// 偵測到無效參數，預設行為是呼叫 _invoke_watson 直接中止整個遊戲行程，
// 而且無法攔截、不會留下任何 log，玩家只會看到遊戲無預警消失）。
//
// 這裡裝兩層防護：
// 1. _set_invalid_parameter_handler：CRT 偵測到無效參數時改成記錄一行 log
//    後直接返回，不呼叫 _invoke_watson／abort。
// 2. SetUnhandledExceptionFilter：行程層級的最後防線，任何沒被 __try/__except
//    接住的例外，至少先記錄 log 再讓系統依原本流程處理，而不是完全沒有線索。
// 純新增、不影響任何既有邏輯，也跟「封包加密維持明文」「.pak維持純XOR」這兩項
// 使用者確認過的既有決定無關。
// =============================================================================
static void __cdecl SafeInvalidParameterHandler(const wchar_t *expression,
                                                const wchar_t *function,
                                                const wchar_t *file,
                                                unsigned int line,
                                                uintptr_t pReserved) {
  launcherdll_net_log(
      "[CRT][WARN] invalid parameter 已攔截（避免行程中止）: function=%ls file=%ls line=%u",
      function ? function : L"?", file ? file : L"?", line);
}

static LONG WINAPI SafeUnhandledExceptionFilter(EXCEPTION_POINTERS *info) {
  __try {
    DWORD code = (info && info->ExceptionRecord) ? info->ExceptionRecord->ExceptionCode : 0;
    void *addr = (info && info->ExceptionRecord) ? info->ExceptionRecord->ExceptionAddress : nullptr;
    launcherdll_net_log("[CRT][CRITICAL] 未處理的例外：code=0x%08X addr=%p", (unsigned)code, addr);
  } __except (1) {
    // 連記錄都失敗就放棄，至少不要讓 filter 本身又拋一次例外
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

static void InstallCrashGuards() {
  _set_invalid_parameter_handler(SafeInvalidParameterHandler);
  SetUnhandledExceptionFilter(SafeUnhandledExceptionFilter);
}

// init: DLL 初始化進入點，供外部 Launcher 呼叫 (不可設為 static)
void init() {
  InstallCrashGuards();
  VMProtectBegin;
  launcherdll_net_log("[DLL-BUILD] rune-trace-v2 (packet semantic trace enabled)");
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

  launcherdll_net_log("[init] ShareInfo size=%u (bdfile wchar path buffer)",
                      (unsigned)sizeof(SHARE_INFO));
  
  // 依 list.txt 裡該伺服器的 encrypt/randenc 旗標走（由 LaunchService.cs::SetupSharedMemory
  // 依 ServerInfo.Encrypt/RandKey 寫入 ShareInfo，對齊 Rust server_list.rs 的 encrypt 欄位語意）。
  // 舊版這裡曾經強制關閉，是因為下面 RSA 金鑰還原的兩個 bug 導致啟用後資料亂碼，稽核比對
  // Rust src/rsa32.rs / src/packet_proxy.rs 後已個別修正，見下方註解。
  launcherdll_net_log("[init] ShareInfo copied: ip=%.31s, port=%d, encrypt=%d, randenc=%d",
                      (const char *)ShareInfo.ip, ShareInfo.port, (int)ShareInfo.encrypt,
                      (int)ShareInfo.randenc);

  // RSA D/N 直接取自共享記憶體，不做額外 XOR 還原 —— LaunchService.cs 寫入時就是明碼
  // （list.txt 整體密文已經過 AES+XOR，Server_Info.rsa_d/rsa_n 欄位內容本身不需要再異或一次）。
  // 舊版這裡用 SERVER_LIST_RSA_XOR_D/N 去異或還原，但 C# 端從未用這兩個常數編碼過，等於
  // 把正確的 D/N 異或壞掉，算出來的金鑰完全是錯的。
  _rsaD = (DWORD)pShareInfo->RSA_D;
  _rsaN = (DWORD)pShareInfo->RSA_N;
  launcherdll_net_log("[init] RSA keys loaded: N=0x%08X, D=0x%08X", _rsaN, _rsaD);
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

  // 所有 API hook 延遲到保護殼解密後安裝（Themida/版本檢查改由 0x004E204E +
  // 0x00722761 兩個 byte patch 處理，見上方稽核註記，不再需要系統時鐘偽造）
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
