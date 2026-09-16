# LightStampHook（已自 LauncherDll 卸載）

S_Light / 物件光罩 stamp 測試用 hook。實測放大表會不穩，已自建置移除，原始碼保留於此。

## 檔案
- `LightStampHook.h` / `LightStampHook.cpp`

## 說明文件
- `LinBin3.81/docs/hooks/LIGHT_STAMP_HOOK_BRIEF.md`

## 若要重新接入
1. 拷回 `LinProj/LauncherDll/`
2. `LauncherDll.vcxproj` 加回 ClCompile / ClInclude
3. `LauncherDll.cpp`：`#include "LightStampHook.h"`，並在 `DelayedDetourThread` 呼叫 `InstallLightStampHook()`
4. 保持 `kTestScale = 1.0f`（只 log）；勿再開 ×2 改表
