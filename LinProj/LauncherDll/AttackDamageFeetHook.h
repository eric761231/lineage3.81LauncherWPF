#pragma once

// 傷害紅字氣泡改到怪物腳下（箭頭朝上）。對照 attack_damage_feet_hook.rs。
// 僅影響色碼 0xF800；需與 AttackDamageHook 一起用。
void InstallAttackDamageFeetHook();
