using System;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace LinLauncher.Services
{
    /// <summary>
    /// 向遊戲伺服器查詢目前狀態（在線人數／人數上限／使用率）。
    /// 伺服器（AcceptDispatcher.java）共用遊戲 port：連線後立刻收到本查詢送出的
    /// 4 bytes ASCII marker "STAT" 就會回一行純文字，然後關閉連線；
    /// 沒收到 marker 的連線（真正的遊戲客戶端）則完全不受影響。
    /// 支援的回應：
    ///   - "{usagePercent}" 或 "{usagePercent}%"（目前主用）
    ///   - "{online}/{max}"（舊版 AcceptDispatcher）
    ///   - "MAINT" / "MAINT:{剩餘秒數}"
    /// 若 TCP 已連上但回應無法解析（例如該端未實作 STAT、回了遊戲握手封包），
    /// 視為「在線、負載未知」，不要顯示成關閉——否則開著的服會被誤判成灰燈。
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
        /// 連線至 host:port，送出狀態查詢 marker，讀回一行狀態並解析。
        /// </summary>
        public static async Task<StatusResult> QueryStatusAsync(string? host, int port, int timeoutMs, CancellationToken cancellationToken = default)
        {
            if (string.IsNullOrWhiteSpace(host) || port <= 0 || port > 65535)
            {
                return new StatusResult { Ok = false, ErrorSummary = "清單中的 IP/主機名稱或埠號無效" };
            }
            string h = host.Trim();
            using var tcp = new TcpClient();
            try
            {
                using var linked = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
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
                    if (n <= 0)
                        break;
                    total += n;
                    if (Array.IndexOf(buffer, (byte)'\n', 0, total) >= 0)
                        break;
                }

                string text = Encoding.ASCII.GetString(buffer, 0, total).Trim();
                if (TryParseStatusText(text, out StatusResult parsed))
                    return parsed;

                // TCP 已連上、但 STAT 回應無法辨識：當「在線／負載未知」而非關閉。
                return new StatusResult
                {
                    Ok = true,
                    Online = 0,
                    Max = 100,
                    ErrorSummary = string.IsNullOrEmpty(text)
                        ? "已連線（無 STAT 回應，負載未知）"
                        : "已連線（STAT 格式無法辨識，負載未知）"
                };
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

        /// <summary>解析 STAT 回應文字；成功回傳 true。</summary>
        internal static bool TryParseStatusText(string text, out StatusResult result)
        {
            result = default;
            if (string.IsNullOrWhiteSpace(text))
                return false;

            if (text.StartsWith("MAINT", StringComparison.OrdinalIgnoreCase))
            {
                long remaining = -1;
                int colon = text.IndexOf(':');
                if (colon >= 0 && colon < text.Length - 1)
                    long.TryParse(text.Substring(colon + 1), out remaining);
                result = new StatusResult
                {
                    Ok = true,
                    IsMaintenance = true,
                    MaintenanceRemainingSeconds = remaining
                };
                return true;
            }

            string cleanText = text.TrimEnd('\r', '\n', '%').Trim();

            // 舊版 "{online}/{max}"
            int slash = cleanText.IndexOf('/');
            if (slash > 0 && slash < cleanText.Length - 1
                && int.TryParse(cleanText.AsSpan(0, slash), out int online)
                && int.TryParse(cleanText.AsSpan(slash + 1), out int max)
                && max > 0 && online >= 0)
            {
                int usage = (int)Math.Round(100.0 * online / max, MidpointRounding.AwayFromZero);
                if (usage > 100) usage = 100;
                result = new StatusResult { Ok = true, Online = usage, Max = 100 };
                return true;
            }

            // 目前主用：純使用率百分比（可帶 %）
            if (double.TryParse(cleanText, System.Globalization.NumberStyles.Any,
                    System.Globalization.CultureInfo.InvariantCulture, out double rate))
            {
                int usagePercent = (int)Math.Round(rate, MidpointRounding.AwayFromZero);
                if (usagePercent < 0) usagePercent = 0;
                if (usagePercent > 100) usagePercent = 100;
                result = new StatusResult { Ok = true, Online = usagePercent, Max = 100 };
                return true;
            }

            return false;
        }
    }
}
