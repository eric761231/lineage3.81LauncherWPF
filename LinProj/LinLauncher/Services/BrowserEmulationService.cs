// BrowserEmulationService.cs: 讓遊戲內嵌的 IWebBrowser2 控制項改用新版 IE11
// 引擎渲染，取代 Windows 對未登記程式的預設 IE7 相容模式。
using System;
using Microsoft.Win32;

namespace LinLauncher.Services
{
    public static class BrowserEmulationService
    {
        private const string SubKey =
            @"Software\Microsoft\Internet Explorer\Main\FeatureControl\FEATURE_BROWSER_EMULATION";

        // IE11 Edge 模式：沿用網頁本身的 doctype/X-UA-Compatible 判斷文件模式，
        // 但引擎本身用最新的 IE11（Trident/Chakra），可以解析 let/const/箭頭
        // 函式/樣板字串等 ES6 語法，不會再卡在預設的 IE7 相容模式。
        private const int Ie11EdgeMode = 11001;

        public static void ApplyForLaunch(string exePath)
        {
            try
            {
                string exeName = System.IO.Path.GetFileName(exePath);
                if (string.IsNullOrEmpty(exeName)) return;
                EnsureBrowserEmulation(exeName, Ie11EdgeMode);
            }
            catch (Exception ex)
            {
                LogService.Warn($"[browser-emulation] ApplyForLaunch failed: {ex.Message}");
            }
        }

        private static void EnsureBrowserEmulation(string exeFileName, int mode)
        {
            using RegistryKey? key = Registry.CurrentUser.CreateSubKey(SubKey, writable: true);
            if (key == null)
                throw new InvalidOperationException("無法開啟 FeatureControl\\FEATURE_BROWSER_EMULATION");

            object? existing = key.GetValue(exeFileName);
            if (existing is int existingInt && existingInt == mode)
                return;

            key.SetValue(exeFileName, mode, RegistryValueKind.DWord);
            LogService.Info($"[browser-emulation] set {exeFileName} => {mode}");
        }
    }
}
