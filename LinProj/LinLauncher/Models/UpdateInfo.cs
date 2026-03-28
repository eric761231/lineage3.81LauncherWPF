// UpdateInfo.cs: 包含更新檔案資訊與進度事件類別。
namespace LinLauncher.Models
{
    public class UpdateInfo
    {
        public string Filename { get; set; } = "";
        public string Md5 { get; set; } = "";
    }

    public class UpdateProgressEventArgs
    {
        public int OverallPercent { get; set; }
        public string CurrentFile { get; set; } = "";
        public int FilePercent { get; set; }
    }
}
