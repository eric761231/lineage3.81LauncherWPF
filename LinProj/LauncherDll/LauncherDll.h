// LauncherDll.h: LauncherDll 的標頭檔，宣告全域變數與匯出函式。
#pragma once

#include <stdio.h>
#include "resource.h"
#include "../LinLauncher/VMProtectSDK.h"
#include "../LinLauncher/ShareMemory.h"
#include "../LinLauncher/aes.h"
#include "../LinGate/packet.h"
#include "../LinGate/encdec.h"
#include "../LinLauncher/zlib.h"
#include "../LinLauncher/configenc.h"
#include "HelperDlg.h"
#include "detours.h"
#include <openssl/bn.h>

#pragma comment(lib, "detours.lib")
#pragma comment(lib, "../LinLauncher/zlib.lib")
#pragma comment(lib, "libeay32.lib")

extern HINSTANCE hins;

// 初始化 DLL 內部狀態：讀共享記憶體、載入資源、註冊 Detours Hook。
void init();