using System.Collections.Generic;

namespace LinEncoder.Services.SmoothRun;

// 順跑（Smooth Run）預編碼 IR 型別 — 對照 Rust 參考
// L1J3.8Launcher(RUST)參考\src\smooth_run\types.rs，逐欄位翻譯，不要憑感覺改欄位語意。

/// 整個變身檔的結構化表示。
public sealed class SpriteFile
{
    /// 第一行（精靈總數 header，例如 "300 0 41210"）。
    public string FileHeader = "";
    /// 所有 sprite，按出現順序。
    public List<Sprite> Sprites = new();
    /// 原始 line 對應（供 emit 階段保留註解／110.framerate／其他指令行）。
    public List<string> RawLines = new();
    /// 原始文本是否以 newline 結尾（供 emit 階段補上尾部 newline）。
    public bool EndsWithNewline;
}

public sealed class Sprite
{
    public ushort Sid;
    public int HeaderLineIdx;
    public string HeaderText = "";
    public uint ImgCount;
    public uint? GfxId;
    public string Name = "";
    /// 110.framerate 行內容（若有）。由 sprite 內「第一次」出現的 110 行決定，不會被後續 110 行覆寫。
    public string? Framerate;
    public List<SmoothRunAction> Actions = new();
}

// 命名成 SmoothRunAction（不叫 Action）避免跟 System.Action 委派型別撞名。
public sealed class SmoothRunAction
{
    public int LineIdx;
    /// 行首縮排（"\t" 等）。
    public string Indent = "";
    /// 主動作號（如 0/4/11/32/33）。
    public uint BaseAction;
    /// dash 副動作編號（`X-1`/`X-2` 語法 → 1／2，其他情況 null）。
    public uint? DashVariant;
    /// 動作名稱（已小寫且 trim，如 "walk"、"runl"、"runr onehandsword"）。
    public string Name = "";
    /// 括號內完整內容（"1 8,8.0:2 8.1:2 ..."）。
    public string Content = "";
    /// 解析自 content 的方向（0/1）。
    public uint Direction;
    /// 解析自 content 的幀數。
    public uint FrameCount;
    /// 第一張 spr 編號（content 第一個逗號後、第一個點前的數字）。
    public uint FirstSpr;
    /// 解析時「最近一次」110.X 的內容（在此 action 之前、同一個 sprite 內出現過的最近一次
    /// framerate）；若此 action 之前同 sprite 內沒有 110 行則為 null。
    public string? FramerateAtParse;
}

/// Sprite 角色分類。
public enum SpriteRole
{
    /// 純 walk sprite（有走路動作、無 RunL/RunR 訊號）— 映射目標。
    Walk,
    /// 純 run sprite（有 RunL/RunR 訊號、無走路動作）— 映射來源。
    Run,
    /// 既有走路也有 RunL/RunR — 自帶完整動作，不參與 cross-sprite 映射。
    Both,
    /// 既無走路也無 run 訊號。
    None,
}

/// RunL/RunR 萃取結果（不對稱 — 允許單側乾淨單側髒時只存乾淨那側；dash 變體 v1/v2 也各自可選）。
public sealed class RunPair
{
    public string? RunL;
    public string? RunR;
    public string? Framerate;
    /// 來源 run sprite 的 img_count（供 emit 階段更新 walk sprite header）。
    public uint SourceImgCount;
}
