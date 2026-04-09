#include "stdafx.h"
#include "ShareMemory.h"
#include <stdio.h>

static HANDLE hMapFile = NULL;
static SHARE_INFO* pBuf = NULL;
static const char* SHM_GUID = "{385FC524-96E3-4839-9909-1F2135D4F928}";

SHARE_INFO* get_shm(DWORD pid, bool create)
{
	char szName[128];
	// 名稱需與 C# LaunchService.cs 中的 SHM_GUID 保持同步
	sprintf_s(szName, "Local\\%s", SHM_GUID);

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
