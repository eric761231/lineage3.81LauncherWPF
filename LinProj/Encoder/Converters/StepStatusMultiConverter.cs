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

        /// <summary>
        /// 將多重布林值（完成狀態與過時狀態）轉換為對應之燈號 Brush 顏色。
        /// </summary>
        /// <param name="values">包含完成與過時狀態之布林陣列</param>
        /// <param name="targetType">目標屬性型別</param>
        /// <param name="parameter">轉換參數</param>
        /// <param name="culture">區域語系資訊</param>
        /// <return>對應狀態之 SolidColorBrush</return>
        public object Convert(object[] values, Type targetType, object parameter, CultureInfo culture)
        {
            bool done = values.Length > 0 && values[0] is bool b0 && b0;
            bool stale = values.Length > 1 && values[1] is bool b1 && b1;
            if (stale)
            {
                return Orange;
            }
            return done ? Green : Gray;
        }

        /// <summary>
        /// 多重反向轉換（未實現）。
        /// </summary>
        /// <param name="value">綁定目標值</param>
        /// <param name="targetTypes">來源型別陣列</param>
        /// <param name="parameter">轉換參數</param>
        /// <param name="culture">區域語系資訊</param>
        /// <return>拋出 NotImplementedException 異常</return>
        public object[] ConvertBack(object value, Type[] targetTypes, object parameter, CultureInfo culture)
        {
            throw new NotImplementedException();
        }
    }
}
