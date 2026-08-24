# 天堂 381 懶人包安裝檔（Inno Setup）

把客戶端打成一鍵安裝檔。來源預設：

`D:\天堂資料\天堂專案#380客戶端+自製登入器`

換包只改 [`Lineage381.iss`](Lineage381.iss) 開頭的 `#define SourceDir`。

## 編譯

1. 本機 Inno Setup 7 路徑：`D:\程式碼測試區\其他\Inno Setup 7`
2. 用 Compiler 開啟 `Lineage381.iss`，或命令列：

```text
"D:\程式碼測試區\其他\Inno Setup 7\ISCC.exe" Lineage381.iss
```

產出：`installer\output\天堂381安裝.exe`（已列入倉庫 `.gitignore`）。

介面使用 `Languages\ChineseTraditional.isl`（Inno Setup 7 已含此檔）。

## 安裝行為

| 項目 | 值 |
|------|-----|
| 預設目錄 | `{系統碟}\天堂381` |
| 權限 | 系統管理員 |
| 捷徑 | `{app}\Core\LinLauncher.exe` |

若雙擊登入器沒視窗，請裝 [.NET Desktop Runtime x86（.NET 10）](https://dotnet.microsoft.com/download/dotnet/10.0)。

## 排除清單

**必排**

- `*.loc`（含 `完族測試.loc`）
- `*.log`（含 `Core\launcher.log`）
- `log\*`（`LinError_*.txt` 整夾）
- `*.WebView2` 快取目錄
- `*.pdb`
- `createdump.exe`、`*.tmp`、`.eat_pending`、`*.pending`

**建議排除（腳本預設已開）**

- `eat.exe`、`EatPack.exe`
- 散檔目錄 `icon\`、`sprite\`、`Surf\`、`text\`、`Tile\`（根目錄 `*.pak` / `*.idx` 仍會打進包）

**會打進包**

- `Sprite*.pak` / `idx`、`Text.pak`、`Tile.pak`、`TW13081901.bin`
- `Core\` 登入器、`LauncherDll.dll`、`config.dat`

卸載只刪安裝器寫入的檔，不會清玩家後來自己產生的 `*.loc`。

## 注意

lzma2 實心壓縮會讓編譯很久（數 GB pak）。本機測試檔不用先刪，排除規則只影響進安裝包的內容。
