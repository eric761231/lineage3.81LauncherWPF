# 📄 HelperDlg.cpp 技術分析與安全性評估報告

---

## 一、 程式碼架構與模組說明

本檔案主要實作遊戲內掛（DLL Injection 形式）的 **UI 互動** 與 **自動化邏輯**。

```text
[ DLL 注入 / 主線程 ]
       │
       ├── ShowOrHideHelperDialog() ─── CreateDialog (MainDlgProc)
       │                                     │
       │                         ┌───────────┴───────────┐
       │                         ▼                       ▼
       │                  ProtectDlgProc           DeleteDlgProc
       │                  (自動補血/補魔)           (自動刪除物品)
       │                         │                       │
       │                         ▼                       ▼
       │                   WM_TIMER (1s)           WM_TIMER (1s)
       │                         │                       │
       │                         ▼                       ▼
       └─────────────────► 遊戲記憶體與 Call 呼叫 ◄─────────┘
                           ├─ GetHP() / GetMP() (讀取 DMA 加密記憶體)
                           ├─ GetItemCount() / GetItem() (遍歷背包)
                           ├─ UseItem() (內聯彙編直接呼叫遊戲 Call)
                           └─ DeleteItem() (內聯彙編直接呼叫遊戲 Call)