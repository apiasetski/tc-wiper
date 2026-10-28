# Packs both plugin builds and pluginst.inf into dist\TCWiper.zip.
# Opening that zip in Total Commander offers to install the plugin.
$ErrorActionPreference = 'Stop'
$root  = $PSScriptRoot
$dist  = Join-Path $root 'dist'
$stage = Join-Path $root 'build\package'

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage, $dist | Out-Null

Copy-Item (Join-Path $root 'build\TCWiper.wfx')   $stage
Copy-Item (Join-Path $root 'build\TCWiper.wfx64') $stage
Copy-Item (Join-Path $root 'pluginst.inf')        $stage

$zip = Join-Path $dist 'TCWiper.zip'
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
Write-Host "Created $zip"
