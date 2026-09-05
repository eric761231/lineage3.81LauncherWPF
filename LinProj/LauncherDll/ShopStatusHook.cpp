#include "stdafx.h"
#include "ShopStatusHook.h"
#include "WarehouseStatusHook.h"
#include "LauncherDll.h"
#include <string.h>

static int g_shBlobLogs = 0;
static int g_shTipLogs = 0;
static int g_shCloneLogs = 0;

typedef char *(__cdecl *SplitFmt_t)(char *src, int *off, int *nlines);
static SplitFmt_t SplitFmt = (SplitFmt_t)0x4AEC90;

typedef void(__cdecl *DrawFec_t)(void *font, const char *str, int len, int x, int y,
                                DWORD color);
static DrawFec_t DrawFec = (DrawFec_t)0x46FEC0;

static int LineLen(const char *s, int n) {
  if (!s || n <= 0) {
    return 0;
  }
  while (n > 0) {
    const unsigned char c = static_cast<unsigned char>(s[n - 1]);
    if (c == 0x17 || c == 0x0A || c == 0x0D || c == 0) {
      n--;
    } else {
      break;
    }
  }
  return n;
}

static int VisLen(const char *s, int n) {
  n = LineLen(s, n);
  int vis = 0;
  for (int i = 0; i < n;) {
    if (i + 2 < n && static_cast<unsigned char>(s[i]) == 0x5C &&
        static_cast<unsigned char>(s[i + 1]) == 0x66) {
      const unsigned char ch = static_cast<unsigned char>(s[i + 2]);
      if (ch >= 0x30 && ch < 0x7D) {
        i += 3;
        continue;
      }
    }
    vis++;
    i++;
  }
  return vis;
}

extern "C" int __cdecl ShopTipWidth(char *name, void *item) {
  int w = 0;
  if (name) {
    w = static_cast<int>(strlen(name));
  }
  if (item) {
    char *fmt = *reinterpret_cast<char **>(static_cast<BYTE *>(item) + 0xa8);
    const int nlines = *reinterpret_cast<int *>(static_cast<BYTE *>(item) + 0x14);
    int *off = reinterpret_cast<int *>(static_cast<BYTE *>(item) + 0x18);
    if (fmt && nlines > 0 && nlines <= 32) {
      const int flen = static_cast<int>(strlen(fmt));
      for (int i = 0; i < nlines; i++) {
        int start = off[i];
        if (start < 0 || start > flen) {
          continue;
        }
        int end = (i + 1 < nlines) ? off[i + 1] : flen;
        if (end < start) {
          end = flen;
        }
        if (end > flen) {
          end = flen;
        }
        const int vis = VisLen(fmt + start, end - start);
        if (vis > w) {
          w = vis;
        }
      }
    }
  }
  if (w < 1) {
    w = 1;
  }
  return w * 6 + 0xA;
}

static int ShopNlines(void *item) {
  if (!item) {
    return 0;
  }
  int n = *reinterpret_cast<int *>(static_cast<BYTE *>(item) + 0x14);
  if (n > 0 && n <= 32) {
    return n;
  }
  char *fmt = *reinterpret_cast<char **>(static_cast<BYTE *>(item) + 0xa8);
  if (!fmt || !fmt[0]) {
    return 0;
  }
  n = 1;
  for (const char *p = fmt; *p; p++) {
    if (static_cast<unsigned char>(*p) == 0x17) {
      n++;
      if (n > 32) {
        return 32;
      }
    }
  }
  return n;
}

extern "C" int __cdecl ShopTipHeight(void *item) {
  int extra = 0xC;
  if (item && *reinterpret_cast<void **>(static_cast<BYTE *>(item) + 0x10)) {
    extra = 0x18;
  }
  return extra + ShopNlines(item) * 0xC + 0xA;
}

extern "C" int __cdecl CopyItemFmtFromBag(void *dst, void *srcItem) {
  if (!dst || !srcItem || dst == srcItem) {
    return 0;
  }
  char *src = *reinterpret_cast<char **>(static_cast<BYTE *>(srcItem) + 0xa8);
  const int nsrc = *reinterpret_cast<int *>(static_cast<BYTE *>(srcItem) + 0x14);
  if (!src || !src[0] || nsrc <= 0 || nsrc > 32) {
    return 0;
  }
  char buf[0x400];
  strncpy_s(buf, sizeof(buf), src, _TRUNCATE);
  int dummyOff[32];
  int dummyN = 0;
  char *copied = SplitFmt(buf, dummyOff, &dummyN);
  if (copied) {
    const int nlist = ApplyListFmtOffBag(src, reinterpret_cast<int *>(static_cast<BYTE *>(dst) + 0x18), 32);
    *reinterpret_cast<int *>(static_cast<BYTE *>(dst) + 0x14) = nlist > 0 ? nlist : nsrc;
    *reinterpret_cast<char **>(static_cast<BYTE *>(dst) + 0xa8) = copied;
  }
  const int n = *reinterpret_cast<int *>(static_cast<BYTE *>(dst) + 0x14);
  if (n > 0 && n <= 32) {
    return n;
  }
  return 0;
}

extern "C" void __cdecl ShopCopyBagFmt(void *clone, void *bag) {
  const int n = CopyItemFmtFromBag(clone, bag);
  if (n && g_shCloneLogs < 8) {
    g_shCloneLogs++;
    launcherdll_hook_log("[ShStatus] clone-fmt n=%d", n);
  }
}

extern "C" void __cdecl ShopTipDrawFmt(void *item, int x, int y0, int color) {
  if (!item) {
    return;
  }
  char *fmt = *reinterpret_cast<char **>(static_cast<BYTE *>(item) + 0xa8);
  const int nlines = *reinterpret_cast<int *>(static_cast<BYTE *>(item) + 0x14);
  int *off = reinterpret_cast<int *>(static_cast<BYTE *>(item) + 0x18);
  if (!fmt || nlines <= 0 || nlines > 32) {
    return;
  }
  void *font = *reinterpret_cast<void **>(0x9A84E0);
  const int flen = static_cast<int>(strlen(fmt));
  if (g_shTipLogs < 8) {
    g_shTipLogs++;
    launcherdll_hook_log("[ShStatus] tip n=%d y=%d flen=%d", nlines, y0, flen);
  }
  for (int i = 0; i < nlines; i++) {
    int start = off[i];
    if (start < 0 || start > flen) {
      continue;
    }
    int end = (i + 1 < nlines) ? off[i + 1] : flen;
    if (end < start) {
      continue;
    }
    if (end > flen) {
      end = flen;
    }
    const int len = LineLen(fmt + start, end - start);
    if (len <= 0) {
      continue;
    }
    DrawFec(font, fmt + start, len, x, y0 + i * 0xC, static_cast<DWORD>(color));
  }
}

extern "C" char *__cdecl ShopPickStatus(char *blob, unsigned len) {
  if (!len) {
    FmtExtraNlSet(0);
    return 0;
  }
  FmtExtraNlSet(1);
  if (g_shBlobLogs < 16) {
    g_shBlobLogs++;
    launcherdll_hook_log("[ShStatus] blob len=%u first=%02X", len,
                         (unsigned)(unsigned char)blob[0]);
  }
  return blob;
}

static void PatchJmpN(void *src, void *dst, size_t nbytes) {
  if (nbytes < 5) {
    return;
  }
  DWORD old = 0;
  VirtualProtect(src, nbytes, PAGE_EXECUTE_READWRITE, &old);
  BYTE *p = static_cast<BYTE *>(src);
  p[0] = 0xE9;
  *reinterpret_cast<DWORD *>(p + 1) =
      (DWORD)((uintptr_t)dst - (uintptr_t)src - 5);
  for (size_t i = 5; i < nbytes; i++) {
    p[i] = 0x90;
  }
  VirtualProtect(src, nbytes, old, &old);
  FlushInstructionCache(GetCurrentProcess(), src, nbytes);
}

__declspec(naked) void Tramp_ShopStatusPtr() {
  __asm {
    movzx eax, byte ptr [ebp - 0x619]
    push eax
    lea ecx, [ebp - 0x410]
    push ecx
    call ShopPickStatus
    add esp, 8
    push eax
    push 0x5423E2
    ret
  }
}

__declspec(naked) void Tramp_ShopTipWidth() {
  __asm {
    mov eax, dword ptr [ebp - 0x168]
    mov ecx, dword ptr [eax + 0x4C]
    mov edx, dword ptr [eax + 0x58]
    mov ecx, dword ptr [edx + ecx * 4]
    push ecx
    push ecx
    mov eax, dword ptr [ecx + 0xC]
    push eax
    call ShopTipWidth
    add esp, 8
    mov dword ptr [ebp - 0x38], eax
    call ShopTipHeight
    add esp, 4
    mov dword ptr [ebp - 0x34], eax
    push 0x59609E
    ret
  }
}

__declspec(naked) void Tramp_ShopCloneFmt() {
  __asm {
    mov dl, byte ptr [ecx + 0xB0]
    mov byte ptr [eax + 0xB0], dl
    push eax
    push ecx
    push ecx
    push eax
    call ShopCopyBagFmt
    add esp, 8
    pop ecx
    pop eax
    push 0x595742
    ret
  }
}

__declspec(naked) void Tramp_ShopTipDraw() {
  __asm {
    mov eax, dword ptr [ebp - 0x168]
    mov ecx, dword ptr [eax + 0x4C]
    mov edx, dword ptr [eax + 0x58]
    mov ecx, dword ptr [edx + ecx * 4]
    mov eax, dword ptr [ebp - 0x28]
    movsx eax, word ptr [eax * 2 + 0xC2D698]
    push eax
    mov edx, dword ptr [ebp - 0xC]
    add edx, 0xC
    push edx
    push dword ptr [ebp - 0x10]
    push ecx
    call ShopTipDrawFmt
    add esp, 16
    push 0x5966A0
    ret
  }
}

void InstallShopStatusHook() {
  // 實驗：商店列表第一行被砍，整組 JMP 先不打。要恢復把這段 return 拿掉即可。
  launcherdll_hook_log("[ShStatus] install skipped (experiment)");
  return;

  BYTE *pBlob = reinterpret_cast<BYTE *>(0x5423DD);
  BYTE *pWidth = reinterpret_cast<BYTE *>(0x596053);
  BYTE *pDraw = reinterpret_cast<BYTE *>(0x596573);
  BYTE *pClone = reinterpret_cast<BYTE *>(0x595736);
  static const BYTE kShopPushStr[5] = {0x68, 0x37, 0x42, 0x8D, 0x00};
  static const BYTE kShopWidth[7] = {0xC7, 0x45, 0xC8, 0x8E, 0x00, 0x00, 0x00};
  static const BYTE kShopDraw[10] = {0x83, 0x7D, 0xD0, 0x00, 0x0F, 0x84,
                                     0x23, 0x01, 0x00, 0x00};
  static const BYTE kShopClone[12] = {0x8A, 0x91, 0xB0, 0x00, 0x00, 0x00,
                                      0x88, 0x90, 0xB0, 0x00, 0x00, 0x00};

  int blobOk = 0;
  if (memcmp(pBlob, kShopPushStr, sizeof(kShopPushStr)) == 0) {
    PatchJmpN(pBlob, reinterpret_cast<void *>(Tramp_ShopStatusPtr), 5);
    blobOk = 1;
  } else {
    launcherdll_hook_log("[ShStatus] 5423DD mismatch, skip blob");
  }

  int cloneOk = 0;
  if (memcmp(pClone, kShopClone, sizeof(kShopClone)) == 0) {
    PatchJmpN(pClone, reinterpret_cast<void *>(Tramp_ShopCloneFmt),
              sizeof(kShopClone));
    cloneOk = 1;
  } else {
    launcherdll_hook_log("[ShStatus] 595736 clone mismatch, skip");
  }

  int widthOk = 0;
  int drawOk = 0;
  if (memcmp(pWidth, kShopWidth, sizeof(kShopWidth)) == 0) {
    PatchJmpN(pWidth, reinterpret_cast<void *>(Tramp_ShopTipWidth),
              sizeof(kShopWidth));
    widthOk = 1;
  } else {
    launcherdll_hook_log("[ShStatus] 596053 width mismatch, skip");
  }
  if (memcmp(pDraw, kShopDraw, sizeof(kShopDraw)) == 0) {
    PatchJmpN(pDraw, reinterpret_cast<void *>(Tramp_ShopTipDraw),
              sizeof(kShopDraw));
    drawOk = 1;
  } else {
    launcherdll_hook_log(
        "[ShStatus] 596573 draw mismatch, skip 10=%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
        pDraw[0], pDraw[1], pDraw[2], pDraw[3], pDraw[4], pDraw[5], pDraw[6],
        pDraw[7], pDraw[8], pDraw[9]);
  }

  launcherdll_hook_log("[ShStatus] blob=%d width=%d draw=%d clone=%d",
                       blobOk, widthOk, drawOk, cloneOk);
}
