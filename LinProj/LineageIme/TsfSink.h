// TsfSink.h: TSF ITfUIElementSink — 直接從 TSF 訂閱候選 UI 事件，跳過 IMM32。
// 對照 Rust 版 ime_overlay/src/tsf_sink.rs。
//
// 為什麼需要這個：Win11 新注音/新酷音等 TSF text service，候選資料只走 TSF
// callback 路徑，不寫進 IMM32 候選 cache（LUnicodeEdit 自己刻的 EDIT 沒有
// MSCTFIME 橋接，ImmGetCandidateListW 永遠回 size=0）。
#pragma once
#include "Common.h"

// 最近一次 LUnicodeEdit 拿到 focus 的 hwnd —— overlay 定位用。
void TsfSetLastFocusEdit(HWND h);

// 在 worker thread 呼叫一次：CoInitializeEx STA → CoCreateInstance ThreadMgr →
// Activate → QI ITfSource/ITfUIElementMgr → AdviseSink(ITfUIElementSink)。
// 必須在遊戲 UI thread 上呼叫（TSF 是 per-thread）。
HRESULT TsfInit();
