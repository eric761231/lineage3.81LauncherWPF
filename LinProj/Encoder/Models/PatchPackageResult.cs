using System.Collections.Generic;

namespace LinEncoder.Models
{
    /// <summary>
    /// 補丁封包打包結果資料模型。
    /// </summary>
    public sealed class PatchPackageResult
    {
        /// <summary>
        /// 是否打包成功。
        /// </summary>
        public bool Success { get; init; }

        /// <summary>
        /// 錯誤訊息 (若失敗)。
        /// </summary>
        public string? ErrorMessage { get; init; }

        /// <summary>
        /// 更新列表檔案路徑。
        /// </summary>
        public string? UpdateListPath { get; init; }

        /// <summary>
        /// 打包包含的檔案列表。
        /// </summary>
        public List<PatchFileRow> Files { get; init; } = new();
    }
}
