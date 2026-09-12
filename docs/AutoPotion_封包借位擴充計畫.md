# 輔助功能封包借位擴充計畫（2026-09-08／接手更新 2026-09-08：ClientExecutor／記憶體對照）

## 背景

自動喝水（治療/補魔）已經用「借位 opcode 75（`C_BroadcastToPledge`）」的方式做完，細節見
`docs/AutoPotionOverlay_現況與圖示交接.md`。技術是：opcode 75 原生只有 0/1 byte 的簡單旗標
（血盟登入通知開關），我們的客戶端故意送一個更長、原生格式不可能產生的封包，伺服器端用
`decrypt.length` 分支把「原生短包」跟「我們的自訂資料」分開處理。

現在要規劃後續一大票輔助功能（技能施放、生存吶喊、傳送道具、自動刪除/溶解道具、自動解毒、
自動喊話、自動製作卷軸、黑魔石精煉……），這份文件回答三個問題：

1. 新功能會不會逼著改現有封包格式？
2. 全部塞進同一個借位 opcode 用「代號」分流，技術上行不行？該不該這樣做？
3. 如果要分散到多個借位 opcode，哪些候選 opcode 實際上安全可用？

**狀態（2026-09-08 接手）**：規劃文件持續更新；真正改 Java／C++ 仍等功能排進開發排程。
操作者定調：本表候選**全是冷門功能，且本服可關閉不用**——借位以「關原生／長度＋subCmd 分流」為準（見 §4.1）。

## 1. 現有 schema 的擴充彈性

`AutoPotionSlot`（客戶端 `AutoPotionConfig.h`）：

```cpp
struct AutoPotionSlot {
  int kind = AutoPotionSlot_None; // 0=空 1=道具 2=法術（目前只定義到這）
  int id = 0;
  int gfxid = 0;
  int thresholdPercent = 0;
};
```

`kind` 目前是 1 byte（封包裡是 `c`），數值範圍 0~255，只是「目前只用到 0/1/2」，不是「封包只
容得下 0/1/2」。這代表：

- **加「生存吶喊」這類特殊觸發手段**：定義新的 `kind` 常數（例如 `AutoPotionSlot_SurvivalShout
  = 3`），伺服器 `PotionTimer.tryUseSection` 判斷 kind 時多一個 case（呼叫吶喊而不是
  `L1PlaySupportSystem.usePotion`）。**封包大小完全不變**（還是 5 槽 ×(threshold+kind+id)），
  純粹是「這格資料怎麼被使用」的邏輯分支，不是資料格式問題。
- **加「傳送道具」**：傳送卷軸本質上就是道具（`kind=Item`），如果放進現有的治療/補魔分類、只是
  擴充 `isAllowedClassname` 的白名單，**完全不用動封包**。
- **只有一種情況真的要動封包大小**：你想幫某個新功能開一個「跟治療/補魔平行、自己有一組 5 槽
  ＋各自百分比門檻」的全新分類（section）——例如「低血量自動使用生存吶喊」如果要跟治療分開算、
  有自己獨立的 5 槽，那才需要在 62-byte 封包後面再接一段 30 bytes（5×6）新分類。這是「加一個
  分類」的量級，不是每加一個新 kind 都要動。

**結論：技能/生存吶喊這類「觸發手段」的擴充，通常只要加 kind 常數＋伺服器分支，不用動封包；
只有真的要開全新獨立分類才要加長封包。**

## 2. 「一個借位 opcode + 代號分流」可行嗎？該不該做？

### 技術可行性：可行

目前的技巧是「長度分支」——`decrypt.length` 用來判斷這包是原生短包還是我們的設定包。長度分支
有個先天限制：**新增的每一種自訂封包，長度都必須跟所有其他分支（含原生、含我們自己其他功能）
不一樣**，隨著功能越加越多，保證長度不撞的難度會一直上升，而且完全看不出「這包裡面到底裝什麼」
除非回去翻文件。

更穩健的做法是「代號 byte」：opcode 後面固定第一個 byte 當作 sub-command，伺服器照這個代號
`switch` 到不同的解析函式，**不再靠長度猜是哪個功能**，長度只在各自的 handler 裡面拿來做基本
驗證（例如「代號 3 的封包預期長度是 X，長度不符就丟棄」），不再是分流的依據。

```
opcode(1) + subCmd(1) + <該 subCmd 自己定義的 payload>
```

新增功能只要挑一個沒用過的 `subCmd` 數值，完全不用檢查跟其他功能的長度會不會撞——這是這個方案
唯一但很關鍵的優點。

### 該不該把所有功能都塞進同一個 opcode：不建議

技術上代號分流能撐住任意數量的子功能，但「該不該」是另一回事。治療/補魔/法術/解毒這幾個功能
概念上都屬於「即時維持角色存活/戰鬥力」，資料形狀也類似（都是門檻+觸發手段），適合共用一個
opcode、用代號分流，甚至可以共用 `PotionTimer` 那種 tick 迴圈。

但「自動刪除/溶解道具」「自動喊話」「自動製作卷軸/黑魔石」這幾個，資料形狀完全不像門檻/道具
配對——刪物是一份 id 黑名單、喊話是一份輪播字串清單、製作卷軸/黑魔石可能牽涉到消耗道具數量/
配方表。硬要塞進同一個 Java class 解析，會讓這個檔案變成「什麼功能都要改它」的瓶頸：

- 多個功能共用一個檔案，日後多個 AI/多人協作時容易互相衝突、互相看不懂彼此的 sub-command。
- 一個功能的 bug（例如某個 subCmd 解析錯誤丟例外）如果沒包好，可能連累同檔案裡其他功能。
- 除錯時，log 都混在同一個「[AutoPotion]」前綴底下，難以快速定位是哪個功能出包。

**建議的分群方式**：

| 借位包 | 涵蓋功能 | 理由 |
|---|---|---|
| `C_PlaySupport`（原 `C_BroadcastToPledge` 改名，opcode 75） | 治療、補魔、法術施放、生存吶喊、（可選）自動解毒 | 都是「即時門檻觸發」，資料形狀相近，適合共用 tick 迴圈 |
| 新借位 opcode #2 | 自動刪除道具／溶解道具 | 黑名單/白名單清單，跟門檻觸發無關 |
| 新借位 opcode #3 | 自動喊話 | 字串輪播清單，跟前兩者都不像 |
| 新借位 opcode #4（或更多） | 自動製作卷軸／黑魔石精煉 | 配方/消耗品邏輯，UI 操作模式也跟其他分頁不同（比較像「一次性批次工作」而不是「持續監控」） |

實際要開幾個新 opcode、怎麼分組，看你規劃的分頁數量而定；上面只是建議的分群邏輯，不是強制切法。

## 3. C_BroadcastToPledge → C_PlaySupport 改名

已經查過實際引用範圍，改名影響很小：

**真的要改的地方**：
- 檔名＋類別名：`C_BroadcastToPledge.java` → `C_PlaySupport.java`，class 名稱一起改（Java 要求
  public class 名稱跟檔名一致）。
- `com/lineage/echo/PacketHandler.java` 第 58 行：`new C_BroadcastToPledge()` → `new
  C_PlaySupport()`。
- `com/lineage/server/model/timer/pssTimer/PotionTimer.java`：`import
  com.lineage.server.clientpackets.C_BroadcastToPledge;` 以及所有 `C_BroadcastToPledge.AUTO_
  POTION_SLOTS`／`SLOT_ITEM`／`SLOT_NONE`／`SLOT_SKILL` 的引用點，改成 `C_PlaySupport.xxx`。

**不用改、純 cosmetic（可做可不做）**：
- `C_PledgeMemberNotify.java`、`S_AutoPotionResolveItem.java` 裡各有一處 javadoc 註解提到
  `C_BroadcastToPledge`，只是文字說明，不影響編譯/執行，改名時順手更新即可。

**完全不用動**：
- opcode 數字（75）不變，客戶端 C++ 端的 `kOpcodeBroadcastToPledge` 常數只是本地名字，數值不變
  就不影響通訊，改不改看你想不想順便把 C++ 端命名也對齊。
- 原生「血盟登入通知開關」（`Action_BrodcastToPledge`）功能完全不受影響——改名只動 Java 類別
  名稱，客戶端原生邏輯完全不知道伺服器這邊叫什麼名字。

## 4. 候選借位 opcode 實查結果

以下候選由操作者挑出，全部在 `server/clientpackets/` 存在；欄位格式依原始碼（`OpcodesClient.java`
常數已對過）。

### 4.1 操作者定調（借位前提）

**整份候選清單都是冷門功能，而且本服可以關閉、不給玩家走原生路徑。**

含義：

- 不必為了借位去拼齊客戶端原生 UI／規格（多份資料也拼不齊屬正常）。
- 借位時原生分支可直接 `return`／關掉 NPC／選單入口；碰撞風險用「關功能」消掉，不是靠「玩家不會按」。
- 同行多數也因沒人用而關閉同類功能——與本服策略一致。
- 技術上仍建議：**自訂包用明顯非原生長度＋首 payload byte 當 subCmd**，避免偶發原生短包誤進自訂 handler。

因此風險排序改看「封包形狀好不好借」，不再看「打城／學技會不會撞」。

| Class | Opcode | 原生 payload | 長度 | 伺服器現況 | 借位適性（關原生後） |
|---|---|---|---|---|---|
| `C_SecurityStatus` | 128 `CASTLESECURITY` | `readD()` | 固定 5 | 城堡治安查詢（非防外掛） | ⭐首選：固定短包，好分流 |
| `C_SecurityStatusSet` | 240 `SETCASTLESECURITY` | `readD+readC+readD` | 固定 10 | 設城堡治安 | ⭐同上 |
| `C_PutHireSolderOK` | 65 `PUTHIRESOLDIER` | `D+H+H+D+H` | 固定 15 | 僱兵配置（註解「暫時」） | ⭐同上 |
| `C_SkillBuyItem` | 245 `SKILLBUYITEM` | `readD()` | 固定 5 | 技能道具購買 | ⭐固定短包；關商店即可借 |
| `C_PledgeMemberNotify` | 99 `CLIENT_READY` | 客戶端 `cd`（見 §4.4）；伺服器原本不解析 | 原生 5 | 進世界 handshake 殼 | ⭐可升：從 `ClientExecutor` 登入白名單／直接 `handlePacket` **移除**後，與 128 同級 |
| `C_PutBowSolderOK` | 102 `PUTBOWSOLDIER` | `D+H+N×(H+H)` | **不固定** | 城牆弓箭手（「未完成」） | ⭐客戶端 **沒有** `SendPacketData` 送點（dump 實查）；借位幾乎無原生碰撞，仍建議 subCmd |
| `C_SkillBuyItemOK` | 191 `SKILLBUYOKITEM` | `H+N×D` | 不固定 | 技能購買確認 | dump 內 **無** 固定 format 送點；可借，一律 subCmd |
| `C_SkillBuyOK` | 39 `SKILLBUYOK` | `H+N×D` | 不固定 | 金幣學技確認 | 同上（法術窗 `0x73xxxx` 只看到 opcode 164／6，沒有 39） |
| `C_SMS` | 253 | 客戶端 `ccss`；Java 只 `readC+readS` | 不固定 | 血盟簡訊 | 可借若關閉簡訊 UI；長度不固定仍要 subCmd |

建議佔用順序（對齊 §2 獨立管理型 #2／#3／#4…）：

| 順序 | Opcode | Class | 建議 |
|---|---|---|---|
| 1 | 128 | `C_SecurityStatus` | 刪物／溶解等 |
| 2 | 240 | `C_SecurityStatusSet` | 自動喊話等 |
| 3 | 65 | `C_PutHireSolderOK` | 製作／黑魔石等 |
| 4 | 245 | `C_SkillBuyItem` | 下一個固定短包槽 |
| 5 | 99 | `C_PledgeMemberNotify` | **先從 `ClientExecutor` 拿掉**再借；原生只剩進世界 `cd` |
| 6+ | 102／253／191／39 | 其餘 | 102 無原生送點；253 有簡訊窗；191／39 無找到送點。一律 subCmd |

已佔用的 opcode 75（`C_BroadcastToPledge`）繼續專管門檻觸發群，不跟上面搶。

### 4.2 `CLIENT_READY`（99）：從 `ClientExecutor` 移除

操作者定調：**`C_PledgeMemberNotify` 可以從 `ClientExecutor` 拿掉**（登入階段允許 opcode 清單，以及少數直接 `handlePacket` 的 case）。

客戶端記憶體對得上這件事：唯一送點是進世界流程 `0x53CEB2` → `0x6B3FD0`，送 `cd`（opcode 99 + `this+4`，ctor 裡預設 0）。這不是血盟成員 UI，只是 handshake 殼。伺服器若不在登入執行緒特別放行／直接處理 99，這包會跟其他 opcode 一樣等進遊戲後才進 `PacketHandler`。

實作（Java，本 repo 無此檔，改 `SVN_Lineage381C`）：

1. `ClientExecutor`：刪掉 opcode **99** 的登入白名單與直接 `handlePacket`。
2. `PacketHandler` 仍對 99 指到 `C_PledgeMemberNotify`（之後在 `start()` 做長度／subCmd 借位）。
3. 原生 `cd`（decrypt.length==5）直接 `return`；自訂包用更長＋subCmd。

拿掉之後，99 不再比 128／240 更「登入敏感」，可當下一個固定短包槽。

### 4.3 上線前輕量驗證（每個即將佔用的 opcode 做一次即可）

1. `PacketHandler` 對應 case 臨時 log：`length` + 前 16 bytes hex。
2. 測機正常遊玩（原生入口已關的前提下）確認幾乎無包，或僅有可辨識的原生短包。
3. 定 payload 文件後再實作；原生分支直接關閉或保留短包 no-op。

### 4.4 客戶端記憶體對照（0827-1 dump／`SendPacketData` @ `0x580E50`）

驗證檔：`LinBin3.81/dumps/game/game_TW13081901_20260827_01.dmp`。掃描：全部 733 處 `call 0x580E50`，以「format 字串前一個 push = opcode」對上表。輔助功能不要去還原完整原生 UI；下列是**關入口／借 opcode 時要認得的原生資料**。

共用：

| 意義 | VA／偏移 |
|---|---|
| 送包 | `0x580E50` `SendPacketData` |
| HTML／NPC 指令分派 | `0x4116A0`（`ebp+8`＝NPC objId，`ebp+0xC`＝指令字串；`0x79A138` 比對） |
| 玩家物件 | `[0x9A8EB8]` |
| 城堡／據點 id | 玩家 `+0x4C` |
| 略過送包旗標 | `[0xC31544]`（非 0 則 `security`／`enableclear`／`disableclear`／`asktime` 不送） |
| NPC 查表 | `0x5ADD70(objId)` → 物件 `+0x18` word；**`0x342`**＝道具學技 NPC |

#### `C_SecurityStatus`（128）

| 項目 | 內容 |
|---|---|
| 指令字串 | `"security"` @ `0x8CB110` |
| 送點 | `0x411CBC` format `"cd"` @ `0x8CB11C` |
| payload | `castleId = [0x9A8EB8]+0x4C` |
| 對 Java | `readD()` 就是這顆城堡 id |

#### `C_SecurityStatusSet`（240）

同一函式 `0x4116A0`、同一座城堡 id：

| 指令 | 送點 | format | 額外欄位（在 castleId 之後） |
|---|---|---|---|
| `"enableclear"` @ `0x8CB120` | `0x411D06` | `"cdcd"` | `c=1, d=1` |
| `"disableclear"` @ `0x8CB134` | `0x411D50` | `"cdcd"` | `c=0, d=1` |

對 Java `readD+readC+readD`。關城堡治安 HTML 即可借。

#### `C_SkillBuyItem`（245）

| 項目 | 內容 |
|---|---|
| 指令字串 | `"xchgspell"` @ `0x8CB0E0` |
| 條件 | `0x5ADD70` 成功且 NPC `+0x18 == 0x342` |
| 送點 | `0x411BB3` format `"cd"`，payload＝NPC objId（`ebp+8`） |
| 對 Java | `readD()`＝NPC objId |

旁支（**不要跟 245 搞混**）：`"buyspell"` @ `0x8CB0D0` 在 **不是** `0x342` 時送 opcode **145**（`0x91`）`"cd"`——金幣學技清單，不是借位清單裡的 39。

#### `C_PutHireSolderOK`（65）

僱兵確認鈕，視窗物件 `this+8`：

| 偏移 | 用途（送 65 時） |
|---|---|
| `+0x54` | 必須 `== 5` 才走 opcode 65 |
| `+0x4C` | `d`（城堡／NPC id，與玩家 `+0x4C` 同類） |
| 常數 `1` | 第一個 `h` |
| `+0x188` word | 第二個 `h` |
| `+0x184` | `d` |
| `+0x60` | 最後一個 `h` |

| 項目 | 內容 |
|---|---|
| format | `"cdhhdh"`（對 Java `D+H+H+D+H`） |
| 送點 | `0x4A2896`（函式 `0x4A2710`）；複本 `0x5F0E76`（`0x5F0CF0`，K HTML 路徑） |
| 其他 mode | `+0x54==1`→opcode `0x13` `"cdh"`；`2`→`0x2C` `"cdd"`；`3`→`0x38` `"cdd"`；`4`→`0x1F` `"cdhhhh"`（都不是 65） |

相關字串：`mercenarySelect`（那是 opcode **107** `"cdh"`，選僱兵，不是 OK）、`mercenaryArrage-k.html`／`mercenarySelect-k.html`、`MercenaryButton`。

#### `C_PledgeMemberNotify`（99）

| 項目 | 內容 |
|---|---|
| 封包物件 ctor | `0x6B3F50`（vtable `0x8E6F60`，`this+4` 初值 0） |
| 送點 | `0x6B3FD0` format `"cd"` @ `0x8E6F00`，payload＝`this+4` |
| 唯一呼叫 | `0x53CEB2`（進世界 handler；同段還讀 `0x402830()` 設定物件 `+0x293` 血盟登入通知旗標） |
| RTTI 雜訊 | `.?AVCS_CLIENT_READY@CommonPacket@@` 是另一套 CommonPacket 名字，不要當成這個送點 |

原生不是「不定長真空」：客戶端固定 **5 bytes**。伺服器若仍不 `readD`，那顆 dword 被丟掉。從 `ClientExecutor` 移除後，這條 handshake 不再走登入特例。

#### `C_SMS`（253）

| 項目 | 內容 |
|---|---|
| RTTI | `.?AUSMSWindow@@` @ `0x963B44`、`.?AUSMSPledgeWindow@@` @ `0x963B5C` |
| 一般簡訊 | `0x4D2710` format `"ccss"`：type **0**，字串來自 `this+0x10`、`this+0x14`（getter vtable `+0x5C`）；空字串則系統訊息 `0x1B2`、不送 |
| 血盟簡訊 | `0x4D2F10` `"ccss"`：type **1**，一個字串（`this+0x14`） |
| 對 Java | 只 `readC+readS` 時會吃 type，第二個 `s` 可能被忽略 |

關簡訊窗即可借；輔助喊話不要去填 `SMSWindow` 欄位，直接 `SendPacketData` 自訂長包。

#### `C_PutBowSolderOK`（102）／`C_SkillBuyOK`（39）／`C_SkillBuyItemOK`（191）

0827-1 dump 裡 **沒有**「format 前一個 push = 這些 opcode」的 `SendPacketData` 呼叫。`push 0x66` 出現處是字元 `'f'`（路徑組字），不是封包。法術欄 `0x730000–0x750000` 的送包只有 opcode **164**（`S_AddSkill` 對向）與 **6**。

這三顆與 Java「未完成／確認清單」註解一致：**客戶端原生路徑很可能沒接上**。借位時不必找齊僱兵／學技確認 UI；用 subCmd＋非原生長度即可。若之後要對齊伺服器 `H+N×D` 的舊 parser，再在自訂 handler 裡避開那段長度。

掃描腳本（可重跑）：`LinBin3.81/scratch/find_borrow_opcodes.py`、`find_borrow_opcodes2.py`。

## 5. Sub-command 封包格式草案（給 C_PlaySupport 用）

如果採用第 2 節建議的「相關功能共用 `C_PlaySupport` + 代號分流」，格式建議：

```
opcode(1B, 固定=75) + subCmd(1B) + <該 subCmd 自訂 payload>
```

已知/預留的 subCmd：

| subCmd | 功能 | 備註 |
|---|---|---|
| 0x01 | 自動喝水設定（治療/補魔/法術，62 bytes payload） | 目前的 `AUTO_POTION_PAYLOAD_LEN` 邏輯搬進來，變成 subCmd=1 的固定格式，不再靠長度判斷 |
| 0x02 | 點道具解析請求（7 bytes payload，即現有 `RESOLVE_ITEM_PAYLOAD_LEN`） | 同上，搬過來變成固定 subCmd |
| 0x03 | （預留）生存吶喊/特殊技能設定 | 待實作時定義 payload |
| 0x04 | （預留）自動解毒設定 | 待實作時定義 payload；如果解毒判斷邏輯跟治療/補魔共用同一個 tick，適合放這裡 |
| 0xF0~0xFE | （預留）保留給未來擴充 | — |

**注意**：既有的原生「血盟登入通知開關」（0/1 byte 短包）跟這個新的 `subCmd` 機制**不衝突**——
因為原生封包長度是 1 或 4（不含 opcode 是 0/1 或 4 bytes），我們的 `subCmd` 家族一律要求
`decrypt.length >= 2`（至少有 opcode+subCmd 兩個 byte），伺服器端判斷順序維持「先看是不是原生
短包長度，不是的話再看第二個 byte 當 subCmd」，跟現有的 `C_BroadcastToPledge.start()` 分支邏輯
是同一套思路，只是把「62-byte 固定格式」升級成「有代號、可以無限擴充新格式」的家族。

**這次不做遷移**：現有 auto-potion 的 62-byte／7-byte 格式維持原樣（已經部署測試中，不建議在
未確定新功能排期前重構），上面的 subCmd 0x01/0x02 是「以後真的要加新功能時，一起把舊格式套進
新框架」的示範，不是這次立刻要做的事。

## 6. 給下一輪實作的檢查清單

真正要動手做某個新功能時，建議照這個順序：

1. 確認這個功能屬於「即時門檻觸發」群（走 `C_PlaySupport`／opcode 75 + subCmd）還是「獨立管理型」
   群（開新 borrow opcode）。
2. 若是獨立管理型，依 §4.1 優先序取下一個空 opcode（預設 128 → 240 → 65 → 245 → **99（先改 `ClientExecutor`）** → …），**先關該
   原生功能入口**（HTML 指令／僱兵窗／簡訊窗；99 則先從 `ClientExecutor` 拿掉），再做 §4.3 輕量 log 驗證後定案。記憶體欄位見 §4.4。
3. 定版該功能的 payload 格式，寫進這份文件（或另開一份功能專屬交接文件，比照
   `AutoPotionOverlay_現況與圖示交接.md`）。
4. 客戶端：新 overlay（沿用 `AutoPotionOverlay.cpp` layered + GDI+）+ `SendPacketData`。
5. 伺服器：在對應 `C_*` 的 `start()` 加長度／subCmd 分支（原生路徑直接關閉或 no-op），資料預設
   「只存記憶體、不落地 DB」（除非要跨連線持久化）。
6. **不要**為了借位去還原完整原生客戶端規格。

## 7. 接手備註／尚未排程

- 既有 opcode 75 的 62B／7B 格式**暫不遷移**到 subCmd（§5）；等第一個新門檻功能真正要上時再
  一起搬家。
- `C_BroadcastToPledge` → `C_PlaySupport` 改名可與第一次 subCmd 重構同 PR，單獨改名價值低。
- 下一個「獨立管理型」功能一確定名稱，在本文件 §4.1 表補上一欄「已佔用：功能名／payload 文件
  連結」，避免兩個功能搶同一個 opcode。
- `C_PledgeMemberNotify`：Java 端從 `ClientExecutor` 移除登入特例（§4.2）；客戶端送點與欄位已寫在 §4.4。
