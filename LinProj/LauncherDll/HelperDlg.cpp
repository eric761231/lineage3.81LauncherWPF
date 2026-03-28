// HelperDlg.cpp: 遊戲內輔助視窗的 UI 與邏輯實作。
#include "stdafx.h" // IWYU pragma: keep
#include "HelperDlg.h"

typedef struct {
  int id;
  char name[128];
} ITEM_INFO;

#pragma pack(push, 1)
typedef struct {
  int unknow1;
  DWORD id;
  int unknow2;
  char *name;
} BAGITEM_INFO;
#pragma pack(pop)

HWND hWndHelper = NULL;
HWND hPage[2];
RECT rcTabCtrl;
HWND hComboBoxHP[6];

std::vector<ITEM_INFO> vtHPItemList;
std::vector<ITEM_INFO> vtHPMPItemList;
std::vector<ITEM_INFO> vtDeleteItemList;

_TCHAR szTabTitle[2][32] = {_T("��ˮ"), _T("�h��")};

// 從遊戲記憶體讀取當前 HP。
int GetHP() {
  DWORD hp = 0;
  __try {
    __asm
    {
			mov eax, 0x00BDC828
			mov ecx, dword ptr ds:[eax]
			xor ecx, 0xC0017921
			mov ebx, dword ptr ds:[eax+4]
			mov edx, dword ptr ds:[ebx+ecx*4]
			xor edx, dword ptr ds:[eax+8]
			mov hp, edx
    }
  } __except (1) {
    hp = 0;
  }
  return hp;
}

// 從遊戲記憶體讀取當前 MP。
int GetMP() {
  DWORD mp = 0;
  __try {
    __asm
    {
			mov eax, 0x00BDC834
			mov ecx, dword ptr ds:[eax]
			xor ecx, 0xC0017921
			mov ebx, dword ptr ds:[eax+4]
			mov edx, dword ptr ds:[ebx+ecx*4]
			xor edx, dword ptr ds:[eax+8]
			mov mp, edx
    }
  } __except (1) {
    mp = 0;
  }
  return mp;
}

// 取得背包物品數量。
int GetItemCount() {
  int count = 0;
  __try {
    int a = *(int *)0x009A9250;
    count = *(int *)(a + 0x2C);
  } __except (1) {
    count = 0;
  }
  return count;
}

// 依索引取得背包物品資訊。
BAGITEM_INFO *GetItem(int index) {
  BAGITEM_INFO *pBagItem = NULL;
  __try {
    DWORD a = *(DWORD *)0x009A9250;
    a = *(DWORD *)(a + 0x58);
    a = *(DWORD *)(a + index * 4);
    pBagItem = (BAGITEM_INFO *)a;
  } __except (1) {
    pBagItem = NULL;
  }
  return pBagItem;
}

// 透過遊戲內呼叫鏈使用指定道具 ID。
void UseItem(DWORD id) {
  __asm
      {
		push 0x008D0733
		push id
		push 0xA4
		push 0x008D07EC
		mov eax, 0x00580E50
		call eax
		add esp, 0x10
      }
}

// 依道具名稱遍歷背包並使用。
void UseItem(const char *name) {
  int count = GetItemCount();
  for (int i = 0; i < count; i++) {
    BAGITEM_INFO *pBagItem = GetItem(i);
    if (pBagItem == NULL)
      continue;
    if (_stricmp(pBagItem->name, name) == 0) {
      UseItem(pBagItem->id);
    }
  }
}

// 透過遊戲內呼叫鏈刪除指定道具 ID。
void DeleteItem(DWORD id) {
  __asm
      {
		push 0
		push id
		push 0x8A
		push 0x008D0978
		mov eax, 0x00580E50
		call eax
		add esp, 0x10
      }
}

// 依道具名稱遍歷背包並刪除。
void DeleteItem(const char *name) {
  int count = GetItemCount();
  for (int i = 0; i < count; i++) {
    BAGITEM_INFO *pBagItem = GetItem(i);
    if (pBagItem == NULL)
      continue;
    if (_stricmp(pBagItem->name, name) == 0) {
      DeleteItem(pBagItem->id);
    }
  }
}

// 切換頁籤對應的子對話框。
void SelectPage(int Page) {
  if (Page == 0) {
    ShowWindow(hPage[0], SW_SHOW);
    ShowWindow(hPage[1], SW_HIDE);
  } else {
    ShowWindow(hPage[0], SW_HIDE);
    ShowWindow(hPage[1], SW_SHOW);
  }
}

// 檢查保護條件：依 HP/MP 門檻自動使用道具。
void CheckProtect(HWND hWnd) {
  int hp, mp, index;
  BOOL translated;
  int curhp = GetHP();
  int curmp = GetMP();
  if (IsDlgButtonChecked(hWnd, IDC_CHECK7)) {
    hp = GetDlgItemInt(hWnd, IDC_EDIT7, &translated, FALSE);
    mp = GetDlgItemInt(hWnd, IDC_EDIT8, &translated, FALSE);
    if (curhp > hp && curmp < mp) {
      index = SendDlgItemMessage(hWnd, IDC_COMBO7, CB_GETCURSEL, 0, 0);
      if (index != CB_ERR) {
        UseItem(vtHPMPItemList[index].name);
      }
    }
  }

  if (IsDlgButtonChecked(hWnd, IDC_CHECK1)) {
    hp = GetDlgItemInt(hWnd, IDC_EDIT1, &translated, FALSE);
    if (hp > 0 && curhp < hp) {
      index = SendDlgItemMessage(hWnd, IDC_COMBO1, CB_GETCURSEL, 0, 0);
      if (index != CB_ERR) {
        UseItem(vtHPItemList[index].name);
      }
      // return;
    }
  }

  if (IsDlgButtonChecked(hWnd, IDC_CHECK2)) {
    hp = GetDlgItemInt(hWnd, IDC_EDIT2, &translated, FALSE);
    if (hp > 0 && curhp < hp) {
      index = SendDlgItemMessage(hWnd, IDC_COMBO2, CB_GETCURSEL, 0, 0);
      if (index != CB_ERR) {
        UseItem(vtHPItemList[index].name);
      }
      // return;
    }
  }

  if (IsDlgButtonChecked(hWnd, IDC_CHECK3)) {
    hp = GetDlgItemInt(hWnd, IDC_EDIT3, &translated, FALSE);
    if (hp > 0 && curhp < hp) {
      index = SendDlgItemMessage(hWnd, IDC_COMBO3, CB_GETCURSEL, 0, 0);
      if (index != CB_ERR) {
        UseItem(vtHPItemList[index].name);
      }
      // return;
    }
  }

  if (IsDlgButtonChecked(hWnd, IDC_CHECK4)) {
    hp = GetDlgItemInt(hWnd, IDC_EDIT4, &translated, FALSE);
    if (hp > 0 && curhp < hp) {
      index = SendDlgItemMessage(hWnd, IDC_COMBO4, CB_GETCURSEL, 0, 0);
      if (index != CB_ERR) {
        UseItem(vtHPItemList[index].name);
      }
      // return;
    }
  }

  if (IsDlgButtonChecked(hWnd, IDC_CHECK5)) {
    hp = GetDlgItemInt(hWnd, IDC_EDIT5, &translated, FALSE);
    if (hp > 0 && curhp < hp) {
      index = SendDlgItemMessage(hWnd, IDC_COMBO5, CB_GETCURSEL, 0, 0);
      if (index != CB_ERR) {
        UseItem(vtHPItemList[index].name);
      }
      // return;
    }
  }

  if (IsDlgButtonChecked(hWnd, IDC_CHECK6)) {
    hp = GetDlgItemInt(hWnd, IDC_EDIT6, &translated, FALSE);
    if (hp > 0 && curhp < hp) {
      index = SendDlgItemMessage(hWnd, IDC_COMBO6, CB_GETCURSEL, 0, 0);
      if (index != CB_ERR) {
        UseItem(vtHPItemList[index].name);
      }
      // return;
    }
  }
}

// 檢查刪除清單：自動刪除命中的道具。
void CheckDeleteItem(HWND hWnd) {
  if (IsDlgButtonChecked(hWnd, IDC_CHECK1)) {
    int cnt = SendDlgItemMessage(hWnd, IDC_LIST1, LB_GETCOUNT, 0, 0);
    if (cnt == LB_ERR)
      return;

    for (int i = 0; i < cnt; i++) {
      char text[MAX_PATH];
      memset(text, 0, MAX_PATH);
      int result =
          SendDlgItemMessageA(hWnd, IDC_LIST1, LB_GETTEXT, i, (LPARAM)text);
      if (result == LB_ERR)
        continue;

      DeleteItem(text);
    }
  }
}

INT_PTR CALLBACK ProtectDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam,
                                LPARAM lParam) {
  switch (uMsg) {
  case WM_INITDIALOG: {
    // 載入保護配置並填充各下拉選單。
    const char *app = "AllHP";
    const char *file = "./LinHelperZ.ini";
    vtHPItemList.clear();
    vtHPMPItemList.clear();
    char key[MAX_PATH];
    ITEM_INFO ItemInfo;
    for (int i = 0; i < 99; i++) {
      memset(&ItemInfo, 0, sizeof(ItemInfo));
      sprintf_s(key, "Item%d", i);
      int len =
          GetPrivateProfileStringA(app, key, "", ItemInfo.name, 128, file);
      if (len <= 0)
        break;
      vtHPItemList.push_back(ItemInfo);
    }

    hComboBoxHP[0] = GetDlgItem(hWnd, IDC_COMBO1);
    hComboBoxHP[1] = GetDlgItem(hWnd, IDC_COMBO2);
    hComboBoxHP[2] = GetDlgItem(hWnd, IDC_COMBO3);
    hComboBoxHP[3] = GetDlgItem(hWnd, IDC_COMBO4);
    hComboBoxHP[4] = GetDlgItem(hWnd, IDC_COMBO5);
    hComboBoxHP[5] = GetDlgItem(hWnd, IDC_COMBO6);

    for (size_t i = 0; i < vtHPItemList.size(); i++) {
      for (int j = 0; j < 6; j++) {
        SendMessageA(hComboBoxHP[j], CB_ADDSTRING, 0,
                     (LPARAM)vtHPItemList[i].name);
      }
    }

    for (int i = 0; i < 99; i++) {
      memset(&ItemInfo, 0, sizeof(ItemInfo));
      sprintf_s(key, "HPMP%d", i);
      int len =
          GetPrivateProfileStringA(app, key, "", ItemInfo.name, 128, file);
      if (len <= 0)
        break;
      vtHPMPItemList.push_back(ItemInfo);
    }

    HWND hComboBox = GetDlgItem(hWnd, IDC_COMBO7);
    for (size_t i = 0; i < vtHPMPItemList.size(); i++) {
      SendMessageA(hComboBox, CB_ADDSTRING, 0, (LPARAM)vtHPMPItemList[i].name);
    }

    SetTimer(hWnd, 1000, 1000, NULL);

    break;
  }
  case WM_TIMER: {
    // 以定時器驅動保護邏輯。
    CheckProtect(hWnd);
    break;
  }
  }
  return FALSE;
}

void SaveDeleteItem(HWND hWnd) {
  // 把刪除清單持久化到 ini，便於下次啟動恢復。
  int count = SendMessage(hWnd, LB_GETCOUNT, 0, 0);
  _TCHAR item[MAX_PATH];
  _TCHAR key[MAX_PATH];
  for (int i = 0; i < count; i++) {
    int result = SendMessage(hWnd, LB_GETTEXT, i, (LPARAM)item);
    if (result != LB_ERR) {
      _stprintf_s(key, _T("Item%d"), i);
      WritePrivateProfileString(_T("DeleteItem"), key, item,
                                _T("./LinHelperZ.ini"));
    }
  }
}

INT_PTR CALLBACK DeleteDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam,
                               LPARAM lParam) {
  switch (uMsg) {
  case WM_INITDIALOG: {
    // 初始化刪除清單分頁並讀取既有配置。
    const _TCHAR *app = _T("DeleteItem");
    const _TCHAR *file = _T("./LinHelperZ.ini");
    _TCHAR key[MAX_PATH];
    _TCHAR item[MAX_PATH];
    for (int i = 0; i < 999; i++) {
      _stprintf_s(key, _T("Item%d"), i);
      int len = GetPrivateProfileString(app, key, _T(""), item, MAX_PATH, file);
      if (len <= 0)
        break;
      SendDlgItemMessage(hWnd, IDC_LIST1, CB_ADDSTRING, 0, (LPARAM)item);
    }

    SetTimer(hWnd, 1001, 1000, NULL);

    break;
  }
  case WM_COMMAND: {
    switch (LOWORD(wParam)) {
    case IDC_BUTTON1: {
      _TCHAR text[MAX_PATH];
      GetDlgItemText(hWnd, IDC_COMBO1, text, MAX_PATH);
      int index =
          SendDlgItemMessage(hWnd, IDC_LIST1, LB_FINDSTRING, -1, (LPARAM)text);
      if (index == LB_ERR) {
        SendDlgItemMessage(hWnd, IDC_LIST1, LB_ADDSTRING, 0, (LPARAM)text);
        SaveDeleteItem(GetDlgItem(hWnd, IDC_LIST1));
      }
      break;
    }
    case IDC_BUTTON2: {
      int index = SendDlgItemMessage(hWnd, IDC_LIST1, LB_GETCURSEL, 0, 0);
      if (index != LB_ERR) {
        SendDlgItemMessage(hWnd, IDC_LIST1, LB_DELETESTRING, index, 0);
        SaveDeleteItem(GetDlgItem(hWnd, IDC_LIST1));
      }
      break;
    }
    case IDC_COMBO1: {
      if (HIWORD(wParam) == CBN_DROPDOWN) {
        SendDlgItemMessage(hWnd, IDC_COMBO1, CB_RESETCONTENT, 0, 0);
        int cnt = GetItemCount();
        for (int i = 0; i < cnt; i++) {
          BAGITEM_INFO *pBagItem = GetItem(i);
          if (pBagItem)
            SendDlgItemMessageA(hWnd, IDC_COMBO1, CB_ADDSTRING, 0,
                                (LPARAM)pBagItem->name);
        }
      }
      break;
    }
    }
  }
  case WM_TIMER: {
    // 以定時器驅動自動刪除檢查。
    CheckDeleteItem(hWnd);
    break;
  }
  }
  return FALSE;
}

INT_PTR CALLBACK MainDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam,
                             LPARAM lParam) {
  switch (uMsg) {
  case WM_INITDIALOG: {
    // 建立兩個分頁子視窗並掛到主 Tab 控制項下。
    GetClientRect(GetDlgItem(hWnd, IDC_TAB1), &rcTabCtrl);
    rcTabCtrl.top += 25;
    rcTabCtrl.left += 4;
    rcTabCtrl.right -= 4;
    rcTabCtrl.bottom -= 4;

    hPage[0] = CreateDialog(hins, MAKEINTRESOURCE(IDD_DIALOG_PAGE1),
                            GetDlgItem(hWnd, IDC_TAB1), ProtectDlgProc);
    hPage[1] = CreateDialog(hins, MAKEINTRESOURCE(IDD_DIALOG_PAGE2),
                            GetDlgItem(hWnd, IDC_TAB1), DeleteDlgProc);

    MoveWindow(hPage[0], rcTabCtrl.left, rcTabCtrl.top,
               rcTabCtrl.right - rcTabCtrl.left,
               rcTabCtrl.bottom - rcTabCtrl.top, TRUE);
    MoveWindow(hPage[1], rcTabCtrl.left, rcTabCtrl.top,
               rcTabCtrl.right - rcTabCtrl.left,
               rcTabCtrl.bottom - rcTabCtrl.top, TRUE);

    SelectPage(0);

    TCITEM tie;
    tie.mask = TCIF_TEXT;
    for (int i = 0; i < 2; i++) {
      tie.pszText = szTabTitle[i];
      SendDlgItemMessage(hWnd, IDC_TAB1, TCM_INSERTITEM, i, (LPARAM)&tie);
    }
    break;
  }
  case WM_NOTIFY: {
    if (((LPNMHDR)lParam)->code == TCN_SELCHANGE) {
      int i = SendDlgItemMessage(hWnd, IDC_TAB1, TCM_GETCURSEL, 0, 0);
      switch (i) {
      case 0:
        SelectPage(0);
        break;
      case 1:
        SelectPage(1);
        break;
      }
    }
    break;
  }
  case WM_SYSCOMMAND: {
    switch (wParam) {
    case SC_CLOSE: {
      ShowWindow(hWnd, SW_HIDE);
      break;
    }
    }
    break;
  }
  }
  return FALSE;
}

bool CreateHelperDialog() {
  // 單例建立，避免重複建立多個輔助視窗。
  if (hWndHelper != NULL)
    return true;

  hWndHelper =
      CreateDialog(hins, MAKEINTRESOURCE(IDD_DIALOG_MAIN), NULL, MainDlgProc);
  if (hWndHelper == NULL)
    return false;

  // �ö�
  SetWindowPos(hWndHelper, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
  return true;
}

void DestroyHelperDialog() {
  if (IsWindow(hWndHelper))
    DestroyWindow(hWndHelper);
}

void ShowOrHideHelperDialog() {
  // 若視窗尚未建立，先建立再顯示。
  if (!IsWindow(hWndHelper)) {
    if (!CreateHelperDialog())
      return;
    ShowWindow(hWndHelper, SW_SHOW);
    return;
  }

  // 以顯示/隱藏切換，不銷毀視窗以保留當前狀態。
  if (IsWindowVisible(hWndHelper)) {
    ShowWindow(hWndHelper, SW_HIDE);
  } else {
    ShowWindow(hWndHelper, SW_SHOW);
  }
}