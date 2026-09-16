using System;
using System.Globalization;
using System.Windows.Data;

namespace LinEncoder.Converters
{
    /// <summary>
    /// 整數與布林值雙向轉換器，用於比對整數值是否相符。
    /// </summary>
    public class IntToBoolConverter : IValueConverter
    {
        /// <summary>
        /// 將整數值與傳入之字串參數進行比對，相符回傳 true，否則回傳 false。
        /// </summary>
        /// <param name="value">輸入值（整數）</param>
        /// <param name="targetType">目標屬性型別</param>
        /// <param name="parameter">目標數值字串</param>
        /// <param name="culture">區域語系資訊</param>
        /// <return>比對結果布林值</return>
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            if (value is int intValue && parameter is string paramString)
            {
                return intValue == int.Parse(paramString);
            }
            return false;
        }

        /// <summary>
        /// 將布林值反向轉換回原本代表的整數數值。
        /// </summary>
        /// <param name="value">輸入值（布林值）</param>
        /// <param name="targetType">來源型別</param>
        /// <param name="parameter">目標數值字串</param>
        /// <param name="culture">區域語系資訊</param>
        /// <return>解析出的整數，或 Binding.DoNothing</return>
        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        {
            if (value is bool boolValue && boolValue && parameter is string paramString)
            {
                return int.Parse(paramString);
            }
            return Binding.DoNothing;
        }
    }
}
