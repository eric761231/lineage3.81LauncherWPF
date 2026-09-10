// AutoPotionOverlay.h: 遊玩輔助 layered 視窗（分頁：BUFF / 道具 / 返回）。
// BUFF 頁四宮格對齊參考截圖；恢復道具（治療／補魔）為可操作區。
#pragma once

// Toggle 顯示／隱藏。HOME 熱鍵呼叫（見 LauncherDll.cpp HookProc）。
void AutoPotionOverlay_Show();

// 遊戲主執行緒呼叫：若 UI 執行緒排了「儲存」，在此寫檔＋送封包。
void AutoPotionOverlay_PumpPendingSave();

// 2026-09-07：拖曳落地驗證用（結論見 docs/AutoPotionOverlay_點選道具計畫.md
// 第 0.4 節——遊戲拖曳沒有 SetCapture，這個函式對「背包拖曳到我們視窗」這個
// 案例其實用不到，保留給其他可能的座標命中判斷用途）。
bool AutoPotionOverlay_HitTestSlot(int screenX, int screenY, int *outSection,
                                   int *outIndex);

// 2026-09-08：「點格子→點背包道具」選道具流程，見計畫文件。
//
// 遊戲主執行緒（HookProc）在偵測到玩家點擊時呼叫，查詢目前 overlay 是不是在
// 等待某個槽位的道具選擇。overlay 不可見或沒有在選擇中時回傳 false。
bool AutoPotionOverlay_IsPicking(int *outSection, int *outSlot);

// PacketBox 借位子類型 32 的回覆抵達時呼叫（見 MimirPowerHook.cpp 的
// OnAutoPotionResolveDispatch）。success=false 時 templateItemId/gfxid/count/
// name 會被忽略。同樣會自行比對 section/slot 是否仍是目前等待中的目標、自行
// 切換執行緒。count 只是選擇當下的快照，純顯示用，不會即時更新。gfxid 用來找
// "item_<gfxid>.png" 這個圖示檔名（見 AutoPotionOverlay_現況與圖示交接.md）。
// 2026-09-09：success=true 時內部會直接自動存檔＋送伺服器（QueueSave），不用
// 等玩家另外按「儲存」——點選道具就是最終確認動作，不再有「確認中」中繼狀態。
void AutoPotionOverlay_OnResolveReply(bool success, int section, int slot,
                                      int templateItemId, int gfxid, int count,
                                      const wchar_t *name);

// 伺服器權威 HP/MP（PacketBox vitals／旁觀更新）。寫入快取；僅 Overlay 可見時
// 標 dirty 觸發重繪（關閉不 Invalidate，避免 LAG）。
void AutoPotionOverlay_OnHpUpdate(int cur, int max);
void AutoPotionOverlay_OnMpUpdate(int cur, int max);
void AutoPotionOverlay_OnVitalsUpdate(int curHp, int maxHp, int curMp, int maxMp);

// PacketBox 46：開面板／用盡時更新該槽顯示數量（count=0 → 灰遮罩）。
void AutoPotionOverlay_OnSlotCounts(int section, int slot, int count);
void AutoPotionOverlay_OnSlotCountsBatch(int n, const int *sections, const int *slots,
                                         const int *counts);

// 遊戲主執行緒：送出「面板開／關」短包，讓伺服器用 pc 推 vitals／數量（僅開著時）。
void AutoPotionOverlay_PumpPendingUiNotify();
