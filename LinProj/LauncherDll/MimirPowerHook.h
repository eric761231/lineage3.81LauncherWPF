// MimirPowerHook.h: 密米爾之泉每日能力選擇，偽裝走原生 S_OPCODE_HIRESOLDIERLIST
// (132) / C_HireSoldier 封包通道（伺服器端有 opcode 白名單，不能用自訂 opcode）。
#pragma once
#ifndef MIMIRPOWERHOOK_H_INCLUDED
#define MIMIRPOWERHOOK_H_INCLUDED
#include <windows.h> // SOCKET already in scope via includer's winsock2.h (see DisconnectHook.h)

// 固定 3 個選項（跟 Java 端 S_HireSoldierList 建構子的 for(i<3) 一致，不是動態筆數）。
constexpr int MIMIR_OPTION_COUNT = 3;

struct MimirOption {
  DWORD iconId;   // 選項圖示編號（Java 端 writeD，client 用這個查 mimir_ui.xml 的 <Icon iconId="..."/>）
  char name[64];  // 選項名稱（短標題，卡片列表跟詳情卡都會顯示）
  char desc[192]; // 能力敘述（只有詳情卡顯示，卡片列表不顯示）
};

// 玩家在 client 上按確認前呼叫,不需回傳值。
void MimirPowerHook_SetSocket(SOCKET s);

// 在 my_recv 裡、real_recv 成功回傳（ret > 0）之後、任何其他邏輯處理 buf 之前呼叫。
// 內部維護一份跟 DisconnectHook.cpp 同一套（已驗證安全）的長度前綴封包重組緩衝區，
// 只在完整、對齊過的封包上判斷是不是偽裝的密米爾封包，不會誤吃其他封包的中段資料。
// 命中時會把該封包從 buf/ret 整段移除（native ProcessPacket 永遠看不到它），並觸發
// MimirPowerOverlay_Show 顯示自訂清單。
void MimirPowerHook_OnRecv(unsigned char *buf, int &ret);

// 玩家在自訂 UI 上按下確認後呼叫：objid 原封不動回傳收到 S 包時的那個值（玩家自己的
// 編號，client 不需要理解它的意義，單純回傳），index 是選到第幾個選項（0~2）。
void MimirPowerHook_SendChoice(DWORD objid, BYTE index);

// 比照 DisconnectHook_ResetSession，新連線時重置重組緩衝區等狀態。
void MimirPowerHook_ResetSession();

#endif // MIMIRPOWERHOOK_H_INCLUDED
