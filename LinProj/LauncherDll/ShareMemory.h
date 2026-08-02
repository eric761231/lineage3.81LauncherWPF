#pragma once
#include <windows.h>
#include <tchar.h>

#pragma pack(push, 1)
struct SHARE_INFO {
	BYTE ip[32];
	int port;
	bool encrypt;
	bool usehelper;
	BYTE key[16];
	bool usebd;
	/** 與 LinLauncher ShareInfo.BdFile、GamePathHelper.MaxBdFileCharCount 一致（約 MAX_PATH）。 */
	wchar_t bdfile[260];
	bool read;
	bool randenc;
	unsigned int RSA_N;
	unsigned int RSA_D;
	unsigned int magic;
	char Account[32];
	char Password[32];
};
#pragma pack(pop)

// 共享記憶體存取介面（DLL 端呼叫 create=false，Launcher 端呼叫 create=true）
SHARE_INFO* get_shm(DWORD pid, bool create = false);
void free_shm();

