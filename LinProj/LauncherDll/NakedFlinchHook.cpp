#include "stdafx.h"
#include "NakedFlinchHook.h"
#include <windows.h>

extern "C" bool __stdcall GetSuppressFlinch(int spriteId);

// 受擊硬直攔截（Flinch Hook）
__declspec(naked) void NakedFlinchHook() {
  __asm {
		pushad
		pushfd
		mov eax, [esi + 0x120] // eax = SpriteID
		push eax
		call GetSuppressFlinch
		test al, al            // 檢查回傳值（bool）
		jnz _SkipFlinch        // 若為 true，跳過受擊動作
		// 恢復受擊動作 (2 = DAMAGE)
		mov dword ptr [esi + 0xAC], 2
	_SkipFlinch:
		popfd
		popad
		// 跳回原程式（原始指令跳接點）
		mov eax, 0x1C01174
		jmp eax
  }
}
