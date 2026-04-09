using System.Runtime.InteropServices;
using System.Text;

namespace LinEncoder.Services
{
    public class IniService
    {
        private string _path;
        [DllImport("kernel32", CharSet = CharSet.Unicode)]
        private static extern long WritePrivateProfileString(string section, string key, string val, string filePath);
        [DllImport("kernel32", CharSet = CharSet.Unicode)]
        private static extern int GetPrivateProfileString(string section, string key, string def, StringBuilder retVal, int size, string filePath);

        public IniService(string path) { _path = path; }
        public void Write(string section, string key, string value) => WritePrivateProfileString(section, key, value, _path);
        public void WriteBool(string section, string key, bool value) => Write(section, key, value.ToString());
        public string Read(string section, string key, string def = "", string? alternatePath = null)
        {
            var res = new StringBuilder(255);
            GetPrivateProfileString(section, key, def, res, 255, alternatePath ?? _path);
            return res.ToString();
        }
        public bool ReadBool(string section, string key, bool def = false) => bool.TryParse(Read(section, key, def.ToString()), out bool r) ? r : def;
        public int ReadInt(string section, string key, int def = 0) => int.TryParse(Read(section, key, def.ToString()), out int r) ? r : def;
    }
}