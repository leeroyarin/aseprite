Add-Type -AssemblyName System.Drawing

$f1 = [System.Drawing.Bitmap]::FromFile('C:\Users\leero\Source\Repos\aseprite2\tools\gp_current_f1.png') # Common
$f2 = [System.Drawing.Bitmap]::FromFile('C:\Users\leero\Source\Repos\aseprite2\tools\gp_current_f2.png') # Rare
$f3 = [System.Drawing.Bitmap]::FromFile('C:\Users\leero\Source\Repos\aseprite2\tools\gp_current_f3.png') # Epic
$f4 = [System.Drawing.Bitmap]::FromFile('C:\Users\leero\Source\Repos\aseprite2\tools\gp_current_f4.png') # Legendary

$scale = 3
$srcW = 108
$srcH = 80
$cardW = $srcW * $scale # 324
$cardH = $srcH * $scale # 240
$pad = 16
$headerH = 36
$totalW = $pad + ($cardW + $pad) * 4
$totalH = $headerH + $cardH + $pad * 2

$bmp = New-Object System.Drawing.Bitmap($totalW, $totalH)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half

# Background (standard dark theme #1A1B26)
$bgBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(26, 27, 38))
$g.FillRectangle($bgBrush, 0, 0, $totalW, $totalH)

$titles = @('Common (Tier 1)', 'Rare (Tier 2)', 'Epic (Tier 3)', 'Legendary (Tier 4)')
$tierColors = @(
    [System.Drawing.Color]::FromArgb(156, 163, 175), # Grey
    [System.Drawing.Color]::FromArgb(56, 189, 248),  # Sky Blue
    [System.Drawing.Color]::FromArgb(192, 132, 252), # Purple
    [System.Drawing.Color]::FromArgb(251, 191, 36)   # Gold
)
$frames = @($f1, $f2, $f3, $f4)

$font = New-Object System.Drawing.Font('Segoe UI', 11, [System.Drawing.FontStyle]::Bold)
$sf = New-Object System.Drawing.StringFormat
$sf.Alignment = [System.Drawing.StringAlignment]::Center
$sf.LineAlignment = [System.Drawing.StringAlignment]::Center

for ($i = 0; $i -lt 4; $i++) {
    $x = $pad + $i * ($cardW + $pad)
    $y = $headerH + $pad

    # Card background (dark slate #232636)
    $cardBg = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(35, 38, 54))
    $g.FillRectangle($cardBg, $x, $y, $cardW, $cardH)

    # Tier Border (2px)
    $pen = New-Object System.Drawing.Pen($tierColors[$i], 2)
    $g.DrawRectangle($pen, $x, $y, $cardW, $cardH)

    # Title
    $textBrush = New-Object System.Drawing.SolidBrush($tierColors[$i])
    $headerHeight = [float]($headerH + $pad)
    $titleRect = New-Object System.Drawing.RectangleF([float]$x, [float]0, [float]$cardW, $headerHeight)
    $g.DrawString($titles[$i], $font, $textBrush, $titleRect, $sf)

    # Sprite
    $g.DrawImage($frames[$i], $x, $y, $cardW, $cardH)
}

$outPath = 'I:\Shared drives\Pitch Quest Shared Drive\Working Files\Music Store\Export\Instruments\Grand Piano - Rarity Preview.png'
$bmp.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)

$f1.Dispose(); $f2.Dispose(); $f3.Dispose(); $f4.Dispose()
$g.Dispose(); $bmp.Dispose()
Write-Host "Generated Grand Piano preview successfully: $outPath"
