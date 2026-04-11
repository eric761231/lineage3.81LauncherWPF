using System;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;

namespace LinLauncher.Services
{
    /// <summary>
    /// 以 TCP 連線測試遊戲伺服器是否在監聽指定埠（與多數登入／遊戲握手相同為 TCP）。
    /// 若伺服器僅開 UDP、或清單埠與實際監聽埠不同，仍會判定為無法連線。
    /// </summary>
    public static class ServerReachabilityService
    {
        public readonly struct TcpProbeResult
        {
            public bool Ok { get; init; }
            /// <summary>失敗時簡短說明（含 Socket 錯誤類型時較易除錯）。</summary>
            public string? ErrorSummary { get; init; }
        }

        /// <summary>
        /// 嘗試連線至 host:port，成功建立 TCP 連線即視為「伺服器有開啟」。
        /// </summary>
        public static async Task<TcpProbeResult> ProbeAsync(string? host, int port, int timeoutMs, CancellationToken cancellationToken = default)
        {
            if (string.IsNullOrWhiteSpace(host) || port <= 0 || port > 65535)
                return new TcpProbeResult { Ok = false, ErrorSummary = "清單中的 IP/主機名稱或埠號無效" };

            string h = host.Trim();
            using var tcp = new TcpClient();
            try
            {
                using var linked = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
                linked.CancelAfter(timeoutMs);
                await tcp.ConnectAsync(h, port, linked.Token).ConfigureAwait(false);
                return new TcpProbeResult { Ok = tcp.Connected };
            }
            catch (OperationCanceledException)
            {
                return new TcpProbeResult { Ok = false, ErrorSummary = "逾時或被取消（請確認網路與防火牆）" };
            }
            catch (SocketException ex)
            {
                string hint = ex.SocketErrorCode switch
                {
                    SocketError.ConnectionRefused => "連線被拒（該埠可能無服務、或伺服器未監聽此埠）",
                    SocketError.TimedOut => "連線逾時（網路不通、防火牆或路由阻擋）",
                    SocketError.HostNotFound => "找不到主機（DNS 或清單 IP／名稱有誤）",
                    SocketError.NetworkUnreachable => "網路無法到達目標",
                    SocketError.AccessDenied => "存取被拒（本機防火牆或安全性軟體可能阻擋）",
                    _ => ex.Message
                };
                return new TcpProbeResult { Ok = false, ErrorSummary = $"{hint}（{ex.SocketErrorCode}）" };
            }
            catch (Exception ex)
            {
                return new TcpProbeResult { Ok = false, ErrorSummary = ex.Message };
            }
        }
    }
}
