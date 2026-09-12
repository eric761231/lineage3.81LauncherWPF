# 自動喝水：現況總結 + 道具圖示反組譯交接

> 寫作日期：2026-09-09
> 目的：讓另一個 AI／開發者在**不重讀整段對話**的前提下接手。
> 取代對象：
> - `AutoPotionOverlay_拖曳交接.md`——**已作廢**，拖曳方案已放棄（原因見下方第 1 節），不用再讀。
> - `AutoPotionOverlay_點選道具計畫.md`——**該文件描述的計畫已經全部實作完成**，這份新文件記錄「實際做出來的樣子」跟計畫文件之間的差異，兩份可以對照看，但以這份為準。

---

## 1. 一句話現況

自動喝水功能（治療／補魔各 5 槽 + 共用門檻百分比）已經**端到端可用**：HOME/Insert 熱鍵開啟自繪視窗 → 點格子進入選擇模式 → 點背包道具 → 伺服器解析樣板 id（含道具類型檢查）→ 客戶端**直接自動存檔＋送伺服器**（不用再按存檔）。伺服器 `PotionTimer`（500ms tick）偵測 HP/MP 低於門檻時自動使用道具。

**還沒做、這次交接的重點**：格子目前只有自己畫的瓶子造型佔位圖示，不是真的道具美術圖。反組譯 `Sprite.idx`/`Sprite.pak` 想抽出真圖，**破解到一半卡住**——見第 5 節，這是這次交接最主要的未完成項目。

---

## 2. 關鍵檔案

| 角色 | 路徑 |
|------|------|
| Overlay UI（自繪 layered 視窗） | `LinProj/LauncherDll/AutoPotionOverlay.cpp` / `.h` |
| 本機設定檔 + 送包/收包函式 | `LinProj/LauncherDll/AutoPotionConfig.cpp` / `.h` |
| 背包道具讀取（DMA） | `LinProj/LauncherDll/InventoryDebugHook.cpp` / `.h` |
| HOME／Insert 熱鍵、背包點擊偵測 | `LinProj/LauncherDll/LauncherDll.cpp`（`HookProc`） |
| PacketBox 借位子類型分派（Mimir + 自動喝水共用同一個 cave） | `LinProj/LauncherDll/MimirPowerHook.cpp` |
| 舊 Helper（DMA 直接讀寫遊戲記憶體，已停用僅參考） | `LinProj/LauncherDll/HelperDlg.cpp`（整檔 `#if 0`） |
| 伺服器收包（借位 opcode 75） | `SVN_Lineage381C/.../clientpackets/C_BroadcastToPledge.java` |
| 伺服器回包（PacketBox 借位子類型 32） | `SVN_Lineage381C/.../serverpackets/S_AutoPotionResolveItem.java` |
| 伺服器欄位 | `.../Instance/L1PcInstance.java`（`autoPotion*`） |
| 伺服器 tick | `.../timer/pssTimer/PotionTimer.java` |
| 伺服器實際喝藥 | `.../model/L1PlaySupportSystem.java`（`usePotion`） |

建置產物：`LinProj/LauncherDll/Release/LauncherDll.dll`（Release｜Win32）。這台機器的 JDK 11 安裝壞掉了（`C:\Program Files\Java\jdk-11\` 只剩 `lib`，沒有 `bin`），**沒辦法完整編譯驗證伺服器端 Java**，只能人工覆核——接手時建議先確認自己的環境能不能編譯。

---

## 3. 資料模型（權威契約）

```cpp
enum AutoPotionSlotKind { None = 0, Item = 1, Skill = 2 };
struct AutoPotionSlot { int kind; int id; }; // id = 樣板 itemId 或 skillId
struct AutoPotionSection { int thresholdPercent; AutoPotionSlot slots[5]; };
struct AutoPotionConfig { bool enabled; AutoPotionSection heal; AutoPotionSection mana; };
constexpr int kAutoPotionSlotsPerSection = 5;
```

本機設定檔：`<遊戲 exe 目錄>\Core\auto_potion.cfg`，手刻 `key=value`（`enabled` / `heal.threshold` / `heal.slotN.kind` / `heal.slotN.id` / `mana.*` 同理）。

**UI-only、不進封包/不存檔的快取**（`AutoPotionOverlay.cpp` 內部）：
```cpp
wchar_t g_slotCachedName[2][5][64]; // hover tooltip 用，只有這次連線選過的格子有值
int g_slotCachedCount[2][5];        // 格子下方數量顯示，選擇當下的快照，不會即時更新
```
從檔案重新載入（開啟 overlay 時）會整組清空——載入的格子在重新選過之前，hover 只能顯示 `#id`，數量不顯示。

---

## 4. 封包格式（借位 opcode 75 + PacketBox 借位子類型 32）

### 4.1 現有的完整設定包（54 bytes，未改動）
```
opcode(c)=75, enabled(c), heal%(c), 5×(kind(c)+id(d)), mana%(c), 5×(kind(c)+id(d))
```
伺服器：`decrypt.length >= 54` → `readAutoPotionConfig`。

### 4.2 點道具解析請求（C→S，7 bytes，本次新增）
```
opcode(c)=75, section(c), slotIndex(c), objId(d)
```
伺服器：`decrypt.length == 7` → `handleResolveItemRequest`（這個分支要放在 54-byte 判斷之後、`>=5` 判斷之前，見 `C_BroadcastToPledge.java` 註解）。

伺服器邏輯：
1. `pc.getInventory().getItem(objId)` 查背包。
2. **道具類型檢查**：比對 `item.getItem().getclassname()` 是不是屬於該 section 允許的類型（見下方常數）。不符合／查無此物直接當失敗回覆。
3. 成功：回 `templateItemId = item.getItemId()`、`count = item.getCount()`、`name = item.getName()`。

```java
// C_BroadcastToPledge.java
HEAL_CLASSNAMES = { "hp.UserAddHp", "hp.UserAddHp2", "hp.UserAddHp3", "hp.UesrAddHp4"(拼字本來就這樣), "hp.UserHpr", "ReaetHpMp" };
MANA_CLASSNAMES = { "mp.UserAddMp", "mp.UserAddMp3", "ReaetHpMp" };
```
這幾個 classname 是從 `com.lineage.data.item_etcitem.{hp,mp}` 底下實際存在的 `ItemExecutor` 子類別檔名核對出來的（`ItemClass.addList` 用 `Class.forName("com.lineage.data.item_etcitem." + classname)` 反射載入，所以這個字串就是道具資料庫 `classname` 欄位的值，不是猜的）。以後道具表換道具，只要新道具的 classname 落在這幾個裡面就會自動被接受，不用改程式碼。

### 4.3 解析回覆（S→C，PacketBox 借位子類型 32，本次新增）
```
[opcode:250][subtype:32] success(c) section(c) slotIndex(c) templateItemId(d) count(d) name(C字串,Big5)
```
`S_AutoPotionResolveItem.java` 兩個建構子（失敗 2 參數／成功 5 參數）。

**客戶端接收端**（`MimirPowerHook.cpp`）：`MimirDispatchCave` 這個 naked asm 函式現在同時處理兩個 sentinel：
- `0x10`（Mimir 之泉，既有）→ `OnMimirDispatch`
- `0x20`（自動喝水解析回覆，本次新增）→ `OnAutoPotionResolveDispatch`

**改到這個檔案是有風險的**：這是共用的、已經穩定運作的 PacketBox 分派攔截點，這次是在既有的 `cmp eax,0x10` 邏輯之後插入 `cmp eax,0x20` 的第二個分支，**沒有動到 Mimir 原本那段**。但既然改過共用程式碼，測試時務必**兩個功能都測**（密米爾之泉開得起來、自動喝水點選也正常），不能只測新功能。

`0x20`（十進位 32）這個子類型值是掃過 `S_PacketBox.java` 全部常數選出來的空號（`31=MSG_FEEL_GOOD`、`33=SOMETHING1`，中間剛好空著），跟 Mimir 用的 `0x10` 不衝突。

---

## 5. 客戶端 UI／互動流程（實際做出來的樣子）

```
HOME／Insert（HookProc，不受 ShareInfo.usehelper 限制——那個旗標被 Encoder
BuildListEntryNative 寫死 false，永遠傳不到客戶端，這是 Encoder 那邊的既有 bug，
還沒決定要不要修）
  → AutoPotionOverlay_Show()（toggle）

點格子（空格或已填格）
  → 進入「選擇中」狀態（g_pickSection/g_pickSlot），同時可以繼續打字用鍵盤輸入
    （Enter=道具 Ctrl+Enter=法術，這條路徑保留給法術/未來備援用，法術目前完全
    沒有點選道具這條路，只能鍵盤輸入 skillId）

點背包道具（HookProc 在遊戲主執行緒偵測到 WM_LBUTTONDOWN）
  → InventoryDebug_FindJustClickedItem()：掃背包全部道具，找 unknow2!=0 那筆
    （這個欄位在剛被點的那一件會短暫變動，用這個判斷「玩家點的是哪一件」，
    不用解背包視窗的螢幕座標版面）
  → 送 7-byte 解析請求

伺服器回覆抵達（OnAutoPotionResolveDispatch，遊戲主執行緒／網路執行緒）
  → PostMessageW 切到 overlay 自己的執行緒
  → 比對 g_pickSection/g_pickSlot 是否還是同一個目標，不符就整包丟棄（玩家可能
    已經點別格或關視窗）
  → 成功：寫入 g_cfg 的 kind/id，快取 name/count，然後【直接呼叫 QueueSave()】
    ——2026-09-09 確認：點選道具就是最終確認動作，不用再按「儲存」，也不會顯示
    「確認中」這種中繼狀態
  → 失敗：只 log，格子維持原狀

儲存按鈕：CommitEdit（把還在打字的內容提交）+ QueueSave，【不關視窗】
  ——2026-09-09 修正：之前誤把 HideWindow() 也放進存檔分支，導致按存檔會關窗，
  已拿掉，只有「關閉」按鈕/標題列 X 會關視窗
```

### 格子畫面

- 有內容：畫一個簡易瓶子造型佔位圖示（`DrawItemPlaceholderIcon`，道具=琥珀色／法術=藍色，純 GDI+ 幾何圖形，不是真的美術圖）+ 格子正下方顯示 `x數量`（只有這次連線選過的格子才有值）。
- hover：顯示名稱 tooltip（`DrawHoverTooltip`），有快取名稱就顯示名稱，沒有（例如載入自舊存檔、還沒重選過）就退回 `#id`。
- 選擇中：格子邊框變色，正下方顯示「請點背包道具」。
- 打字輸入中：格子中央顯示輸入緩衝區內容。

### 已修好的 bug（這次交接前的問題，供對照）

1. **「點任何按鈕都會移除剛選好的藥水」**：根因是「取消打字輸入」跟「取消還在等伺服器回覆的道具選擇」原本綁在同一個 `ClearEdit()` 函式裡，存檔／關閉／±這些跟道具選擇無關的按鈕一按，就把 `g_pickSection/g_pickSlot` 重置成 -1，導致稍後真正抵達的伺服器回覆對不上目標、被丟棄。修法：拆成 `CancelTextEdit()`（只取消打字）跟 `CancelPick()`（只取消道具選擇），`ClearEdit()` = 兩者都做，只在真的要兩者都放棄時呼叫（點別的格子、Esc、伺服器回覆已處理完畢）。
2. **存檔按鈕會關視窗**：已拿掉 `HideWindow()`，只有明確的關閉動作才關窗。

---

## 6. 道具圖示反組譯進度（這次交接的重點，還沒完成）

### 6.1 已確認：道具 id 對應哪個圖，答案就是伺服器端的 `gfxid`

```java
// L1ItemInstance.java
public int get_gfxid() {
    // 魔法娃娃/盧恩技石有特例，一般情況：
    return _item.getGfxId(); // L1Item._gfxId 欄位
}
```
`S_AddItem.java` 本來就用 `writeH(item.get_gfxid())` 把這個值送給客戶端（`H`=2 bytes，WORD 範圍）——這代表**伺服器解析道具時只要多送這個欄位，客戶端完全不用自己反查對照表**。

**待辦**：`S_AutoPotionResolveItem.java` 目前**還沒加 gfxid 欄位**，`C_BroadcastToPledge.java` 的 `handleResolveItemRequest` 也還沒呼叫 `item.get_gfxid()`。這是下一步最直接能做的事——伺服器端加一個 `gfxid(d)` 欄位（或 `h` 2 bytes，視 `writeH`/`writeD` 選用），客戶端 `OnAutoPotionResolveDispatch`／`AutoPotionOverlay_OnResolveReply` 也要跟著加一個參數存起來，供之後畫圖用。

### 6.2 已確認：`Sprite.idx` 的二進位格式

檔案：`D:\天堂資料\天堂專案#380客戶端+自製登入器\Sprite.idx`（2,585,972 bytes）+ `Sprite.pak`（1,904,012,586 bytes，同目錄還有 `Sprite00.idx/.pak` ~ `Sprite13.idx/.pak` 之類的分卷，這次沒有查那些是不是同一體系或獨立的）。

```
檔頭：4 bytes，小端序 u32 = 記錄總筆數（實測 = 92356，跟 (檔案大小-4)/28 算出來的筆數完全吻合）
每筆記錄 28 bytes：
  +0x00: 4 bytes  小端序 i32／u32 = 此檔在 Sprite.pak 內的 byte offset（真實偏移）
  +0x04: 20 bytes 檔名，null-terminated ASCII，例如 "1000.tbt"、"30-0.spr"
  +0x18: 4 bytes  小端序 u32 = 此檔在 pak 裡的位元組數（size）
```
用 Python 解析驗證過：92356 筆記錄，檔頭數字完全對上筆數，在檔案深處（offset 0x100000）格式依然對齊，不是巧合。

**道具圖示的副檔名是 `.tbt`（使用者提供的關鍵資訊），命名規則單純就是 `<gfxid>.tbt`**（例如 gfxid=1000 → 找 `Sprite.idx` 裡名為 `"1000.tbt"` 的記錄），不需要額外對照表——`strings -a Sprite.idx | grep tbt` 可以看到大量 `NNNN.tbt` 這種純數字檔名。

**（2026-09-08 已解）** 先前把 `+0x00` 當成「hash」是錯的。對照  
`C:\Users\eric7\OneDrive\桌面\LineageTool_20230627`（IL 見同目錄旁 `LineageToolPro\_濾鏡patch\_work\LineageTool.il`）的 `L1IdxConvert.load`：讀完筆數後每筆依序 `ReadInt32()` → **`IndexRecord.offset`**，再 20 byte 檔名、`ReadInt32()` → `fileSize`。`BaseViewer.loadPakData` 對 pak 做 **`Seek(offset)` + `Read(size)`**。實測例：`1000.tbt` offset≈527115110、size=361。

### 6.3 `Sprite.pak` 內部偏移（已解決）

**不要**用「前面所有筆 size 累加」算偏移——idx 順序≠pak 加入順序時一定錯；size 加總與 pak 檔案大小差約 5.4MB 也證明天真累加不可用。

正確作法：

1. 開 `Sprite.idx`，讀 u32 `count`，迴圈每筆 28 bytes。
2. 找檔名 `"{gfxid}.tbt"`。
3. 該筆 **開頭 4 bytes = pak offset**，結尾 4 bytes = size。
4. `pak.Seek(offset); Read(size)` 取出 blob（經典 Sprite 多半 `zlibSize==0`，直接是資料）。

`.pak` 內通常**不含**檔名字串（整檔搜 `"1000.tbt"` 找不到屬正常）。

### 6.4 `.tbt` 像素格式本身——尚未解碼，但找到現成的解碼邏輯（IL）可以對照

2026-09-09 確認使用者不想手動打包靜態圖片（不要「離線用 LineageTool 轉圖、包進 ui.pak」這條路），目標是**在 DLL 裡即時解碼 `.tbt`、直接畫在 overlay 上**。

已知：
- `LineageTool.exe`（GUI 工具，`C:\Users\eric7\OneDrive\桌面\LineageTool_20230627\LineageTool.exe`）**確認可以把 `.tbt` 轉成圖片**（使用者實測過）。
- 這個工具沒有命令列/批次模式（`LineageTool.ini` 只有語言/路徑設定，沒有 CLI 參數），檢查過 `/?` 只會開出正常 GUI 視窗，**不能拿來在遊戲注入的 DLL 裡即時呼叫**，排除掉「執行期呼叫外部工具代勞」這條路。
- **有現成的反編譯 IL 可以對照**（別人先前反編譯好、還做過濾鏡 patch 實驗留下的）：
  ```
  C:\Users\eric7\OneDrive\桌面\LineageToolPro\_濾鏡patch\_work\LineageTool.il         （原始版）
  C:\Users\eric7\OneDrive\桌面\LineageToolPro\_濾鏡patch\_work\LineageTool_patched.il （patch 過的版本）
  ```
  共 147,466 行。
- `LineageTool.L1ImgConvert::Load_TBT(uint8[])` **本身只是個轉接殼**（IL 只有 17 bytes / 10 行）：
  ```
  Load_TBT(tbtdata) {
      return getLineageBmpData(tbtdata).bmp;
  }
  ```
  真正的解碼邏輯全部在 `L1ImgConvert::getLineageBmpData(uint8[])`。
- `getLineageBmpData` **非常龐大**：光是看得到的區域變數就有 `V_0` ~ `V_361`+（300+ 個），內部用了多個巢狀 `List<T>` 集合：`List<BlockDef>`、`List<ColorData>`、`List<array7>`、`List<array9>`、`List<array10>`、`List<array11>`——型別名稱看起來像是反編譯器自動產生的匿名/內部型別（原始碼可能用了 tuple 或匿名型別），沒有語意化的名稱可以直接看懂用途，**要逐行讀 IL 才能搞懂每個集合實際存什麼**。這證實 `.tbt` 是多區塊（block）+ 調色盤（color）的複雜壓縮格式，不是簡單的 raw bitmap。
- 相關的還有 `Load_TBT2`、`Load_IMG`、`ShowBlackWhite`、`FullSameColor`、`L1ImgConvert/Frame`（帶 `image` 欄位，代表這個格式可能有多影格/動畫）、`L1ImgConvert/LineageBmpData`（`getLineageBmpData` 的回傳型別，內含 `bmp` 欄位）——這些都还沒細看，但都是同一個 class 底下，之後移植時大概率都要牽連到。

**2026-09-09 決定**：這是獨立、規模不小的移植工程（保守估計工作量跟這次 session 做完的「點選道具」整條流程差不多大，甚至更大），**這次先停在這裡，留待後續**（可能是排一次專門的 session、或交給另一位 AI）。

### 6.5 建議的下一步順序（給接手的人）

1. 先把 `gfxid` 欄位接上封包（伺服器+客戶端）——存起來、log 出來，這步不依賴像素解碼，可以先做。
2. 用 idx 的 **offset＋size** 從 `Sprite.pak` 抽出 `<gfxid>.tbt` 驗證（不必再猜累加，見 6.3）。
3. **移植 `getLineageBmpData` 前，建議先把它的 IL 單獨抽出存成一個檔案**（用 `grep`/`awk` 從 `LineageTool.il` 撈出這個方法的完整 body，行數應該很長，直接在 147,466 行的大檔案裡找太痛苦），逐段分析：
   - 檔頭欄位怎麼讀（寬高？block 數量？調色盤大小？）
   - `BlockDef`／`ColorData`／`Frame` 這幾個型別實際存什麼欄位
   - 有沒有用到標準壓縮演算法（RLE／LZ 系列／zlib，這個專案的其他 pak 格式常用 zlib，`.tbt` 也可能用同一套）
   - 是不是真的有多影格（`Frame.image`／`Frame`s 列表），如果只需要「道具圖示」，可能只要第一影格就夠，不用完整支援動畫
4. 移植成 C++（`AutoPotionOverlay.cpp` 或獨立新檔案），輸出成 GDI+ 能吃的像素緩衝區（`Gdiplus::Bitmap` 用 `LockBits`/直接建構子指定 scan data），取代目前的 `DrawItemPlaceholderIcon` 佔位圖。
5. 法術圖示同一套：`skill gfxid` → `"N.tbt"` → idx offset（見 LinBin3.81 `docs/hooks/SPELL_WINDOW_MEMORY_BRIEF.md`），但這次範圍只到道具，法術圖示不強制一起做。

---

## 7. 明確不要做／已排除

- 不要復活 `HelperDlg.cpp` 的 DMA 自動喝藥（已 `#if 0`），後端判斷版才是正路。
- 不要走背包拖曳這條路（`WM_LBUTTONUP` 會直接送到我們自己 overlay 視窗的執行緒，不會經過遊戲主執行緒的 `HookProc`，這個architecture 已經證實行不通，改用點擊）。
- 不要為這個功能開新 opcode／新的 PacketBox 子類型以外的東西——設定包繼續走 75 長包，回覆繼續走子類型 32。
- 改 `MimirDispatchCave` 時不要動到既有 `0x10` 那段邏輯，只能在旁邊加新分支。
- 法術／技能槽的點選——記憶體規格見 LinBin3.81 `docs/hooks/SPELL_WINDOW_MEMORY_BRIEF.md`；自動喝藥這次仍維持鍵盤／道具點選流程，不強制接技能窗。

---

## 8. 給下一棒的最小閱讀清單

1. 本文件
2. `AutoPotionOverlay.cpp`：`DrawSlot`／`DrawItemPlaceholderIcon`／`WM_AUTOPOTION_RESOLVE_REPLY` case（圖示要畫在哪裡接、目前佔位邏輯長怎樣）
3. `MimirPowerHook.cpp`：`OnAutoPotionResolveDispatch`／`MimirDispatchCave`（封包解析/子類型分派要在哪裡加 gfxid）
4. `C_BroadcastToPledge.java`／`S_AutoPotionResolveItem.java`（伺服器端要加 gfxid 欄位的地方）
5. 本文件第 6 節列的幾個 Python 分析腳本思路（沒有留下實際的 .py 檔案，這次都在 scratchpad 臨時跑的，重新寫不難）
