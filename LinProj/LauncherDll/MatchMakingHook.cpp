// MatchMakingHook.cpp: see MatchMakingHook.h.
//
// Root cause (full reverse-engineering writeup: LinBin3.81 project's
// docs/hooks/MATCHMAKING_PATCH_BRIEF.md): clicking a Killer/Hunter/Talker
// radio button in the "血盟推薦" (clan/pledge recommendation) registration
// window calls SetTypeLabel (VA 0x64F700), which writes the matching preview
// string (Killer=$3285, Hunter=$3256, Talker=$3284) into Intro_Label - it
// never touches Intro_Edit, which is the control Register (0x64FC50)
// actually reads via strlen()/vtable+0x94. So the intro text field is always
// empty unless the player manually types something in, and Register
// silently no-ops on an empty string (jumps to 0x64FE92, `return 1` with no
// packet sent).
//
// Second bug, found while chasing a reported flicker ("白字互搶顯示" - white
// text fighting the already-displayed content) in the "修正已登錄"
// (FixRegister, re-editing an existing registration) flow: full disassembly
// of 0x64F700 shows it NEVER checks mm+0x1D8 (the "this window already has
// saved intro text, Intro_Edit is the active display, Intro_Label is
// supposed to stay hidden" flag) at all - every single Radio-button click
// unconditionally updates Intro_Label's preview text and color via
// vtable+0x8C/+0xE0, whether or not Label is supposed to be hidden in favor
// of Edit. This is a genuine native game bug (nothing in this project
// introduced it), and it's the real explanation for the flicker: Label's
// content keeps changing underneath/behind whatever's currently on screen
// every time a Radio button is clicked, regardless of which widget is
// actually meant to be visible. +0x3D9 (the real selected-type field
// Register reads) is set by the caller (0x64FBC0/0x64FBF0/0x64FC20) before
// calling SetTypeLabel, not by SetTypeLabel itself - so skipping the real
// call entirely when mm+0x1D8 != 0 loses nothing functionally and stops the
// unwanted Label repaint.
//
// CORRECTION (tested after deploying the above): the flicker theory was
// wrong. launcher.log confirms real_SetTypeLabel is now skipped on every
// single click in mm+0x1D8!=0 mode (i.e. 100% of this file's/the native
// click handlers' (0x64FBC0/0x64FBF0/0x64FC20) painting-related code is
// bypassed), yet the flicker still happens. The +0x1D8!=0 skip above is
// still kept (it's a real, harmless fix for a real bug - the native
// mismatch it addresses is genuine, and skipping it changes nothing about
// +0x3D9, which the caller sets independently), but it is NOT what causes
// the flicker. The user independently confirmed the flicker is a
// pre-existing native engine rendering quirk unrelated to DDrawCompat: with
// DDraw.dll removed entirely, the same flicker still occurs (barely
// visible, but present; text also stops rendering correctly without
// DDraw.dll) - so this is not fixable from this hook, and not a DDraw.dll
// bug. Root cause is presumably in the Radio button widget's own native
// click/repaint path, not in SetTypeLabel/Intro_Label/Intro_Edit at all.
// Left as a known cosmetic issue per user decision (2026-09-02) since
// registration itself functions correctly.
#include "stdafx.h"
#include "MatchMakingHook.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include "detours.h"

#pragma comment(lib, "detours.lib")

namespace {

// Each translation unit in this project owns its own tiny logger (see NetLog
// in DisconnectHook.cpp, HfLog in HitFlinchPatch.cpp) - the
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

// VA 0x4021B0: LookupString(ecx=stringTableObj, id) -> ANSI char* (Big5).
// Confirmed-working call site already in this codebase's own reverse-
// engineering (same helper C_SendLocation-side briefs reference); string
// table singleton lives at fixed VA 0xC2D0B0.
typedef char *(__thiscall *LookupStringFn)(void *stringTable, unsigned id);
const LookupStringFn LookupString = (LookupStringFn)0x4021B0;
void *const g_StringTable = (void *)0xC2D0B0;

// Generic "get Nth child widget" vtable-slot callers - MatchMakingUI itself
// uses vtable+0x50 to fetch a top-level child window by index; the widgets
// below that (MatchRegister_Window, Intro_Control) use vtable+0x3C for the
// same purpose at their own level (confirmed via disassembly of both
// 0x64F700 and 0x64CCD0's own bodies - these are two different
// classes/vtables, not the same slot reused).
typedef void *(__thiscall *GetChildFn)(void *self, int index);

void *GetTopChild(void *mm, int index) {
  void **vt = *reinterpret_cast<void ***>(mm);
  return reinterpret_cast<GetChildFn>(vt[0x50 / 4])(mm, index);
}

void *GetChild(void *widget, int index) {
  void **vt = *reinterpret_cast<void ***>(widget);
  return reinterpret_cast<GetChildFn>(vt[0x3C / 4])(widget, index);
}

// vtable+0x94: MultiEdit::SetText(ansiText) - same slot ChangeLabel's own
// core (0x64CCD0) writes this+0x1D9 through when redisplaying a saved
// intro.
typedef void(__thiscall *SetTextFn)(void *editWidget, const char *ansi);

void FillIntroEdit(void *mm, const char *text) {
  void *wnd = GetTopChild(mm, 2); // MatchRegister_Window
  if (!wnd) {
    MmLog("FillIntroEdit: no MatchRegister_Window (window not open?)");
    return;
  }
  void *introCtrl = GetChild(wnd, 1); // Intro_Control
  if (!introCtrl) {
    MmLog("FillIntroEdit: no Intro_Control");
    return;
  }
  void *edit = GetChild(introCtrl, 0); // Intro_Edit (child 0; Intro_Label is child 1)
  if (!edit) {
    MmLog("FillIntroEdit: no Intro_Edit");
    return;
  }
  void **editVt = *reinterpret_cast<void ***>(edit);
  reinterpret_cast<SetTextFn>(editVt[0x94 / 4])(edit, text);
}

typedef void(__thiscall *SetTypeLabelFn)(void *mm, int type);
SetTypeLabelFn real_SetTypeLabel = (SetTypeLabelFn)0x64F700;

// MSVC won't let a free function be defined with __thiscall directly -
// __fastcall's first two params land in ECX/EDX, binary-compatible with how
// a thiscall caller puts `this` in ECX; the unused edx parameter just
// absorbs whatever thiscall didn't put there (same trick used in
// HitFlinchPatch.cpp's Hook_ShouldSkipFlinch).
void __fastcall Hook_SetTypeLabel(void *mm, void * /*edx*/, int type) {
  // mm+0x1D8 != 0 means this window already has previously-saved intro text
  // loaded (the "修正已登錄" / FixRegister re-edit path, per the brief
  // §1.3/§2.1: 0x64CAF0 only takes the "hide Label, show+fill Edit from
  // +0x1D9" branch when this byte is non-zero) - Intro_Edit is the real,
  // active display in this mode and Intro_Label is supposed to stay hidden.
  // Full disassembly of 0x64F700 confirms it never checks this flag itself
  // and always repaints Label's preview text/color regardless - skip
  // calling it at all here so that unwanted repaint (the actual flicker
  // root cause) never happens. +0x3D9 (the type Register reads) is already
  // set by the caller before this hook runs, so nothing functional is lost.
  BYTE hasSavedIntro = *(reinterpret_cast<BYTE *>(mm) + 0x1D8);
  if (hasSavedIntro != 0) {
    return;
  }

  real_SetTypeLabel(mm, type); // fresh registration: let official code update Intro_Label as normal

  // type is a signed char in the real object (-1 = nothing selected yet).
  // Only 0/1/2 (Killer/Hunter/Talker) have a real string to fill in; leave
  // Intro_Edit alone otherwise (brief §3.1: do NOT write the -1/"$3307
  // please pick a type" placeholder into Edit).
  if (type != 0 && type != 1 && type != 2) {
    return;
  }

  const unsigned id = (type == 0) ? 3285u : (type == 1) ? 3256u : 3284u;
  char *s = LookupString(g_StringTable, id);
  if (!s || !s[0]) {
    MmLog("LookupString(%u) empty, skip", id);
    return;
  }

  FillIntroEdit(mm, s);
}

} // namespace

void InstallMatchMakingHook() {
  const BYTE expected[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10};
  if (memcmp((void *)0x64F700, expected, sizeof(expected)) != 0) {
    MmLog("0x64F700 prologue mismatch, skipping install");
    return;
  }

  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  DetourAttach(&(PVOID &)real_SetTypeLabel, reinterpret_cast<PVOID>(Hook_SetTypeLabel));
  LONG result = DetourTransactionCommit();
  MmLog("SetTypeLabel hook install result=%ld", result);
}
