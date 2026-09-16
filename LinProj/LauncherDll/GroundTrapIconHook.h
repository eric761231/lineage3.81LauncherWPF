#pragma once

// 地面障礙專用狀態圖示（opcode 254，Java 端 S_GroundTrapIcon.java）。
// 功能跟原生 S_SkillHaste（opcode 255）一樣，差在固定多帶一個 icon(H) 欄位，
// 不碰 S_SkillHaste 既有的 60+ 個伺服器呼叫點。客戶端原生完全沒有這個
// opcode 的處理邏輯，全部由這支 hook 實作。
//
// 位址／行為抄自 S_SkillHaste handler（0x52C410）反組譯結果，見
// C:\python_training\LinBin3.81\docs\hooks\SKILLHASTE_CUSTOM_ICON_PACKET_BRIEF.md。
// Detour 分派入口 0x544A20：opcode==254 時自己處理並直接返回（不呼叫原生
// dispatch，因為原生對這個 opcode 沒有對應 case）；其他 opcode 一律照原樣
// 呼叫原生 dispatch，不影響任何既有封包。
void InstallGroundTrapIconHook();
