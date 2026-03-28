using System;
using System.Globalization;
using System.Windows.Data;

namespace LinLauncher.Converters
{
    /// <summary>
    /// 將整數 (int) 與字串參數比較，轉換為布林值 (bool) 的 WPF 轉換器。
    ///
    /// 初學說明：當你在 XAML 中使用資料綁定 (Binding) 時，常會需要把一個整數欄位和
    /// UI 元件（像是 RadioButton）綁定在一起。可以把相同的整數用作參數傳入，當來源值
    /// 與參數相等時，顯示為選取 (true)。
    /// </summary>
    public class IntToBoolConverter : IValueConverter
    {
        /// <summary>
        /// 將來源值 (value) 與參數 (parameter) 比較，若相等回傳 true，否則回傳 false。
        /// value 預期為 int，parameter 預期為代表整數的字串，例如 "1"。
        /// </summary>
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            // 檢查來源值是否為 int，且參數是否為字串
            if (value is int intValue && parameter is string paramString)
            {
                // 解析字串參數為 int 並比較是否相等，若解析失敗會拋例外（可視需求再加上例外處理）
                return intValue == int.Parse(paramString);
            }

            // 型別不符合時預設回傳 false（表示未選取）
            return false;
        }

        /// <summary>
        /// 將 UI 回傳的布林值轉回整數。
        /// 當 UI 回傳 true 且參數是字串時，會回傳該參數解析後的整數；
        /// 否則回傳 Binding.DoNothing 表示不更新來源值。
        /// </summary>
        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        {
            // 當控制項回傳 true（被選取），並且 parameter 是字串，則把參數解析為 int 並回傳
            if (value is bool boolValue && boolValue && parameter is string paramString)
            {
                return int.Parse(paramString);
            }

            // 不要變更來源值（例如 RadioButton 未被選取時），回傳 Binding.DoNothing
            return Binding.DoNothing;
        }
    }
}
