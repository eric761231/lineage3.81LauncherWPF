// HelperDlg.cpp: 遊戲內外掛/輔助視窗的 UI 互動與內部邏輯實作。
// 本檔案透過 Direct Memory Access (DMA) 讀取遊戲記憶體資料，並透過呼叫遊戲內部 Function (Call 技) 實現自動喝水與自動刪除物品。
//
// 2026-09-07：整份檔案停用（#if 0 包起來，不刪除，僅供參考）。之後不會再使用
// 這套做法，自動喝藥已經改成後端判斷版（見 PssConfig.cpp /
// PssOverlay.cpp，HOME 熱鍵開啟設定視窗），不再走這裡的 DMA 直接讀寫
// 記憶體/內聯組合語言呼叫遊戲 Call 這條路。
//
// 已知異常（保留這份記錄，供之後參考用，不代表會修）：
// - GetHP()/GetMP() 的解密位址/XOR key 是寫死的常數，遊戲更新後很容易失效
//   （沒有版本檢查機制，失效時會靜默回傳 0，不會有任何錯誤提示）。
// - UseItem(const char*)/DeleteItem(const char*) 用道具「名稱」比對背包，同名
//   道具（例如不同強化值但顯示名稱相同）會被一起用掉/刪掉，沒有辦法只選特定
//   一顆；找到符合的就處理，不會 return，一次觸發可能誤傷多顆同名道具。
// - CheckProtect() 沒有任何節流/冷卻機制，純粹用 1 秒 Timer 掃描門檻，同一
//   秒內只要條件持續成立就會不斷嘗試觸發 UseItem，沒有 L1ItemDelay 那種伺服器
//   端冷卻保護，理論上可能造成短時間內重複送出使用道具的呼叫。
// - UseItem(DWORD)/DeleteItem(DWORD) 直接內聯組合語言呼叫遊戲內部 Call
//   （0x00580E50 等寫死位址），沒有前置的存在性/合法性檢查，遊戲版本一換就是
//   直接照著錯的位址跳過去，行為不可預期（比 DMA 讀取更危險，讀取失敗頂多讀到
//   0，呼叫錯的 Call 位址可能直接讓遊戲當掉）。
// - DeleteDlgProc 的 IDC_COMBO1 case 註解自己承認「原有程式碼誤用 CB_ADDSTRING」
//   （ComboBox 訊息用在 ListBox 上），能動是因為訊息數值剛好對應到，不是正確
//   用法，不要照抄。
//
// #include 留在 #if 0 外面：/Yu"stdafx.h" 預編譯標頭機制需要在檔案裡實際處理
// 到這行 #include，放進 #if 0 裡面前置處理器根本不會走到，會導致
// C1010（找不到預編譯標頭停止點）編譯錯誤。
#include "stdafx.h" // IWYU pragma: keep
#include "HelperDlg.h"

// 停用後仍提供空實作，避免其他 TU 連結失敗。
HWND hWndHelper = NULL;
bool CreateHelperDialog() { return false; }
void DestroyHelperDialog() {}
void ShowOrHideHelperDialog() {}

#if 0
// ---------------------------------------------------------------------------
// 結構體定義 (Data Structures)
// ---------------------------------------------------------------------------

// 儲存從 INI 設定檔讀取出來的物品資訊
typedef struct {
  int id;          // 物品 ID
  char name[128];  // 物品名稱 (ANSI 格式)
} ITEM_INFO;

// 強制編譯器採用 1 位元組對齊 (Byte Alignment)，對齊遊戲內部記憶體結構
#pragma pack(push, 1)
typedef struct {
  int unknow1;   // 偏移 0x00: 未知用途或內部標記
  DWORD id;      // 偏移 0x04: 背包物品的唯一動態實體 ID (Item Instance ID)
  int unknow2;   // 偏移 0x08: 未知用途 (例如數量或狀態)
  char *name;    // 偏移 0x0C: 指向物品名稱字串的記憶體指標 (Pointer to string)
} BAGITEM_INFO;
#pragma pack(pop)

// ---------------------------------------------------------------------------
// 全域變數 (Global Variables)
// ---------------------------------------------------------------------------

HWND hWndHelper = NULL;    // 輔助器主視窗控制代碼 (Window Handle)
HWND hPage[2];             // 子分頁對話框控制代碼陣列 (0: 保護分頁, 1: 刪除物品分頁)
RECT rcTabCtrl;            // 儲存 Tab 控制項的客戶區範圍，用於定位子分頁
HWND hComboBoxHP[6];       // 儲存保護分頁中 6 個血藥下拉選單的 Handle

std::vector<ITEM_INFO> vtHPItemList;      // 儲存可供選擇的 HP 恢復道具列表
std::vector<ITEM_INFO> vtHPMPItemList;    // 儲存可供選擇的 HP+MP 恢復道具列表
std::vector<ITEM_INFO> vtDeleteItemList;  // 儲存自動刪除物品列表

// Tab 頁籤標題 (亂碼通常是因為原本檔案編碼為 Big5 / GBK，在 UTF-8 下顯示異常)
// 正確字串預計為：_T("藥水"), _T("刪除")
_TCHAR szTabTitle[2][32] = {_T("藥水"), _T("刪除")};

// ---------------------------------------------------------------------------
// 遊戲記憶體讀取與 Call 呼叫函式 (Game Memory & ASM Functions)
// ---------------------------------------------------------------------------

/**
 * @brief 從遊戲記憶體中動態解密並讀取當前角色 HP
 * @return int 當前 HP 值，若讀取失敗或例外則回傳 0
 * 
 * ASM 邏輯分析：
 * 1. 0x00BDC828 為基址 (Base Address)
 * 2. 透過 XOR 0xC0017921 進行解碼取得陣列/結構索引
 * 3. 透過多重指標偏移量抓取加密後的 HP，最後再 XOR 解密出真實數值
 */
int GetHP() {
  DWORD hp = 0;
  __try {
    __asm
    {
      mov eax, 0x00BDC828             // 載入玩家資料結構基址
      mov ecx, dword ptr ds:[eax]     // 取出加密的 Key / Index 基礎值
      xor ecx, 0xC0017921             // 與常數 XOR 解碼出實際陣列索引
      mov ebx, dword ptr ds:[eax+4]   // 取得動態資料表格位址
      mov edx, dword ptr ds:[ebx+ecx*4] // 讀取加密的 HP 原始資料
      xor edx, dword ptr ds:[eax+8]   // 與次級 Key 解密，得到真實 HP 數值
      mov hp, edx                     // 將結果寫回 C++ 變數
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    // 防止因遊戲記憶體位址變更或讀取無效位址造成輔助器崩潰 (Crash)
    hp = 0;
  }
  return hp;
}

/**
 * @brief 從遊戲記憶體中動態解密並讀取當前角色 MP
 * @return int 當前 MP 值，若讀取失敗或例外則回傳 0
 */
int GetMP() {
  DWORD mp = 0;
  __try {
    __asm
    {
      mov eax, 0x00BDC834             // 載入 MP 資料基址 (與 HP 位址偏移 0x0C)
      mov ecx, dword ptr ds:[eax]     // 解密步驟與 GetHP 相同
      xor ecx, 0xC0017921
      mov ebx, dword ptr ds:[eax+4]
      mov edx, dword ptr ds:[ebx+ecx*4]
      xor edx, dword ptr ds:[eax+8]
      mov mp, edx
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    mp = 0;
  }
  return mp;
}

/**
 * @brief 讀取當前背包內的物品總數量
 * @return int 物品數量
 */
int GetItemCount() {
  int count = 0;
  __try {
    // 0x009A9250 為背包管理物件指標
    int a = *(int *)0x009A9250;
    count = *(int *)(a + 0x2C); // 偏移 0x2C 處存放當前背包物品計數
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    count = 0;
  }
  return count;
}

/**
 * @brief 依背包索引取得物品結構指標
 * @param index 物品在背包陣列中的索引 (0 ~ count-1)
 * @return BAGITEM_INFO* 指向物品結構體的指標，失敗回傳 NULL
 */
BAGITEM_INFO *GetItem(int index) {
  BAGITEM_INFO *pBagItem = NULL;
  __try {
    DWORD a = *(DWORD *)0x009A9250;        // 取得背包物件基址
    a = *(DWORD *)(a + 0x58);              // 偏移 0x58 處為物品指標陣列位址
    a = *(DWORD *)(a + index * 4);         // 依索引取出對應物品的動態記憶體位址
    pBagItem = (BAGITEM_INFO *)a;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    pBagItem = NULL;
  }
  return pBagItem;
}

/**
 * @brief 直接呼叫遊戲內部的「使用道具」函式 (Internal Call)
 * @param id 物品的動態實體 ID
 */
void UseItem(DWORD id) {
  __asm
  {
    push 0x008D0733     // 參數 4: 遊戲內部封包/呼叫常數或回呼 context
    push id             // 參數 3: 要使用的物品實體 ID
    push 0xA4           // 參數 2: 操作指令碼 (Action ID, 0xA4 代表使用物品)
    push 0x008D07EC     // 參數 1: 遊戲內部物件/結構位址
    mov eax, 0x00580E50 // 遊戲「通用操作/送出封包」的 Call 位址
    call eax            // 執行 Call
    add esp, 0x10       // 平衡堆疊 (__cdecl 呼叫慣例，4 個參數共 16 bytes)
  }
}

/**
 * @brief 根據道具名稱搜尋背包，並呼叫 UseItem(id) 使用該道具
 * @param name 道具名稱
 */
void UseItem(const char *name) {
  int count = GetItemCount();
  for (int i = 0; i < count; i++) {
    BAGITEM_INFO *pBagItem = GetItem(i);
    // 檢查指標與名稱指標是否有效，避免 Access Violation
    if (pBagItem == NULL || pBagItem->name == NULL)
      continue;
    
    // 不分大小寫比對道具名稱
    if (_stricmp(pBagItem->name, name) == 0) {
      UseItem(pBagItem->id);
      // 注意：這裡找到第一項就使用，若背包有多個同名物品會使用找到的第一個
    }
  }
}

/**
 * @brief 直接呼叫遊戲內部的「刪除道具」函式 (Internal Call)
 * @param id 物品的動態實體 ID
 */
void DeleteItem(DWORD id) {
  __asm
  {
    push 0              // 參數 4: 數量或其他標記
    push id             // 參數 3: 要刪除的物品實體 ID
    push 0x8A           // 參數 2: 操作指令碼 (Action ID, 0x8A 代表刪除物品)
    push 0x008D0978     // 參數 1: 遊戲內部刪除物件/結構位址
    mov eax, 0x00580E50 // 遊戲通用 Call 位址
    call eax            // 執行 Call
    add esp, 0x10       // 平衡堆疊 (__cdecl)
  }
}

/**
 * @brief 根據道具名稱搜尋背包，並呼叫 DeleteItem(id) 刪除該道具
 * @param name 道具名稱
 */
void DeleteItem(const char *name) {
  int count = GetItemCount();
  for (int i = 0; i < count; i++) {
    BAGITEM_INFO *pBagItem = GetItem(i);
    if (pBagItem == NULL || pBagItem->name == NULL)
      continue;
    
    if (_stricmp(pBagItem->name, name) == 0) {
      DeleteItem(pBagItem->id);
    }
  }
}

// ---------------------------------------------------------------------------
// 介面邏輯與保護機制 (UI & Auto Protect Logic)
// ---------------------------------------------------------------------------

/**
 * @brief 切換 Tab 頁籤時顯示/隱藏對應的子對話框
 * @param Page 頁籤索引 (0 或 1)
 */
void SelectPage(int Page) {
  for (int i = 0; i < 2; i++) {
    ShowWindow(hPage[i], (i == Page) ? SW_SHOW : SW_HIDE);
  }
}

/**
 * @brief 檢查自動保護條件 (自動補血 / 補魔邏輯)
 * @param hWnd 保護設定頁面的 HWND
 */
void CheckProtect(HWND hWnd) {
  int hp, mp, index;
  BOOL translated;
  int curhp = GetHP();
  int curmp = GetMP();

  // 1. 特殊條件：同時判斷 HP 高於門檻且 MP 低於門檻 (例如使用洗泉水/特定複合藥水)
  if (IsDlgButtonChecked(hWnd, IDC_CHECK7)) {
    hp = GetDlgItemInt(hWnd, IDC_EDIT7, &translated, FALSE);
    mp = GetDlgItemInt(hWnd, IDC_EDIT8, &translated, FALSE);
    if (curhp > hp && curmp < mp) {
      index = SendDlgItemMessage(hWnd, IDC_COMBO7, CB_GETCURSEL, 0, 0);
      if (index != CB_ERR && index < (int)vtHPMPItemList.size()) {
        UseItem(vtHPMPItemList[index].name);
      }
    }
  }

  // 2. 6 組獨立的 HP 門檻保護檢查 (逐一檢查 1~6 號設定)
  // 設定檔通常按緊急程度排列 (如: 15% 喝大白, 50% 喝中白...)
  
  // 門檻 1 檢查
  if (IsDlgButtonChecked(hWnd, IDC_CHECK1)) {
    hp = GetDlgItemInt(hWnd, IDC_EDIT1, &translated, FALSE);
    if (hp > 0 && curhp < hp) {
      index = SendDlgItemMessage(hWnd, IDC_COMBO1, CB_GETCURSEL, 0, 0);
      if (index != CB_ERR && index < (int)vtHPItemList.size()) {
        UseItem(vtHPItemList[index].name);
      }
      // return; // 若取消註解，則觸發高優先度保護後，不再繼續觸發後續保護
    }
  }

  // 門檻 2 檢查
  if (IsDlgButtonChecked(hWnd, IDC_CHECK2)) {
    hp = GetDlgItemInt(hWnd, IDC_EDIT2, &translated, FALSE);
    if (hp > 0 && curhp < hp) {
      index = SendDlgItemMessage(hWnd, IDC_COMBO2, CB_GETCURSEL, 0, 0);
      if (index != CB_ERR && index < (int)vtHPItemList.size()) {
        UseItem(vtHPItemList[index].name);
      }
    }
  }

  // 門檻 3 檢查
  if (IsDlgButtonChecked(hWnd, IDC_CHECK3)) {
    hp = GetDlgItemInt(hWnd, IDC_EDIT3, &translated, FALSE);
    if (hp > 0 && curhp < hp) {
      index = SendDlgItemMessage(hWnd, IDC_COMBO3, CB_GETCURSEL, 0, 0);
      if (index != CB_ERR && index < (int)vtHPItemList.size()) {
        UseItem(vtHPItemList[index].name);
      }
    }
  }

  // 門檻 4 檢查
  if (IsDlgButtonChecked(hWnd, IDC_CHECK4)) {
    hp = GetDlgItemInt(hWnd, IDC_EDIT4, &translated, FALSE);
    if (hp > 0 && curhp < hp) {
      index = SendDlgItemMessage(hWnd, IDC_COMBO4, CB_GETCURSEL, 0, 0);
      if (index != CB_ERR && index < (int)vtHPItemList.size()) {
        UseItem(vtHPItemList[index].name);
      }
    }
  }

  // 門檻 5 檢查
  if (IsDlgButtonChecked(hWnd, IDC_CHECK5)) {
    hp = GetDlgItemInt(hWnd, IDC_EDIT5, &translated, FALSE);
    if (hp > 0 && curhp < hp) {
      index = SendDlgItemMessage(hWnd, IDC_COMBO5, CB_GETCURSEL, 0, 0);
      if (index != CB_ERR && index < (int)vtHPItemList.size()) {
        UseItem(vtHPItemList[index].name);
      }
    }
  }

  // 門檻 6 檢查
  if (IsDlgButtonChecked(hWnd, IDC_CHECK6)) {
    hp = GetDlgItemInt(hWnd, IDC_EDIT6, &translated, FALSE);
    if (hp > 0 && curhp < hp) {
      index = SendDlgItemMessage(hWnd, IDC_COMBO6, CB_GETCURSEL, 0, 0);
      if (index != CB_ERR && index < (int)vtHPItemList.size()) {
        UseItem(vtHPItemList[index].name);
      }
    }
  }
}

/**
 * @brief 自動刪除物品邏輯：遍歷 UI 列表框中的物品名稱並執行刪除
 * @param hWnd 刪除頁面的 HWND
 */
void CheckDeleteItem(HWND hWnd) {
  // 檢查刪除功能開關是否勾選
  if (IsDlgButtonChecked(hWnd, IDC_CHECK1)) {
    int cnt = SendDlgItemMessage(hWnd, IDC_LIST1, LB_GETCOUNT, 0, 0);
    if (cnt == LB_ERR)
      return;

    // 逐一取得 ListBox 中的物品名稱，並從背包刪除
    for (int i = 0; i < cnt; i++) {
      char text[MAX_PATH];
      memset(text, 0, MAX_PATH);
      int result = SendDlgItemMessageA(hWnd, IDC_LIST1, LB_GETTEXT, i, (LPARAM)text);
      if (result == LB_ERR)
        continue;

      DeleteItem(text);
    }
  }
}

// ---------------------------------------------------------------------------
// 對話框程序 (Dialog Procedures)
// ---------------------------------------------------------------------------

/**
 * @brief 保護分頁對話框程序
 */
INT_PTR CALLBACK ProtectDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  switch (uMsg) {
  case WM_INITDIALOG: {
    // 1. 初始化並從 LinHelperZ.ini 載入藥水選單配置
    const char *app = "AllHP";
    const char *file = "./LinHelperZ.ini";
    vtHPItemList.clear();
    vtHPMPItemList.clear();
    char key[MAX_PATH];
    ITEM_INFO ItemInfo;

    // 讀取 HP 藥水設定 (Item0 ~ Item98)
    for (int i = 0; i < 99; i++) {
      memset(&ItemInfo, 0, sizeof(ItemInfo));
      sprintf_s(key, "Item%d", i);
      int len = GetPrivateProfileStringA(app, key, "", ItemInfo.name, 128, file);
      if (len <= 0)
        break; // 讀到空值代表結束
      vtHPItemList.push_back(ItemInfo);
    }

    // 綁定 6 個 ComboBox 控制項 Handle
    hComboBoxHP[0] = GetDlgItem(hWnd, IDC_COMBO1);
    hComboBoxHP[1] = GetDlgItem(hWnd, IDC_COMBO2);
    hComboBoxHP[2] = GetDlgItem(hWnd, IDC_COMBO3);
    hComboBoxHP[3] = GetDlgItem(hWnd, IDC_COMBO4);
    hComboBoxHP[4] = GetDlgItem(hWnd, IDC_COMBO5);
    hComboBoxHP[5] = GetDlgItem(hWnd, IDC_COMBO6);

    // 將讀取的藥水選單塞入 6 個 ComboBox 下拉選單中
    for (size_t i = 0; i < vtHPItemList.size(); i++) {
      for (int j = 0; j < 6; j++) {
        SendMessageA(hComboBoxHP[j], CB_ADDSTRING, 0, (LPARAM)vtHPItemList[i].name);
      }
    }

    // 讀取 HP/MP 複合藥水設定 (HPMP0 ~ HPMP98)
    for (int i = 0; i < 99; i++) {
      memset(&ItemInfo, 0, sizeof(ItemInfo));
      sprintf_s(key, "HPMP%d", i);
      int len = GetPrivateProfileStringA(app, key, "", ItemInfo.name, 128, file);
      if (len <= 0)
        break;
      vtHPMPItemList.push_back(ItemInfo);
    }

    // 將複合藥水選單塞入 IDC_COMBO7
    HWND hComboBox = GetDlgItem(hWnd, IDC_COMBO7);
    for (size_t i = 0; i < vtHPMPItemList.size(); i++) {
      SendMessageA(hComboBox, CB_ADDSTRING, 0, (LPARAM)vtHPMPItemList[i].name);
    }

    // 設定定時器：Timer ID 1000，每 1000 ms (1秒) 觸發一次 CheckProtect
    SetTimer(hWnd, 1000, 1000, NULL);
    break;
  }

  case WM_TIMER: {
    // 收到定時器訊號，執行自動保護邏輯
    CheckProtect(hWnd);
    break;
  }
  }
  return FALSE;
}

/**
 * @brief 將當前 ListBox 的刪除清單寫入 INI 設定檔進行持久化
 * @param hWnd ListBox 的 HWND
 */
void SaveDeleteItem(HWND hWnd) {
  int count = SendMessage(hWnd, LB_GETCOUNT, 0, 0);
  _TCHAR item[MAX_PATH];
  _TCHAR key[MAX_PATH];

  // 寫入前可先考慮清空舊設定 (此處程式碼採用覆蓋寫入模式)
  for (int i = 0; i < count; i++) {
    int result = SendMessage(hWnd, LB_GETTEXT, i, (LPARAM)item);
    if (result != LB_ERR) {
      _stprintf_s(key, _T("Item%d"), i);
      WritePrivateProfileString(_T("DeleteItem"), key, item, _T("./LinHelperZ.ini"));
    }
  }
}

/**
 * @brief 刪除物品分頁對話框程序
 */
INT_PTR CALLBACK DeleteDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  switch (uMsg) {
  case WM_INITDIALOG: {
    // 初始化並從 INI 讀取要刪除的物品名稱列表
    const _TCHAR *app = _T("DeleteItem");
    const _TCHAR *file = _T("./LinHelperZ.ini");
    _TCHAR key[MAX_PATH];
    _TCHAR item[MAX_PATH];

    for (int i = 0; i < 999; i++) {
      _stprintf_s(key, _T("Item%d"), i);
      int len = GetPrivateProfileString(app, key, _T(""), item, MAX_PATH, file);
      if (len <= 0)
        break;
      
      // 注意：此處原有程式碼誤用 CB_ADDSTRING (ComboBox 訊息)，對於 ListBox 應使用 LB_ADDSTRING
      SendDlgItemMessage(hWnd, IDC_LIST1, LB_ADDSTRING, 0, (LPARAM)item);
    }

    // 設定 Timer ID 1001，每 1000 ms 觸發一次刪除檢查
    SetTimer(hWnd, 1001, 1000, NULL);
    break;
  }

  case WM_COMMAND: {
    switch (LOWORD(wParam)) {
    case IDC_BUTTON1: { // 「新增」按鈕邏輯
      _TCHAR text[MAX_PATH];
      GetDlgItemText(hWnd, IDC_COMBO1, text, MAX_PATH);
      
      // 檢查清單中是否已經存在該物品，不存在才新增
      int index = SendDlgItemMessage(hWnd, IDC_LIST1, LB_FINDSTRING, -1, (LPARAM)text);
      if (index == LB_ERR) {
        SendDlgItemMessage(hWnd, IDC_LIST1, LB_ADDSTRING, 0, (LPARAM)text);
        SaveDeleteItem(GetDlgItem(hWnd, IDC_LIST1)); // 同步儲存至 INI
      }
      break;
    }
    case IDC_BUTTON2: { // 「刪除」按鈕邏輯
      int index = SendDlgItemMessage(hWnd, IDC_LIST1, LB_GETCURSEL, 0, 0);
      if (index != LB_ERR) {
        SendDlgItemMessage(hWnd, IDC_LIST1, LB_DELETESTRING, index, 0);
        SaveDeleteItem(GetDlgItem(hWnd, IDC_LIST1)); // 同步儲存至 INI
      }
      break;
    }
    case IDC_COMBO1: {
      // 下拉選單點開時 (CBN_DROPDOWN)，動態讀取當前背包物品並填充至選單中
      if (HIWORD(wParam) == CBN_DROPDOWN) {
        SendDlgItemMessage(hWnd, IDC_COMBO1, CB_RESETCONTENT, 0, 0); // 清空舊選項
        int cnt = GetItemCount();
        for (int i = 0; i < cnt; i++) {
          BAGITEM_INFO *pBagItem = GetItem(i);
          if (pBagItem && pBagItem->name)
            SendDlgItemMessageA(hWnd, IDC_COMBO1, CB_ADDSTRING, 0, (LPARAM)pBagItem->name);
        }
      }
      break;
    }
    }
    break;
  }

  case WM_TIMER: {
    // 執行自動刪除檢查
    CheckDeleteItem(hWnd);
    break;
  }
  }
  return FALSE;
}

/**
 * @brief 主對話框程序 (包含 Tab Control 控制項)
 */
INT_PTR CALLBACK MainDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  switch (uMsg) {
  case WM_INITDIALOG: {
    // 1. 計算 Tab 控制項內的顯示區域範圍 (扣除標題頁籤高度與邊框)
    GetClientRect(GetDlgItem(hWnd, IDC_TAB1), &rcTabCtrl);
    rcTabCtrl.top += 25;
    rcTabCtrl.left += 4;
    rcTabCtrl.right -= 4;
    rcTabCtrl.bottom -= 4;

    // 2. 建立兩個頁籤對應的無模式對話框 (Modeless Dialog)
    hPage[0] = CreateDialog(hins, MAKEINTRESOURCE(IDD_DIALOG_PAGE1),
                            GetDlgItem(hWnd, IDC_TAB1), ProtectDlgProc);
    hPage[1] = CreateDialog(hins, MAKEINTRESOURCE(IDD_DIALOG_PAGE2),
                            GetDlgItem(hWnd, IDC_TAB1), DeleteDlgProc);

    // 3. 將子對話框移動並調整至 Tab 控制項內部區域
    for (int i = 0; i < 2; i++) {
      MoveWindow(hPage[i], rcTabCtrl.left, rcTabCtrl.top,
                 rcTabCtrl.right - rcTabCtrl.left,
                 rcTabCtrl.bottom - rcTabCtrl.top, TRUE);
    }

    // 預設顯示第一個頁籤
    SelectPage(0);

    // 4. 初始化並插入 Tab 頁籤標題
    TCITEM tie;
    tie.mask = TCIF_TEXT;
    for (int i = 0; i < 2; i++) {
      tie.pszText = szTabTitle[i];
      SendDlgItemMessage(hWnd, IDC_TAB1, TCM_INSERTITEM, i, (LPARAM)&tie);
    }
    break;
  }

  case WM_NOTIFY: {
    // 處理 Tab 控制項切換事件
    if (((LPNMHDR)lParam)->code == TCN_SELCHANGE) {
      int i = SendDlgItemMessage(hWnd, IDC_TAB1, TCM_GETCURSEL, 0, 0);
      if (i >= 0 && i < 2) {
        SelectPage(i);
      }
    }
    break;
  }

  case WM_SYSCOMMAND: {
    switch (wParam) {
    case SC_CLOSE: {
      // 點擊關閉按鈕時，僅隱藏視窗而不銷毀，保持後台 Timer 繼續運作
      ShowWindow(hWnd, SW_HIDE);
      break;
    }
    }
    break;
  }
  }
  return FALSE;
}

// ---------------------------------------------------------------------------
// 外部導出/呼叫控制介面
// ---------------------------------------------------------------------------

/**
 * @brief 建立輔助視窗 (單例模式)
 * @return bool 成功建立或已存在回傳 true
 */
bool CreateHelperDialog() {
  if (hWndHelper != NULL)
    return true;

  hWndHelper = CreateDialog(hins, MAKEINTRESOURCE(IDD_DIALOG_MAIN), NULL, MainDlgProc);
  if (hWndHelper == NULL)
    return false;

  // 設定視窗為最上層顯示 (Topmost)
  SetWindowPos(hWndHelper, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
  return true;
}

/**
 * @brief 銷毀輔助視窗
 */
void DestroyHelperDialog() {
  if (IsWindow(hWndHelper))
    DestroyWindow(hWndHelper);
}

/**
 * @brief 切換顯示或隱藏輔助視窗 (常用於熱鍵觸發，例如按下 Home/End 鍵)
 */
void ShowOrHideHelperDialog() {
  if (!IsWindow(hWndHelper)) {
    if (!CreateHelperDialog())
      return;
    ShowWindow(hWndHelper, SW_SHOW);
    return;
  }

  if (IsWindowVisible(hWndHelper)) {
    ShowWindow(hWndHelper, SW_HIDE);
  } else {
    ShowWindow(hWndHelper, SW_SHOW);
  }
}
#endif // 0 -- HelperDlg 舊版 DMA/內聯組合語言輔助功能，已停用僅供參考