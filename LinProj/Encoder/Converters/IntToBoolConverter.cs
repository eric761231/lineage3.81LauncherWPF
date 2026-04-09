using System;
using System.Globalization;
using System.Windows.Data;

namespace LinEncoder.Converters
{
    public class IntToBoolConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            if (value is int intValue && parameter is string paramString)
            {
                return intValue == int.Parse(paramString);
            }
            return false;
        }

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