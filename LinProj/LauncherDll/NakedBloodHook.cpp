#include "stdafx.h"
#include "NakedBloodHook.h"
#include <windows.h>

extern "C" int __stdcall GetBloodEffect(int spriteId);

// 血液特效替換（Blood Effect Hook）
__declspec(naked) void NakedBloodHook() {
  __asm {
		pushad
		mov eax, [esi + 0x120] // 取出 SpriteID
		push eax
		call GetBloodEffect
		mov [esp + 0x1C], eax  // 覆蓋 pushad 暫存的 eax 位置（最後 pop 時還原）
		popad
		push eax               // 推入新的特效 ID
		// 跳回原程式（原始指令跳接點）
		mov eax, 0x449D2C
		jmp eax
  }
}
