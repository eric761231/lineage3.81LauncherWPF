using System.Collections.Generic;
using System.Linq;

namespace LinEncoder.Services.SmoothRun;

// Phase 3：對 Run／Both 角色 sprite 萃取 (RunL_content, RunR_content)。
// 對照 L1J3.8Launcher(RUST)參考\src\smooth_run\extract.rs，逐函式翻譯。
//
// 萃取優先序（命中第一條即停）：
//   1. Dash variant：dash_variant=1 → RunL，=2 → RunR
//   2. Named (Path B)：name 開頭 "runl" → RunL，"runr" → RunR
//   3. 32/33 結構（Path C）：strict +8 spr_diff
//   4. 0/4 結構（模板 A）：abs_diff=8 + fc=8，不乾淨命名側「不寫入」（不對稱）
public static class SmoothRunExtractor
{
    public static Dictionary<ushort, RunPair> Extract(SpriteFile sf, Dictionary<ushort, SpriteRole> roles)
    {
        var runs = new Dictionary<ushort, RunPair>();
        foreach (var sprite in sf.Sprites)
        {
            var role = roles.TryGetValue(sprite.Sid, out var r) ? r : SpriteRole.None;
            var pair = ExtractOne(sprite, role);
            if (pair == null) continue;

            // Template A framerate fallback：對應 legacy line 363-368。若 sprite 滿足 template A
            // 條件（fc=8 + abs_diff=8 + clean name）但 pair.Framerate 為 null（因為 extract_dash 等
            // 先命中而沒帶 framerate），從 action 0/4 framerate_at_parse 補位。
            if (pair.Framerate == null)
            {
                var fr = TemplateAFramerateFallback(sprite);
                if (fr != null) pair.Framerate = fr;
            }
            runs[sprite.Sid] = pair;
        }
        return runs;
    }

    private static string? TemplateAFramerateFallback(Sprite sprite)
    {
        var a0 = sprite.Actions.FirstOrDefault(a => a.DashVariant == null && a.BaseAction == 0);
        if (a0 == null) return null;
        var a4 = sprite.Actions.FirstOrDefault(a => a.DashVariant == null && a.BaseAction == 4);
        if (a4 == null) return null;
        if (a0.Direction != 1 || a4.Direction != 1 || a0.FrameCount != 8 || a4.FrameCount != 8) return null;
        if (SmoothRunHelpers.AbsDiff(a0.FirstSpr, a4.FirstSpr) != 8) return null;
        if (!IsCleanRunSourceName(a0.Name) && !IsCleanRunSourceName(a4.Name)) return null;
        return a0.FramerateAtParse ?? a4.FramerateAtParse;
    }

    private static RunPair? ExtractOne(Sprite sprite, SpriteRole role)
    {
        return ExtractDash(sprite) ?? ExtractNamed(sprite) ?? Extract3233Strict(sprite) ??
            Extract04Abs(sprite, role);
    }

    /// Builtin LR —— 對應 legacy line 656-694：action 0/4 名稱含 walkfast/run 前綴 + s0!=s4 + 同
    /// fc。由 pair.rs Stage 5 在 cross-sprite mapping 之後呼叫，僅對未配對的 sprite 補位。
    public static RunPair? ExtractBuiltinLr(Sprite sprite)
    {
        var a0 = sprite.Actions.FirstOrDefault(a => a.DashVariant == null && a.BaseAction == 0);
        if (a0 == null) return null;
        var a4 = sprite.Actions.FirstOrDefault(a => a.DashVariant == null && a.BaseAction == 4);
        if (a4 == null) return null;
        if (!IsLrWalkName(a0.Name)) return null;
        if (a0.FirstSpr == a4.FirstSpr) return null;
        if (a0.FrameCount != a4.FrameCount) return null;
        return new RunPair
        {
            RunL = a0.Content,
            RunR = a4.Content,
            Framerate = a0.FramerateAtParse ?? a4.FramerateAtParse,
            SourceImgCount = sprite.ImgCount,
        };
    }

    private static bool IsLrWalkName(string name)
    {
        string lower = name.ToLowerInvariant();
        return lower.StartsWith("walkfast") || lower.StartsWith("run");
    }

    private static RunPair? ExtractDash(Sprite sprite)
    {
        // base_action >= 121 的 dash variant 整對被排除（overflow action，3.8 client 不支援）。
        const uint maxActionSlot = 121;
        var l = sprite.Actions.FirstOrDefault(a => a.DashVariant == 1 && a.BaseAction < maxActionSlot);
        var r = sprite.Actions.FirstOrDefault(a => a.DashVariant == 2 && a.BaseAction < maxActionSlot);
        if (l == null && r == null) return null;
        // dash variant 不帶 framerate；framerate 由 template A/B 或 Path B 在主流程外另外計算
        // （TemplateAFramerateFallback 補位）。
        return new RunPair
        {
            RunL = l?.Content,
            RunR = r?.Content,
            Framerate = null,
            SourceImgCount = sprite.ImgCount,
        };
    }

    private static RunPair? ExtractNamed(Sprite sprite)
    {
        var l = sprite.Actions.FirstOrDefault(a => a.DashVariant == null && a.Name.StartsWith("runl"));
        var r = sprite.Actions.FirstOrDefault(a => a.DashVariant == null && a.Name.StartsWith("runr"));
        if (l == null && r == null) return null;
        string? framerate = ActionRunlRunrFramerate(l) ?? ActionRunlRunrFramerate(r);
        return new RunPair
        {
            RunL = l?.Content,
            RunR = r?.Content,
            Framerate = framerate,
            SourceImgCount = sprite.ImgCount,
        };
    }

    private static string? ActionRunlRunrFramerate(SmoothRunAction? a)
    {
        if (a == null) return null;
        return (a.Name.StartsWith("runl") || a.Name.StartsWith("runr")) ? a.FramerateAtParse : null;
    }

    private static RunPair? Extract3233Strict(Sprite sprite)
    {
        var a32 = sprite.Actions.FirstOrDefault(a => a.DashVariant == null && a.BaseAction == 32);
        if (a32 == null) return null;
        var a33 = sprite.Actions.FirstOrDefault(a => a.DashVariant == null && a.BaseAction == 33);
        if (a33 == null) return null;
        if (a32.Direction != 1 || a33.Direction != 1 || a32.FrameCount != 8 || a33.FrameCount != 8) return null;

        SmoothRunAction l, r;
        if (a33.FirstSpr >= a32.FirstSpr && a33.FirstSpr - a32.FirstSpr == 8) { l = a32; r = a33; }
        else if (a32.FirstSpr >= a33.FirstSpr && a32.FirstSpr - a33.FirstSpr == 8) { l = a33; r = a32; }
        else return null;

        // Path C 自身不注入 110.framerate；但若同 sprite 模板 A 也命中（0/4 spr 差 8 + 乾淨名），
        // 用 walk_action_framerate 補位。
        return new RunPair
        {
            RunL = l.Content,
            RunR = r.Content,
            Framerate = TemplateAWalkFramerate(sprite),
            SourceImgCount = sprite.ImgCount,
        };
    }

    private static RunPair? Extract04Abs(Sprite sprite, SpriteRole role)
    {
        var l = sprite.Actions.FirstOrDefault(a => a.DashVariant == null && a.BaseAction == 0);
        var r = sprite.Actions.FirstOrDefault(a => a.DashVariant == null && a.BaseAction == 4);
        if (l == null && r == null) return null;

        // 單動作 case（對應 legacy 模板 B 單動作 promote）：僅 a0 或僅 a4 存在，role 必須為 Run
        // （已由 classify 模板 B 升等）+ fc=8 + 乾淨名。
        if ((l != null) ^ (r != null))
        {
            if (role != SpriteRole.Run) return null;
            var single = (l ?? r)!;
            if (single.Direction != 1 || single.FrameCount != 8) return null;
            if (!IsCleanRunSourceName(single.Name)) return null;
            return new RunPair
            {
                RunL = l?.Content,
                RunR = r?.Content,
                Framerate = single.FramerateAtParse,
                SourceImgCount = sprite.ImgCount,
            };
        }

        var ll = l!;
        var rr = r!;
        string? framerate = ll.FramerateAtParse ?? rr.FramerateAtParse;

        // Interleaved LR：action 0/4 frames 行列交錯時直接雙側 populate，不查 clean name guard。
        // 僅在 sprite 已被 classify 為 Run 時接受——template A 或 template B 其中之一已驗證，
        // 避免無 gfx 的 fc=4 sprite 過度命中。
        if (role == SpriteRole.Run && SmoothRunHelpers.IsTianmInterleavedLr(ll.Content, rr.Content))
        {
            return new RunPair
            {
                RunL = ll.Content,
                RunR = rr.Content,
                Framerate = framerate,
                SourceImgCount = sprite.ImgCount,
            };
        }

        // 模板 A：strict fc=8 + dir=1 + abs_diff=8 + 乾淨名。
        if (ll.Direction != 1 || rr.Direction != 1 || ll.FrameCount != 8 || rr.FrameCount != 8) return null;
        if (SmoothRunHelpers.AbsDiff(ll.FirstSpr, rr.FirstSpr) != 8) return null;
        bool cleanL = IsCleanRunSourceName(ll.Name);
        bool cleanR = IsCleanRunSourceName(rr.Name);
        if (!cleanL && !cleanR) return null;

        // 不對稱：乾淨那側才寫入 slot 98/99。
        return new RunPair
        {
            RunL = cleanL ? ll.Content : null,
            RunR = cleanR ? rr.Content : null,
            Framerate = framerate,
            SourceImgCount = sprite.ImgCount,
        };
    }

    /// 模板 A walk framerate fallback —— 用於 Path C 同 sprite 也命中模板 A 時。
    /// 注意：跟其他查找不同，這裡「不」過濾 DashVariant，逐字對照 Rust 原始碼保留這個不對稱。
    private static string? TemplateAWalkFramerate(Sprite sprite)
    {
        if (!TemplateAEligible(sprite)) return null;
        var a0 = sprite.Actions.FirstOrDefault(a => a.BaseAction == 0);
        var a4 = sprite.Actions.FirstOrDefault(a => a.BaseAction == 4);
        return a0?.FramerateAtParse ?? a4?.FramerateAtParse;
    }

    /// 模板 A（action 0/4，direction=1，frame_count=8，abs_diff=8，乾淨名）。
    /// 同上：刻意不過濾 DashVariant，對照 Rust 原始碼。
    private static bool TemplateAEligible(Sprite sprite)
    {
        var a0 = sprite.Actions.FirstOrDefault(a => a.BaseAction == 0);
        if (a0 == null) return false;
        var a4 = sprite.Actions.FirstOrDefault(a => a.BaseAction == 4);
        if (a4 == null) return false;
        if (a0.Direction != 1 || a4.Direction != 1 || a0.FrameCount != 8 || a4.FrameCount != 8) return false;
        if (SmoothRunHelpers.AbsDiff(a0.FirstSpr, a4.FirstSpr) != 8) return false;
        return IsCleanRunSourceName(a0.Name) || IsCleanRunSourceName(a4.Name);
    }

    /// 接受空字串、純 walk/runl/runr、walkfast 前綴。拒絕帶武器後綴（避免持劍 bug）。
    private static bool IsCleanRunSourceName(string name)
    {
        string trimmed = name.Trim();
        if (trimmed.Length == 0) return true;
        return trimmed is "walk" or "runl" or "runr" || trimmed.StartsWith("walkfast");
    }
}
