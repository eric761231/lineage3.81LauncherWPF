// Dbg.h: 除錯日誌 — 對照 Rust 版 ime_overlay/src/dbg.rs。
// DLL 在遊戲行程內，沒有 console/stdout，只能寫檔案。
#pragma once

void ImeDbgLog(const char *fmt, ...);

// 沿用跟 Rust 版一樣的呼叫慣例：IME_LOG("...%d...", x)
#define IME_LOG(...) ImeDbgLog(__VA_ARGS__)
