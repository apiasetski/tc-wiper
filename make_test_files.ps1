# Creates a folder of throwaway files for trying TC Wiper in Total Commander.
# Usage: powershell -ExecutionPolicy Bypass -File make_test_files.ps1 [folder]
param([string]$Folder = (Join-Path $env:USERPROFILE 'WipeTest'))
$ErrorActionPreference = 'Stop'

New-Item -ItemType Directory -Force $Folder | Out-Null
$sub  = New-Item -ItemType Directory -Force (Join-Path $Folder 'Folder with files')
$deep = New-Item -ItemType Directory -Force (Join-Path $sub 'Nested folder')

Set-Content (Join-Path $Folder 'notes.txt') 'Some secret notes'
Set-Content (Join-Path $sub 'report.txt') ('Line of text ' * 2000)
Set-Content (Join-Path $deep 'deep file.txt') 'Deep inside'
Set-Content (Join-Path $Folder ([string][char]0x017E + 'lu' + [char]0x0165 + 'ou' + [char]0x010D + 'k' + [char]0x00FD + ' file.txt')) 'Unicode name'

$ro = Join-Path $Folder 'read-only.txt'
Set-Content $ro 'Read-only file'
(Get-Item $ro).Attributes = 'ReadOnly'

$ads = Join-Path $Folder 'with hidden stream.txt'
Set-Content $ads 'Visible content'
Set-Content $ads -Stream 'secret' 'Hidden stream content'

$big = [byte[]]::new(20MB)
(New-Object Random).NextBytes($big)
[IO.File]::WriteAllBytes((Join-Path $Folder 'big file.bin'), $big)

Write-Host "Test files created in $Folder"
