#include "stdafx.h"
#include "MorphPakInject.h"
#include "LauncherDll.h"
#include "PatchUtil.h"
#include "configenc.h"
#include "zlib.h"
#include "VMProtectSDK.h"

extern SHARE_INFO ShareInfo;

static BYTE *g_morphBuffer = NULL;
static DWORD g_morphBufferLen = 0;

__declspec(naked) void GetFileData(void) {
  __asm {
		mov eax, g_morphBufferLen
		mov dword ptr ss:[ebp - 0x14], eax
		mov eax, g_morphBuffer
		mov edx, dword ptr ss:[ebp - 0x23C]
		mov dword ptr ds:[edx + 0x08], eax
		mov eax, 0x0058794F
		jmp eax
  }
}

static BYTE *LoadMorphFile() {
  FILE *fp = NULL;
  unsigned int len = 0;
  g_morphBufferLen = 0;

  if (_wfopen_s(&fp, ShareInfo.bdfile, L"rb") != 0 || fp == NULL)
    return NULL;

  fseek(fp, 0, SEEK_END);
  len = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (len < 20) {
    fclose(fp);
    return NULL;
  }

  BYTE *file_data = new BYTE[len];
  fread(file_data, 1, len, fp);
  fclose(fp);

  VMProtectBegin;
  config_decrypt(&file_data[4], &file_data[20], len - 20);

  DWORD un_len = *(DWORD *)file_data;
  uLongf destLen = un_len;
  BYTE *un_buffer = new BYTE[un_len + 1];
  int ret = uncompress(un_buffer, &destLen, &file_data[20], len - 20);
  un_buffer[destLen] = 0;
  delete[] file_data;
  VMProtectEnd;

  if (ret == Z_OK) {
    g_morphBufferLen = destLen;
    return un_buffer;
  }

  delete[] un_buffer;
  return NULL;
}

bool MorphPak_Load() {
  g_morphBuffer = LoadMorphFile();
  launcherdll_hook_log("[Install] MorphPak load %s",
                       g_morphBuffer ? "ok" : "fail");
  return g_morphBuffer != NULL;
}

void MorphPak_InstallHook() {
  if (g_morphBuffer == NULL)
    return;
  HookCode((void *)0x0058788B, (void *)GetFileData, 5);
  launcherdll_hook_log("[Install] MorphPak hook ok");
}
