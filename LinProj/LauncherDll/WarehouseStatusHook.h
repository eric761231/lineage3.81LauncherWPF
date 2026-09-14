#pragma once

// 倉庫清單 type 2/3/5/9/17/18：官方 dchcdcs 後沒有 status。
// 吃名稱後 blob、注入 4AF070；黑框改畫 item+0xA8；+ 前綴吃完整數字。
// 必須與伺服器「每筆名稱後 writeC(0) 或 blob」成對。DelayedDetour 後安裝。
void InstallWarehouseStatusHook();

// 僅倉庫注入／商店賣出列打開。背包與交易走官方 4AEC90，不要改列數。
extern "C" void FmtExtraNlSet(int on);
// 2026-09-14：驗證 g_fmtExtraNl 是否會在商店 Clone 路徑呼叫 SplitFmt 當下
// 意外殘留非 0（見 docs/PrivateShopStatus_開發須知.md 的待驗證線索），暫時
// 只給診斷 log 用，確認完可以拿掉。
extern "C" int FmtExtraNlGet();

extern "C" int ApplyListFmtOff(const char *src, int *off, int cap);
extern "C" int ApplyListFmtOffBag(const char *src, int *off, int cap);
