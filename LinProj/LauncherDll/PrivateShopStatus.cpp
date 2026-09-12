#include "stdafx.h"
#include "PrivateShopStatus.h"
#include "WarehouseStatusHook.h"
#include "LauncherDll.h"
#include <string.h>

// ---------------------------------------------------------------------------
// 全域除錯與 Log 計數器：限制高頻路徑的 Log 輸出次數，避免刷屏。
// ---------------------------------------------------------------------------
static int g_shBlobLogs = 0;   // PrivateShopPickStatus 的執行次數
static int g_shTipLogs = 0;    // ShopTipDrawFmt 的執行次數
static int g_shCloneLogs = 0;  // PrivateShopCopyBagFmt 的執行次數

// ---------------------------------------------------------------------------
// 遊戲主程式內的原生函式指標與固定記憶體位址。
// ---------------------------------------------------------------------------
// SplitFmt：將原始格式化字串依 0x17 切割成多行並填入偏移量。
// 位址 0x4AEC90，使用 cdecl 呼叫慣例。
typedef char *(__cdecl *SplitFmt_t)(char *src, int *off, int *nlines);
static SplitFmt_t SplitFmt = (SplitFmt_t)0x4AEC90;

// DrawFec：遊戲底層文字繪製函式。
// 位址 0x46FEC0，使用 cdecl 呼叫慣例。
typedef void(__cdecl *DrawFec_t)(void *font, const char *str, int len, int x, int y,
                                DWORD color);
static DrawFec_t DrawFec = (DrawFec_t)0x46FEC0;

/**
 * @brief 計算去掉末端特殊控制字元後的字串實際長度。
 * @param s 字串指標
 * @param n 原始長度
 * @return 裁切後的字串長度
 */
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

/**
 * @brief 計算字串的可見字元數量（過濾顏色控制碼如 \\fX）。
 * @param s 字串指標
 * @param n 字串長度
 * @return 可見字元數量
 */
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

/**
 * @brief 計算商店提示框（Tooltip）的寬度。
 * @param name 道具名稱
 * @param item 道具結構指標
 * @return 計算出的 Tip 視窗寬度
 */
extern "C" int __cdecl PrivateShopTipWidth(char *name, void *item) {
  int w = 0;
  // 先以道具名稱長度作為目前寬度下限，再與說明文字最長行比較。
  if (name) {
    w = static_cast<int>(strlen(name));
  }
  if (item) {
    // 道具結構：+0xA8 為格式字串，+0x14 為行數，+0x18 為行偏移量陣列。
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
        // 取所有說明行的可見字元數最大值，避免控制碼影響視窗寬度。
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
  // 遊戲以每字元約 6 像素計算，另加固定左右邊距 0xA。
  return w * 6 + 0xA;
}

/**
 * @brief 取得商店道具格式說明的行數。
 * @param item 道具結構指標
 * @return 說明文字行數
 */
static int ShopNlines(void *item) {
  if (!item) {
    return 0;
  }
  // 優先使用道具結構 +0x14 已計算好的行數。
  int n = *reinterpret_cast<int *>(static_cast<BYTE *>(item) + 0x14);
  if (n > 0 && n <= 32) {
    return n;
  }
  // 若結構沒有有效行數，退回掃描 +0xA8 格式字串中的 0x17 分隔符。
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

/**
 * @brief 計算商店提示框（Tooltip）的高度。
 * @param item 道具結構指標
 * @return 計算出的 Tip 視窗高度
 */
extern "C" int __cdecl PrivateShopTipHeight(void *item) {
  // +0x10 非空代表有附加狀態說明，需要增加 Tooltip 基礎高度。
  int extra = 0xC;
  if (item && *reinterpret_cast<void **>(static_cast<BYTE *>(item) + 0x10)) {
    extra = 0x18;
  }
  // 高度 = 基礎高度 + 行數 * 每行高度 0xC + 固定邊距 0xA。
  return extra + ShopNlines(item) * 0xC + 0xA;
}

/**
 * @brief 從背包道具複製格式說明到目標結構中。
 * @param dst 目標道具指標
 * @param srcItem 來源背包道具指標
 * @return 格式總行數
 */
extern "C" int __cdecl PrivateShopCopyItemFmtFromBag(void *dst, void *srcItem) {
  if (!dst || !srcItem || dst == srcItem) {
    return 0;
  }
  // 從來源背包道具的 +0xA8 取得格式字串，+0x14 取得原始行數。
  char *src = *reinterpret_cast<char **>(static_cast<BYTE *>(srcItem) + 0xa8);
  const int nsrc = *reinterpret_cast<int *>(static_cast<BYTE *>(srcItem) + 0x14);
  if (!src || !src[0] || nsrc <= 0 || nsrc > 32) {
    return 0;
  }
  // 先複製到區域緩衝區，交由原生 SplitFmt 建立目標使用的 heap 字串。
  char buf[0x400];
  strncpy_s(buf, sizeof(buf), src, _TRUNCATE);
  int dummyOff[32];
  int dummyN = 0;
  char *copied = SplitFmt(buf, dummyOff, &dummyN);
  if (copied) {
    // 目標 +0x18 的行偏移量必須依來源字串重新計算，不能共用來源指標。
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

/**
 * @brief 商店複製背包格式說明處理。
 * @param clone 克隆的商店道具指標
 * @param bag 背包道具指標
 */
extern "C" void __cdecl PrivateShopCopyBagFmt(void *clone, void *bag) {
  const int n = PrivateShopCopyItemFmtFromBag(clone, bag);
  if (n && g_shCloneLogs < 8) {
    g_shCloneLogs++;
    launcherdll_hook_log("[ShStatus] clone-fmt n=%d", n);
  }
}

/**
 * @brief 繪製商店提示框（Tooltip）中的格式化文字。
 * @param item 道具結構指標
 * @param x 繪製起點 X 座標
 * @param y0 繪製起點 Y 座標
 * @param color 文字顏色
 */
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
  // 取得遊戲全域字型物件，位址為 0x9A84E0。
  void *font = *reinterpret_cast<void **>(0x9A84E0);
  const int flen = static_cast<int>(strlen(fmt));
  if (g_shTipLogs < 8) {
    g_shTipLogs++;
    launcherdll_hook_log("[ShStatus] tip n=%d y=%d flen=%d", nlines, y0, flen);
  }
  // 逐行繪製，行距固定為 0xC；LineLen 會移除行尾控制字元。
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

/**
 * @brief 挑選並處理商店狀態 Blob 資料。
 * @param blob 資料指標
 * @param len 資料長度
 * @return 處理後的 blob 指標
 */
extern "C" char *__cdecl PrivateShopPickStatus(char *blob, unsigned len) {
  if (!len) {
    FmtExtraNlSet(0);
    return 0;
  }
  // 有狀態資料時開啟額外換行，讓後續 Tooltip 能保留狀態列。
  FmtExtraNlSet(1);
  if (g_shBlobLogs < 16) {
    g_shBlobLogs++;
    launcherdll_hook_log("[ShStatus] blob len=%u first=%02X", len,
                         (unsigned)(unsigned char)blob[0]);
  }
  return blob;
}

/**
 * @brief 寫入指定位元組數量的 JMP 轉址修補，多餘長度補 0x90 NOP。
 * @param src 原始位址
 * @param dst 跳轉目標位址
 * @param nbytes 欲修補的總位元組數 (至少 5 位元組)
 */
static void PatchJmpN(void *src, void *dst, size_t nbytes) {
  if (nbytes < 5) {
    return;
  }
  // 暫時解除頁面保護，寫入 x86 E9 relative JMP 並以 NOP 填滿覆蓋區。
  DWORD old = 0;
  VirtualProtect(src, nbytes, PAGE_EXECUTE_READWRITE, &old);
  BYTE *p = static_cast<BYTE *>(src);
  p[0] = 0xE9;
  *reinterpret_cast<DWORD *>(p + 1) =
      (DWORD)((uintptr_t)dst - (uintptr_t)src - 5);
  for (size_t i = 5; i < nbytes; i++) {
    p[i] = 0x90;
  }
  // 還原頁面保護並清除指令快取，確保 CPU 立即看到新指令。
  VirtualProtect(src, nbytes, old, &old);
  FlushInstructionCache(GetCurrentProcess(), src, nbytes);
}

/**
 * @brief 商店狀態指標 Hook 跳板 (Trampoline) 函式。
 */
// 各跳板保留原生流程需要的暫存器／堆疊形狀，再跳回固定續接位址。
__declspec(naked) void Tramp_PrivateShopStatusPtr() {
  __asm {
    movzx eax, byte ptr [ebp - 0x619]
    push eax
    lea ecx, [ebp - 0x410]
    push ecx
    call PrivateShopPickStatus
    add esp, 8
    push eax
    push 0x5423E2
    ret
  }
}

/**
 * @brief 商店 Tip 寬度計算 Hook 跳板 (Trampoline) 函式。
 */
__declspec(naked) void Tramp_PrivateShopTipWidth() {
  __asm {
    mov eax, dword ptr [ebp - 0x168]
    mov ecx, dword ptr [eax + 0x4C]
    mov edx, dword ptr [eax + 0x58]
    mov ecx, dword ptr [edx + ecx * 4]
    push ecx
    push ecx
    mov eax, dword ptr [ecx + 0xC]
    push eax
    call PrivateShopTipWidth
    add esp, 8
    mov dword ptr [ebp - 0x38], eax
    call PrivateShopTipHeight
    add esp, 4
    mov dword ptr [ebp - 0x34], eax
    push 0x59609E
    ret
  }
}

/**
 * @brief 商店複製格式 Hook 跳板 (Trampoline) 函式。
 */
__declspec(naked) void Tramp_PrivateShopCloneFmt() {
  __asm {
    mov dl, byte ptr [ecx + 0xB0]
    mov byte ptr [eax + 0xB0], dl
    push eax
    push ecx
    push ecx
    push eax
    call PrivateShopCopyBagFmt
    add esp, 8
    pop ecx
    pop eax
    push 0x595742
    ret
  }
}

/**
 * @brief 商店 Tip 繪製 Hook 跳板 (Trampoline) 函式。
 */
__declspec(naked) void Tramp_PrivateShopTipDraw() {
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
    call PrivateShopTipDrawFmt
    add esp, 16
    push 0x5966A0
    ret
  }
}

/**
 * @brief 安裝商店狀態 Hook 函式。
 */
void InstallPrivateShopStatusHook() {
  // 實驗：商店列表第一行被砍，整組 JMP 先不打。要恢復把這段 return 拿掉即可。
  launcherdll_hook_log("[ShStatus] install skipped (experiment)");
  return;

  // 各 Hook 目標位址：Blob、Tooltip 寬高、Tooltip 繪製與商品克隆。
  BYTE *pBlob = reinterpret_cast<BYTE *>(0x5423DD);
  BYTE *pWidth = reinterpret_cast<BYTE *>(0x596053);
  BYTE *pDraw = reinterpret_cast<BYTE *>(0x596573);
  BYTE *pClone = reinterpret_cast<BYTE *>(0x595736);
  // 先比對原生機器碼特徵，版本不符時跳過修補，避免錯位寫入。
  static const BYTE kShopPushStr[5] = {0x68, 0x37, 0x42, 0x8D, 0x00};
  static const BYTE kShopWidth[7] = {0xC7, 0x45, 0xC8, 0x8E, 0x00, 0x00, 0x00};
  static const BYTE kShopDraw[10] = {0x83, 0x7D, 0xD0, 0x00, 0x0F, 0x84,
                                     0x23, 0x01, 0x00, 0x00};
  static const BYTE kShopClone[12] = {0x8A, 0x91, 0xB0, 0x00, 0x00, 0x00,
                                      0x88, 0x90, 0xB0, 0x00, 0x00, 0x00};

  // 依序嘗試 Blob、克隆、Tooltip 寬度與繪製四條路徑，彼此獨立記錄結果。
  int blobOk = 0;
  if (memcmp(pBlob, kShopPushStr, sizeof(kShopPushStr)) == 0) {
    PatchJmpN(pBlob, reinterpret_cast<void *>(Tramp_PrivateShopStatusPtr), 5);
    blobOk = 1;
  } else {
    launcherdll_hook_log("[ShStatus] 5423DD mismatch, skip blob");
  }

  int cloneOk = 0;
  if (memcmp(pClone, kShopClone, sizeof(kShopClone)) == 0) {
    PatchJmpN(pClone, reinterpret_cast<void *>(Tramp_PrivateShopCloneFmt),
              sizeof(kShopClone));
    cloneOk = 1;
  } else {
    launcherdll_hook_log("[ShStatus] 595736 clone mismatch, skip");
  }

  int widthOk = 0;
  int drawOk = 0;
  if (memcmp(pWidth, kShopWidth, sizeof(kShopWidth)) == 0) {
    PatchJmpN(pWidth, reinterpret_cast<void *>(Tramp_PrivateShopTipWidth),
              sizeof(kShopWidth));
    widthOk = 1;
  } else {
    launcherdll_hook_log("[ShStatus] 596053 width mismatch, skip");
  }
  if (memcmp(pDraw, kShopDraw, sizeof(kShopDraw)) == 0) {
    PatchJmpN(pDraw, reinterpret_cast<void *>(Tramp_PrivateShopTipDraw),
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
