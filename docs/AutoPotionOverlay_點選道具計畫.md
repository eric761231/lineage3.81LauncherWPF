# 自動喝水：「點格子→點背包道具」選道具計畫

> 寫作日期：2026-09-08
> 取代對象：`AutoPotionOverlay_拖曳交接.md` 裡「背包拖曳進槽」那條路——已確認**這個遊戲的拖曳沒有 SetCapture**，放開滑鼠的 `WM_LBUTTONUP` 會直接送到我們自己 overlay 視窗自己的執行緒/訊息佇列，跟遊戲主執行緒的 `HookProc` 完全不同路，之前的驗證 log 因此一直看錯地方（見對話記錄，這裡不重複）。改用「點擊」取代「拖曳」，架構簡單很多。
> 本文件只寫**這一輪要做的計畫**，實作前给使用者過目確認。

---

## 0. 已確認的前提（不用重查）

1. **背包指標鏈可靠**（`InventoryDebugHook.cpp` 已驗證）：
   ```
   背包基址：*(DWORD*)0x009A9250
   物品數量：*(int*)(bag + 0x2C)
   物品指標陣列：*(DWORD*)(bag + 0x58) → [index] 取物件指標
   struct BAGITEM_INFO {
     int unknow1;   // +0x00：所有物品共用同一個值 0x008D0A30，是 vtable/類別指標，不是道具資料
     DWORD id;      // +0x04：**實體 id（instance objId）**，不是樣板 id
     int unknow2;   // +0x08：目前只看到 0/1 在點擊瞬間變動，用途不明，非重點
     char *name;    // +0x0C：道具顯示名稱字串指標（Big5），可靠
   };
   ```
2. **`id`（+0x04）證實是實體 id，不是樣板 id**：背包裡 5 筆同名道具（`§ø¥Áªº¿򪫠(®ګ´)`），`id` 分別是 `3803036/3803032/3803040/3803038/3803034`——彼此靠近但不同，是動態配置的實體序號特徵。`+0x10` 之後的欄位在藥水類道具全部是 0，找不到穩定共用的樣板 id 欄位（且懷疑再往後讀會讀到下一個物件，不是本物件資料，不建議繼續往這個方向擴大 hex dump）。
3. **決定**：樣板 id 由**伺服器解析**（伺服器本來就有 `pc.getInventory().getItem(objId)` → `L1ItemInstance.getItemId()` 這條路，不用在客戶端額外查記憶體）。使用者已確認選這條路（相對「存實體 id、遊戲端换新庫存就失效」更穩，代價是要多一組來回封包）。
4. **`WM_LBUTTONUP` 訊息路由**：遊戲的拖曳沒有 `SetCapture()`，游標飄到我們視窗上面時，Windows 直接把滑鼠訊息送給我們自己的視窗（游標從遊戲自畫圖示變回系統箭頭就是證據）。**這代表點擊我們 overlay 上的格子，訊息會進 `AutoPotionOverlay.cpp` 自己的執行緒**（`WndProc`／`OnLButtonDown`／`OnLButtonUp`，現有程式碼已經在處理），**點擊遊戲背包裡的道具，訊息會進遊戲主執行緒**，被 `LauncherDll.cpp` 的 `HookProc`（`WH_GETMESSAGE`）看到——兩邊各自處理各自看得到的點擊，不用互相轉發。
5. **PacketBox 借位子類型可行**（`MimirPowerHook.cpp` 已經穩定在跑）：分派點 `0x0053939A`，`jmp dword ptr [eax*4+0x5415B4]`，`eax` 是子類型值（不是外層 opcode 250）。Mimir 用 `0x10`(16)。掃過 `S_PacketBox.java` 全部常數，`32`(0x20) 沒被使用（`31`=MSG_FEEL_GOOD、`33`=SOMETHING1，中間空號），這次借 `32`。

---

## 1. 整體流程

```
玩家點 overlay 上的空格（或已填格，改成重新選）
  → 進入「選擇中」狀態（UI 顯示「請選擇道具」，比照現有編輯模式）

玩家點背包道具
  → HookProc（遊戲主執行緒）偵測到點擊落在某個背包格
  → 讀 BAGITEM_INFO，取得 objId + 顯示名稱（本機可直接讀到，先顯示在 UI 當「確認中」的暫時標籤）
  → 送一個新的小封包給伺服器：借位 opcode 75，長度 7（跟現有 2/5/54 三種長度都不撞）
     格式：opcode(c) + section(c) + slotIndex(c) + objId(d)

伺服器（C_BroadcastToPledge.java 新分支，decrypt.length==7）
  → 用 objId 查玩家背包：L1ItemInstance item = pc.getInventory().getItem(objId)
  → 查不到 / objId 不屬於這個玩家 → 回覆失敗
  → 查到 → 樣板 id = item.getItemId()，名稱 = item.getName()
  → 回一個新的 S→C 封包（PacketBox 借位子類型 32），內容：
     success(c) + section(c) + slotIndex(c) + templateItemId(d) + name(字串)

客戶端（比照 MimirPowerHook.cpp 的 PacketBox 分派攔截手法，同一個 cave 多加一個 sentinel）
  → 收到回覆，比對目前是不是還在等這個 section/slotIndex 的「選擇中」狀態
     （玩家可能已經取消/選別的格子，過期的回覆要丟棄，不寫入）
  → 相符：寫入 g_cfg.<section>.slots[slotIndex] = {kind: Item, id: templateItemId}，
     順便快取顯示名稱給 UI 用（不送進封包，只是畫面好看）
  → 離開「選擇中」狀態，重繪
  → 不自動存檔——維持現有規則，按「儲存」才真的寫檔＋送 54-byte 完整設定包
```

---

## 2. 客戶端修改

### 2.1 `AutoPotionOverlay.h` / `.cpp`

- 新增選擇狀態（比照現有 `g_editMode`/`g_editSlot` 的做法，另開一組，不要共用避免邏輯打架）：
  ```cpp
  int g_pickSection = -1; // 0=heal 1=mana，-1=沒有在選
  int g_pickSlot = -1;
  std::wstring g_pickPendingName; // 本機讀到的暫時顯示名稱（等伺服器回覆前先顯示這個）
  ```
- 點擊空格／已填格：跟現在一樣可以進「輸入模式」（保留鍵盤輸入當備用/手動覆寫路徑，不要拿掉——樣板 id 有時候可能需要手動指定，例如伺服器解析失敗時的備援），但**新增**：同一個點擊事件也同時把 `g_pickSection/g_pickSlot` 設起來，代表「現在也接受背包點擊」。兩種輸入方式並存：玩家可以繼續打字 Enter 提交（跟現在一樣），也可以改成去點背包（新路徑）。
- UI 提示文字：選擇中的格子顯示「請選擇道具」或已讀到暫時名稱時顯示該名稱＋「確認中…」。
- 新增函式（提供給 `HookProc`／新的 PacketBox 攔截呼叫）：
  ```cpp
  // 遊戲主執行緒偵測到背包點擊、讀到 objId+暫時名稱後呼叫。純粹記錄「已送出等待
  // 解析」的狀態＋更新暫時顯示名稱，不改 kind/id（那要等伺服器回覆才算數）。
  void AutoPotionOverlay_OnPickCandidate(int section, int slot, const wchar_t *pendingName);

  // PacketBox 子類型 32 回覆抵達時呼叫（見下方 AutoPotionResolveHook.cpp）。
  // 內部比對 g_pickSection/g_pickSlot 是否還相符，不符就丟棄（玩家可能中途取消）。
  void AutoPotionOverlay_OnResolveReply(bool success, int section, int slot,
                                        int templateItemId, const wchar_t *name);
  ```
  這兩個函式都可能在**非 overlay 執行緒**呼叫（`HookProc`＝遊戲主執行緒，PacketBox 攔截也是遊戲主執行緒／網路相關執行緒），內部要 `PostMessageW` 丟一個新的自訂訊息（例如 `WM_AUTOPOTION_PICK_CANDIDATE`／`WM_AUTOPOTION_RESOLVE_REPLY`）回 overlay 自己的執行緒處理，不要跨執行緒直接動 `g_cfg`（比照現有 `WM_SHOW_AUTOPOTION` 的模式，用訊息把資料帶過去，實際寫 `g_cfg` 的邏輯留在 `WndProc` 裡）。

### 2.2 `InventoryDebugHook.h` / `.cpp` → 精簡成正式功能

- 拿掉「印全部 49 筆」那段迴圈式 log（已經達成任務、造成卡頓，這輪要移除/精簡）。
- 新增一個「找出某螢幕座標命中的那一格道具」函式（不需要解螢幕座標→格子index的版面公式，直接沿用**點擊當下背包指標鏈給的『目前有哪些道具』**這個資訊——但這裡還是需要知道「玩家點的是第幾格」才能挑出陣列裡對應的那個 `BAGITEM_INFO`）。

  **待確認的小缺口**：目前只驗證了「陣列內容讀得到」，還沒驗證「陣列 index 是否等於畫面格子順序」。上一輪日誌因為只點了背包最後一格（第 49 格＝index 48），只驗證了「最後一格」這一個資料點，不足以確認整個陣列順序規則。這次實作時建議**用點擊瞬間 `unknow2` 那個會變動的欄位**當作「這是玩家剛點的那格」的判斷依據（已觀察到只有被點的那筆 `unknow2` 會變 1，其餘維持 0），比自己猜座標公式可靠、也不用額外查版面。做法：
  ```cpp
  // 掃過全部 itemCount 筆，回傳 unknow2!=0 的那一筆（如果剛好有多筆同時非 0，
  // 取第一筆；理論上同一時間只會有一筆被點）。
  BAGITEM_INFO *FindJustClickedItem();
  ```
  這個函式要在 `WM_LBUTTONDOWN` **當下**（或極短時間內）呼叫，`unknow2` 這個旗標似乎只在按下瞬間短暫非 0（上一輪 log 顯示 LBUTTONDOWN/LBUTTONUP 兩次快照之間就已經在切換），時機要抓準，建議优先在 `WM_LBUTTONDOWN` 那一刻讀。若這個做法實測不穩定（例如兩次點擊之間旗標沒有可靠地變動），退回方案：改成真的量測背包視窗版面（螢幕座標→格子 index 的公式），量測方式跟 `AutoPotionOverlay.cpp` 的 `SlotRc` 概念一樣，只是這次是量遊戲原生背包視窗，需要另外開一輪實測（在遊戲裡挪動滑鼠到已知格子、記錄螢幕座標，跟 log 對照）。

### 2.3 `LauncherDll.cpp`（`HookProc`）

- `WM_LBUTTONDOWN` 時，若 `AutoPotionOverlay` 目前有 `g_pickSection/g_pickSlot`（透過一個新的查詢函式 `AutoPotionOverlay_IsPicking(int*section,int*slot)` 取得）：
  1. 呼叫 `FindJustClickedItem()` 找出剛被點的道具。
  2. 找到 → 取 `objId`（`item->id`）與名稱（`item->name`，注意 Big5，字串生命週期只在這次事件內有效，要立刻複製/轉寬字元，不要保留指標跨執行緒/跨訊息使用）。
  3. 呼叫 `AutoPotionOverlay_OnPickCandidate(section, slot, wideName)` 更新 UI 暫時顯示。
  4. 呼叫 `SendPacketData("cccd", (int)kOpcodeBroadcastToPledge, section, slot, (int)objId)` 送出 7-byte 解析請求（在遊戲主執行緒呼叫，符合現有「`SendPacketData` 只能在主執行緒呼叫」的規則，天然滿足，不用額外處理）。
  5. 找不到（點擊沒對到任何道具，例如點到背包空格）→ 不送包，維持選擇中狀態，讓玩家可以再點一次。

---

## 3. 伺服器修改

### 3.1 `C_BroadcastToPledge.java`

新增第四個長度分支（**放在既有 54-byte 分支之後判斷**，因為要先排除掉更長的封包）：

```java
} else if (decrypt != null && decrypt.length == 7) {
    handleResolveItemRequest(pc);
}
```

```java
/**
 * 自動喝水「點背包道具解析樣板 id」請求：section(c) + slotIndex(c) + objId(d)。
 * 只查詢、不修改任何 pc 狀態；結果透過 S_AutoPotionResolveItem 回給客戶端，
 * 客戶端自己決定要不要寫入 UI（伺服器這邊完全 stateless，不用等 Save）。
 */
private void handleResolveItemRequest(final L1PcInstance pc) {
    final int section = readC();
    final int slotIndex = readC();
    final int objId = readD();

    final L1ItemInstance item = pc.getInventory().getItem(objId);
    if (item == null) {
        pc.sendPackets(new S_AutoPotionResolveItem(section, slotIndex));
        return; // success=false 建構子，見下方
    }
    pc.sendPackets(new S_AutoPotionResolveItem(section, slotIndex,
            item.getItemId(), item.getName()));
}
```

（`readD()` 目前這個 class 沒有 import/使用過 `L1ItemInstance`，記得補 `import com.lineage.server.model.Instance.L1ItemInstance;`。）

### 3.2 新檔案 `S_AutoPotionResolveItem.java`

比照 `S_PacketBoxConfig.java` 的寫法（同樣是 `S_OPCODE_PACKETBOX` 外殼）：

```java
package com.lineage.server.serverpackets;

public class S_AutoPotionResolveItem extends ServerBasePacket {

    private static final int SUBTYPE = 32; // 借位，未使用值，見計畫文件第 0.5 節

    private byte[] _byte;
    private final boolean _success;
    private final int _section;
    private final int _slotIndex;
    private final int _templateItemId;
    private final String _name;

    /** 失敗建構子。 */
    public S_AutoPotionResolveItem(int section, int slotIndex) {
        _success = false;
        _section = section;
        _slotIndex = slotIndex;
        _templateItemId = 0;
        _name = "";
    }

    /** 成功建構子。 */
    public S_AutoPotionResolveItem(int section, int slotIndex, int templateItemId, String name) {
        _success = true;
        _section = section;
        _slotIndex = slotIndex;
        _templateItemId = templateItemId;
        _name = name != null ? name : "";
    }

    @Override
    public byte[] getContent() {
        if (_byte == null) {
            writeC(S_OPCODE_PACKETBOX);
            writeC(SUBTYPE);
            writeC(_success ? 1 : 0);
            writeC(_section);
            writeC(_slotIndex);
            writeD(_templateItemId);
            writeS(_name); // 對齊 S_PacketBoxMimir 等既有寫法，Big5 帶結尾 0
            _byte = getBytes();
        }
        return _byte;
    }

    @Override
    public String getType() {
        return getClass().getSimpleName();
    }
}
```

> 實作前務必打開一個既有的 `S_PacketBox*.java` 檔案核對 `writeC`/`writeD`/`writeS` 的正確呼叫順序與 `getBytes()`/`getContent()` 慣例（不同專案版本可能有些微差異，上面是示意，不是照抄就能過編譯，要對照這個 repo 實際的 base class）。

---

## 4. 客戶端接收回覆（PacketBox 借位子類型 32）

比照 `MimirPowerHook.cpp` 的 `MimirDispatchCave`／`OnMimirDispatch`：

1. `MimirDispatchCave` 的內嵌組合語言目前只判斷 `cmp eax, 0x10`（Mimir 專用），**這次要多加一個 `cmp eax, 0x20` 分支**，命中就呼叫新的 `OnAutoPotionResolveDispatch(pktData)`（跟 `OnMimirDispatch` 平行的一個新函式，可以放在 `AutoPotionOverlay.cpp` 或新開一個 `AutoPotionResolveHook.cpp`，簽名對齊 `extern "C" DWORD __cdecl OnAutoPotionResolveDispatch(const BYTE *pktData)`，回傳 1 代表吃掉這包）。
2. `pktData` 從 subtype byte 之後開始（子類型本身已經被讀進 EAX，不在 `pktData` 裡）——對照我們的 payload 順序：`success(1B)+section(1B)+slotIndex(1B)+templateItemId(4B)+name(C字串)`。
3. 解析完呼叫 `AutoPotionOverlay_OnResolveReply(success, section, slotIndex, templateItemId, wideName)`（`name` 是 Big5，要用 `MultiByteToWideChar(950, ...)` 轉寬字元，比照 `MimirPowerOverlay.cpp` 的 `Big5ToWide`）。
4. **修改既有函式是有風險的**：`MimirDispatchCave` 是已經穩定運作的程式碼，這次要在裡面加一個新分支。務必：
   - 先讀懂 `MimirPowerHook.cpp` 現有的 naked asm 全部內容（含檔案開頭那段位址穩定性/跳轉表機制的說明），不要憑印象改。
   - 加分支時比照現有 `cmp eax, 0x10 / jne pass_through` 的寫法複製一份改成 `0x20`，不要動到 Mimir 那段既有邏輯本身。
   - 改完要同時測 Mimir 功能還正常（密米爾之泉開得起來）＋新功能，不能只測新的。

---

## 5. 邊界情況／要處理的錯誤

| 情況 | 處理方式 |
|---|---|
| 玩家點背包空格（沒點到任何道具） | 不送包，維持「選擇中」，可以再點一次或按 Esc 取消 |
| 伺服器查無此 objId（道具被移走/使用掉，剛好卡在請求送出後、伺服器處理前） | 回覆 `success=false`，客戶端顯示「選擇失敗，請重試」，格子維持原本內容（不要清空玩家原本已存的設定） |
| 玩家送出請求後，在收到回覆前就切到別的格子重新選 | 客戶端收到回覆時比對 `g_pickSection/g_pickSlot` 是否還等於回覆帶的 section/slot；不符就整包丟棄不處理 |
| 玩家送出請求後直接關閉 overlay | 同上，靠 section/slot 比對自然擋掉；不需要額外取消機制 |
| 玩家點了不是道具的東西（例如角色模型、地板） | 背包點擊偵測本來就只在「有偵測到 `BAGITEM_INFO`」時送包，點別的地方自然不會觸發 |
| 法術／技能槽 | **這次不做**——技能視窗完全沒反組譯過，維持現有鍵盤輸入（Ctrl+Enter）當唯一設定方式 |

---

## 6. 明確不做（這一輪範圍外）

- 不畫道具圖示（icon），繼續顯示文字（id 或名稱）。
- 不做技能／法術的點選（維持鍵盤輸入）。
- 不做「背包格子座標量測」除非上面 3.2 節的 `unknow2` 判斷法實測不可靠才退回這條路。
- 不改封包借位方案（繼續用 opcode 75 分支長度＋PacketBox 借位子類型），不開新 opcode。
- 不做道具白名單檢查（例如擋掉玩家選一把武器當「藥水」）——先讓功能接通，這類防呆之後再加。

---

## 7. 實作順序建議

1. 伺服器：`C_BroadcastToPledge.java` 新分支 + `S_AutoPotionResolveItem.java`（獨立可先寫完，不影響現有功能，寫完可以先用假封包測試伺服器端邏輯）。
2. 客戶端：`InventoryDebugHook.cpp` 精簡＋加 `FindJustClickedItem()`，先只 log 驗證「點哪個道具，抓到的 objId/name 對不對」，不急著送包。
3. 客戶端：`AutoPotionOverlay.cpp` 加選擇狀態＋兩個新 API，UI 先能顯示「選擇中」/「確認中」。
4. 客戶端：`LauncherDll.cpp` `HookProc` 接上「偵測背包點擊→送 7-byte 請求」。
5. 客戶端：`MimirDispatchCave` 加子類型 32 分支＋ `OnAutoPotionResolveDispatch`，接上 `AutoPotionOverlay_OnResolveReply`。
6. 端到端測試：點格子→點藥水→確認格子顯示正確道具、`auto_potion.cfg` 存檔內容正確、伺服器 `PotionTimer` 行為正確。
7. 回頭測 Mimir 之泉功能沒有被步驟 5 影響到。

---

## 8. 給下一棒／驗收清單

- [ ] 點 overlay 空格 → 顯示「請選擇道具」
- [ ] 點背包道具 → 格子先顯示暫時名稱＋「確認中」
- [ ] 伺服器回覆後 → 格子顯示正確樣板 id（可以先用道具名稱反查伺服器資料庫確認 id 對不對）
- [ ] 按儲存 → `auto_potion.cfg` 寫入正確 id，伺服器 `PotionTimer` 能正確找到該道具使用
- [ ] 中途取消（點別格/關視窗）不會寫入錯誤資料
- [ ] Mimir 之泉功能未受影響
- [ ] 效能：不再有「每次點擊印 49 筆」這種高頻 log
