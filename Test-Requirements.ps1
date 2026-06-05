# Preflight check for building/deploying the xbAI probe on a fresh clone.
# Reports PASS/WARN/FAIL per requirement; non-fatal so you see the whole picture.
$root = $PSScriptRoot
$ok = $true
function Pass($m) { Write-Host "  [PASS] $m" -ForegroundColor Green }
function Warn($m) { Write-Host "  [WARN] $m" -ForegroundColor Yellow }
function Fail($m) { Write-Host "  [FAIL] $m" -ForegroundColor Red; $script:ok = $false }

Write-Host "xbAI - requirements check`n" -ForegroundColor Cyan

# 1. .env
$envPath = Join-Path $root ".env"
$cfg = $null
if (Test-Path $envPath) {
    Pass ".env present"
    try { $cfg = & (Join-Path $root "Load-Env.ps1") } catch { Fail $_.Exception.Message }
} else {
    Fail ".env missing - copy .env.example to .env and fill it in"
}

# 2. MSBuild with the C++/CX UWP (WindowsXaml) targets
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
if ($msbuild) { Pass "MSBuild with C++/CX UWP targets: $msbuild" }
else { Fail "No MSBuild with UWP C++ targets. Install VS 2022 + 'Universal Windows Platform development' + C++ (v143) UWP tools" }

# 3. Windows SDK 10.0.26100 (project target)
$sdkRoot = "C:\Program Files (x86)\Windows Kits\10\bin"
if (Test-Path (Join-Path $sdkRoot "10.0.26100.0")) { Pass "Windows SDK 10.0.26100 present" }
elseif (Test-Path $sdkRoot) { Warn "Windows SDK present but not 10.0.26100 - retarget the project if build fails" }
else { Fail "No Windows 10 SDK found" }

# 4. curl.exe (used for Device Portal REST)
if (Get-Command curl.exe -ErrorAction SilentlyContinue) { Pass "curl.exe available" }
else { Fail "curl.exe not found (ships with Windows 10/11)" }

# 5. Native runtime DLLs (restored by Get-Dependencies.ps1)
$redist = Join-Path $root "xbprobe\xbprobe\redist"
$needDlls = @("DirectML.dll","onnxruntime.dll","onnxruntime-genai.dll","vcruntime140.dll","vcruntime140_1.dll","msvcp140.dll","msvcp140_1.dll")
$missing = $needDlls | Where-Object { -not (Test-Path (Join-Path $redist $_)) }
if (-not $missing) { Pass "redist DLLs present ($($needDlls.Count))" }
else { Warn "redist missing: $($missing -join ', ') - run xbprobe\Get-Dependencies.ps1" }

# 6. Signing cert
if ($cfg -and $cfg.SIGNING_CERT_PFX) {
    $pfx = Join-Path $root $cfg.SIGNING_CERT_PFX
    if (Test-Path $pfx) { Pass "signing cert: $($cfg.SIGNING_CERT_PFX)" }
    else { Warn "signing cert not found at $($cfg.SIGNING_CERT_PFX) - create your own .pfx (see README)" }
}

# 7. Model staged (optional - only needed to actually run inference, not to build)
$model = Join-Path $root "xbprobe\model-stage\phi3-mini-int4\model.onnx.data"
if (Test-Path $model) { Pass "ONNX model staged" }
else { Warn "no ONNX model staged - fetch one from Hugging Face (see model-stage\README.md)" }

# 8. Console reachable (optional)
if ($cfg) {
    $resp = curl.exe -k -s -o NUL -w "%{http_code}" "https://$($cfg.XBOX_HOST):$($cfg.WDP_PORT)/" 2>$null
    if ($resp -match "^\d{3}$" -and $resp -ne "000") { Pass "Device Portal reachable at $($cfg.XBOX_HOST):$($cfg.WDP_PORT) (HTTP $resp)" }
    else { Warn "Device Portal not reachable at $($cfg.XBOX_HOST):$($cfg.WDP_PORT) - is the console on and in Dev Mode?" }
}

# 9. gh (optional, for publishing)
if (Get-Command gh -ErrorAction SilentlyContinue) { Pass "gh CLI available (optional)" }
else { Warn "gh CLI not found (only needed to publish the repo)" }

Write-Host ""
if ($ok) { Write-Host "Ready. Next: .\Start-Build.ps1" -ForegroundColor Green }
else { Write-Host "Resolve the [FAIL] items above, then re-run." -ForegroundColor Red; exit 1 }
