namespace LinEncoder.Models
{
    /// <summary>補丁打包結果列表（對應 DataGrid 列）。</summary>
    public sealed class PatchFileRow
    {
        public int Index { get; set; }
        public string RelativePath { get; set; } = "";
        public long SizeBytes { get; set; }
        public string SizeKbDisplay => (SizeBytes / 1024.0).ToString("F1");
    }
}
