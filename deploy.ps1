# deploy.ps1 (Final Stable Version)
$ErrorActionPreference = "Continue"

Write-Host ">>> [1/5] Initializing paths..." -ForegroundColor Cyan
$Root = $PSScriptRoot
if ([string]::IsNullOrEmpty($Root)) { $Root = Get-Location }

$EncoderProj = "$Root\LinProj\Encoder\Encoder.csproj"
$LauncherProj = "$Root\LinProj\LinLauncher\LinLauncher.csproj"
$PartnerDir = "$Root\EncoderForPartners"
$EnvDir = "$PartnerDir\LinLauncher_Environment"
$IconPath = "$Root\LinProj\LinLauncher\Assets\LinLauncher.ico"

Write-Host ">>> [2/5] Stopping running processes..." -ForegroundColor Cyan
try {
    taskkill /F /IM encoder.exe /T /FI "STATUS eq RUNNING" 2>$null
    taskkill /F /IM LinLauncher.exe /T /FI "STATUS eq RUNNING" 2>$null
} catch { }
Start-Sleep -Seconds 1

Write-Host ">>> [3/5] Compiling projects (dotnet publish)..." -ForegroundColor Cyan
Write-Host "    - Restoring packages..."
dotnet restore "$Root\LinProj\LauncherWPF381.sln"

Write-Host "    - Publishing Encoder..."
dotnet publish "$EncoderProj" -c Release -r win-x86 --no-restore --self-contained true -p:PublishSingleFile=true -o "$Root\LinProj\Encoder\publish"
Write-Host "    - Publishing Launcher..."
dotnet publish "$LauncherProj" -c Release -r win-x86 --no-restore --self-contained true -o "$Root\LinProj\LinLauncher\publish"

Write-Host ">>> [4/5] Processing Proxy Template (CSC)..." -ForegroundColor Cyan
$ProxyCode = @"
using System; using System.Diagnostics; using System.IO; using System.Windows.Forms;
class Program { [STAThread] static void Main() {
    string d = AppDomain.CurrentDomain.BaseDirectory;
    string e = Path.Combine(d, "LinLauncher_Environment");
    string t = Path.Combine(e, "LinLauncher.exe");
    if (!File.Exists(t)) { MessageBox.Show("Error: Core not found!"); return; }
    try {
        byte[] s = File.ReadAllBytes(Process.GetCurrentProcess().MainModule.FileName);
        int idx = -1; ulong sign = 0x12345678FEDCBAFF;
        for (int i = s.Length - 8; i >= 0; i--) { if (BitConverter.ToUInt64(s, i) == sign) { idx = i; break; } }
        if (idx != -1) {
            int len = s.Length - idx; byte[] conf = new byte[len];
            Array.Copy(s, idx, conf, 0, len);
            File.WriteAllBytes(Path.Combine(e, "config.dat"), conf);
        }
    } catch { }
    try { Process.Start(new ProcessStartInfo(t) { WorkingDirectory = e }); } catch { }
} }
"@
$ProxySource = "$Root\Proxy_Temp.cs"
Set-Content -Path $ProxySource -Value $ProxyCode -Encoding UTF8

$csc = $null
$cscPaths = @(
    "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe",
    "C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe"
)
foreach ($p in $cscPaths) {
    if (Test-Path $p) {
        $csc = $p
        break
    }
}

if ($csc) {
    & $csc /target:winexe /out:"$PartnerDir\LinLauncher.dat" /win32icon:"$IconPath" "$ProxySource"
    Write-Host "    - Proxy Template generated successfully." -ForegroundColor Green
} else {
    Write-Host "    [!] Error: CSC.exe not found." -ForegroundColor Red
}

if (Test-Path $ProxySource) {
    Remove-Item $ProxySource -Force
}

Write-Host ">>> [5/5] Syncing files to distribution folder..." -ForegroundColor Cyan
if (!(Test-Path $EnvDir)) {
    New-Item -ItemType Directory -Path $EnvDir -Force
}
Copy-Item "$Root\LinProj\Encoder\publish\*" "$PartnerDir" -Recurse -Force
Copy-Item "$Root\LinProj\LinLauncher\publish\*" "$EnvDir" -Recurse -Force
if (Test-Path $IconPath) {
    Copy-Item "$IconPath" "$PartnerDir\LinLauncher.ico" -Force
}

Write-Host "`n✅ All updates deployed successfully!" -ForegroundColor Green
Write-Host "Target: $PartnerDir"
pause
