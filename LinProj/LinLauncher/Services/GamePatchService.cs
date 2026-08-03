// GamePatchService.cs: Stage2 記憶體修補 — 補齊 LauncherDll 目前沒有的功能
// （AC 偵測繞過 / img·png 圖檔上限 / 背包顯示上限 / 移動封包不加密）。
// 逐項對照移植自 Rust 版 src/patch.rs，技術路線比照原版：外部行程
// ReadProcessMemory/WriteProcessMemory + 特徵碼掃描，不透過 LauncherDll。
//
// 每一步都先讀值核對，不符合預期就記錄警告並跳過（fail-soft），不強行寫入 —
// 這點跟 patch.rs 的設計一致，避免版本不符時把遊戲行程寫壞。
using System;
using System.Threading;
using System.Threading.Tasks;

namespace LinLauncher.Services
{
    public static class GamePatchService
    {
        private const int DecryptWaitTimeoutMs = 120_000;
        private const int DecryptPollIntervalMs = 50;

        /// <summary>
        /// 等待 packer 解密完成，然後套用 Stage2 補齊修補。應在 DLL 注入 + ResumeThread 之後、
        /// 背景執行（不阻塞 UI），失敗不影響遊戲正常啟動（僅記錄警告）。
        /// 內部自行開啟／關閉 process handle，不依賴呼叫端傳入的 handle 生命週期。
        /// </summary>
        public static Task ApplyGapFillPatchesAsync(uint pid)
        {
            return Task.Run(() =>
            {
                IntPtr hProcess = NativeMethods.OpenProcess(NativeMethods.ProcessAccessFlags.All, false, pid);
                if (hProcess == IntPtr.Zero)
                {
                    LogService.Warn($"[GamePatch] OpenProcess 失敗 (pid={pid})，跳過 Stage2 補齊修補");
                    return;
                }

                try
                {
                    var mem = new GameMemoryService(hProcess);
                    if (!WaitForDecryptGate(mem))
                    {
                        LogService.Warn("[GamePatch] 等待 packer 解密逾時，跳過 Stage2 補齊修補");
                        return;
                    }

                    LogService.Info("[GamePatch] packer 已解密，開始套用 Stage2 補齊修補");
                    PatchAcCheck(mem);
                    PatchImgLimit(mem, GamePatchSpecs.ImgLimitMax);
                    PatchPngLimit(mem, GamePatchSpecs.ImgLimitMax);
                    PatchInventoryLimit(mem, GamePatchSpecs.InventoryLimitMax);
                    PatchMovePacketNoEncrypt(mem);
                    LogService.Info("[GamePatch] Stage2 補齊修補流程結束");
                }
                catch (Exception ex)
                {
                    LogService.Error("[GamePatch] Stage2 補齊修補發生例外（不影響遊戲繼續執行）", ex);
                }
                finally
                {
                    NativeMethods.CloseHandle(hProcess);
                }
            });
        }

        /// <summary>輪詢 decrypt-gate（唯讀，不寫入 — 對齊 patch.rs::try_wait_and_patch 的等待邏輯）。</summary>
        private static bool WaitForDecryptGate(GameMemoryService mem)
        {
            var start = DateTime.UtcNow;
            while ((DateTime.UtcNow - start).TotalMilliseconds < DecryptWaitTimeoutMs)
            {
                if (mem.TryReadUInt32(GamePatchSpecs.DecryptGateAddr, out uint val))
                {
                    if (val == GamePatchSpecs.DecryptGateExpected || val == GamePatchSpecs.DecryptGateAlreadyPatchedByRust)
                    {
                        LogService.Info($"[GamePatch] decrypt-gate 就緒 @ 0x{GamePatchSpecs.DecryptGateAddr:X8}=0x{val:X8}");
                        return true;
                    }
                }
                Thread.Sleep(DecryptPollIntervalMs);
            }
            return false;
        }

        private static void PatchAcCheck(GameMemoryService mem)
        {
            var hit1 = mem.ScanPattern(GamePatchSpecs.TextScanStart, GamePatchSpecs.TextScanEnd, GamePatchSpecs.AcCheckPattern1);
            if (hit1.HasValue)
            {
                uint jzAddr = hit1.Value + GamePatchSpecs.AcCheckPattern1JzOffset;
                if (mem.WriteCode(jzAddr, new byte[] { 0xEB }))
                    LogService.Info($"[GamePatch] AC 檢查1（CRC比較）已繞過 @ 0x{jzAddr:X8}");
            }
            else
            {
                LogService.Warn("[GamePatch] 找不到 AC 檢查1 特徵碼，跳過");
            }

            var hit2 = mem.ScanPattern(GamePatchSpecs.TextScanStart, GamePatchSpecs.TextScanEnd, GamePatchSpecs.AcCheckPattern2);
            if (hit2.HasValue)
            {
                uint jzAddr = hit2.Value + GamePatchSpecs.AcCheckPattern2JzOffset;
                if (mem.WriteCode(jzAddr, new byte[] { 0xEB }))
                    LogService.Info($"[GamePatch] AC 檢查2（固定hash）已繞過 @ 0x{jzAddr:X8}");
            }
            else
            {
                LogService.Warn("[GamePatch] 找不到 AC 檢查2 特徵碼，跳過");
            }
        }

        private static void PatchImgLimit(GameMemoryService mem, uint newLimit)
        {
            newLimit = Math.Clamp(newLimit, GamePatchSpecs.ImgLimitMin, GamePatchSpecs.ImgLimitMax);
            byte[] newBytes = BitConverter.GetBytes(newLimit);
            byte[] newAlloc = BitConverter.GetBytes(newLimit * 4);
            int count = 0;

            foreach (uint hit in mem.ScanPatternAll(GamePatchSpecs.ImgLimitScanStart, GamePatchSpecs.ImgLimitScanEnd, GamePatchSpecs.ImgLimitRangePattern))
            {
                if (mem.WriteCode(hit + GamePatchSpecs.ImgLimitRangePatchOffset, newBytes)) count++;
            }
            foreach (uint hit in mem.ScanPatternAll(GamePatchSpecs.ImgLimitScanStart, GamePatchSpecs.ImgLimitScanEnd, GamePatchSpecs.ImgLimitAllocPattern))
            {
                if (mem.WriteCode(hit + GamePatchSpecs.ImgLimitAllocPatchOffset, newAlloc)) count++;
            }

            if (count == 0)
                LogService.Warn("[GamePatch] img 上限：未找到任何修補位置");
            else
                LogService.Info($"[GamePatch] img 圖檔上限突破：{count} 處修補（目標 {newLimit}）");
        }

        private static void PatchPngLimit(GameMemoryService mem, uint newLimit)
        {
            byte[] newLimitBytes = BitConverter.GetBytes(newLimit);
            byte[] newAllocBytes = BitConverter.GetBytes(newLimit * 4);
            int count = 0;

            var allocHit = mem.ScanPattern(GamePatchSpecs.TextScanStart, GamePatchSpecs.TextScanEnd, GamePatchSpecs.PngLimitAllocPattern);
            if (allocHit.HasValue && mem.WriteCode(allocHit.Value + GamePatchSpecs.PngLimitAllocPatchOffset, newAllocBytes))
                count++;

            var initHit = mem.ScanPattern(GamePatchSpecs.TextScanStart, GamePatchSpecs.TextScanEnd, GamePatchSpecs.PngLimitInitLoopPattern);
            if (initHit.HasValue && mem.WriteCode(initHit.Value + GamePatchSpecs.PngLimitInitLoopPatchOffset, newLimitBytes))
                count++;

            var cleanupHit = mem.ScanPattern(GamePatchSpecs.TextScanStart, GamePatchSpecs.TextScanEnd, GamePatchSpecs.PngLimitCleanupLoopPattern);
            if (cleanupHit.HasValue && mem.WriteCode(cleanupHit.Value + GamePatchSpecs.PngLimitCleanupLoopPatchOffset, newLimitBytes))
                count++;

            if (count == 0)
                LogService.Warn("[GamePatch] png 上限：3 個特徵碼皆未套用成功");
            else
                LogService.Info($"[GamePatch] png 圖檔上限突破：{count}/3 處修補（目標 {newLimit}）");
        }

        private static void PatchInventoryLimit(GameMemoryService mem, uint newLimit)
        {
            newLimit = Math.Clamp(newLimit, GamePatchSpecs.InventoryLimitMin, GamePatchSpecs.InventoryLimitMax);
            var hit = mem.ScanPattern(GamePatchSpecs.InventoryLimitScanStart, GamePatchSpecs.InventoryLimitScanEnd, GamePatchSpecs.InventoryLimitPattern);
            if (!hit.HasValue)
            {
                LogService.Warn("[GamePatch] 找不到背包顯示上限格式字串，跳過");
                return;
            }

            string digits = newLimit.ToString();
            byte[] bytes = { 0x20, 0x20, 0x20 }; // 空白填充，對齊 Rust 固定 3 bytes 緩衝
            for (int i = 0; i < digits.Length && i < 3; i++)
                bytes[i] = (byte)digits[i];

            if (mem.WriteCode(hit.Value + GamePatchSpecs.InventoryLimitPatchOffset, bytes))
                LogService.Info($"[GamePatch] 背包顯示上限：180 → {newLimit}");
        }

        private static void PatchMovePacketNoEncrypt(GameMemoryService mem)
        {
            int patched = 0;

            var stateHit = mem.ScanPattern(GamePatchSpecs.TextScanStart, GamePatchSpecs.TextScanEnd, GamePatchSpecs.MoveStateObfuscationPattern);
            if (stateHit.HasValue)
            {
                uint addr = stateHit.Value + GamePatchSpecs.MoveStateObfuscationOffset;
                if (mem.TryReadBytes(addr, 1, out var cur) && cur[0] == GamePatchSpecs.MoveStateObfuscationOriginal)
                {
                    if (mem.WriteCode(addr, new[] { GamePatchSpecs.MoveStateObfuscationPatched })) patched++;
                }
                else if (cur.Length == 1 && cur[0] == GamePatchSpecs.MoveStateObfuscationPatched)
                {
                    patched++; // 已修補過
                }
                else
                {
                    LogService.Warn($"[GamePatch] move state obfuscation @ 0x{addr:X8} 目前值不符預期，跳過");
                }
            }
            else
            {
                LogService.Warn("[GamePatch] 找不到 move state obfuscation 特徵碼，跳過");
            }

            var encHit = mem.ScanPattern(GamePatchSpecs.TextScanStart, GamePatchSpecs.TextScanEnd, GamePatchSpecs.MovePacketEncryptionPattern);
            if (encHit.HasValue)
            {
                uint addr = encHit.Value + GamePatchSpecs.MovePacketEncryptionOffset;
                if (mem.TryReadBytes(addr, 1, out var cur) && cur[0] == GamePatchSpecs.MovePacketEncryptionOriginal)
                {
                    if (mem.WriteCode(addr, new[] { GamePatchSpecs.MovePacketEncryptionPatched })) patched++;
                }
                else if (cur.Length == 1 && cur[0] == GamePatchSpecs.MovePacketEncryptionPatched)
                {
                    patched++;
                }
                else
                {
                    LogService.Warn($"[GamePatch] move packet encryption @ 0x{addr:X8} 目前值不符預期，跳過");
                }
            }
            else
            {
                LogService.Warn("[GamePatch] 找不到 move packet encryption 特徵碼，跳過");
            }

            LogService.Info($"[GamePatch] 移動封包不加密：{patched}/2 處修補");
        }
    }
}
