# 自動喝水 Overlay：紅／藍條顯示當前 HP／MP（後端封包快取）

> 寫作日期：2026-09-08  
> 背景：BUFF 頁左下「治療／補魔」的紅條、藍條要顯示角色**當前／最大**血魔。  
> 已否決：客戶端 DMA（`0xBDC828` / `0xBDC834` XOR + max 明文位址）——換版脆弱，且與「啟動會經過 pc、應從後端取」方向不符。  
> 本文件只寫**這一輪要做的計畫**，實作前給使用者過目確認。

---

## 0. 已確認的前提

1. **`pc` 只在伺服器**。Overlay 在 `LauncherDll`，無法直接讀 `L1PcInstance`；「從後端取」= 吃 S→C 封包後在客戶端快取。
2. **「啟動」 alone 不夠當血條來源**。`C_PlaySupport` 啟停時伺服器確實有 `pc`，可順便回一包當**初始值**；但血魔會因戰鬥／回血／喝藥持續變，條要即時，必須有**持續更新**路徑。
3. **伺服器本來就會推血魔**：
   - `S_HPUpdate`：`OpcodeServer.S_OPCODE_HPUPDATE = 225`  
     格式：`writeC(225)` + `writeH(cur)` + `writeH(max)`（cur/max 夾在 1～32767）
   - `S_MPUpdate`：`S_OPCODE_MPUPDATE = 33`  
     格式：`writeC(33)` + `writeH(cur)` + `writeH(max)`（邊界處理略不同，見 Java）
   - 登入等時機也會送（如 `C_LoginToServer`）；`setCurrentHp`／`setCurrentMp` 等路徑會再送。
4. **現有 Mimir cave（`0x0053939A`）攔不到這兩包的外層 opcode**。該點的 `EAX` 是 **PacketBox 子類型**，不是 225／33。`my_recv` 在 socket 層也難自己解到穩定明文（既有註解已說明）。因此不能假設「沿用 Mimir cave 多兩個 sentinel」就能吃到 HP／MP。
5. **Overlay UI 已接好條＋文字槽**：`DrawSection(..., cur, max)`；目前暫接 `ReadPlayerVitals`（DMA）——計畫落地後要整段拿掉。

---

## 1. 目標／非目標

### 目標

- 紅條：填滿比例 = `curHp / maxHp`，文字 `curHp/maxHp`。
- 藍條：同上，`curMp/maxMp`。
- 數值來源 = **伺服器權威**（封包），不讀固定記憶體血魔位址。
- Overlay 開啟期間，血魔變化後條會跟著變（允許極短延遲，例如下一幀重繪）。

### 非目標（本輪不做）

- 改自動喝藥判定邏輯（伺服器 `PotionTimer` 仍用自己的 `pc`）。
- 改封包借位 opcode 75／設定存檔格式。
- 做「向伺服器主動詢問血魔」的新 C→S 查詢（除非攔截方案卡死才當備援，見 §4）。
- 完美處理尚未收到任何 HP／MP 包時的顯示美學（可先顯示 `—/—` 或 `0/0`）。

---

## 2. 建議主路徑：攔截既有 `S_HPUpdate`／`S_MPUpdate`

```
伺服器 pc 血魔變更
  → 既有 sendPackets(S_HPUpdate / S_MPUpdate)
  → 客戶端解密後進入 ProcessPacket（或等價分派）
  → 【新增】我們在「外層 opcode 可見」的點偷看 225 / 33
  → 解析 writeH+writeH，寫入 g_vitals 快取
  → 若 Overlay 可見 → Invalidate／下一幀 DrawBuffPage 讀快取畫條
  → 原生 handler 照常跑（遊戲自己的血條也要更新，我們只旁觀，不吞包）
```

**為什麼優先這條**

- 伺服器**零改**或幾乎零改（最多可選：Overlay 開啟／按啟動時強制再送一次當 seed）。
- 與遊戲 UI 同源，不會跟真實血魔脫節。
- 符合「從後端取」且不綁死記憶體位址。

**關鍵風險（實作前必須先驗證）**

- 要找到「解密後、外層 opcode 還在 EAX／封包頭第一 byte」的穩定攔截點。  
  已知 **不可用**：`0x0053939A` PacketBox 子類型表。  
  候選方向（實作 Phase 0）：
  1. 往 ProcessPacket **更前面**找讀取 `packet[0]`／外層 jump table 的位置；
  2. 或直接 hook 遊戲內 **HP／MP update handler** 函式入口（跳轉表 `0x5415B4` 對應項若其實是外層表的另一段，需 CE 釐清——目前註解認為該表是 PacketBox 子類型用）；
  3. 旁觀解析後**必須放行**原生處理（與 Mimir sentinel「吃掉包」相反）。

---

## 3. 客戶端設計（主路徑）

### 3.1 快取

```cpp
struct PlayerVitalsCache {
  int curHp = 0, maxHp = 0;
  int curMp = 0, maxMp = 0;
  bool hasHp = false;
  bool hasMp = false;
};
// 由封包攔截執行緒／遊戲主執行緒寫入；Draw 只讀。
```

- 寫入時夾值：`cur >= 0`，`max >= 1`（若 max 異常則不更新該側或標記無效）。
- `hasHp`／`hasMp` 分開：可能先收到其中一包。

### 3.2 API（給 hook 呼叫）

```cpp
void AutoPotionOverlay_OnHpUpdate(int cur, int max);
void AutoPotionOverlay_OnMpUpdate(int cur, int max);
```

內部：更新快取；若視窗可見則 `InvalidateRect`（或沿用既有 timer 重繪，見下）。

### 3.3 繪製

- `DrawBuffPage`：**刪除** `ReadPlayerVitals`／`DecodeXorStat`／相關常數。
- 改讀 `g_vitals`；`!hasHp` 時文字用 `—/—`、條寬 0（或保持空槽）。
- **移除對 DMA timer 的依賴**：現有 `TIMER_VITALS`（250ms）若只為輪詢記憶體，改為：
  - **優先**：封包到達時 Invalidate（事件驅動）；
  - 若仍要 caret 閃爍等 UI timer，與 vitals **解耦**，不要為了血條去 poll 記憶體。

### 3.4 新檔／掛點（暫定）

| 項目 | 說明 |
|------|------|
| `VitalsUpdateHook.cpp/.h`（名稱可調） | 安裝外層 opcode 旁觀 hook；解析 225／33；呼叫 Overlay API |
| `AutoPotionOverlay.cpp` | 快取 + 畫條；移除 DMA |
| `LauncherDll.cpp` | Init／Uninit 掛上 hook（比照 Mimir） |

封包長度：預期 body 至少 `1 + 2 + 2`（opcode + cur + max）；長度異常則忽略、不改快取。

---

## 4. 備援路徑（僅當 Phase 0 證明外層攔截不可行）

走 **PacketBox 借位子類型**（與 Mimir／道具解析同一 cave），伺服器主動推 vitals。

| 項目 | 建議 |
|------|------|
| 子類型 | 另借一個未用常數（**不要**重用 16／32）；實作前再掃 `S_PacketBox.java` |
| S→C 格式（示意） | subtype(c) + curHp(h) + maxHp(h) + curMp(h) + maxMp(h) |
| 何時送 | （1）`C_PlaySupport` 啟動／存檔成功；（2）`setCurrentHp`／`setCurrentMp` 或既有送 `S_HPUpdate`／`S_MPUpdate` 的同一處**多送一包**（易漏點，要集中包裝） |
| 客戶端 | 既有 cave 多一個 sentinel：**旁觀或吃掉**均可（若吃掉則遊戲血條仍靠原生 225／33） |

代價：伺服器改動面較大、易漏更新點；只有主路徑卡死才啟用。  
**不要**用「只在啟動時送一次」當最終方案。

可選輕量 seed（可與主路徑並用，非取代）：

- Overlay 開啟或按「啟動」時，伺服器再 `sendPackets(new S_HPUpdate(pc))` + `S_MPUpdate(pc)` 一次，縮短「開面板後空白」窗口（主路徑攔 225／33 時特別有用）。

---

## 5. 實作階段

| Phase | 內容 | 完成標準 |
|-------|------|----------|
| **0 探勘** | CE／既有 ProcessPacket 註解，定位外層 opcode 可見點；對 225／33 下斷確認 EAX／`[ebp+8]` 封包頭 | 寫下位址、是否可旁觀、是否需吞包；決定走 §2 或 §4 |
| **1 快取＋UI** | 移除 DMA；接 `g_vitals`；無資料顯示 `—/—`；可用測試函式手動灌值驗證畫條 | Release Win32 建置過；HOME 開面板條 UI 正確 |
| **2 Hook** | 依 Phase 0 結果接 225／33（或 PacketBox 備援） | 打怪／回血後面板數字與遊戲血條一致 |
| **3（可選）Seed** | 啟動／開面板時伺服器重送 HP／MP | 開面板後幾乎立刻有數字 |
| **4 清理** | 刪 DMA 常數／timer 輪詢記憶體；補一句交接註解到 `現況與圖示交接.md`（若需要） | 無殘留位址讀血魔 |

建議順序：**0 → 1 → 2**；1 可與 0 部分並行（先把 DMA 拿掉，避免錯誤方向留在 tree 裡）。

---

## 6. 測試計畫

1. 登入進世界 → 開 Overlay（HOME）→ BUFF 頁紅／藍條應有合理 `cur/max`（或先 `—/—` 再在下一包出現）。
2. 受傷／回血／喝紅 → 紅條與遊戲 UI 同步（允許一幀延遲）。
3. 耗魔／回魔 → 藍條同步。
4. 按「啟動／停止」→ 不應靠 DMA；啟停後條仍繼續更新（主路徑下與啟停無關）。
5. 關 Overlay 再開 → 快取可保留上次值；若進程內從未收過包則 `—/—`。
6. 換角／重登 → 應被新的 HP／MP 包覆蓋（若有殘留舊值需在登出／選角清快取——Phase 2 一併決定清點，可掛在既有 disconnect／login 觀察處）。

---

## 7. 決策請確認

1. **主路徑**：攔既有 `S_HPUpdate`(225)／`S_MPUpdate`(33)，伺服器盡量不改——是否同意？
2. **Phase 0 若失敗**：是否接受改走 PacketBox 借位 + 伺服器在血魔變更處多推一包？
3. **無資料時 UI**：`—/—` 還是 `0/0`？
4. **可選 seed**：啟動／開面板時伺服器強制再送一次 HP／MP——要不要做？

確認後再動程式碼（先 Phase 0＋拿掉 DMA）。
