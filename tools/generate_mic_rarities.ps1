Add-Type -AssemblyName System.Drawing

$W = 32
$H = 60
$offsetX = 6  # Mic pole at x = 9 + 6 = 15

function Hex($hexStr, $a=255) {
    $h = $hexStr.TrimStart('#')
    $r = [Convert]::ToInt32($h.Substring(0, 2), 16)
    $g = [Convert]::ToInt32($h.Substring(2, 2), 16)
    $b = [Convert]::ToInt32($h.Substring(4, 2), 16)
    return [System.Drawing.Color]::FromArgb($a, $r, $g, $b)
}

# Load original base mic pixels
$origBmp = [System.Drawing.Bitmap]::FromFile("c:\Users\leero\Source\Repos\aseprite2\Mic.png")
$basePixels = @()
for ($y = 0; $y -lt $origBmp.Height; $y++) {
    for ($x = 0; $x -lt $origBmp.Width; $x++) {
        $p = $origBmp.GetPixel($x, $y)
        if ($p.A -gt 0) {
            $basePixels += ,@($x, $y, $p.R, $p.G, $p.B, $p.A)
        }
    }
}
$origBmp.Dispose()

$bmp1 = New-Object System.Drawing.Bitmap($W, $H)
$bmp2 = New-Object System.Drawing.Bitmap($W, $H)
$bmp3 = New-Object System.Drawing.Bitmap($W, $H)
$bmp4 = New-Object System.Drawing.Bitmap($W, $H)

# -------------------------------------------------------------
# 1. COMMON (Mic 1): Classic Studio Mic (Original Base design)
# -------------------------------------------------------------
foreach ($item in $basePixels) {
    $x = $item[0] + $offsetX
    $y = $item[1]
    $color = [System.Drawing.Color]::FromArgb($item[5], $item[2], $item[3], $item[4])
    $bmp1.SetPixel($x, $y, $color)
}

# -------------------------------------------------------------
# 2. RARE (Mic 2): Cobalt Platinum Studio Mic
# -------------------------------------------------------------
foreach ($item in $basePixels) {
    $ox = $item[0]
    $oy = $item[1]
    $x = $ox + $offsetX
    $y = $oy
    $a = $item[5]

    if ($oy -ge 3 -and $oy -le 6) {
        if ($oy -eq 3) { $c = Hex "FFFFFF" }
        elseif ($oy -eq 4) {
            if ($ox -eq 13) { $c = Hex "FFFFFF" }
            elseif ($ox -eq 12) { $c = Hex "BAE6FD" }
            else { $c = Hex "7DD3FC" }
        }
        elseif ($oy -eq 5) {
            if ($ox -eq 12) { $c = Hex "E0F2FE" }
            elseif ($ox -eq 15) { $c = Hex "38BDF8" }
            else { $c = Hex "7DD3FC" }
        }
        else { $c = Hex "0284C7" }
    }
    elseif ($oy -eq 7) {
        $c = Hex "00E5FF"
    }
    elseif ($oy -ge 8 -and $oy -le 15) {
        if ($ox -eq 8 -or $ox -eq 9) { $c = Hex "334155" }
        elseif ($ox -eq 10) { $c = Hex "475569" }
        else { $c = Hex "1E293B" }
    }
    elseif ($oy -ge 16 -and $oy -le 49) {
        if ($oy -eq 25) { $c = Hex "00E5FF" }
        elseif ($oy -eq 24 -or $oy -eq 26) { $c = Hex "0284C7" }
        elseif ($oy % 2 -eq 0) { $c = Hex "CBD5E1" }
        else { $c = Hex "94A3B8" }
    }
    elseif ($oy -ge 50 -and $oy -le 57) {
        if ($a -eq 255) {
            $c = Hex "64748B"
            if ($oy -eq 54 -and ($ox -eq 4 -or $ox -eq 5)) { $c = Hex "94A3B8" }
        } else {
            $c = [System.Drawing.Color]::FromArgb($a, 15, 23, 42)
        }
    }
    else {
        $c = [System.Drawing.Color]::FromArgb($a, $item[2], $item[3], $item[4])
    }
    $bmp2.SetPixel($x, $y, $c)
}
$bmp2.SetPixel($offsetX + 10, 7, (Hex "00B0FF"))
$bmp2.SetPixel($offsetX + 15, 7, (Hex "0091EA"))

# -------------------------------------------------------------
# 3. EPIC (Mic 3): Vintage Royal Gold & Amethyst Velvet
# Classic vintage studio mic with circular shockmount & purple velvet handle
# -------------------------------------------------------------
foreach ($item in $basePixels) {
    $ox = $item[0]
    $oy = $item[1]
    $x = $ox + $offsetX
    $y = $oy
    $a = $item[5]

    if ($oy -ge 3 -and $oy -le 6) {
        # Gold vintage capsule
        if ($oy -eq 3) {
            if ($ox -eq 13) { $c = Hex "FFFBEB" } else { $c = Hex "FEF08A" }
        }
        elseif ($oy -eq 4) {
            if ($ox -eq 13) { $c = Hex "FEF08A" }
            elseif ($ox -eq 14) { $c = Hex "FBBF24" }
            elseif ($ox -eq 12) { $c = Hex "F59E0B" }
            else { $c = Hex "D97706" }
        }
        elseif ($oy -eq 5) {
            if ($ox -eq 13) { $c = Hex "FBBF24" }
            elseif ($ox -eq 14) { $c = Hex "F59E0B" }
            elseif ($ox -eq 12) { $c = Hex "D97706" }
            else { $c = Hex "B45309" }
        }
        else {
            $c = Hex "B45309"
        }
    }
    elseif ($oy -eq 7) {
        # Shockmount collar
        $c = Hex "FBBF24"
    }
    elseif ($oy -ge 8 -and $oy -le 15) {
        # Royal Purple Velvet Handle
        if ($oy -eq 8 -or $oy -eq 15) {
            $c = Hex "F59E0B" # Gold trim bands
        } elseif ($ox -eq 8 -or $ox -eq 9) {
            $c = Hex "C084FC" # Purple highlight
        } elseif ($ox -eq 10) {
            $c = Hex "9333EA" # Purple mid
        } else {
            $c = Hex "581C87" # Purple shadow
        }
    }
    elseif ($oy -ge 16 -and $oy -le 49) {
        # Brass / Gold Stand
        if ($oy % 3 -eq 0) { $c = Hex "FDE68A" }
        elseif ($oy % 3 -eq 1) { $c = Hex "F59E0B" }
        else { $c = Hex "D97706" }
    }
    elseif ($oy -ge 50 -and $oy -le 57) {
        if ($a -eq 255) {
            if ($oy -eq 54 -or $oy -eq 55) { $c = Hex "B45309" }
            else { $c = Hex "D97706" }
        } else {
            $c = [System.Drawing.Color]::FromArgb($a, 40, 20, 5)
        }
    }
    else {
        $c = [System.Drawing.Color]::FromArgb($a, $item[2], $item[3], $item[4])
    }
    $bmp3.SetPixel($x, $y, $c)
}

# Vintage Shockmount Frame / Outer Ring for Mic 3 (Epic)
$shockmountPixels = @(
    # Left shockmount bracket:
    @(14, 4, "FEF08A"), @(13, 5, "F59E0B"), @(13, 6, "D97706"), @(14, 7, "B45309"),
    # Right shockmount bracket:
    @(23, 4, "FEF08A"), @(24, 5, "F59E0B"), @(24, 6, "D97706"), @(23, 7, "B45309"),
    # Cross elastic bands:
    @(15, 5, "9333EA"), @(22, 5, "9333EA")
)
foreach ($sp in $shockmountPixels) {
    $bmp3.SetPixel($sp[0], $sp[1], (Hex $sp[2]))
}

# -------------------------------------------------------------
# 4. LEGENDARY (Mic 4): Celestial Seraphim (Angelic Golden Wings Aura)
# Rich deep bronze outlines, brilliant golden highlights, layered wings
# -------------------------------------------------------------
foreach ($item in $basePixels) {
    $ox = $item[0]
    $oy = $item[1]
    $x = $ox + $offsetX
    $y = $oy
    $a = $item[5]

    if ($oy -ge 3 -and $oy -le 6) {
        # Brilliant Gold & Sunburst Core
        if ($oy -eq 3) {
            if ($ox -eq 13) { $c = Hex "FFFFFF" }
            elseif ($ox -eq 12 -or $ox -eq 14) { $c = Hex "FEF08A" }
            else { $c = Hex "FDE047" }
        }
        elseif ($oy -eq 4) {
            if ($ox -eq 13) { $c = Hex "FFFFFF" }
            elseif ($ox -eq 14) { $c = Hex "FEF08A" }
            elseif ($ox -eq 12) { $c = Hex "F59E0B" }
            else { $c = Hex "92400E" }
        }
        elseif ($oy -eq 5) {
            if ($ox -eq 13) { $c = Hex "FEF08A" }
            elseif ($ox -eq 14) { $c = Hex "F59E0B" }
            elseif ($ox -eq 12) { $c = Hex "D97706" }
            else { $c = Hex "78350F" }
        }
        else {
            if ($ox -eq 13) { $c = Hex "F59E0B" } else { $c = Hex "78350F" }
        }
    }
    elseif ($oy -eq 7) {
        # Golden Shockmount Ring with Ruby Gem in center
        if ($ox -eq 12) { $c = Hex "EF4444" } # Ruby gemstone
        else { $c = Hex "F59E0B" }
    }
    elseif ($oy -ge 8 -and $oy -le 15) {
        # Ornate Gold Fluted Handle
        if ($oy -eq 8 -or $oy -eq 15) {
            $c = Hex "78350F" # Outline ring
        } elseif ($ox -eq 8) {
            $c = Hex "78350F" # Outline
        } elseif ($ox -eq 9) {
            $c = Hex "FEF08A" # Gold highlight
        } elseif ($ox -eq 10) {
            $c = Hex "F59E0B" # Gold mid
        } elseif ($ox -eq 11) {
            $c = Hex "D97706" # Warm gold
        } else {
            $c = Hex "92400E" # Bronze shadow
        }
    }
    elseif ($oy -ge 16 -and $oy -le 49) {
        # Celestial Fluted Stand
        if ($oy % 6 -eq 0) { $c = Hex "FEF08A" }
        elseif ($oy % 3 -eq 0) { $c = Hex "F59E0B" }
        elseif ($oy % 2 -eq 0) { $c = Hex "D97706" }
        else { $c = Hex "92400E" }
    }
    elseif ($oy -ge 50 -and $oy -le 57) {
        if ($a -eq 255) {
            if ($oy -eq 54 -and ($ox -eq 4 -or $ox -eq 5)) { $c = Hex "FEF08A" }
            elseif ($oy -eq 55 -or $oy -eq 56) { $c = Hex "B45309" }
            else { $c = Hex "78350F" }
        } else {
            $c = [System.Drawing.Color]::FromArgb($a, 180, 83, 9)
        }
    }
    else {
        $c = [System.Drawing.Color]::FromArgb($a, $item[2], $item[3], $item[4])
    }
    $bmp4.SetPixel($x, $y, $c)
}

# Detailed Angelic Wings connected to Mic Capsule & Shockmount (x=18, y=5..7)
# Stylized after Trumphet4.png wings:
# Dark outline #78350F, shadow #B45309, midtone #F59E0B, light #FEF08A, tip #FFFFFF
$wingsConnected = @(
    # --- LEFT WING (sweeps up & left from collar at x=17, y=6..7) ---
    # Tip feather (top left):
    @(7, 0, "78350F"), @(8, 0, "FEF08A"), @(9, 0, "FFFFFF"), @(10, 0, "78350F"),
    @(6, 1, "78350F"), @(7, 1, "FEF08A"), @(8, 1, "FFFFFF"), @(9, 1, "FDE047"), @(10, 1, "78350F"),
    @(6, 2, "78350F"), @(7, 2, "FFFFFF"), @(8, 2, "FEF08A"), @(9, 2, "F59E0B"), @(10, 2, "B45309"), @(11, 2, "78350F"),
    # Second feather tier:
    @(7, 3, "78350F"), @(8, 3, "FFFFFF"), @(9, 3, "FEF08A"), @(10, 3, "F59E0B"), @(11, 3, "B45309"), @(12, 3, "78350F"),
    @(8, 4, "78350F"), @(9, 4, "FEF08A"), @(10, 4, "FDE047"), @(11, 4, "F59E0B"), @(12, 4, "B45309"), @(13, 4, "78350F"),
    # Lower wing & connection to mic collar (x=14..16):
    @(10, 5, "78350F"), @(11, 5, "FEF08A"), @(12, 5, "F59E0B"), @(13, 5, "B45309"), @(14, 5, "78350F"),
    @(12, 6, "78350F"), @(13, 6, "F59E0B"), @(14, 6, "FEF08A"), @(15, 6, "F59E0B"), @(16, 6, "78350F"),
    @(14, 7, "78350F"), @(15, 7, "F59E0B"), @(16, 7, "FEF08A"),

    # --- RIGHT WING (sweeps up & right from collar at x=20, y=6..7) ---
    # Tip feather (top right):
    @(27, 0, "78350F"), @(28, 0, "FFFFFF"), @(29, 0, "FEF08A"), @(30, 0, "78350F"),
    @(27, 1, "78350F"), @(28, 1, "FDE047"), @(29, 1, "FFFFFF"), @(30, 1, "FEF08A"), @(31, 1, "78350F"),
    @(26, 2, "78350F"), @(27, 2, "B45309"), @(28, 2, "F59E0B"), @(29, 2, "FEF08A"), @(30, 2, "FFFFFF"), @(31, 2, "78350F"),
    # Second feather tier:
    @(25, 3, "78350F"), @(26, 3, "B45309"), @(27, 3, "F59E0B"), @(28, 3, "FEF08A"), @(29, 3, "FFFFFF"), @(30, 3, "78350F"),
    @(24, 4, "78350F"), @(25, 4, "B45309"), @(26, 4, "F59E0B"), @(27, 4, "FDE047"), @(28, 4, "FEF08A"), @(29, 4, "78350F"),
    # Lower wing & connection to mic collar (x=21..23):
    @(23, 5, "78350F"), @(24, 5, "B45309"), @(25, 5, "F59E0B"), @(26, 5, "FEF08A"), @(27, 5, "78350F"),
    @(21, 6, "78350F"), @(22, 6, "F59E0B"), @(23, 6, "FEF08A"), @(24, 6, "F59E0B"), @(25, 6, "78350F"),
    @(21, 7, "FEF08A"), @(22, 7, "F59E0B"), @(23, 7, "78350F")
)

foreach ($w in $wingsConnected) {
    $bmp4.SetPixel($w[0], $w[1], (Hex $w[2]))
}

# Celestial Halo & Sparkle Aura
$legendaryAura = @(
    # Top halo star:
    @(18, 0, "FFFFFF"), @(19, 0, "FEF08A"),
    # Left sparkle glint:
    @(4, 3, "FEF08A"), @(3, 4, "FFFFFF"), @(4, 4, "FFFFFF"), @(5, 4, "FFFFFF"), @(4, 5, "FEF08A"),
    # Right sparkle glint:
    @(30, 4, "FEF08A"), @(29, 5, "FFFFFF"), @(30, 5, "FFFFFF"), @(31, 5, "FFFFFF"), @(30, 6, "FEF08A")
)
foreach ($a in $legendaryAura) {
    $bmp4.SetPixel($a[0], $a[1], (Hex $a[2]))
}

# -------------------------------------------------------------
# SAVE PNGs:
# -------------------------------------------------------------
$outDirInstruments = "I:\Shared drives\Pitch Quest Shared Drive\Working Files\Music Store\Export\Instruments"
$localOutDir = "c:\Users\leero\Source\Repos\aseprite2\ref_instruments"

$bmps = @($bmp1, $bmp2, $bmp3, $bmp4)
for ($i = 0; $i -lt 4; $i++) {
    $num = $i + 1
    $curBmp = $bmps[$i]

    # Native 1x
    $curBmp.Save("$localOutDir\Mic$num.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $curBmp.Save("$outDirInstruments\Mic$num.png", [System.Drawing.Imaging.ImageFormat]::Png)

    # 4x Upscaled Preview (pixel-perfect 4x nearest neighbor)
    $scaleBmp = New-Object System.Drawing.Bitmap 128, 240
    for ($sy = 0; $sy -lt 60; $sy++) {
        for ($sx = 0; $sx -lt 32; $sx++) {
            $px = $curBmp.GetPixel($sx, $sy)
            if ($px.A -gt 0) {
                for ($dy = 0; $dy -lt 4; $dy++) {
                    for ($dx = 0; $dx -lt 4; $dx++) {
                        $scaleBmp.SetPixel(($sx * 4 + $dx), ($sy * 4 + $dy), $px)
                    }
                }
            }
        }
    }

    $scaleBmp.Save("$localOutDir\Mic-Preview$num.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $scaleBmp.Save("$outDirInstruments\Mic-Preview$num.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $scaleBmp.Dispose()
    $curBmp.Dispose()
}

Write-Host "Generated all 4 refined mic rarity variations successfully!"
