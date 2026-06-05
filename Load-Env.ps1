# Loads the repo-root .env into process environment variables and returns a
# hashtable of the values. Dot-source or call with & to use:
#
#   $cfg = & "$repoRoot\Load-Env.ps1"
#   $cfg.XBOX_HOST ; $env:SIGNING_CERT_PASSWORD
#
# Fails loudly if .env is missing so a fresh clone gets a clear next step.
param([string]$Path)

if (-not $Path) { $Path = Join-Path $PSScriptRoot ".env" }
if (-not (Test-Path $Path)) {
    throw ".env not found at '$Path'. Copy .env.example to .env and fill in your values."
}

$cfg = @{}
foreach ($line in Get-Content $Path) {
    $t = $line.Trim()
    if ($t -eq "" -or $t.StartsWith("#")) { continue }
    $i = $t.IndexOf("=")
    if ($i -lt 1) { continue }
    $k = $t.Substring(0, $i).Trim()
    $v = $t.Substring($i + 1).Trim()
    if ($v.Length -ge 2 -and $v.StartsWith('"') -and $v.EndsWith('"')) {
        $v = $v.Substring(1, $v.Length - 2)
    }
    $cfg[$k] = $v
    Set-Item -Path "Env:$k" -Value $v
}
return $cfg
