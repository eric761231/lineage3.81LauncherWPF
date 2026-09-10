// BroadcastToPledgeHook.h: 血盟「成員登入訊息」快捷 Action（Action_BrodcastToPledge）。
// 原廠 VA 0x62EC10；確保切換旗標後一定送出 C_BroadcastToPledge（opcode 75）。
#pragma once

// 在保護殼解密完成後呼叫（DelayedDetourThread）。
void InstallBroadcastToPledgeHook();
