#pragma once

// 個人商店賣出列：blob 傳進 4AF070，黑框畫 item+0xA8。
// 掛收／自己開攤克隆（5956A0）把背包 +0xA8 拆列拷到克隆，黑框高跟列數走。
// 不碰交易視窗。DelayedDetour 後單獨安裝。
void InstallShopStatusHook();

// 從背包件拷已格式化列到目標（heap 新字串，列偏移跟背包走，不共用 +0xA8）。
// 成功回傳目標列數，略過回 0。不共用 +0xA8 指標。
extern "C" int __cdecl CopyItemFmtFromBag(void *dst, void *src);
