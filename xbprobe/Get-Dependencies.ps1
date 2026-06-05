# Restores the native runtime DLLs the probe bundles into its appx.
#
# These are NOT committed (large, redistributable on their own terms). This script
# downloads the exact NuGet packages used for the published findings, extracts the
# win-x64 native DLLs into xbprobe\redist\, and copies the desktop VC runtime DLLs
# the AppContainer needs (the UWP CRT differs from the one ORT-GenAI links against).
#
# Run once after cloning, before build.ps1.
$ErrorActionPreference = "Stop"
$root   = $PSScriptRoot
$redist = Join-Path $root "xbprobe\redist"
$work   = Join-Path $root "thirdparty\nupkg"
$extract= Join-Path $root "thirdparty\extract"
New-Item -ItemType Directory -Force -Path $redist, $work, $extract | Out-Null

# id, version, internal DLL path, output name
$pkgs = @(
    @{ id="Microsoft.AI.DirectML";                   ver="1.15.2";  src="bin/x64-win/DirectML.dll";                     out="DirectML.dll" },
    @{ id="Microsoft.ML.OnnxRuntime.DirectML";       ver="1.20.1";  src="runtimes/win-x64/native/onnxruntime.dll";      out="onnxruntime.dll" },
    @{ id="Microsoft.ML.OnnxRuntimeGenAI.DirectML";  ver="0.6.0";   src="runtimes/win-x64/native/onnxruntime-genai.dll";out="onnxruntime-genai.dll" }
)

foreach ($p in $pkgs) {
    $url = "https://www.nuget.org/api/v2/package/$($p.id)/$($p.ver)"
    $zip = Join-Path $work "$($p.id).$($p.ver).nupkg"
    $dir = Join-Path $extract $p.id
    if (-not (Test-Path $zip)) {
        Write-Host "Downloading $($p.id) $($p.ver) ..." -ForegroundColor Cyan
        Invoke-WebRequest -Uri $url -OutFile $zip
    }
    if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
    Expand-Archive -Path $zip -DestinationPath $dir -Force
    $dll = Join-Path $dir $p.src
    if (-not (Test-Path $dll)) { throw "Expected DLL not found in package: $dll" }
    Copy-Item $dll (Join-Path $redist $p.out) -Force
    Write-Host "  -> redist\$($p.out)" -ForegroundColor Green
}

# Desktop VC runtime DLLs (System32) - bundled to satisfy ORT-GenAI inside the
# AppContainer, whose default CRT differs. If absent, install the VC++ 2015-2022
# x64 redistributable.
$crt = @("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "msvcp140_1.dll")
foreach ($c in $crt) {
    $sys = Join-Path $env:WINDIR "System32\$c"
    if (Test-Path $sys) {
        Copy-Item $sys (Join-Path $redist $c) -Force
        Write-Host "  -> redist\$c (from System32)" -ForegroundColor Green
    } else {
        Write-Warning "$c not found in System32 - install the VC++ 2015-2022 x64 redistributable."
    }
}

Write-Host "`nDependencies restored to $redist" -ForegroundColor Green
