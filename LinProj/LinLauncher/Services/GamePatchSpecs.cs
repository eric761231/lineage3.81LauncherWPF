// GamePatchSpecs.cs: Stage2 記憶體修補的位址／特徵碼常數。
// 逐項移植自 Rust 版 src/patch.rs，只收錄「AC bypass / img 上限 / png 上限 /
// 背包顯示上限 / 移動封包不加密」這 5 類 — 這是 C# LauncherDll 目前完全沒有
// 涵蓋的功能缺口（見 docs/rust-vs-csharp-parity.md）。
//
// 每個位址/特徵碼都已用 LinBin3.81/verify_addresses.py 對照執行期記憶體 dump
// （TW13081901.DMP）驗證過，13/13 全數命中，詳見
// LinBin3.81/address_verification_report.md。
using static LinLauncher.Services.PatternByte;

namespace LinLauncher.Services
{
    internal static class GamePatchSpecs
    {
        // .text 段掃描範圍（對齊 patch.rs TEXT_SCAN_START/END）
        public const uint TextScanStart = 0x00401000;
        public const uint TextScanEnd = 0x00830000;

        // ── 解密完成偵測（唯讀輪詢，不寫入 —— LauncherDll 的 timeController.cpp
        //    已用 API hook 方式達成等價的時間保護繞過，這裡只用來確認 packer
        //    已經把 .text 解密完成，可以安全開始掃描/修補）──
        public const uint DecryptGateAddr = 0x004E204E;
        public const uint DecryptGateExpected = 0x0097850F;      // 解密完成、尚未被任何 launcher patch 過
        public const uint DecryptGateAlreadyPatchedByRust = 0x0097E990; // 若曾被 Rust 版 patch 過

        // ── AC（反外掛）偵測繞過：patch.rs::patch_ac_check ──
        // 檢查 1：CRC 比較 jz→jmp（patch 點 = AOB 命中 +12）
        public static readonly PatternByte[] AcCheckPattern1 =
        {
            B(0x89), B(0x45), B(0xFC), B(0x8B), B(0x45), B(0xFC), B(0x3B), B(0x05),
            Wildcard, Wildcard, Wildcard, Wildcard,
            B(0x74), B(0x3C), B(0x83), B(0x3D),
            Wildcard, Wildcard, Wildcard, Wildcard,
            B(0x03), B(0x75), B(0x33),
        };
        public const int AcCheckPattern1JzOffset = 12;

        // 檢查 2：固定 hash 比較 jz→jmp（patch 點 = AOB 命中 +8）
        public static readonly PatternByte[] AcCheckPattern2 =
        {
            B(0x83), B(0xC4), B(0x08), B(0x3D), B(0x67), B(0x59), B(0x00), B(0x00),
            B(0x74), B(0x2A),
        };
        public const int AcCheckPattern2JzOffset = 8;

        // ── img 圖檔上限突破：patch.rs::patch_img_limit ──
        public const uint ImgLimitScanStart = 0x00401000;
        public const uint ImgLimitScanEnd = 0x00800000;
        public const uint ImgLimitMin = 7000;
        public const uint ImgLimitMax = 50000;
        public const uint ImgLimitOldValue = 6295; // 0x1897

        // 第一層：資源範圍 push 7000 → push N（patch 點 = AOB 命中 +7，4 bytes）
        public static readonly PatternByte[] ImgLimitRangePattern =
        {
            B(0x6A), B(0x00), B(0x68), Wildcard, Wildcard, Wildcard, Wildcard,
            B(0x68), B(0x58), B(0x1B), B(0x00), B(0x00), B(0xE8),
        };
        public const int ImgLimitRangePatchOffset = 8; // 對齊 Rust：write_code(hit+8,...)

        // 第三層：陣列分配 push 25180 (6295*4) → push (N*4)
        public static readonly PatternByte[] ImgLimitAllocPattern =
        {
            B(0x68), B(0x5C), B(0x62), B(0x00), B(0x00),
        };
        public const int ImgLimitAllocPatchOffset = 1;

        // ── png 圖檔上限突破：patch.rs::patch_png_limit ──
        public const uint PngLimitOldLimit = 0x61C;  // 1564
        public const uint PngLimitOldAlloc = 0x1870; // 6256 = 1564*4

        public static readonly PatternByte[] PngLimitAllocPattern =
        {
            B(0x68), B(0x70), B(0x18), B(0x00), B(0x00), B(0xE8),
            Wildcard, Wildcard, Wildcard, Wildcard,
            B(0x83), B(0xC4), B(0x04),
        };
        public const int PngLimitAllocPatchOffset = 1;

        public static readonly PatternByte[] PngLimitInitLoopPattern =
        {
            B(0x89), B(0x55), B(0xF0), B(0x81), B(0x7D), B(0xF0),
            B(0x1C), B(0x06), B(0x00), B(0x00), B(0x7D),
        };
        public const int PngLimitInitLoopPatchOffset = 6;

        public static readonly PatternByte[] PngLimitCleanupLoopPattern =
        {
            B(0x89), B(0x4D), B(0xFC), B(0x81), B(0x7D), B(0xFC),
            B(0x1C), B(0x06), B(0x00), B(0x00), B(0x7D),
        };
        public const int PngLimitCleanupLoopPatchOffset = 6;

        // ── 背包顯示上限文字：patch.rs::patch_inventory_limit ──
        public const uint InventoryLimitScanStart = 0x00800000;
        public const uint InventoryLimitScanEnd = 0x00A00000;
        public const uint InventoryLimitMin = 180;
        public const uint InventoryLimitMax = 255;

        // 格式字串 "%d / 180\0"（patch 點 = AOB 命中 +5，寫入 3 bytes ASCII 數字）
        public static readonly PatternByte[] InventoryLimitPattern =
        {
            B(0x25), B(0x64), B(0x20), B(0x2F), B(0x20), B(0x31), B(0x38), B(0x30), B(0x00),
        };
        public const int InventoryLimitPatchOffset = 5;

        // ── 移動封包不加密：patch.rs::patch_move_packet_no_encrypt ──
        public static readonly PatternByte[] MoveStateObfuscationPattern =
        {
            B(0x0F), B(0xBE), B(0x42), B(0x14), B(0x83), B(0xF8), B(0x08), B(0x74), B(0x21),
            B(0x8B), B(0x0D), B(0xB8), B(0xD2), B(0xC2), B(0x00),
        };
        public const int MoveStateObfuscationOffset = 7;
        public const byte MoveStateObfuscationOriginal = 0x74;
        public const byte MoveStateObfuscationPatched = 0xEB;

        public static readonly PatternByte[] MovePacketEncryptionPattern =
        {
            B(0x0F), B(0xBE), B(0x15), B(0xE1), B(0xAE), B(0x9A), B(0x00), B(0x83), B(0xFA), B(0x03),
            B(0x75), B(0x22), B(0xA1), B(0xB8), B(0xD2), B(0xC2), B(0x00), B(0x0F), B(0xBE), B(0x48),
            B(0x15), B(0x83), B(0xF1), B(0x49),
        };
        public const int MovePacketEncryptionOffset = 10;
        public const byte MovePacketEncryptionOriginal = 0x75;
        public const byte MovePacketEncryptionPatched = 0xEB;
    }
}
