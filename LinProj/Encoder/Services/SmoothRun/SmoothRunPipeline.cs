namespace LinEncoder.Services.SmoothRun;

// 5 階段 pipeline 串接。對照 L1J3.8Launcher(RUST)參考\src\smooth_run\pipeline.rs
// 的 process_variant_lines_pipeline（等價 legacy 的 process_variant_lines，輸出 byte-equal）。
public static class SmoothRunPipeline
{
    public static string ProcessVariantLines(string text)
    {
        var sf = SmoothRunParser.Parse(text);
        var roles = SmoothRunClassifier.Classify(sf);
        var runs = SmoothRunExtractor.Extract(sf, roles);
        var walkToRun = SmoothRunPairer.PairWalksToRuns(sf, roles, runs);
        return SmoothRunEmitter.Emit(sf, walkToRun);
    }
}
