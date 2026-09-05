#pragma once

// 交易視窗 ctor：NOP 掉「item+0x10==0 則跳過 4AEC90」。
// 不碰個人商店、不碰 4AF070。DelayedDetour 後單獨安裝。
void InstallTradeStatusHook();
