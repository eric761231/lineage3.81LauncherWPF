using System;
using System.Collections.Generic;
using System.Linq;

namespace LinEncoder.Services.SmoothRun;

// Phase 1：把變身檔文字解析成結構化 SpriteFile IR。純 lexer/parser，不做任何分類。
// 對照 L1J3.8Launcher(RUST)參考\src\smooth_run\parse.rs，逐行翻譯。
public static class SmoothRunParser
{
    // 對應 legacy.rs:126 同一份 inline 掃描範圍（WALK_ACTIONS + 32/33）。
    private static readonly uint[] InlineScanActions =
        { 0, 4, 11, 20, 24, 40, 46, 50, 54, 58, 62, 83, 88, 119, 32, 33 };

    public static SpriteFile Parse(string text)
    {
        bool endsWithNewline = text.EndsWith("\n", StringComparison.Ordinal);
        var rawLines = text.Split('\n').ToList();
        string fileHeader = rawLines.Count > 0 ? rawLines[0] : "";
        var sprites = new List<Sprite>();
        // 對應 legacy `cur_framerate` —— sprite-scoped，進入新 sprite 時 reset。
        string? curFramerate = null;

        for (int idx = 0; idx < rawLines.Count; idx++)
        {
            string line = rawLines[idx];
            string trimmed = line.TrimStart();

            if (trimmed.StartsWith("#", StringComparison.Ordinal))
            {
                ushort? sid = SmoothRunHelpers.ParseSpriteId(trimmed);
                if (sid != null)
                {
                    var headerExt = SmoothRunHelpers.ParseHeaderExt(trimmed);
                    var sprite = new Sprite
                    {
                        Sid = sid.Value,
                        HeaderLineIdx = idx,
                        HeaderText = line,
                        ImgCount = headerExt?.ImgCount ?? 0,
                        GfxId = headerExt?.GfxId,
                        Name = headerExt?.Name ?? "",
                        Framerate = null,
                    };
                    curFramerate = null;
                    // 天R 風格 inline action 掃描：同一份 header 行可能含 0.walkfastI(...)、
                    // 4.walkfastII(...) 等。每個命中的 action 用 line_idx = idx（= header_line_idx），
                    // indent = ""。
                    foreach (uint actionNum in InlineScanActions)
                    {
                        var hit = SmoothRunHelpers.ExtractInlineActionWithName(trimmed, actionNum);
                        if (hit == null) continue;
                        var (content, name) = hit.Value;
                        if (content.Length == 0) continue;
                        var action = BuildInlineAction(idx, actionNum, content, name, curFramerate);
                        if (action != null) sprite.Actions.Add(action);
                    }
                    sprites.Add(sprite);
                }
                continue;
            }

            if (sprites.Count == 0) continue;
            var currentSprite = sprites[^1];

            var variant = SmoothRunHelpers.ParseVariantLine(trimmed);
            if (variant != null)
            {
                // 對應 legacy line 152：只 capture variant == 1 或 2 為 dash action；其他
                // variant（如 0-3.spell no direction）在 legacy 也是 continue，但不進
                // variant_map，emit 自然 pass-through。pipeline 同樣不收進 sprite.Actions，
                // 讓 emit 不誤刪。
                if (variant.Value.Variant == 1 || variant.Value.Variant == 2)
                {
                    string indent = line.Substring(0, line.Length - trimmed.Length);
                    var action = BuildAction(idx, indent, variant.Value.Base, variant.Value.Variant, line,
                        trimmed, curFramerate);
                    if (action != null) currentSprite.Actions.Add(action);
                }
                continue;
            }

            uint? baseOpt = SmoothRunHelpers.ParseActionNumber(trimmed);
            if (baseOpt != null)
            {
                uint baseNum = baseOpt.Value;
                if (baseNum == 110)
                {
                    string content = SmoothRunHelpers.ExtractFrameContent(line);
                    if (content.Length != 0)
                    {
                        if (currentSprite.Framerate == null) currentSprite.Framerate = content;
                        curFramerate = content;
                    }
                }
                string indent = line.Substring(0, line.Length - trimmed.Length);
                var action = BuildAction(idx, indent, baseNum, null, line, trimmed, curFramerate);
                if (action != null) currentSprite.Actions.Add(action);
            }
        }

        return new SpriteFile
        {
            FileHeader = fileHeader,
            Sprites = sprites,
            RawLines = rawLines,
            EndsWithNewline = endsWithNewline,
        };
    }

    private static SmoothRunAction? BuildInlineAction(int lineIdx, uint baseAction, string content, string name,
        string? curFramerate)
    {
        var split = SmoothRunHelpers.SplitFrameContent(content);
        if (split == null) return null;
        // 對應 Rust header_iter.next()?（方向）跟 next()?（幀數）都要求「有這個 token」，
        // 缺第二個 token 時整個 action 直接丟棄，不是預設成 0——只有第二個 token「解析失敗」才
        // 預設 0（unwrap_or）。
        var headerParts = split.Value.Header.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        if (headerParts.Length < 2) return null;
        if (!uint.TryParse(headerParts[0], out uint direction)) return null;
        if (!uint.TryParse(headerParts[1], out uint frameCount)) frameCount = 0;
        uint firstSpr = SmoothRunHelpers.FirstSprFromContent(content) ?? 0;
        return new SmoothRunAction
        {
            LineIdx = lineIdx,
            Indent = "",
            BaseAction = baseAction,
            DashVariant = null,
            Name = name.ToLowerInvariant().Trim(),
            Content = content,
            Direction = direction,
            FrameCount = frameCount,
            FirstSpr = firstSpr,
            FramerateAtParse = curFramerate,
        };
    }

    private static SmoothRunAction? BuildAction(int lineIdx, string indent, uint baseAction, uint? dashVariant,
        string fullLine, string trimmed, string? curFramerate)
    {
        string content = SmoothRunHelpers.ExtractFrameContent(fullLine);
        if (content.Length == 0) return null;
        var split = SmoothRunHelpers.SplitFrameContent(content);
        if (split == null) return null;
        var headerParts = split.Value.Header.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        if (headerParts.Length < 2) return null;
        if (!uint.TryParse(headerParts[0], out uint direction)) return null;
        if (!uint.TryParse(headerParts[1], out uint frameCount)) frameCount = 0;
        uint firstSpr = SmoothRunHelpers.FirstSprFromContent(content) ?? 0;
        string name = SmoothRunHelpers.ExtractActionName(trimmed).ToLowerInvariant().Trim();
        return new SmoothRunAction
        {
            LineIdx = lineIdx,
            Indent = indent,
            BaseAction = baseAction,
            DashVariant = dashVariant,
            Name = name,
            Content = content,
            Direction = direction,
            FrameCount = frameCount,
            FirstSpr = firstSpr,
            FramerateAtParse = curFramerate,
        };
    }
}
