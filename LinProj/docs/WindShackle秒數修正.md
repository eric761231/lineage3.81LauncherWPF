# 風之枷鎖圖示秒數：設定幾秒就幾秒（不用 ×4）

實測有效（2026-09-16）。TW13081901 / PacketBox opcode 250、subtype 44。

## 摘要

原生是「伺服器先 `>>2`、客戶端再 `shl 2`（×4）」配對。圖示看起來像秒，實際只能對齊 4 的倍數，奇數秒被截掉。

兩邊一起改成**封包時間 byte = 圖示秒數**：

| 端 | 改什麼 | 結果 |
|----|--------|------|
| 伺服器 `S_WindShackle` | 拿掉 `time >> 2`，`writeH` 改 `writeC`，秒數夾在 0～255 | 封包多 1 byte 時間 |
| 客戶端 LauncherDll | 解殼後把 `0x0053D02B` 的 `shl ecx,2`（`C1 E1 02`）NOP 成 `90 90 90` | 不再 ×4 |

只換 Java：圖示仍 ×4。只換 DLL：圖示變 1/4。必須同一組部署。

客戶端解析字串是 `"dc"`（objectId dword + 時間 **1 byte**）。`writeH` 也能跑（多的高位 byte 這條 handler 不讀），但不能讓圖示超過 255 秒；要超過必須改 `"dc"` → `"dh"` 並改讀時間那段。

## 原生資料流

1. Java 呼叫 `new S_WindShackle(objId, 秒)`（技能／傳送／武器屬性等傳的是秒）。
2. 舊包：`buffTime = time >> 2` + `writeH`。例：17 → `04 00`。
3. 客戶端 PacketBox 44：`call 0x522110` 格式 `"dc"`，`movzx` 時間 byte。
4. `0x0053D02B`：`shl ecx, 2` 後 `push ecx`、`push 0x34`，`call 0x004EE400` 畫 icon。

例：設 16 秒 → 封包 4 → 圖示 16。設 17 秒 → 封包 4 → 圖示 16。

## 新封包

- `fa` = 250 PacketBox
- `2c` = 44 風之枷鎖
- `writeD(objectId)`
- `writeC(秒)`

設 17 秒應為長度 7：`fa 2c .. .. .. .. 11`。  
若仍長度 8 且結尾 `04 00`，執行中的仍是舊 class（`>>2` + `writeH`），不是 Eclipse `bin`。本專案 `ServerStart.bat` 跑的是 `Server_Game.jar`，`build\` 曾殘留 9/5 舊 class。

## 客戶端位址（解殼後／CE）

| VA | 用途 |
|----|------|
| `0053939A` | PacketBox 跳表 |
| `0053CFAE` | subtype 44 handler |
| `00522110` | 解析 `"dc"` |
| **`0053D02B`** | `shl ecx,2` → NOP；斷點看 **ECX = 圖示秒數** |
| `004EE400` | ApplyEffect／畫 icon |
| `005ADD70` | FindObject |
| `[0xC2D2B8]` | 自己 |

解殼前這段是亂碼。Memory View：`0053D02B` 應為 `90 90 90`。安裝成功 log：`[WindShackle] nop shl ecx,2 @ 0053D02B`；位元組不符會 `skip`（不要假設 ×4 已拿掉）。

CE：進世界後再下斷。`0053CFAE` → 單步到 `0053D02B`。設 17 秒時 ECX 應為 `0x11`。

## effectlist2.xml `duration` 與記憶體

風之枷鎖是 effect **id=52**（handler 寫死 `push 0x34`）。官方／磁碟上的 `effectlist2.xml_d.xml` 是 `duration="16"`（秒），不是 `>>2` 單位。你貼的 `255` 是後來改的；進遊戲後要以表為準。

倒數用的是封包秒數，不是這欄：

```
movzx ecx, time_byte     ; 封包
; shl ecx,2  已 NOP
push  0x34               ; effect id 52
call  0x4EE400           ; 第一參數＝ecx（秒）
```

`duration="0"` 的效果（加速等）本來就只吃封包。`duration="16"` 是資料表預設／上限類欄位（沉默也是 16、祝福武器 1800），單位已是秒，**不要再 ×4 改成 64**。

若技能要顯示超過 16 秒、且發現圖示被卡在 16，再把表列改成實際上限（封包 byte 最多 255）。改 XML 必須打進客戶端實際載入的 pak 並重開，否則記憶體仍是舊 16。

CE 對表（進世界後）：

1. 指標 `[00AC4CD8]` → 表基址  
2. id52 列：`基址 + 52×3C` = `基址 + C30`  
3. 列寬 `3C`；列裡找 `1623`（icon）旁邊的 dword：`10 00 00 00`＝16，`FF 00 00 00`＝255  
4. 斷 `004EE400`：第一參數應等於封包秒（17→`11`），第二參數 `34`。這才是倒數；表裡的 duration 只是同一列的資料。

## 程式位置

- `LinProj/LauncherDll/WindShackleTimePatch.cpp` / `.h`
- `LauncherDll.cpp`：`DelayedDetourThread` 在 `InstallGroundTrapIconHook()` 之後呼叫 `InstallWindShackleTimePatch()`
- Java：`SVN_Lineage381C/.../S_WindShackle.java`

注入的是 Core 的 `LauncherDll.dll`，不是只編 `LinProj\LauncherDll\Release`。
