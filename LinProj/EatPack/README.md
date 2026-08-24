# EatPack

獨立命令列吃檔工具，專案在 `LinProj/EatPack`。登入器內建吃檔是 `LinLauncher/Services/EatService.cs`，兩邊邏輯對齊，不要再從測試客戶端目錄維護一份。

對應客戶端 `eat.exe` / `eat.dll`：把散檔寫進 idx/pak。

在 solution 根目錄建置：

```text
dotnet build LinProj381.sln -c Release
dotnet run --project EatPack -- -d "C:\3.81Lineage自製登入器測試用"
```

## 用法

在客戶端根目錄（有 `Sprite.idx`、`Text.idx`、`Tile.idx` 的地方）執行：

```text
EatPack.exe
EatPack.exe --dry-run
EatPack.exe --keep
EatPack.exe -d "C:\3.81Lineage自製登入器測試用"
```

需要已安裝 [.NET 8 Runtime](https://dotnet.microsoft.com/download/dotnet/8.0)。

## 目錄對應

| 資料夾 | 副檔名 | 目標 |
|--------|--------|------|
| `icon` | `.tbt` `.ico` | `Sprite.idx` / `Sprite00.idx`～`Sprite15.idx` |
| `sprite` | `.spr` `.png` | 同上 |
| `Surf` | `.img` | 同上 |
| `text` | `.html` `.tbl` `.txt`、以及 `list.spr` `list.spz` | `Text.idx`（L1 加密） |
| `Tile` | `.til` `.xml` | `Tile.idx` |

idx 檔名欄位只有 **20 bytes**。超過的名稱會被截斷，與原廠 eat 相同。例如 `orcfhuwoomoscroll-c.html` 在 idx 裡是 `orcfhuwoomoscroll-c.`。

已存在的檔會**更新**（資料接到 pak 尾端並改 idx 位移）；新檔會**新增**。Sprite 分卷若接近 1.9GB 會改寫入下一個還有空間的 `SpriteNN.pak`。

預設吃完會刪散檔；`--keep` 可保留。
