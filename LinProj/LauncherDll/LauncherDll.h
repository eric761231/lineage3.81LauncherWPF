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
#include "detours.h"

#include "VMProtectSDK.h"
// Removed redundant VMProtect dummy macros as they are now in VMProtectSDK.h

#pragma comment(lib, "detours.lib")
#pragma comment(lib, "zlib.lib")

extern HINSTANCE hins;

// 初始化 DLL 內部狀態：讀共享記憶體、載入資源、註冊 Detours Hook。
void init();