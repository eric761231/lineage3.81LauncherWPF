#include "stdafx.h"
#include "WarehouseStatusHook.h"
#include "ShopStatusHook.h"
#include "LauncherDll.h"
#include "detours.h"
#include <string.h>

#pragma comment(lib, "detours.lib")

__declspec(thread) char g_whBlob[256];
__declspec(thread) char *g_whStatus = 0;
__declspec(thread) unsigned g_whLen = 0;
__declspec(thread) int g_fmtExtraNl = 0;
static int g_consumeLogs = 0;

static void ZeroAfterNul(char *p, size_t cap) {
  if (!p || cap == 0) {
    return;
  }
  for (size_t i = 0; i < cap; i++) {
    if (p[i] == 0) {
      if (i + 1 < cap) {
        memset(p + i + 1, 0, cap - i - 1);
      }
      return;
    }
  }
  p[cap - 1] = 0;
}

// pkt, ident, nameSrc(0x100), nameDst(0x100 轉碼前清零), type, site
extern "C" char *__cdecl ConsumeWhStatus(char *pkt, unsigned identified,
                                        char *nameSrc, char *nameDst,
                                        unsigned typeByte, unsigned site) {
  g_whStatus = 0;
  g_whLen = 0;
  if (nameDst) {
    memset(nameDst, 0, 0x100);
  }
  if (nameSrc) {
    ZeroAfterNul(nameSrc, 0x100);
  }
  if (!pkt) {
    return pkt;
  }
  const unsigned char b0 = *reinterpret_cast<unsigned char *>(pkt);
  const unsigned char b1 = reinterpret_cast<unsigned char *>(pkt)[1];
  const unsigned char b2 = reinterpret_cast<unsigned char *>(pkt)[2];
  const unsigned char b3 = reinterpret_cast<unsigned char *>(pkt)[3];
  char name16[48] = {0};
  if (nameSrc) {
    sprintf_s(name16, "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
              (unsigned)(unsigned char)nameSrc[0], (unsigned)(unsigned char)nameSrc[1],
              (unsigned)(unsigned char)nameSrc[2], (unsigned)(unsigned char)nameSrc[3],
              (unsigned)(unsigned char)nameSrc[4], (unsigned)(unsigned char)nameSrc[5],
              (unsigned)(unsigned char)nameSrc[6], (unsigned)(unsigned char)nameSrc[7],
              (unsigned)(unsigned char)nameSrc[8], (unsigned)(unsigned char)nameSrc[9],
              (unsigned)(unsigned char)nameSrc[10], (unsigned)(unsigned char)nameSrc[11],
              (unsigned)(unsigned char)nameSrc[12], (unsigned)(unsigned char)nameSrc[13],
              (unsigned)(unsigned char)nameSrc[14], (unsigned)(unsigned char)nameSrc[15]);
  }
  if (g_consumeLogs < 24) {
    g_consumeLogs++;
    launcherdll_hook_log(
        "[WhStatus] open type=%u site=%08X pkt ident=%u next=%02X %02X %02X %02X "
        "name16=%s",
        typeByte, site, identified, b0, b1, b2, b3, name16);
  }
  if (b0 == 0x1E && b1 == 0 && b2 == 0 && b3 == 0) {
    return pkt;
  }
  if (identified == 0) {
    if (b0 == 0) {
      return pkt + 1;
    }
    return pkt;
  }
  pkt++;
  if (b0 == 0) {
    return pkt;
  }
  if (b0 >= sizeof(g_whBlob)) {
    if (g_consumeLogs <= 24) {
      launcherdll_hook_log("[WhStatus] len=%u too big, skip attach", (unsigned)b0);
    }
    return pkt + b0;
  }
  memcpy(g_whBlob, pkt, b0);
  g_whBlob[b0] = 0;
  g_whStatus = g_whBlob;
  g_whLen = b0;
  g_fmtExtraNl = 1;
  return pkt + b0;
}

typedef void(__thiscall *AttachStatus_t)(void *self, char *status);
AttachStatus_t real_AttachStatus = (AttachStatus_t)0x4AF070;

typedef char *(__cdecl *SplitFmt_t)(char *src, int *off, int *nlines);
SplitFmt_t real_SplitFmt = (SplitFmt_t)0x4AEC90;

static int FmtBreak(unsigned char c) {
  return c == 0x0A || c == 0x0D || c == 0x17;
}

static int BreakLen(const char *s, int i, int dotSpace) {
  const unsigned char c = static_cast<unsigned char>(s[i]);
  if (FmtBreak(c)) {
    return 1;
  }
  if (dotSpace && c == '.' && s[i + 1] == ' ') {
    return 2;
  }
  return 0;
}

extern "C" void FmtExtraNlSet(int on) {
  g_fmtExtraNl = on ? 1 : 0;
}

static int ApplyListFmtOffEx(const char *src, int *off, int cap, int dotSpace) {
  if (!src || !off || cap < 1) {
    return 0;
  }
  int i = 0;
  while (src[i]) {
    const int bl = BreakLen(src, i, dotSpace);
    if (!bl) {
      break;
    }
    i += bl;
  }
  if (!src[i]) {
    return 0;
  }
  int n = 0;
  off[n++] = i;
  for (; src[i] && n < cap; i++) {
    const int bl = BreakLen(src, i, dotSpace);
    if (!bl) {
      continue;
    }
    int next = i + bl;
    while (src[next]) {
      const int nbl = BreakLen(src, next, dotSpace);
      if (!nbl) {
        break;
      }
      next += nbl;
    }
    if (!src[next]) {
      break;
    }
    off[n++] = next;
    i = next - 1;
  }
  return n;
}

extern "C" int ApplyListFmtOff(const char *src, int *off, int cap) {
  return ApplyListFmtOffEx(src, off, cap, 0);
}

extern "C" int ApplyListFmtOffBag(const char *src, int *off, int cap) {
  return ApplyListFmtOffEx(src, off, cap, 1);
}

char *__cdecl Hook_SplitFmt(char *src, int *off, int *nlines) {
  int saved[32];
  int savedN = 0;
  if (g_fmtExtraNl && src) {
    savedN = ApplyListFmtOff(src, saved, 32);
  }
  char *copied = real_SplitFmt(src, off, nlines);
  if (g_fmtExtraNl && copied && off && nlines && savedN > 0) {
    memcpy(off, saved, savedN * sizeof(int));
    *nlines = savedN;
  }
  return copied;
}

typedef void *(__thiscall *FindInvItem_t)(void *inv, unsigned objid);
static FindInvItem_t FindInvItem = (FindInvItem_t)0x4B1ED0;
static int g_whBagLogs = 0;

void __fastcall Hook_AttachStatus(void *self, void * /*edx*/, char *status) {
  char *use = status;
  if (!status && g_whStatus) {
    use = g_whStatus;
    g_fmtExtraNl = 1;
  }
  real_AttachStatus(self, use);
  g_whStatus = 0;
  g_whLen = 0;
  g_fmtExtraNl = 0;
  if (!self) {
    return;
  }
  const int n = *reinterpret_cast<int *>(static_cast<BYTE *>(self) + 0x14);
  if (n > 0 && n <= 32) {
    return;
  }
  void *inv = *reinterpret_cast<void **>(0x9A9250);
  if (!inv) {
    return;
  }
  const unsigned objid = *reinterpret_cast<unsigned *>(static_cast<BYTE *>(self) + 4);
  if (!objid) {
    return;
  }
  void *bag = FindInvItem(inv, objid);
  if (!bag || bag == self) {
    return;
  }
  const int copied = CopyItemFmtFromBag(self, bag);
  if (copied && g_whBagLogs < 8) {
    g_whBagLogs++;
    launcherdll_hook_log("[WhStatus] bag-fmt n=%d", copied);
  }
}

typedef void(__cdecl *DrawFec_t)(void *font, const char *str, int len, int x, int y,
                                DWORD color);
static DrawFec_t DrawWhFec = (DrawFec_t)0x46FEC0;
static int g_tipLogs = 0;

static int WhLineLen(const char *s, int n) {
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

static int WhVisLen(const char *s, int n) {
  n = WhLineLen(s, n);
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

// 倉庫黑框寬度：官方只 strlen(名稱)*6+10，詳細列比名稱長就會被裁。
extern "C" int __cdecl WhTipWidth(char *name, void *item) {
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
        const int vis = WhVisLen(fmt + start, end - start);
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

// 黑框把 +0x18 當「名稱字串內的偏移」畫，但 4AF070 寫的是 +0xA8 列偏移 → 亂碼。
extern "C" void __cdecl WhTipDrawFmt(void *item, int x, int y0, int color) {
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
  if (g_tipLogs < 8) {
    g_tipLogs++;
    launcherdll_hook_log("[WhStatus] tip n=%d y=%d flen=%d", nlines, y0, flen);
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
    const int len = WhLineLen(fmt + start, end - start);
    if (len <= 0) {
      continue;
    }
    DrawWhFec(font, fmt + start, len, x, y0 + i * 0xC, static_cast<DWORD>(color));
  }
}

typedef void(__thiscall *WhEnchantTint_t)(void *self, void *font, const char *s, int len,
                                         int x, int y);
static WhEnchantTint_t WhEnchantTint = (WhEnchantTint_t)0x5B4760;
typedef void(__cdecl *InvEnchantTint_t)(void *font, const char *s, int unk, int x, int y);
static InvEnchantTint_t InvEnchantTint = (InvEnchantTint_t)0x4700B0;
typedef void(__cdecl *DrawNameE6_t)(void *font, const char *s, int len, int x, int y,
                                   DWORD color, int flag);
static DrawNameE6_t DrawNameE6 = (DrawNameE6_t)0x46E6B0;
typedef void(__cdecl *DrawNameF0_t)(void *font, const char *s, int x, int y, DWORD color,
                                   int flag);
static DrawNameF0_t DrawNameF0 = (DrawNameF0_t)0x46E0F0;

static int EnchantPrefixLen(const char *s) {
  if (!s || s[0] != '+') {
    return 0;
  }
  int n = 1;
  while (s[n] >= '0' && s[n] <= '9') {
    n++;
  }
  if (n < 2) {
    return 0;
  }
  if (s[n] == ' ') {
    n++;
  }
  return n;
}

static DWORD BlessColor(int idx) {
  return static_cast<DWORD>(
      *reinterpret_cast<short *>(0xC2D698 + idx * 2));
}

static int CopyPrefix(const char *name, char *buf, int cap) {
  int pref = EnchantPrefixLen(name);
  if (pref < 2) {
    pref = 3;
  }
  if (pref >= cap) {
    pref = cap - 1;
  }
  memcpy(buf, name, pref);
  buf[pref] = 0;
  return pref;
}

// 官方只 strncpy 3 字（+ 與兩位數字），+100／+99999999 後面數字變回祝福白。
extern "C" void __cdecl WhDrawPlusCounted(void *item, char *name, int counted, int x0,
                                         int y0, int bless) {
  char buf[32];
  const int pref = CopyPrefix(name, buf, sizeof(buf));
  void *font = *reinterpret_cast<void **>(0x9A84E0);
  const int y = y0 - 7;
  const int xPref = x0 - 4;
  WhEnchantTint(item, font, buf, pref, xPref, y);
  int rest = counted - pref;
  if (rest < 0) {
    rest = 0;
  }
  DrawNameE6(font, name + pref, rest, xPref + pref * 6, y, BlessColor(bless), 1);
}

extern "C" void __cdecl WhDrawPlusCstr(void *item, char *name, int x0, int y0, int bless) {
  char buf[32];
  const int pref = CopyPrefix(name, buf, sizeof(buf));
  void *font = *reinterpret_cast<void **>(0x9A84E0);
  const int y = y0 - 7;
  const int xPref = x0 - 4;
  WhEnchantTint(item, font, buf, pref, xPref, y);
  DrawNameF0(font, name + pref, xPref + pref * 6, y, BlessColor(bless), 1);
}

extern "C" void __cdecl InvDrawPlusCstr(char *name, int x0, int y0, int bless) {
  char buf[32];
  const int pref = CopyPrefix(name, buf, sizeof(buf));
  void *font = *reinterpret_cast<void **>(0x9A84E0);
  const int y = y0 - 7;
  const int xPref = x0 - 4;
  InvEnchantTint(font, buf, 0, xPref, y);
  DrawNameF0(font, name + pref, xPref + pref * 6, y, BlessColor(bless), 1);
}

static const BYTE kAfterParse[6] = {0x83, 0xC4, 0x28, 0x89, 0x45, 0x08};

void PatchJmp6(void *src, void *dst) {
  DWORD old = 0;
  VirtualProtect(src, 6, PAGE_EXECUTE_READWRITE, &old);
  BYTE *p = static_cast<BYTE *>(src);
  p[0] = 0xE9;
  *reinterpret_cast<DWORD *>(p + 1) =
      (DWORD)((uintptr_t)dst - (uintptr_t)src - 5);
  p[5] = 0x90;
  VirtualProtect(src, 6, old, &old);
  FlushInstructionCache(GetCurrentProcess(), src, 6);
}

void PatchJmpN(void *src, void *dst, size_t nbytes) {
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

__declspec(naked) void Tramp_WhTipWidth() {
  __asm {
    mov eax, dword ptr [ebp - 0x4C]
    mov ecx, dword ptr [eax + 0x4C]
    mov edx, dword ptr [eax + 0x58]
    mov ecx, dword ptr [edx + ecx * 4]
    push ecx
    push dword ptr [ebp - 0x38]
    call WhTipWidth
    add esp, 8
    mov dword ptr [ebp - 0x40], eax
    push 0x5B6605
    ret
  }
}

__declspec(naked) void Tramp_WhTipDraw18() {
  __asm {
    mov eax, dword ptr [ebp - 0x4C]
    mov ecx, dword ptr [eax + 0x4C]
    mov edx, dword ptr [eax + 0x58]
    mov ecx, dword ptr [edx + ecx * 4]
    mov eax, dword ptr [ebp - 0x30]
    movsx eax, word ptr [eax * 2 + 0xC2D698]
    push eax
    mov edx, dword ptr [ebp - 0xC]
    add edx, 0x18
    push edx
    push dword ptr [ebp - 0x10]
    push ecx
    call WhTipDrawFmt
    add esp, 16
    push 0x5B6A37
    ret
  }
}

__declspec(naked) void Tramp_WhTipDraw0C() {
  __asm {
    mov eax, dword ptr [ebp - 0x4C]
    mov ecx, dword ptr [eax + 0x4C]
    mov edx, dword ptr [eax + 0x58]
    mov ecx, dword ptr [edx + ecx * 4]
    mov eax, dword ptr [ebp - 0x30]
    movsx eax, word ptr [eax * 2 + 0xC2D698]
    push eax
    mov edx, dword ptr [ebp - 0xC]
    add edx, 0xC
    push edx
    push dword ptr [ebp - 0x10]
    push ecx
    call WhTipDrawFmt
    add esp, 16
    push 0x5B6B78
    ret
  }
}

__declspec(naked) void Tramp_WhPlusCounted() {
  __asm {
    push dword ptr [ebp - 0x20]
    push dword ptr [ebp - 8]
    push dword ptr [ebp - 0xC]
    push dword ptr [ebp - 0x40]
    push dword ptr [ebp - 0x18]
    push dword ptr [ebp - 0x68]
    call WhDrawPlusCounted
    add esp, 24
    push 0x5B4B15
    ret
  }
}

__declspec(naked) void Tramp_WhPlusCstr() {
  __asm {
    push dword ptr [ebp - 0x20]
    push dword ptr [ebp - 8]
    push dword ptr [ebp - 0xC]
    push dword ptr [ebp - 0x18]
    push dword ptr [ebp - 0x68]
    call WhDrawPlusCstr
    add esp, 20
    push 0x5B4C6D
    ret
  }
}

__declspec(naked) void Tramp_InvPlusCstr() {
  __asm {
    mov ecx, dword ptr [ebp - 0x16C]
    push dword ptr [ebp - 0x18]
    push dword ptr [ebp - 4]
    push dword ptr [ebp - 8]
    mov eax, dword ptr [ecx + 0xC]
    push eax
    call InvDrawPlusCstr
    add esp, 16
    push 0x594B17
    ret
  }
}

__declspec(naked) void Tramp_WhType29() {
  __asm {
    add esp, 0x28
    mov dword ptr [ebp + 8], eax
    movzx edx, byte ptr [ebp - 0xA]
    push 0x52D762
    push edx
    lea ecx, [ebp - 0x10A0]
    push ecx
    lea edx, [ebp - 0x11A8]
    push edx
    movzx ecx, byte ptr [ebp - 0x11AA]
    push ecx
    push eax
    call ConsumeWhStatus
    add esp, 24
    mov dword ptr [ebp + 8], eax
    push 0x52D768
    ret
  }
}

__declspec(naked) void Tramp_WhType21() {
  __asm {
    add esp, 0x28
    mov dword ptr [ebp + 8], eax
    movzx edx, byte ptr [ebp - 0xA]
    push 0x52D4EC
    push edx
    lea ecx, [ebp - 0xA60]
    push ecx
    lea edx, [ebp - 0xB68]
    push edx
    movzx ecx, byte ptr [ebp - 0xB6A]
    push ecx
    push eax
    call ConsumeWhStatus
    add esp, 24
    mov dword ptr [ebp + 8], eax
    push 0x52D4F2
    ret
  }
}

__declspec(naked) void Tramp_WhType0B() {
  __asm {
    add esp, 0x28
    mov dword ptr [ebp + 8], eax
    movzx edx, byte ptr [ebp - 0xA]
    push 0x52CF94
    push edx
    lea ecx, [ebp - 0x140]
    push ecx
    lea edx, [ebp - 0x248]
    push edx
    movzx ecx, byte ptr [ebp - 0x24A]
    push ecx
    push eax
    call ConsumeWhStatus
    add esp, 24
    mov dword ptr [ebp + 8], eax
    push 0x52CF9A
    ret
  }
}

void InstallWarehouseStatusHook() {
  BYTE *pAttach = reinterpret_cast<BYTE *>(0x4AF070);
  BYTE *p29 = reinterpret_cast<BYTE *>(0x52D762);
  BYTE *p21 = reinterpret_cast<BYTE *>(0x52D4EC);
  BYTE *p0B = reinterpret_cast<BYTE *>(0x52CF94);
  BYTE *pWidth = reinterpret_cast<BYTE *>(0x5B65F0);
  BYTE *pDraw18 = reinterpret_cast<BYTE *>(0x5B6930);
  BYTE *pDraw0C = reinterpret_cast<BYTE *>(0x5B6A73);
  BYTE *pPlusA = reinterpret_cast<BYTE *>(0x5B4A30);
  BYTE *pPlusB = reinterpret_cast<BYTE *>(0x5B4B98);
  BYTE *pPlusInv = reinterpret_cast<BYTE *>(0x594AA0);
  static const BYTE kTipWidth[21] = {0x8B, 0x55, 0xC8, 0x52, 0xE8, 0x33, 0x3B,
                                     0x1E, 0x00, 0x83, 0xC4, 0x04, 0x6B, 0xC0,
                                     0x06, 0x83, 0xC0, 0x0A, 0x89, 0x45, 0xC0};
  static const BYTE kTipDraw18[10] = {0x83, 0x7D, 0xC8, 0x00, 0x0F, 0x84,
                                      0xFD, 0x00, 0x00, 0x00};
  static const BYTE kTipDraw0C[10] = {0x83, 0x7D, 0xC8, 0x00, 0x0F, 0x84,
                                      0xFB, 0x00, 0x00, 0x00};
  static const BYTE kPlusA[8] = {0x6A, 0x03, 0x8B, 0x55, 0xE8, 0x52, 0x8D, 0x45};
  static const BYTE kPlusB[8] = {0x6A, 0x03, 0x8B, 0x45, 0xE8, 0x50, 0x8D, 0x4D};
  static const BYTE kPlusInv[8] = {0x6A, 0x03, 0x8B, 0x95, 0x94, 0xFE, 0xFF, 0xFF};
  BYTE *pSplit = reinterpret_cast<BYTE *>(0x4AEC90);
  if (pAttach[0] != 0x55 || pAttach[1] != 0x8B || pAttach[2] != 0xEC) {
    launcherdll_hook_log("[WhStatus] 4AF070 prologue mismatch, skipping");
    return;
  }
  if (memcmp(p29, kAfterParse, 6) != 0 || memcmp(p21, kAfterParse, 6) != 0 ||
      memcmp(p0B, kAfterParse, 6) != 0) {
    launcherdll_hook_log("[WhStatus] dchcdcs epilogue mismatch, skipping");
    return;
  }

  const int splitPrologueOk =
      (pSplit[0] == 0x55 && pSplit[1] == 0x8B && pSplit[2] == 0xEC);
  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  DetourAttach(&(PVOID &)real_AttachStatus, reinterpret_cast<PVOID>(Hook_AttachStatus));
  int splitOk = 0;
  if (splitPrologueOk) {
    DetourAttach(&(PVOID &)real_SplitFmt, reinterpret_cast<PVOID>(Hook_SplitFmt));
    splitOk = 1;
  } else {
    launcherdll_hook_log("[WhStatus] 4AEC90 prologue mismatch, skip nl");
  }
  const LONG result = DetourTransactionCommit();
  if (result != 0) {
    launcherdll_hook_log("[WhStatus] 4AF070/4AEC90 Detour result=%ld", result);
    return;
  }
  if (!splitPrologueOk) {
    splitOk = 0;
  }

  PatchJmp6(p29, reinterpret_cast<void *>(Tramp_WhType29));
  PatchJmp6(p21, reinterpret_cast<void *>(Tramp_WhType21));
  PatchJmp6(p0B, reinterpret_cast<void *>(Tramp_WhType0B));

  int tipOk = 0;
  if (memcmp(pWidth, kTipWidth, sizeof(kTipWidth)) == 0 &&
      memcmp(pDraw18, kTipDraw18, sizeof(kTipDraw18)) == 0 &&
      memcmp(pDraw0C, kTipDraw0C, sizeof(kTipDraw0C)) == 0) {
    PatchJmpN(pWidth, reinterpret_cast<void *>(Tramp_WhTipWidth), sizeof(kTipWidth));
    PatchJmpN(pDraw18, reinterpret_cast<void *>(Tramp_WhTipDraw18), sizeof(kTipDraw18));
    PatchJmpN(pDraw0C, reinterpret_cast<void *>(Tramp_WhTipDraw0C), sizeof(kTipDraw0C));
    tipOk = 1;
  } else {
    launcherdll_hook_log("[WhStatus] tooltip site mismatch, skip 5B65F0/6930/6A73");
  }
  int plusOk = 0;
  if (memcmp(pPlusA, kPlusA, sizeof(kPlusA)) == 0 &&
      memcmp(pPlusB, kPlusB, sizeof(kPlusB)) == 0 &&
      memcmp(pPlusInv, kPlusInv, sizeof(kPlusInv)) == 0) {
    PatchJmpN(pPlusA, reinterpret_cast<void *>(Tramp_WhPlusCounted), 0x74);
    PatchJmpN(pPlusB, reinterpret_cast<void *>(Tramp_WhPlusCstr), 0x6C);
    PatchJmpN(pPlusInv, reinterpret_cast<void *>(Tramp_InvPlusCstr), 0x77);
    plusOk = 1;
  } else {
    launcherdll_hook_log("[WhStatus] plus-prefix site mismatch, skip 5B4A30/4B98/594AA0");
  }
  launcherdll_hook_log(
      "[WhStatus] 52D762/52D4EC/52CF94 consume + 4AF070 attach result=0 tip=%d plus=%d "
      "nl=%d",
      tipOk, plusOk, splitOk);
}
