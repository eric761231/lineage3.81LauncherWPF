using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace LinEncoder.Services
{
    public class IniService
    {
        private string _path;
        private Dictionary<string, Dictionary<string, string>> _data = new Dictionary<string, Dictionary<string, string>>(StringComparer.OrdinalIgnoreCase);

        public IniService(string path)
        {
            _path = path;
            Load();
        }

        public void Load()
        {
            try
            {
                _data.Clear();
                if (!File.Exists(_path)) return;

                string currentSection = "";
                foreach (var line in File.ReadAllLines(_path, Encoding.UTF8))
                {
                    string trimmedLine = line.Trim();
                    if (string.IsNullOrWhiteSpace(trimmedLine) || trimmedLine.StartsWith(";") || trimmedLine.StartsWith("#"))
                        continue;

                    if (trimmedLine.StartsWith("[") && trimmedLine.EndsWith("]"))
                    {
                        currentSection = trimmedLine.Substring(1, trimmedLine.Length - 2).Trim();
                        if (!_data.ContainsKey(currentSection))
                            _data[currentSection] = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
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

        public void Write(string section, string key, string value)
        {
            if (!_data.ContainsKey(section))
                _data[section] = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            
            _data[section][key] = value;
            Save();
        }

        public void WriteBool(string section, string key, bool value) => Write(section, key, value.ToString());

        public string Read(string section, string key, string def = "")
        {
            if (_data.TryGetValue(section, out var keys) && keys.TryGetValue(key, out var value))
            {
                return value;
            }
            return def;
        }

        public bool ReadBool(string section, string key, bool def = false)
        {
            string value = Read(section, key, "");
            if (bool.TryParse(value, out bool result)) return result;
            if (value == "1") return true;
            if (value == "0") return false;
            return def;
        }

        public int ReadInt(string section, string key, int def = 0)
        {
            if (int.TryParse(Read(section, key, ""), out int result)) return result;
            return def;
        }
    }
}