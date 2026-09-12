# 自動喝水 Overlay 交接文件（給下一棒：背包拖曳進槽）

> 寫作日期：2026-09-07  
> 目的：讓另一個 AI／開發者在**不重讀整段對話**的前提下，接上「從遊戲背包拖曳道具到 AutoPotion 槽位」功能。  
> 本文件描述**已完成現況**、**資料契約**、**接線點**、**已知記憶體線索**、**尚未查到的缺口**。

---

## 1. 一句話現況

客戶端已有 **Mimir 式 layered 自繪視窗**（無美術圖、GDI+ 幾何），HOME 開關；槽位目前用**鍵盤輸入 itemId／skillId**；存檔＋借位 opcode 75 長包已通；伺服器 `PotionTimer` 只會對 **kind=Item** 的槽呼叫 `usePotion`。

**還沒做**：從背包／技能欄「拖曳」寫入槽位（需反組譯「目前拖曳中的物品」狀態）。

---

## 2. 關鍵檔案

| 角色 | 路徑 |
|------|------|
| Overlay UI | `LinProj/LauncherDll/AutoPotionOverlay.cpp` / `.h` |
| 設定＋送包 | `LinProj/LauncherDll/AutoPotionConfig.cpp` / `.h` |
| HOME／Pump | `LinProj/LauncherDll/LauncherDll.cpp`（`HookProc`、`my_send`） |
| 舊 Helper（停用，僅參考 DMA） | `LinProj/LauncherDll/HelperDlg.cpp`（整檔 `#if 0`，外層有 no-op stub） |
| 伺服器收包 | `SVN_Lineage381C/.../clientpackets/C_BroadcastToPledge.java` |
| 伺服器欄位 | `.../Instance/L1PcInstance.java`（`autoPotion*`） |
| 伺服器 tick | `.../timer/pssTimer/PotionTimer.java` |
| UI 設計稿參考 | 對話中的 Gemini mockup（治療／補魔各 5 槽） |
| 原始規劃 | `C:\Users\eric7\.claude\plans\disconnectoverlay-distributed-boole.md`（提到拖曳待辦） |

建置產物：`LinProj/LauncherDll/Release/LauncherDll.dll`（Release｜Win32）。

---

## 3. 資料模型（權威契約）

```cpp
enum AutoPotionSlotKind { None = 0, Item = 1, Skill = 2 };

struct AutoPotionSlot {
  int kind; // 0/1/2
  int id;   // itemId（樣板 ID）或 skillId；不是背包 instance objId
};

struct AutoPotionSection {
  int thresholdPercent; // 該區共用門檻 0–100；0 = 不觸發
  AutoPotionSlot slots[5];
};

struct AutoPotionConfig {
  bool enabled;
  AutoPotionSection heal; // 治療設定（HP）
  AutoPotionSection mana; // 補魔設定（MP）
};
```

常數：`kAutoPotionSlotsPerSection = 5`。

### 本機設定檔

- 路徑：`<遊戲 exe 目錄>\Core\auto_potion.cfg`
- 手刻 `key=value`，例：

```text
enabled=1
heal.threshold=50
mana.threshold=30
heal.slot0.kind=1
heal.slot0.id=40010
heal.slot1.kind=0
heal.slot1.id=0
...
mana.slot0.kind=1
mana.slot0.id=40018
...
```

- 舊版 `slotN.threshold` / `slotN.itemId`（3 組 per-slot）**讀取時忽略**，存檔會覆寫成新格式。

### API

- `AutoPotionConfig_Load()` / `Save()` / `SendToServer()` / `HasAnySlot()`
- **`SendToServer` 只能在遊戲主執行緒呼叫**（內部 `SendPacketData @ 0x580E50`）。

---

## 4. 封包（借位 opcode 75）

- Opcode：`75`（`C_BroadcastToPledge`）
- 與原生短包用**長度**區分：
  - 原生：decrypt.length ≈ 2（`readC`）或 5（`readD`）→ 血盟登入訊息旗標
  - 自動喝水：**54 bytes（含 opcode）**

Payload（opcode 之後）：

```text
enabled(c)
heal%(c)
×5: kind(c) + id(d)
mana%(c)
×5: kind(c) + id(d)
```

客戶端 `SendPacketData` format：

```text
"ccccdcdcdcdcdccdcdcdcdcd"
```

伺服器：`decrypt.length >= 54` → `readAutoPotionConfig`；否則走短包 `isClanLoginMsg`。

`PotionTimer`（500ms）：HP／MP 低於該區 `threshold` 時，**由左到右**找第一個 `kind==ITEM && id>0` 且背包有貨的道具呼叫 `L1PlaySupportSystem.usePotion`。  
**Skill 槽目前只存不施放**（此服無可用的 `L1SkillUse`）。

---

## 5. Overlay 架構（拖曳必懂）

```text
HOME (HookProc, 需 ShareInfo.usehelper)
  → AutoPotionOverlay_Show()  // toggle
  → 獨立 UI 執行緒 + WS_EX_LAYERED + owner=g_hGameWnd
  → UpdateLayeredWindow 自繪（無子控制項）

儲存按鈕
  → QueueSave() 只設 g_pendingSave + 複製 g_pendingCfg
  → AutoPotionOverlay_PumpPendingSave() 在 HookProc / my_send（遊戲主執行緒）
       → Save + SendToServer
```

### 座標／命中

- 設計座標：`kBaseW=420`, `kBaseH=560`；依遊戲 client／`refW=1153,refH=798` 縮放。
- **槽位矩形**：`SlotRc(section, index)`
  - `section`：`0` = heal，`1` = mana
  - `index`：`0..4`
  - 未縮放：slot 邊長 `kSlotSize=48`，間距 `kSlotGap=10`，列起點約 `(36, boxY+36)`，`boxY` heal=72／mana=286。

拖曳落地時：把螢幕座標轉成 overlay client 座標，對 `SlotRc(s,i)` 做 `PtIn`，命中哪個槽就寫哪個。

### 執行緒注意

- Overlay 活在**自己的 UI thread**；背包 DMA／讀遊戲記憶體應在**遊戲主執行緒**做（或讀完後 `PostMessage` 回 UI thread 改 `g_cfg`）。
- 改 `g_cfg` 需持 `g_lock`（見 `AutoPotionOverlay.cpp`）。
- **不要**在 UI thread 直接 `SendPacketData`。

### 建議公開給拖曳用的接線（尚未加，下一棒可加）

在 `AutoPotionOverlay.h` 增加類似 API（名稱可自訂）：

```cpp
// 若 overlay 可見且 (x,y) 落在某槽，寫入 Item 槽並回傳 true。
// x,y：相對 overlay client；或提供螢幕座標版本再內部 ScreenToClient。
bool AutoPotionOverlay_TryDropItem(int clientX, int clientY, int itemId);

// 可選：清空槽、查是否可見
bool AutoPotionOverlay_IsVisible();
```

實作上直接改：

```cpp
g_cfg.heal.slots[i].kind = AutoPotionSlot_Item;
g_cfg.heal.slots[i].id = itemId;
```

（或 mana），然後 `PaintLayered`；**不必立刻 Save**——與現況一致：按「儲存」才寫檔送包。

---

## 6. 目前槽位互動（無拖曳）

| 操作 | 行為 |
|------|------|
| 點空槽 | 進入編輯，鍵盤輸入數字 |
| Enter | 提交為 **Item** |
| Ctrl+Enter | 提交為 **Skill** |
| 再點已填槽 | 清空 |
| HP/MP `<` `>` | ±1；點中間數字可鍵盤改 % |
| 儲存 | pending → 主執行緒 Save+Send → 關窗 |
| 關閉／X | 只隱藏，不存 |
| 標題列 | `HTCAPTION` 可拖視窗 |

Log tag：`[AutoPotionUI]`（`Core\launcher.log`）、設定層 `[AutoPotion]`。

---

## 7. 背包記憶體線索（舊 HelperDlg，供拖曳／解析用）

`HelperDlg.cpp`（`#if 0` 內）曾用：

```text
背包管理指標：*(DWORD*)0x009A9250
物品數量：    *(int*)(bag + 0x2C)
物品指標陣列：*(DWORD*)(bag + 0x58)   → 再 [index] 取物件*
```

結構註解（可能不完整，拖曳前請用 dump 再確認）：

```cpp
#pragma pack(push, 1)
typedef struct {
  int unknow1;   // +0x00
  DWORD id;      // +0x04  註解寫「動態實體 ID」(instance objId)
  int unknow2;   // +0x08
  char *name;    // +0x0C  名稱指標
} BAGITEM_INFO;
#pragma pack(pop);
```

倉庫側另有：`FindInvItem @ 0x4B1ED0`（`WarehouseStatusHook.cpp`，以 objid 找背包件）。

### 拖曳功能真正缺的一塊

舊 Overlay 註解已寫明：

> 需要另外反組譯遊戲「**目前拖曳中物品**」的記憶體位置，還沒查。

也就是：玩家在原生 UI 按住背包格拖曳時，客戶端某處會暫存「手上那件」的 objId／itemId／gfx。下一棒應優先：

1. 在 3.81 dump／執行期找出 **drag cursor / pickup item** 全域或 UI 狀態（常見在 Action／Inventory 相關物件）。
2. 在 `WM_LBUTTONUP`（或遊戲自己的 drop 路徑）判斷滑鼠是否在我們的 overlay 槽上。
3. 從手上那件解出 **樣板 itemId**（設定檔／封包要的是 itemId，不是 instance id）。若 `BAGITEM_INFO+0x04` 只是 instance id，需再跟物品表／結構其它 offset 對到 template id（可對照伺服器 `L1ItemInstance.getItemId()` 在客戶端的對應欄位）。

原生 `LinHelperUI.xml` 的 `ItemIcon` 拖曳**不要擴充去用**（規劃文件已實測：寫死 2 格、背景不拉長）——我們走自繪 overlay 接 DMA／hook。

### 2026-09-07 追查進度（靜態 dump 分析，`tools/linmem`）

**排除掉的路**：`0x009A9250`（背包基址指標）在 `dumps/game/game_TW13081901_20260827_01.dmp` 裡被**超過 200 處**程式碼引用，範圍從 `0x00406B64` 散布到 `0x00734F7A`——這個指標被裝備／丟棄／交易／倉庫／商店等所有背包相關子系統共用，不是單一「拖曳」類別的方法集合。用這條線索純靜態逐一排查哪段是拖曳中物品狀態，成本太高、命中率太低，這次沒有繼續往下切。真正定位「拖曳中物品」大概率需要**動態/即時方法**（下面凍結比對），不是純 dump 比對能有效率解決的。

**意外找到的替代方案**（不是直接答案，但可能可以繞過整個拖曳 RE）：透過 `C_CharcterConfig`（opcode 244，伺服器端純 blob 存取，不解析內容）追出客戶端的「快捷鍵設定」解析函式：

- PacketBox 子類型 41（`S_PacketBox.CHARACTER_CONFIG`）分派位址 `0x0053CD3C` → 真正解析/套用 blob 的函式 `0x004B8A10`。
- `0x004B8A10` 是帶版本號的設定格式解析器，核心是 **24 格迴圈**，每格讀取：
  - `0x009A91C8`：byte[24]，類型旗標
  - `0x00963200`：byte[24]，第二旗標
  - `0x009A91E0`：byte[24]，第三旗標
  - `0x009A98F8`：**DWORD[24]，每格實際的道具／技能 ID**
  - 套用函式：`0x004B5400(index, 緩衝區指標, byteA, byteB)`
- 實測讀 dump：`0x009A98F8` 陣列 index=1 的值是 `0x53B`（1339，合理的道具 ID），對照 `0x009A91C8` 的旗標呈 4 格一組規律，24 格＝6 組 F 鍵快捷列×4 格。**還沒實機驗證玩家拖曳道具進某個 F 鍵格子時這個值會不會即時更新**，只確認了 dump 快照裡的值形狀合理。

**替代方案的想法**：與其自己刻「背包拖曳到我們的 overlay」，不如借用**遊戲原生 F 鍵快捷列本來就有的拖曳機制**——UI 上引導玩家「把藥水拖到某個 F 鍵格子」，我們的 DLL 定期讀 `0x009A98F8[該格]` 取得道具 ID，完全不用碰拖曳手勢本身。缺點：多一道「先設 F 鍵」的操作、佔用玩家自己的快捷列格位、還沒驗證即時更新是否可靠。

**如果之後真的要走原生拖曳**：建議動態抓——玩家實際按住道具拖曳不放時，用 Cheat Engine 之類工具凍結行程，比對「按下拖曳前」vs「拖曳中」兩份記憶體快照的差異（跟本文件開發過程中角色名稱指標鏈、`PotionTimer` 等其他位址的抓法一致），這比純靜態反組譯有效率很多。抓到候選位址後可以把 dump 或位址丟回來，我可以幫忙用 `tools/linmem` 反組譯周邊程式碼驗證/縮小範圍。

---

## 8. 建議實作順序（拖曳）

1. **查址**：手上拖曳物品的全域／物件欄位；確認 template `itemId` offset。  
2. **API**：`AutoPotionOverlay_TryDropItem`（或 PostMessage 到 UI thread）。  
3. **Hook 點**（擇一或並用）：
   - 遊戲主執行緒的滑鼠 up（已有 `WH_GETMESSAGE` `HookProc`）；
   - 或 Detour 原生 inventory drop／use 前的「放開」路徑。  
4. **命中**：overlay 可見時，螢幕座標 → overlay client → `SlotRc`。  
5. **寫槽**：`kind=Item`, `id=itemId`；可選拒絕非藥水（之後再加表）；重繪。  
6. **UX**：拖入後槽顯示 `#id`（現況）；有圖階段再畫 icon（`ui.pak`）。  
7. **不要**在 drop 當下強制送包；維持「儲存」才同步伺服器。

法術拖曳（skillId）可第二階段；伺服器暫時也不會施放 skill 槽。

---

## 9. 明確不要做／已排除

- 不要復活 HelperDlg DMA 自動喝藥（已 `#if 0`）；後端判斷版才是正路。  
- 不要為拖曳開新 opcode；設定仍走 75 長包。  
- 本階段可不做 mockup 美術／`auto_potion_ui.xml`（hardcode rect 已夠接拖曳）。  
- 不要在 UI thread 呼叫 `SendPacketData`／加密送包。

---

## 10. 驗收（拖曳完成時）

1. HOME 開 overlay，從背包拖一瓶藥水到 heal 槽 → 槽顯示 `#itemId`、kind=道具。  
2. 拖到 mana 槽同理。  
3. 按儲存 → `auto_potion.cfg` 與伺服器 log／`PotionTimer` 行為正確。  
4. 關閉不儲存 → 重開仍是檔案舊值。  
5. Overlay 隱藏時，背包拖曳行為與原廠一致（不誤吞）。

---

## 11. 給下一棒的最小閱讀清單

1. 本文件  
2. `AutoPotionOverlay.cpp`：`SlotRc`、`OnLButtonDown`、`QueueSave`、`g_lock`/`g_cfg`  
3. `AutoPotionConfig.h`（契約）  
4. `HelperDlg.cpp` 內 `GetItemCount`／`GetItem`／`BAGITEM_INFO`（僅當 DMA 起點）  
5. `C_BroadcastToPledge.java` + `PotionTimer.java`（確認 itemId 語意）

查到「拖曳中物品」位址後，請把 VA／結構 offset 補回本文件第 7 節，方便後續維護。
