# Pulls probe-results.txt back from the app's LocalState via the Device Portal.
param(
    [string]$DeviceHost,
    [int]$Port
)
$ErrorActionPreference = "Stop"
$cfg = & (Join-Path (Split-Path $PSScriptRoot -Parent) "Load-Env.ps1")
if (-not $DeviceHost) { $DeviceHost = $cfg.XBOX_HOST }
if (-not $Port)       { $Port = [int]$cfg.WDP_PORT }
$base = "https://$($DeviceHost):$Port"

$pkgs = curl.exe -k -s "$base/api/app/packagemanager/packages" | ConvertFrom-Json
$pfn = $pkgs.InstalledPackages |
       Where-Object { $_.PackageFamilyName -like "xbAI.Probe*" -or $_.PackageFullName -like "xbAI.Probe*" } |
       Select-Object -First 1 -ExpandProperty PackageFullName
if (-not $pfn) { throw "xbAI.Probe not installed." }
Write-Host "PFN: $pfn"

$out = Join-Path $PSScriptRoot "probe-results.txt"
# NB: the 'path' must carry a leading backslash (\LocalState) or the Device
# Portal concatenates it wrong and returns an empty/incorrect listing.
curl.exe -k -s "$base/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$pfn&path=%5CLocalState&filename=probe-results.txt" -o $out
if ((Get-Item $out -ErrorAction SilentlyContinue).Length -gt 0) {
    Write-Host "`n===== probe-results.txt =====`n" -ForegroundColor Green
    Get-Content $out
} else {
    Write-Host "No results yet - has the app been launched on the console at least once?" -ForegroundColor Yellow
}
