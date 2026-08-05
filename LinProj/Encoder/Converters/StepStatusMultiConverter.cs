using System;
using System.Globalization;
using System.Windows.Data;
using System.Windows.Media;

namespace LinEncoder.Converters
{
    /// <summary>
    /// 常駐流程導覽面板用：兩個 bool 輸入（是否已完成、是否因為前一步驟重做而過時）合成一顆燈號。
    /// 過時（第二個值 true）優先顯示橘色，其次已完成＝綠色，都不是＝灰色。
    /// 用來表示「有真正技術依據的順序錯誤」（例如金鑰重新產生後 list.txt 沒跟著重新產生）。
    /// </summary>
    public class StepStatusMultiConverter : IMultiValueConverter
    {
        private static readonly SolidColorBrush Green = new SolidColorBrush(System.Windows.Media.Color.FromRgb(0x2E, 0xCC, 0x40));
        private static readonly SolidColorBrush Gray = new SolidColorBrush(System.Windows.Media.Color.FromRgb(0x66, 0x66, 0x66));
        private static readonly SolidColorBrush Orange = new SolidColorBrush(System.Windows.Media.Color.FromRgb(0xFF, 0x85, 0x1B));

        public object Convert(object[] values, Type targetType, object parameter, CultureInfo culture)
        {
            bool done = values.Length > 0 && values[0] is bool b0 && b0;
            bool stale = values.Length > 1 && values[1] is bool b1 && b1;
            if (stale) return Orange;
            return done ? Green : Gray;
        }

        public object[] ConvertBack(object value, Type[] targetTypes, object parameter, CultureInfo culture)
            => throw new NotImplementedException();
    }
}
