# PrivateShopStatus 開發須知

## 目前狀態（2026-09-14 更新，第二輪）

**原始回報 bug 已修復並實機驗證**：「掛賣道具沒有詳細資料」指的是**別的玩家開你的商店瀏覽時**看不到說明（不是賣家自己設定畫面）。根因是掛賣（上架）這個動作走的原生流程完全沒有呼叫到 `Hook_AttachStatus`／`PrivateShopCopyItemFmtFromBag` 這條共用複製鏈，商店結構的 `+0xA8`/`+0x14`/`+0x18` 從頭到尾是空的；掛收（收購）能顯示是因為它會觸發 `WarehouseStatusHook.cpp` 的 `Hook_AttachStatus` fallback，兩者原生路徑本來就不同。

修法：**額外重新啟用「Blob」這一條**（位址 `0x5423DD`，機器碼特徵 `kShopPushStr`，對應「上架/掛賣」情境），連同原本的 Clone 一起裝。實機測試（賣家掛賣 → 另一玩家開商店查看）確認道具詳細資料已經正常顯示。Tooltip 寬高／繪製這兩條**維持停用**。

**目前唯一剩下的已知小問題**：買家看到的說明文字，名稱跟內容之間會多一行空白（賣家自己設定畫面也有同樣現象，兩邊一致）。已排除的可能原因：
- ~~來源字串（bag item `+0xA8`）本身開頭是空白/控制碼~~ ——用 log 印出 `src_head` 實際內容確認過，是正常文字，不是空白開頭。
- ~~`g_fmtExtraNl` 共用旗標跨道具污染~~ ——已經把 `Hook_SplitFmt`（`WarehouseStatusHook.cpp`）改成用完立刻歸零（見下方「共用旗標」一節），但實測空白行仍然存在，代表不是這個旗標造成的。

**尚未驗證、目前最可疑的方向**：商店道具結構 `+0x10`（附加狀態指標欄位）殘留非 0 值，導致原生 Tooltip 高度計算多保留一行、原生繪製嘗試畫出但內容是空的。**注意**：查這個要在 `PrivateShopPickStatus`（Blob 路徑，`0x5423DD`）裡查，不是 `PrivateShopCopyBagFmt`（Clone 路徑，`0x595736`）——實測掛賣時只會觸發 Blob，完全不會進到 Clone，先前在 Clone 裡加的 `extra_ptr` 診斷 log 因此沒抓到任何資料，方向抓錯了，之後要接著查請改在 Blob 這條路徑上加類似的診斷（印出當下商店道具結構 `+0x10` 的值與內容）。另外，CE 外部除錯器在這個客戶端上中斷點打不進去（有保護殼／反除錯），查資料要用 DLL 內部加 log 的方式，不能指望即時中斷觀察暫存器。

- `InstallPrivateShopStatusHook()` 已接進 `LauncherDll.cpp` 的 `DelayedDetourThread`（跟 `InstallWarehouseStatusHook()`/`InstallTradeStatusHook()` 同一批，解密完成後才裝）——之前這個函式即使拿掉 `return` 也不會被呼叫到，已補上呼叫點。
- `PrivateShopCopyBagFmt()` 是「目的地 `+0x14` 還沒有有效行數才 fallback 複製背包資料」，對齊 `WarehouseStatusHook.cpp` 的 `Hook_AttachStatus` 寫法，不是每次無條件覆蓋。但實測顯示掛賣不會經過這個函式（見上），它目前只在掛收等其他情境下被當保險機制觸發。
- 停用 Width/Draw 的原因：先前四條一起啟用時曾造成商店列表第一行被截斷，還沒有單獨驗證過是不是這兩條造成的。**在個別驗證通過前，不要打開**。
- 共用旗標：`g_fmtExtraNl`（`WarehouseStatusHook.cpp`，thread-local）被 Blob hook（`PrivateShopPickStatus`）、倉庫 `Hook_AttachStatus`、以及透過 Detours 全局攔截的 `Hook_SplitFmt` 三處共用。`Hook_SplitFmt` 已改成「用完立刻歸零」（一次性消費），避免殘留影響到下一個不相關道具的 `SplitFmt` 呼叫——這個修正是對的（防止跨道具污染），但沒有解決空白行問題，空白行根因還在別處。
- 個人商店 Hook 與交易視窗 Hook 是兩條不同路徑。交易視窗由 `TradeStatusHook` 負責，不要用個人商店的位址或跳板處理交易視窗。
- 目前程式碼裡還留著多個暫存診斷 log（`[Pss][diag]` 開頭，`PrivateShopCopyItemFmtFromBag`/`PrivateShopCopyBagFmt`/`PrivateShopPickStatus` 各一段，都有 8 次上限）。等空白行問題查完、確認不需要再看資料後，記得依「Log 原則」全部移除，不要留在正式路徑。

## 共用函式

- `PrivateShopCopyItemFmtFromBag()` 雖然位於個人商店模組，仍被 `WarehouseStatusHook` 使用。
- 若要停用或拆除個人商店 Hook，不可直接刪除整個 `PrivateShopStatus.cpp/.h`；至少要先把倉庫的格式複製邏輯移到獨立模組，並完成原生建置與執行驗證。
- 格式字串位於道具結構 `+0xA8`，行數位於 `+0x14`，行偏移量位於 `+0x18`。目標道具不可共用來源的字串指標或行偏移量。

## Hook 安裝注意事項

- 所有位址都是特定 32 位元客戶端版本的固定位址；必須等保護殼解密完成後才可安裝。
- 寫入 JMP 前必須先比對原生機器碼特徵。版本不符時應跳過該路徑，不可盲寫固定位址。
- `PatchJmpN()` 的覆蓋長度必須包含完整原生指令，不能只用「剛好 5 bytes」的直覺決定；續接位址也必須和跳板保存的暫存器／堆疊狀態一致。
- 修改可執行記憶體後必須還原頁面保護並呼叫 `FlushInstructionCache()`。
- 若重新啟用 Hook，應逐一驗證 Blob、Clone、Tooltip 寬度／高度、Tooltip 繪製四條路徑，不要一次取消所有防護。

## Log 原則

- Blob、Tooltip 繪製與商品 Clone 都是高頻路徑，不應在正常成功流程逐次輸出 Log。
- 正常情況只保留 Hook 安裝成功的摘要；機器碼不符時保留單一失敗摘要即可。
- 若需要追查資料內容，應暫時加入受限次數的診斷 Log，確認問題後立即移除，不要把逐次資料 Log 長期留在正式路徑。

## 修改流程

1. 先確認目標客戶端版本與原生機器碼特徵。
2. 再確認 Hook 只影響個人商店，不影響交易與倉庫。
3. 先在本機完成原生 DLL 建置，再進行實際客戶端測試。
4. 測試列表第一行、商品說明多行、附加狀態、收攤／掛攤克隆，以及倉庫道具說明。
5. 只有在上述流程通過後，才可考慮移除安裝函式的停用 `return`。
