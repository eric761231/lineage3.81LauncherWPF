# PSS：128 名單打亂加密、憑空盟信、開信斷線（2026-09-12）

> 狀態：**已修**（DLL + Java 須一起部署）  
> 相關：`docs/PSS_封包與頁面搬遷計畫.md` 階段 2、`PssConfig.cpp`／`PssOverlay.cpp`、`C_SecurityStatus.java`、`C_SMS.java`、`C_PlaySupport.java`

昨天（吃肉／修武還在 opcode 75）正常。今天才把溶解／刪除名單與吃肉／修武改走 **128 `C_SecurityStatus`**，同一天出現：自動吃肉、憑空收到信、打開信件斷線。

---

## 1. 表面症狀（容易誤判）

| 看起來像 | 實際 |
|---|---|
| 不能再用 `C_SecurityStatus` | 4-byte **128／magic 0x5A**（吃肉／修武 flags）沒問題 |
| `C_PlaySupport` 長度閘把開信封包吃掉 | 信件是 **C 87**，喝水是 **C 75**，長度閘看不到開信 |
| 有人寄信／系統發信 | 服務端把錯位 C 包當成 **C_SMS (253)**，寫了一封血盟「盟信」給自己 |

吃肉過程的 S 包是正常的：`S_ItemStatus` 數量遞減、`S_Weight`、`$23`、`S_PacketBox 0x0A/0x0B`、`S_GameTime`。面板開著時 `S_AutoPotionVitals`（`fa 27`）每 500ms 一包也是設計如此。

---

## 2. 真正原因（兩件事疊在一起）

### 2.1 native `SendPacketData`（`0x580E50`）被撐壞

名單曾固定 format `"cccc" + 20 個 d`：

- `n=0`：走 4-byte `cccc`（安全）
- `n=1`：仍寫 **84 bytes**（4 + 20×4），多出來的 dword 是 0

喝水 62-byte 包昨天就在用、緩衝夠。84-byte 名單今天才出現，native 組包緩衝不夠會弄髒**下一包**的長度／Blowfish 狀態。之後所有 C opcode 都可能是錯的。

### 2.2 開面板連送三輪，而且組包會重入

開面板同一條遊戲執行緒、約 200ms 內：

1. `PumpPendingUiNotify`：75（62）＋128 flags（4）＋名單 type0 `n=1`＋type1 `n=0`＋`ui visible=1`＋`0x59` 請回推
2. PacketBox 47 回推 → `WM_PSS_ITEM_FILTER` → **再 `QueueSave`**
3. `PumpPendingSave` 再送一整輪 75＋128

更糟：`SendPacketData` 最後會走到 hook 過的 `send` → `my_send` → 再 `PumpPendingSave`／`PumpPendingUiNotify`。**組包還沒結束又送下一包**，native 緩衝上疊包。

DLL log 特徵（同一 TID 連打三輪）：

```
send potion: done (62-byte)
send misc flags=0x03 (128/0x5A 4-byte)
send item-filter type=0 n=1
send item-filter type=1 n=0
ui visible=1
save: wrote ...
send potion: ...
save: wrote ...          ← 回推又存檔／重入
```

---

## 3. 「沒人寄信」為什麼會跳出信件 UI

錯位後某一包第一個 byte 變成 **253 `C_OPCODE_SMS`**。舊 `C_SMS.java`：**不管 type、不管有沒有內文**，只要角色在血盟裡就 `L1Clan.sendSMS` → `sendLatter` → `writeSMSMail` 主題「盟信」。

服務端 log 對得上 `sendLatter` 固定四包，不是 `C_Mail` 開信箱：

```
S_Mail  ba 50 ...          subtype 0x50 新信通知（寄件備份格式）
S_Mail  ba 01 + 同一 mailId  血盟信件內容（TYPE_CLAN_MAIL=1）
S_Mail  ba 01 01 00 ...     血盟信箱列表（剛好 1 封）
S_SkillSound  動畫 1091    「收到信」音效
```

玩家打開這封假盟信，客戶端信件 UI 再送 C 包，stream 已經錯，後面才出現：

- 遊戲中收到 **`S_CharCreateStatus` `62 15 ...`（REASON_WRONG_AMOUNT）** → 伺服器把某包當成 **C 84 創角**
- `C_CharcterConfig`：`readD()-3` 當 blob 長度寫 DB → `length` 欄位溢位
- 未處理 opcode（例如 211）
- 角色切換後 `_activeChar==null` 仍 `handlePacket` → `C_DeleteInventoryItem` NPE

`type:139` 是 **C 254 `C_SendLocation`** 對不明 type 的 log，與 PSS magic（`0x56`–`0x5B`）無關。

---

## 4. 已做的修正（前後端一起換）

**LauncherDll**

- `SendItemFilterChunk`：只組 **n 個 `d`**（`n=1` → 8 bytes；`n=0` 維持 4）。超過 20 筆才第二包 magic **0x5B** 追加，不要一次 40 個 d。
- `g_inPump`：`PumpPendingSave`／`PumpPendingUiNotify` 合併為一次 Pump；`SendPacketData`→`my_send` 重入直接 return。
- PacketBox 47 回推只更新畫面，**不要再 `QueueSave`**。

**Java（Eclipse `SVN_Lineage381C`，須重編）**

- `C_PlaySupport`：喝水 **`decrypt.length == 62`**（註解早就這樣寫，程式曾誤用 `>=`）。
- `C_SMS`：長度 ≥ 4、**type==1**、內文非空才 `sendSMS`。
- `C_CreateChar`：已在遊戲中直接 return，不要回創角結果。
- `C_CharcterConfig`：長度不合法不寫 DB。
- `C_DeleteInventoryItem`：`pc==null` return。

---

## 5. 以後加 PSS C 包時不要再犯

1. `SendPacketData` 的 format **有幾個欄位就寫幾個**；不要「預留 20 個 d、沒用的填 0」。
2. 任何會呼叫 `SendPacketData` 的 Pump，都要假設它會進 `my_send`；**組包中禁止再 Pump**。
3. 伺服器回推（PacketBox 47 等）不要再觸發「存檔＋重送 C 包」，否則開面板必連打。
4. 借位 opcode 的 Java handler：**短包／空字串／不明 magic 必須 return**，不可當正式功能執行（`C_SMS` 空包寄盟信就是例子）。
5. 128 的 4-byte `0x5A` 可繼續用；若再觀察到錯位，先查 format 長度與重入，不要先換 opcode。
6. opcode／magic／長度有改，**DLL 與 Java 必須同一天部署**。
