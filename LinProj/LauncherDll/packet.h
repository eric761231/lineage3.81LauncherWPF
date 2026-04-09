// packet.h — 封包格式定義
// 定義天堂 Client ↔ Server 間封包的基本結構與大小限制。
// 所有封包以 Packet_Head（2 bytes 長度欄位）開頭，後接 payload。
#pragma once

#define ENCRPT_PACKET_SIGN 0x44FF44FF           // 加密封包識別標記（Magic Number）
#define MAX_CLIENT_SIZE    2000                  // 最大同時連線數
#define MAX_PACKET_SIZE    20480                 // 單一封包最大位元組數（20 KB）
#define MAX_BUFFER_SIZE    (MAX_PACKET_SIZE * 2) // 接收緩衝區大小（預留組裝空間）

// 封包標頭（1-byte 對齊，直接對應網路位元組流）
#pragma pack(push, 1)
typedef struct {
	WORD len;   // 封包總長度（含標頭本身）
} Packet_Head;
#pragma pack(pop)