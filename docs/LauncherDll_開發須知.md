# LauncherDll 開發須知

## 模組責任

- `LauncherDll.cpp` 是 DLL 的協調入口，不是所有 Hook 的實作位置。
- `DelayedDetourThread()` 只負責等待解密、安裝共用 API／視窗 Detours，以及依序啟動各模組安裝函式。
- 網路編解碼、PSS UI、登入、封包攔截與遊戲位址修補應維持在各自模組；不要把新的功能邏輯繼續塞進本檔案。
- `HookProc()` 只在遊戲主執行緒泵送待處理工作與 UI 事件。需要讀取遊戲 UI 狀態時，不要從背景執行緒直接碰遊戲物件。

## 解密與安裝順序

- 保護殼解密完成前，對遊戲程式碼寫入的 Patch 可能被覆蓋；需要遊戲位址的 Hook 必須放在 `DelayedDetourThread()` 的解密等待之後。
- `PatchThread()` 另有自己的解密標記等待，因為 ConditionalPatch／PATCHCODE1 的時機與一般 Detours 不完全相同。
- 不要把 `PatchThread()` 改回背景執行緒而未驗證；目前由延遲流程直接呼叫，目的是確保安裝 Log 與 Patch 確實執行。
- 新增 Hook 時，先確認安裝函式是否需要解密後位址，再決定放在 `DelayedDetourThread()` 或 `PatchThread()`。

## 網路編解碼

- `_seed`、`_xorByte` 與 `inited` 共同描述目前連線的編碼狀態；重新 connect 時必須清除 `inited`。
- `my_send()` 與 `MimirSendEncoded()` 共用同一條金鑰流，必須共用 `GetSendLock()`。少鎖一次就可能讓 RandomEnc 與伺服器永久錯位。
- Mimir 待送封包要呼叫 `real_send()`，不能再進 `my_send()`，否則會重複 XOR／RandomEnc。
- 不要恢復逐包 hex dump；實測每個 recv 寫 Log 會造成關窗卡住。封包追查只能使用短期、受限次數的診斷工具。
- RSA authdata 是固定 4 bytes。讀取不足 4 bytes 時必須繼續收齊，不能把半個握手封包交給後續解碼。

## Hook 與 UI

- `WH_GETMESSAGE` Hook 會在主執行緒高頻觸發；不要在其中做檔案 I/O、網路等待或大量 Log。
- 道具點選必須在 `WM_LBUTTONUP` 且遊戲完成原生點擊處理後讀取，提前讀取可能拿到上一格道具。
- `CreateWindowExA/W` 只處理 class `Lineage` 的主視窗。其他視窗必須完整透傳，避免破壞登入器或遊戲子視窗。
- `OnLineageWindowCreating()` 以旗標防止重複安裝 `WH_GETMESSAGE`；不要移除 `g_hooked` 防護。

## 已知失敗與停用行為

- `VitalsPacketHook` 進世界曾造成斷線，目前不要在延遲安裝流程啟用；若要重試，必須先建立可重現的連線測試。
- `PrivateShopStatus` 整組修補曾造成個人商店列表第一行被截斷，目前刻意停用。細節見 `docs/PrivateShopStatus_開發須知.md`。
- 個人商店與交易視窗是不同原生路徑；不要共用固定位址、跳板或安裝函式。
- 個人商店的格式複製函式仍被倉庫使用，不能因個人商店 Hook 停用而刪除整個模組。
- 所有固定位址 Hook 都必須先比對原生機器碼特徵，版本不符時應跳過，不可盲寫。

## Log 原則

- `[Install]` 只保留安裝成功摘要、失敗、逾時與例外；不要逐一記錄每個已成功的內部步驟。
- 高頻路徑（recv、send、訊息泵送、Tooltip、Blob、Clone）預設不寫 Log。
- PSS 的必要 UI 診斷使用 `[Pss]`／`[PssUI]` 前綴；其他訊息會被 `launcherdll_vlog()` 過濾。
- 需要臨時診斷時，使用明確前綴與次數上限，問題確認後一併移除。

## 修改與驗證流程

1. 先在所屬模組修改功能，不要先擴張 `LauncherDll.cpp`。
2. 若需要新增安裝順序，更新 `DelayedDetourThread()` 的責任註解與本文件。
3. 先做檔案診斷，再執行原生 DLL 建置；不要只用 .NET Launcher 建置代替 C++ 驗證。
4. 安裝失敗時先檢查解密時機、原生機器碼、覆蓋長度與續接位址。
5. 實機驗證完成後，才移除停用 `return` 或恢復暫時性的診斷 Log。
