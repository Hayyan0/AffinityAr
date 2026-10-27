$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$versionPath = Join-Path $root 'version.txt'
$cargoPath = Join-Path $root 'installer\src-tauri\Cargo.toml'
$tauriPath = Join-Path $root 'installer\src-tauri\tauri.conf.json'

$canonical = (Get-Content -LiteralPath $versionPath -Raw).Trim()
if ($canonical -notmatch '^\d+\.\d+\.\d+$') {
    Write-Error "version.txt must contain a three-part numeric version, got: '$canonical'"
    exit 1
}

$cargoLine = Select-String -LiteralPath $cargoPath -Pattern '^version\s*=\s*"([^"]+)"' | Select-Object -First 1
if (-not $cargoLine) {
    Write-Error "Could not read [package] version from $cargoPath"
    exit 1
}
$cargoVersion = $cargoLine.Matches[0].Groups[1].Value

$tauriVersion = (Get-Content -LiteralPath $tauriPath -Raw | ConvertFrom-Json).version

$ok = $true
if ($cargoVersion -ne $canonical) {
    Write-Error "Cargo.toml version '$cargoVersion' does not match version.txt '$canonical'"
    $ok = $false
}
if ($tauriVersion -ne $canonical) {
    Write-Error "tauri.conf.json version '$tauriVersion' does not match version.txt '$canonical'"
    $ok = $false
}

if (-not $ok) { exit 1 }
Write-Host "Version OK: $canonical"
exit 0
