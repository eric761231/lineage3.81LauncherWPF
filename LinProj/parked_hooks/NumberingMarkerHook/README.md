# NumberingMarkerHook（已自 LauncherDll 卸載）

隊伍快捷列「編號標記」（Action 5582）改開關／隊長攻擊目標標記用 hook。  
客戶端 Feature A 早已 no-op；整組原始碼改封存於此（同 LightStampHook）。

## 檔案
- `NumberingMarkerHook.h` / `NumberingMarkerHook.cpp`

## 說明文件
- `LinBin3.81/docs/hooks/NUMBERING_MARKER_TOGGLE_BRIEF.md`

## 若要重新接入
1. 拷回 `LinProj/LauncherDll/`
2. `LauncherDll.vcxproj` 加回 ClCompile / ClInclude
3. `LauncherDll.cpp`：`#include "NumberingMarkerHook.h"`，並在 `DelayedDetourThread` 呼叫 `InstallNumberingMarkerHook()`
4. 確認 `.cpp` 內 `InstallNumberingMarkerHook` 是否仍 early-return（官方 `0x62EC90`），再決定是否重開 Detour
