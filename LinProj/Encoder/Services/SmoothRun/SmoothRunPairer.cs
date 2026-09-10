using System.Collections.Generic;
using System.Linq;

namespace LinEncoder.Services.SmoothRun;

// Phase 4：把 Walk sprites 對映到對應的 Run pair。
// 對照 L1J3.8Launcher(RUST)參考\src\smooth_run\pair.rs，逐段翻譯。
//
// 配對規則：
//   1. Both 角色 sprite 用自身
//   2. 純 Run 角色 sprite（沒有 Walk 動作）也注入 slot 98/99
//   3. 同 gfx_id 群組：Walk → 群組內第一個 Run 的 RunPair（只補空欄位）
//   4. Stage B：純 Run sprite 的 gfx_id（當 u16）直接指向某個 walk sprite ID
//   5. Builtin LR 兜底
public static class SmoothRunPairer
{
    public static Dictionary<ushort, RunPair> PairWalksToRuns(SpriteFile sf, Dictionary<ushort, SpriteRole> roles,
        Dictionary<ushort, RunPair> runs)
    {
        var walkToRun = new Dictionary<ushort, RunPair>();

        // 1. Both 角色 sprite 用自身。
        foreach (var sprite in sf.Sprites)
        {
            if (roles.TryGetValue(sprite.Sid, out var role) && role == SpriteRole.Both)
            {
                if (runs.TryGetValue(sprite.Sid, out var p))
                    walkToRun[sprite.Sid] = ClonePair(p);
            }
        }

        // 2. 純 Run 角色 sprite（沒有 Walk 動作）也注入 slot 98/99 —— dash variant／named
        //    RunL/RunR／32-33 結構的精靈。
        foreach (var sprite in sf.Sprites)
        {
            if (roles.TryGetValue(sprite.Sid, out var role) && role == SpriteRole.Run)
            {
                if (runs.TryGetValue(sprite.Sid, out var p))
                    walkToRun[sprite.Sid] = ClonePair(p);
            }
        }

        // 3. 同 gfx_id 群組:Walk → 群組內第一個 Run 的 RunPair。
        var gfxGroups = new SortedDictionary<uint, (List<ushort> Walks, List<ushort> Runs)>();
        foreach (var sprite in sf.Sprites)
        {
            if (sprite.GfxId == null) continue;
            var role = roles.TryGetValue(sprite.Sid, out var r) ? r : SpriteRole.None;
            if (!gfxGroups.TryGetValue(sprite.GfxId.Value, out var entry))
                gfxGroups[sprite.GfxId.Value] = entry = (new List<ushort>(), new List<ushort>());
            // Both 也視為 walk-side target —— legacy 可與 sprite_has_run_actions 同時存在，
            // Both-role sprite 進到 originals[]。
            if (role == SpriteRole.Walk || role == SpriteRole.Both) entry.Walks.Add(sprite.Sid);
            else if (role == SpriteRole.Run) entry.Runs.Add(sprite.Sid);
        }
        foreach (var (_, group) in gfxGroups)
        {
            var walks = group.Walks;
            walks.Sort();
            var runSids = group.Runs;
            runSids.Sort();
            // 迴圈所有 run sprite，對 orig 做 per-field or_insert，讓後到的 run sprite 補齊先前
            // run sprite 缺的欄位（例如 framerate）。
            foreach (var runSid in runSids)
            {
                if (!runs.TryGetValue(runSid, out var p)) continue;
                foreach (var w in walks)
                {
                    if (walkToRun.TryGetValue(w, out var existing))
                    {
                        if (existing.RunL == null && p.RunL != null) existing.RunL = p.RunL;
                        if (existing.RunR == null && p.RunR != null) existing.RunR = p.RunR;
                        if (existing.Framerate == null && p.Framerate != null) existing.Framerate = p.Framerate;
                        if (p.SourceImgCount > existing.SourceImgCount) existing.SourceImgCount = p.SourceImgCount;
                    }
                    else
                    {
                        walkToRun[w] = ClonePair(p);
                    }
                }
            }
        }

        // 4. Stage B：純 Run sprite 的 gfx_id（當 u16）直接指向某個 walk sprite ID。
        //    例：#10641 gfx=4910 把 RunL/RunR 注入 #4910；#16851 gfx=16848 把 framerate 補進
        //    Both-role #16848（內容已由 Path C 注入）。walk_sids 包含 Walk 與 Both。
        var walkSids = new HashSet<ushort>(sf.Sprites
            .Where(s => roles.TryGetValue(s.Sid, out var r) && (r == SpriteRole.Walk || r == SpriteRole.Both))
            .Select(s => s.Sid));
        var pureRunSids = sf.Sprites
            .Where(s => roles.TryGetValue(s.Sid, out var r) && r == SpriteRole.Run)
            .Select(s => s.Sid)
            .OrderBy(x => x)
            .ToList();
        foreach (var runSid in pureRunSids)
        {
            var runSprite = sf.Sprites.FirstOrDefault(s => s.Sid == runSid);
            if (runSprite?.GfxId == null) continue;
            if (runSprite.GfxId.Value > ushort.MaxValue) continue;
            ushort candidate = (ushort)runSprite.GfxId.Value;
            if (candidate == runSid) continue;
            if (!walkSids.Contains(candidate)) continue;
            if (!runs.TryGetValue(runSid, out var p)) continue;

            if (walkToRun.TryGetValue(candidate, out var existing))
            {
                if (existing.Framerate == null) existing.Framerate = p.Framerate;
                if (existing.RunL == null && p.RunL != null) existing.RunL = p.RunL;
                if (existing.RunR == null && p.RunR != null) existing.RunR = p.RunR;
            }
            else
            {
                walkToRun[candidate] = ClonePair(p);
            }
        }

        // 5. Builtin LR fallback：僅對尚未在 walk_to_run 內的 sprite 生效；cross-sprite 映射
        //    （Stage 3/4）優先。
        foreach (var sprite in sf.Sprites)
        {
            if (walkToRun.ContainsKey(sprite.Sid)) continue;
            var p = SmoothRunExtractor.ExtractBuiltinLr(sprite);
            if (p != null) walkToRun[sprite.Sid] = p;
        }

        return walkToRun;
    }

    private static RunPair ClonePair(RunPair p) => new()
    {
        RunL = p.RunL,
        RunR = p.RunR,
        Framerate = p.Framerate,
        SourceImgCount = p.SourceImgCount,
    };
}
