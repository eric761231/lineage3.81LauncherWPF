using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace LinEncoder.Services.SmoothRun;

// Phase 5：把 SpriteFile + Walk→RunPair 映射 emit 為輸出文字。
// 對照 L1J3.8Launcher(RUST)參考\src\smooth_run\emit.rs，逐段翻譯。
//
// 流程：
//   - 逐行 emit RawLines
//   - dash variant 行 → skip（被結構化的 slot 98/99 取代）
//   - 動作號 >= 121 → skip（3.8 client 不支援）
//   - sprite 結束位置 → 為有 RunPair 的 walk sprite 注入 slot 98/99
//   - 若 source_img_count > sprite.img_count → 改寫 sprite header（原樣照抄 Rust 的字串首次
//     出現替換，見 SmoothRunHelpers.UpdateHeaderImgCount 的說明，不要「修好」這個已知邊界風險）
public static class SmoothRunEmitter
{
    public static string Emit(SpriteFile sf, Dictionary<ushort, RunPair> walkToRun)
    {
        var output = new StringBuilder();

        var spriteBySid = sf.Sprites.ToDictionary(s => s.Sid, s => s);

        // line_idx → sid mapping（確定每行屬於哪個 sprite）。
        var spriteStarts = sf.Sprites.Select(s => (s.HeaderLineIdx, s.Sid)).OrderBy(t => t.HeaderLineIdx).ToList();
        var sidAtIdx = new ushort?[sf.RawLines.Count];
        {
            int si = 0;
            ushort? current = null;
            for (int idx = 0; idx < sf.RawLines.Count; idx++)
            {
                while (si < spriteStarts.Count && spriteStarts[si].HeaderLineIdx == idx)
                {
                    current = spriteStarts[si].Sid;
                    si++;
                }
                sidAtIdx[idx] = current;
            }
        }

        // sid → set<dash_line_idx>（O(1) 查 dash variant 行）。
        var dashLines = new Dictionary<ushort, HashSet<int>>();
        foreach (var sprite in sf.Sprites)
        {
            var set = new HashSet<int>();
            foreach (var action in sprite.Actions)
                if (action.DashVariant != null) set.Add(action.LineIdx);
            if (set.Count > 0) dashLines[sprite.Sid] = set;
        }

        // 確認哪些行會被跳過（dash variant 或 action >= 121）。
        var willSkipLine = new bool[sf.RawLines.Count];
        for (int idx = 0; idx < sf.RawLines.Count; idx++)
        {
            var sid = sidAtIdx[idx];
            if (sid == null) continue;
            if (dashLines.TryGetValue(sid.Value, out var set) && set.Contains(idx))
            {
                willSkipLine[idx] = true;
                continue;
            }
            string trimmed = sf.RawLines[idx].TrimStart();
            var actionNum = SmoothRunHelpers.ParseActionNumber(trimmed);
            if (actionNum != null && actionNum.Value >= 121) willSkipLine[idx] = true;
        }

        // 每個 sprite 的最後非跳過行（用於注入 slot 98/99）。
        var lastLinePerSprite = new Dictionary<ushort, int>();
        foreach (var sprite in sf.Sprites)
        {
            int last = sprite.HeaderLineIdx;
            for (int lineIdx = sprite.HeaderLineIdx + 1; lineIdx < sf.RawLines.Count; lineIdx++)
            {
                if (sidAtIdx[lineIdx] != sprite.Sid) break;
                if (!willSkipLine[lineIdx]) last = lineIdx;
            }
            lastLinePerSprite[sprite.Sid] = last;
        }

        for (int idx = 0; idx < sf.RawLines.Count; idx++)
        {
            string line = sf.RawLines[idx];
            if (willSkipLine[idx]) continue;

            var sid = sidAtIdx[idx];

            // header 改寫：若該 sprite 有 walk_to_run 命中且 source_img_count > 自身 img_count。
            if (sid != null && spriteBySid.TryGetValue(sid.Value, out var sprite) &&
                sprite.HeaderLineIdx == idx)
            {
                if (walkToRun.TryGetValue(sid.Value, out var hp) && hp.SourceImgCount > sprite.ImgCount)
                {
                    string newLine = SmoothRunHelpers.UpdateHeaderImgCount(line, sprite.ImgCount, hp.SourceImgCount);
                    output.Append(newLine);
                    output.Append('\n');
                    continue;
                }
            }

            output.Append(line);

            // 注入 slot 98/99（在 sprite 最後一行 action 之後）。
            if (sid != null && lastLinePerSprite.TryGetValue(sid.Value, out var lastIdx) && lastIdx == idx)
            {
                if (walkToRun.TryGetValue(sid.Value, out var p))
                {
                    // 只有在 runl/runr 至少一側存在 + framerate 存在時才注入 110.framerate。
                    if ((p.RunL != null || p.RunR != null) && p.Framerate != null)
                    {
                        output.Append('\n');
                        output.Append('\t').Append("110.framerate(").Append(p.Framerate).Append(')');
                    }
                    if (p.RunL != null)
                    {
                        output.Append('\n');
                        output.Append('\t').Append("98.walk(").Append(p.RunL).Append(')');
                    }
                    if (p.RunR != null)
                    {
                        output.Append('\n');
                        output.Append('\t').Append("99.walk(").Append(p.RunR).Append(')');
                    }
                }
            }

            if (idx < sf.RawLines.Count - 1) output.Append('\n');
        }

        // 補充尾部 newline（若原始文本以 newline 結尾而輸出沒有）。
        if (sf.EndsWithNewline && (output.Length == 0 || output[^1] != '\n'))
            output.Append('\n');

        return output.ToString();
    }
}
