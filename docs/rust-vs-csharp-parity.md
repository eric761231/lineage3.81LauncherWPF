# Rust launcher (`L1J3.8Launcher(RUST)參考`) vs C# `LauncherWPF381` 功能對照表

本文件比對 Rust 版天堂3.8登入器（約 85 個原始檔，`L1J3.8Launcher(RUST)參考/`）與現有 C# 移植
（`LinProj/LinLauncher` + `LinProj/Encoder` + `LinProj/LauncherDll`）的功能落差，作為後續移植的路線圖。

兩邊技術路線不同：Rust 版是「外部行程 ReadProcessMemory/WriteProcessMemory + 特徵碼掃描直接改
byte，完全不注入 DLL」；C# 版是「CreateRemoteThread 注入原生 C++ DLL，DLL 內用 API hook/送收封包
hook 達成等價效果」。下表「C# 現況」欄標註的是**效果對照**，不代表實作方式相同。

## ⚠️ Phase 3：封包加密公式修復 + 變身跑步移植 + 受傷不後仰調查

使用者指定三項優先修復（依序 3→1→2）：

| 項目 | 狀態 | 說明 |
|---|---|---|
| 封包加密 RSA 公式 | **已修復** | 找到兩個獨立 bug：(A) RSA D/N 讀取時多做了一次跟 C# 端寫入方式對不上的 XOR 還原（`SERVER_LIST_RSA_XOR_D/N`），(B) `_xorByte` 直接截斷 `modpow` 結果成 1 byte，正確公式應為 `(plain % 255) + 1`（對齊 `src/rsa32.rs`/`src/packet_proxy.rs::auth_xor_from_cipher`，已用 Rust 測試向量核對修對）。兩個 bug 修好後移除了 `ShareInfo.encrypt=0`/`randenc=0` 的強制關閉，改回依 list.txt 各伺服器的 `Encrypt`/`RandKey` 旗標決定（對齊 Rust `server_list.rs` 的 `encrypt` 欄位語意）。`randenc`（`nextRand()` 逐 byte LCG）路徑 Rust 完全沒有對應機制，維持原樣但標記為未驗證 |
| 變身跑步（順跑） | **已移植** | Rust 完全沒被搬過（先前誤以為 `NakedLocomotionHook.cpp` 對應，實際上是位址、技術都不同且從未安裝過的死碼，現已刪除）。這次逐 byte 翻譯 `src/smooth_run_hook.rs` 的 per-entity 中段 hook（`0x00449776`，已用 dump 驗證原始 5 bytes 完全吻合），新增 `LauncherDll.cpp::SmoothRunPatch` 命名空間 |
| 受傷不後仰 | **未實作，僅調查** | Rust 完全沒有這個功能（搜尋整個 Rust 專案無任何相關字樣），C++ 原本的 `NakedFlinchHook.cpp`/`NakedBloodHook.cpp` 也是從未安裝過的死碼（現已刪除，邏輯可從 git 歷史找回）。稽核時把 `L1Offsets.h` 記載的 hook 位址逐一拿 dump 驗證，**幾乎全部確認錯誤或無法映射**（`HOOK_FLINCH_ACTIVATION_381`/`FUNC_SETUP_ACTION_381` 完全 unmapped，`HOOK_UPDATE_ANIMATION`/`HOOK_BLOOD_EFFECT_PUSH` 位址存在但 bytes 對不上預期指令），CObject/CPlayer 結構欄位偏移跟 Rust 已驗證的同類欄位（`entity+0x14~0x29`）差距很大、不是同一結構。詳細逐項結果見 `LinBin3.81/rust_verified_addresses_for_L1Offsets.md`，需要重新用 AOB 或 debugger 動態驗證才能繼續 |

## ⚠️ Phase 3 延伸稽核：時間保護（Themida 授權）機制重複

實機測試「連線一段時間後畫面當掉」時發現：`LauncherDll.cpp` 對 Themida/版本檢查**同時疊了兩套機制**，
而 Rust（`patch.rs::apply_time_guard_patches`）只用其中一套：

| 機制 | Rust 現況 | C# 舊現況 | 本次處理 |
|---|---|---|---|
| Byte patch（`0x004E204E` JNZ→NOP+JMP + `0x00722761`） | 唯一機制，等 packer 解密完成後一次性原子寫入，完全不碰系統時鐘 | 其實已經對齊做了（`PatchThread` 裝 `0x004E204E`、`my_CreateWindowEx` 裝 `0x00722761`），先前 Phase 1 判定「已有等價實作」沒有問題 | 維持不動 |
| 系統時鐘偽造（`SetFakeSystemTime`/`RestoreSystemTime` + `timeController.cpp` 5 個 API hook） | **完全沒有**（全專案搜尋 `SetLocalTime`/`SetSystemTime`/`SYSTEMTIME` 0 命中） | 額外疊加：`SetFakeSystemTime()` 呼叫真正的 `SetLocalTime()` 把**整台機器**的系統時鐘改成 2013/8/1，`RestoreSystemTime()` 負責改回來；`GetTickCount`/`timeGetTime` 被 hook 成永遠回傳固定常數 `3600000`，完全不遞增 | **已移除**：確認 byte patch 單獨就足夠繞過檢查（Rust 已驗證），系統時鐘偽造屬多餘機制，且有兩個實測風險——(1) 一旦 `RestoreSystemTime()` 沒執行到，玩家整台電腦時鐘會卡在 2013 年，大幅度時鐘回撥也是常見的防毒/EDR 可疑行為特徵；(2) `GetTickCount`/`timeGetTime` 永遠不遞增，遊戲內部任何依賴「經過多少毫秒」的邏輯（逾時/心跳）都可能長時間運作後進入異常分支，是「過一陣子當機」的頭號懷疑對象。已刪除 `timeController.cpp`/`.h` 與所有呼叫端，待使用者實機驗證當機是否消失 |

## ⚠️ Phase 2 稽核重點

Phase 1 版本的「已有等價實作」表格只做了粗略比對（兩邊都有類似功能就判定 equivalent），使用者
指出這不可靠，要求逐項深入比對程式碼，因為 Rust 版是已測試驗證正確有效的基準。稽核後發現多項
「寫著 equivalent 但實際上沒有真正做到」的問題，其中兩項已確認是**當機/無效等級的真 bug 並修復**：

| 子系統 | Phase 1 原判定 | 稽核後實況 | 本次處理 |
|---|---|---|---|
| 裝備欄擴充 | 已有等價實作 | 拿正式執行期 dump 逐 byte 比對，原本 `L1Offsets::PatchTargets` 寫死的 4 個位址內容跟預期的 `CMP reg,0x0E` 完全對不上（是不相關的 LEA/PUSH 指令）——不只沒效果，還會把不相關指令改壞，有當機風險 | **已修復**：改用特徵碼（AOB）掃描動態定位（`LauncherDll.cpp::EquipUiPatch` 命名空間），逐項移植 Rust `equip_ui.rs` 的 Patch A（codecave 動態映射表）+ Patch B（SetupSlots 雙 hook）+ Patch D（Surf bounds check），支援 14→31（不只 18），全部位址都已用 dump 驗證命中 |
| 伺服器清單加解密 | 已有等價實作 | `ServerListEntryNative.BdFile` 誤寫成 260 字元（應為 32），導致 struct 從 213 bytes 膨脹成 669 bytes；`ServerService.cs` 又多了一個錯誤的「長度必須是16倍數」檢查（213 bytes 本來就不是16倍數）。這兩個 bug 讓任何 Rust 工具/真實 server 產生的 `list.txt` 全部被靜默丟棄；因 Encoder 端用同一個錯誤 struct 且會 padding，C#↔C# 自產自銷才「看起來正常」 | **已修復**：struct 改回 213 bytes（欄位順序/大小對齊 Rust `Server_Info`），移除錯誤的 %16 檢查與 Encoder 端多餘的 padding。已驗證加密→解密往返正確，密文長度維持 213 bytes 不再跟 Rust/真實 server 格式對不起來 |
| Encoder「產生登入器」 | （未列入比對，屬獨立 bug） | `EncoderService.CreateLauncher` 只是複製範本檔案，完全忽略傳入的設定；且 `LinEncoder.Models.LauncherConfig` 本身是另一份不相容的舊定義（Sign 用 "PROXYCFG"、欄位順序也不同），跟 `LinLauncher.Proxy` 實際讀取用的 `LinLauncher.Models.LauncherConfig`（Sign=`0x12345678FEDCBAFF`）對不上，就算接上寫入邏輯也會因為格式不對讀不到 | **已修復**：`LinEncoder.Models.LauncherConfig` 改為跟 `LinLauncher.Models.LauncherConfig` 逐欄位位元組對齊，新增 `LinEncoder.Services.ConfigDatWriter`（比照 `ConfigDatGen` 已驗證過的寫入流程），`CreateLauncher` 改成範本 + 附加加密 config 區塊寫入輸出檔。已驗證「Encoder產生→Proxy從exe尾端抽取→ConfigDatCodec解密」全流程往返一致 |
| CRT watson 崩潰防護 | 已有等價實作（原標「未逐一確認」） | 全專案完全沒有 `_set_invalid_parameter_handler`／`SetUnhandledExceptionFilter`／`SetErrorMode`，而 DLL 大量使用 `sprintf_s`/`vsprintf_s` 系列，任何一次參數驗證失敗都會讓整個遊戲行程直接硬崩潰、無法攔截也沒有 log | **已修復**：`LauncherDll.cpp::init()` 一開始安裝 `_set_invalid_parameter_handler`（記錄 log 後直接返回，不 abort）+ `SetUnhandledExceptionFilter`（行程層級最後防線，記錄例外碼/位址後才往下走） |
| DPI/視窗設定 | 已有等價實作 | 確認完全沒有實作：`windowController.cpp` 只做了視窗拖曳（WM_NCHITTEST subclassing），全專案（含 C# WPF 端與 app.manifest）都沒有 DPI awareness 設定，也沒有 `lineage.cfg`／window_mode 相關程式碼 | **未處理**，已移到下方路線圖（工作量較大，屬新功能開發而非修 bug） |

## 使用者刻意保留的差異（不是 bug，不要再誤判成「沒做成功」）

以下這項稽核時發現跟 Rust 版做法不同，但**經使用者確認是先前刻意的決定**，維持不動：

| 子系統 | Rust 做法 | C# 現況 | 保留原因 |
|---|---|---|---|
| .pak/BD 檔案完整性 | `inject.rs`/`morph_auth.rs`：AES-128 + HMAC-SHA256 + zlib，防竄改 | `encdec.cpp`/`aes.cpp` 只用純 XOR，無 HMAC/簽章驗證 | commit 紀錄顯示這是先前刻意改的（"Fix: Implement pure XOR encryption for PAK and sync keys with DLL"），使用者確認尊重先前決定 |

（原本「帳密/封包加密維持明文」也列在這裡，Phase 3 已找到並修好根因的兩個公式 bug，不再是刻意保留的差異，詳見上方 Phase 3 章節。）

## 已有等價實作（稽核通過，維持原判定）

| 子系統 | Rust 來源 | C# 現況 |
|---|---|---|
| 時間保護繞過（RSA 效期） | `patch.rs::wait_and_patch`（byte patch DECRYPT_ADDR/PATCHCODE1） | `PatchThread`/`my_CreateWindowEx` 的 `0x004E204E`/`0x00722761` byte patch，跟 Rust 對齊。~~`timeController.cpp` API hook~~ 已確認是多餘的重複機制並移除，詳見上方「Phase 3 延伸稽核」 |
| .pak/.txt morph 載入（解密機制本身，不含完整性驗證，見上表） | `inject.rs`, `morph_auth.rs` | `LauncherDll.cpp::GetFileBuffer`（XOR 解密 + Virtual Packer） |
| launcher.ini 對應設定（不含 window_mode，見路線圖） | `config.rs` | `LauncherConfig`/`ConfigDatCodec` 系列 |

## 缺口 — Phase 1 本次補齊

以下皆已用 `LinBin3.81/verify_addresses.py` 對照執行期記憶體 dump 驗證通過（13/13 ✅，見
`LinBin3.81/address_verification_report.md`），確認位址/特徵碼在目前遊戲版本上有效，才納入本次移植：

| 功能 | Rust 來源 | 說明 |
|---|---|---|
| AC（反外掛）偵測繞過 | `patch.rs::patch_ac_check` | 兩組 AOB pattern，JZ→JMP 單 byte 修改 |
| img 圖檔讀取上限突破 | `patch.rs::patch_img_limit` | 3 層：資源範圍 push、Surf 陣列邊界 cmp、陣列分配 push |
| png 圖檔上限突破 | `patch.rs::patch_png_limit` | 3 處：array malloc / init loop / cleanup loop 的 0x61C 常數 |
| 背包顯示上限文字 | `patch.rs::patch_inventory_limit` | 格式字串 `"%d / 180"` → `"%d / <N>"` |
| 移動封包不加密 | `patch.rs::patch_move_packet_no_encrypt` | 兩處條件跳轉 patch |

新增 C# 檔案：`LinProj/LinLauncher/Services/GameMemoryService.cs`（對齊 `memory.rs`/`process.rs`：
ReadProcessMemory/WriteProcessMemory/VirtualProtectEx/AOB scan/suspend-resume threads）、
`GamePatchSpecs.cs`（上述 5 項的位址/特徵碼常數）。掛載點：`LaunchService.cs` 在遊戲行程建立、
`LauncherDll` 注入完成之後，新增一段等待遊戲初始化再套用這些 patch，套用前都會先讀值核對，
不符就跳過（fail-soft，比照 Rust 原本的作法）。

## 缺口 — 路線圖（本次未實作，供後續會話接續）

規模較大，需要獨立的多次會話才能完成，依建議順序列出：

### 1. AUX 基礎建設
- `AddressRegistry` 等價物 — 對齊 `src/aux/address.rs`（★信度標記制度、玩家/物品欄結構偏移）
- `AuxScheduler`/`AuxSettings` 等價物 — 對齊 `src/aux/runtime.rs`（背後排程器 + 可序列化設定）
- LHX 8 分頁 WPF 視窗殼（Home 鍵呼叫）— 對齊 `src/aux/lhx_window.rs`

### 2. 簡單 toggle 類（各自獨立，適合先做）
- 全白天 — `src/aux/toggle/all_day.rs`
- 海底抽水 — `src/aux/toggle/underwater_pump.rs`
- 怪物等級色彩 — `src/aux/monster_color_patch.rs`
- 顯示遊戲時鐘 — `src/aux/show_clock_patch.rs`
- 降低 CPU — `src/aux/low_cpu_hook.rs`
- 聊天框寬度 — `src/aux/chat_width.rs`

### 3. 自動化操作類
- 自動喝水 — `src/aux/drink_hook.rs`（shellcode 注入）
- 自動施法/輔助 — `src/aux/buff_dispatch.rs`, `spell_book.rs`, `spell_db.rs`, `class_remap.rs`
- 自動變身/解毒/磨刀石修武器 — `src/aux/player_state.rs`, `poison_hook.rs`
- 刪物/溶解道具 — `src/aux/use_item_spy.rs`, `inventory.rs`
- 喊話（多組輪詢）— `src/aux/chat.rs`
- 快鍵系統 — `src/aux/hotkey.rs`, `input_sim.rs`
- 定時道具/技能 — 對應 tab6（定時），複用 `AuxScheduler`
- 攻擊傷害顯示 — `src/aux/attack_damage_hook.rs`, `attack_damage_feet_hook.rs`
- 經驗值追蹤 — `src/aux/exp_tracker.rs`
- 角色 profile（per-character 設定）— `src/aux/profile.rs`

### 4. 撿物通知 + EXP 飄字疊層
- Packet hook（PACKETBOX opcode 250）— `src/aux/notification/packet_hook.rs`
- 道具 ID→名稱/圖示 dispatcher — `src/aux/notification/dispatcher.rs`
- ImageElement 繪圖 hook（0x42F450）— `src/aux/notification/image_draw_hook.rs`
- Queue + Overlay 疊層渲染 — `src/aux/notification/{queue,overlay,renderer,layout,sprite_pak,tbt}.rs`

### 5. 3.8 順跑 morph pipeline
- ~~中段 hook（0x449776，per-entity）— `src/smooth_run_hook.rs`~~ **Phase 3 已移植**（`LauncherDll.cpp::SmoothRunPatch`）
- Morph 檔案 parse/classify/extract/emit pipeline — `src/smooth_run/*.rs`（尚未搬，清理不相容 variant 行的優化層，不影響 hook 本身能不能動）

### 6. DPI / 視窗設定（Phase 2 稽核新發現的缺口，非本次修）
- DPI awareness — `src/dpi_override.rs`；C# 目前全專案（含 app.manifest）完全沒有 DPI 設定
- `lineage.cfg` window_mode（400x300/800x600/1200x900/1600x1200 四檔）— `src/lineage_cfg.rs`；
  C# `windowController.cpp` 目前只做視窗拖曳，沒有視窗大小模式邏輯

### 7. 其他
- 動態對話框 — `src/img_hover.rs`
- IME 注入 — `src/ime_inject.rs`
- 選單注入 — `src/aux/menu_inject.rs`
- 日誌視窗 — `src/aux/log_window.rs`
- HP/MP 32-bit 擴展（6 opcode 完整修補）— `src/hp_mp_patch.rs`（極複雜：codecave/trampoline/動態
  AOB 掃描 sprintf hook，C# 現況用不同方式處理 HP/MP 顯示，是否需要 1:1 搬移待評估）

## 驗證方式參考

`C:\python_training\LinBin3.81\verify_addresses.py` 可重複執行來驗證新位址（用法見檔頭註解），
需要一份執行期完整記憶體 dump（`.DMP`，`MiniDumpWithFullMemory` 格式）。此工具已修正
`lin_master.py` 既有的 minidump 解析 bug（模組名稱/記憶體 stream type 錯誤），細節見
`LinBin3.81/address_verification_report.md` 附註。
