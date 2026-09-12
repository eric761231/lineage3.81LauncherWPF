#pragma once

// 個人商店賣出列：blob 傳進 4AF070，Tooltip 使用 item+0xA8 的格式說明。
// 掛收／自己開攤克隆（5956A0）會把背包格式資料複製到商店道具。
// 不碰交易視窗；完整停用原因與重新啟用前檢查項目見
// docs/PrivateShopStatus_開發須知.md。DelayedDetour 後才具備安裝條件。
void InstallPrivateShopStatusHook();

// 從背包道具拷貝已格式化說明到目標道具：建立新的 heap 字串並重建行偏移量。
// 成功回傳目標行數，任何輸入不合法或格式切割失敗都回傳 0。
// 此函式也被 WarehouseStatusHook 使用，不能因個人商店 Hook 停用而刪除。
extern "C" int __cdecl PrivateShopCopyItemFmtFromBag(void *dst, void *src);
