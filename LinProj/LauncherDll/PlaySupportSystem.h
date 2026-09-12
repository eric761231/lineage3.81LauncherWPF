// PlaySupportSystem.h: PacketBox 借位裡屬於遊玩輔助（PSS）的子類型分派。
// 密米爾之泉（子類型 16）仍在 MimirPowerHook；共用 cave 會把其餘 PSS 子類型
// 轉進來。SendPacketData / 開面板仍必須在遊戲主執行緒。
#pragma once

#include <windows.h>

// PacketBox 子類型（EAX @ 0x0053939A）。數值對齊 Java S 包，不可改。
constexpr DWORD kPssPacketBoxResolve = 0x20;     // 32 解析道具回覆
constexpr DWORD kPssPacketBoxVitals = 0x27;      // 39 HP/MP
constexpr DWORD kPssPacketBoxSlotCounts = 0x2E;  // 46 槽數量
constexpr DWORD kPssPacketBoxItemFilter = 0x2F;  // 47 刪除／溶解名單

// cave 呼叫：subtype 為 EAX。若為 PSS 子類型則解析並回傳非 0（吃掉這包）。
extern "C" DWORD __cdecl PlaySupportSystem_OnPacketBox(DWORD subtype,
                                                       const BYTE *pktData);
