// Candidates.cpp: 對照 Rust 版 ime_overlay/src/candidates.rs。
// 兩個結構需要小心（跟 Rust 版註解一致）：
//   1. CANDIDATELIST 是變長 — header 28 bytes(0x18)... 實際上 header 到 dwPageSize 是
//      0x18 bytes，接著 dwOffset[dwCount]，每個是相對 buffer 起始的 byte offset，
//      指向 buffer 內的 wide-char 字串。
//   2. ImmGetCompositionStringW 回傳的是 byte 長度（不是 char 數），要 / 2 換 char 數。
#include "Common.h"
#include "Candidates.h"
#include "Dbg.h"
#include <imm.h>
#include <vector>

#pragma comment(lib, "imm32.lib")

std::vector<std::wstring> ImeState::PageItems() const {
  size_t start = pageStart;
  size_t end = start + pageSize;
  if (end > items.size()) end = items.size();
  if (start > end) return {};
  return std::vector<std::wstring>(items.begin() + start, items.begin() + end);
}

int ImeState::PageSelection() const {
  if (selection >= pageStart && selection < pageStart + pageSize) {
    return (int)(selection - pageStart);
  }
  return -1;
}

// 嘗試多個 hwnd 拿 HIMC；回傳擁有該 HIMC 的 hwnd（釋放時要用同一個）。
static bool ResolveHimc(HWND hwnd, HIMC *outHimc, HWND *outOwner) {
  HIMC himc = ImmGetContext(hwnd);
  if (himc) {
    *outHimc = himc;
    *outOwner = hwnd;
    return true;
  }
  HWND parent = GetParent(hwnd);
  if (parent) {
    himc = ImmGetContext(parent);
    if (himc) {
      *outHimc = himc;
      *outOwner = parent;
      return true;
    }
  }
  HWND root = GetAncestor(hwnd, GA_ROOT);
  if (root) {
    himc = ImmGetContext(root);
    if (himc) {
      *outHimc = himc;
      *outOwner = root;
      return true;
    }
  }
  IME_LOG("[ime] HIMC FAIL hwnd=0x%p", hwnd);
  return false;
}

// 讀組字字串（GCS_COMPSTR）
static std::wstring ReadComposition(HIMC himc, DWORD kind) {
  LONG need = ImmGetCompositionStringW(himc, kind, NULL, 0);
  if (need <= 0 || (size_t)need > 4096) return L"";
  std::vector<wchar_t> buf((size_t)need / 2 + 1, 0);
  LONG got = ImmGetCompositionStringW(himc, kind, buf.data(), (DWORD)need);
  if (got <= 0) return L"";
  size_t chars = (size_t)got / 2;
  if (chars > buf.size()) chars = buf.size();
  return std::wstring(buf.data(), chars);
}

// 解析 CANDIDATELIST 結構（header 定義同 Win32 imm.h 的 CANDIDATELIST，
// dwOffset 是變長陣列，用原始 byte buffer + 手動邊界檢查存取）。
static bool ParseCandList(const std::vector<BYTE> &buf, UINT *total, UINT *selection,
                          UINT *pageStart, UINT *pageSize, std::vector<std::wstring> *items) {
  if (buf.size() < 0x18) return false;
  auto readU32 = [&](size_t off) -> DWORD {
    return *(const DWORD *)(buf.data() + off);
  };

  DWORD dwCount = readU32(0x08);
  DWORD dwSelection = readU32(0x0C);
  DWORD dwPageStart = readU32(0x10);
  DWORD dwPageSize = readU32(0x14);

  if (dwCount == 0 || dwCount > 1024) return false;

  size_t visible = dwCount < 64 ? dwCount : 64;
  items->clear();
  items->reserve(visible);
  for (size_t i = 0; i < visible; i++) {
    size_t offPos = 0x18 + i * 4;
    if (offPos + 4 > buf.size()) break;
    DWORD off = readU32(offPos);
    if (off >= buf.size()) {
      items->push_back(L"");
      continue;
    }
    std::wstring s;
    size_t p = off;
    while (p + 2 <= buf.size()) {
      wchar_t ch = *(const wchar_t *)(buf.data() + p);
      if (ch == 0) break;
      s.push_back(ch);
      p += 2;
      if (s.size() > 64) break; // 安全上限
    }
    items->push_back(s);
  }

  *total = dwCount;
  *selection = dwSelection;
  *pageStart = dwPageStart;
  *pageSize = dwPageSize;
  return true;
}

static bool ReadCandidateList(HIMC himc, UINT *total, UINT *selection, UINT *pageStart,
                              UINT *pageSize, std::vector<std::wstring> *items) {
  DWORD need = ImmGetCandidateListW(himc, 0, NULL, 0);
  if (need == 0) {
    IME_LOG("[ime] ImmGetCandidateListW size=0 (himc=0x%p)", himc);
    return false;
  }
  if (need > 32768) {
    IME_LOG("[ime] ImmGetCandidateListW size=%lu too large", (unsigned long)need);
    return false;
  }
  std::vector<BYTE> buf(need, 0);
  DWORD got = ImmGetCandidateListW(himc, 0, (LPCANDIDATELIST)buf.data(), need);
  if (got == 0) {
    IME_LOG("[ime] ImmGetCandidateListW fill returned 0");
    return false;
  }
  if (!ParseCandList(buf, total, selection, pageStart, pageSize, items)) {
    IME_LOG("[ime] ParseCandList returned false (need=%lu)", (unsigned long)need);
    return false;
  }
  return true;
}

bool FetchImeState(HWND hwnd, ImeState &out) {
  HIMC himc;
  HWND owner;
  if (!ResolveHimc(hwnd, &himc, &owner)) return false;

  bool ok = false;
  out.composition = ReadComposition(himc, GCS_COMPSTR);
  ok = ReadCandidateList(himc, &out.total, &out.selection, &out.pageStart, &out.pageSize, &out.items);

  ImmReleaseContext(owner, himc);
  return ok;
}
