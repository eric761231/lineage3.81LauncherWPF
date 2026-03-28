using System;
using System.Runtime.InteropServices;
using System.Text;

namespace LinLauncher.Services
{
    public class IniService
    {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        private static extern long WritePrivateProfileString(string section, string key, string value, string filePath);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        private static extern int GetPrivateProfileString(string section, string key, string defaultValue, StringBuilder retVal, int size, string filePath);

        private readonly string _path;

        public IniService(string path)
        {
            _path = System.IO.Path.GetFullPath(path);
        }

        public void Write(string section, string key, string value, string filePath = null)
        {
            WritePrivateProfileString(section, key, value, filePath ?? _path);
        }

        public string Read(string section, string key, string defaultValue = "", string filePath = null)
        {
            StringBuilder temp = new StringBuilder(1024);
            GetPrivateProfileString(section, key, defaultValue, temp, 1024, filePath ?? _path);
            return temp.ToString();
        }

        public int ReadInt(string section, string key, int defaultValue = 0)
        {
            string val = Read(section, key, defaultValue.ToString());
            return int.TryParse(val, out int result) ? result : defaultValue;
        }

        public bool ReadBool(string section, string key, bool defaultValue = false)
        {
            string val = Read(section, key, defaultValue ? "1" : "0");
            return val == "1";
        }

        public void WriteBool(string section, string key, bool value)
        {
            Write(section, key, value ? "1" : "0");
        }
    }
}
