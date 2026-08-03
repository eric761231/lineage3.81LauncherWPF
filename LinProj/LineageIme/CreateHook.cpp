// CreateHook.cpp: 對照 Rust 版 ime_overlay/src/create_hook.rs（Tier 1 範圍）。
//
// Win10/11 32-bit 的 user32!CreateWindowExA/W 通常以「hot-patch prologue」開頭
// （8B FF 55 8B EC = mov edi,edi; push ebp; mov ebp,esp，共 5 bytes），剛好給
// 我們塞 5-byte JMP rel32。做法：
//   1. 配置 trampoline（原 5 bytes + JMP 回 target+5），讓我們能呼叫「原函式」
//   2. 把 target 前 5 bytes 改成 JMP rel32 → 我們的 detour
// 開頭如果不是預期的 5 bytes 就 bail（可能被別人 hook，或 prologue 形式改了）。
#include "Common.h"
#include "CreateHook.h"
#include "Dbg.h"
#include <imm.h>
#include <atomic>

#pragma comment(lib, "imm32.lib")

namespace {

const BYTE EXPECTED_PROLOGUE[5] = {0x8B, 0xFF, 0x55, 0x8B, 0xEC};
const wchar_t *TARGET_CLASS_W = L"LUnicodeEdit";
const char *TARGET_CLASS_A = "LUnicodeEdit";

std::atomic<bool> g_tsfDisableTried{false};
std::atomic<UINT_PTR> g_trampolineA{0};
std::atomic<UINT_PTR> g_trampolineW{0};
std::atomic<size_t> g_hitCount{0};

typedef HWND(WINAPI *CreateWindowExAFn)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND,
                                        HMENU, HINSTANCE, LPVOID);
typedef HWND(WINAPI *CreateWindowExWFn)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND,
                                        HMENU, HINSTANCE, LPVOID);

// ImmDisableTextFrameService 文件規定必須在 thread 還沒 init TSF 之前呼叫，
// init 後呼叫可能無效；我們 inject 時間點通常在 init 之後，可能無效，但呼叫
// 便宜，先試一次，看 log 結果再決定下一步（跟 Rust 版同樣的保留態度）。
void TryDisableTsfOnce() {
  bool expected = false;
  if (!g_tsfDisableTried.compare_exchange_strong(expected, true)) return;
  DWORD tid = GetCurrentThreadId();
  BOOL r1 = ImmDisableTextFrameService(tid);
  BOOL r2 = ImmDisableTextFrameService((DWORD)-1); // process-wide
  IME_LOG("[ime] ImmDisableTextFrameService(tid=%lu) -> %d | (-1 process-wide) -> %d",
          (unsigned long)tid, r1 ? 1 : 0, r2 ? 1 : 0);
}

bool ClassMatchesA(LPCSTR p) {
  if ((UINT_PTR)p < 0x10000 || !p) return false;
  return _stricmp(p, TARGET_CLASS_A) == 0;
}
bool ClassMatchesW(LPCWSTR p) {
  if ((UINT_PTR)p < 0x10000 || !p) return false;
  return _wcsicmp(p, TARGET_CLASS_W) == 0;
}

HWND WINAPI HookedCreateWindowExA(DWORD exStyle, LPCSTR className, LPCSTR windowName, DWORD style,
                                  int x, int y, int w, int h, HWND parent, HMENU menu,
                                  HINSTANCE inst, LPVOID param) {
  TryDisableTsfOnce();
  if (ClassMatchesA(className)) {
    size_t n = ++g_hitCount;
    if (n <= 8) IME_LOG("[ime] CreateWindowExA LUnicodeEdit #%zu: ex 0x%08X", n, (unsigned)exStyle);
  }
  auto original = (CreateWindowExAFn)g_trampolineA.load();
  return original(exStyle, className, windowName, style, x, y, w, h, parent, menu, inst, param);
}

HWND WINAPI HookedCreateWindowExW(DWORD exStyle, LPCWSTR className, LPCWSTR windowName, DWORD style,
                                  int x, int y, int w, int h, HWND parent, HMENU menu,
                                  HINSTANCE inst, LPVOID param) {
  TryDisableTsfOnce();
  if (ClassMatchesW(className)) {
    size_t n = ++g_hitCount;
    if (n <= 8) IME_LOG("[ime] CreateWindowExW LUnicodeEdit #%zu: ex 0x%08X", n, (unsigned)exStyle);
  }
  auto original = (CreateWindowExWFn)g_trampolineW.load();
  return original(exStyle, className, windowName, style, x, y, w, h, parent, menu, inst, param);
}

// 安裝單一 hot-patch trampoline hook；成功回空字串。
std::string InstallOne(BYTE *target, void *detour, std::atomic<UINT_PTR> &trampolineSlot,
                       const char *tag) {
  BYTE prologue[5];
  memcpy(prologue, target, 5);
  if (memcmp(prologue, EXPECTED_PROLOGUE, 5) != 0) {
    char buf[160];
    sprintf_s(buf, "%s prologue not hot-patchable: %02X %02X %02X %02X %02X", tag, prologue[0],
              prologue[1], prologue[2], prologue[3], prologue[4]);
    return buf;
  }

  BYTE *trampoline = (BYTE *)VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
  if (!trampoline) return std::string(tag) + ": VirtualAlloc trampoline failed";

  memcpy(trampoline, target, 5);
  trampoline[5] = 0xE9; // JMP rel32
  INT32 rel = (INT32)((UINT_PTR)target + 5 - ((UINT_PTR)trampoline + 10));
  memcpy(trampoline + 6, &rel, 4);
  trampolineSlot.store((UINT_PTR)trampoline);

  DWORD oldProtect = 0;
  if (!VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
    return std::string(tag) + ": VirtualProtect RWX failed";
  }
  target[0] = 0xE9;
  INT32 relDetour = (INT32)((UINT_PTR)detour - ((UINT_PTR)target + 5));
  memcpy(target + 1, &relDetour, 4);
  DWORD dummy;
  VirtualProtect(target, 5, oldProtect, &dummy);

  IME_LOG("[ime] %s hooked: target=0x%p trampoline=0x%p detour=0x%p", tag, target, trampoline,
          detour);
  return "";
}

} // namespace

std::string InstallCreateWindowHook() {
  HMODULE user32 = GetModuleHandleA("user32.dll");
  if (!user32) return "GetModuleHandleA(user32.dll) failed";

  BYTE *targetA = (BYTE *)GetProcAddress(user32, "CreateWindowExA");
  BYTE *targetW = (BYTE *)GetProcAddress(user32, "CreateWindowExW");
  if (!targetA || !targetW) return "GetProcAddress(CreateWindowEx A/W) returned NULL";

  std::string err = InstallOne(targetA, (void *)HookedCreateWindowExA, g_trampolineA,
                               "CreateWindowExA");
  if (!err.empty()) return err;
  err = InstallOne(targetW, (void *)HookedCreateWindowExW, g_trampolineW, "CreateWindowExW");
  if (!err.empty()) return err;

  return "";
}
