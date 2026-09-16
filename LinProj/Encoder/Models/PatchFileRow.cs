namespace LinEncoder.Models
{
    /// <summary>
    /// 補丁打包結果列表項目資料模型（對應 DataGrid 列）。
    /// </summary>
    public sealed class PatchFileRow
    {
        /// <summary>
        /// 項目編號索引。
        /// </summary>
        public int Index { get; set; }

        /// <summary>
        /// 檔案相對路徑。
        /// </summary>
        public string RelativePath { get; set; } = "";

        /// <summary>
        /// 檔案大小（位元組）。
        /// </summary>
        public long SizeBytes { get; set; }

        /// <summary>
        /// 檔案大小顯示格式（KB）。
        /// </summary>
        public string SizeKbDisplay => (SizeBytes / 1024.0).ToString("F1");
    }
}
