param([string]$BuildDir = 'build')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$version = '20.0'
$expected = 'd32ab327b8bbb42a2528866afb03bb582bdb779d0005488da32b90292afd3ff5'
$cache = Join-Path $repoRoot '.tools'
New-Item -ItemType Directory -Force $cache | Out-Null
$archive = Join-Path $cache "source2viewer-$version.zip"
if (-not (Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest "https://github.com/ValveResourceFormat/ValveResourceFormat/releases/download/$version/cli-windows-x64.zip" -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected) {
    throw 'Source 2 decoder archive checksum mismatch.'
}
$destination = Join-Path (Join-Path $repoRoot $BuildDir) 'tools/source2viewer'
New-Item -ItemType Directory -Force $destination | Out-Null
Expand-Archive -LiteralPath $archive -DestinationPath $destination -Force
Write-Output "Installed verified Source 2 decoder $version at $destination"
