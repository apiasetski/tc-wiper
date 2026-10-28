# Packs the release zip dist\tcwiper_<version>.zip and its SHA-256 checksum.
# Opening the zip in Total Commander offers to install the plugin.
# The version comes from src\version.h (TCW_VERSION_TEXT).
$ErrorActionPreference = 'Stop'
$root  = $PSScriptRoot
$dist  = Join-Path $root 'dist'
$stage = Join-Path $root 'build\package'

$header = Get-Content (Join-Path $root 'src\version.h') -Raw
if ($header -notmatch '#define\s+TCW_VERSION_TEXT\s+"([^"]+)"') { throw 'TCW_VERSION_TEXT not found in version.h' }
$version = $Matches[1]
$fileVersion = $version -replace '\s+', '_'

foreach ($dir in $stage, $dist) {
    if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
    New-Item -ItemType Directory -Force $dir | Out-Null
}

Copy-Item (Join-Path $root 'build\TCWiper.wfx')   $stage
Copy-Item (Join-Path $root 'build\TCWiper.wfx64') $stage
Copy-Item (Join-Path $root 'LICENSE.txt')         $stage
Copy-Item (Join-Path $root 'history.txt')         $stage
Copy-Item (Join-Path $root 'docs\TCWiper-Readme.txt')    (Join-Path $stage 'Readme.txt')
Copy-Item (Join-Path $root 'docs\TCWiper-Readme.ru.txt') (Join-Path $stage 'Readme_ru.txt')

# Install instructions for Total Commander.
@"
[plugininstall]
description=TC Wiper $version - move files here with F6 to securely wipe them
type=wfx
file=TCWiper.wfx
defaultdir=TCWiper
version=$version
"@ | Set-Content (Join-Path $stage 'pluginst.inf') -Encoding ascii

$zip = Join-Path $dist "tcwiper_$fileVersion.zip"
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip

# Source archive matching the binaries (required by the GPL when distributing):
# every file in the project except build output, inside one top-level folder.
$srcName  = "tcwiper_${fileVersion}_src"
$srcStage = Join-Path $root "build\$srcName"
if (Test-Path $srcStage) { Remove-Item $srcStage -Recurse -Force }
$files = git -C $root ls-files --cached --others --exclude-standard
if ($LASTEXITCODE -ne 0) { throw 'git ls-files failed' }
foreach ($f in $files) {
    $src = Join-Path $root $f
    if (-not (Test-Path $src -PathType Leaf)) { continue }  # deleted but not yet committed
    $dst = Join-Path $srcStage $f
    New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
    Copy-Item $src $dst
}
$srcZip = Join-Path $dist "$srcName.zip"
Compress-Archive -Path $srcStage -DestinationPath $srcZip

foreach ($z in $zip, $srcZip) {
    $hash = (Get-FileHash $z -Algorithm SHA256).Hash.ToLower()
    "$hash  $(Split-Path $z -Leaf)" | Set-Content "$z.sha256" -Encoding ascii
    Write-Host "Created $z"
    Write-Host "SHA-256 $hash"
}
