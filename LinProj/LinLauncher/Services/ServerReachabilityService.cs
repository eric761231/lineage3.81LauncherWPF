using System;
using System.IO;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace LinLauncher.Services
{
    /// <summary>
    /// 向遊戲伺服器查詢目前狀態（在線人數／人數上限）。
    /// 伺服器（AcceptDispatcher.java）共用遊戲 port：連線後立刻收到本查詢送出的
    /// 4 bytes ASCII marker "STAT" 就會回一行純文字 "{online}/{max}\n" 然後關閉連線；
    /// 沒收到 marker 的連線（真正的遊戲客戶端）則完全不受影響，照原本流程走。
    /// </summary>
    public static class ServerReachabilityService
    {
        public readonly struct StatusResult
        {
            public bool Ok { get; init; }
            public int Online { get; init; }
            public int Max { get; init; }
            /// <summary>伺服器目前是否處於維護中（回應為 "MAINT" 時）。</summary>
            public bool IsMaintenance { get; init; }
            /// <summary>維護中時距離結束還剩幾秒；非維護中或未知為 -1。</summary>
            public long MaintenanceRemainingSeconds { get; init; }
            /// <summary>失敗時簡短說明（含 Socket 錯誤類型時較易除錯）。</summary>
            public string? ErrorSummary { get; init; }
        }

        private static readonly byte[] StatusQueryMarker = Encoding.ASCII.GetBytes("STAT");

        /// <summary>
        /// 連線至 host:port，送出狀態查詢 marker，讀回一行 "{online}/{max}" 並解析。
        /// </summary>
        public static async Task<StatusResult> QueryStatusAsync(string? host, int port, int timeoutMs, CancellationToken cancellationToken = default)
        {
            if (string.IsNullOrWhiteSpace(host) || port <= 0 || port > 65535)
            {
                return new StatusResult { Ok = false, ErrorSummary = "清單中的 IP/主機名稱或埠號無效" };
            }
            string h = host.Trim(); /// hostname 可能有空白，Trim() 後再傳給 TcpClient.ConnectAsync()，否則會失敗。
            using var tcp = new TcpClient(); /// 建立TcpClient 會自動使用 IPv4 或 IPv6，依系統設定而定。
            try
            {
                using var linked = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken); /// 連線逾時或被取消時，會拋出 OperationCanceledException。
                linked.CancelAfter(timeoutMs);

                await tcp.ConnectAsync(h, port, linked.Token).ConfigureAwait(false);
                tcp.NoDelay = true;

                var stream = tcp.GetStream();
                await stream.WriteAsync(StatusQueryMarker.AsMemory(), linked.Token).ConfigureAwait(false);
                await stream.FlushAsync(linked.Token).ConfigureAwait(false);

                var buffer = new byte[64];
                int total = 0;
                while (total < buffer.Length)
                {
                    int n = await stream.ReadAsync(buffer.AsMemory(total, buffer.Length - total), linked.Token).ConfigureAwait(false);
                    if (n <= 0) {
                        break;
                    }
                    total += n;
                    if (Array.IndexOf(buffer, (byte)'\n', 0, total) >= 0) 
                    {
                        break;
                    }
                }

                string text = Encoding.ASCII.GetString(buffer, 0, total).Trim();

                if (text.StartsWith("MAINT", StringComparison.OrdinalIgnoreCase))
                {
                    // 格式為 "MAINT" 或 "MAINT:{剩餘秒數}"
                    long remaining = -1;
                    int colon = text.IndexOf(':');
                    if (colon >= 0 && colon < text.Length - 1)
                    {
                        long.TryParse(text.Substring(colon + 1), out remaining);
                    }
                    return new StatusResult { Ok = true, IsMaintenance = true, MaintenanceRemainingSeconds = remaining };
                }

                // ------------------------------------------------------------------
                // 解析使用率 (例如: "46%" 或 "45.5%")
                // ------------------------------------------------------------------
                // 1. 移除字串末尾的 % 符號與換行空白
                string cleanText = text.TrimEnd('\r', '\n', '%').Trim();
                // 2. 嘗試解析為浮點數（若 Java 傳回整數如 "46%" 或小數如 "45.5%" 皆可相容）
                if (!double.TryParse(cleanText, System.Globalization.NumberStyles.Any, System.Globalization.CultureInfo.InvariantCulture, out double rate))
                {
                    return new StatusResult { Ok = false, ErrorSummary = $"回應格式異常：{text}" };
                }
                // 3. 四捨五入成整數百分比 (例如 45.5% -> 46%)
                int usagePercent = (int)Math.Round(rate, MidpointRounding.AwayFromZero);
 
                // 將使用率填入 Online，上限固定設為 100
                return new StatusResult { Ok = true, Online = usagePercent, Max = 100 };
            }
            catch (OperationCanceledException)
            {
                return new StatusResult { Ok = false, ErrorSummary = "逾時或被取消（請確認網路與防火牆）" };
            }
            catch (SocketException ex)
            {
                string hint = ex.SocketErrorCode switch
                {
                    SocketError.ConnectionRefused => "連線被拒，該埠可能無服務",
                    SocketError.TimedOut => "連線逾時（網路不通、防火牆或路由阻擋）",
                    SocketError.HostNotFound => "找不到主機",
                    SocketError.NetworkUnreachable => "網路無法到達目標",
                    SocketError.AccessDenied => "存取被拒（本機防火牆或安全性軟體可能阻擋）",
                    _ => ex.Message
                };
                return new StatusResult { Ok = false, ErrorSummary = $"{hint}（{ex.SocketErrorCode}）" };
            }
            catch (Exception ex)
            {
                return new StatusResult { Ok = false, ErrorSummary = ex.Message };
            }
        }
    }
}
