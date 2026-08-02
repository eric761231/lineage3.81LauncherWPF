#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "resource.h"
#include "ShareMemory.h"
#include "aes.h"
#include "packet.h"
#include "encdec.h"
#include "zlib.h"
#include "configenc.h"
#include "HelperDlg.h"
#include "MimirPowerDlg.h"
#include "detours.h"

#include "VMProtectSDK.h"
// Removed redundant VMProtect dummy macros as they are now in VMProtectSDK.h

#pragma comment(lib, "detours.lib")
#pragma comment(lib, "zlib.lib")

extern HINSTANCE hins;

// 初始化 DLL 內部狀態：讀共享記憶體、載入資源、註冊 Detours Hook。
void init();

// 送出自訂（非原生協定）C 封包：len(2,含標頭)+opcode(1)+payload，
// 套用跟其他封包一樣的加密（走 my_send，而非直接呼叫 real_send)。
// 目前僅支援 ShareInfo.randenc==false（靜態 XOR）的情況，randenc 模式尚無對應
// 接收端解密狀態，不支援。
void SendCustomPacket(BYTE opcode, const BYTE *payload, int payloadLen);