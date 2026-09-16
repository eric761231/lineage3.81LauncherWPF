# PSS 全白天（All-Day）Hook 計畫

## 背景

`PssOverlay.cpp` 的「其他」分頁（`kMiscToggleLabels`）第一格就是「全白天」勾選框，但目前**完全沒有接後端**——`kMiscIdx_Whetstone`/`kMiscIdx_EatMeat`/`kMiscIdx_ShowDamage`/`kMiscIdx_UnderwaterPump` 都有對應的 index 常數跟處理邏輯，唯獨全白天（index 0）沒有 `kMiscIdx_AllDay`，勾了也不會做任何事。這是 2026-09-09 那次的既有決定：「全白天」在當時評估要處理 8 個原生位址＋（RUST 參考專案的）remote-thread injection，複雜度較高，先擱置。

這次要把當時擱置的原因重新檢視一次：**RUST 參考專案是外部行程（獨立的 launcher exe）在控制遊戲，所以要用 `CreateRemoteThread`/`WriteProcessMemory` 從外部操作；但我們的 `LauncherDll.dll` 是直接注入到遊戲行程內部執行的，同一份記憶體空間，不需要 remote thread 這一層**——這個複雜度來源在我們的架構下其實不存在，值得重新評估。

## 現有相關但不同的功能（不要混淆）

- **S_Light／`LightStampHook`**（`LinProj/parked_hooks/LightStampHook/`，已卸載但保留原始碼）：這是**單一物件的光罩貼圖範圍**（`obj+0x16` 存 level、依 level 查 `0xABF8E8`/`0xABF8EC` 尺寸表貼圖），對應「角色帶燈/點燃光術後，亮的範圍只有約 5 格，理論上 14 格」那個問題。已實測「只改尺寸表會讀越界、畫面花掉」，結論是這條路不通，要嘛換更大的貼圖資產，要嘛整個換成環境光（也就是本篇的 all_day）方向。**這篇 all_day 計畫不處理這個**，全白天做的是整張地圖的環境光／調色盤，不是單一光源的貼圖半徑。
- **`docs/hooks/MAP_300_800_ALL_DAY_BRIEF.md`**（`LinBin3.81/docs/hooks/`）：研究地圖 300／800（亞丁內城／市場中心）視覺上「看起來像全白天」的原因，結論是這兩張圖沒有走 cave_dark 強制昏暗、白天亮度峰值本來就等於全白天的目標值，兩者剛好撞到同一個 palette 結果，不是客戶端對這兩張圖另開了什麼「全白天」開關。這篇筆記把客戶端亮度計算的路徑（`0x4EA4DE` 進圖流程、`cave_dark` 旗標 `0x9ABCEF`、`brightness_calc`）交代得很清楚，本計畫直接沿用其中的位址。

## 資料來源與信度

位址與 patch 內容**完整抄自** RUST 參考專案 `C:\python_training\L1J3.8Launcher(RUST)參考\src\aux\toggle\all_day.rs`（同一支 `TW13081901.bin`，含單元測試鎖定關鍵位址/位元組，信度高）。此檔案完整實作了 `enable`/`disable` 兩個方向，以下位址原封不動照抄：

| 符號 | 位址 | 意義 |
|---|---|---|
| `BRIGHTNESS_CALC_ADDR` | `0x00786D70` | 亮度計算函式（吃遊戲時間，正常回傳 5~15） |
| `WEATHER_RENDER_ADDR` | `0x004ED890` | 天氣（雨/雪/霧）渲染函式 |
| `DAYLIGHT_CHECK_ADDR` | `0x00787040` | 「是否為白天」判斷函式 |
| `PALETTE_DARKEN_LOAD_ARG_ADDR` | `0x0057E6F7` | 調色盤變暗參數載入點 |
| `PALETTE_DARKEN_CACHE_ARG_ADDR` | `0x0057E707` | 調色盤變暗參數快取點 |
| `PALETTE_DARK_LEVEL_ADDR` | `0x00BDC9D0` | 調色盤暗度等級（disable 還原用） |
| `PALETTE_TABLE_PTR_ADDR` | `0x00BDC9D4` | 調色盤表指標（disable 還原用） |
| `CAVE_DARK_FLAG_ADDR` | `0x009ABCEF` | cave_dark 旗標（byte） |
| `CAVE_DARK_HIGH_MAP_SET_IMM_ADDR` | `0x004EA514` | 進圖時「mapId≥0x4000 → cave_dark=1」那行立即數 |
| `CAVE_DARK_TILESET_SET_IMM_ADDR` | `0x004EA551` | 進圖時「tileset 在暗圖表 → cave_dark=1」那行立即數 |
| `CAVE_LIGHT_FORCE_IMM_ADDR` | `0x004EA6D4` | cave 路徑強制昏暗的立即數（`palette.method(1)`） |
| `LIGHT_RECOMPUTE_SKIP_BRANCH_ADDR` | `0x004EAD19` | 「光源已是最大值就跳過重算」分支 |
| `ENVIRONMENT_OVERLAY_BRANCH_ADDR` | `0x004F0E92` | 環境暗層 overlay 繪製分支 |
| `FINAL_LIGHT_ARG_ADDR` | `0x004F037C` | 最終送進繪製流程的光源參數 |
| `PALETTE_OBJ_THIS` | `0x00BDC7A4` | 調色盤物件 this 指標（thiscall） |
| `PALETTE_OBJ_REFRESH` | `0x00579E10` | 調色盤刷新方法 |
| `GAME_TIME_GLOBAL` | `0x00C31E7C` | 遊戲時間全域變數 |
| `MAP_ID_GLOBAL` | `0x00965B60` | 目前地圖 ID |
| `TILESET_ID_GLOBAL` | `0x00965B64` | 目前 tileset ID |
| `TILESET_TABLE_BASE`／`LEN` | `0x009655F0`／`0x15A` | 暗圖 tileset 表（線性掃描比對） |
| `WEATHER_STATE_ADDRS` | `0xABF324`(雨)／`0xABF328`(雪)／`0xABF8A4`(霧) | 天氣強度數值，enable 時歸零 |

以及 RUST 附的原始/patch 位元組對（照抄，不要自己重新反組譯一次，`all_day.rs` 裡每組都有對應的 `#[cfg(test)]` 鎖定）：

```
BRIGHTNESS_CALC：15 bytes 開頭改成 mov eax,0xF; ret（直接回傳 15，跳過原本吃時間的計算）＋ NOP 補滿
WEATHER_RENDER：入口第一個 byte 55→C3（函式一開始就 ret，天氣不繪製）
DAYLIGHT_CHECK：入口 3 bytes 改成 mov al,1; ret（永遠回傳「是白天」）
PALETTE_DARKEN_LOAD_ARG／CACHE_ARG：8B 45 08→31 C0 90（歸零參數，不吃呼叫端傳進來的暗度）
CAVE_DARK_HIGH_MAP_SET_IMM／TILESET_SET_IMM：立即數 01→00（cave_dark 兩處判斷都改成寫 0）
CAVE_LIGHT_FORCE_IMM：01→0F（cave 路徑的強制暗度也改成最大亮度 15）
LIGHT_RECOMPUTE_SKIP_BRANCH：6 bytes 條件跳轉改 NOP（不要跳過重算，讓最大光源真的套用）
ENVIRONMENT_OVERLAY_BRANCH：條件檢查改成無條件跳過環境暗層繪製
FINAL_LIGHT_ARG：8B 55 AC → 6A 0F 5A（最終光源參數直接塞 15）
```

`disable` 是上述每組的逆向還原（RUST 用 `original`/`patched` 比對何者已套用，我們的房規慣例是先 `memcmp` 驗證特徵再動手，兩者精神一致，實作時直接沿用我們自己的 `PatchJmpN`/`PatchCode` 風格，不用照抄 RUST 的 `apply_patch`/`restore_patch` 函式簽章）。

## 跟 RUST 版本的關鍵差異：不需要 remote thread

RUST 的 `force_palette_refresh()`（`disable` 收尾用，讓「剛從地監關閉全白天」時畫面立刻變回正確暗度，而不是等下次自然重算）是因為 RUST launcher 是**外部行程**，只能用 `CreateRemoteThread` 把一段手刻 shellcode 注入到遊戲行程裡執行，等於用組合語言重寫了一次「重算 cave_dark + 呼叫 palette 刷新」的邏輯（`build_palette_refresh_shellcode()`，111 bytes，手動組出跳轉/呼叫）。

**我們的 `LauncherDll.dll` 本來就活在遊戲行程裡**，等同的邏輯可以直接寫成一般 C++ 函式呼叫，不用組 shellcode、不用 `CreateRemoteThread`：

```cpp
// 概念示意，實作時放進新檔案 AllDayPatch.cpp
typedef void(__thiscall *PaletteRefresh_t)(void *thisPtr, int brightness);
static PaletteRefresh_t PaletteRefresh = (PaletteRefresh_t)0x00579E10;
typedef int(__cdecl *BrightnessCalc_t)(int gameTime, int, int, int);
static BrightnessCalc_t BrightnessCalc = (BrightnessCalc_t)0x00786D70; // 注意：全白天啟用時這個位址本身已經被我們 patch 成「直接回傳 15」，所以 disable 流程要先還原這個 patch，才能呼叫它拿到「正確」的當下亮度

void RefreshPaletteNow() {
  void *paletteObj = reinterpret_cast<void *>(0x00BDC7A4);
  int mapId = *reinterpret_cast<int *>(0x00965B60);
  BYTE cave = 0;
  if (mapId >= 0x4000) {
    cave = 1;
  } else {
    int tileset = *reinterpret_cast<int *>(0x00965B64);
    int *table = reinterpret_cast<int *>(0x009655F0);
    for (DWORD i = 0; i < 0x15A; i++) {
      if (table[i] == tileset) { cave = 1; break; }
    }
  }
  *reinterpret_cast<BYTE *>(0x009ABCEF) = cave;
  if (cave) {
    PaletteRefresh(paletteObj, 1);
  } else {
    int gameTime = *reinterpret_cast<int *>(0x00C31E7C);
    int brightness = BrightnessCalc(gameTime, 0, 0, 0);
    PaletteRefresh(paletteObj, brightness);
  }
}
```

這段直接翻譯 RUST shellcode 裡的邏輯（見 `all_day.rs` 底部大段註解跟反組譯結果 `0x004EA470`/`0x004EA6B9`），比維護一份手刻機器碼陣列好懂、好改、好除錯，是這次移植相對於原始 RUST 版本的主要簡化。

`enable()` 收尾的「立即讓畫面變亮」也一樣：不用另外找刷新時機，直接在裝完全部 patch 後呼叫一次 `RefreshPaletteNow()` 即可（此時 `cave_dark` 判斷邏輯還是原生正常邏輯，只是後面 `brightness_calc`/其他 patch 已經生效，所以算出來的 cave 分支結果會是「最亮」）。

## 實作範圍

1. **新增 `AllDayPatch.cpp`/`.h`**（比照 `ShowClockPatch.cpp` 風格：`InstallAllDayHook()`，內部先不真的啟用，只驗證全部特徵位元組、準備好 `EnableAllDay()`/`DisableAllDay()` 兩個函式）。
   - 每個 patch 點先 `memcmp` 驗證原始 or 已 patch 位元組，比對失敗要 log 並整組放棄（不要「一半套上去一半沒套」的中間狀態）——這點比 RUST 版本更嚴格一些：RUST 用 `apply_patch`/`restore_patch` 各自獨立判斷、彼此有 rollback 鏈；我們可以先在 `EnableAllDay()` 開頭一次把全部 14 個 patch 點的「原始特徵」都驗證過一輪，通過才開始真的寫，寫的過程理論上不會再失敗（除非中途記憶體保護出問題），失敗處理可以簡化成「記錄失敗、不 rollback 已寫的部分、log 警告」，而不必像 RUST 那樣每一步都寫一次 rollback chain（我們在同一個行程內，patch 動作是原子的記憶體寫入，不像 RUST 隔著行程邊界要防「寫一半外部行程就死掉」）。
   - `WEATHER_STATE_ADDRS` 三個歸零、`CAVE_DARK_FLAG_ADDR` 歸零，這些是「資料」不是「程式碼」，用一般 `memcpy` 寫，不用 JMP/NOP。
2. **`PssOverlay.cpp`**：加 `kMiscIdx_AllDay = 0`，比照 `kMiscIdx_UnderwaterPump` 的寫法，勾選時呼叫 `EnableAllDay()`，取消勾選呼叫 `DisableAllDay()`，並且要進 `g_cfg`／`auto_potion.cfg` 存檔（開關狀態要記住，重登/重開要保留使用者選擇——這點也要決定：全白天是**純客戶端視覺效果**，要不要跟其他 misc flags 一樣送封包給伺服器？目前看起來不需要，伺服器不需要知道這件事，存檔可以只存本機 cfg，不用佔用 `0x5A`/`0x5C` 那些 4-byte flags 欄位裡的 bit，除非之後有伺服器端要知道玩家開了全白天的理由）。
3. **進世界自動套用**：現有「其他分頁」的開關是「進世界自動套用」模式（`PssUI world enter: apply cfg`），全白天要一併接進那個流程，玩家勾過一次、下次登入自動生效，不用每次手動勾。
4. **`LauncherDll.cpp`**：`InstallAllDayHook()` 加進 `DelayedDetourThread`（解密完成後才裝，跟其他原生 hook 同一批），但注意**這個 install 只驗證特徵、不會真的下 patch**——patch 動作要等玩家實際勾選（或進世界自動套用時）才呼叫 `EnableAllDay()`，不要一裝好 hook 就預設全白天。

## 驗證方式

1. **啟用**：找一張會受夜晚影響的一般野外圖（非 cave），開全白天，觀察畫面立即變亮、天氣特效消失；切換到一張 cave 圖（如地監），確認也是亮的（`CAVE_LIGHT_FORCE_IMM` 那個 patch 生效）。
2. **停用**：在亮著的狀態下取消勾選，確認 `RefreshPaletteNow()` 有正確依「當下實際地圖/時間」算出正確亮度（白天圖應該還是亮、夜晚圖應該變暗、cave 圖應該變暗）——這是最容易翻車的一步，RUST 版本特別為此寫了整段 shellcode，實作時要仔細測「在地監開全白天後，走出地監到一般野外，再取消勾選」跟「在野外開、切換到地監、取消勾選」兩種順序都要測。
3. **重登/重開**：勾選後登出重登，確認 cfg 有記住、進世界自動重新套用。
4. **跟其他系統不衝突**：`S_Light`（個人光源）、`LightStampHook` 停用中不受影響；跟商店/倉庫等其他 hook 沒有位址重疊（14 個位址都在 `0x4E`~`0x79`/`0xA9`~`0xBD` 範圍，跟目前其他 hook 用到的位址沒有衝突，但部署前還是要一個一個跟現有 hook 表對過，避免巧合撞到）。

## 風險與待確認

- 這是這次除了 `PrivateShopStatus`（4 條路徑同時啟用曾造成「商店列表第一行被截斷」）之外，**位址數量最多、彼此有依賴關係的一組 patch**（14 個 code/data 點），比對「原生特徵位元組」這一步務必逐一做，不要因為都抄自已驗證的 RUST 位址就跳過——RUST 驗證的是 RUST 那次連線到的那個 client 版本，我們用的是不是完全同一份 build 還是要用 `memcmp` 現場確認，跟這個 session 一路以來處理 `PrivateShopStatus`/`WarehouseStatusHook` 的原則一致。
- `BRIGHTNESS_CALC` 這個位址在 `EnableAllDay()`／`DisableAllDay()` 兩邊都要用到（disable 流程呼叫它算「正確」亮度），但它本身也是被 patch 的目標之一——**disable 必須先把這個函式本身的 patch 還原，才能呼叫它拿到有意義的回傳值**，順序不能顛倒（先還原 `BRIGHTNESS_CALC` 的 patch，再呼叫它算亮度，再呼叫 `PaletteRefresh` 套用），這點 RUST 版本因為呼叫順序是 disable() 先整組還原 8 個 code patch、最後才呼叫 `force_palette_refresh()`，天然沒有這個問題，移植時要維持同樣的順序，不要為了圖方便把 `RefreshPaletteNow()` 提前呼叫。
- 目前只做了「靜態抄位址＋規劃」，**還沒寫任何程式碼、沒編譯、沒部署測試**，是給下一輪（或接手的另一位）驗證執行用的計畫文件，不是完成品。
