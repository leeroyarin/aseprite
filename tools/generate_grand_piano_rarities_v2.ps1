Add-Type -AssemblyName System.Drawing

$origW = 100
$origH = 70
$W = 108
$H = 80
$offX = 4
$offY = 9

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

$gpFlat = [System.Drawing.Bitmap]::FromFile("C:\Users\leero\Source\Repos\aseprite2\tools\gp_flat.png")
$b1 = [System.Drawing.Bitmap]::FromFile("C:\Users\leero\Source\Repos\aseprite2\tools\gp_layer_1_Layer_56.png")
$b2 = [System.Drawing.Bitmap]::FromFile("C:\Users\leero\Source\Repos\aseprite2\tools\gp_layer_2_Grand_Piano.png")
$b4 = [System.Drawing.Bitmap]::FromFile("C:\Users\leero\Source\Repos\aseprite2\tools\gp_layer_4_Layer_61.png")
$b6 = [System.Drawing.Bitmap]::FromFile("C:\Users\leero\Source\Repos\aseprite2\tools\gp_layer_6_Layer_60.png")

# Helper to compute lid lower boundary Y for a given original X (43..94)
function Get-LidBottomY([int]$ox) {
    if ($ox -lt 43) { return 43 }
    if ($ox -le 54) { return 18 }
    if ($ox -le 56) { return 17 }
    if ($ox -le 58) { return 16 }
    if ($ox -le 61) { return 15 }
    if ($ox -le 66) { return 14 }
    if ($ox -le 75) { return 13 }
    if ($ox -le 85) { return 12 }
    if ($ox -le 90) { return 11 }
    return 10
}

# Pre-classify all pixels from original 100x70 canvas
$pixelData = @()
for ($oy = 0; $oy -lt $origH; $oy++) {
    for ($ox = 0; $ox -lt $origW; $ox++) {
        $cf = $gpFlat.GetPixel($ox, $oy)
        if ($cf.A -eq 0) { continue }

        $c1 = $b1.GetPixel($ox, $oy)
        $c2 = $b2.GetPixel($ox, $oy)
        $c4 = $b4.GetPixel($ox, $oy)
        $c6 = $b6.GetPixel($ox, $oy)

        $lum = ($cf.R * 0.299 + $cf.G * 0.587 + $cf.B * 0.114) / 255.0
        $warmth = ($cf.R - $cf.B)

        $zone = "RIM"
        if ($c2.A -eq 0) {
            $zone = "SHADOW"
        }
        elseif ($ox -le 20) {
            if ($oy -ge 31 -and $oy -le 44 -and $ox -ge 8 -and $ox -le 18) {
                if ($c6.A -gt 90 -and $lum -gt 0.45) { $zone = "BENCH_TUFT" }
                else { $zone = "BENCH_CUSHION" }
            }
            elseif ($oy -ge 45 -and $oy -le 46 -and $ox -ge 8 -and $ox -le 18 -and $lum -gt 0.4) {
                $zone = "BENCH_TRIM"
            }
            else {
                $zone = "BENCH_FRAME"
            }
        }
        elseif ($ox -ge 25 -and $ox -le 27 -and $oy -ge 14 -and $oy -le 45) {
            if ($lum -gt 0.55) { $zone = "KEY_WHITE" }
            else { $zone = "KEY_BLACK" }
        }
        elseif ($ox -ge 35 -and $ox -le 44 -and $oy -ge 48 -and $oy -le 51 -and $lum -gt 0.16) {
            $zone = "PLAQUE"
        }
        elseif (($oy -ge 66 -and $ox -ge 36 -and $ox -le 41) -or ($oy -ge 40 -and $oy -le 42 -and $ox -ge 87 -and $ox -le 92)) {
            $zone = "CASTER"
        }
        elseif (($oy -ge 54 -and $ox -ge 36 -and $ox -le 43) -or ($oy -ge 35 -and $oy -le 40 -and $ox -ge 88 -and $ox -le 92)) {
            $zone = "LEG"
        }
        elseif ($ox -ge 48 -and $ox -le 49 -and $oy -ge 18 -and $oy -le 43) {
            $zone = "PROP_STICK"
        }
        elseif ($ox -ge 33 -and $ox -le 34 -and $oy -ge 12 -and $oy -le 36 -and $warmth -ge 8) {
            $zone = "DESK_INLAY"
        }
        elseif ($ox -ge 39 -and $ox -le 42 -and $oy -ge 13 -and $oy -le 38 -and ($warmth -ge 8 -or $lum -lt 0.15)) {
            $zone = "DESK_LEDGE"
        }
        elseif ($ox -le 42 -and $oy -le 43) {
            $zone = "CASE_TOP"
        }
        elseif ($ox -ge 43) {
            $lidBot = Get-LidBottomY $ox
            if ($oy -le $lidBot) {
                if ($ox -ge 54 -and $ox -le 57) { $zone = "LID_HINGE" }
                elseif ($ox -lt 54) { $zone = "LID_FLAP" }
                else { $zone = "LID_MAIN" }
            }
            elseif ($ox -ge 44 -and $ox -le 46 -and $oy -ge 16 -and $oy -le 43) {
                $zone = "PINBLOCK"
            }
            elseif ($ox -ge 47 -and $ox -le 50 -and $oy -ge 40 -and $oy -le 44) {
                $zone = "BRIDGE_STUD"
            }
            elseif ($lum -gt 0.32 -and $ox -ge 48 -and $ox -le 92 -and $oy -ge 15 -and $oy -le 44) {
                $zone = "STRINGS"
            }
            elseif ($ox -ge 45 -and $ox -le 94 -and $oy -ge 14 -and $oy -le 46) {
                $zone = "SOUNDBOARD"
            }
            else {
                $zone = "RIM"
            }
        }
        else {
            $zone = "RIM"
        }

        $pixelData += [pscustomobject]@{
            X = $ox + $offX
            Y = $oy + $offY
            OX = $ox
            OY = $oy
            Lum = $lum
            A = $cf.A
            R = $cf.R
            G = $cf.G
            B = $cf.B
            Zone = $zone
        }
    }
}

$gpFlat.Dispose(); $b1.Dispose(); $b2.Dispose(); $b4.Dispose(); $b6.Dispose()

# v2 variation specs - grounded tier rules (see Pitch Quest Instrument Rarity brief)
# Common: clean baseline studio materials (ebony, walnut, maple, slate) - no trim, no extras
# Rare:   polished lacquer finish + 1px accent trim (cobalt, alpine white, racing green, crimson)
# Epic:   structural upgrade - contrasting apron band + jewel studs + gem crest + banded pedals
# Legendary: prominent 24K gold hardware across 4 colour themes
$Spruce = @("3B2A14", "6B4A24", "A07A3E", "D4AE6A", "F2DCA0")
$SpruceStr = @("6B4A24", "A07A3E", "D4AE6A", "F2DCA0", "FFF6DC")
$variations = @(
    # ================= COMMON =================
    @{
        Id = 1; Rarity = "Common"; RarityIdx = 1; VarIdx = 1; Name = "Common 1 - Classic Concert Ebony"
        IsOriginal = $true
    },
    @{
        Id = 2; Rarity = "Common"; RarityIdx = 1; VarIdx = 2; Name = "Common 2 - Satin Walnut"
        Lid = @("1C120B", "33200F", "4F3318", "6E4A26", "8F6637")
        Rim = @("120B06", "1C120B", "33200F", "4F3318", "6E4A26")
        RimHi = "8F6637"
        Soundboard = @("2A1C0E", "4A3318", "7A5A30", "A8824A", "D1AD73")
        Strings = $SpruceStr
        Pinblock = "D1AD73"; PropStick = "A8824A"; Studs = "A8824A"
        Plaque = @("33200F", "6E4A26", "D1AD73"); Caster = "8F6637"
        KeyW = @("EDE9E3", "FAF8F4"); KeyB = @("1C1917", "3A3532")
        BenchCushion = @("1C120B", "33200F", "4F3318", "6E4A26", "8F6637")
        BenchTuft = "A8824A"; BenchTrim = "6E4A26"
        ShadowRgb = @(20, 13, 8)
        WoodGrain = $true
    },
    @{
        Id = 3; Rarity = "Common"; RarityIdx = 1; VarIdx = 3; Name = "Common 3 - Natural Maple"
        Lid = @("5A4128", "866238", "B48B54", "D6B27A", "ECD3A4")
        Rim = @("3E2C18", "5A4128", "866238", "A67D48", "C79F66")
        RimHi = "ECD3A4"
        Soundboard = @("5A4128", "866238", "B48B54", "D6B27A", "F0DDB5")
        Strings = $SpruceStr
        Pinblock = "F0DDB5"; PropStick = "D6B27A"; Studs = "D6B27A"
        Plaque = @("5A4128", "A67D48", "F0DDB5"); Caster = "A67D48"
        KeyW = @("F5F1EA", "FFFFFF"); KeyB = @("2B2622", "4A433D")
        BenchCushion = @("5A4128", "866238", "A67D48", "C79F66", "D6B27A")
        BenchTuft = "ECD3A4"; BenchTrim = "A67D48"
        ShadowRgb = @(30, 22, 14)
        WoodGrain = $true
    },
    @{
        Id = 4; Rarity = "Common"; RarityIdx = 1; VarIdx = 4; Name = "Common 4 - Studio Slate"
        Lid = @("0F172A", "1E293B", "334155", "475569", "64748B")
        Rim = @("020617", "0F172A", "1E293B", "334155", "475569")
        RimHi = "94A3B8"
        Soundboard = @("1E293B", "334155", "475569", "64748B", "94A3B8")
        Strings = @("64748B", "94A3B8", "CBD5E1", "E2E8F0", "FFFFFF")
        Pinblock = "E2E8F0"; PropStick = "94A3B8"; Studs = "CBD5E1"
        Plaque = @("1E293B", "64748B", "CBD5E1"); Caster = "94A3B8"
        KeyW = @("F1F5F9", "FFFFFF"); KeyB = @("0F172A", "1E293B")
        BenchCushion = @("0F172A", "1E293B", "334155", "475569", "64748B")
        BenchTuft = "94A3B8"; BenchTrim = "64748B"
        ShadowRgb = @(12, 16, 26)
    },

    # ================= RARE : polished lacquer + 1px accent trim =================
    @{
        Id = 5; Rarity = "Rare"; RarityIdx = 2; VarIdx = 1; Name = "Rare 1 - Cobalt Lacquer, Silver Trim"
        Lid = @("0A1A4D", "11307F", "1B4DBF", "2F6FE8", "7FAEFF")
        Rim = @("050E2E", "0A1A4D", "11307F", "1B4DBF", "2F6FE8")
        RimHi = "D5DEEA"; TrimHex = "D5DEEA"
        Soundboard = $Spruce; Strings = $SpruceStr
        Pinblock = "F2DCA0"; PropStick = "D5DEEA"; Studs = "D5DEEA"
        Plaque = @("0A1A4D", "2F6FE8", "D5DEEA"); Caster = "D5DEEA"
        KeyW = @("F5F5F4", "FFFFFF"); KeyB = @("0B1226", "1C2540")
        BenchCushion = @("0A1A4D", "11307F", "1B4DBF", "2F6FE8", "4F8CF5")
        BenchTuft = "D5DEEA"; BenchTrim = "D5DEEA"
        ShadowRgb = @(8, 14, 38)
    },
    @{
        Id = 6; Rarity = "Rare"; RarityIdx = 2; VarIdx = 2; Name = "Rare 2 - Alpine White Lacquer, Champagne Trim"
        Lid = @("7C8799", "A3AEBF", "CCD5E1", "EAEFF6", "FFFFFF")
        Rim = @("5B6677", "7C8799", "A3AEBF", "CCD5E1", "EAEFF6")
        RimHi = "D9B95A"; TrimHex = "D9B95A"
        Soundboard = $Spruce; Strings = $SpruceStr
        Pinblock = "FFF6DC"; PropStick = "D9B95A"; Studs = "D9B95A"
        Plaque = @("5B6677", "A3AEBF", "D9B95A"); Caster = "D9B95A"
        KeyW = @("FFFFFF", "FFFFFF"); KeyB = @("1E293B", "334155")
        BenchCushion = @("7C8799", "A3AEBF", "CCD5E1", "EAEFF6", "FFFFFF")
        BenchTuft = "D9B95A"; BenchTrim = "D9B95A"
        ShadowRgb = @(26, 32, 44)
    },
    @{
        Id = 7; Rarity = "Rare"; RarityIdx = 2; VarIdx = 3; Name = "Rare 3 - Racing Green Lacquer, Brass Trim"
        Lid = @("04200F", "0A3A1D", "11602F", "1C8A45", "4CC47A")
        Rim = @("021408", "04200F", "0A3A1D", "11602F", "1C8A45")
        RimHi = "D9A63A"; TrimHex = "D9A63A"
        Soundboard = $Spruce; Strings = $SpruceStr
        Pinblock = "F2DCA0"; PropStick = "D9A63A"; Studs = "D9A63A"
        Plaque = @("04200F", "1C8A45", "D9A63A"); Caster = "D9A63A"
        KeyW = @("F5F5F0", "FFFFFF"); KeyB = @("04200F", "0A3A1D")
        BenchCushion = @("04200F", "0A3A1D", "11602F", "1C8A45", "2FA85C")
        BenchTuft = "D9A63A"; BenchTrim = "D9A63A"
        ShadowRgb = @(4, 22, 12)
    },
    @{
        Id = 8; Rarity = "Rare"; RarityIdx = 2; VarIdx = 4; Name = "Rare 4 - Crimson Lacquer, Chrome Trim"
        Lid = @("3A0509", "6B0A12", "A30F1C", "D21F2E", "FF6E78")
        Rim = @("220306", "3A0509", "6B0A12", "A30F1C", "D21F2E")
        RimHi = "E5E7EB"; TrimHex = "E5E7EB"
        Soundboard = $Spruce; Strings = $SpruceStr
        Pinblock = "F2DCA0"; PropStick = "E5E7EB"; Studs = "E5E7EB"
        Plaque = @("3A0509", "D21F2E", "E5E7EB"); Caster = "E5E7EB"
        KeyW = @("F5F5F4", "FFFFFF"); KeyB = @("1A0407", "300A10")
        BenchCushion = @("3A0509", "6B0A12", "A30F1C", "D21F2E", "EF4450")
        BenchTuft = "E5E7EB"; BenchTrim = "E5E7EB"
        ShadowRgb = @(34, 6, 10)
    },

    # ================= EPIC : apron band + jewel studs + gem crest =================
    @{
        Id = 9; Rarity = "Epic"; RarityIdx = 3; VarIdx = 1; Name = "Epic 1 - Amethyst Jewel Apron, Silver Band"
        Lid = @("1F0836", "35106A", "532092", "7835C4", "A870F0")
        Rim = @("14052A", "1F0836", "35106A", "532092", "7835C4")
        RimHi = "E2E8F0"
        Soundboard = $Spruce; Strings = $SpruceStr
        Pinblock = "F2DCA0"; PropStick = "CBD5E1"; Studs = "E2E8F0"
        Plaque = @("35106A", "7835C4", "E2E8F0"); Caster = "CBD5E1"
        KeyW = @("FAF5FF", "FFFFFF"); KeyB = @("1F0836", "35106A")
        BenchCushion = @("1F0836", "35106A", "532092", "7835C4", "A870F0")
        BenchTuft = "E2E8F0"; BenchTrim = "CBD5E1"
        ShadowRgb = @(20, 8, 34)
        BandLo = "94A3B8"; BandHi = "E2E8F0"; Gem = "C084FC"; GemHi = "F5D0FE"
    },
    @{
        Id = 10; Rarity = "Epic"; RarityIdx = 3; VarIdx = 2; Name = "Epic 2 - Obsidian Ruby Apron, Bronze Band"
        Lid = @("0C0C10", "17171E", "262631", "3A3A48", "565668")
        Rim = @("08080B", "0C0C10", "17171E", "262631", "3A3A48")
        RimHi = "E0A060"
        Soundboard = $Spruce; Strings = $SpruceStr
        Pinblock = "F2DCA0"; PropStick = "CD7F32"; Studs = "E0A060"
        Plaque = @("17171E", "CD7F32", "F0C890"); Caster = "CD7F32"
        KeyW = @("F5F5F4", "FFFFFF"); KeyB = @("0C0C10", "17171E")
        BenchCushion = @("3A0509", "6B0A12", "A30F1C", "D21F2E", "EF4450")
        BenchTuft = "E0A060"; BenchTrim = "CD7F32"
        ShadowRgb = @(10, 10, 14)
        BandLo = "CD7F32"; BandHi = "F0C890"; Gem = "EF4444"; GemHi = "FECACA"
    },
    @{
        Id = 11; Rarity = "Epic"; RarityIdx = 3; VarIdx = 3; Name = "Epic 3 - Sapphire Jewel Apron, Platinum Band"
        Lid = @("081430", "10265C", "1A3F94", "2A5CC8", "5C92F5")
        Rim = @("050C1E", "081430", "10265C", "1A3F94", "2A5CC8")
        RimHi = "E8EEF5"
        Soundboard = $Spruce; Strings = $SpruceStr
        Pinblock = "F2DCA0"; PropStick = "CBD5E1"; Studs = "E8EEF5"
        Plaque = @("10265C", "2A5CC8", "E8EEF5"); Caster = "CBD5E1"
        KeyW = @("F0F9FF", "FFFFFF"); KeyB = @("081430", "10265C")
        BenchCushion = @("081430", "10265C", "1A3F94", "2A5CC8", "5C92F5")
        BenchTuft = "E8EEF5"; BenchTrim = "CBD5E1"
        ShadowRgb = @(6, 14, 34)
        BandLo = "A9B7C9"; BandHi = "F1F5F9"; Gem = "38BDF8"; GemHi = "E0F2FE"
    },
    @{
        Id = 12; Rarity = "Epic"; RarityIdx = 3; VarIdx = 4; Name = "Epic 4 - Emerald Jewel Apron, Copper Band"
        Lid = @("04221A", "0A3B2C", "125A43", "1C7F5F", "37B88C")
        Rim = @("03150F", "04221A", "0A3B2C", "125A43", "1C7F5F")
        RimHi = "E9A36A"
        Soundboard = $Spruce; Strings = $SpruceStr
        Pinblock = "F2DCA0"; PropStick = "B87333"; Studs = "E9A36A"
        Plaque = @("0A3B2C", "B87333", "F2C49A"); Caster = "B87333"
        KeyW = @("F0FDF4", "FFFFFF"); KeyB = @("04221A", "0A3B2C")
        BenchCushion = @("04221A", "0A3B2C", "125A43", "1C7F5F", "37B88C")
        BenchTuft = "E9A36A"; BenchTrim = "B87333"
        ShadowRgb = @(4, 22, 16)
        BandLo = "B87333"; BandHi = "F2C49A"; Gem = "34D399"; GemHi = "D1FAE5"
    },

    # ================= LEGENDARY : prominent 24K gold hardware =================
    @{
        Id = 13; Rarity = "Legendary"; RarityIdx = 4; VarIdx = 1; Name = "Legendary 1 - 24K Gold Sovereign"
        Lid = @("5B3A06", "8A5A0B", "B8860B", "DDAE1E", "F7D96B")
        Rim = @("1A1206", "30220A", "4D370D", "6E4F12", "93691A")
        RimHi = "FEF08A"
        Soundboard = @("0B0B0E", "17171E", "262631", "3A3A48", "565668")
        Strings = @("B8860B", "DDAE1E", "FBBF24", "FEF08A", "FFFBEB")
        Pinblock = "FEF08A"; PropStick = "FBBF24"; Studs = "FEF08A"
        Plaque = @("30220A", "FBBF24", "FEF08A"); Caster = "FBBF24"
        KeyW = @("FFFBEB", "FFFFFF"); KeyB = @("0B0B0E", "17171E")
        BenchCushion = @("1A1206", "30220A", "4D370D", "6E4F12", "93691A")
        BenchTuft = "FEF08A"; BenchTrim = "FBBF24"
        ShadowRgb = @(26, 18, 6)
        GoldHinge = $true
    },
    @{
        Id = 14; Rarity = "Legendary"; RarityIdx = 4; VarIdx = 2; Name = "Legendary 2 - Imperial Crimson & Gold"
        Lid = @("3E0508", "6E0A10", "A3101C", "D01F2D", "F2616D")
        Rim = @("260306", "3E0508", "6E0A10", "A3101C", "D01F2D")
        RimHi = "FEF08A"
        Soundboard = @("120405", "2A0A0C", "4A1015", "7A1A20", "B02A33")
        Strings = @("B8860B", "DDAE1E", "FBBF24", "FEF08A", "FFFBEB")
        Pinblock = "FEF08A"; PropStick = "FBBF24"; Studs = "FEF08A"
        Plaque = @("6E0A10", "FBBF24", "FEF08A"); Caster = "FBBF24"
        KeyW = @("FFFBEB", "FFFFFF"); KeyB = @("260306", "3E0508")
        BenchCushion = @("3E0508", "6E0A10", "A3101C", "D01F2D", "F2616D")
        BenchTuft = "FEF08A"; BenchTrim = "FBBF24"
        ShadowRgb = @(34, 6, 8)
        GoldHinge = $true
    },
    @{
        Id = 15; Rarity = "Legendary"; RarityIdx = 4; VarIdx = 3; Name = "Legendary 3 - Celestial Lapis & Gold"
        Lid = @("0A1240", "13207A", "1E3AA8", "2E56D6", "6B96F7")
        Rim = @("060B28", "0A1240", "13207A", "1E3AA8", "2E56D6")
        RimHi = "FEF08A"
        Soundboard = @("050B26", "0B1646", "14297A", "1E40AF", "3B6FE0")
        Strings = @("B8860B", "DDAE1E", "FBBF24", "FEF08A", "FFFBEB")
        Pinblock = "FEF08A"; PropStick = "FBBF24"; Studs = "FEF08A"
        Plaque = @("13207A", "FBBF24", "FEF08A"); Caster = "FBBF24"
        KeyW = @("FFFBEB", "FFFFFF"); KeyB = @("060B28", "0A1240")
        BenchCushion = @("0A1240", "13207A", "1E3AA8", "2E56D6", "6B96F7")
        BenchTuft = "FEF08A"; BenchTrim = "FBBF24"
        ShadowRgb = @(6, 10, 36)
        GoldHinge = $true
    },
    @{
        Id = 16; Rarity = "Legendary"; RarityIdx = 4; VarIdx = 4; Name = "Legendary 4 - Imperial Jade & Gold"
        Lid = @("04281C", "07412C", "0B6443", "11875C", "3CCB98")
        Rim = @("021A12", "04281C", "07412C", "0B6443", "11875C")
        RimHi = "FEF08A"
        Soundboard = @("032016", "064E3B", "047857", "059669", "34D399")
        Strings = @("B8860B", "DDAE1E", "FBBF24", "FEF08A", "FFFBEB")
        Pinblock = "FEF08A"; PropStick = "FBBF24"; Studs = "FEF08A"
        Plaque = @("07412C", "FBBF24", "FEF08A"); Caster = "FBBF24"
        KeyW = @("FFFBEB", "FFFFFF"); KeyB = @("021A12", "04281C")
        BenchCushion = @("04281C", "07412C", "0B6443", "11875C", "3CCB98")
        BenchTuft = "FEF08A"; BenchTrim = "FBBF24"
        ShadowRgb = @(4, 26, 18)
        GoldHinge = $true
    }
)

function SetPx($bmp, [int]$x, [int]$y, [string]$hex, [int]$a = 255) {
    if ($x -ge 0 -and $x -lt $bmp.Width -and $y -ge 0 -and $y -lt $bmp.Height) {
        $bmp.SetPixel($x, $y, (Hex $hex $a))
    }
}

# Helper to apply inlaid gold filigree & golden hardware for Legendary
function Apply-LegendaryGoldInlay($bmp) {
    # 1. Continuous Inlaid Gold Filigree Border along the S-Curve Rim
    for ($ox = 24; $ox -le 94; $ox++) {
        $botY = -1
        for ($oy = 53; $oy -ge 20; $oy--) {
            $pMatch = $bmp.GetPixel($ox + $offX, $oy + $offY)
            if ($pMatch.A -eq 255) { $botY = $oy; break }
        }
        if ($botY -gt 20) {
            # Inlaid gold border: 2-pixel fine gold strip with pinstripe highlights
            SetPx $bmp ($ox + $offX) ($botY + $offY) "D97706"     # Gold shadow
            SetPx $bmp ($ox + $offX) ($botY + $offY - 1) "FBBF24" # Pure gold
            SetPx $bmp ($ox + $offX) ($botY + $offY - 2) "FEF08A" # Polished gold highlight
            # Periodic rosette inlays every 6 pixels
            if (($ox % 6) -eq 0 -and $ox -ge 40) {
                SetPx $bmp ($ox + $offX) ($botY + $offY - 3) "FEF08A"
                SetPx $bmp ($ox + $offX) ($botY + $offY - 4) "F59E0B"
            }
        }
    }

    # 2. Inlaid Gold Border along the Grand Lid Edge
    for ($ox = 43; $ox -le 94; $ox++) {
        $ly = (Get-LidBottomY $ox) + $offY
        SetPx $bmp ($ox + $offX) $ly "D97706"
        SetPx $bmp ($ox + $offX) ($ly - 1) "FBBF24"
        if (($ox % 4) -eq 0) {
            SetPx $bmp ($ox + $offX) ($ly - 2) "FEF08A"
        }
    }

    # 3. Gold Inlaid Music Desk Crest & Lyre Border
    for ($oy = 13; $oy -le 37; $oy++) {
        SetPx $bmp (32 + $offX) ($oy + $offY) "FBBF24"
        SetPx $bmp (33 + $offX) ($oy + $offY) "FEF08A"
    }
    # Royal gold crown emblem at top of music desk (ox=33..37, oy=11..13)
    $crownPx = @(
        @(33,13,"B45309"), @(34,13,"FBBF24"), @(35,13,"FEF08A"), @(36,13,"FBBF24"), @(37,13,"B45309"),
        @(33,12,"FBBF24"), @(34,12,"FEF08A"), @(35,12,"FEF08A"), @(36,12,"FEF08A"), @(37,12,"FBBF24"),
        @(33,11,"FEF08A"), @(35,11,"FFFBEB"), @(37,11,"FEF08A")
    )
    foreach ($cp in $crownPx) { SetPx $bmp ($cp[0] + $offX) ($cp[1] + $offY) $cp[2] }

    # 4. Polished 24K Gold Pedals & Lyre Posts
    for ($dx = 38; $dx -le 42; $dx++) {
        SetPx $bmp ($dx + $offX) (59 + $offY) "FEF08A"
        SetPx $bmp ($dx + $offX) (60 + $offY) "FBBF24"
        SetPx $bmp ($dx + $offX) (61 + $offY) "D97706"
    }

    # 5. Polished Gold Leg Ferrules & Castors
    for ($dx = 37; $dx -le 42; $dx++) {
        SetPx $bmp ($dx + $offX) (66 + $offY) "FEF08A"
        SetPx $bmp ($dx + $offX) (67 + $offY) "FBBF24"
    }
    for ($dx = 88; $dx -le 92; $dx++) {
        SetPx $bmp ($dx + $offX) (40 + $offY) "FEF08A"
        SetPx $bmp ($dx + $offX) (41 + $offY) "FBBF24"
    }
}


# Find lowest opaque Y of the case rim at original X
function Get-RimBottomY($bmp, [int]$ox) {
    for ($oy = 53; $oy -ge 20; $oy--) {
        if ($bmp.GetPixel($ox + $offX, $oy + $offY).A -eq 255) { return $oy }
    }
    return -1
}

# Rare: single 1px trim line on lower rim and lid edge
function Apply-RareTrim($bmp, $trimHex) {
    for ($ox = 24; $ox -le 94; $ox++) {
        $botY = Get-RimBottomY $bmp $ox
        if ($botY -gt 20) { SetPx $bmp ($ox + $offX) ($botY + $offY - 1) $trimHex }
    }
    for ($ox = 43; $ox -le 94; $ox++) {
        SetPx $bmp ($ox + $offX) ((Get-LidBottomY $ox) + $offY) $trimHex
    }
}

# Epic: 2px metal apron band under the rim, jewel studs, gem crest on music desk, banded pedals
function Apply-EpicJewelApron($bmp, $bandLo, $bandHi, $gem, $gemHi) {
    for ($ox = 24; $ox -le 94; $ox++) {
        $botY = Get-RimBottomY $bmp $ox
        if ($botY -gt 20) {
            SetPx $bmp ($ox + $offX) ($botY + $offY) $bandLo
            SetPx $bmp ($ox + $offX) ($botY + $offY - 1) $bandHi
            if (($ox % 8) -eq 0 -and $ox -ge 40 -and $ox -le 90) {
                SetPx $bmp ($ox + $offX) ($botY + $offY - 2) $gem
                SetPx $bmp ($ox + $offX + 1) ($botY + $offY - 2) $gem
                SetPx $bmp ($ox + $offX) ($botY + $offY - 3) $gemHi
                SetPx $bmp ($ox + $offX + 1) ($botY + $offY - 3) $gem
            }
        }
    }
    # Lid edge molding
    for ($ox = 43; $ox -le 94; $ox++) {
        SetPx $bmp ($ox + $offX) ((Get-LidBottomY $ox) + $offY) $bandHi
    }
    # Gem crest on music desk (diamond)
    $cx = 35 + $offX; $cy = 12 + $offY
    SetPx $bmp $cx ($cy - 1) $gemHi
    SetPx $bmp ($cx - 1) $cy $gem; SetPx $bmp $cx $cy $gem; SetPx $bmp ($cx + 1) $cy $gem
    SetPx $bmp $cx ($cy + 1) $gem
    SetPx $bmp ($cx - 2) $cy $bandLo; SetPx $bmp ($cx + 2) $cy $bandLo
    # Banded pedals
    for ($dx = 38; $dx -le 42; $dx++) {
        SetPx $bmp ($dx + $offX) (59 + $offY) $bandHi
        SetPx $bmp ($dx + $offX) (60 + $offY) $bandLo
    }
}

$outDirWorking = "I:\Shared drives\Pitch Quest Shared Drive\Working Files\Music Store\Export\Instruments"
$localOutDir = "C:\Users\leero\Source\Repos\aseprite2\ref_instruments\grand_piano"
$layerDir = "C:\Users\leero\AppData\Local\Temp\claude\2975b516535c99c5\122b1d71-7e79-49b0-8cf3-6753e169f3b9\scratchpad\layers"
New-Item -ItemType Directory -Force -Path $layerDir | Out-Null
New-Item -ItemType Directory -Force -Path $localOutDir | Out-Null

$generatedBitmaps = @()

foreach ($v in $variations) {
    $swapBmp = $null
    $bmp = New-Object System.Drawing.Bitmap($W, $H)

    if ($v.IsOriginal) {
        foreach ($px in $pixelData) {
            $c = [System.Drawing.Color]::FromArgb($px.A, $px.R, $px.G, $px.B)
            $bmp.SetPixel($px.X, $px.Y, $c)
        }
    }
    else {
        foreach ($px in $pixelData) {
            $x = $px.X; $y = $px.Y
            $ox = $px.OX; $oy = $px.OY
            $lum = $px.Lum
            $a = $px.A

            switch ($px.Zone) {
                "SHADOW" {
                    $sr = $v.ShadowRgb[0]; $sg = $v.ShadowRgb[1]; $sb = $v.ShadowRgb[2]
                    $c = [System.Drawing.Color]::FromArgb($a, $sr, $sg, $sb)
                }
                "BENCH_CUSHION" {
                    $adjLum = [Math]::Min(1.0, $lum * 1.45)
                    $r = $v.BenchCushion
                    $c = PaletteRamp $r[0] $r[1] $r[2] $r[3] $r[4] $adjLum $a
                }
                "BENCH_TUFT" {
                    $c = Hex $v.BenchTuft $a
                }
                "BENCH_TRIM" {
                    $c = Hex $v.BenchTrim $a
                }
                "BENCH_FRAME" {
                    $adjLum = [Math]::Min(1.0, $lum * 2.2)
                    $r = $v.Rim
                    $c = PaletteRamp $r[0] $r[1] $r[2] $r[3] $r[4] $adjLum $a
                }
                "KEY_WHITE" {
                    $c = LerpColor (Hex $v.KeyW[0]) (Hex $v.KeyW[1]) $lum $a
                }
                "KEY_BLACK" {
                    $c = LerpColor (Hex $v.KeyB[0]) (Hex $v.KeyB[1]) ([Math]::Min(1.0, $lum * 2.5)) $a
                }
                "PLAQUE" {
                    if ($ox -eq 39 -or $ox -eq 40) { $c = Hex $v.Plaque[2] $a }
                    elseif ($oy -eq 48 -or $oy -eq 51 -or $ox -eq 35 -or $ox -eq 44) { $c = Hex $v.Plaque[1] $a }
                    else { $c = Hex $v.Plaque[0] $a }
                }
                "CASTER" {
                    $c = Hex $v.Caster $a
                }
                "LEG" {
                    $adjLum = [Math]::Min(1.0, $lum * 3.0 + ($(if ($ox -eq 38 -or $ox -eq 89) { 0.35 } else { 0.0 })))
                    $r = $v.Rim
                    $c = PaletteRamp $r[0] $r[1] $r[2] $r[3] $r[4] $adjLum $a
                }
                "PROP_STICK" {
                    $c = Hex $v.PropStick $a
                }
                "DESK_INLAY" {
                    $c = Hex $v.RimHi $a
                }
                "DESK_LEDGE" {
                    $c = Hex $v.Plaque[1] $a
                }
                "CASE_TOP" {
                    $adjLum = [Math]::Min(1.0, $lum * 1.65)
                    if ($v.WoodGrain -and (($oy % 3) -eq 0)) { $adjLum = [Math]::Min(1.0, $adjLum + 0.12) }
                    $r = $v.Lid
                    $c = PaletteRamp $r[0] $r[1] $r[2] $r[3] $r[4] $adjLum $a
                }
                "LID_HINGE" {
                    $adjLum = [Math]::Min(1.0, $lum * 2.1 + 0.2)
                    $r = $v.Lid
                    if ($v.GoldHinge) { $r = @("7C4A03", "B8860B", "DDAE1E", "FBBF24", "FEF08A") }
                    $c = PaletteRamp $r[0] $r[1] $r[2] $r[3] $r[4] $adjLum $a
                }
                "LID_FLAP" {
                    $adjLum = [Math]::Min(1.0, $lum * 1.75)
                    if ($v.WoodGrain -and (($oy % 3) -eq 0)) { $adjLum = [Math]::Min(1.0, $adjLum + 0.12) }
                    $r = $v.Lid
                    $c = PaletteRamp $r[0] $r[1] $r[2] $r[3] $r[4] $adjLum $a
                }
                "LID_MAIN" {
                    $adjLum = [Math]::Min(1.0, $lum * 1.85)
                    if ($v.WoodGrain -and ((($oy + [int]($ox / 8)) % 3) -eq 0)) { $adjLum = [Math]::Min(1.0, $adjLum + 0.14) }
                    $r = $v.Lid
                    $c = PaletteRamp $r[0] $r[1] $r[2] $r[3] $r[4] $adjLum $a
                }
                "PINBLOCK" {
                    $c = Hex $v.Pinblock $a
                }
                "BRIDGE_STUD" {
                    $c = Hex $v.Studs $a
                }
                "STRINGS" {
                    $adjLum = [Math]::Min(1.0, $lum * 2.2)
                    $r = $v.Strings
                    $c = PaletteRamp $r[0] $r[1] $r[2] $r[3] $r[4] $adjLum $a
                }
                "SOUNDBOARD" {
                    $adjLum = [Math]::Min(1.0, $lum * 2.1)
                    $r = $v.Soundboard
                    $c = PaletteRamp $r[0] $r[1] $r[2] $r[3] $r[4] $adjLum $a
                }
                "RIM_HIGHLIGHT" {
                    $c = Hex $v.RimHi $a
                }
                default {
                    $adjLum = [Math]::Min(1.0, $lum * 3.2)
                    $r = $v.Rim
                    $c = PaletteRamp $r[0] $r[1] $r[2] $r[3] $r[4] $adjLum $a
                }
            }
            $bmp.SetPixel($x, $y, $c)
        }


        $swapBmp = $bmp.Clone([System.Drawing.Rectangle]::new(0, 0, $W, $H), $bmp.PixelFormat)

        # Rare: 1px accent trim along lower rim + lid edge (polished lacquer finish)
        if ($v.RarityIdx -eq 2) {
            Apply-RareTrim $bmp $v.TrimHex
        }

        # Epic: structural upgrade - apron band, jewel studs, gem crest, banded pedals
        if ($v.RarityIdx -eq 3) {
            Apply-EpicJewelApron $bmp $v.BandLo $v.BandHi $v.Gem $v.GemHi
        }

        # Legendary: prominent 24K gold hardware
        if ($v.RarityIdx -eq 4) {
            Apply-LegendaryGoldInlay $bmp
        }
    }
    if ($null -eq $swapBmp) {
        $swapBmp = $bmp.Clone([System.Drawing.Rectangle]::new(0, 0, $W, $H), $bmp.PixelFormat)
    }
    $swapBmp.Save("$layerDir\swap_$($v.Id).png", [System.Drawing.Imaging.ImageFormat]::Png)
    $detBmp = New-Object System.Drawing.Bitmap($W, $H)
    $detCount = 0
    for ($sy = 0; $sy -lt $H; $sy++) {
        for ($sx = 0; $sx -lt $W; $sx++) {
            $cp = $bmp.GetPixel($sx, $sy); $sq = $swapBmp.GetPixel($sx, $sy)
            if ($cp.ToArgb() -ne $sq.ToArgb()) { $detBmp.SetPixel($sx, $sy, $cp); $detCount++ }
        }
    }
    if (Test-Path "$layerDir\detail_$($v.Id).png") { Remove-Item "$layerDir\detail_$($v.Id).png" }
    if ($detCount -gt 0) { $detBmp.Save("$layerDir\detail_$($v.Id).png", [System.Drawing.Imaging.ImageFormat]::Png) }
    $detBmp.Dispose(); $swapBmp.Dispose()

    $id = $v.Id
    $bmp.Save("$localOutDir\GrandPiano_Var$id.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Save("$outDirWorking\Grand Piano - Variation $id ($($v.Rarity) $($v.VarIdx)).png", [System.Drawing.Imaging.ImageFormat]::Png)

    if ($v.VarIdx -eq 1) {
        $rNum = $v.RarityIdx
        $bmp.Save("$outDirWorking\Grand Piano$rNum.png", [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Save("$localOutDir\Grand Piano$rNum.png", [System.Drawing.Imaging.ImageFormat]::Png)

        # 4x Nearest-Neighbor Preview
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
        $p4.Save("$outDirWorking\Grand Piano-Preview$rNum.png", [System.Drawing.Imaging.ImageFormat]::Png)
        $p4.Save("$localOutDir\Grand Piano-Preview$rNum.png", [System.Drawing.Imaging.ImageFormat]::Png)
        $p4.Dispose()
    }

    $generatedBitmaps += $bmp
}

# Build 3x Nearest-Neighbor 4x4 Master Grid Preview Sheet
$scale = 3
$cellW = $W * $scale
$cellH = $H * $scale
$pad = 16
$headerH = 36
$labelW = 150
$gridW = $labelW + 4 * $cellW + 5 * $pad
$gridH = $headerH + 4 * $cellH + 5 * $pad

$sheet = New-Object System.Drawing.Bitmap($gridW, $gridH)
$g = [System.Drawing.Graphics]::FromImage($sheet)
$g.Clear((Hex "14161F"))
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half

$fontTitle = New-Object System.Drawing.Font("Segoe UI", 11, [System.Drawing.FontStyle]::Bold)
$fontSub = New-Object System.Drawing.Font("Segoe UI", 9, [System.Drawing.FontStyle]::Bold)
$brushWhite = New-Object System.Drawing.SolidBrush((Hex "F8FAFC"))

$rarityHeaders = @(
    @{ Name = "COMMON"; Color = "94A3B8"; Bg = "1E2230" },
    @{ Name = "RARE"; Color = "38BDF8"; Bg = "112840" },
    @{ Name = "EPIC"; Color = "C084FC"; Bg = "281542" },
    @{ Name = "LEGENDARY"; Color = "FACC15"; Bg = "362410" }
)

for ($c = 0; $c -lt 4; $c++) {
    $cx = $labelW + $pad + $c * ($cellW + $pad)
    $g.DrawString("Variation $($c + 1)", $fontTitle, $brushWhite, ($cx + [int]($cellW / 2) - 42), 8)
}

for ($r = 0; $r -lt 4; $r++) {
    $ry = $headerH + $pad + $r * ($cellH + $pad)
    $rh = $rarityHeaders[$r]
    $brushR = New-Object System.Drawing.SolidBrush((Hex $rh.Color))
    $brushBg = New-Object System.Drawing.SolidBrush((Hex $rh.Bg))
    $penBorder = New-Object System.Drawing.Pen((Hex $rh.Color 100), 2)

    $g.DrawString($rh.Name, $fontTitle, $brushR, 14, ($ry + [int]($cellH / 2) - 18))
    $g.DrawString("Tier $($r + 1) (4 Vars)", $fontSub, $brushWhite, 14, ($ry + [int]($cellH / 2) + 4))

    # Per-rarity 4-variation strip
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
    $rStrip.Save("$outDirWorking\Grand Piano - $rName Variations Preview.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $rStrip.Save("$localOutDir\Grand Piano - $rName Variations Preview.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $rStrip.Dispose()
    $brushR.Dispose(); $brushBg.Dispose(); $penBorder.Dispose()
}

$g.Dispose()
$sheet.Save("$outDirWorking\Grand Piano - All 16 Variations Preview.png", [System.Drawing.Imaging.ImageFormat]::Png)
$sheet.Save("$localOutDir\Grand Piano - All 16 Variations Preview.png", [System.Drawing.Imaging.ImageFormat]::Png)
$sheet.Dispose()

foreach ($b in $generatedBitmaps) { $b.Dispose() }
Write-Host "Successfully generated all 16 Grand Piano rarity variations and preview sheets in Working Files!"
