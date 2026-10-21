$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$src = "C:\Users\Administrator\.minimax\v2\assets\2026\10\06\18-00-24-013-asset_20261006-180024-013_37337ae5730a_8b58faea-image.png"
$outDir = "D:\code\project\Go2ProController\prepared-media"
$out = Join-Path $outDir "park-path-first-frame-video-creater-ready.png"
if (-not (Test-Path -LiteralPath $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

$img = [System.Drawing.Image]::FromFile($src)
$sw = $img.Width
$sh = $img.Height

# Target 16:9 at the source height: 621 * 16/9 = 1104 exactly.
$tw = 1104
$th = 621
$padX = [int](($tw - $sw) / 2)
$padY = [int](($th - $sh) / 2)

# 1) tiny downscale -> 2) upscale to full canvas = soft edge-aware fill
$tiny = New-Object System.Drawing.Bitmap 48, 27
$gTiny = [System.Drawing.Graphics]::FromImage($tiny)
$gTiny.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$gTiny.DrawImage($img, (New-Object System.Drawing.Rectangle 0, 0, 48, 27))
$gTiny.Dispose()

$bg = New-Object System.Drawing.Bitmap $tw, $th
$gBg = [System.Drawing.Graphics]::FromImage($bg)
$gBg.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$gBg.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
$gBg.DrawImage($tiny, (New-Object System.Drawing.Rectangle 0, 0, $tw, $th))
$gBg.Dispose()
$tiny.Dispose()

# 2) original 1:1 centered on top, untouched
$canvas = New-Object System.Drawing.Bitmap $tw, $th, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($canvas)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
$g.DrawImage($bg, (New-Object System.Drawing.Rectangle 0, 0, $tw, $th))
$g.DrawImage($img, (New-Object System.Drawing.Rectangle $padX, $padY, $sw, $sh))
$g.Dispose()
$bg.Dispose()

$canvas.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$canvas.Dispose()
$img.Dispose()

$fi = Get-Item -LiteralPath $out
"out=$($fi.FullName)"
"bytes=$($fi.Length)"
"target=${tw}x${th}"
"source=${sw}x${sh} padX=$padX padY=$padY"