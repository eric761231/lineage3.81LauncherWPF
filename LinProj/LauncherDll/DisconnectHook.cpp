// DisconnectHook.cpp: detect S_Disconnect primarily via S2C framing.
// S→C payload uses Lineage Encryption (not LCG). Length header is plaintext.
// ProcessPacket ja@0x53938E kept as secondary; often silent on packed client.
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <atomic>
#include "detours.h"
#include "DisconnectHook.h"
#include "DisconnectOverlay.h"

#pragma comment(lib, "detours.lib")

namespace {

constexpr DWORD DISCONNECT_OPCODE = 227;
constexpr DWORD INIT_OPCODE = 150; // S_OPCODE_INITPACKET
constexpr DWORD G_GAME_STATE_ADDR = 0x009AB5E8;

// Wire size = payload_len + 2. S_Disconnect payload = 7 -> total 9.
constexpr int DISCONNECT_WIRE_LEN = 9;

constexpr DWORD HOOK_ADDR = 0x0053938E;
constexpr DWORD NOP_HANDLER_ADDR = 0x00541596;
constexpr int OPCODE_EBP_OFFSET = -0x60f4;

std::atomic<bool> g_installed{false};
std::atomic<bool> g_armedFromPacket{false};
std::atomic<bool> g_postLogin{false}; // true after client sent auth/login
BYTE *g_cave = nullptr;

BYTE g_pktBuf[8192];
int g_pktHave = 0;
int g_pktNeed = -1;
bool g_seenInit = false;
std::atomic<int> g_pktIndex{0};

void NetLog(const char *fmt, ...) {
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
  char msg[1024] = {0};
  va_list args;
  va_start(args, fmt);
  vsprintf_s(msg, fmt, args);
  va_end(args);
  fprintf(fp, "[%04d-%02d-%02d %02d:%02d:%02d.%03d][PID=%u][TID=%u] %s\n",
          st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
          st.wMilliseconds, (unsigned)GetCurrentProcessId(),
          (unsigned)GetCurrentThreadId(), msg);
  fflush(fp);
  fclose(fp);
}

int ReadGameState() {
  int st = -1;
  __try {
    st = (int)(*(volatile DWORD *)G_GAME_STATE_ADDR);
  } __except (1) {
    st = -1;
  }
  return st;
}

void HandleCompletePacket(const BYTE *pkt, int len) {
  // pkt: [len_lo][len_hi][payload...]  payload may be Encryption-ciphertext after INIT
  if (len < 3)
    return;

  g_pktIndex++;
  unsigned first = pkt[2];
  int st = ReadGameState();

  if (g_pktIndex <= 24 || len == DISCONNECT_WIRE_LEN) {
    NetLog("[disconnect-probe] s2c #%d wireLen=%d b0=0x%02X state=%d seenInit=%d",
           g_pktIndex.load(), len, first, st, (int)g_seenInit);
  }

  // INIT is plaintext on wire
  if (!g_seenInit && first == INIT_OPCODE) {
    g_seenInit = true;
    NetLog("[disconnect-probe] INIT packet seen (plaintext)");
    return;
  }

  // After INIT, payload is encrypted. Use wire length heuristic ONLY after
  // the player has submitted account/password (avoid early false positives).
  if (g_seenInit && g_postLogin.load() && len == DISCONNECT_WIRE_LEN &&
      (st == 1 || st == 2 || st < 0)) {
    NetLog("[disconnect] S_Disconnect inferred (wireLen=9, postLogin, state=%d)",
           st);
    g_armedFromPacket.store(true);
    DisconnectOverlay_Notify(500);
    return;
  }

  // NOTE: a "plaintext opcode 227" fallback used to live here for the case
  // encryption is off/mis-synced. Removed: payload is *always* encrypted in
  // practice (every session logs encrypt=1), so `first` here is really just
  // a ciphertext byte -- comparing it to 227 is comparing against noise, and
  // over a long play session it eventually matches by coincidence (observed
  // false-triggering the overlay while just talking to an NPC, with
  // nonsense "reason" values like 59192 decoded from more ciphertext bytes).
}

void FeedRecvByte(BYTE b) {
  if (g_pktHave >= (int)sizeof(g_pktBuf)) {
    g_pktHave = 0;
    g_pktNeed = -1;
  }
  g_pktBuf[g_pktHave++] = b;

  for (;;) {
    if (g_pktNeed < 0) {
      if (g_pktHave < 2)
        return;
      g_pktNeed = (int)g_pktBuf[0] | ((int)g_pktBuf[1] << 8);
      if (g_pktNeed < 3 || g_pktNeed > (int)sizeof(g_pktBuf)) {
        NetLog("[disconnect-probe] bad wireLen=%d, resync", g_pktNeed);
        memmove(g_pktBuf, g_pktBuf + 1, (size_t)(g_pktHave - 1));
        g_pktHave--;
        g_pktNeed = -1;
        continue;
      }
    }
    if (g_pktHave < g_pktNeed)
      return;
    HandleCompletePacket(g_pktBuf, g_pktNeed);
    int left = g_pktHave - g_pktNeed;
    if (left > 0)
      memmove(g_pktBuf, g_pktBuf + g_pktNeed, (size_t)left);
    g_pktHave = left;
    g_pktNeed = -1;
  }
}

extern "C" void __cdecl OnHighOpcodePacket(DWORD opcode, BYTE *payload) {
  static int s_logCount = 0;
  if (s_logCount < 64 || opcode == DISCONNECT_OPCODE) {
    s_logCount++;
    NetLog("[disconnect-probe] high-opcode=%u (0x%02X)", opcode, opcode & 0xFF);
  }
  if (opcode == DISCONNECT_OPCODE) {
    if (!g_postLogin.load())
      return;
    unsigned short reason = 500;
    if (payload) {
      __try {
        reason = *(unsigned short *)payload;
      } __except (1) {
        reason = 500;
      }
    }
    g_armedFromPacket.store(true);
    DisconnectOverlay_Notify(reason);
  }
}

bool PatchJaToCave(BYTE *cave) {
  BYTE cur[6] = {};
  __try {
    memcpy(cur, (void *)HOOK_ADDR, 6);
  } __except (1) {
    return false;
  }
  if (cur[0] != 0x0F || cur[1] != 0x87)
    return false;

  BYTE *p = cave;
  *p++ = 0x9C;
  *p++ = 0x60;
  *p++ = 0x8B;
  *p++ = 0x85;
  *(int *)p = OPCODE_EBP_OFFSET;
  p += 4;
  *p++ = 0x8B;
  *p++ = 0x4D;
  *p++ = 0x08;
  *p++ = 0x51;
  *p++ = 0x50;
  *p++ = 0xE8;
  {
    DWORD callSite = (DWORD)(p + 4);
    *(DWORD *)p = (DWORD)(uintptr_t)&OnHighOpcodePacket - callSite;
    p += 4;
  }
  *p++ = 0x83;
  *p++ = 0xC4;
  *p++ = 0x08;
  *p++ = 0x61;
  *p++ = 0x9D;
  *p++ = 0xE9;
  {
    DWORD jmpSite = (DWORD)(p + 4);
    *(DWORD *)p = NOP_HANDLER_ADDR - jmpSite;
    p += 4;
  }

  DWORD jaNext = HOOK_ADDR + 6;
  DWORD rel = (DWORD)(uintptr_t)cave - jaNext;
  BYTE patch[6] = {0x0F, 0x87, 0, 0, 0, 0};
  memcpy(patch + 2, &rel, 4);
  DWORD oldProt = 0;
  if (!VirtualProtect((void *)HOOK_ADDR, 6, PAGE_EXECUTE_READWRITE, &oldProt))
    return false;
  memcpy((void *)HOOK_ADDR, patch, 6);
  VirtualProtect((void *)HOOK_ADDR, 6, oldProt, &oldProt);
  FlushInstructionCache(GetCurrentProcess(), (void *)HOOK_ADDR, 6);
  NetLog("[disconnect-hook] ProcessPacket ja@0x%08X -> cave@0x%p", HOOK_ADDR,
         cave);
  return true;
}

DWORD WINAPI GameStateWatchThread(void *) {
  int last = -1;
  for (;;) {
    Sleep(100);
    int st = ReadGameState();
    if (st != last) {
      if (last != -1 || st == 9)
        NetLog("[disconnect-probe] G_GAME_STATE %d -> %d", last, st);
      last = st;
    }
  }
  return 0;
}

} // namespace

int(WINAPI *real_MessageBoxA)(HWND, LPCSTR, LPCSTR, UINT) = MessageBoxA;
int(WINAPI *real_MessageBoxW)(HWND, LPCWSTR, LPCWSTR, UINT) = MessageBoxW;
int(WINAPI *real_closesocket)(SOCKET s) = closesocket;

void DisconnectHook_OnClientLoginSent() {
  g_postLogin.store(true);
  NetLog("[disconnect-hook] client login/auth sent -> postLogin armed");
}

bool DisconnectHook_IsPostLogin() { return g_postLogin.load(); }

void DisconnectHook_OnRecvClosed() {
  NetLog("[disconnect-probe] recv closed: postLogin=%d armedFromPacket=%d",
         (int)g_postLogin.load(), (int)g_armedFromPacket.load());
}

void DisconnectHook_InitRecvCipher(int seed) {
  (void)seed;
  g_pktHave = 0;
  g_pktNeed = -1;
  g_seenInit = false;
  g_pktIndex = 0;
  g_armedFromPacket.store(false);
  // Keep g_postLogin across cipher reset within same session; clear on new connect
  // via Install path / explicit reset when RSA handshake restarts from my_connect.
  NetLog("[disconnect-hook] s2c assembler reset");
}

void DisconnectHook_ResetSession() {
  g_postLogin.store(false);
  g_armedFromPacket.store(false);
  g_pktHave = 0;
  g_pktNeed = -1;
  g_seenInit = false;
  g_pktIndex = 0;
  DisconnectOverlay_Hide(); // new login cycle starting -- retire any prior overlay
  NetLog("[disconnect-hook] session reset (new connect)");
}

void DisconnectHook_DecodeRandenc(unsigned char *dst, const unsigned char *src,
                                  int len) {
  // Deprecated for S2C: kept so LauncherDll links; copy through only.
  if (!dst || !src || len <= 0)
    return;
  memcpy(dst, src, (size_t)len);
}

void DisconnectHook_InspectRecvPlain(const unsigned char *data, int len) {
  // data must be raw wire bytes (length header plaintext).
  if (!data || len <= 0)
    return;
  for (int i = 0; i < len; i++)
    FeedRecvByte(data[i]);
}

int WINAPI my_MessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption,
                          UINT uType) {
  NetLog("[MessageBoxA] Caption='%s' Text='%s'",
         lpCaption ? lpCaption : "(null)", lpText ? lpText : "(null)");
  if (DisconnectOverlay_ShouldSwallowNative()) {
    NetLog("[disconnect] native MessageBoxA swallowed");
    if (lpText && lpText[0]) {
      wchar_t wtext[512] = {0};
      if (MultiByteToWideChar(CP_ACP, 0, lpText, -1, wtext,
                              (int)(sizeof(wtext) / sizeof(wtext[0]))) > 0)
        DisconnectOverlay_SetLiveText(wtext);
    }
    return IDOK;
  }
  // No prior packet-based trigger armed this. Empirically (see NetLog history),
  // the client never shows a native MessageBox on a normal successful login;
  // any box shown while postLogin is armed is the client's own connection/
  // kick failure notice (immediate post-login kick, idle-at-login-screen kick,
  // etc.) even when no S_Disconnect/S_LoginResult packet was ever observed.
  if (g_postLogin.load()) {
    NetLog("[disconnect] native MessageBoxA (postLogin, no prior packet trigger) -> overlay");
    g_armedFromPacket.store(true);
    DisconnectOverlay_ArmSwallow();
    DisconnectOverlay_Notify(500);
    if (lpText && lpText[0]) {
      wchar_t wtext[512] = {0};
      if (MultiByteToWideChar(CP_ACP, 0, lpText, -1, wtext,
                              (int)(sizeof(wtext) / sizeof(wtext[0]))) > 0)
        DisconnectOverlay_SetLiveText(wtext);
    }
    return IDOK;
  }
  return real_MessageBoxA(hWnd, lpText, lpCaption, uType);
}

int WINAPI my_MessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption,
                          UINT uType) {
  NetLog("[MessageBoxW] Caption='%ls' Text='%ls'",
         lpCaption ? lpCaption : L"(null)", lpText ? lpText : L"(null)");
  if (DisconnectOverlay_ShouldSwallowNative()) {
    NetLog("[disconnect] native MessageBoxW swallowed");
    if (lpText && lpText[0])
      DisconnectOverlay_SetLiveText(lpText);
    return IDOK;
  }
  if (g_postLogin.load()) {
    NetLog("[disconnect] native MessageBoxW (postLogin, no prior packet trigger) -> overlay");
    g_armedFromPacket.store(true);
    DisconnectOverlay_ArmSwallow();
    DisconnectOverlay_Notify(500);
    if (lpText && lpText[0])
      DisconnectOverlay_SetLiveText(lpText);
    return IDOK;
  }
  return real_MessageBoxW(hWnd, lpText, lpCaption, uType);
}

int WINAPI my_closesocket(SOCKET s) {
  NetLog("[disconnect-probe] closesocket s=%u armed=%d", (unsigned)s,
         (int)g_armedFromPacket.load());
  if (g_armedFromPacket.load() && !DisconnectOverlay_ShouldSwallowNative())
    DisconnectOverlay_Notify(500);
  return real_closesocket(s);
}

void InstallDisconnectHooks() {
  if (g_installed.exchange(true))
    return;

  if (!DisconnectOverlay_Start())
    NetLog("[disconnect-hook] overlay start failed (continue hooks)");

  g_cave = (BYTE *)VirtualAlloc(NULL, 256, MEM_COMMIT | MEM_RESERVE,
                                PAGE_EXECUTE_READWRITE);
  if (!g_cave)
    NetLog("[disconnect-hook] VirtualAlloc cave failed");
  else if (!PatchJaToCave(g_cave))
    NetLog("[disconnect-hook] PatchJaToCave failed (s2c framing remains)");

  CloseHandle(CreateThread(NULL, 0, GameStateWatchThread, NULL, 0, NULL));
  NetLog("[disconnect-hook] ready: opcode-227 packet detection only "
         "(wireLen heuristic + in-process ProcessPacket hook)");
}
