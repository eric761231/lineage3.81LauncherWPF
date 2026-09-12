// PssOverlay.h: 遊玩輔助 layered 視窗（分頁：BUFF / 道具 / 返回）。
#pragma once

void PssOverlay_Show();

void PssOverlay_PumpPendingSave();

bool PssOverlay_HitTestSlot(int screenX, int screenY, int *outSection,
                            int *outIndex);

bool PssOverlay_IsPicking(int *outSection, int *outSlot);

void PssOverlay_OnResolveReply(bool success, int section, int slot,
                               int templateItemId, int gfxid, int count,
                               const wchar_t *name);

void PssOverlay_OnHpUpdate(int cur, int max);
void PssOverlay_OnMpUpdate(int cur, int max);
void PssOverlay_OnVitalsUpdate(int curHp, int maxHp, int curMp, int maxMp);

void PssOverlay_OnSlotCounts(int section, int slot, int count);
void PssOverlay_OnSlotCountsBatch(int n, const int *sections, const int *slots,
                                  const int *counts);

void PssOverlay_PumpPendingUiNotify();

void PssOverlay_OnItemFilterList(int listType, int n, const int *itemIds,
                                 const int *gfxids, const wchar_t names[][64]);
