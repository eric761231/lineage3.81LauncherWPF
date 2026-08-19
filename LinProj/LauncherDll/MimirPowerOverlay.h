// MimirPowerOverlay.h: 密米爾之泉自繪清單視窗（卡片列表＋選中詳情＋確認鈕）。
// 不用原生 HTML UI（互動效果太少），自己畫，素材/版面走 mimir_ui.pak/idx +
// mimir_ui.xml，改素材/版面不用重編 DLL。
#pragma once
#include <windows.h>
#include "MimirPowerHook.h" // MimirOption, MIMIR_OPTION_COUNT

// 顯示密米爾之泉選擇清單（固定 3 個選項）。
//   options: 固定 3 筆。
//   defaultDetail: 詳情卡預設（尚未點擊任何選項前）顯示的資料，視窗一開就直接顯示，
//                  不用等玩家點。玩家點了某一筆卡片後，詳情卡改顯示那筆 options[] 的資料。
//   countdownSeconds: 「選擇將於 hh:mm:ss 後更新」倒數的起始秒數，client 收到當下記錄
//                     本地時間，之後每秒用經過時間往下扣顯示，不會再自己另外猜時間。
void MimirPowerOverlay_Show(const MimirOption options[MIMIR_OPTION_COUNT],
                            const MimirOption &defaultDetail, DWORD countdownSeconds);
