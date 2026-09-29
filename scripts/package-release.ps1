param(
    [string]$BuildDir = 'build',
    [string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$buildRoot = (Resolve-Path (Join-Path $repoRoot $BuildDir)).Path
& (Join-Path $PSScriptRoot 'setup-map-decoder.ps1') -BuildDir $BuildDir
$distRoot = Join-Path $repoRoot 'dist'
$stageRoot = Join-Path $distRoot 'sourcesight-windows'
if (Test-Path -LiteralPath $stageRoot) {
    $resolvedStage = (Resolve-Path -LiteralPath $stageRoot).Path
    if ($resolvedStage -ne (Join-Path $repoRoot 'dist/sourcesight-windows')) {
        throw 'Unexpected staging path.'
    }
    Remove-Item -LiteralPath $resolvedStage -Recurse -Force
}
New-Item -ItemType Directory -Force $stageRoot | Out-Null
& cmake --install $buildRoot --config $Configuration --prefix $stageRoot
if ($LASTEXITCODE -ne 0) { throw 'CMake installation failed.' }
New-Item -ItemType Directory -Force (Join-Path $stageRoot 'tools') | Out-Null
Copy-Item -LiteralPath (Join-Path $buildRoot 'tools/source2viewer') -Destination (Join-Path $stageRoot 'tools/source2viewer') -Recurse
$licenseRoot = Join-Path $stageRoot 'licenses'
New-Item -ItemType Directory -Force $licenseRoot | Out-Null
$licenses = @{
    'imgui.txt' = 'src/external/imgui/LICENSE.txt'
    'json.txt' = 'src/external/json/LICENSE.MIT'
    'AsyncLogger.txt' = 'src/external/AsyncLogger/LICENSE'
    'VisCheckCS2.txt' = 'src/external/VisCheckCS2/LICENSE'
    'glfw.txt' = 'src/external/glfw/LICENSE.md'
    'glew.txt' = (Join-Path $buildRoot '_deps/glew-src/LICENSE.txt')
}
foreach ($entry in $licenses.GetEnumerator()) {
    $source = if ([IO.Path]::IsPathRooted($entry.Value)) { $entry.Value } else { Join-Path $repoRoot $entry.Value }
    Copy-Item -LiteralPath $source -Destination (Join-Path $licenseRoot $entry.Key)
}
Copy-Item -LiteralPath (Join-Path $repoRoot 'assets/README.md') -Destination (Join-Path $licenseRoot 'asset-notices.txt')
Get-ChildItem -LiteralPath (Join-Path $repoRoot 'licenses') -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $licenseRoot $_.Name)
}
$versionMatch = Select-String -LiteralPath (Join-Path $repoRoot 'CMakeLists.txt') -Pattern '^project\(sourcesight_windows VERSION ([0-9.]+)'
$version = $versionMatch.Matches[0].Groups[1].Value
$archive = Join-Path $distRoot "sourcesight-windows-v$version-x64.zip"
Compress-Archive -Path $stageRoot -DestinationPath $archive -Force
Write-Output $archive
