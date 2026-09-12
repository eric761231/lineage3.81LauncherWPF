# PSS 封包與頁面搬遷計畫（2026-09-12）

對齊目前共識：BUFF 頁以道具為主；右上自訂格**只准技能**（技能欄尚未接，該區先鎖）；其他頁全是打勾；自動施放不要再塞進 opcode 75。舊文件 `docs/AutoPotion_封包借位擴充計畫.md` 裡「新功能繼續往 75 加長包」的方向**作廢**，以本檔為準。

客戶端已改名：`PssConfig`／`PssOverlay`／`PlaySupportSystem`。Java `L1PlaySupportSystem` 名稱不變。設定檔仍是 `Core\auto_potion.cfg`。

---

## 1. 目標切分

| 通道 | Opcode／借位 | 負責 |
|---|---|---|
| 恢復 | **75** `C_PlaySupport` | 治療／補魔 62-byte、點背包解析 8-byte、面板開關 4-byte（0x56） |
| 道具頁＋其他頁 | **128** `C_SecurityStatus` | 刪除／溶解（0x58／0x59，已上）；其他頁 **flags 打勾**（吃肉／修武先搬） |
| BUFF 固定＋自訂施放 | **另借一個 C 包** | 左上固定區道具（解毒／料理／BUFF 藥）；右上技能（技能欄完成後） |
| 密米爾 | PacketBox 16 | 不屬 PSS；cave 仍在 `MimirPowerHook`，PSS 子類型轉 `PlaySupportSystem` |

原生約束：

- **75**：原生血盟短包（約 2 或 5 bytes）。自訂包必須是現有固定長度 4／8／62，喝水判斷必須 **剛好 == 62**（`>= 62` 會把任何長包當藥水；Java 已改 ==）。
- **128**：原生城堡治安 **剛好 5 bytes**。其餘長度看 magic。flags 必須 **4 bytes**（≠5）。名單 **只帶實際 n 個 d**，禁止固定 20／40 個 d。
- **SendPacketData @ 0x580E50** 必須在遊戲主執行緒，且 **組包途中不可再進 `my_send` 裡的 Pump**（會重入把加密打亂）。事故見 `docs/PSS_128名單_SendPacketData重入與假盟信_2026-09-12.md`。
- 新 C 包：選原生長度很死、本服可關的冷門 opcode；用 magic 分流，不要再用「長度猜功能」。

S 包仍走 PacketBox 空號（0～183、Java 沒用過）。已用：16 密米爾、32 解析、39 vitals、46 槽數量、47 名單。BUFF 剩餘時間另借新子類型。

---

## 2. BUFF 頁四宮格（選法＋封包）

| 區 | 只能放 | 選法 | C 包 | 後端觸發 |
|---|---|---|---|---|
| 左下治療／補魔 | 道具 | 點背包（已有） | 75 | HP%／MP% |
| 左上 BUFF-固定 | 道具 | 點背包（沿用 resolve，換 section／白名單） | 新 C | 中毒／料理沒了／BUFF 沒了或快過期 |
| 右上 BUFF-自訂 | **只有技能** | 技能欄（**未做，先鎖格子**） | 同一新 C | 效果沒了再放；禁止背包 objId |
| 右下變身 | 另議 | — | 不進本輪 | — |

點背包解析：治療／補魔維持 75、length==8。固定區若也走 75 的 8-byte，要加 section 編號且白名單分開；或解析改掛新 C 的 magic。**不要**讓固定區 itemId 寫進 62-byte 的 heal／mana 槽。

啟動鈕：現況 `autoPotionEnabled` 只開藥水 timer。BUFF 區之後用獨立開關（可同一顆 UI，後端兩個 timer），避免技能欄沒接就開始施放。

---

## 3. 其他頁打勾 → 128 flags

畫面 6 格：全白天、自動修理武器、海底抽水、自動吃肉、顯示傷害、待設定。

| 格子 | 進伺服器？ | 搬遷 |
|---|---|---|
| 修武、吃肉 | 是 | **已搬**：128 magic 0x5A、4-byte flags（bit0 吃肉、bit1 修武），長度 ≠ 5。不再上傳 itemId |
| 顯示傷害 | 否 | 只寫本機 cfg／`AttackDamageHook` |
| 全白天、抽水、待設定 | 暫否 | 本機佔位；真要後端再佔 flags bit |

建議 128 magic（可微調，**數值一旦上線勿改**）：

| Magic | 用途 |
|---|---|
| 0x58 | 名單第一段（覆寫；每包最多 20 個 itemId） |
| 0x59 | 請伺服器推名單（PacketBox 47） |
| 0x5A | 其他頁 flags（4-byte：opcode+magic+flags+pad） |
| 0x5B | 名單續段（追加，不清空；與 0x58 合起來最多 40 筆） |

75 的 0x56／0x57 與 128 的 magic 不相通。搬遷過渡：後端 75 仍吃 length==12，前端改送 128 的 0x5A 後再刪 75 的 status 分支。

---

## 4. 階段（可分開送測）

### 階段 0（已完成）

- 檔名：`PssConfig`／`PssOverlay`／`PlaySupportSystem`
- 道具頁刪除／溶解：128 + PacketBox 47
- 密米爾 cave 只留 16，PSS 子類型轉出

### 階段 1 — 凍結 75、降低誤吃（已完成 2026-09-12）

- `C_PlaySupport`：喝水改 **`decrypt.length == 62`**（不要 `>=`）
- 註解寫死：75 只剩恢復＋面板

### 階段 2 — 吃肉／修武搬到 128（已完成 2026-09-12）

1. Java `C_SecurityStatus`：length≠5 且 magic 0x5A → 寫 `eatMeat`／`whetstone`，開停 `StatusSupportTimer`
2. `PssConfig_SendStatusToServer` 改送 128 的 4-byte flags；不再送 75 的 12-byte
3. cfg 仍可留 `eatMeatItemId`，不再上傳（後端固定 0＝白名單自選）
4. 刪 `C_PlaySupport.handleStatusSupport` 與 length==12 分支

### 階段 3 — 左上 BUFF-固定（道具，獨立 C）

1. 選定新 C opcode（掃 `OpcodesClient` + 原生長度）；Java 新 handler 或既有 class 用 magic
2. Overlay：左上可點格、點背包、白名單（解毒／料理／BUFF 藥 classname）
3. 本機 cfg 新 section；權威仍本機，開面板／儲存灌 State
4. 後端 timer：有毒才解毒、料理／BUFF 效果消失才用；與 `PotionTimer` 分開
5. PacketBox 可推固定區槽數量／剩餘時間（新子類型）
6. 右上仍鎖

### 階段 4 — 技能欄＋右上 BUFF-自訂

1. 反組／hook 技能欄點選 → skillId（對齊現在背包 objId）
2. 右上只接受技能；背包點選回覆失敗
3. 同一新 C 包另一 magic 或同一清單結構用 kind=skill
4. 自動施放才打開

### 階段 5 — 之後（本檔只記入口）

- 轉職：優先 128 新 magic，或等擴充通道穩定再獨立
- 變身格、傳送頁：未定，不進 75
- **道具頁提煉黑魔石／製作魔法卷軸**：見 `docs/PSS_道具頁_黑魔石與魔法卷軸計畫.md`（尚未實作；黑魔石走 128 magic 0x5C，卷軸這輪只留區域）

---

## 5. 程式對照（搬的時候改哪）

**客戶端**

- `PssConfig.cpp`：75 只留藥水／resolve／0x56；status 改 128 0x5A
- `PssOverlay.cpp`：其他頁打勾仍 `QueueSave`；左上／右上分 section
- `PlaySupportSystem.cpp`：只加新 PacketBox 子類型，不要把邏輯寫回 `MimirPowerHook`

**伺服器**

- `C_PlaySupport.java`：階段 1 長度、階段 2 刪 status
- `C_SecurityStatus.java`：加 0x5A；length==5 原生不動
- `L1PlaySupportState.java`：flags 可留 `eatMeat`／`whetstone`；BUFF 固定／自訂新陣列
- 新 timer 不要塞進 `PotionTimer`

---

## 6. 明確不做

- 75 再加長包或 `length >= 62` 當萬用入口
- 右上可放道具、或技能欄沒做就手填 skillId
- 解毒／料理／BUFF 藥寫進 heal／mana 五格
- 顯示傷害、全白天（未做）送進 C 包
- 把自動施放跟其他頁 flags 塞進同一個 128 magic
- 名單 `SendPacketData` 固定塞 20／40 個 `d`（n=1 也送 84 bytes）
- `my_send`／`HookProc` 在 `SendPacketData` 尚未返回時再 Pump 下一包
- PacketBox 47 回推名單時再 `QueueSave`（開面板會連送三輪 75＋128）

---

## 7. 2026-09-12 事故（摘要）

吃肉／修武改走 128 當天：開面板連送三輪 75＋128，`n=1` 卻寫 20 個 `d`（84 bytes）打爆 native 組包緩衝；`SendPacketData`→`my_send`→Pump 重入。C 流錯位後被當成 **C_SMS (253)**，空包仍 `sendSMS`，角色收到自己的「盟信」，開信斷線並出現 `S_CharCreateStatus`／快捷鍵 `length` 溢位。

**不是**「128 這個號碼不能用」，也**不是**有人寄信。4-byte `0x5A` 可留。DLL：只帶 n 個 d、Pump 禁止重入、回推不存檔。Java：`C_SMS` 必須 type=1 且內文非空；`C_PlaySupport` 喝水 `==62`。

詳見 `docs/PSS_128名單_SendPacketData重入與假盟信_2026-09-12.md`。
