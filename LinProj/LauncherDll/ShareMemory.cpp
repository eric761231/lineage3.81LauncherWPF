#include "stdafx.h"
#include "ShareMemory.h"
#include <stdio.h>

static HANDLE hMapFile = NULL;
static SHARE_INFO* pBuf = NULL;
static const char* SHM_GUID = "{385FC524-96E3-4839-9909-1F2135D4F928}";

SHARE_INFO* get_shm(DWORD pid, bool create)
{
	char szName[128];
	// 名稱需與 C# LaunchService.cs 中的 SHM_GUID 保持同步，且都要帶 pid——
	// 雙開時如果沒有 pid，兩個遊戲行程會開到同一塊共用記憶體，C# 端幫第二個
	// 遊戲寫入帳密/連線資訊時可能剛好撞上第一個行程還沒讀完，讀到被覆蓋的
	// 資料（實測會導致當機）。pid 用目標遊戲行程自己的 PID（呼叫端已經傳
	// GetCurrentProcessId()），每個行程各自獨立一塊，不會互相碰撞。
	sprintf_s(szName, "Local\\%s-%lu", SHM_GUID, pid);

	if (create) {
		hMapFile = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(SHARE_INFO), szName);
	}
	else {
		hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, szName);
	}

	if (hMapFile == NULL) return NULL;

	pBuf = (SHARE_INFO*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SHARE_INFO));
	return pBuf;
}

void free_shm()
{
	if (pBuf) {
		UnmapViewOfFile(pBuf);
		pBuf = NULL;
	}
	if (hMapFile) {
		CloseHandle(hMapFile);
		hMapFile = NULL;
	}
}
