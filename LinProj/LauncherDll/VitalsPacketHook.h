// VitalsPacketHook.h: 攔截原生 S_HIT_POINT/S_MANA_POINT 封包解析，取得目前
// HP/MP 餵給 AutoPotionOverlay 畫紅/藍條。見 VitalsPacketHook.cpp 開頭說明。
#pragma once

void InstallVitalsPacketHook();
