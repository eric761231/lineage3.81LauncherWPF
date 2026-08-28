// MatchMakingHook.cpp: see MatchMakingHook.h.
//
// Root cause: clicking Killer/Hunter/Talker in MatchRegister_Window only
// writes the category's description string into Intro_Label (a display-only,
// hidden control) - it never reaches Intro_Edit, which is what Register
// actually reads and sends to the server. A player who doesn't manually
// type/paste their own text into the intro field gets an empty string, and
// Register silently no-ops (server replies with string 3307, "please enter
// your desired introduction text", but nothing shows it - it just looks like
// the button does nothing).
//
// Fix: hook SetTypeLabel (the function both the Label-coloring code and the
// registration-failure path funnel through) and, after letting it do its
// normal job, also push the matching description string into Intro_Edit via
// the same vtable-based child-control walk the game itself uses.
#include "stdafx.h"
#include "MatchMakingHook.h"
#include <cstring>
#include "detours.h"

#pragma comment(lib, "detours.lib")

namespace {

// Each translation unit in this project owns its own tiny logger (see
// NetLog in DisconnectHook.cpp, ImGuiLog in ImGuiHook.cpp) - the
// launcherdll_net_log in LauncherDll.cpp is `static` (internal linkage) and
// isn't linkable from here.
void MmLog(const char *fmt, ...) {
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
  if (fopen_s(&fp, logPath, "a+") != 0 || fp == NULL) return;
  SYSTEMTIME st;
  GetLocalTime(&st);
  char msg[512] = {0};
  va_list args;
  va_start(args, fmt);
  vsprintf_s(msg, fmt, args);
  va_end(args);
  fprintf(fp, "[%04d-%02d-%02d %02d:%02d:%02d.%03d][PID=%u][TID=%u] [MatchMaking] %s\n",
          st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
          st.wMilliseconds, (unsigned)GetCurrentProcessId(), (unsigned)GetCurrentThreadId(), msg);
  fflush(fp);
  fclose(fp);
}

typedef void (__thiscall *SetTypeLabel_t)(void *mm, int type);
typedef char *(__thiscall *LookupString_t)(void *self, unsigned id);
typedef void *(__thiscall *VCall1_t)(void *self, int index);
typedef void (__thiscall *SetText_t)(void *self, const char *ansi);

SetTypeLabel_t real_SetTypeLabel = (SetTypeLabel_t)0x64F700;
const LookupString_t LookupString = (LookupString_t)0x4021B0;
void *const kStringTable = (void *)0xC2D0B0;

template <typename Fn>
Fn VtableSlot(void *obj, DWORD byteOffset) {
  void *vtbl = *(void **)obj;
  return *(Fn *)((BYTE *)vtbl + byteOffset);
}

// MSVC won't let a free function be defined with __thiscall directly (only
// member functions and function-pointer types may use it - real_SetTypeLabel
// above is fine as a typedef'd pointer, but this definition needs the
// standard __fastcall-with-a-dummy-EDX-slot trick: __fastcall passes its
// first two params in ECX/EDX, which is binary-compatible with how a
// thiscall caller already puts `this` in ECX; the unused edx parameter just
// absorbs whatever thiscall didn't put there, and `type` still arrives via
// the stack exactly like a thiscall stack argument would.
void __fastcall Hook_SetTypeLabel(void *mm, void * /*edx*/, int type) {
  real_SetTypeLabel(mm, type);
  if (type != 0 && type != 1 && type != 2) return;

  unsigned id = (type == 0) ? 3285u : (type == 1) ? 3256u : 3284u;
  char *s = LookupString(kStringTable, id);
  if (!s || !s[0]) {
    MmLog("Hook_SetTypeLabel: LookupString(%u) empty, skipping", id);
    return;
  }

  void *wnd = VtableSlot<VCall1_t>(mm, 0x50)(mm, 2);
  if (!wnd) return;
  void *introCtl = VtableSlot<VCall1_t>(wnd, 0x3C)(wnd, 1);
  if (!introCtl) return;
  void *edit = VtableSlot<VCall1_t>(introCtl, 0x3C)(introCtl, 0);
  if (!edit) return;
  VtableSlot<SetText_t>(edit, 0x94)(edit, s);
  MmLog("Hook_SetTypeLabel: type=%d wrote string id=%u into Intro_Edit", type, id);
}

} // namespace

void InstallMatchMakingHook() {
  const BYTE expected[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10};
  if (memcmp((void *)0x64F700, expected, 6) != 0) {
    MmLog("0x64F700 prologue mismatch, skipping install");
    return;
  }

  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  DetourAttach(&(PVOID &)real_SetTypeLabel, reinterpret_cast<PVOID>(Hook_SetTypeLabel));
  LONG result = DetourTransactionCommit();
  MmLog("SetTypeLabel hook install result=%ld", result);
}
