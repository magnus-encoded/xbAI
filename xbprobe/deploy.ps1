# Uploads the built .appx (+ cert + dependencies) to the Xbox via the
# Windows Device Portal REST API, then polls install state.
param(
    [string]$DeviceHost,
    [int]$Port
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$cfg = & (Join-Path (Split-Path $root -Parent) "Load-Env.ps1")
if (-not $DeviceHost) { $DeviceHost = $cfg.XBOX_HOST }
if (-not $Port)       { $Port = [int]$cfg.WDP_PORT }
$base = "https://$($DeviceHost):$Port"

# Find the newest main package (recursively), excluding the VCLibs deps folder.
$appRoot = Join-Path $root "xbprobe\AppPackages"
$main = Get-ChildItem $appRoot -Recurse -File -Include *.appx,*.msix |
        Where-Object { $_.FullName -notmatch "\\Dependencies\\" } |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $main) { throw "No main .appx/.msix under $appRoot - run build.ps1 first." }
$pkgDir = $main.Directory
$cer  = Get-ChildItem $pkgDir.FullName -Filter *.cer -File | Select-Object -First 1
$deps = @(Get-ChildItem (Join-Path $pkgDir.FullName "Dependencies\x64") -Filter *.appx -File -ErrorAction SilentlyContinue)
Write-Host "Package : $($main.Name)"
Write-Host "Cert    : $($cer.Name)"
Write-Host "Deps    : $($deps.Name -join ', ')"

# 1. Get a CSRF token (cookie) from the portal root.
$cookies = Join-Path $env:TEMP "wdp_cookies.txt"
curl.exe -k -s -c $cookies "$base/" -o NUL
$token = (Select-String -Path $cookies -Pattern "CSRF-Token\s+(\S+)").Matches.Groups[1].Value
if (-not $token) { throw "Could not read CSRF-Token." }
Write-Host "CSRF    : $token"

# 2. Build the multipart upload (main package + cert + every dependency).
$formArgs = @("-F", "$($main.Name)=@$($main.FullName);type=application/octet-stream")
if ($cer)  { $formArgs += @("-F", "$($cer.Name)=@$($cer.FullName);type=application/x-x509-ca-cert") }
foreach ($d in $deps) { $formArgs += @("-F", "$($d.Name)=@$($d.FullName);type=application/octet-stream") }

Write-Host "`nUploading..." -ForegroundColor Cyan
curl.exe -k -s -b $cookies -H "X-CSRF-Token: $token" `
    -X POST "$base/api/app/packagemanager/package?package=$($main.Name)" `
    @formArgs -w "`nHTTP %{http_code}`n"

# 3. Poll install state.
Write-Host "`nPolling install state..." -ForegroundColor Cyan
for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Seconds 2
    $state = curl.exe -k -s -b $cookies "$base/api/app/packagemanager/state"
    Write-Host $state
    if ($state -match "Idle" -or $state -match "Complete" -or $state -notmatch "Installing") { break }
}
Write-Host "`nDone. Launch 'xbAI Capability Probe' on the console, then run get-results.ps1." -ForegroundColor Green
