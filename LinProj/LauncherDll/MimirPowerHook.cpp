// MimirPowerHook.cpp: 密米爾之泉 in-process hook。
//
// S→C 封包偽裝走 PacketBox（S_OPCODE_PACKETBOX = 250 / 0xFA）子類型欄位。
//
// 關鍵發現（這次用 CE 對真實送出的 S_MapUITeleport 封包直接抓斷點才確認）：
// 0x0053939A 這個共用分派點的 EAX，**不是**外層 opcode 本身（250），而是已經被
// 提前抽出來的 PacketBox 子類型值（例如 S_MapUITeleport 用的 TOWN_TELEPORT=176/
// 0xB0，實測 EAX 就是 000000B0，ESI 才是 000000FA=250）。opcode 250 本身的辨識
// 跟子類型抽取是在別的地方處理完，才帶著子類型當 EAX 跳回這個分派點。
// 而且這個分派點的有效範圍只有 0~183（往上數會看到 cmp [ebp-0x60F4],0xB7 (183);
// ja <另外處理>），超過 183 的值永遠不會讓 EAX 在這裡等於它。這解釋了之前 opcode
// 200(0xC8)、250(0xFA) 本身、還有我們原本選的子類型 0xF5(245，超過 183 而且還
// 撞上真實的 S_OPCODE_IDENTIFYDESC=245) 全部都攔不到的原因。
//
// 所以正確做法：直接拿 EAX 跟我們自己選的子類型值比對（不用先判斷是不是 250），
// 選一個 0~183 範圍內、Java 端所有 S_OPCODE_* 常數跟所有 PacketBox 子類型都沒人
// 用的值——目前選 0x10(16)。[ebp+8] 在這個分派點已經指向子類型 byte 之後的內容
// （子類型本身已經被讀進 EAX，不在 [ebp+8] 指向的資料裡），所以我們自己封包裡
// [opcode:250][subtype:16] 這 2 個 byte 都不會出現在 pktData 裡，直接從 iconId
// 開始讀。
//
// 之所以不能像 DisconnectHook 那樣在 my_recv 的 socket 層攔截、自己 XOR 解密，
// 是因為這個 client 的 S→C payload 真的有加密（Lineage Encryption，有狀態的
// Blowfish 衍生加密，client 端沒辦法自己重算，這點這次除錯過程已經用
// launcherdll_net_log 的 rawHex 實測驗證過：完全是亂碼，XOR 解不出東西）。
//
// 改成的作法：不解密封包本身，等遊戲自己解密、準備分派 opcode 的那個瞬間，直接
// 從遊戲的記憶體裡讀已經解密好的明文。原生 ProcessPacket 的分派邏輯長這樣（跟
// DisconnectHook.cpp 找到的 ja@0x0053938E 是同一段）：
//   0053938E: ja      <cave>                  ; 分派值超出 0~183 另外處理
//   00539394: mov     eax,[ebp-0x60F4]        ; eax = PacketBox 子類型
//   0053939A: jmp     dword ptr [eax*4+0x5415B4]  ; 跳轉表，這裡是我們要蓋掉的地方
//
// 我們的 hook 蓋掉 0053939A 那個 jmp（7 bytes: FF 24 85 <4-byte table base>），
// 換成跳到我們自己的 cave：eax==16 才進一步解析、顯示我們的畫面、直接跳到共用
// 收尾（0x00541596，DisconnectHook.cpp 也是用這個位址），原生對應這個子類型的
// 處理邏輯完全不會執行到；不是的話就照原本的 jmp dword ptr[eax*4+0x5415B4] 正常
// 跳走，不影響其他任何功能。
//
// 位址穩定性：0x0053938E 這個位置本身（連同往下數固定的 mov/jmp 指令排列）已經
// 被 DisconnectHook.cpp 長期使用、證實穩定；會隨著每次啟動變動的是「跳轉表裡面
// 存的內容」（0x5415B4 + 值*4 這些位置存的目的地位址），不是分派指令本身的位置，
// 所以我們蓋 0053939A 這個固定位址是安全的。
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "MimirPowerHook.h"
#include "LineageEncryption.h"
#include "MimirPowerOverlay.h"

#pragma comment(lib, "detours.lib")

namespace {

// ja@0x0053938E（DisconnectHook.cpp 同一個位置）往下數：6 bytes 的 ja 本身 +
// 6 bytes 的 mov eax,[ebp-0x60f4]，第 12 個 byte 開始就是 jmp dword ptr[...]。
constexpr DWORD JMP_PATCH_ADDR = 0x0053939A;
constexpr DWORD JUMP_TABLE_BASE = 0x005415B4;
constexpr DWORD DISPATCH_EPILOGUE_ADDR = 0x00541596;

// 密米爾封包偽裝走 PacketBox（S_OPCODE_PACKETBOX=250）的子類型欄位。這裡直接
// 比對的是子類型值本身，不是 250——CE 實測證實 0x0053939A 這個分派點的 EAX
// 就是子類型值，250 這個外層 opcode 從沒出現在這個位置過（見檔案開頭說明）。
// 0x10(16)：掃過 Java 端所有 S_OPCODE_*/PacketBox 子類型都沒人用，實測也確認
// 真的會被攔截、跳出我們自己的 MimirPowerOverlay 視窗（一度誤以為撞到原生
// 「日常活躍」面板，後來確認那個標題文字其實是 MimirPowerOverlay.cpp 自己的
// 內建預設標題，不是原生功能——攔截本身沒問題，問題在畫面渲染邏輯）。
constexpr BYTE MIMIR_SENTINEL_0 = 0x10;

// 真實 C_CheckPK 的 wire opcode，Java 端 OpcodesClient：
// public static final int C_OPCODE_CHECKPK = 51;
constexpr BYTE C_CHECKPK_OPCODE = 51;

constexpr int MAX_SCAN_LEN = 4096; // 防禦性上限，配合 SEH 避免真的讀到不該讀的記憶體

SOCKET g_gameSocket = INVALID_SOCKET;
DWORD g_mimirConsumedFlag = 0;

// -1 = 沒有待送出的選擇；否則是 0~255 的合法 index（BYTE 全值域都合法，用另一個
// LONG 存 -1 當「沒有」的哨兵值，避免跟合法 index 撞在一起）。MimirPowerHook_
// SendChoice（overlay 執行緒）寫入，MimirPowerHook_PumpPendingChoice（my_recv，
// 遊戲網路執行緒）讀取並清掉，用 InterlockedExchange 保證單一寫入者/讀取者之間
// 不會撕裂。
volatile LONG g_pendingChoiceIndex = -1;

void NetLog(const char *fmt, ...) {
  // 密米爾之泉功能已經確認穩定運作，暫時關掉這個檔案的 log（跟其他問題的除錯過程
  // 混在一起容易誤判），要恢復就把這行 return 拿掉。
  return;
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

// 讀一筆 [iconId:4(D)][name:C字串+0x00][desc:C字串+0x00]，pos 由呼叫端傳入/更新。
// 成功回傳 true 並填好 out；資料不夠/字串沒有結尾 NUL 就回傳 false。
bool ReadOneEntry(const BYTE *data, int maxLen, int *pos, MimirOption *out) {
  if (*pos + (int)sizeof(DWORD) > maxLen)
    return false;
  memcpy(&out->iconId, data + *pos, sizeof(DWORD));
  *pos += sizeof(DWORD);

  auto readString = [&](char *dst, size_t dstSize) -> bool {
    int start = *pos;
    while (*pos < maxLen && data[*pos] != 0)
      (*pos)++;
    if (*pos >= maxLen)
      return false; // 沒有結尾 NUL
    int strLen = *pos - start;
    if (strLen >= (int)dstSize)
      strLen = (int)dstSize - 1;
    memcpy(dst, data + start, strLen);
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

// pktData: 遊戲已經解密好的明文，從 opcode(250) 後面那個 byte 開始（也就是
// PacketBox 子類型欄位，我們拿來當 sentinel 用）。回傳 1 代表這包被密米爾邏輯
// 吃掉了（cave 要直接跳到收尾、不讓原生 PacketBox 子類型分派邏輯執行到）；回傳
// 0 代表不是我們的封包（真的 PacketBox 封包），cave 要照原本的方式正常跳轉表
// 分派。
extern "C" DWORD __cdecl OnMimirDispatch(const BYTE *pktData) {
  __try {
    // cave 呼叫進來就代表 EAX 已經等於 MIMIR_SENTINEL_0 了（比對在 cave 的
    // __asm 裡直接用 cmp eax 做掉，這裡不用也不能再比對一次——子類型 byte 本身
    // 已經被讀進 EAX，不在 pktData 指向的內容裡，pktData 直接是我們的資料
    // [iconId]... 開始）。
    int pos = 0;
    MimirOption options[MIMIR_OPTION_COUNT] = {};
    bool ok = true;
    for (int i = 0; i < MIMIR_OPTION_COUNT && ok; i++)
      ok = ReadOneEntry(pktData, MAX_SCAN_LEN, &pos, &options[i]);

    MimirOption defaultDetail = {};
    if (ok)
      ok = ReadOneEntry(pktData, MAX_SCAN_LEN, &pos, &defaultDetail);

    DWORD countdownSeconds = 0;
    if (ok && pos + (int)sizeof(DWORD) <= MAX_SCAN_LEN) {
      memcpy(&countdownSeconds, pktData + pos, sizeof(DWORD));
    } else {
      ok = false;
    }

    if (!ok) {
      NetLog("[mimir] sentinel matched but payload malformed (stopped at pos=%d)", pos);
      return 1; // 格式異常，還是吃掉避免流入原生解析器造成錯位
    }

    NetLog("[mimir] intercepted PacketBox sentinel packet, countdown=%us", countdownSeconds);
    MimirPowerOverlay_Show(options, defaultDetail, countdownSeconds);
    return 1;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    NetLog("[mimir] *** EXCEPTION *** while parsing dispatch payload, passing through");
    return 0;
  }
}

// naked 函式：進來的時候 eax=分派值（opcode 或 PacketBox 子類型）、ebp 是原生
// ProcessPacket 那個巨大函式的 frame（[ebp+8]=封包資料指標）。用 jmp（不是
// call）進來，所以不用管返回位址，處理完直接跳走，不 ret。
__declspec(naked) void MimirDispatchCave() {
  // 注意：底下故意全部用寫死的十六進位立即值（0x10、0x5415B4、0x541596），不要
  // 用具名的 constexpr 常數（JUMP_TABLE_BASE/DISPATCH_EPILOGUE_ADDR）。這次實測
  // 抓到 MSVC 內嵌組譯器不會把 constexpr DWORD 內聯成立即值，而是當成「變數」
  // 處理，把 [eax*4+JUMP_TABLE_BASE] 編譯成「以我們 DLL 裡儲存該常數的記憶體
  // 位址」當 base 做索引，而不是真正的遊戲跳轉表位址 0x5415B4，算出來的目的
  // 位址完全是垃圾記憶體、跳過去就當機（CE 反組譯直接看到 jmp dword ptr[eax*4+
  // 658CB1F0] 這種指向我們自己 DLL 資料段的位址，不是 0x5415B4，實測證實）。
  //
  // 這裡直接比對 EAX == 0x10（我們的 PacketBox 子類型值），不是比對 250——見
  // 檔案開頭說明，這個分派點的 EAX 本來就是子類型值，250 這個外層 opcode 從沒
  // 出現在這裡過。
  __asm {
    cmp eax, 0x10 // MIMIR_SENTINEL_0
    jne pass_through

    pushfd
    pushad
    mov eax, [ebp + 8]
    push eax
    call OnMimirDispatch
    add esp, 4
    mov g_mimirConsumedFlag, eax
    popad
    popfd

    cmp g_mimirConsumedFlag, 0
    jne consumed

  pass_through:
    jmp dword ptr [eax * 4 + 0x5415B4] // JUMP_TABLE_BASE

  consumed:
    mov eax, 0x541596 // DISPATCH_EPILOGUE_ADDR
    jmp eax
  }
}

} // namespace

void InstallMimirPowerHook() {
  BYTE cur[7] = {};
  __try {
    memcpy(cur, (void *)JMP_PATCH_ADDR, sizeof(cur));
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    NetLog("[mimir-hook] read original bytes @0x%08X failed", JMP_PATCH_ADDR);
    return;
  }

  static const BYTE expected[7] = {0xFF, 0x24, 0x85, 0xB4, 0x15, 0x54, 0x00};
  if (memcmp(cur, expected, sizeof(expected)) != 0) {
    NetLog("[mimir-hook] unexpected original bytes @0x%08X, abort (got %02X %02X %02X %02X %02X %02X %02X)",
           JMP_PATCH_ADDR, cur[0], cur[1], cur[2], cur[3], cur[4], cur[5], cur[6]);
    return;
  }

  BYTE patch[7];
  patch[0] = 0xE9; // jmp rel32
  DWORD caveTarget = (DWORD)(uintptr_t)&MimirDispatchCave;
  DWORD callSite = JMP_PATCH_ADDR + 5;
  DWORD rel = caveTarget - callSite;
  memcpy(patch + 1, &rel, sizeof(rel));
  patch[5] = 0x90; // nop（原本 7 bytes，E9+rel32 只用 5 bytes，剩 2 bytes 補 NOP）
  patch[6] = 0x90;

  DWORD oldProt = 0;
  if (!VirtualProtect((void *)JMP_PATCH_ADDR, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProt)) {
    NetLog("[mimir-hook] VirtualProtect failed @0x%08X", JMP_PATCH_ADDR);
    return;
  }
  memcpy((void *)JMP_PATCH_ADDR, patch, sizeof(patch));
  VirtualProtect((void *)JMP_PATCH_ADDR, sizeof(patch), oldProt, &oldProt);
  FlushInstructionCache(GetCurrentProcess(), (void *)JMP_PATCH_ADDR, sizeof(patch));
  NetLog("[mimir-hook] installed @0x%08X -> cave@0x%p", JMP_PATCH_ADDR, &MimirDispatchCave);
}

void MimirPowerHook_SetSocket(SOCKET s) { g_gameSocket = s; }

void MimirPowerHook_SendChoice(BYTE index) {
  // 只記旗標、立刻回傳，不做任何加密/送出——實測直接從這個 overlay 執行緒呼叫
  // 借來的原生加密函式（0x00580640）會卡住整個執行緒（游標沒反應，最後連線逾時
  // 重置），推測是那個函式預期只被遊戲自己的網路執行緒呼叫，換一個執行緒呼叫會
  // 卡在某個只有在原本執行緒才會被滿足的條件上。真正的加密+送出移到
  // MimirPowerHook_PumpPendingChoice，由 my_recv（穩定跑在遊戲網路執行緒）呼叫。
  InterlockedExchange(&g_pendingChoiceIndex, (LONG)index);
  NetLog("[mimir] SendChoice queued index=%u (will send from network thread)",
         (unsigned)index);
}

void MimirPowerHook_PumpPendingChoice() {
  LONG index = InterlockedExchange(&g_pendingChoiceIndex, -1);
  if (index < 0)
    return; // 沒有待送出的選擇
  NetLog("[mimir] PumpPendingChoice step1 entered, index=%ld", index);

  if (g_gameSocket == INVALID_SOCKET) {
    NetLog("[mimir] PumpPendingChoice: no game socket, drop index=%ld", index);
    return;
  }
  NetLog("[mimir] PumpPendingChoice step2 socket ok");
  // wire: [len:2][opcode:1=51][index:1][pad:2]
  // Java 端 DecryptExecutor.decrypt() 把 body（扣掉 2 byte 長度欄位本身）丟給
  // Encryption._decrypt() 之前，body 長度不管封包實際欄位多少都「一定」要 >=4
  // byte——_decrypt() 一開頭就無條件存取 buf[0]..buf[3]（Blowfish 衍生的串流
  // 解密，這層跟我們 DLL 這邊的 XOR 混淆是分開兩層，不是同一個）。body 只有
  // opcode+index 共 2 byte 時，_decrypt 存取 buf[3] 直接
  // ArrayIndexOutOfBoundsException（實測 log 證實：Index 3 out of bounds for
  // length 2）。原生 C_CheckPK 的 handler 雖然不讀任何欄位，但封包本身一定也還
  // 是有補到至少 4 byte body，不然連原生 /checkpk 都送不出去。這裡補 2 byte
  // padding 湊滿最低要求，padding 值本身沒意義、Java 端 handler 也不會去讀。
  // 借來的原生加密函式（0x00580640）從沒被驗證過逐 byte 行為——它原本只被
  // wrapper 拿去對遊戲自己那個 4096-byte 靜態緩衝區操作，從沒被要求只操作剛好
  // 4 byte 的緊湊緩衝區。實測兩次都在這個呼叫「之後」看到本函式區域變數印出
  // 不可能的值（超出 BYTE 範圍），伺服器實際收到的內容卻是對的——最合理的解釋
  // 是它多寫了幾個 byte、踩到堆疊上緊鄰的東西。這裡刻意放大緩衝區、留一大段
  // 空間在 body 後面，就算真的多寫幾個 byte 也只會踩進沒人用的 padding，不會
  // 弄壞任何區域變數。實際送上線的長度還是固定 wireLen(=6)，padding 不會送出去。
  constexpr int kWireLen = 6;
  BYTE payload[64] = {0};
  WORD wireLen = (WORD)kWireLen;
  memcpy(payload, &wireLen, 2);
  payload[2] = C_CHECKPK_OPCODE;
  payload[3] = (BYTE)index;
  payload[4] = 0;
  payload[5] = 0;
  NetLog("[mimir] PumpPendingChoice step3 payload built");

  // 除了 my_send 那層外層 XOR，body 在原生流程裡還會先被遊戲自己的 Blowfish
  // 衍生加密處理過（LineageEncryption.cpp 有完整說明，含金鑰推進補done的部分）。
  // 現在保證跑在遊戲網路執行緒上（my_recv 呼叫），跟原生程式碼呼叫這個函式時
  // 是同一個執行緒 context。
  LineageEncryption_EncryptBody(&payload[2], 4);
  NetLog("[mimir] PumpPendingChoice step4 encrypted");

  // 呼叫 MimirSendEncoded（不是 send()）：直接做外層 XOR 編碼＋呼叫 real_send，
  // 不會被 Detours 導回 my_send，避免鉤子巢狀呼叫自己（PumpPendingChoice 本來就是
  // my_send 呼叫的，如果這裡再呼叫 send() 被導回 my_send，就是鉤子呼叫自己）。
  // 只送 wireLen(=6) bytes，後面的 padding 不會送出去。
  int sent = MimirSendEncoded(g_gameSocket, payload, kWireLen);
  NetLog("[mimir] PumpPendingChoice step5 sent=%d", sent);
  NetLog("[mimir] PumpPendingChoice index=%ld sent=%d bytes=%d", index, sent,
         kWireLen);
}
