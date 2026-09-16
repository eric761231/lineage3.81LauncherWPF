#pragma once

// PacketBox 44（風之枷鎖）原生會把時間 byte shl 2（×4）再送圖示。
// 解密後把 0x0053D02B 的 shl ecx,2 改 NOP，圖示秒數＝封包秒數。
void InstallWindShackleTimePatch();
