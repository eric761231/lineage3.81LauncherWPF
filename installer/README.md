# 天堂 381 懶人包安裝檔（Inno Setup）

兩份腳本：

| 腳本 | 用途 | 來源預設 | 啟動 |
|------|------|----------|------|
| [`Lineage381.iss`](Lineage381.iss) | 自製登入器（含 `Core\`） | `D:\天堂資料\天堂專案#380客戶端+自製登入器` | `Core\LinLauncher.exe` |
| [`Lineage381x.iss`](Lineage381x.iss) | 測試懶人包（無 Core） | `D:\天堂資料\3.81測試懶人包` | `Login.exe` |

換包只改對應 `.iss` 開頭的 `#define SourceDir`。

## 編譯

1. 本機 Inno Setup 7 路徑：`D:\程式碼測試區\其他\Inno Setup 7`
2. 用 Compiler 開啟要打的 `.iss`，或命令列：

```text
"D:\程式碼測試區\其他\Inno Setup 7\ISCC.exe" Lineage381x.iss
```

產出在 `installer\output\`（已列入倉庫 `.gitignore`）。

介面使用 `Languages\ChineseTraditional.isl`（Inno Setup 7 已含此檔）。

## 安裝行為（懶人包 Lineage381x）

| 項目 | 值 |
|------|-----|
| 預設目錄 | `{系統碟}\天堂381` |
| 權限 | 系統管理員 |
| 捷徑 | `{app}\Login.exe` |

## 排除清單

**必排**

- `*.loc`、`*.log`、`log\*`、`*.pdb`、`*.tmp`、`.eat_pending`、`*.pending`
- `Capture\*`（擷圖目錄）
- `createdump.exe`；另排 `eat.exe`、`EatPack.exe`

**建議排除（腳本預設已開）**

- 散檔目錄 `icon\`、`sprite\`、`Surf\`、`text\`、`Tile\`（根目錄 `*.pak` / `*.idx` 仍會打進包）

**自製登入器版另排**

- `*.WebView2` 快取目錄

卸載只刪安裝器寫入的檔，不會清玩家後來自己產生的 `*.loc`。

## 注意

lzma2 實心壓縮會讓編譯很久（數 GB pak）。本機測試檔不用先刪，排除規則只影響進安裝包的內容。
