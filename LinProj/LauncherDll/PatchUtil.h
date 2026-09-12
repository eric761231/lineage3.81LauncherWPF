#pragma once
#include <windows.h>

// 解殼閘門：0x0058788B 已變成預期 opcode（或已套過 GetFileData hook）。
bool IsCodeDecrypt();

void PatchCode(void *addr, const void *code, int len);
void HookCode(void *addr, void *func, int len);
