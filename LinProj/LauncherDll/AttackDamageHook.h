#pragma once

// 攻擊傷害顯示：普攻／單體 + 範圍（本服 S_RangeSkill 的 H＝真傷）。
// 腳下氣泡另見 AttackDamageFeetHook。
// DelayedDetour 後 Install；Overlay「顯示傷害」→ SetEnabled（可持久化）。
void InstallAttackDamageHook();
void AttackDamageHook_SetEnabled(bool enabled);
bool AttackDamageHook_IsEnabled();
