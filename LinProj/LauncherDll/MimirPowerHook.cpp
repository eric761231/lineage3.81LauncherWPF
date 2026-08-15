// MimirPowerHook.cpp: 攔截偽裝成 S_OPCODE_HIRESOLDIERLIST(132) 的密米爾之泉封包。
//
// 舊版（已刪除，見 git cf4263b/a5775ab）用全新自訂 opcode 189/195，因為伺服器端對
// 封包 opcode 有白名單檢查、未知 opcode 會被擋，這次改偽裝成既有原生封包通道。
// 舊版還有一個從沒查完的懷疑斷線 bug：只看單次 recv() 開頭幾個 byte 判斷 opcode，
// TCP 是位元組流、封包不保證切在 recv() 邊界上，容易誤判到別的封包中段。這裡改用
// 跟 DisconnectHook.cpp 的 FeedRecvByte/g_pktBuf 同一套（已驗證安全）長度前綴封包
// 重組邏輯，只在完整、對齊過的封包上判斷，不會再誤吃中段資料。
//
// 跟 DisconnectHook_InspectRecvPlain 不同的是：這裡直接操作 my_recv 的真實
// buf/ret，因為命中時要把偽裝封包整段從緩衝區拿掉，不讓原生 ProcessPacket 看到、
// 跳出原生傭兵清單 UI。
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "MimirPowerHook.h"
#include "MimirPowerOverlay.h"
#include "ShareMemory.h"

extern SHARE_INFO ShareInfo;
extern int _xorByte;
extern bool inited;

namespace {

// 真正的 S_OPCODE_HIRESOLDIERLIST 欄位是
// [len:2][opcode:1][objId:4(D)][castlemoney:4(D)][count:2(H)] + per-entry。
// 密米爾偽裝版本改用自己的 S_HireSoldierList(objId, powerList) 建構子，欄位配置：
//   [len:2][opcode:1]
//   [objId:4(D)]                  玩家自己的編號，client 不解讀意義，收到什麼原封
//                                  不動在 SendChoice 送回去
//   [sentinel:4(D)]                固定值，借用真實封包 castlemoney 的位置，用來讓
//                                  client 分辨「這是密米爾封包」，真的傭兵清單這個
//                                  位置放的是城堡資金，幾乎不會剛好等於這個保留值
//   repeat MIMIR_OPTION_COUNT(=3) 次:
//     [sortId:2(H)]                 client 用不到，只是跟著真實欄位形狀跳過
//     [iconId:4(D)]                 圖示編號
//     [name:C字串+0x00]             選項名稱
//     [desc:C字串+0x00]             能力敘述
//   // 詳情卡預設（尚未點擊任何選項前）顯示的資料，格式跟上面單筆一樣：
//   [sortId:2(H)] [iconId:4(D)] [name:C字串+0x00] [desc:C字串+0x00]
//   [countdownSeconds:4(D)]        「選擇將於 hh:mm:ss 後更新」的起始秒數
constexpr BYTE HIRESOLDIERLIST_OPCODE = 132;

// 真的傭兵清單封包這個位置放城堡資金，不會剛好等於這個保留值，用來安全區分兩者。
constexpr DWORD MIMIR_SENTINEL = 0xDEADBEEFu;

// 真實 C_HireSoldier 的 wire opcode 數值，來自 Java 端 OpcodesClient：
// public static final int C_OPCODE_HIRESOLDIER = 31;
constexpr BYTE C_HIRESOLDIER_OPCODE = 31;

// C_HireSoldier payload 欄位（依 Java 端 C_HireSoldier.start() 原始碼確認）：
//   objid:4(D) mercenaryTypeCount:2(H)
//   repeat mercenaryTypeCount 次: id:2(H) count:2(H) price:2(H)
// 偽裝密米爾選擇時：objid=sentinel、mercenaryTypeCount=1、單筆 id=選到的
// index、count/price 填 0（Java 端會在真正扣款邏輯前就依 sentinel 分流掉，
// 不會用到這兩個欄位）。

constexpr int MAX_PKT_LEN = 8192;

BYTE g_pktBuf[MAX_PKT_LEN];
int g_pktHave = 0;
int g_pktNeed = -1;

SOCKET g_gameSocket = INVALID_SOCKET;

void NetLog(const char *fmt, ...) {
  char exePath[MAX_PATH] = {0};
  char logPath[MAX_PATH] = "./launcherdll_net.log";
  if (GetModuleFileNameA(NULL, exePath, MAX_PATH) > 0) {
    for (int i = (int)strlen(exePath) - 1; i >= 0; i--) {
      if (exePath[i] == '\\' || exePath[i] == '/') {
        exePath[i] = '\0';
        break;
      }
    }
    sprintf_s(logPath, "%s\\launcherdll_net.log", exePath);
  }
  FILE *fp = NULL;
  if (fopen_s(&fp, logPath, "a+") != 0 || fp == NULL)
    return;
  SYSTEMTIME st;
  GetLocalTime(&st);
  char msg[1024] = {0};
  va_list args;
  va_start(args, fmt);
  vsprintf_s(msg, fmt, args);
  va_end(args);
  fprintf(fp, "[%04d-%02d-%02d %02d:%02d:%02d.%03d][PID=%u][TID=%u] %s\n",
          st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
          st.wMilliseconds, (unsigned)GetCurrentProcessId(),
          (unsigned)GetCurrentThreadId(), msg);
  fflush(fp);
  fclose(fp);
}

// 只支援 ShareInfo.randenc==false 的固定 XOR 加密模式（跟舊版一樣的限制）；
// randenc（逐 byte LCG）模式沒有對應的接收端解密狀態，遇到就放棄攔截。
bool CanDecrypt() {
  return ShareInfo.encrypt && !ShareInfo.randenc && inited;
}

void XorDecodeInto(BYTE *dst, const BYTE *src, int len) {
  for (int i = 0; i < len; i++)
    dst[i] = (BYTE)(src[i] ^ (BYTE)_xorByte);
}

// 讀一筆 [sortId:2(H)][iconId:4(D)][name:C字串+0x00][desc:C字串+0x00]，pos 由呼叫端
// 傳入/更新。成功回傳 true 並填好 out；資料不夠/字串沒有結尾 NUL 就回傳 false。
bool ReadOneEntry(const BYTE *decoded, int decodeLen, int *pos, MimirOption *out) {
  if (*pos + (int)sizeof(WORD) > decodeLen)
    return false;
  *pos += sizeof(WORD); // sortId，密米爾用不到，跳過

  if (*pos + (int)sizeof(DWORD) > decodeLen)
    return false;
  memcpy(&out->iconId, decoded + *pos, sizeof(DWORD));
  *pos += sizeof(DWORD);

  auto readString = [&](char *dst, size_t dstSize) -> bool {
    int start = *pos;
    while (*pos < decodeLen && decoded[*pos] != 0)
      (*pos)++;
    if (*pos >= decodeLen)
      return false; // 沒有結尾 NUL
    int strLen = *pos - start;
    if (strLen >= (int)dstSize)
      strLen = (int)dstSize - 1;
    memcpy(dst, decoded + start, strLen);
    dst[strLen] = 0;
    (*pos)++; // 跳過 NUL
    return true;
  };

  if (!readString(out->name, sizeof(out->name)))
    return false;
  if (!readString(out->desc, sizeof(out->desc)))
    return false;
  return true;
}

// pkt: 完整一包 wire bytes（[len:2] 明文 + payload，payload 視 CanDecrypt() 而定
// 可能是密文）。回傳 true 代表這包已被密米爾邏輯吃掉，呼叫端不應該把它複製進
// outBuf（也就是不讓原生 client 看到）。
bool TryHandleCompletePacket(const BYTE *pkt, int len) {
  // 最短合法長度：opcode(1)+objId(4)+sentinel(4) = 9，加上 2-byte wire len header。
  if (len < 11 || !CanDecrypt())
    return false;

  BYTE decoded[MAX_PKT_LEN];
  int decodeLen = (len < MAX_PKT_LEN) ? len : MAX_PKT_LEN;
  XorDecodeInto(decoded, pkt, decodeLen);

  if (decoded[2] != HIRESOLDIERLIST_OPCODE)
    return false;

  DWORD objid;
  memcpy(&objid, decoded + 3, sizeof(DWORD));

  DWORD sentinel;
  memcpy(&sentinel, decoded + 7, sizeof(DWORD));
  if (sentinel != MIMIR_SENTINEL)
    return false; // 真的傭兵清單封包（這個位置是城堡資金），放行給原生處理

  int pos = 11;
  MimirOption options[MIMIR_OPTION_COUNT] = {};
  bool ok = true;
  for (int i = 0; i < MIMIR_OPTION_COUNT && ok; i++)
    ok = ReadOneEntry(decoded, decodeLen, &pos, &options[i]);

  MimirOption defaultDetail = {};
  if (ok)
    ok = ReadOneEntry(decoded, decodeLen, &pos, &defaultDetail);

  DWORD countdownSeconds = 0;
  if (ok && pos + (int)sizeof(DWORD) <= decodeLen) {
    memcpy(&countdownSeconds, decoded + pos, sizeof(DWORD));
    pos += sizeof(DWORD);
  } else {
    ok = false;
  }

  if (!ok) {
    NetLog("[mimir] sentinel matched but payload malformed (len=%d, stopped at pos=%d)",
           len, pos);
    return true; // 格式異常，還是吃掉避免流入原生解析器造成錯位
  }

  NetLog("[mimir] intercepted opcode132 sentinel packet, objid=0x%08X countdown=%us",
         objid, countdownSeconds);
  MimirPowerOverlay_Show(objid, options, defaultDetail, countdownSeconds);
  return true;
}

} // namespace

void MimirPowerHook_SetSocket(SOCKET s) { g_gameSocket = s; }

void MimirPowerHook_ResetSession() {
  g_pktHave = 0;
  g_pktNeed = -1;
}

void MimirPowerHook_OnRecv(unsigned char *buf, int &ret) {
  if (!buf || ret <= 0)
    return;

  static BYTE outBuf[MAX_PKT_LEN];
  int outLen = 0;
  bool anyConsumed = false;

  for (int byteIdx = 0; byteIdx < ret; byteIdx++) {
    if (g_pktHave >= MAX_PKT_LEN) {
      // 異常大包，防禦性重置（跟 DisconnectHook 的 resync 邏輯一致）。
      g_pktHave = 0;
      g_pktNeed = -1;
    }
    g_pktBuf[g_pktHave++] = buf[byteIdx];

    for (;;) {
      if (g_pktNeed < 0) {
        if (g_pktHave < 2)
          break;
        g_pktNeed = (int)g_pktBuf[0] | ((int)g_pktBuf[1] << 8);
        if (g_pktNeed < 3 || g_pktNeed > MAX_PKT_LEN) {
          // 壞的長度頭：跟 DisconnectHook 一樣，丟掉第一個 byte 重新對齊，
          // 而不是整個緩衝區清空（避免真的吃掉半包好資料）。
          if (outLen < MAX_PKT_LEN)
            outBuf[outLen++] = g_pktBuf[0];
          memmove(g_pktBuf, g_pktBuf + 1, (size_t)(g_pktHave - 1));
          g_pktHave--;
          g_pktNeed = -1;
          continue;
        }
      }
      if (g_pktHave < g_pktNeed)
        break;

      bool consumed = TryHandleCompletePacket(g_pktBuf, g_pktNeed);
      if (consumed) {
        anyConsumed = true;
      } else if (outLen + g_pktNeed <= MAX_PKT_LEN) {
        memcpy(outBuf + outLen, g_pktBuf, (size_t)g_pktNeed);
        outLen += g_pktNeed;
      }
      int left = g_pktHave - g_pktNeed;
      if (left > 0)
        memmove(g_pktBuf, g_pktBuf + g_pktNeed, (size_t)left);
      g_pktHave = left;
      g_pktNeed = -1;
    }
  }

  if (anyConsumed) {
    memcpy(buf, outBuf, (size_t)outLen);
    ret = outLen;
  }
}

void MimirPowerHook_SendChoice(DWORD objid, BYTE index) {
  if (g_gameSocket == INVALID_SOCKET) {
    NetLog("[mimir] SendChoice: no game socket, skip objid=0x%08X index=%u",
           objid, (unsigned)index);
    return;
  }
  // wire: [len:2][opcode:1][objid:4][mercenaryTypeCount:2=1][id:2][count:2=0][price:2=0]
  BYTE payload[15];
  WORD wireLen = (WORD)sizeof(payload);
  memcpy(payload, &wireLen, 2);
  payload[2] = C_HIRESOLDIER_OPCODE;
  memcpy(payload + 3, &objid, sizeof(DWORD));
  WORD typeCount = 1;
  memcpy(payload + 7, &typeCount, 2);
  WORD id = (WORD)index;
  WORD zero = 0;
  memcpy(payload + 9, &id, 2);
  memcpy(payload + 11, &zero, 2); // count
  memcpy(payload + 13, &zero, 2); // price

  // 呼叫 send()（不是 real_send）：讓它照樣被 Detours 導到 my_send，自動套用跟
  // 真實封包一致的 C2S 加密與既有 log，對伺服器來說跟真的僱傭兵請求長得一樣。
  int sent = send(g_gameSocket, (const char *)payload, (int)sizeof(payload), 0);
  NetLog("[mimir] SendChoice objid=0x%08X index=%u sent=%d bytes=%d", objid,
         (unsigned)index, sent, (int)sizeof(payload));
}
