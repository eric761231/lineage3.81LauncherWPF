#include "stdafx.h"
#include "PrivateShopStatus.h"
#include "WarehouseStatusHook.h"
#include "LauncherDll.h"
#include <string.h>

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
  // 寬度取道具名稱與說明文字最長行的較大值；控制碼不應佔用可視寬度。
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
      // 每個 offset 指向一行起點；最後一行的結尾使用整個格式字串長度。
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
  // 這個函式同時被個人商店與倉庫使用，因此只處理格式資料，不安裝任何 Hook。
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
  // 2026-09-14：暫時診斷 log，驗證 g_fmtExtraNl（WarehouseStatusHook.cpp）是否
  // 會在商店 Clone 呼叫 SplitFmt 當下意外殘留非 0（懷疑是之前「商店列表第一行
  // 被截斷」的可能根因之一，見 docs/PrivateShopStatus_開發須知.md）。確認完
  // 這個旗標的實際行為後，這段 log 要拿掉，不要留在正式路徑。
  static int s_flagCheckLogs = 0;
  if (s_flagCheckLogs < 8) {
    s_flagCheckLogs++;
    // 把 src 前段內容轉成可視化字串（不可見字元印成 \xNN），確認第一行實際
    // 內容是不是空白/純控制碼，藉此判斷空白行是不是來自來源字串本身。
    char esc[200] = {0};
    int ei = 0;
    for (int si = 0; si < 48 && src[si] && ei < (int)sizeof(esc) - 5; si++) {
      const unsigned char c = static_cast<unsigned char>(src[si]);
      if (c >= 0x20 && c < 0x7F) {
        esc[ei++] = static_cast<char>(c);
      } else {
        ei += sprintf_s(esc + ei, sizeof(esc) - ei, "\\x%02X", c);
      }
    }
    launcherdll_hook_log(
        "[Pss][diag] PrivateShopCopyItemFmtFromBag: g_fmtExtraNl=%d nsrc=%d "
        "src_head=\"%s\" before SplitFmt (n=%d)",
        FmtExtraNlGet(), nsrc, esc, s_flagCheckLogs);
  }
  // SplitFmt 會回傳新的格式字串；dummyOff 只用來接收切割結果，真正的行偏移量
  // 由 ApplyListFmtOffBag 依來源內容填入目標結構。
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
  // 只在目的地還沒有有效說明資料時才複製——對齊 WarehouseStatusHook.cpp
  // 的 Hook_AttachStatus「先讓原生流程跑完，line count 還是無效才 fallback
  // 複製背包資料」寫法，而不是每次都無條件覆蓋。原生某些情況下可能已經
  // 正確帶好 +0x14/+0x18，這裡不需要、也不應該蓋掉。
  if (!clone) {
    return;
  }
  const int existing = *reinterpret_cast<int *>(static_cast<BYTE *>(clone) + 0x14);
  // 2026-09-14：驗證「existing 已經是 1~32 就直接 return 不 fallback 複製」
  // 這個判斷會不會誤判——懷疑掛賣當下 clone 的 +0x14 本來就非 0，導致這裡
  // 提早 return，複製流程整個沒跑到。確認完就拿掉，不要留在正式路徑。
  static int s_bagFmtLogs = 0;
  if (s_bagFmtLogs < 8) {
    s_bagFmtLogs++;
    // 2026-09-14：CE 中斷點在保護殼下打不進去，改用程式碼內 log 直接查
    // clone+0x10（附加狀態指標）的值，確認空白行是不是這個欄位殘留非 0
    // 造成的（原生 Tooltip 高度/繪製會依這個欄位多保留一行）。
    void *extra = *reinterpret_cast<void **>(static_cast<BYTE *>(clone) + 0x10);
    char extraEsc[80] = {0};
    if (extra) {
      // extra 是原生欄位讀出來的原始指標，內容格式未知，直接讀取有機率
      // 讀到無效記憶體，用 SEH 包起來避免弄壞玩家正在玩的遊戲行程。
      __try {
        const char *es = reinterpret_cast<const char *>(extra);
        int ei = 0;
        for (int si = 0; si < 24 && es[si] && ei < (int)sizeof(extraEsc) - 5; si++) {
          const unsigned char c = static_cast<unsigned char>(es[si]);
          if (c >= 0x20 && c < 0x7F) {
            extraEsc[ei++] = static_cast<char>(c);
          } else {
            ei += sprintf_s(extraEsc + ei, sizeof(extraEsc) - ei, "\\x%02X", c);
          }
        }
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        strncpy_s(extraEsc, sizeof(extraEsc), "<unreadable>", _TRUNCATE);
      }
    }
    launcherdll_hook_log(
        "[Pss][diag] PrivateShopCopyBagFmt: existing=%d clone=%p bag=%p "
        "extra_ptr=%p extra_head=\"%s\" (n=%d)",
        existing, clone, bag, extra, extraEsc, s_bagFmtLogs);
  }
  if (existing > 0 && existing <= 32) {
    return;
  }
  // 跳板只需要執行複製；正常成功不逐次記錄 Log，避免掛攤操作刷屏。
  const int n = PrivateShopCopyItemFmtFromBag(clone, bag);
  (void)n;
}

/**
 * @brief 繪製商店提示框（Tooltip）中的格式化文字。
 * @param item 道具結構指標
 * @param x 繪製起點 X 座標
 * @param y0 繪製起點 Y 座標
 * @param color 文字顏色
 */
extern "C" void __cdecl ShopTipDrawFmt(void *item, int x, int y0, int color) {
  // 這是 Tooltip 的高頻繪製路徑，只負責逐行呼叫原生繪字函式，不輸出逐次 Log。
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
  // 2026-09-14：暫時診斷 log，確認掛賣時這個 Blob 路徑真的有被呼叫到。
  // 確認完就拿掉，不要留在正式路徑（這是高頻路徑，正常不應該逐次記錄）。
  static int s_pickLogs = 0;
  if (s_pickLogs < 8) {
    s_pickLogs++;
    launcherdll_hook_log("[Pss][diag] PrivateShopPickStatus: len=%u blob=%p (n=%d)",
                         len, blob, s_pickLogs);
  }
  if (!len) {
    FmtExtraNlSet(0);
    return 0;
  }
  // 有狀態資料時開啟額外換行，讓後續 Tooltip 能保留狀態列。
  FmtExtraNlSet(1);
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
    call ShopTipDrawFmt
    add esp, 16
    push 0x5966A0
    ret
  }
}

/**
 * @brief 安裝商店狀態 Hook 函式。
 */
void InstallPrivateShopStatusHook() {
  // 2026-09-14：依 docs/PrivateShopStatus_開發須知.md 的「逐一驗證四條路徑，
  // 不要一次取消所有防護」原則。Clone 這條已驗證過是「保險機制」，掛收能顯示
  // 資料其實是靠 WarehouseStatusHook.cpp 的 Hook_AttachStatus fallback，跟
  // Clone 無關；而掛賣的道具完全沒有觸發任何格式複製（連 Hook_AttachStatus
  // 都沒進），證實掛賣走的是另一條原生流程。這次額外重新啟用「Blob」這一條
  // （位址 0x5423DD，機器碼特徵 kShopPushStr——push 常數字串，對應「上架/
  // 掛賣」情境），驗證是不是掛賣真正需要的路徑。Tooltip 寬高／繪製這兩條
  // 維持停用，之前「商店列表第一行被截斷」是四條一起開造成的，還沒驗證是
  // 不是這兩條的問題，先不要一起打開。
  BYTE *pBlob = reinterpret_cast<BYTE *>(0x5423DD);
  BYTE *pWidth = reinterpret_cast<BYTE *>(0x596053);
  BYTE *pDraw = reinterpret_cast<BYTE *>(0x596573);
  BYTE *pClone = reinterpret_cast<BYTE *>(0x595736);
  (void)pWidth;
  (void)pDraw;
  // 先比對原生機器碼特徵，版本不符時跳過修補，避免錯位寫入。
  static const BYTE kShopPushStr[5] = {0x68, 0x37, 0x42, 0x8D, 0x00};
  static const BYTE kShopWidth[7] = {0xC7, 0x45, 0xC8, 0x8E, 0x00, 0x00, 0x00};
  static const BYTE kShopDraw[10] = {0x83, 0x7D, 0xD0, 0x00, 0x0F, 0x84,
                                     0x23, 0x01, 0x00, 0x00};
  static const BYTE kShopClone[12] = {0x8A, 0x91, 0xB0, 0x00, 0x00, 0x00,
                                      0x88, 0x90, 0xB0, 0x00, 0x00, 0x00};
  (void)kShopWidth;
  (void)kShopDraw;

  int cloneOk = 0;
  if (memcmp(pClone, kShopClone, sizeof(kShopClone)) == 0) {
    PatchJmpN(pClone, reinterpret_cast<void *>(Tramp_PrivateShopCloneFmt),
              sizeof(kShopClone));
    cloneOk = 1;
  }

  int blobOk = 0;
  if (memcmp(pBlob, kShopPushStr, sizeof(kShopPushStr)) == 0) {
    PatchJmpN(pBlob, reinterpret_cast<void *>(Tramp_PrivateShopStatusPtr), 5);
    blobOk = 1;
  }

  // Width／Draw 暫時不裝：
  // int widthOk = 0, drawOk = 0;
  // if (memcmp(pWidth, kShopWidth, sizeof(kShopWidth)) == 0) {
  //   PatchJmpN(pWidth, reinterpret_cast<void *>(Tramp_PrivateShopTipWidth),
  //             sizeof(kShopWidth));
  //   widthOk = 1;
  // }
  // if (memcmp(pDraw, kShopDraw, sizeof(kShopDraw)) == 0) {
  //   PatchJmpN(pDraw, reinterpret_cast<void *>(Tramp_PrivateShopTipDraw),
  //             sizeof(kShopDraw));
  //   drawOk = 1;
  // }

  // 只保留安裝結果摘要；這次裝 clone+blob，width/draw 固定顯示為停用中。
  launcherdll_hook_log(
      "[Pss][Install] PrivateShopStatus hook result clone=%d blob=%d "
      "(width/draw disabled, see docs/PrivateShopStatus_開發須知.md)",
      cloneOk, blobOk);
}
