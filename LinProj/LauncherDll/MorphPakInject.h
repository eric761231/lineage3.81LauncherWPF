#pragma once

// 變身 pak：Encoder 自訂格式注入，略過遊戲讀 TW13081901.pak 的加密。
bool MorphPak_Load();
void MorphPak_InstallHook();
