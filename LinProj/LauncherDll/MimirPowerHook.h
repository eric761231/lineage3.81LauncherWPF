// MimirPowerHook.h: 密米爾之泉每日能力選擇。
//
// 走原生封包管道（伺服器端有 opcode 白名單，不能用自訂 opcode），但 S→C 方向的
// 內容是真的加密過的（Lineage Encryption，client 端沒辦法自己解密，這點已經
// 實測驗證過，見這次除錯過程），所以不能像 DisconnectHook 那樣在 socket 層攔截、
// 自己解碼——改成直接在遊戲本身「已經解密完、準備分派 opcode」的那個位置掛
// in-process hook，讀原生已經解密好的明文，見 MimirPowerHook.cpp 的說明。
#pragma once
#ifndef MIMIRPOWERHOOK_H_INCLUDED
#define MIMIRPOWERHOOK_H_INCLUDED
#include <windows.h> // SOCKET already in scope via includer's winsock2.h (see DisconnectHook.h)

// 固定 3 個選項（跟 Java 端 S_PledgeWatch(HashMap) 建構子的 for(i<3) 一致，不是動態筆數）。
constexpr int MIMIR_OPTION_COUNT = 3;

struct MimirOption {
  DWORD iconId;      // 選項編號（資料表 L1MimirWell.id）
  DWORD iconPngId;   // 圖片編號（資料表 L1MimirWell.icon_pngid），client 用這個查圖示
  char name[64];     // 選項名稱（短標題，卡片列表跟詳情卡都會顯示）
  char desc[192];    // 能力敘述（只有詳情卡顯示，卡片列表不顯示）
  char pvpText[16];  // PVP 適用範圍（"適用"/"專屬"），後端算好直接送字串，client 端
                      // 不用自己判斷規則；其餘統計欄位（效果類型/來源/持續時間/是否
                      // 可疊加/冷卻時間）改成 mimir_ui.xml 每張卡各自寫死，不走封包。
};

// 在 code 解密完成、DetourTransactionCommit 之後呼叫一次（比照
// InstallDisconnectHooks 的時機），安裝 in-process hook。
void InstallMimirPowerHook();

// 記錄目前遊戲行程實際在用的連線 socket，供 MimirPowerHook_SendChoice 主動送出
// 偽裝封包用。my_connect 每次連線都要更新。
void MimirPowerHook_SetSocket(SOCKET s);

// 玩家在自訂 UI 上按下確認後呼叫：index 是選到第幾個選項（0~2），偽裝成
// C_CheckPK（opcode 51）送回伺服器，Java 端用長度分辨這不是真的 /checkpk 查詢。
// 注意：這個函式本身只記一個「待送出」旗標就馬上回傳，不會真的加密/送出——
// 見 MimirPowerHook_PumpPendingChoice。
void MimirPowerHook_SendChoice(BYTE index);

// 真正執行加密＋送出的地方。原生加密函式借用的那個記憶體位址只被遊戲自己的網路
// 執行緒動過，從我們另外開的 overlay 執行緒直接處理過金鑰會有問題，所以改成
// my_send（穩定跑在遊戲主執行緒上）每次送完真封包後呼叫這個函式，有待送出的
// 選擇才在這裡真正處理。內部呼叫 MimirSendEncoded（見 LauncherDll.cpp）而不是
// send()，避免鉤子巢狀呼叫自己（實測撞過兩種不同的當機，都是巢狀呼叫造成的）。
// 第二個呼叫點：HookProc（LauncherDll.cpp 的 WH_GETMESSAGE hook）也會呼叫這個
// 函式——觸發頻率遠高於 send()（每次遊戲主迴圈收到訊息就觸發一次，不用等下一
// 個真封包送出），但一樣穩定跑在同一條遊戲主執行緒上，不需要新的同步機制，加了
// 這個呼叫點是為了讓玩家按確認後幾乎立即送出，不用等到下一次心跳封包（可能要
// 等幾十秒）。
void MimirPowerHook_PumpPendingChoice();

// 密米爾之泉專用送出函式：外層 XOR 編碼＋直接呼叫 real_send，不經過 send()（不會
// 觸發 my_send 鉤子，避免遞迴）。實作在 LauncherDll.cpp（需要那邊的 ShareInfo/
// _xorByte/nextRand()/SendLockGuard 等狀態）。
int MimirSendEncoded(SOCKET s, const BYTE *body, int len);

#endif // MIMIRPOWERHOOK_H_INCLUDED
