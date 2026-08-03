using System;

namespace LinEncoder.Services
{
    /// <summary>
    /// 伺服器綑綁用的 RSA-32 金鑰生成，移植自 L1J3.8Launcher(RUST)參考 src/rsa32.rs。
    /// 對齊 TGG EP6 server 端 Java 的小型 RSA：
    ///   server 端接受連線時 random∈[255, 900000254]，authdata = random^E mod N（4 bytes 小端送給客戶端）
    ///   客戶端用 D 還原：random = authdata^D mod N，取得後 xorByte = random%255 + 1
    /// N 必須塞進 uint，用兩個 ~16-bit 質數 p、q 相乘，n = p*q ∈ [2^30, 2^32)。
    /// </summary>
    public static class Rsa32Service
    {
        public readonly struct Rsa32
        {
            public readonly uint E;
            public readonly uint D;
            public readonly uint N;
            public Rsa32(uint e, uint d, uint n) { E = e; D = d; N = n; }
        }

        private static readonly ulong[] Witnesses = { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37 };

        private static ulong MulMod(ulong a, ulong b, ulong m)
        {
            return (ulong)((UInt128)a * b % m);
        }

        public static ulong ModPow(ulong baseVal, ulong exp, ulong m)
        {
            if (m == 1) return 0;
            ulong result = 1;
            baseVal %= m;
            while (exp > 0)
            {
                if ((exp & 1) == 1)
                    result = MulMod(result, baseVal, m);
                exp >>= 1;
                baseVal = MulMod(baseVal, baseVal, m);
            }
            return result;
        }

        private static ulong Gcd(ulong a, ulong b)
        {
            while (b != 0)
            {
                ulong t = b;
                b = a % b;
                a = t;
            }
            return a;
        }

        private static (long g, long x, long y) ExtGcd(long a, long b)
        {
            if (b == 0) return (a, 1, 0);
            var (g, x1, y1) = ExtGcd(b, a % b);
            return (g, y1, x1 - (a / b) * y1);
        }

        private static ulong? ModInverse(ulong a, ulong m)
        {
            var (g, x, _) = ExtGcd((long)a, (long)m);
            if (g != 1) return null;
            long mi = (long)m;
            return (ulong)((x % mi + mi) % mi);
        }

        private static bool IsPrime(ulong n)
        {
            if (n < 2) return false;
            foreach (ulong p in Witnesses)
            {
                if (n == p) return true;
                if (n % p == 0) return false;
            }
            ulong d = n - 1;
            uint r = 0;
            while ((d & 1) == 0) { d >>= 1; r++; }
            foreach (ulong a in Witnesses)
            {
                ulong x = ModPow(a, d, n);
                if (x == 1 || x == n - 1) continue;
                bool witnessComposite = true;
                for (int i = 0; i < r - 1; i++)
                {
                    x = MulMod(x, x, n);
                    if (x == n - 1) { witnessComposite = false; break; }
                }
                if (witnessComposite) return false;
            }
            return true;
        }

        private static ulong RandomPrime16Bit(Random rng)
        {
            while (true)
            {
                ulong n = (ulong)rng.NextInt64(40000, 65536);
                n |= 1;
                if (IsPrime(n)) return n;
            }
        }

        /// <summary>產生 RSA-32 金鑰：N ∈ [2^30, 2^32)，E、D 也 &lt; N。</summary>
        public static Rsa32 Generate()
        {
            var rng = new Random();
            while (true)
            {
                ulong p = RandomPrime16Bit(rng);
                ulong q;
                do { q = RandomPrime16Bit(rng); } while (q == p);

                ulong n = p * q;
                if (n > uint.MaxValue) continue;
                if (n < (1UL << 30)) continue;

                ulong phi = (p - 1) * (q - 1);
                for (int attempt = 0; attempt < 1024; attempt++)
                {
                    ulong d = (ulong)rng.NextInt64(3, (long)phi);
                    if ((d & 1) == 0) continue;
                    if (Gcd(d, phi) != 1) continue;

                    ulong? eOpt = ModInverse(d, phi);
                    if (eOpt is not ulong e || e <= 1 || e >= phi) continue;

                    ulong probe = Math.Max(n / 7, 2);
                    ulong cipher = ModPow(probe, e, n);
                    ulong plain = ModPow(cipher, d, n);
                    if (plain == probe)
                        return new Rsa32((uint)e, (uint)d, (uint)n);
                }
            }
        }
    }
}
