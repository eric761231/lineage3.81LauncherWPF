#pragma once

// 2026-09-16：暫時診斷工具，驗證 docs/hooks/SKILLHASTE_CUSTOM_ICON_PACKET_BRIEF.md
//（LinBin3.81 專案）想擴充的 S_SkillHaste（opcode 149）狀態圖示欄位到底存在
// target 物件的哪個位置。該計畫自己寫的兩條線索（0x4AEF30 內部用
// [0xABF4C8]、target+ICON_FIELD 未知）反組譯後對不上：0x4AEF30 完全沒有
// 引用 [0xABF4C8]，真正用到的地方在 0x4EE26F~0x4EE6A2 一帶（涵蓋 RUST 位址
// 表記載的 apply_buff@0x4EE400）。
//
// 這裡改成在原生呼叫 0x4AEF30（套用狀態、格式化顯示文字）前後，把 target
// 物件已知會被這個函式讀取的欄位（+0x9A／+0xA4／+0xB2／+0xB8／+0xA8）印出
// 來，讓玩家實際觸發加速/減速效果時能對照封包內容跟這些欄位的變化，藉此
// 找出真正的 icon 欄位在哪，或確認現有機制是不是走 state_id 查表而非直接
// 存 icon 數值。確認完是否有用，這段診斷跟 hook 都要拿掉，不要留在正式路徑。
void InstallSkillHasteHook();
