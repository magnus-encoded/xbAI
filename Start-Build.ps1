# One-shot: restore deps (if needed) -> build the signed appx -> deploy to the
# console over the Device Portal. Reads all environment specifics from .env.
param(
    [switch]$SkipDeploy,   # build only
    [switch]$RestoreDeps   # force a NuGet/CRT restore first
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$cfg = & (Join-Path $root "Load-Env.ps1")

$redist = Join-Path $root "xbprobe\xbprobe\redist"
$haveDlls = Test-Path (Join-Path $redist "onnxruntime-genai.dll")
if ($RestoreDeps -or -not $haveDlls) {
    Write-Host "== Restoring native dependencies ==" -ForegroundColor Cyan
    & (Join-Path $root "xbprobe\Get-Dependencies.ps1")
}

Write-Host "`n== Building signed appx ==" -ForegroundColor Cyan
& (Join-Path $root "xbprobe\build.ps1")

if ($SkipDeploy) {
    Write-Host "`nBuild complete (deploy skipped)." -ForegroundColor Green
    return
}

Write-Host "`n== Deploying to $($cfg.XBOX_HOST):$($cfg.WDP_PORT) ==" -ForegroundColor Cyan
& (Join-Path $root "xbprobe\deploy.ps1")

Write-Host "`nNext steps:" -ForegroundColor Green
Write-Host "  1. On the console (Dev Home > My games & apps) set the probe's content type to 'Game'."
Write-Host "  2. Launch 'xbAI Capability Probe' on the console."
Write-Host "  3. Pull results:  .\xbprobe\get-results.ps1"
