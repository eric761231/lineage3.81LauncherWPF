using System;
using System.Collections.Generic;
using System.Linq;

namespace LinEncoder.Services.SmoothRun;

// 低階字串解析輔助函式，逐一對照 Rust 參考
// L1J3.8Launcher(RUST)參考\src\smooth_run\helpers.rs。
//
// 全部用 char/index 操作、不用正則，是因為 Rust 原始碼本身是手寫 byte-scan，逐字對照才不會
// 行為漂移。呼叫端一律用 Encoding.Latin1 讀寫檔案（byte↔char 一對一），這裡的 index 運算才會
// 跟 Rust 的 byte index 完全對應。
internal static class SmoothRunHelpers
{
    public static uint AbsDiff(uint a, uint b) => a > b ? a - b : b - a;

    /// 提取括號內完整內容（含方向數+幀數+幀資料）。
    /// "0-1.RunL(1 8,64.0:2 64.1:2 ...)" → "1 8,64.0:2 64.1:2 ..."
    public static string ExtractFrameContent(string line)
    {
        string trimmed = line.TrimStart();
        int parenStart = trimmed.IndexOf('(');
        if (parenStart < 0) return "";
        int parenEnd = trimmed.LastIndexOf(')');
        if (parenEnd < 0) return "";
        if (parenEnd <= parenStart + 1) return "";
        return trimmed.Substring(parenStart + 1, parenEnd - parenStart - 1);
    }

    public static ushort? ParseSpriteId(string trimmed)
    {
        if (!trimmed.StartsWith("#", StringComparison.Ordinal)) return null;
        string afterHash = trimmed.Substring(1);
        int i = 0;
        while (i < afterHash.Length && char.IsAsciiDigit(afterHash[i])) i++;
        if (i == 0) return null;
        return ushort.TryParse(afterHash.Substring(0, i), out ushort v) ? v : null;
    }

    public static (uint Base, uint Variant)? ParseVariantLine(string trimmed)
    {
        int numEnd = 0;
        while (numEnd < trimmed.Length && char.IsAsciiDigit(trimmed[numEnd])) numEnd++;
        if (numEnd == 0) return null;
        if (!uint.TryParse(trimmed.Substring(0, numEnd), out uint baseNum)) return null;
        string afterNum = trimmed.Substring(numEnd);
        if (!afterNum.StartsWith("-", StringComparison.Ordinal)) return null;
        string afterDash = afterNum.Substring(1);
        int varEnd = 0;
        while (varEnd < afterDash.Length && char.IsAsciiDigit(afterDash[varEnd])) varEnd++;
        if (varEnd == 0) return null;
        if (!uint.TryParse(afterDash.Substring(0, varEnd), out uint variant)) return null;
        string afterVar = afterDash.Substring(varEnd);
        if (!afterVar.StartsWith(".", StringComparison.Ordinal)) return null;
        return (baseNum, variant);
    }

    public static uint? ParseActionNumber(string trimmed)
    {
        int numEnd = 0;
        while (numEnd < trimmed.Length && char.IsAsciiDigit(trimmed[numEnd])) numEnd++;
        if (numEnd == 0) return null;
        if (numEnd >= trimmed.Length || trimmed[numEnd] != '.') return null;
        return uint.TryParse(trimmed.Substring(0, numEnd), out uint v) ? v : null;
    }

    /// 從動作行提取動作名稱（"0.walkfastI(...)" → "walkfastI"）。
    public static string ExtractActionName(string trimmed)
    {
        int dotPos = trimmed.IndexOf('.');
        if (dotPos < 0) return "";
        string afterDot = trimmed.Substring(dotPos + 1);
        int parenPos = afterDot.IndexOf('(');
        if (parenPos < 0) parenPos = afterDot.Length;
        return afterDot.Substring(0, parenPos).Trim();
    }

    /// 天m 格式：解析 header 行的圖片數、圖檔 ID、名稱。
    /// `#14798 360=3213 LMS knight male_run` → (360, 3213, "LMS knight male_run")
    /// `#14491 448 Dragon_slayer_run` → (448, null, "Dragon_slayer_run")
    public static (uint ImgCount, uint? GfxId, string Name)? ParseHeaderExt(string trimmed)
    {
        if (!trimmed.StartsWith("#", StringComparison.Ordinal)) return null;
        string afterHash = trimmed.Substring(1);

        // 對應 Rust `.find(|c| !is_ascii_digit)?`：找不到非數字字元（即整段都是數字，或空字串）
        // 時整個函式回傳 null，不是把 idEnd 設成字串結尾。
        int idEnd = 0;
        while (idEnd < afterHash.Length && char.IsAsciiDigit(afterHash[idEnd])) idEnd++;
        if (idEnd == afterHash.Length) return null;

        string rest = afterHash.Substring(idEnd).TrimStart();
        if (rest.Length == 0) return null;

        // 對應 Rust `.find(...).unwrap_or(rest.len())`：找不到非數字時 numEnd = 整段長度（不是 null）。
        int numEnd = 0;
        while (numEnd < rest.Length && char.IsAsciiDigit(rest[numEnd])) numEnd++;
        if (numEnd == 0) return null;
        if (!uint.TryParse(rest.Substring(0, numEnd), out uint imgCount)) return null;
        string afterCount = rest.Substring(numEnd);

        if (afterCount.StartsWith("=", StringComparison.Ordinal))
        {
            string stripped = afterCount.Substring(1);
            int gfxEnd = 0;
            while (gfxEnd < stripped.Length && char.IsAsciiDigit(stripped[gfxEnd])) gfxEnd++;
            if (gfxEnd > 0)
            {
                if (!uint.TryParse(stripped.Substring(0, gfxEnd), out uint gfxId)) return null;
                string name = stripped.Substring(gfxEnd).Trim();
                return (imgCount, gfxId, name);
            }
        }

        return (imgCount, null, afterCount.Trim());
    }

    public static (string Header, List<string> Frames)? SplitFrameContent(string content)
    {
        int commaPos = content.IndexOf(',');
        if (commaPos < 0) return null;
        string header = content.Substring(0, commaPos);
        string frameText = content.Substring(commaPos + 1);
        var frames = frameText.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries).ToList();
        if (frames.Count == 0) return null;
        return (header, frames);
    }

    private static uint? FrameRow(string frame)
    {
        int dot = frame.IndexOf('.');
        if (dot < 0) return null;
        return uint.TryParse(frame.Substring(0, dot), out uint v) ? v : null;
    }

    public static bool IsTianmInterleavedLr(string content0, string content4)
    {
        var s0 = SplitFrameContent(content0);
        if (s0 == null) return false;
        var s4 = SplitFrameContent(content4);
        if (s4 == null) return false;
        var (header0, frames0) = s0.Value;
        var (header4, frames4) = s4.Value;
        if (header0.Trim() != header4.Trim() || frames0.Count != frames4.Count || frames0.Count < 2)
            return false;

        uint? row0First = FrameRow(frames0[0]);
        if (row0First == null) return false;
        uint? row4First = FrameRow(frames4[0]);
        if (row4First == null) return false;
        if (row0First == row4First) return false;

        for (int i = 1; i < frames0.Count; i++)
        {
            if (FrameRow(frames0[i]) != row4First || FrameRow(frames4[i]) != row0First)
                return false;
        }
        return true;
    }

    /// 天m 格式：更新 header 行的圖片數。原樣照抄 Rust 的「字串首次出現替換」邏輯——這是已知的
    /// 邊界風險（若同一行更早處有同樣的數字字串會被誤換），刻意保留跟原始行為一致，不要自作
    /// 主張改成結構化解析。
    /// `#61 320=3213 knight` + old=320, new=360 → `#61 360=3213 knight`
    public static string UpdateHeaderImgCount(string line, uint oldCount, uint newCount)
    {
        string oldStr = oldCount.ToString();
        string newStr = newCount.ToString();
        int pos = line.IndexOf(oldStr, StringComparison.Ordinal);
        if (pos < 0) return line;
        return line.Substring(0, pos) + newStr + line.Substring(pos + oldStr.Length);
    }

    /// 從單行格式中提取指定動作的幀內容 + 名稱（括號配對安全，深度追蹤）。
    /// 例如: "#1353 24 great dane 0.run_one(1 4,...) 4.run_two(1 4,...) ..."
    /// ExtractInlineActionWithName(line, 0) → ("1 4,...", "run_one")
    public static (string Content, string Name)? ExtractInlineActionWithName(string line, uint targetAction)
    {
        int depth = 0;
        int i = 0;
        int len = line.Length;

        while (i < len)
        {
            char c = line[i];
            if (c == '(') { depth++; i++; continue; }
            if (c == ')') { depth--; i++; continue; }
            if (depth != 0) { i++; continue; }

            if (char.IsAsciiDigit(c))
            {
                bool atBoundary = i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t' || line[i - 1] == ')';
                if (atBoundary)
                {
                    int numStart = i;
                    int numEnd = i;
                    while (numEnd < len && char.IsAsciiDigit(line[numEnd])) numEnd++;
                    if (numEnd < len && line[numEnd] == '.')
                    {
                        if (uint.TryParse(line.Substring(numStart, numEnd - numStart), out uint actionNum) &&
                            actionNum == targetAction)
                        {
                            int parenOff = line.Substring(numEnd).IndexOf('(');
                            if (parenOff >= 0)
                            {
                                int parenAbs = numEnd + parenOff;
                                string name = line.Substring(numEnd + 1, parenAbs - (numEnd + 1)).Trim();
                                int closeOff = line.Substring(parenAbs + 1).IndexOf(')');
                                if (closeOff >= 0)
                                {
                                    return (line.Substring(parenAbs + 1, closeOff), name);
                                }
                            }
                        }
                    }
                    i = numEnd;
                    continue;
                }
            }
            i++;
        }
        return null;
    }

    /// 從 frame content（"1 8,200.0:2 ..."）取第一張 spr。
    public static uint? FirstSprFromContent(string content)
    {
        int commaPos = content.IndexOf(',');
        if (commaPos < 0) return null;
        string after = content.Substring(commaPos + 1).TrimStart();
        int dotPos = after.IndexOf('.');
        if (dotPos < 0) return null;
        return uint.TryParse(after.Substring(0, dotPos).Trim(), out uint v) ? v : null;
    }
}
