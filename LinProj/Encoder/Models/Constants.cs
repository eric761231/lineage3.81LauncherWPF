namespace LinEncoder.Models
{
    /// <summary>
    /// 全域常數類別，須與 LinLauncher.Models.Constants 一致，否則 list.txt ServerData 無法被登入器解密。
    /// </summary>
    public static class Constants
    {
        /// <summary>
        /// 伺服器列表加解密金鑰。
        /// </summary>
        public const string ServerListKey = "4zF8sAc5bYkCRM3w";

        /// <summary>
        /// 檔案加解密金鑰。
        /// </summary>
        public const string FileEncryptKey = "PAt82IqEvNBmERYl";

        /// <summary>
        /// 伺服器列表 RSA 解密之 XOR D 混淆常數。
        /// </summary>
        public const uint ServerListRsaXorD = 32345678;

        /// <summary>
        /// 伺服器列表 RSA 解密之 XOR N 混淆常數。
        /// </summary>
        public const uint ServerListRsaXorN = 22345678;
    }
}
