using System;
using System.Globalization;
using System.Windows.Data;
using System.Windows.Media;

namespace LinEncoder.Converters
{
    /// <summary>
    /// 常駐流程導覽面板用：true=綠色（已完成/最新），false=灰色（尚未做/待處理）。
    /// 傳 ConverterParameter="Warn" 時 false 改用橘色（表示「需要處理」而不是單純「還沒做」，
    /// 例如原始碼比已建置版本新，語意上比灰色更急迫）。
    /// </summary>
    public class BoolToStatusBrushConverter : IValueConverter
    {
        private static readonly SolidColorBrush Green = new SolidColorBrush(System.Windows.Media.Color.FromRgb(0x2E, 0xCC, 0x40));
        private static readonly SolidColorBrush Gray = new SolidColorBrush(System.Windows.Media.Color.FromRgb(0x66, 0x66, 0x66));
        private static readonly SolidColorBrush Orange = new SolidColorBrush(System.Windows.Media.Color.FromRgb(0xFF, 0x85, 0x1B));

        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            bool b = value is bool v && v;
            if (b) return Green;
            return string.Equals(parameter as string, "Warn", StringComparison.OrdinalIgnoreCase) ? Orange : Gray;
        }

        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
            => throw new NotImplementedException();
    }
}
