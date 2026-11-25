Add-Type -AssemblyName System.Drawing

$origW = 20
$origH = 60
$W = 32
$H = 60
$offX = 6
$offY = 0

function Hex($hexStr, $a = 255) {
    $h = $hexStr.TrimStart('#')
    $r = [Convert]::ToInt32($h.Substring(0, 2), 16)
    $g = [Convert]::ToInt32($h.Substring(2, 2), 16)
    $b = [Convert]::ToInt32($h.Substring(4, 2), 16)
    return [System.Drawing.Color]::FromArgb([int]$a, [int]$r, [int]$g, [int]$b)
}

function LerpColor($c1, $c2, [double]$t, $a = 255) {
    if ($t -lt 0.0) { $t = 0.0 }
    if ($t -gt 1.0) { $t = 1.0 }
    $r = [int]([Math]::Round($c1.R + ($c2.R - $c1.R) * $t))
    $g = [int]([Math]::Round($c1.G + ($c2.G - $c1.G) * $t))
    $b = [int]([Math]::Round($c1.B + ($c2.B - $c1.B) * $t))
    return [System.Drawing.Color]::FromArgb([int]$a, $r, $g, $b)
}

function PaletteRamp($shadowHex, $darkHex, $midHex, $lightHex, $hiHex, [double]$lum, $a = 255) {
    $c0 = Hex $shadowHex
    $c1 = Hex $darkHex
    $c2 = Hex $midHex
    $c3 = Hex $lightHex
    $c4 = Hex $hiHex
    if ($lum -le 0.25) { return LerpColor $c0 $c1 ($lum / 0.25) $a }
    elseif ($lum -le 0.50) { return LerpColor $c1 $c2 (($lum - 0.25) / 0.25) $a }
    elseif ($lum -le 0.75) { return LerpColor $c2 $c3 (($lum - 0.50) / 0.25) $a }
    else { return LerpColor $c3 $c4 (($lum - 0.75) / 0.25) $a }
}

$micBase = [System.Drawing.Bitmap]::FromFile("i:\Shared drives\Pitch Quest Shared Drive\Finalized Files\Instruments\Mic.png")

# Classify base pixels (ox in 0..19, oy in 0..59)
$pixelData = @()
for ($y = 0; $y -lt $origH; $y++) {
    for ($x = 0; $x -lt $origW; $x++) {
        $p = $micBase.GetPixel($x, $y)
        if ($p.A -gt 0) {
            $lum = (0.299 * $p.R + 0.587 * $p.G + 0.114 * $p.B) / 255.0
            $part = "UNKNOWN"

            if ($y -ge 55 -and ($p.R -le 40 -and $p.G -le 30 -and $p.B -le 10)) {
                $part = "SHADOW"
            }
            elseif ($y -ge 53) {
                $part = "BASE"
            }
            elseif ($y -ge 26) {
                $part = "STAND_SHAFT"
            }
            elseif ($y -ge 22) {
                $part = "CLIP"
            }
            elseif ($y -ge 13) {
                $part = "HANDLE"
            }
            else {
                if ($y -ge 11) { $part = "GRILL_COLLAR" }
                else { $part = "GRILL_MESH" }
            }

            $pixelData += [PSCustomObject]@{
                X = $x
                Y = $y
                Color = $p
                Alpha = $p.A
                Lum = $lum
                Part = $part
            }
        }
    }
}
$micBase.Dispose()

# 16 Refined, Non-Flashy Variations with Prominent Gold on Legendary
$variations = @(
    # --- COMMON (Tier 1 - Studio Essentials) ---
    @{
        Id = 1; Rarity = "Common"; VarIdx = 1; RarityIdx = 1; Name = "Studio Standard"
        Mesh = @("201F26", "545156", "7B7276", "978A8A", "AEA19C")
        Handle = @("18181B", "2F2E37", "383740", "5A565B", "94A3B8")
        Stand = @("18181B", "212128", "2F2E37", "3E3C45", "64748B")
        Accent = "94A3B8"; Shockmount = $false; LegendaryGold = $false
    },
    @{
        Id = 2; Rarity = "Common"; VarIdx = 2; RarityIdx = 1; Name = "Matte Obsidian"
        Mesh = @("09090B", "18181B", "27272A", "3F3F46", "71717A")
        Handle = @("09090B", "18181B", "27272A", "3F3F46", "52525B")
        Stand = @("09090B", "18181B", "27272A", "3F3F46", "52525B")
        Accent = "52525B"; Shockmount = $false; LegendaryGold = $false
    },
    @{
        Id = 3; Rarity = "Common"; VarIdx = 3; RarityIdx = 1; Name = "Vintage Nickel"
        Mesh = @("334155", "475569", "64748B", "94A3B8", "CBD5E1")
        Handle = @("1E293B", "334155", "475569", "64748B", "94A3B8")
        Stand = @("1E293B", "334155", "475569", "64748B", "94A3B8")
        Accent = "CBD5E1"; Shockmount = $false; LegendaryGold = $false
    },
    @{
        Id = 4; Rarity = "Common"; VarIdx = 4; RarityIdx = 1; Name = "Warm Bronze"
        Mesh = @("451A03", "78350F", "92400E", "B45309", "D97706")
        Handle = @("1C1917", "292524", "44403C", "57534E", "78716C")
        Stand = @("1C1917", "292524", "44403C", "57534E", "78716C")
        Accent = "B45309"; Shockmount = $false; LegendaryGold = $false
    },

    # --- RARE (Tier 2 - Premium Studio Finishes, Subtle Accents) ---
    @{
        Id = 5; Rarity = "Rare"; VarIdx = 1; RarityIdx = 2; Name = "Cobalt Blue Pro"
        Mesh = @("1E293B", "334155", "64748B", "94A3B8", "E2E8F0")
        Handle = @("0F172A", "1E3A8A", "1D4ED8", "2563EB", "60A5FA")
        Stand = @("0F172A", "1E293B", "334155", "475569", "64748B")
        Accent = "38BDF8"; CollarAccent = "38BDF8"; Shockmount = $false; LegendaryGold = $false
    },
    @{
        Id = 6; Rarity = "Rare"; VarIdx = 2; RarityIdx = 2; Name = "Alpine White"
        Mesh = @("334155", "475569", "64748B", "94A3B8", "E2E8F0")
        Handle = @("475569", "64748B", "94A3B8", "E2E8F0", "F8FAFC")
        Stand = @("1E293B", "334155", "475569", "64748B", "94A3B8")
        Accent = "E2E8F0"; CollarAccent = "CBD5E1"; Shockmount = $false; LegendaryGold = $false
    },
    @{
        Id = 7; Rarity = "Rare"; VarIdx = 3; RarityIdx = 2; Name = "Racing Green"
        Mesh = @("1E293B", "334155", "64748B", "94A3B8", "CBD5E1")
        Handle = @("022C22", "064E3B", "065F46", "047857", "10B981")
        Stand = @("0F172A", "1E293B", "334155", "475569", "64748B")
        Accent = "34D399"; CollarAccent = "D97706"; Shockmount = $false; LegendaryGold = $false
    },
    @{
        Id = 8; Rarity = "Rare"; VarIdx = 4; RarityIdx = 2; Name = "Crimson Ribbon"
        Mesh = @("1E293B", "334155", "64748B", "94A3B8", "E2E8F0")
        Handle = @("450A0A", "7F1D1D", "991B1B", "B91C1C", "DC2626")
        Stand = @("0F172A", "1E293B", "334155", "475569", "64748B")
        Accent = "EF4444"; CollarAccent = "94A3B8"; Shockmount = $false; LegendaryGold = $false
    },

    # --- EPIC (Tier 3 - Studio Broadcast Shockmount) ---
    @{
        Id = 9; Rarity = "Epic"; VarIdx = 1; RarityIdx = 3; Name = "Broadcast Studio"
        Mesh = @("18181B", "2F2E37", "475569", "64748B", "CBD5E1")
        Handle = @("18181B", "27272A", "3F3F46", "52525B", "71717A")
        Stand = @("18181B", "27272A", "3F3F46", "52525B", "94A3B8")
        Accent = "CBD5E1"; ShockColor = "94A3B8"; BandColor = "334155"; Shockmount = $true; LegendaryGold = $false
    },
    @{
        Id = 10; Rarity = "Epic"; VarIdx = 2; RarityIdx = 3; Name = "Royal Amethyst"
        Mesh = @("2E1065", "4C1D95", "581C87", "7E22CE", "A855F7")
        Handle = @("2E1065", "3B0764", "4C1D95", "581C87", "6B21A8")
        Stand = @("18181B", "27272A", "3F3F46", "52525B", "71717A")
        Accent = "C084FC"; ShockColor = "A855F7"; BandColor = "2E1065"; Shockmount = $true; LegendaryGold = $false
    },
    @{
        Id = 11; Rarity = "Epic"; VarIdx = 3; RarityIdx = 3; Name = "Rose Gold Edition"
        Mesh = @("4C0519", "881337", "9F1239", "BE123C", "FB7185")
        Handle = @("4C0519", "881337", "9F1239", "BE123C", "FDA4AF")
        Stand = @("1C1917", "292524", "44403C", "78716C", "A8A29E")
        Accent = "FB7185"; ShockColor = "FB7185"; BandColor = "4C0519"; Shockmount = $true; LegendaryGold = $false
    },
    @{
        Id = 12; Rarity = "Epic"; VarIdx = 4; RarityIdx = 3; Name = "Platinum Deluxe"
        Mesh = @("334155", "475569", "64748B", "94A3B8", "E2E8F0")
        Handle = @("334155", "475569", "64748B", "94A3B8", "F8FAFC")
        Stand = @("1E293B", "334155", "475569", "64748B", "94A3B8")
        Accent = "E2E8F0"; ShockColor = "E2E8F0"; BandColor = "475569"; Shockmount = $true; LegendaryGold = $false
    },

    # --- LEGENDARY (Tier 4 - Golden Masterpieces, Prominent Gold Parts, Zero Clutter) ---
    @{
        Id = 13; Rarity = "Legendary"; VarIdx = 1; RarityIdx = 4; Name = "24K Gold Sovereign"
        # Solid 24k polished gold capsule mesh, gold shockmount, gold base feet & pinstripe
        Mesh = @("78350F", "B45309", "D97706", "FBBF24", "FEF08A")
        Handle = @("18181B", "27272A", "3F3F46", "52525B", "FBBF24")
        Stand = @("18181B", "27272A", "3F3F46", "52525B", "FBBF24")
        Accent = "FEF08A"; ShockColor = "FBBF24"; BandColor = "78350F"; Shockmount = $true; LegendaryGold = $true
        GoldCapsule = $true; GoldBase = $true; GoldPinstripe = $true
    },
    @{
        Id = 14; Rarity = "Legendary"; VarIdx = 2; RarityIdx = 4; Name = "Golden Imperial Crimson"
        # 24k gold capsule mesh, imperial crimson lacquer handle, gold shockmount & gold stand collars
        Mesh = @("78350F", "B45309", "D97706", "FBBF24", "FEF08A")
        Handle = @("450A0A", "7F1D1D", "991B1B", "B91C1C", "DC2626")
        Stand = @("18181B", "27272A", "450A0A", "78350F", "FBBF24")
        Accent = "FEF08A"; ShockColor = "FBBF24"; BandColor = "7F1D1D"; Shockmount = $true; LegendaryGold = $true
        GoldCapsule = $true; GoldBase = $true; GoldPinstripe = $false
    },
    @{
        Id = 15; Rarity = "Legendary"; VarIdx = 3; RarityIdx = 4; Name = "Celestial Gold & Lapis"
        # Deep royal lapis blue body with gleaming 24k gold grille, gold shockmount & gold clutch
        Mesh = @("78350F", "B45309", "D97706", "FBBF24", "FEF08A")
        Handle = @("0F172A", "1E3A8A", "1D4ED8", "1E40AF", "FBBF24")
        Stand = @("0F172A", "1E293B", "1E3A8A", "78350F", "FBBF24")
        Accent = "FEF08A"; ShockColor = "FBBF24"; BandColor = "1E3A8A"; Shockmount = $true; LegendaryGold = $true
        GoldCapsule = $true; GoldBase = $true; GoldPinstripe = $true
    },
    @{
        Id = 16; Rarity = "Legendary"; VarIdx = 4; RarityIdx = 4; Name = "Imperial Jade & Gold"
        # Emerald jade body with 24k gold capsule, gold shockmount, and gold hardware
        Mesh = @("78350F", "B45309", "D97706", "FBBF24", "FEF08A")
        Handle = @("022C22", "064E3B", "065F46", "047857", "FBBF24")
        Stand = @("022C22", "064E3B", "18181B", "78350F", "FBBF24")
        Accent = "FEF08A"; ShockColor = "FBBF24"; BandColor = "064E3B"; Shockmount = $true; LegendaryGold = $true
        GoldCapsule = $true; GoldBase = $true; GoldPinstripe = $true
    }
)

function SetPx([System.Drawing.Bitmap]$b, [int]$x, [int]$y, [string]$hex, [int]$a = 255) {
    if ($x -ge 0 -and $x -lt $b.Width -and $y -ge 0 -and $y -lt $b.Height) {
        $b.SetPixel($x, $y, (Hex $hex $a))
    }
}

function Draw-CompactShockmount([System.Drawing.Bitmap]$b, [string]$ringHex, [string]$bandHex, [int]$cx, [int]$cy) {
    # Neat, compact 10px circular studio shockmount ring hugging the capsule base (y=14..20)
    # No oversized messy projections
    $ringPts = @(
        @(-5, -2), @(-5, -1), @(-5, 0), @(-5, 1), @(-5, 2),
        @( 5, -2), @( 5, -1), @( 5, 0), @( 5, 1), @( 5, 2),
        @(-4, -3), @(-3, -3), @( 3, -3), @( 4, -3),
        @(-4,  3), @(-3,  3), @( 3,  3), @( 4,  3)
    )
    foreach ($pt in $ringPts) {
        SetPx $b ($cx + $pt[0]) ($cy + $pt[1]) $ringHex
    }

    # Clean taut suspension bands
    SetPx $b ($cx - 3) ($cy - 1) $bandHex
    SetPx $b ($cx - 2) ($cy - 1) $bandHex
    SetPx $b ($cx + 2) ($cy - 1) $bandHex
    SetPx $b ($cx + 3) ($cy - 1) $bandHex
    SetPx $b ($cx - 3) ($cy + 1) $bandHex
    SetPx $b ($cx - 2) ($cy + 1) $bandHex
    SetPx $b ($cx + 2) ($cy + 1) $bandHex
    SetPx $b ($cx + 3) ($cy + 1) $bandHex
}

function Apply-GoldAccents([System.Drawing.Bitmap]$b, [int]$cx, [bool]$goldPinstripe, [bool]$goldBase) {
    # 1. 24K Gold Capsule Bevel Highlight (ox=14..17, oy=0..1)
    SetPx $b ($cx - 1) 0 "FEF08A"
    SetPx $b $cx 0 "FEF08A"
    SetPx $b ($cx + 1) 0 "FBBF24"

    # 2. Gold Capsule Collar Ring (oy=11..12)
    for ($gx = ($cx - 2); $gx -le ($cx + 3); $gx++) {
        SetPx $b $gx 11 "FEF08A"
        SetPx $b $gx 12 "D97706"
    }

    # 3. Gold Handle Pinstripe Inlay (oy=15..20)
    if ($goldPinstripe) {
        for ($gy = 15; $gy -le 20; $gy += 2) {
            SetPx $b $cx $gy "FBBF24"
        }
    }

    # 4. Gold Stand Clutch Collar (oy=27..28)
    SetPx $b ($cx - 1) 27 "FBBF24"
    SetPx $b $cx 27 "FEF08A"
    SetPx $b ($cx - 1) 28 "B45309"
    SetPx $b $cx 28 "D97706"

    # 5. Gold Tripod Base Feet Tips (oy=54..57)
    if ($goldBase) {
        SetPx $b ($cx - 7) 54 "FBBF24"
        SetPx $b ($cx - 6) 55 "FEF08A"
        SetPx $b ($cx + 6) 55 "FEF08A"
        SetPx $b ($cx + 7) 54 "FBBF24"
        SetPx $b $cx 57 "FEF08A"
        SetPx $b ($cx + 1) 57 "FBBF24"
    }
}

$outDirWorking = "i:\Shared drives\Pitch Quest Shared Drive\Working Files\Music Store\Export\Instruments"
$localOutDir = "C:\Users\leero\Source\Repos\aseprite2\ref_instruments\mic"
New-Item -ItemType Directory -Force -Path $localOutDir | Out-Null
New-Item -ItemType Directory -Force -Path $outDirWorking | Out-Null

$generatedBitmaps = @()

foreach ($v in $variations) {
    $bmp = New-Object System.Drawing.Bitmap($W, $H)

    # Base instrument body rendering with centered offset ($offX = 6)
    foreach ($px in $pixelData) {
        $x = $px.X + $offX
        $y = $px.Y + $offY
        $lum = $px.Lum
        $a = $px.Alpha

        $c = $px.Color
        switch ($px.Part) {
            "GRILL_MESH" {
                $adjLum = [Math]::Min(1.0, $lum * 1.05)
                $r = $v.Mesh
                $c = PaletteRamp $r[0] $r[1] $r[2] $r[3] $r[4] $adjLum $a
            }
            "GRILL_COLLAR" {
                $c = Hex $v.Accent $a
            }
            "HANDLE" {
                $adjLum = [Math]::Min(1.0, $lum * 1.05)
                $r = $v.Handle
                $c = PaletteRamp $r[0] $r[1] $r[2] $r[3] $r[4] $adjLum $a
            }
            "CLIP" {
                $c = Hex $v.Accent $a
            }
            "STAND_SHAFT" {
                $r = $v.Stand
                $c = PaletteRamp $r[0] $r[1] $r[2] $r[3] $r[4] $lum $a
            }
            "BASE" {
                $r = $v.Stand
                $c = PaletteRamp $r[0] $r[1] $r[2] $r[3] $r[4] $lum $a
            }
            "SHADOW" {
                $c = Hex "14100A" 140
            }
            default {
                $c = $px.Color
            }
        }
        $bmp.SetPixel($x, $y, $c)
    }

    # Rare Features: Subtle, crisp 1px collar ring (no noisy particle crosses)
    if ($v.RarityIdx -eq 2) {
        for ($rx = (15 - 2); $rx -le (15 + 3); $rx++) {
            SetPx $bmp $rx 11 $v.CollarAccent
        }
    }

    # Epic Features: Compact, clean studio shockmount ring
    if ($v.Shockmount -and (-not $v.LegendaryGold)) {
        Draw-CompactShockmount $bmp $v.ShockColor $v.BandColor 15 17
    }

    # Legendary Features: Explicit Golden Parts (Gold Capsule, Gold Shockmount, Gold Inlays & Feet, zero floating clutter)
    if ($v.LegendaryGold) {
        Draw-CompactShockmount $bmp $v.ShockColor $v.BandColor 15 17
        Apply-GoldAccents $bmp 15 $v.GoldPinstripe $v.GoldBase
    }

    $id = $v.Id
    $bmp.Save("$localOutDir\Mic_Var$id.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Save("$outDirWorking\Mic - Variation $id ($($v.Rarity) $($v.VarIdx)).png", [System.Drawing.Imaging.ImageFormat]::Png)

    # Save representative tier PNGs
    if ($v.VarIdx -eq 1) {
        $rNum = $v.RarityIdx
        $bmp.Save("$outDirWorking\Mic$rNum.png", [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Save("$localOutDir\Mic$rNum.png", [System.Drawing.Imaging.ImageFormat]::Png)

        # 4x Nearest Neighbor upscale preview
        $p4 = New-Object System.Drawing.Bitmap(($W * 4), ($H * 4))
        for ($sy = 0; $sy -lt $H; $sy++) {
            for ($sx = 0; $sx -lt $W; $sx++) {
                $px = $bmp.GetPixel($sx, $sy)
                if ($px.A -gt 0) {
                    for ($dy = 0; $dy -lt 4; $dy++) {
                        for ($dx = 0; $dx -lt 4; $dx++) {
                            $p4.SetPixel(($sx * 4 + $dx), ($sy * 4 + $dy), $px)
                        }
                    }
                }
            }
        }
        $p4.Save("$outDirWorking\Mic-Preview$rNum.png", [System.Drawing.Imaging.ImageFormat]::Png)
        $p4.Save("$localOutDir\Mic-Preview$rNum.png", [System.Drawing.Imaging.ImageFormat]::Png)
        $p4.Dispose()
    }

    $generatedBitmaps += $bmp
}

# Build Master 4x4 Contact Preview Sheet (4 Rarities Rows x 4 Variations Columns)
$scale = 3
$cellW = $W * $scale
$cellH = $H * $scale
$pad = 16
$headerH = 34
$labelW = 140
$gridW = $labelW + 4 * $cellW + 5 * $pad
$gridH = $headerH + 4 * $cellH + 5 * $pad

$sheet = New-Object System.Drawing.Bitmap($gridW, $gridH)
$g = [System.Drawing.Graphics]::FromImage($sheet)
$g.Clear((Hex "14161F"))

$fontTitle = New-Object System.Drawing.Font("Segoe UI", 10.5, [System.Drawing.FontStyle]::Bold)
$fontSub = New-Object System.Drawing.Font("Segoe UI", 8.5, [System.Drawing.FontStyle]::Bold)
$brushWhite = New-Object System.Drawing.SolidBrush((Hex "F8FAFC"))

$rarityHeaders = @(
    @{ Name = "COMMON"; Color = "94A3B8"; Bg = "1E2230" },
    @{ Name = "RARE"; Color = "38BDF8"; Bg = "112840" },
    @{ Name = "EPIC"; Color = "C084FC"; Bg = "281542" },
    @{ Name = "LEGENDARY"; Color = "FACC15"; Bg = "362410" }
)

for ($c = 0; $c -lt 4; $c++) {
    $cx = $labelW + $pad + $c * ($cellW + $pad)
    $g.DrawString("Variation $($c + 1)", $fontTitle, $brushWhite, ($cx + [int]($cellW / 2) - 40), 8)
}

for ($r = 0; $r -lt 4; $r++) {
    $ry = $headerH + $pad + $r * ($cellH + $pad)
    $rh = $rarityHeaders[$r]
    $brushR = New-Object System.Drawing.SolidBrush((Hex $rh.Color))
    $brushBg = New-Object System.Drawing.SolidBrush((Hex $rh.Bg))
    $penBorder = New-Object System.Drawing.Pen((Hex $rh.Color 100), 2)

    $g.DrawString($rh.Name, $fontTitle, $brushR, 14, ($ry + [int]($cellH / 2) - 16))
    $g.DrawString("Tier $($r + 1) (4 Vars)", $fontSub, $brushWhite, 14, ($ry + [int]($cellH / 2) + 6))

    # Build per-rarity 4-variation strip
    $stripW = 4 * $cellW + 5 * $pad
    $stripH = $cellH + 2 * $pad
    $rStrip = New-Object System.Drawing.Bitmap($stripW, $stripH)
    $gr = [System.Drawing.Graphics]::FromImage($rStrip)
    $gr.Clear((Hex $rh.Bg))

    for ($c = 0; $c -lt 4; $c++) {
        $idx = $r * 4 + $c
        $curBmp = $generatedBitmaps[$idx]
        $cx = $labelW + $pad + $c * ($cellW + $pad)
        $scx = $pad + $c * ($cellW + $pad)

        $g.FillRectangle($brushBg, $cx, $ry, $cellW, $cellH)
        $g.DrawRectangle($penBorder, $cx, $ry, $cellW, $cellH)
        $gr.DrawRectangle($penBorder, $scx, $pad, $cellW, $cellH)

        for ($sy = 0; $sy -lt $H; $sy++) {
            for ($sx = 0; $sx -lt $W; $sx++) {
                $px = $curBmp.GetPixel($sx, $sy)
                if ($px.A -gt 0) {
                    $bgC = Hex $rh.Bg
                    $alpha = $px.A / 255.0
                    $outR = [int]([Math]::Round($px.R * $alpha + $bgC.R * (1.0 - $alpha)))
                    $outG = [int]([Math]::Round($px.G * $alpha + $bgC.G * (1.0 - $alpha)))
                    $outB = [int]([Math]::Round($px.B * $alpha + $bgC.B * (1.0 - $alpha)))
                    $compC = [System.Drawing.Color]::FromArgb(255, $outR, $outG, $outB)
                    for ($dy = 0; $dy -lt $scale; $dy++) {
                        for ($dx = 0; $dx -lt $scale; $dx++) {
                            $sheet.SetPixel(($cx + $sx * $scale + $dx), ($ry + $sy * $scale + $dy), $compC)
                            $rStrip.SetPixel(($scx + $sx * $scale + $dx), ($pad + $sy * $scale + $dy), $compC)
                        }
                    }
                }
            }
        }
    }
    $gr.Dispose()
    $rName = $variations[$r * 4].Rarity
    $rStrip.Save("$outDirWorking\Mic - $rName Variations Preview.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $rStrip.Save("$localOutDir\Mic - $rName Variations Preview.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $rStrip.Dispose()
}

$g.Dispose()
$sheet.Save("$outDirWorking\Mic - All 16 Variations Preview.png", [System.Drawing.Imaging.ImageFormat]::Png)
$sheet.Save("$localOutDir\Mic - All 16 Variations Preview.png", [System.Drawing.Imaging.ImageFormat]::Png)
$sheet.Dispose()

foreach ($b in $generatedBitmaps) { $b.Dispose() }
Write-Output "Successfully generated all 16 refined Mic rarity variations (non-flashy, golden Legendary) strictly in Working Files!"
