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
# y=0..12: Grill / Mesh Capsule
# y=13..21: Handle / Body
# y=22..25: Clip / Connector
# y=26..53: Vertical Pole / Shaft
# y=54..59: Tripod Base & Shadow
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
                # y=0..12
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

# 16 Variation Palette & Feature Definitions
$variations = @(
    # --- COMMON (Tier 1) ---
    @{
        Id = 1; Rarity = "Common"; VarIdx = 1; RarityIdx = 1; Name = "Studio Standard"
        Mesh = @("201F26", "545156", "7B7276", "978A8A", "AEA19C")
        Handle = @("18181B", "2F2E37", "383740", "5A565B", "CBD5E1")
        Stand = @("18181B", "212128", "2F2E37", "3E3C45", "71717A")
        Accent = "CBD5E1"; Shockmount = $false; Wings = $false
    },
    @{
        Id = 2; Rarity = "Common"; VarIdx = 2; RarityIdx = 1; Name = "Matte Obsidian"
        Mesh = @("09090B", "18181B", "27272A", "3F3F46", "71717A")
        Handle = @("09090B", "18181B", "27272A", "3F3F46", "52525B")
        Stand = @("09090B", "18181B", "18181B", "27272A", "3F3F46")
        Accent = "52525B"; Shockmount = $false; Wings = $false
    },
    @{
        Id = 3; Rarity = "Common"; VarIdx = 3; RarityIdx = 1; Name = "Vintage Broadcast"
        Mesh = @("451A03", "78350F", "B45309", "D97706", "FEF08A")
        Handle = @("1E293B", "334155", "64748B", "94A3B8", "E2E8F0")
        Stand = @("1E293B", "334155", "475569", "64748B", "CBD5E1")
        Accent = "FBBF24"; Shockmount = $false; Wings = $false
    },
    @{
        Id = 4; Rarity = "Common"; VarIdx = 4; RarityIdx = 1; Name = "Warm Copper Ribbon"
        Mesh = @("431407", "7C2D12", "C2410C", "EA580C", "FDE68A")
        Handle = @("431407", "7C2D12", "9A3412", "C2410C", "FDBA74")
        Stand = @("1C1917", "292524", "44403C", "78716C", "D6D3D1")
        Accent = "FB923C"; Shockmount = $false; Wings = $false
    },

    # --- RARE (Tier 2) ---
    @{
        Id = 5; Rarity = "Rare"; VarIdx = 1; RarityIdx = 2; Name = "Electric Cyan LED"
        Mesh = @("083344", "0E7490", "06B6D4", "22D3EE", "A5F3FC")
        Handle = @("0F172A", "1E293B", "334155", "64748B", "E2E8F0")
        Stand = @("0F172A", "1E293B", "334155", "475569", "94A3B8")
        Accent = "00E5FF"; RingColor = "00E5FF"; Shockmount = $false; Wings = $false
    },
    @{
        Id = 6; Rarity = "Rare"; VarIdx = 2; RarityIdx = 2; Name = "Sapphire Ice"
        Mesh = @("082F49", "0369A1", "0284C7", "38BDF8", "E0F2FE")
        Handle = @("082F49", "075985", "0284C7", "38BDF8", "BAE6FD")
        Stand = @("0F172A", "1E293B", "334155", "64748B", "BAE6FD")
        Accent = "38BDF8"; RingColor = "E0F2FE"; Shockmount = $false; Wings = $false
    },
    @{
        Id = 7; Rarity = "Rare"; VarIdx = 3; RarityIdx = 2; Name = "Emerald Matrix"
        Mesh = @("022C22", "065F46", "059669", "10B981", "A7F3D0")
        Handle = @("022C22", "064E3B", "047857", "10B981", "6EE7B7")
        Stand = @("0F172A", "1E293B", "064E3B", "047857", "6EE7B7")
        Accent = "10B981"; RingColor = "34D399"; Shockmount = $false; Wings = $false
    },
    @{
        Id = 8; Rarity = "Rare"; VarIdx = 4; RarityIdx = 2; Name = "Crystal Pearl"
        Mesh = @("3B0764", "6B21A8", "9333EA", "C084FC", "FAF5FF")
        Handle = @("334155", "64748B", "94A3B8", "E2E8F0", "FFFFFF")
        Stand = @("1E293B", "334155", "64748B", "CBD5E1", "F8FAFC")
        Accent = "C084FC"; RingColor = "FFFFFF"; Shockmount = $false; Wings = $false
    },

    # --- EPIC (Tier 3) ---
    @{
        Id = 9; Rarity = "Epic"; VarIdx = 1; RarityIdx = 3; Name = "Royal Baroque Gold"
        Mesh = @("451A03", "78350F", "B45309", "F59E0B", "FEF08A")
        Handle = @("3B0764", "581C87", "7E22CE", "9333EA", "E9D5FF")
        Stand = @("451A03", "78350F", "B45309", "F59E0B", "FEF08A")
        Accent = "FBBF24"; ShockColor = "FBBF24"; BandColor = "9333EA"; Shockmount = $true; Wings = $false
    },
    @{
        Id = 10; Rarity = "Epic"; VarIdx = 2; RarityIdx = 3; Name = "Crimson Emperor"
        Mesh = @("450A0A", "7F1D1D", "B91C1C", "EF4444", "FEE2E2")
        Handle = @("450A0A", "7F1D1D", "991B1B", "DC2626", "FCA5A5")
        Stand = @("451A03", "78350F", "B45309", "F59E0B", "FEF08A")
        Accent = "EF4444"; ShockColor = "FBBF24"; BandColor = "DC2626"; Shockmount = $true; Wings = $false
    },
    @{
        Id = 11; Rarity = "Epic"; VarIdx = 3; RarityIdx = 3; Name = "Arcane Runes"
        Mesh = @("2E1065", "4C1D95", "6D28D9", "8B5CF6", "EDE9FE")
        Handle = @("09090B", "18181B", "27272A", "3F3F46", "8B5CF6")
        Stand = @("09090B", "18181B", "27272A", "4C1D95", "C084FC")
        Accent = "A855F7"; ShockColor = "C084FC"; BandColor = "4C1D95"; Shockmount = $true; Wings = $false
    },
    @{
        Id = 12; Rarity = "Epic"; VarIdx = 4; RarityIdx = 3; Name = "Mythic Rose Gold"
        Mesh = @("4C0519", "881337", "BE123C", "E11D48", "FFE4E6")
        Handle = @("4C0519", "881337", "9F1239", "FB7185", "FECDD3")
        Stand = @("4C0519", "881337", "BE123C", "FB7185", "FFE4E6")
        Accent = "FB7185"; ShockColor = "FB7185"; BandColor = "FFFFFF"; Shockmount = $true; Wings = $false
    },

    # --- LEGENDARY (Tier 4) ---
    @{
        Id = 13; Rarity = "Legendary"; VarIdx = 1; RarityIdx = 4; Name = "Celestial Seraphim"
        Mesh = @("451A03", "78350F", "B45309", "F59E0B", "FEF08A")
        Handle = @("451A03", "78350F", "B45309", "F59E0B", "FFFBEB")
        Stand = @("451A03", "78350F", "B45309", "F59E0B", "FEF08A")
        Accent = "FEF08A"; ShockColor = "FBBF24"; BandColor = "EF4444"; Shockmount = $true; Wings = $true
        WingType = "SERAPHIM"
    },
    @{
        Id = 14; Rarity = "Legendary"; VarIdx = 2; RarityIdx = 4; Name = "Solar Phoenix"
        Mesh = @("450A0A", "7F1D1D", "C2410C", "F97316", "FEF08A")
        Handle = @("450A0A", "7F1D1D", "C2410C", "EA580C", "FDE047")
        Stand = @("450A0A", "7F1D1D", "991B1B", "EA580C", "FDE047")
        Accent = "F97316"; ShockColor = "F97316"; BandColor = "FEF08A"; Shockmount = $true; Wings = $true
        WingType = "PHOENIX"
    },
    @{
        Id = 15; Rarity = "Legendary"; VarIdx = 3; RarityIdx = 4; Name = "Archangel Astral"
        Mesh = @("082F49", "0369A1", "0284C7", "38BDF8", "E0F2FE")
        Handle = @("082F49", "075985", "0284C7", "38BDF8", "FFFFFF")
        Stand = @("082F49", "0369A1", "0284C7", "38BDF8", "E0F2FE")
        Accent = "00E5FF"; ShockColor = "38BDF8"; BandColor = "FEF08A"; Shockmount = $true; Wings = $true
        WingType = "ARCHANGEL"
    },
    @{
        Id = 16; Rarity = "Legendary"; VarIdx = 4; RarityIdx = 4; Name = "Imperial Dragon"
        Mesh = @("022C22", "064E3B", "047857", "10B981", "A7F3D0")
        Handle = @("451A03", "78350F", "B45309", "D97706", "FEF08A")
        Stand = @("022C22", "064E3B", "047857", "10B981", "6EE7B7")
        Accent = "10B981"; ShockColor = "F59E0B"; BandColor = "10B981"; Shockmount = $true; Wings = $true
        WingType = "DRAGON"
    }
)

function SetPx([System.Drawing.Bitmap]$b, [int]$x, [int]$y, [string]$hex, [int]$a = 255) {
    if ($x -ge 0 -and $x -lt $b.Width -and $y -ge 0 -and $y -lt $b.Height) {
        $b.SetPixel($x, $y, (Hex $hex $a))
    }
}

function Draw-Sparkle([System.Drawing.Bitmap]$b, [int]$cx, [int]$cy, [string]$coreHex, [string]$glowHex) {
    SetPx $b $cx $cy $coreHex 255
    SetPx $b ($cx - 1) $cy $glowHex 200
    SetPx $b ($cx + 1) $cy $glowHex 200
    SetPx $b $cx ($cy - 1) $glowHex 200
    SetPx $b $cx ($cy + 1) $glowHex 200
}

function Draw-Shockmount([System.Drawing.Bitmap]$b, [string]$ringHex, [string]$bandHex, [int]$cx, [int]$cy) {
    # Outer circular suspension ring (radius ~7 px around capsule mount)
    $ringCoords = @(
        @(-6, -3), @(-7, -2), @(-7, -1), @(-7, 0), @(-7, 1), @(-7, 2), @(-6, 3),
        @( 6, -3), @( 7, -2), @( 7, -1), @( 7, 0), @( 7, 1), @( 7, 2), @( 6, 3),
        @(-5, -5), @(-4, -6), @(-3, -6), @( 3, -6), @( 4, -6), @( 5, -5),
        @(-5,  5), @(-4,  6), @(-3,  6), @( 3,  6), @( 4,  6), @( 5,  5)
    )
    foreach ($pt in $ringCoords) {
        SetPx $b ($cx + $pt[0]) ($cy + $pt[1]) $ringHex
    }

    # Cross-elastic bands linking capsule to ring
    SetPx $b ($cx - 4) ($cy - 2) $bandHex
    SetPx $b ($cx - 3) ($cy - 1) $bandHex
    SetPx $b ($cx + 3) ($cy - 1) $bandHex
    SetPx $b ($cx + 4) ($cy - 2) $bandHex
    SetPx $b ($cx - 4) ($cy + 2) $bandHex
    SetPx $b ($cx - 3) ($cy + 1) $bandHex
    SetPx $b ($cx + 3) ($cy + 1) $bandHex
    SetPx $b ($cx + 4) ($cy + 2) $bandHex
}

function Draw-AngelicWings([System.Drawing.Bitmap]$b, [string]$darkHex, [string]$midHex, [string]$lightHex, [string]$hiHex, [string]$gemHex) {
    # Sweeping 3-feather winged brackets emerging from shockmount (y=16..26)
    # Left Wing (x=1..11, y=14..26)
    $lWing = @(
        # Upper sweeping primary feather
        @(9, 16, $darkHex), @(8, 15, $darkHex), @(7, 14, $midHex), @(6, 13, $midHex), @(5, 12, $lightHex), @(4, 11, $hiHex), @(3, 11, $hiHex),
        @(8, 16, $midHex),  @(7, 15, $lightHex),@(6, 14, $hiHex),  @(5, 13, $hiHex),   @(4, 12, $lightHex),
        # Middle secondary feather
        @(9, 18, $darkHex), @(7, 18, $darkHex), @(5, 17, $midHex), @(3, 16, $lightHex), @(2, 16, $hiHex),
        @(8, 19, $midHex),  @(6, 18, $lightHex),@(4, 17, $hiHex),
        # Lower feather
        @(9, 21, $darkHex), @(7, 21, $midHex),  @(5, 20, $lightHex),@(3, 20, $hiHex),
        @(8, 22, $darkHex), @(6, 22, $midHex),  @(4, 21, $lightHex)
    )
    foreach ($p in $lWing) { SetPx $b $p[0] $p[1] $p[2] }

    # Right Wing (x=20..30, symmetric mirror: rx = 31 - lx)
    foreach ($p in $lWing) {
        $rx = 31 - $p[0]
        SetPx $b $rx $p[1] $p[2]
    }

    # Center Heart Core Gem at shockmount joint (x=15..16, y=17..18)
    SetPx $b 15 17 $gemHex
    SetPx $b 16 17 $hiHex
    SetPx $b 15 18 $darkHex
    SetPx $b 16 18 $gemHex
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
                $adjLum = [Math]::Min(1.0, $lum * 1.1)
                $r = $v.Handle
                $c = PaletteRamp $r[0] $r[1] $r[2] $r[3] $r[4] $adjLum $a
            }
            "CLIP" {
                $c = Hex $v.Accent $a
            }
            "STAND_SHAFT" {
                $adjLum = [Math]::Min(1.0, $lum * 1.0)
                $r = $v.Stand
                $c = PaletteRamp $r[0] $r[1] $r[2] $r[3] $r[4] $adjLum $a
            }
            "BASE" {
                $adjLum = [Math]::Min(1.0, $lum * 1.0)
                $r = $v.Stand
                $c = PaletteRamp $r[0] $r[1] $r[2] $r[3] $r[4] $adjLum $a
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

    # Rare Features: Glowing LED ring or frost/matrix glints
    if ($v.RarityIdx -eq 2) {
        # LED indicator ring at capsule collar (x=13..18, y=11..12)
        for ($rx = 13; $rx -le 18; $rx++) {
            SetPx $bmp $rx 11 $v.RingColor
            SetPx $bmp $rx 12 $v.Accent
        }
        if ($v.VarIdx -eq 1) {
            # Cyan equalizing pulses
            SetPx $bmp 15 28 "00E5FF"
            SetPx $bmp 16 34 "00E5FF"
            SetPx $bmp 15 40 "00E5FF"
        }
        elseif ($v.VarIdx -eq 2) {
            # Ice crystal sparkle
            Draw-Sparkle $bmp 18 6 "FFFFFF" "38BDF8"
        }
        elseif ($v.VarIdx -eq 3) {
            # Cyber matrix nodes
            SetPx $bmp 15 27 "34D399"
            SetPx $bmp 16 35 "10B981"
            SetPx $bmp 15 43 "34D399"
        }
        elseif ($v.VarIdx -eq 4) {
            # Diamond glints
            Draw-Sparkle $bmp 17 5 "FFFFFF" "C084FC"
            Draw-Sparkle $bmp 14 8 "FFFFFF" "E2E8F0"
        }
    }

    # Epic Features: Ornate suspension shockmount ring
    if ($v.Shockmount) {
        Draw-Shockmount $bmp $v.ShockColor $v.BandColor 15 17
        # Crown rim atop capsule (x=14..17, y=1)
        SetPx $bmp 14 1 $v.Accent
        SetPx $bmp 15 0 "FFFFFF"
        SetPx $bmp 16 0 "FFFFFF"
        SetPx $bmp 17 1 $v.Accent

        if ($v.VarIdx -eq 3) {
            # Arcane violet glints
            Draw-Sparkle $bmp 8 16 "FFFFFF" "C084FC"
            Draw-Sparkle $bmp 23 16 "FFFFFF" "C084FC"
        }
    }

    # Legendary Features: Sweeping Wings + Halo Crown + Holy Sparkles
    if ($v.Wings) {
        Draw-Shockmount $bmp $v.ShockColor $v.BandColor 15 17

        if ($v.WingType -eq "SERAPHIM") {
            Draw-AngelicWings $bmp "78350F" "B45309" "F59E0B" "FEF08A" "EF4444"
            # Golden Halo above capsule (x=13..18, y= -1..0)
            for ($hx = 13; $hx -le 18; $hx++) { SetPx $bmp $hx 0 "FEF08A" }
            SetPx $bmp 15 0 "FFFFFF"; SetPx $bmp 16 0 "FFFFFF"
            Draw-Sparkle $bmp 6 10 "FFFFFF" "FEF08A"
            Draw-Sparkle $bmp 25 10 "FFFFFF" "FEF08A"
            Draw-Sparkle $bmp 4 22 "FFFFFF" "FDE047"
            Draw-Sparkle $bmp 27 22 "FFFFFF" "FDE047"
        }
        elseif ($v.WingType -eq "PHOENIX") {
            Draw-AngelicWings $bmp "7F1D1D" "C2410C" "F97316" "FDE047" "EF4444"
            # Solar Phoenix Flame Crown
            SetPx $bmp 15 0 "FFFFFF"; SetPx $bmp 16 0 "FFFFFF"
            SetPx $bmp 14 1 "EA580C"; SetPx $bmp 17 1 "EA580C"
            Draw-Sparkle $bmp 5 11 "FFFFFF" "F97316"
            Draw-Sparkle $bmp 26 11 "FFFFFF" "F97316"
            Draw-Sparkle $bmp 3 23 "FEF08A" "EF4444"
            Draw-Sparkle $bmp 28 23 "FEF08A" "EF4444"
        }
        elseif ($v.WingType -eq "ARCHANGEL") {
            Draw-AngelicWings $bmp "0369A1" "0284C7" "38BDF8" "BAE6FD" "00E5FF"
            # Astral Starlight Halo
            for ($hx = 13; $hx -le 18; $hx++) { SetPx $bmp $hx 0 "38BDF8" }
            SetPx $bmp 15 0 "FFFFFF"; SetPx $bmp 16 0 "FFFFFF"
            Draw-Sparkle $bmp 5 10 "FFFFFF" "00E5FF"
            Draw-Sparkle $bmp 26 10 "FFFFFF" "00E5FF"
            Draw-Sparkle $bmp 4 21 "FFFFFF" "38BDF8"
            Draw-Sparkle $bmp 27 21 "FFFFFF" "38BDF8"
        }
        elseif ($v.WingType -eq "DRAGON") {
            Draw-AngelicWings $bmp "064E3B" "047857" "10B981" "6EE7B7" "F59E0B"
            # Horned Golden Dragon Crest
            SetPx $bmp 13 0 "F59E0B"; SetPx $bmp 18 0 "F59E0B"
            SetPx $bmp 14 1 "FEF08A"; SetPx $bmp 17 1 "FEF08A"
            SetPx $bmp 15 0 "10B981"; SetPx $bmp 16 0 "6EE7B7"
            Draw-Sparkle $bmp 5 11 "FFFFFF" "10B981"
            Draw-Sparkle $bmp 26 11 "FFFFFF" "10B981"
            Draw-Sparkle $bmp 3 22 "FEF08A" "6EE7B7"
            Draw-Sparkle $bmp 28 22 "FEF08A" "6EE7B7"
        }
    }

    $id = $v.Id
    $bmp.Save("$localOutDir\Mic_Var$id.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Save("$outDirWorking\Mic - Variation $id ($($v.Rarity) $($v.VarIdx)).png", [System.Drawing.Imaging.ImageFormat]::Png)

    # Save representative tier PNGs (Var 1 = Common, Var 5 = Rare, Var 9 = Epic, Var 13 = Legendary)
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
Write-Output "Successfully generated all 16 Mic rarity variations and preview sheets strictly in Working Files!"
