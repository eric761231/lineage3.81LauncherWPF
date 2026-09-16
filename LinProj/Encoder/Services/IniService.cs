using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace LinEncoder.Services
{
    /// <summary>
    /// INI 格式組態檔案讀寫管理服務類別。
    /// </summary>
    public class IniService
    {
        private string _path;
        private Dictionary<string, Dictionary<string, string>> _data = new Dictionary<string, Dictionary<string, string>>(StringComparer.OrdinalIgnoreCase);

        /// <summary>
        /// 建構函式，傳入 INI 檔案路徑並自動進行載入。
        /// </summary>
        /// <param name="path">INI 檔案路徑</param>
        public IniService(string path)
        {
            _path = path;
            Load();
        }

        /// <summary>
        /// 自指定的 INI 檔案載入 Section 與 Key-Value 資料。
        /// </summary>
        public void Load()
        {
            try
            {
                _data.Clear();
                if (!File.Exists(_path))
                {
                    return;
                }

                string currentSection = "";
                foreach (var line in File.ReadAllLines(_path, Encoding.UTF8))
                {
                    string trimmedLine = line.Trim();
                    if (string.IsNullOrWhiteSpace(trimmedLine) || trimmedLine.StartsWith(";") || trimmedLine.StartsWith("#"))
                    {
                        continue;
                    }

                    if (trimmedLine.StartsWith("[") && trimmedLine.EndsWith("]"))
                    {
                        currentSection = trimmedLine.Substring(1, trimmedLine.Length - 2).Trim();
                        if (!_data.ContainsKey(currentSection))
                        {
                            _data[currentSection] = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                        }
                    }
                    else if (trimmedLine.Contains("="))
                    {
                        int index = trimmedLine.IndexOf('=');
                        string key = trimmedLine.Substring(0, index).Trim();
                        string value = trimmedLine.Substring(index + 1).Trim();

                        if (!string.IsNullOrEmpty(currentSection))
                        {
                            _data[currentSection][key] = value;
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                System.Windows.MessageBox.Show($"載入設定檔時出錯：{_path}\n{ex.Message}", "INI 載入錯誤", System.Windows.MessageBoxButton.OK, System.Windows.MessageBoxImage.Warning);
            }
        }

        /// <summary>
        /// 將內部字典資料寫回至 INI 檔案中。
        /// </summary>
        public void Save()
        {
            var sb = new StringBuilder();
            foreach (var section in _data)
            {
                sb.AppendLine($"[{section.Key}]");
                foreach (var kvp in section.Value)
                {
                    sb.AppendLine($"{kvp.Key}={kvp.Value}");
                }
                sb.AppendLine();
            }
            File.WriteAllText(_path, sb.ToString(), Encoding.UTF8);
        }

        /// <summary>
        /// 寫入指定 Section 與 Key 的字串數值並立即儲存。
        /// </summary>
        /// <param name="section">Section 名稱</param>
        /// <param name="key">Key 名稱</param>
        /// <param name="value">寫入之字串值</param>
        public void Write(string section, string key, string value)
        {
            if (!_data.ContainsKey(section))
            {
                _data[section] = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            }
            
            _data[section][key] = value;
            Save();
        }

        /// <summary>
        /// 寫入布林值。
        /// </summary>
        /// <param name="section">Section 名稱</param>
        /// <param name="key">Key 名稱</param>
        /// <param name="value">寫入之布林值</param>
        public void WriteBool(string section, string key, bool value) => Write(section, key, value.ToString());

        /// <summary>
        /// 讀取字串數值。
        /// </summary>
        /// <param name="section">Section 名稱</param>
        /// <param name="key">Key 名稱</param>
        /// <param name="def">預設值</param>
        /// <return>讀取之字串，不存在回傳預設值</return>
        public string Read(string section, string key, string def = "")
        {
            if (_data.TryGetValue(section, out var keys) && keys.TryGetValue(key, out var value))
            {
                return value;
            }
            return def;
        }

        /// <summary>
        /// 讀取布林數值。
        /// </summary>
        /// <param name="section">Section 名稱</param>
        /// <param name="key">Key 名稱</param>
        /// <param name="def">預設值</param>
        /// <return>讀取之布林值</return>
        public bool ReadBool(string section, string key, bool def = false)
        {
            string value = Read(section, key, "");
            if (bool.TryParse(value, out bool result))
            {
                return result;
            }
            if (value == "1")
            {
                return true;
            }
            if (value == "0")
            {
                return false;
            }
            return def;
        }

        /// <summary>
        /// 讀取整數數值。
        /// </summary>
        /// <param name="section">Section 名稱</param>
        /// <param name="key">Key 名稱</param>
        /// <param name="def">預設值</param>
        /// <return>讀取之整數</return>
        public int ReadInt(string section, string key, int def = 0)
        {
            if (int.TryParse(Read(section, key, ""), out int result))
            {
                return result;
            }
            return def;
        }
    }
}
