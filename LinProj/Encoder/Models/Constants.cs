// Constants.cs: 定義加密與伺服器列表相關的常數金鑰。
namespace LinLauncher.Models
{
    public static class Constants
    {
        public const string ServerListKey = "4zF8sAc5bYkCRM3w";
        public const string FileEncryptKey = "PAt82IqEvNBmERYl";
        
        public const uint ServerListRsaXorD = 32345678u;
        public const uint ServerListRsaXorN = 22345678u;
    }
}
