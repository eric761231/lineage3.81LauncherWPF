// AttackDamageFeetHook.cpp: 將遊戲中紅色傷害數字（氣泡）的繪製錨點改為角色的腳下。
// 對照 RUST 版本 attack_damage_feet_hook.rs（採用四段 Codecave 記憶體 Hook 方式實現）。
#include "stdafx.h"
#include "AttackDamageFeetHook.h"
#include "LauncherDll.h"
#include <cstring>

namespace {

// ============================================================================
// 記憶體 Hook 目標位址 (遊戲記憶體中的原程式碼位址)
// ============================================================================
constexpr DWORD kLocalAddr        = 0x0042B9D2; // 本地玩家傷害顯示 Hook 點
constexpr DWORD kRemoteAddr       = 0x0042B9FB; // 其他玩家/遠端傷害顯示 Hook 點
constexpr DWORD kPostAc80Addr     = 0x0042BAFB; // 傷害氣泡生成後置處理 Hook 點
constexpr DWORD kAc80ResetAddr    = 0x0042AE0C; // 狀態重置 Hook 點
constexpr DWORD kReturnAfterPos   = 0x0042BA01; // 位置調整完成後的跳回位址
constexpr DWORD kReturnAfterPost  = 0x0042BB00; // 後置處理完成後的跳回位址
constexpr DWORD kReturnAfterReset = 0x0042AE13; // 重置處理完成後的跳回位址

// 各 Hook 點要被替換的原指令長度（Byte 數）
constexpr int kLocalLen  = 6;
constexpr int kRemoteLen = 6;
constexpr int kPostLen   = 5;
constexpr int kResetLen  = 7;

// ============================================================================
// 原廠程式碼 Byte 驗證碼 (用來確認遊戲版本是否匹配，避免寫錯記憶體導致 Crash)
// ============================================================================
const BYTE kLocalOrig[kLocalLen]   = {0x89, 0x8A, 0x80, 0x03, 0x00, 0x00}; // mov [edx+0x380], ecx
const BYTE kRemoteOrig[kRemoteLen] = {0x89, 0x81, 0x80, 0x03, 0x00, 0x00}; // mov [ecx+0x380], eax
const BYTE kPostOrig[kPostLen]     = {0x0F, 0xB6, 0xC0, 0x85, 0xC0};       // movzx eax, al; test eax, eax
const BYTE kResetOrig[kResetLen]   = {0xC6, 0x81, 0x6A, 0x03, 0x00, 0x00, 0x00}; // mov byte ptr [ecx+0x36A], 0

// ============================================================================
// 參數定義
// ============================================================================
constexpr BYTE kColorLo  = 0x00; // 傷害顏色判斷小端序低位 (0xF800 -> 代表紅色)
constexpr BYTE kColorHi  = 0xF8; // 傷害顏色判斷小端序高位
constexpr BYTE kFeetPad  = 0x20; // 腳下 Y 軸偏移量 (Padding)
constexpr size_t kCaveSize = 0x400; // 分配給 Codecave 的記憶體大小 (1024 Bytes)

bool g_installed = false; // 紀錄 Hook 是否已安裝

/**
 * @brief 修改指定記憶體區域的代碼 (寫入 Patch)
 * @param addr 目標記憶體位址
 * @param code 要寫入的新指令 byte
 * @param len 長度
 */
void PatchCode(void *addr, const void *code, int len) {
  DWORD oldProt = 0;
  // 解除記憶體寫入保護 (改為可讀可寫)
  VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, PAGE_READWRITE, &oldProt);
  memcpy(addr, code, len);
  // 恢復原本的記憶體保護狀態
  VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, oldProt, &oldProt);
}

/**
 * @brief 在 Codecave 結尾生成相對 JMP 32 位元跳轉指令，跳回原程式碼流程
 * @param sc Shellcode 緩衝區
 * @param n 當前寫入指標 (會隨之更新)
 * @param here 當前 JMP 指令所在的記憶體位址
 * @param target 要跳轉目標的位址
 */
void EmitJmp32(BYTE *sc, int &n, DWORD here, DWORD target) {
  sc[n++] = 0xE9; // JMP opcode
  *(int *)&sc[n] = (int)((intptr_t)target - (intptr_t)(here + 5)); // 計算相對偏移量 (Target - (Here + 5))
  n += 4;
}

/**
 * @brief 構造注入原程式碼區段的 JMP Patch (會用 NOP 補齊剩餘長度)
 */
void BuildJmpPatch(BYTE *patch, int len, DWORD hookAddr, DWORD target) {
  memset(patch, 0x90, len); // 用 NOP (0x90) 填滿
  patch[0] = 0xE9; // 設定開頭為 JMP
  *(int *)&patch[1] = (int)((intptr_t)target - (intptr_t)hookAddr - 5);
}

// ============================================================================
// Shellcode 生成函數 (Codecave 核心邏輯)
// ============================================================================

/**
 * @brief 遠端/其他玩家傷害顯示 Hook 邏輯
 * 邏輯：寫入預設 Y 座標 -> 檢查傷害顏色是否為紅色 -> 若為紅色，則讀取角色高度資料，調整 Y 座標至腳下
 */
int EmitRemote(BYTE *sc, DWORD segStart) {
  int n = 0;
  const BYTE body[] = {
      0x89, 0x81, 0x80, 0x03, 0x00, 0x00, // mov [ecx+0x380], eax (執行原本被覆蓋掉的指令)
      0x66, 0x81, 0x7D, 0x10, kColorLo, kColorHi, // cmp word ptr [ebp+0x10], 0xF800 (檢查是否為紅色傷害)
      0x75, 0x2A,                         // jne +0x2A (若非紅色，跳過腳下座標修正)
      0x8B, 0x91, 0x98, 0x03, 0x00, 0x00, // mov edx, [ecx+0x398] (取得角色結構/模型指針)
      0x85, 0xD2,                         // test edx, edx
      0x74, 0x20,                         // jz +0x20 (若指針無效則跳過)
      0x0F, 0xBE, 0x42, 0x1D,             // movsx eax, byte ptr [edx+0x1D] (取得角色動作/形態 ID)
      0x6B, 0xC0, 0x18,                   // imul eax, eax, 0x18 (計算數據表偏移量 offset = id * 24)
      0x8B, 0x92, 0x8C, 0x00, 0x00, 0x00, // mov edx, [edx+0x8C] (取得高度表基址)
      0x85, 0xD2,                         // test edx, edx
      0x74, 0x0F,                         // jz +0x0F
      0x8B, 0x54, 0x02, 0x14,             // mov edx, [edx+eax*1+0x14] (讀取特定姿態下的高度修正值)
      0xF7, 0xDA,                         // neg edx (取負數，將向上偏移轉為向下)
      0x83, 0xC2, kFeetPad,               // add edx, kFeetPad (加上腳下 Padding)
      0x01, 0x91, 0x80, 0x03, 0x00, 0x00  // add [ecx+0x380], edx (將計算出的位移套用到 Y 軸座標)
  };
  memcpy(sc, body, sizeof(body));
  n = (int)sizeof(body);
  EmitJmp32(sc, n, segStart + n, kReturnAfterPos); // 跳回遊戲原本流程
  return n;
}

/**
 * @brief 本地玩家傷害顯示 Hook 邏輯 (同 EmitRemote，但暫存器組合不同)
 */
int EmitLocal(BYTE *sc, DWORD segStart) {
  int n = 0;
  const BYTE body[] = {
      0x89, 0x8A, 0x80, 0x03, 0x00, 0x00, // mov [edx+0x380], ecx (覆蓋原指令)
      0x66, 0x81, 0x7D, 0x10, kColorLo, kColorHi, // cmp word ptr [ebp+0x10], 0xF800 (判定紅色)
      0x75, 0x2A,                         // jne
      0x8B, 0x8A, 0x98, 0x03, 0x00, 0x00, // mov ecx, [edx+0x398]
      0x85, 0xC9,                         // test ecx, ecx
      0x74, 0x20,                         // jz
      0x0F, 0xBE, 0x41, 0x1D,             // movsx eax, byte ptr [ecx+0x1D]
      0x6B, 0xC0, 0x18,                   // imul eax, eax, 0x18
      0x8B, 0x89, 0x8C, 0x00, 0x00, 0x00, // mov ecx, [ecx+0x8C]
      0x85, 0xC9,                         // test ecx, ecx
      0x74, 0x0F,                         // jz
      0x8B, 0x4C, 0x01, 0x14,             // mov ecx, [ecx+eax*1+0x14]
      0xF7, 0xD9,                         // neg ecx
      0x83, 0xC1, kFeetPad,               // add ecx, kFeetPad
      0x01, 0x8A, 0x80, 0x03, 0x00, 0x00  // add [edx+0x380], ecx
  };
  memcpy(sc, body, sizeof(body));
  n = (int)sizeof(body);
  EmitJmp32(sc, n, segStart + n, kReturnAfterPos);
  return n;
}

/**
 * @brief 傷害顯示後置標記處理 (Post-process)
 * 判斷傷害若為紅色，將特定狀態標記 (Offset 0x36A) 設為 4。
 */
int EmitPostAc80(BYTE *sc, DWORD segStart) {
  int n = 0;
  const BYTE body[] = {
      0x8B, 0x55, 0xE0,                   // mov edx, [ebp-0x20]
      0x66, 0x81, 0xBA, 0x9C, 0x03, 0x00, 0x00, kColorLo, kColorHi, // cmp word ptr [edx+0x39C], 0xF800
      0x75, 0x07,                         // jne +0x07 (如果不是紅色則跳過修改)
      0xC6, 0x82, 0x6A, 0x03, 0x00, 0x00, 0x04, // mov byte ptr [edx+0x36A], 4 (將標記設為 4)
      0x0F, 0xB6, 0xC0,                   // movzx eax, al (原被覆蓋指令)
      0x85, 0xC0                          // test eax, eax (原被覆蓋指令)
  };
  memcpy(sc, body, sizeof(body));
  n = (int)sizeof(body);
  EmitJmp32(sc, n, segStart + n, kReturnAfterPost);
  return n;
}

/**
 * @brief 狀態重置 Hook
 * 針對非紅色傷害，清空狀態標記 (Offset 0x36A 清零)。
 */
int EmitAc80Reset(BYTE *sc, DWORD segStart) {
  int n = 0;
  const BYTE body[] = {
      0x66, 0x81, 0xB9, 0x9C, 0x03, 0x00, 0x00, kColorLo, kColorHi, // cmp word ptr [ecx+0x39C], 0xF800
      0x74, 0x07,                         // je +0x07 (若是紅色則保留狀態，不清除)
      0xC6, 0x81, 0x6A, 0x03, 0x00, 0x00, 0x00 // mov byte ptr [ecx+0x36A], 0 (否則清零，執行原指令)
  };
  memcpy(sc, body, sizeof(body));
  n = (int)sizeof(body);
  EmitJmp32(sc, n, segStart + n, kReturnAfterReset);
  return n;
}

} // namespace

/**
 * @brief 安裝「紅色傷害數字腳下錨點」Hook 的主進入點
 */
void InstallAttackDamageFeetHook() {
  if (g_installed)
    return;

  // 1. 安全檢查：比對記憶體中的指令是否與原廠預期的 Byte 吻合 (防止版本不符導致 Crash)
  if (memcmp((void *)kLocalAddr, kLocalOrig, kLocalLen) != 0 ||
      memcmp((void *)kRemoteAddr, kRemoteOrig, kRemoteLen) != 0 ||
      memcmp((void *)kPostAc80Addr, kPostOrig, kPostLen) != 0 ||
      memcmp((void *)kAc80ResetAddr, kResetOrig, kResetLen) != 0) {
    launcherdll_hook_log("[AttackDmgFeet][WARN] bytes mismatch, skip");
    return;
  }

  // 2. 向系統申請可執行(PAGE_EXECUTE_READWRITE)的 Codecave 記憶體空間
  BYTE *cave = (BYTE *)VirtualAlloc(NULL, kCaveSize, MEM_COMMIT | MEM_RESERVE,
                                    PAGE_EXECUTE_READWRITE);
  if (!cave) {
    launcherdll_hook_log("[AttackDmgFeet][WARN] VirtualAlloc failed");
    return;
  }

  DWORD base = (DWORD)(uintptr_t)cave;
  int off = 0;

  // 3. 在 Codecave 中依序填入四段 Shellcode 邏輯
  int remoteOff = off;
  off += EmitRemote(cave + off, base + remoteOff);
  
  int localOff = off;
  off += EmitLocal(cave + off, base + localOff);
  
  int postOff = off;
  off += EmitPostAc80(cave + off, base + postOff);
  
  int resetOff = off;
  off += EmitAc80Reset(cave + off, base + resetOff);

  // 4. 對遊戲記憶體進行 Patch：寫入 JMP 指令，將原本的執行流程轉移至 Codecave 中
  BYTE patch[8];
  
  BuildJmpPatch(patch, kRemoteLen, kRemoteAddr, base + remoteOff);
  PatchCode((void *)kRemoteAddr, patch, kRemoteLen);

  BuildJmpPatch(patch, kLocalLen, kLocalAddr, base + localOff);
  PatchCode((void *)kLocalAddr, patch, kLocalLen);

  BuildJmpPatch(patch, kPostLen, kPostAc80Addr, base + postOff);
  PatchCode((void *)kPostAc80Addr, patch, kPostLen);

  BuildJmpPatch(patch, kResetLen, kAc80ResetAddr, base + resetOff);
  PatchCode((void *)kAc80ResetAddr, patch, kResetLen);

  // 5. 標記已成功安裝並輸出日誌
  g_installed = true;
  launcherdll_hook_log(
      "[AttackDmgFeet] installed cave=%p (remote=+0x%X local=+0x%X post=+0x%X reset=+0x%X)",
      cave, remoteOff, localOff, postOff, resetOff);
}