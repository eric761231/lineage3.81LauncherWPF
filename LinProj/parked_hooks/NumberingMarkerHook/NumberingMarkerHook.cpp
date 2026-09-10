// NumberingMarkerHook.cpp: see NumberingMarkerHook.h.
//
// This is "Feature A" only (the on/off switch) from
// docs/hooks/NUMBERING_MARKER_TOGGLE_BRIEF.md (LinBin3.81 project, revised
// 2026-08-29) - the party-leader-attack-target broadcast piece ("Feature B":
// hook the leader's own attack-target-changed instruction, send objid to the
// server, server implements L1Party.updateLeaderAttackTarget to broadcast it
// to the whole party via a new packet, every member's client writes its own
// slot on receipt) is NOT implemented here yet - it needs its own hook(s)
// plus real server-side (Java) work, and the brief itself flags the client
// "attack target changed" instruction as not yet located in the dump.
//
// Root cause: the hotbar action "Action_NumberingMarker" (icon 5582) is
// wired to VA 0x62EC90, which does mouse target-selection (call 0x62BDE0 ->
// 0x735940 -> 0x7354F0, writing L1Helper's slot 0/1/2) - this is "click a
// person, mark them" (whoever you personally click), a completely different
// feature from "the party leader's current attack target, shown to the
// whole party." An earlier version of this hook kept calling that chain to
// force an immediate local redraw on toggle-ON; the revised brief explicitly
// rules that out (it would draw on the wrong thing - your mouse selection,
// not the leader's attack target) and it's been removed. The server already
// has a real, working handler for the plain on/off signal -
// C_SendLocation.java's PARTY_ATTACK_TARGET_MARK=50 case, decrypt.length<=3
// branch, readC()!=0 -> applyPartyLeaderTargetMark - but nothing on the
// client ever sent that short packet. This hook fully replaces 0x62EC90's
// body so clicking the button toggles a flag, clears this client's own
// L1Helper slots on OFF, and sends that short packet.
//
// Reference pattern: VA 0x62EC10 (Action_BrodcastToPledge, a real working
// toggle-style Action handler) does exactly this shape, confirmed via
// disassembly of dumps/game/game_TW13081901_20260827_01.dmp:
//   mov  eax, [ebp+8]          ; eax = Action component (the button)
//   call 0x402830               ; eax = global settings object
//   add  eax, 0x293             ; eax = &settings->broadcastToPledgeFlag
//   push eax
//   mov  ecx, [ebp-4]           ; ecx = component
//   call 0x62B980                ; component->SyncButtonVisual(&flag) -> AL
//   ...AL==0 means "not yet synced this click" -> read+flip+write the byte
// We reuse the SyncButtonVisual half of this mechanism (see g_markEnabled's
// own comment further down for why the flag byte itself is NOT borrowed
// from the same settings object BrodcastToPledge uses - two different
// offsets there both turned out to already be in use by something else).
//
// L1Helper note (revised brief corrected this vs the first draft): 0xC30D40
// is the object's address itself, NOT a pointer to it - 0x735930/0x735940
// load it as `mov eax, 0xC30D40`, not `mov eax, [0xC30D40]`. So the mark
// slots are flat globals at 0xC30D40+0x2C/+0x30/+0x34 (dword [0xC30D6C] for
// slot 0), never dereferenced through [0xC30D40] first.
#include "stdafx.h"
#include "NumberingMarkerHook.h"
#include <cstring>
#include <cstdarg>
#include <cstdint>
#include "detours.h"

#pragma comment(lib, "detours.lib")

namespace {

// Each translation unit in this project owns its own tiny logger (see
// NetLog in DisconnectHook.cpp/MimirPowerHook.cpp, ImGuiLog in
// ImGuiHook.cpp) - the launcherdll_net_log in LauncherDll.cpp is `static`
// (internal linkage) and isn't linkable from here.
void NmLog(const char *fmt, ...) {
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
  fprintf(fp, "[%04d-%02d-%02d %02d:%02d:%02d.%03d][PID=%u][TID=%u] [NumberingMarker] %s\n",
          st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
          st.wMilliseconds, (unsigned)GetCurrentProcessId(), (unsigned)GetCurrentThreadId(), msg);
  fflush(fp);
  fclose(fp);
}

// VA 0x62B980: thiscall(component, &flagByte) -> AL. Syncs the button's lit/
// unlit visual with *flagByte and reports whether this click still needs
// processing (AL==0) or was already handled (AL!=0) - same call BrodcastToPledge
// (0x62EC10) uses on its own flag byte.
typedef BYTE(__thiscall *SyncButtonVisualFn)(void *component, BYTE *flagAddr);
const SyncButtonVisualFn SyncButtonVisual = (SyncButtonVisualFn)0x62B980;

// VA 0x580E50: SendPacketData(const char *format, ...) - cdecl, one stack
// slot per format character regardless of width ('c'=byte, 's'=string ptr,
// 'd'=dword). Confirmed-working call site already in this codebase: see
// LauncherDll.cpp's Login77 naked-asm ("cssddddddd", 0x77, id, pwd, ...).
typedef void(__cdecl *SendPacketDataFn)(const char *format, ...);
const SendPacketDataFn SendPacketData = (SendPacketDataFn)0x580E50;

// Official toggle text is NOT 0x583DE0(0xB06): that helper returns immediately
// when [0x9683B8]==0 (live log: always 0). BrodcastToPledge shows 「關閉」／
// 「開啟」 on the Action button itself: XML builder 0x62CE6C only creates the
// ON_OFF child when Action+8==0 (control=toggle), then paint 0x62D287 calls
// 0x62B9B0 and writes child+0xE4 = 5208 (on) / 5209 (off). NumberingMarker
// was built as control=target so it never got that child. We attach one to
// the clicked ActionUI button (grid from 0x62E3D0) and write +0xE4 ourselves.
// Do not flip Action+8 to 0 (native 0x62B870 / paint would then assume the
// child exists on every copy of this Action, including hotbar).
typedef void *(__thiscall *UiVecGetFn)(void *vec, int index);
const UiVecGetFn UiVecGet = (UiVecGetFn)0x626C30;
typedef void *(__cdecl *UiAllocFn)(int size);
const UiAllocFn UiAlloc = (UiAllocFn)0x79A150;
typedef void *(__thiscall *UiWidgetCtorFn)(void *mem);
const UiWidgetCtorFn UiWidgetCtor = (UiWidgetCtorFn)0x63A9E0;
constexpr DWORD kOnOffGfxOn = 0x1458;  // 5208
constexpr DWORD kOnOffGfxOff = 0x1459; // 5209
#define kOnOffName ((const char *)0x8E1E2C)

void *g_actionUiGrid = nullptr;
void *g_onOffWidget = nullptr;

typedef void(__thiscall *ActionUiClickFn)(void *self, void *grid);
ActionUiClickFn real_ActionUiClick = (ActionUiClickFn)0x62E3D0;

void __fastcall Hook_ActionUiClick(void *self, void * /*edx*/, void *grid) {
  if (grid) {
    g_actionUiGrid = grid;
  }
  real_ActionUiClick(self, grid);
}

void *ActionUiButtonAt(void *grid, int index) {
  if (!grid || index < 0 || index > 256) {
    return nullptr;
  }
  void *slot = UiVecGet((BYTE *)grid + 0x120, index);
  if (!slot) {
    return nullptr;
  }
  return *(void **)slot;
}

void *VtCall1(void *obj, int vtOff, void *a0) {
  void **vt = *(void ***)obj;
  typedef void *(__thiscall *Fn)(void *, void *);
  return ((Fn)vt[vtOff / 4])(obj, a0);
}

void VtCall3(void *obj, int vtOff, int a0, int a1, int a2) {
  void **vt = *(void ***)obj;
  typedef void(__thiscall *Fn)(void *, int, int, int);
  ((Fn)vt[vtOff / 4])(obj, a0, a1, a2);
}

void ApplyOnOffGfx(void *widget, BYTE enabled) {
  if (!widget) {
    return;
  }
  *(DWORD *)((BYTE *)widget + 0xE4) = enabled ? kOnOffGfxOn : kOnOffGfxOff;
  *(DWORD *)((BYTE *)widget + 0xDC) = 1;
}

void EnsureOnOffOnClickedButton(BYTE enabled) {
  if (g_onOffWidget) {
    ApplyOnOffGfx(g_onOffWidget, enabled);
    return;
  }
  if (!g_actionUiGrid) {
    NmLog("ON_OFF skip: no ActionUI grid yet (open Action window once, or click there)");
    return;
  }
  int index = *(int *)((BYTE *)g_actionUiGrid + 0x140);
  void *button = ActionUiButtonAt(g_actionUiGrid, index);
  if (!button) {
    NmLog("ON_OFF skip: no button at grid+0x140 index=%d", index);
    return;
  }
  void *mem = UiAlloc(0x100);
  if (!mem) {
    return;
  }
  void *w = UiWidgetCtor(mem);
  if (!w) {
    return;
  }
  VtCall1(w, 0x5C, (void *)kOnOffName);
  ApplyOnOffGfx(w, enabled);
  VtCall1(w, 0x28, button);
  VtCall3(w, 0x58, 0, 1, 1);
  VtCall1(button, 0x2C, w);
  g_onOffWidget = w;
  NmLog("ON_OFF attached button=%p widget=%p gfx=%u", button, w,
        enabled ? kOnOffGfxOn : kOnOffGfxOff);
}

typedef void(__cdecl *ActionHandlerFn)(void *component);
ActionHandlerFn real_ActionNumberingMarker = (ActionHandlerFn)0x62EC90;

// VA 0x62B870: the SHARED Action-click dispatcher every hotbar/Action button
// goes through (thiscall(component), confirmed via dump disassembly - "push
// ecx; mov [ebp-4],ecx" prologue, not [ebp+8]). Reads a DWORD "control type"
// field at component+8 (0=toggle/1=action -> call [component+0x34]
// immediately, THEN unconditionally also calls 0x62B950(component) and
// 0x62B960(component) as more of the "genuine toggle-type Action" dance;
// 2=target -> arm cursor-select mode instead: writes [0xC2FA14]=component,
// calls 0x404640(0x62BC40, 0x1E, 0xB3) which shows the target-select cursor
// and defers the real call to [component+0x34] until the player actually
// clicks something). This is why hooking 0x62EC90 alone (component+0x34 for
// this Action) can't stop the cursor - by the time 0x62EC90 runs, the
// cursor has already been armed/shown by THIS function on a separate,
// earlier click.
//
// First attempt here patched this Action's own +8 field to 0 in place, then
// called straight through to the real 0x62B870 so its native +8==0 branch
// would do the rest (the same path BrodcastToPledge's genuine toggle-type
// Action already takes safely). That crashed the game within ~1 second of
// clicking (confirmed via launcher.log timestamps lining up with
// LaunchGame's "遊戲行程結束" a moment after "fixing ... control type +8
// from 2 to 0"). Root cause: 0x62B960 does `ecx = component+0x38; call
// 0x67F5506E` - 0x67F5506E resolves outside the game's own module (a CRT/
// STL-style helper in some other loaded DLL), meaning it treats
// component+0x38 as a properly-constructed sub-object. A real control=
// "toggle" Action gets that (and whatever 0x62B950 touches) set up correctly
// by the XML/Action-list loader at startup; NumberingMarker's Action object
// was built as control="target" and never went through that setup, so
// component+0x38 is unrelated leftover data - not a valid object for
// 0x67F5506E to operate on. Forcing +8=0 makes the native dispatcher run
// that path anyway and it crashes on garbage.
//
// Fix: don't delegate to the native dispatcher for our own Action at all.
// When component+0x34 is our own 0x62EC90 (i.e. this click is
// NumberingMarker - every other Action's +0x34 points elsewhere and takes
// the real, unmodified 0x62B870 exactly as before), call the handler
// directly ourselves and return, skipping both the target-cursor branch AND
// the 0x62B950/0x62B960 calls the +8==0 branch would otherwise also trigger.
typedef void(__thiscall *ActionDispatchFn)(void *component);
ActionDispatchFn real_ActionDispatch = (ActionDispatchFn)0x62B870;

void __fastcall Hook_ActionDispatch(void *component, void * /*edx*/) {
  if (component) {
    void *handler = *(void **)((BYTE *)component + 0x34);
    if (handler == (void *)0x62EC90) {
      // Do NOT write component+8 to 0. 0x62D287 *does* treat +8==0 as
      // toggle and then writes ON_OFF child's +0xE4 from 0x62B9B0 — but
      // this Action was built as target and has no ON_OFF child, so
      // flipping +8 makes that paint path dereference garbage. Overlay is
      // attached separately (EnsureOnOffOnClickedButton).
      ((ActionHandlerFn)handler)(component);
      return;
    }
  }
  real_ActionDispatch(component);
}

// L1Helper's fixed instance address and this feature's 3 mark slots (flat
// globals, NOT dereferenced through [0xC30D40] - see file header). -1 means
// "empty". Feature B (not implemented here) will eventually write real
// objids into slot 0 (L1HELPER_BASE + kSlot0Offset); this file only ever
// clears them (on toggle-OFF).
constexpr uintptr_t kL1HelperBase = 0xC30D40;
constexpr int kSlot0Offset = 0x2C;
constexpr int kSlot1Offset = 0x30;
constexpr int kSlot2Offset = 0x34;

void ClearOwnMarkSlots() {
  *(int32_t *)(kL1HelperBase + kSlot0Offset) = -1;
  *(int32_t *)(kL1HelperBase + kSlot1Offset) = -1;
  *(int32_t *)(kL1HelperBase + kSlot2Offset) = -1;
}

// Flag storage for this toggle. Two different offsets borrowed from the
// game's own settings object (+0x2C0, then +0x310 - the latter picked via a
// full-binary scan of every call site to the settings-object getter, 422 of
// them, looking for the largest gap with zero references) both turned out
// to already be in live use by something else (launcher.log: "looks
// occupied (current byte=0x14/0xF7 ...)" both times, caught safely by a
// sanity check before either could be corrupted). Scanning for a genuinely
// free byte in a ~1KB struct referenced from 400+ call sites project-wide
// isn't reliable enough to keep guessing at. NumberingMarker was never a
// real toggle-type Action in the first place (see the dispatch-hook comment
// above), so there was never an "official" slot for this flag to begin
// with - own storage entirely in this DLL's data segment side-steps the
// whole problem instead of trying to find an unclaimed byte in someone
// else's struct. SyncButtonVisual (0x62B980) just needs a stable, writable
// byte address; nothing requires it to live inside the settings object.
BYTE g_markEnabled = 0;

constexpr BYTE kOpcodeSendLocation = 254;      // C_OPCODE_SENDLOCATION
constexpr BYTE kTypePartyAttackTargetMark = 50; // C_SendLocation.PARTY_ATTACK_TARGET_MARK

void __cdecl Hook_ActionNumberingMarker(void *component) {
  BYTE *flagAddr = &g_markEnabled;

  // Official 0x62EC10 skips the flip when 0x62B980 returns 1 because XML
  // load already bound +0x54 via 0x62B870(arg=1). We never get that bind
  // (control was target), so treating return-1 as "ignore this click"
  // eats the first press and never updates the button text. Bind, then
  // always flip.
  SyncButtonVisual(component, flagAddr);

  BYTE newVal = (*flagAddr == 0) ? 1 : 0;
  *flagAddr = newVal;

  if (newVal == 0) {
    // OFF: stop showing anything on this client immediately, regardless of
    // whether Feature B (not implemented yet) had written a real objid here.
    ClearOwnMarkSlots();
  }
  // ON: no client-side draw here - that only happens once Feature B's
  // "attack target changed" hook writes a real objid into slot 0. Toggling
  // this switch alone has nothing to draw yet.

  // "ccc" (3-byte body: opcode+type+flag) landed as a 4-byte packet on the
  // wire instead (server log: len=4, trailing byte varying/uninitialized-
  // looking) - SendPacketData appears to pad short bodies, and the extra
  // byte pushed C_SendLocation.java's PARTY_ATTACK_TARGET_MARK case out of
  // its decrypt.length<=3/readC() branch and into the <=6/readD() branch
  // instead, reading past our intended payload. It happened to still decode
  // right only because our flag sat in the low byte of the resulting DWORD.
  // Send a real, fully-determined DWORD instead ("ccd": opcode+type+4-byte
  // flag = 6 bytes) so it deliberately lands in the <=6/readD() branch with
  // no reliance on whatever padding byte(s) SendPacketData adds.
  SendPacketData("ccd", (int)kOpcodeSendLocation, (int)kTypePartyAttackTargetMark, (int)newVal);
  EnsureOnOffOnClickedButton(newVal);
  NmLog("toggled flag=%d, sent opcode=%d type=%d, onOff=%p", newVal, kOpcodeSendLocation,
        kTypePartyAttackTargetMark, g_onOffWidget);
}

} // namespace

void InstallNumberingMarkerHook() {
  // 2026-08-30: Feature A client hook withdrawn. 5582 goes back to stock
  // 0x62EC90 (mouse pick + 0x7354F0 slots + official C 254/50 blob). Do not
  // DetourAttach 0x62EC90 / 0x62B870 / 0x62E3D0. Hook body stays in this
  // file for later; remaining product (leader-target / party sync) undecided.
  NmLog("disabled: using native Action_NumberingMarker 0x62EC90");
  return;

  // 55 8B EC E8 48 D1 FF FF = push ebp; mov ebp,esp; call 0x62BDE0 (rel32 to
  // the original target-select helper). Re-read directly from the dump via
  // verify_addresses.Minidump.read(0x62EC90, 8) to confirm this exact value -
  // an earlier hand-computed rel32 (48 FE FF FF) was wrong, which made this
  // check fail on every single install attempt (confirmed via launcher.log:
  // "0x62EC90 prologue mismatch, skipping install" on every launch since
  // this hook was first deployed) - the hook was never actually installed,
  // so the stock target-select behavior kept running unmodified.
  const BYTE expected[8] = {0x55, 0x8B, 0xEC, 0xE8, 0x48, 0xD1, 0xFF, 0xFF};
  if (memcmp((void *)0x62EC90, expected, sizeof(expected)) != 0) {
    NmLog("0x62EC90 prologue mismatch, skipping install");
    return;
  }

  // 55 8B EC 51 89 4D FC C7 05 30 F3 AB 00 00 00 00 00 = push ebp; mov
  // ebp,esp; push ecx; mov [ebp-4],ecx; mov dword [0xABF330],0 (start of the
  // shared per-click housekeeping). Read directly from the dump the same
  // way, not hand-computed.
  const BYTE expectedDispatch[17] = {0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0xC7, 0x05,
                                     0x30, 0xF3, 0xAB, 0x00, 0x00, 0x00, 0x00, 0x00};
  bool dispatchOk = memcmp((void *)0x62B870, expectedDispatch, sizeof(expectedDispatch)) == 0;
  if (!dispatchOk) {
    NmLog("0x62B870 prologue mismatch, skipping dispatch hook (select-cursor fix won't apply, "
          "toggle itself still installs)");
  }

  const BYTE expectedUiClick[8] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x89, 0x4D};
  bool uiClickOk = memcmp((void *)0x62E3D0, expectedUiClick, sizeof(expectedUiClick)) == 0;
  if (!uiClickOk) {
    NmLog("0x62E3D0 prologue mismatch, ON_OFF attach may not find the button");
  }

  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  DetourAttach(&(PVOID &)real_ActionNumberingMarker, reinterpret_cast<PVOID>(Hook_ActionNumberingMarker));
  if (dispatchOk) {
    DetourAttach(&(PVOID &)real_ActionDispatch, reinterpret_cast<PVOID>(Hook_ActionDispatch));
  }
  if (uiClickOk) {
    DetourAttach(&(PVOID &)real_ActionUiClick, reinterpret_cast<PVOID>(Hook_ActionUiClick));
  }
  LONG result = DetourTransactionCommit();
  NmLog("Action_NumberingMarker hook install result=%ld (dispatch %s, uiClick %s)", result,
        dispatchOk ? "attached" : "skipped", uiClickOk ? "attached" : "skipped");
}
