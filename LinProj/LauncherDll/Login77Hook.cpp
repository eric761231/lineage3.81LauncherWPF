#include "stdafx.h"
#include "Login77Hook.h"
#include "LauncherDll.h"
#include "PatchUtil.h"

static BYTE g_id[32];
static BYTE g_pwd[32];
static int g_pwd_pos = 0;
static bool g_loginHooksInstalled = false;

const DWORD USER_HOOK_ADDR = 0x0077317D;
const DWORD USER_RETN_ADDR = 0x00773183;
const DWORD PASS_HOOK_ADDR = 0x004AA38E;
const DWORD PASS_RETN_ADDR = 0x004AA395;
const DWORD LOGIN77_HOOK_ADDR = 0x00772E07;
const DWORD LOGIN77_RETN_ADDR = 0x00772E77;
const DWORD LOGIN77_HOOK_SIZE = 10;
const DWORD SEND_PACKET_DATA = 0x00580E50;
static const char LOGIN77_FORMAT[] = "cssddddddd";

void Login77Hook_SetAccount(const void *id, const void *pwd) {
  memset(g_id, 0, 32);
  memset(g_pwd, 0, 32);
  if (id)
    memcpy(g_id, id, 32);
  if (pwd)
    memcpy(g_pwd, pwd, 32);
  g_id[31] = 0;
  g_pwd[31] = 0;
  g_pwd_pos = (int)strlen((const char *)g_pwd);
}

static void __stdcall UserNameHandler(void *p) {
  memcpy(g_id, p, 32);
  g_id[31] = 0;
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

static void __stdcall PasswordHandler(BYTE PassByte) {
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
    g_pwd[g_pwd_pos] = 0;
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

// 對齊 Rust login.rs（Login.dll 相容 opcode 0x77 / cssddddddd）
__declspec(naked) void Login77(void) {
  __asm {
		push 0x1F
		push 0
		push 0
		push 0
		push 0
		push 0
		push 0x0100007F
		lea eax, g_pwd
		push eax
		lea eax, g_id
		push eax
		push 0x77
		lea eax, LOGIN77_FORMAT
		push eax
		mov eax, SEND_PACKET_DATA
		call eax
		add esp, 0x2C
		mov g_pwd_pos, 0
		jmp LOGIN77_RETN_ADDR
  }
}

void InstallLogin77Hooks() {
  if (g_loginHooksInstalled)
    return;
  g_loginHooksInstalled = true;

  HookCode((void *)USER_HOOK_ADDR, (void *)GetUsername,
           USER_RETN_ADDR - USER_HOOK_ADDR);
  HookCode((void *)PASS_HOOK_ADDR, (void *)GetPassword,
           PASS_RETN_ADDR - PASS_HOOK_ADDR);
  HookCode((void *)LOGIN77_HOOK_ADDR, (void *)Login77, LOGIN77_HOOK_SIZE);

  launcherdll_hook_log("[Install] Login77 ok");
}
