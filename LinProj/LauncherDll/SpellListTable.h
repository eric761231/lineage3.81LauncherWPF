#pragma once
#include <windows.h>

// 從 ui.pak 的 CSpellList.xml 建 packedId → icon／名稱。遊戲不 parse 此檔。
void SpellList_EnsureLoaded();

// packedSkillId 對 XML id（與 CSpellList.xml 的 id、entry+0x04 相同，不是
// 伺服器 skill_id；skill_id = packed + 1）。找到回 true。
bool SpellList_Find(int packedId, int *outIcon, wchar_t *outName, int nameChars);

#ifdef __cplusplus
namespace Gdiplus {
class Bitmap;
}
Gdiplus::Bitmap *SpellList_GetIconBitmap(int packedId);
#endif
