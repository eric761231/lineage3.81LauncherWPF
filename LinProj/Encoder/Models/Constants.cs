namespace LinEncoder.Models
{
    /// <summary>須與 LinLauncher.Models.Constants 一致，否則 list.txt ServerData 無法被登入器解密。</summary>
    public static class Constants
    {
        public const string ServerListKey = "4zF8sAc5bYkCRM3w";
        public const string FileEncryptKey = "PAt82IqEvNBmERYl";
        public const uint ServerListRsaXorD = 32345678;
        public const uint ServerListRsaXorN = 22345678;
    }
}