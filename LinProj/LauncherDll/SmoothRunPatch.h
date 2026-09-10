// SmoothRunPatch.h: 變身跑步（順跑）@0x00449776。
//
// 變身表需 98=左、99=右。教學：EncoderForPartners/EncoderTool/順跑變身檔教學.md
//
// 規則：
//   - 怪物／NPC（+0x27==0）：有 98＋走路 → 切腳
//   - 人物（+0x27≠0）：必須同時具備一段＋二段＋三段，缺一不切
//       一段 getMoveSpeed()==1 → +0x24（S_SkillHaste）
//       二段 getBraveSpeed()!=0 → +0x29（S_SkillBrave；勿用 +0x20）
//       三段 STATUS_THIRD_SPEED → +0x12B!=0（type==8；+0x5A 是等級不是三段）
#pragma once

void InstallSmoothRunPatch();
