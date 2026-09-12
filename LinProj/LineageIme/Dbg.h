// Dbg.h: 除錯日誌。DLL 在遊戲行程內，寫 <遊戲根>\Core\ime_debug.log。
#pragma once

void ImeDbgLog(const char *fmt, ...);

#define IME_LOG(...) ImeDbgLog(__VA_ARGS__)
