#pragma once

// S_MapID 0x52C0C0：開時把 underwater byte 當 0，走官方陸地分支消藍。
// DelayedDetour／PatchThread 後 Install。PSS「海底抽水」→ SetEnabled（本機 cfg）。
void InstallUnderwaterPumpHook();
void UnderwaterPumpHook_SetEnabled(bool enabled);
bool UnderwaterPumpHook_IsEnabled();
// 遊戲主執行緒：立刻停／開 overlay（590D90／590BA0）。
void UnderwaterPumpHook_PumpPending();
