#include "stdafx.h"
#include "NakedLocomotionHook.h"
#include <windows.h>

// 移動動畫修正（Locomotion Hook - 影子拼接版）
// 目的：讓 3.81 客戶端在播放 Action 0 (walkR) 時，
// 透過攔截 4087/4088 事件，來達成與 Action 4 (walkL) 的平滑對接。
__declspec(naked) void NakedLocomotionHook() {
  __asm {
        pushad
        
        // ESI = CObject/CPlayer 指標
        // [ESI + 0xAC] = Action ID
        // [ESI + 0xB0] = Frame ID
        
        mov eax, [esi + 0xAC]
        cmp eax, 0
        jne _CheckAction4
        
        // 檢查是否触发了左腳事件 (4088)
        // (註：實際事件檢測通常在 0x48A0F0，此處主要處理動作狀態維護)
        
    _CheckAction4:
        cmp eax, 4
        jne _ExitHook
        
        // 如果目前是 Action 4，且我們已經透過 GetAction Hook 修復了資料獲取，
        // 則這裡只需要確保影格更新不會超出 Action 4 的範圍。

    _ExitHook:
        popad

        // 原始指令執行的位址與內容 (PUSH EBP; MOV EBP, ESP)
		push ebp
		mov ebp, esp
		mov eax, 0x489672
		jmp eax
  }
}
