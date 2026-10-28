# Draws the TC Wiper icon (a sheet of paper going into a red shredder) at
# every size Windows uses and writes src\tcwiper.ico plus a preview PNG.
# Usage: powershell -ExecutionPolicy Bypass -File tools\make_icon.ps1
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root    = Split-Path $PSScriptRoot -Parent
$icoPath = Join-Path $root 'src\tcwiper.ico'
$sizes   = 16, 20, 24, 32, 40, 48, 64, 256

function New-IconBitmap([int]$s) {
    $u = $s / 32.0
    $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    # Small sizes are drawn pixel-sharp; smoothing only blurs them.
    $small = $s -lt 32
    $sharp = $s -le 48
    $g.SmoothingMode   = if ($sharp) { 'None' } else { 'AntiAlias' }
    $g.PixelOffsetMode = if ($sharp) { 'None' } else { 'HighQuality' }
    $g.Clear([System.Drawing.Color]::Transparent)

    $outline = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 90, 90, 90)), ([Math]::Max(1, $u))
    $paper   = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::White)

    # Sheet of paper entering the shredder.
    $g.FillRectangle($paper, 8 * $u, 2 * $u, 16 * $u, 14 * $u)
    $g.DrawRectangle($outline, 8 * $u, 2 * $u, 16 * $u, 14 * $u)
    if ($s -ge 24) {
        $text = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 150, 160, 175)), ([Math]::Max(1, 1.2 * $u))
        foreach ($y in 6, 9, 12) { $g.DrawLine($text, 11 * $u, $y * $u, 21 * $u, $y * $u) }
    }

    # Shredded strips coming out underneath.
    if ($small) {
        # One-pixel grey strips with one-pixel gaps.
        $grey = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 120, 120, 120))
        $i = 0
        for ($x = [int](9 * $u); $x -le [int](23 * $u); $x += 2) {
            $len = if ($i++ % 2 -eq 0) { [int](9 * $u) } else { [int](7 * $u) }
            $g.FillRectangle($grey, $x, [int](21 * $u), [Math]::Max(1, [int]$u), $len)
            if ($s -ge 24) { $x++ }
        }
    } else {
        $strip = 2 * $u
        for ($x = 9; $x -lt 24; $x += 3) {
            $len = if ((($x - 9) / 3) % 2 -eq 0) { 9 } else { 7 }
            $g.FillRectangle($paper, $x * $u, 20 * $u, $strip, $len * $u)
            $g.DrawRectangle($outline, $x * $u, 20 * $u, $strip, $len * $u)
        }
    }

    # Red shredder body with a dark slot.
    $body = New-Object System.Drawing.Drawing2D.LinearGradientBrush (
        (New-Object System.Drawing.PointF 0, (13 * $u)), (New-Object System.Drawing.PointF 0, (22 * $u)),
        [System.Drawing.Color]::FromArgb(255, 239, 83, 80), [System.Drawing.Color]::FromArgb(255, 183, 28, 28))
    $g.FillRectangle($body, 3 * $u, 13 * $u, 26 * $u, 8 * $u)
    $g.DrawRectangle((New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 127, 0, 0)), ([Math]::Max(1, $u))),
                     3 * $u, 13 * $u, 26 * $u, 8 * $u)
    $g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 60, 0, 0))),
                     6 * $u, 16 * $u, 20 * $u, [Math]::Max(1, 2 * $u))

    $g.Dispose()
    return $bmp
}

# ICO file with one PNG image per size.
$images = foreach ($s in $sizes) {
    $bmp = New-IconBitmap $s
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    , $ms.ToArray()
}
$out = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter $out
$w.Write([UInt16]0); $w.Write([UInt16]1); $w.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $dim = if ($sizes[$i] -ge 256) { 0 } else { $sizes[$i] }
    $w.Write([byte]$dim); $w.Write([byte]$dim); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([UInt16]1); $w.Write([UInt16]32)
    $w.Write([UInt32]$images[$i].Length); $w.Write([UInt32]$offset)
    $offset += $images[$i].Length
}
foreach ($img in $images) { $w.Write($img) }
[IO.File]::WriteAllBytes($icoPath, $out.ToArray())
Write-Host "Created $icoPath"

# Preview: the small sizes enlarged without smoothing, plus the large one.
$preview = New-Object System.Drawing.Bitmap 760, 300
$g = [System.Drawing.Graphics]::FromImage($preview)
$g.Clear([System.Drawing.Color]::FromArgb(255, 240, 240, 240))
$g.InterpolationMode = 'NearestNeighbor'
$g.PixelOffsetMode = 'Half'
$x = 10
foreach ($s in 16, 24, 32, 48) {
    $bmp = New-IconBitmap $s
    $g.DrawImage($bmp, $x, 10, $s * 4, $s * 4)
    $g.DrawImage($bmp, $x, 220, $s, $s)
    $x += $s * 4 + 15
}
$g.InterpolationMode = 'HighQualityBicubic'
$big = New-IconBitmap 256
$g.DrawImage($big, 500, 10, 250, 250)
$g.Dispose()
$previewPath = Join-Path $root 'build\icon_preview.png'
New-Item -ItemType Directory -Force (Split-Path $previewPath) | Out-Null
$preview.Save($previewPath, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Host "Preview $previewPath"
