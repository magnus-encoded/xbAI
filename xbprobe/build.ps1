# Builds and packages the xbAI probe as a sideload-signed .appx.
# Requires VS 2022 Build Tools with the C++ + UWP build-tools workloads.
param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64"
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$repoRoot = Split-Path $root -Parent
$cfg = & (Join-Path $repoRoot "Load-Env.ps1")
$proj = Join-Path $root "xbprobe\xbprobe.vcxproj"
$pfx  = Join-Path $repoRoot $cfg.SIGNING_CERT_PFX
if (-not (Test-Path $pfx)) {
    throw "Signing cert not found at '$pfx'. Create your own .pfx (see README) and point SIGNING_CERT_PFX in .env at it."
}
$certPw = $cfg.SIGNING_CERT_PASSWORD

# Locate MSBuild via vswhere.
# Pick the MSBuild whose tree actually has the C++/CX UWP (WindowsXaml) targets.
# A bare VS 18 Build Tools install on this box lacks them, so we can't just take
# vswhere -latest; we verify each install before choosing.
$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = $null
if (Test-Path $vswhere) {
    foreach ($inst in (& $vswhere -all -products * -format value -property installationPath)) {
        $hasUwp = Get-ChildItem (Join-Path $inst "MSBuild\Microsoft\WindowsXaml") -Recurse `
                    -Filter "Microsoft.Windows.UI.Xaml.CPP.Targets" -ErrorAction SilentlyContinue | Select-Object -First 1
        $cand = Join-Path $inst "MSBuild\Current\Bin\MSBuild.exe"
        if ($hasUwp -and (Test-Path $cand)) { $msbuild = $cand; break }
    }
}
if (-not $msbuild) {
    $fallback = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    if (Test-Path $fallback) { $msbuild = $fallback }
}
if (-not $msbuild) { throw "MSBuild with C++/CX UWP targets not found." }
Write-Host "MSBuild: $msbuild" -ForegroundColor Cyan

& $msbuild $proj `
    /p:Configuration=$Configuration `
    /p:Platform=$Platform `
    /p:AppxBundle=Never `
    /p:UapAppxPackageBuildMode=SideloadOnly `
    /p:GenerateAppxPackageOnBuild=true `
    /p:AppxPackageSigningEnabled=true `
    /p:PackageCertificateKeyFile=$pfx `
    /p:PackageCertificatePassword=$certPw `
    /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }

Write-Host "`nProduced packages:" -ForegroundColor Green
Get-ChildItem (Join-Path $root "xbprobe\AppPackages") -Recurse -Include *.appx,*.msix -ErrorAction SilentlyContinue |
    Select-Object FullName, @{n="MB";e={[math]::Round($_.Length/1MB,2)}}
