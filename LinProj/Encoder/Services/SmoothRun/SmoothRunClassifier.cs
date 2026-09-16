using System.Collections.Generic;
using System.Linq;

namespace LinEncoder.Services.SmoothRun;

/// <summary>
/// Phase 2：對 SpriteFile 中每個 sprite 分類為 SpriteRole 的分類器服務類別。
/// 對照 L1J3.8Launcher(RUST)參考\src\smooth_run\classify.rs，逐函式翻譯。
/// </summary>
public static class SmoothRunClassifier
{
    private static readonly HashSet<uint> WalkActions =
        new() { 0, 4, 11, 20, 24, 40, 46, 50, 54, 58, 62, 83, 88, 119 };

    private const uint CrossSpriteSprGap = 16;

    /// <summary>
    /// 對整份 SpriteFile 進行 SpriteRole 角色分類。
    /// </summary>
    /// <param name="sf">Sprite 檔案模型</param>
    /// <return>以 Sid 為 Key、SpriteRole 為 Value 之字典</return>
    public static Dictionary<ushort, SpriteRole> Classify(SpriteFile sf)
    {
        var roles = new Dictionary<ushort, SpriteRole>();
        foreach (var sprite in sf.Sprites)
        {
            roles[sprite.Sid] = ClassifyOne(sprite);
        }
        // Template B（對應 legacy.rs:395-480）：跨 sprite spr_diff>=16 升等。同 gfx_id 群組內，
        // action 0 first_spr 比 baseline 高 >=16 + 0/4 內容 interleaved 或名稱顯式 run-source
        // → 升等為 Run。
        ApplyTemplateB(roles, sf);
        return roles;
    }

    /// <summary>
    /// 對單一 Sprite 進行角色分類。
    /// </summary>
    /// <param name="sprite">Sprite 模型</param>
    /// <return>分類出的 SpriteRole</return>
    private static SpriteRole ClassifyOne(Sprite sprite)
    {
        bool hasRun = HasRunSignal(sprite);
        bool hasWalk = HasWalkSignal(sprite, hasRun);
        SpriteRole role = (hasWalk, hasRun) switch
        {
            (true, true) => SpriteRole.Both,
            (true, false) => SpriteRole.Walk,
            (false, true) => SpriteRole.Run,
            _ => SpriteRole.None,
        };
        // Legacy 模板 A 升等：Both → Run（action 0/4 結構命中 + 乾淨名／interleaved）。
        if (role == SpriteRole.Both && TemplateAPromotesToRun(sprite))
        {
            return SpriteRole.Run;
        }
        return role;
    }

    /// <summary>
    /// 套用 Template B 規則：跨 Sprite spr_diff 比對並升等為 Run。
    /// </summary>
    /// <param name="roles">Sprite 角色字典</param>
    /// <param name="sf">Sprite 檔案模型</param>
    private static void ApplyTemplateB(Dictionary<ushort, SpriteRole> roles, SpriteFile sf)
    {
        var byGfx = new SortedDictionary<uint, List<(ushort Sid, uint Spr)>>();
        foreach (var sprite in sf.Sprites)
        {
            if (sprite.GfxId == null)
            {
                continue;
            }
            var a0 = sprite.Actions.FirstOrDefault(a => a.DashVariant == null && a.BaseAction == 0);
            if (a0 == null)
            {
                continue;
            }
            if (!byGfx.TryGetValue(sprite.GfxId.Value, out var list))
            {
                byGfx[sprite.GfxId.Value] = list = new List<(ushort, uint)>();
            }
            list.Add((sprite.Sid, a0.FirstSpr));
        }

        foreach (var (_, entries) in byGfx)
        {
            if (entries.Count < 2)
            {
                continue;
            }
            entries.Sort((a, b) => a.Spr.CompareTo(b.Spr));
            uint baseline = entries[0].Spr;
            uint maxSpr = entries[^1].Spr;
            if (maxSpr < baseline + CrossSpriteSprGap)
            {
                continue;
            }

            foreach (var (sid, spr) in entries)
            {
                if (spr < baseline + CrossSpriteSprGap)
                {
                    continue;
                }
                // 已分類為 Run 略過（template A 已處理，對應 legacy line 428-430）。
                if (roles.TryGetValue(sid, out var existingRole) && existingRole == SpriteRole.Run)
                {
                    continue;
                }

                var sprite = sf.Sprites.FirstOrDefault(s => s.Sid == sid);
                if (sprite == null)
                {
                    continue;
                }
                var a0 = sprite.Actions.FirstOrDefault(a => a.DashVariant == null && a.BaseAction == 0);
                if (a0 == null)
                {
                    continue;
                }
                var a4 = sprite.Actions.FirstOrDefault(a => a.DashVariant == null && a.BaseAction == 4);

                bool promote;
                if (a4 != null)
                {
                    // 雙動作：對應 legacy.rs:434 is_cross_sprite_tianm_run_source —— interleaved LR
                    // 或任一側名稱含 explicit run source（runone/runtwo/runl/runr/walkfast）。
                    bool interleaved = SmoothRunHelpers.IsTianmInterleavedLr(a0.Content, a4.Content);
                    bool explicitName = IsExplicitRunSourceName(a0.Name) || IsExplicitRunSourceName(a4.Name);
                    promote = interleaved || explicitName;
                }
                else
                {
                    // 單動作：對應 legacy 模板 B 在僅有 action 0 的 sprite 上仍會 promote（例
                    // keina female_run polymorph 的 walkfastI 單動作 case）。收緊條件：fc=8 +
                    // explicit run name（避免 fc=4 unnamed weapon walk 誤命中）。
                    promote = a0.FrameCount == 8 && IsExplicitRunSourceName(a0.Name);
                }

                if (!promote)
                {
                    continue;
                }
                roles[sid] = SpriteRole.Run;
            }
        }
    }

    /// <summary>
    /// 檢查動作名稱是否為顯式的 Run 來源名稱。
    /// </summary>
    /// <param name="name">動作名稱</param>
    /// <return>若為顯式 Run 名稱回傳 true，否則回傳 false</return>
    private static bool IsExplicitRunSourceName(string name)
    {
        string trimmed = name.ToLowerInvariant().Trim();
        return trimmed.StartsWith("runl") || trimmed.StartsWith("runr") || trimmed.StartsWith("walkfast")
            || trimmed is "runone" or "runtwo" or "run_one" or "run_two" or "run one" or "run two";
    }

    /// <summary>
    /// 檢查 Template A 條件是否可將 SpriteRole 升等為 Run。
    /// </summary>
    /// <param name="sprite">Sprite 模型</param>
    /// <return>可升等回傳 true，否則回傳 false</return>
    private static bool TemplateAPromotesToRun(Sprite sprite)
    {
        var a0 = sprite.Actions.FirstOrDefault(a => a.DashVariant == null && a.BaseAction == 0);
        if (a0 == null)
        {
            return false;
        }
        var a4 = sprite.Actions.FirstOrDefault(a => a.DashVariant == null && a.BaseAction == 4);
        if (a4 == null)
        {
            return false;
        }
        if (a0.Direction != 1 || a4.Direction != 1 || a0.FrameCount != 8 || a4.FrameCount != 8)
        {
            return false;
        }
        if (SmoothRunHelpers.AbsDiff(a0.FirstSpr, a4.FirstSpr) != 8)
        {
            return false;
        }
        // 對應 legacy.rs:354 insert_tianm_run_pair：乾淨名 OR interleaved LR 都接受。
        return IsCleanRunSourceName(a0.Name) || IsCleanRunSourceName(a4.Name)
            || SmoothRunHelpers.IsTianmInterleavedLr(a0.Content, a4.Content);
    }

    /// <summary>
    /// 判斷名稱是否為乾淨的 Run 來源名稱。
    /// </summary>
    /// <param name="name">動作名稱</param>
    /// <return>若為乾淨名稱回傳 true，否則回傳 false</return>
    private static bool IsCleanRunSourceName(string name)
    {
        string trimmed = name.Trim();
        if (trimmed.Length == 0)
        {
            return true;
        }
        return trimmed is "walk" or "runl" or "runr" || trimmed.StartsWith("walkfast");
    }

    /// <summary>
    /// 檢查 Sprite 是否包含 Run 訊號特徵。
    /// </summary>
    /// <param name="sprite">Sprite 模型</param>
    /// <return>含 Run 訊號回傳 true，否則回傳 false</return>
    private static bool HasRunSignal(Sprite sprite)
    {
        // 1. dash variant —— 只算 base < 121（對應 legacy line 626 overflow skip）。
        if (sprite.Actions.Any(a => a.DashVariant != null && a.BaseAction < 121))
        {
            return true;
        }

        // 2. named runL/runR —— 看非 dash variant 動作（Path B 對 base 不限，如 #18853 的
        //    137.RunR shield axe 仍命中）。
        if (sprite.Actions.Any(a =>
                a.DashVariant == null && (a.Name.StartsWith("runl") || a.Name.StartsWith("runr"))))
        {
            return true;
        }

        // 3. Path C：32/33 spr_diff = 8（position-agnostic —— 對應 legacy
        //    find_run_pair_structurally 雙向枚舉）。
        var a32 = sprite.Actions.FirstOrDefault(a => a.DashVariant == null && a.BaseAction == 32);
        var a33 = sprite.Actions.FirstOrDefault(a => a.DashVariant == null && a.BaseAction == 33);
        if (a32 != null && a33 != null)
        {
            if (a32.Direction == 1 && a33.Direction == 1 && a32.FrameCount == 8 && a33.FrameCount == 8)
            {
                if (SmoothRunHelpers.AbsDiff(a32.FirstSpr, a33.FirstSpr) == 8)
                {
                    return true;
                }
            }
        }

        // 4. 模板 A：0/4 abs_diff = 8 + （乾淨名／interleaved）。
        if (TemplateAPromotesToRun(sprite))
        {
            return true;
        }

        return false;
    }

    /// <summary>
    /// 檢查 Sprite 是否包含 Walk 訊號特徵。
    /// </summary>
    /// <param name="sprite">Sprite 模型</param>
    /// <param name="hasRun">是否已判定含 Run 訊號</param>
    /// <return>含 Walk 訊號回傳 true，否則回傳 false</return>
    private static bool HasWalkSignal(Sprite sprite, bool hasRun)
    {
        foreach (var a in sprite.Actions)
        {
            // Dash variant 不是 walk-action 收集對象。
            if (a.DashVariant != null)
            {
                continue;
            }
            if (!WalkActions.Contains(a.BaseAction))
            {
                continue;
            }
            if (hasRun && (a.Name.StartsWith("runl") || a.Name.StartsWith("runr")))
            {
                continue;
            }
            return true;
        }
        return false;
    }
}
