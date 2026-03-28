// targetver.h: 指定開發目標作業系統平台版本。
#pragma once

// 指定最低支援平台為 Windows 10 / Windows 11
// Windows 10 / 11 對應版本號: 0x0A00
// 參考: https://learn.microsoft.com/en-us/cpp/porting/modifying-winver-and-win32-winnt

#ifndef WINVER
#define WINVER 0x0A00           // 最低支援 Windows 10
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00     // 最低支援 Windows 10
#endif

#ifndef _WIN32_IE               // Internet Explorer 11 (Windows 10 內建)
#define _WIN32_IE 0x0B00
#endif
