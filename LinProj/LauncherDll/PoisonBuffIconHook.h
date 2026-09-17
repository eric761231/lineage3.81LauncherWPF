#pragma once

// PacketBox 子型 161（S_PoisonIcon）擴充：尾端多讀 H effectId。
// effectId＝effectlist2.xml 的 <effect id>，不是任意 PNG 編號（同 GroundTrapIcon）。
// 0＝維持原生固定圖：type1→391、type2→386、type6→381。
//
// 由 GroundTrapIconHook 的 0x544A20 分派一併攔截（避免對同一入口雙重 Detour）；
// 回傳 true＝已自行處理，呼叫端勿再轉原生。

// 若為 250+161 且 len>=8（含尾端 effectId）則處理並回傳 true；否則 false。
bool TryHandlePoisonBuffIconPacket(void *pkt, int len);
