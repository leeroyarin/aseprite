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
            elseif ($ox -ge 44 -and $ox -le 49 -and $oy -ge 18 -and $oy -le 39 -and $lum -gt 0.45) {
                $zone = "PINBLOCK"
            }
            elseif ($warmth -ge 7 -or ($oy -le 27 -and $ox -le 92 -and $lum -gt 0.09)) {
                # Distinguish bridge studs / bright strings from soundboard wood
                if ((($ox -eq 61 -or $ox -eq 62) -and ($oy -eq 29 -or $oy -eq 30)) -or
                    (($ox -eq 66 -or $ox -eq 67) -and ($oy -eq 28 -or $oy -eq 29)) -or
                    (($ox -eq 71 -or $ox -eq 72) -and ($oy -eq 27 -or $oy -eq 28)) -or
                    (($ox -eq 76 -or $ox -eq 77) -and ($oy -eq 26 -or $oy -eq 27)) -or
                    (($ox -eq 81 -or $ox -eq 82) -and ($oy -eq 25 -or $oy -eq 26)) -or
                    (($ox -eq 86 -or $ox -eq 87) -and ($oy -eq 24 -or $oy -eq 25))) {
                    $zone = "BRIDGE_STUD"
                }
                elseif ($lum -gt 0.24 -or ($c4.A -gt 40 -and (($ox + $oy) % 3 -eq 0))) {
                    $zone = "STRINGS"
                }
                else {
                    $zone = "SOUNDBOARD"
                }
            }
            else {
                if ($c6.A -gt 40) { $zone = "RIM_HIGHLIGHT" }
                else { $zone = "RIM" }
            }
        }
        if ($zone -eq "RIM" -and $c6.A -gt 40) {
            $zone = "RIM_HIGHLIGHT"
        }

        $pixelData += [PSCustomObject]@{
            OX = $ox; OY = $oy
            X = $ox + $offX; Y = $oy + $offY
            R = $cf.R; G = $cf.G; B = $cf.B; A = $cf.A
            Lum = $lum; Warmth = $warmth
            Zone = $zone
            C6A = $c6.A
        }
    }
}

$gpFlat.Dispose(); $b1.Dispose(); $b2.Dispose(); $b4.Dispose(); $b6.Dispose()

# Define the 16 variation specifications (4 per rarity)
$variations = @(
    # ================= COMMON (1 - 4) =================
    @{
        Id = 1; Rarity = "Common"; RarityIdx = 1; VarIdx = 1; Name = "Common 1 - Classic Concert Ebony"
        IsOriginal = $true
        ShadowRgb = @(15, 15, 22)
    },
    @{
        Id = 2; Rarity = "Common"; RarityIdx = 1; VarIdx = 2; Name = "Common 2 - Heritage Mahogany & Walnut"
        Lid = @("2A0E07", "451A03", "7C2D12", "9A3412", "C2410C")
        Rim = @("1A0804", "2A0E07", "451A03", "7C2D12", "B45309")
        RimHi = "D97706"
        Soundboard = @("291509", "451A03", "78350F", "B45309", "F59E0B")
        Strings = @("78350F", "B45309", "D97706", "FBBF24", "FEF08A")
        Pinblock = "FDE68A"; PropStick = "FDE68A"; Studs = "FBBF24"
        Plaque = @("78350F", "B45309", "F59E0B"); Caster = "D97706"
        KeyW = @("E7E5E4", "FAFAF9"); KeyB = @("1C1917", "44403C")
        BenchCushion = @("2A0E07", "451A03", "7C2D12", "9A3412", "B45309")
        BenchTuft = "FBBF24"; BenchTrim = "D97706"
        ShadowRgb = @(25, 12, 6)
        WoodGrain = $true
    },
    @{
        Id = 3; Rarity = "Common"; RarityIdx = 1; VarIdx = 3; Name = "Common 3 - Honey Oak & Natural Maple"
        Lid = @("451A03", "78350F", "B45309", "D97706", "F59E0B")
        Rim = @("291002", "451A03", "78350F", "92400E", "D97706")
        RimHi = "FBBF24"
        Soundboard = @("451A03", "78350F", "A16207", "CA8A04", "FDE047")
        Strings = @("713F12", "A16207", "EAB308", "FACC15", "FEF9C3")
        Pinblock = "FEF3C7"; PropStick = "FEF3C7"; Studs = "FDE047"
        Plaque = @("78350F", "D97706", "FDE047"); Caster = "F59E0B"
        KeyW = @("F5F5F4", "FFFFFF"); KeyB = @("292524", "57534E")
        BenchCushion = @("1C1917", "292524", "44403C", "57534E", "78716C")
        BenchTuft = "FDE68A"; BenchTrim = "F59E0B"
        ShadowRgb = @(28, 18, 8)
        WoodGrain = $true
    },
    @{
        Id = 4; Rarity = "Common"; RarityIdx = 1; VarIdx = 4; Name = "Common 4 - Studio Slate & Crimson Felt"
        Lid = @("0F172A", "1E293B", "334155", "475569", "64748B")
        Rim = @("020617", "0F172A", "1E293B", "334155", "475569")
        RimHi = "94A3B8"
        Soundboard = @("2A0A10", "450A0A", "7F1D1D", "991B1B", "DC2626")
        Strings = @("64748B", "94A3B8", "CBD5E1", "E2E8F0", "FFFFFF")
        Pinblock = "E2E8F0"; PropStick = "E2E8F0"; Studs = "EF4444"
        Plaque = @("334155", "94A3B8", "E2E8F0"); Caster = "94A3B8"
        KeyW = @("E2E8F0", "F8FAFC"); KeyB = @("0F172A", "334155")
        BenchCushion = @("450A0A", "7F1D1D", "991B1B", "B91C1C", "EF4444")
        BenchTuft = "E2E8F0"; BenchTrim = "94A3B8"
        ShadowRgb = @(12, 16, 28)
    },

    # ================= RARE (5 - 8) =================
    @{
        Id = 5; Rarity = "Rare"; RarityIdx = 2; VarIdx = 1; Name = "Rare 1 - Cobalt Platinum & Electric Cyan LED"
        Lid = @("334155", "475569", "94A3B8", "CBD5E1", "F8FAFC")
        Rim = @("0F172A", "1E293B", "334155", "64748B", "CBD5E1")
        RimHi = "00E5FF"
        Soundboard = @("082F49", "0C4A6E", "0369A1", "0284C7", "38BDF8")
        Strings = @("0284C7", "00B0FF", "00E5FF", "7DD3FC", "FFFFFF")
        Pinblock = "E0F2FE"; PropStick = "00E5FF"; Studs = "00E5FF"
        Plaque = @("0369A1", "00E5FF", "FFFFFF"); Caster = "38BDF8"
        KeyW = @("E0F2FE", "FFFFFF"); KeyB = @("0284C7", "00E5FF")
        BenchCushion = @("1E293B", "334155", "475569", "64748B", "94A3B8")
        BenchTuft = "00E5FF"; BenchTrim = "00E5FF"
        ShadowRgb = @(8, 28, 48)
        RareMode = "CYAN_LED"
    },
    @{
        Id = 6; Rarity = "Rare"; RarityIdx = 2; VarIdx = 2; Name = "Rare 2 - Sapphire Midnight & Ice-Blue Neon"
        Lid = @("0F172A", "1E3A8A", "1D4ED8", "2563EB", "60A5FA")
        Rim = @("020617", "0F172A", "172554", "1E3A8A", "3B82F6")
        RimHi = "7DD3FC"
        Soundboard = @("0F172A", "1E293B", "1E40AF", "0284C7", "38BDF8")
        Strings = @("38BDF8", "7DD3FC", "BAE6FD", "E0F2FE", "FFFFFF")
        Pinblock = "BAE6FD"; PropStick = "7DD3FC"; Studs = "38BDF8"
        Plaque = @("1E40AF", "38BDF8", "E0F2FE"); Caster = "60A5FA"
        KeyW = @("1E293B", "334155"); KeyB = @("00E5FF", "E0F2FE") # Reversed cyber keys
        BenchCushion = @("172554", "1E3A8A", "1D4ED8", "2563EB", "60A5FA")
        BenchTuft = "E0F2FE"; BenchTrim = "38BDF8"
        ShadowRgb = @(10, 20, 55)
        RareMode = "SAPPHIRE_ICE"
    },
    @{
        Id = 7; Rarity = "Rare"; RarityIdx = 2; VarIdx = 3; Name = "Rare 3 - Cyber Titanium & Emerald Matrix"
        Lid = @("1E293B", "334155", "475569", "94A3B8", "E2E8F0")
        Rim = @("022C22", "064E3B", "065F46", "047857", "10B981")
        RimHi = "34D399"
        Soundboard = @("022C22", "064E3B", "047857", "059669", "10B981")
        Strings = @("059669", "10B981", "34D399", "6EE7B7", "ECFDF5")
        Pinblock = "A7F3D0"; PropStick = "34D399"; Studs = "6EE7B7"
        Plaque = @("065F46", "10B981", "A7F3D0"); Caster = "10B981"
        KeyW = @("ECFDF5", "FFFFFF"); KeyB = @("064E3B", "10B981")
        BenchCushion = @("064E3B", "065F46", "047857", "059669", "34D399")
        BenchTuft = "A7F3D0"; BenchTrim = "34D399"
        ShadowRgb = @(6, 36, 28)
        RareMode = "EMERALD_MATRIX"
    },
    @{
        Id = 8; Rarity = "Rare"; RarityIdx = 2; VarIdx = 4; Name = "Rare 4 - Frosted Pearl White & Crystal Cyan"
        Lid = @("64748B", "94A3B8", "CBD5E1", "F1F5F9", "FFFFFF")
        Rim = @("475569", "64748B", "94A3B8", "E2E8F0", "FFFFFF")
        RimHi = "00E5FF"
        Soundboard = @("0369A1", "0284C7", "38BDF8", "7DD3FC", "E0F2FE")
        Strings = @("0284C7", "38BDF8", "BAE6FD", "F0F9FF", "FFFFFF")
        Pinblock = "FFFFFF"; PropStick = "FFFFFF"; Studs = "00E5FF"
        Plaque = @("0284C7", "38BDF8", "FFFFFF"); Caster = "38BDF8"
        KeyW = @("FFFFFF", "FFFFFF"); KeyB = @("0284C7", "38BDF8")
        BenchCushion = @("64748B", "94A3B8", "CBD5E1", "F1F5F9", "FFFFFF")
        BenchTuft = "00E5FF"; BenchTrim = "38BDF8"
        ShadowRgb = @(14, 34, 52)
        RareMode = "CRYSTAL_PEARL"
    },

    # ================= EPIC (9 - 12) =================
    @{
        Id = 9; Rarity = "Epic"; RarityIdx = 3; VarIdx = 1; Name = "Epic 1 - Royal Amethyst Velvet & Vintage Gold"
        Lid = @("3B0764", "581C87", "7E22CE", "9333EA", "C084FC")
        Rim = @("2E1065", "4C1D95", "6B21A8", "9333EA", "FBBF24")
        RimHi = "FBBF24"
        Soundboard = @("451A03", "78350F", "B45309", "D97706", "FBBF24")
        Strings = @("B45309", "D97706", "F59E0B", "FDE047", "FFFBEB")
        Pinblock = "FEF08A"; PropStick = "FBBF24"; Studs = "C084FC"
        Plaque = @("78350F", "F59E0B", "FEF08A"); Caster = "FBBF24"
        KeyW = @("FEFCE8", "FFFFFF"); KeyB = @("581C87", "9333EA")
        BenchCushion = @("3B0764", "581C87", "7E22CE", "9333EA", "C084FC")
        BenchTuft = "FBBF24"; BenchTrim = "FBBF24"
        ShadowRgb = @(32, 12, 48)
        EpicMode = "AMETHYST_GOLD"
    },
    @{
        Id = 10; Rarity = "Epic"; RarityIdx = 3; VarIdx = 2; Name = "Epic 2 - Imperial Crimson Ruby & Baroque Gold"
        Lid = @("450A0A", "7F1D1D", "991B1B", "B91C1C", "EF4444")
        Rim = @("2A0404", "450A0A", "7F1D1D", "B91C1C", "FBBF24")
        RimHi = "FDE047"
        Soundboard = @("451A03", "78350F", "B45309", "F59E0B", "FEF08A")
        Strings = @("B45309", "F59E0B", "FBBF24", "FEF08A", "FFFFFF")
        Pinblock = "FEF08A"; PropStick = "FDE047"; Studs = "EF4444"
        Plaque = @("78350F", "FBBF24", "FEF08A"); Caster = "FBBF24"
        KeyW = @("FFFBEB", "FFFFFF"); KeyB = @("7F1D1D", "DC2626")
        BenchCushion = @("450A0A", "7F1D1D", "991B1B", "DC2626", "F87171")
        BenchTuft = "FDE047"; BenchTrim = "FBBF24"
        ShadowRgb = @(42, 10, 14)
        EpicMode = "CRIMSON_BAROQUE"
    },
    @{
        Id = 11; Rarity = "Epic"; RarityIdx = 3; VarIdx = 3; Name = "Epic 3 - Arcane Nightshade & Magenta Runes"
        Lid = @("1E1B4B", "2E1065", "4C1D95", "6D28D9", "A855F7")
        Rim = @("0F0728", "1E1B4B", "3B0764", "581C87", "F59E0B")
        RimHi = "E879F9"
        Soundboard = @("2E1065", "4C1D95", "701A75", "A21CAF", "E879F9")
        Strings = @("A21CAF", "D946EF", "E879F9", "F5D0FE", "FFFFFF")
        Pinblock = "F5D0FE"; PropStick = "FBBF24"; Studs = "FDE047"
        Plaque = @("581C87", "E879F9", "FEF08A"); Caster = "F59E0B"
        KeyW = @("2E1065", "4C1D95"); KeyB = @("FBBF24", "FEF08A") # Arcane dark keys with gold sharps
        BenchCushion = @("1E1B4B", "3B0764", "581C87", "7E22CE", "D946EF")
        BenchTuft = "FDE047"; BenchTrim = "E879F9"
        ShadowRgb = @(24, 10, 44)
        EpicMode = "ARCANE_RUNES"
    },
    @{
        Id = 12; Rarity = "Epic"; RarityIdx = 3; VarIdx = 4; Name = "Epic 4 - Baroque Sunburst Gold & Amethyst Core"
        Lid = @("78350F", "B45309", "D97706", "FBBF24", "FEF08A")
        Rim = @("451A03", "78350F", "B45309", "F59E0B", "FEF08A")
        RimHi = "FFFBEB"
        Soundboard = @("2E1065", "4C1D95", "6B21A8", "9333EA", "C084FC")
        Strings = @("9333EA", "C084FC", "FBBF24", "FEF08A", "FFFFFF")
        Pinblock = "FEF08A"; PropStick = "FEF08A"; Studs = "FDE047"
        Plaque = @("4C1D95", "9333EA", "FEF08A"); Caster = "FEF08A"
        KeyW = @("FEF08A", "FFFBEB"); KeyB = @("581C87", "9333EA")
        BenchCushion = @("3B0764", "581C87", "7E22CE", "9333EA", "C084FC")
        BenchTuft = "FEF08A"; BenchTrim = "FBBF24"
        ShadowRgb = @(44, 24, 8)
        EpicMode = "GILDED_AMETHYST"
    },

    # ================= LEGENDARY (13 - 16) =================
    @{
        Id = 13; Rarity = "Legendary"; RarityIdx = 4; VarIdx = 1; Name = "Legendary 1 - Celestial Seraphim Wings & Ruby Core"
        Lid = @("78350F", "B45309", "F59E0B", "FDE047", "FFFBEB")
        Rim = @("451A03", "78350F", "B45309", "F59E0B", "FEF08A")
        RimHi = "FFFFFF"
        Soundboard = @("451A03", "78350F", "D97706", "F59E0B", "FEF08A")
        Strings = @("D97706", "F59E0B", "FDE047", "FEF08A", "FFFFFF")
        Pinblock = "FFFFFF"; PropStick = "FFFFFF"; Studs = "EF4444"
        Plaque = @("991B1B", "EF4444", "FEF08A"); Caster = "FEF08A"
        KeyW = @("FFFBEB", "FFFFFF"); KeyB = @("B45309", "F59E0B")
        BenchCushion = @("7F1D1D", "991B1B", "DC2626", "EF4444", "FCA5A5")
        BenchTuft = "FEF08A"; BenchTrim = "FEF08A"
        ShadowRgb = @(120, 60, 10)
        LegendaryMode = "SERAPHIM"
    },
    @{
        Id = 14; Rarity = "Legendary"; RarityIdx = 4; VarIdx = 2; Name = "Legendary 2 - Solar Phoenix Flame & Sunburst Crest"
        Lid = @("7F1D1D", "B91C1C", "EA580C", "F59E0B", "FEF08A")
        Rim = @("450A0A", "7F1D1D", "C2410C", "F59E0B", "FEF08A")
        RimHi = "FEF08A"
        Soundboard = @("7C2D12", "C2410C", "EA580C", "F97316", "FDE047")
        Strings = @("F97316", "FB923C", "FDE047", "FEF08A", "FFFFFF")
        Pinblock = "FEF08A"; PropStick = "FEF08A"; Studs = "FFFFFF"
        Plaque = @("7C2D12", "F97316", "FEF08A"); Caster = "FDE047"
        KeyW = @("FEF08A", "FFFFFF"); KeyB = @("991B1B", "EA580C")
        BenchCushion = @("7C2D12", "9A3412", "EA580C", "F97316", "FDE047")
        BenchTuft = "FFFFFF"; BenchTrim = "FEF08A"
        ShadowRgb = @(135, 45, 12)
        LegendaryMode = "PHOENIX"
    },
    @{
        Id = 15; Rarity = "Legendary"; RarityIdx = 4; VarIdx = 3; Name = "Legendary 3 - Astral Archangel & Sapphire Halo"
        Lid = @("94A3B8", "CBD5E1", "FEF08A", "FFFBEB", "FFFFFF")
        Rim = @("78350F", "B45309", "F59E0B", "FEF08A", "FFFFFF")
        RimHi = "38BDF8"
        Soundboard = @("0F172A", "1E3A8A", "0284C7", "38BDF8", "E0F2FE")
        Strings = @("38BDF8", "7DD3FC", "FEF08A", "FFFBEB", "FFFFFF")
        Pinblock = "FFFFFF"; PropStick = "38BDF8"; Studs = "FEF08A"
        Plaque = @("0284C7", "38BDF8", "FEF08A"); Caster = "FEF08A"
        KeyW = @("FFFFFF", "FFFFFF"); KeyB = @("0284C7", "38BDF8")
        BenchCushion = @("0369A1", "0284C7", "38BDF8", "BAE6FD", "FFFFFF")
        BenchTuft = "FEF08A"; BenchTrim = "FEF08A"
        ShadowRgb = @(30, 80, 140)
        LegendaryMode = "ARCHANGEL"
    },
    @{
        Id = 16; Rarity = "Legendary"; RarityIdx = 4; VarIdx = 4; Name = "Legendary 4 - Sovereign Dragon Gold & Emerald Jade"
        Lid = @("451A03", "78350F", "B45309", "F59E0B", "FEF08A")
        Rim = @("064E3B", "047857", "B45309", "F59E0B", "FEF08A")
        RimHi = "34D399"
        Soundboard = @("022C22", "064E3B", "047857", "10B981", "6EE7B7")
        Strings = @("10B981", "34D399", "FBBF24", "FEF08A", "FFFFFF")
        Pinblock = "A7F3D0"; PropStick = "FEF08A"; Studs = "10B981"
        Plaque = @("064E3B", "10B981", "FEF08A"); Caster = "FEF08A"
        KeyW = @("ECFDF5", "FFFFFF"); KeyB = @("047857", "10B981")
        BenchCushion = @("064E3B", "047857", "059669", "10B981", "6EE7B7")
        BenchTuft = "FEF08A"; BenchTrim = "F59E0B"
        ShadowRgb = @(16, 90, 50)
        LegendaryMode = "DRAGON"
    }
)

function SetPx($bmp, [int]$x, [int]$y, [string]$hex, [int]$a = 255) {
    if ($x -ge 0 -and $x -lt $bmp.Width -and $y -ge 0 -and $y -lt $bmp.Height) {
        $bmp.SetPixel($x, $y, (Hex $hex $a))
    }
}

# Helper to draw ornate feathered wings above the grand piano lid (x = 46..98, y = 0..15)
function Draw-Wings($bmp, $outlineHex, $darkHex, $midHex, $lightHex, $tipHex, $gemHex) {
    # Left/Front Sweeping Wing rising from Lid Hinge (around ox=53..57 -> x=57..61, y=10)
    $leftWing = @(
        # Base connection at hinge (x=56..62, y=9..11)
        @(57,10,$outlineHex), @(58,10,$darkHex), @(59,10,$midHex), @(60,10,$lightHex), @(61,10,$outlineHex),
        @(55,9,$outlineHex), @(56,9,$midHex), @(57,9,$lightHex), @(58,9,$tipHex), @(59,9,$midHex), @(60,9,$outlineHex),
        # Mid wing arch sweeping up-left and up-right
        @(52,8,$outlineHex), @(53,8,$midHex), @(54,8,$lightHex), @(55,8,$tipHex), @(56,8,$lightHex), @(57,8,$midHex), @(58,8,$darkHex), @(59,8,$outlineHex),
        @(49,7,$outlineHex), @(50,7,$lightHex), @(51,7,$tipHex), @(52,7,$lightHex), @(53,7,$midHex), @(54,7,$darkHex), @(55,7,$midHex), @(56,7,$lightHex), @(57,7,$outlineHex),
        @(46,6,$outlineHex), @(47,6,$tipHex), @(48,6,$lightHex), @(49,6,$midHex), @(50,6,$darkHex), @(51,6,$outlineHex), @(52,6,$lightHex), @(53,6,$tipHex), @(54,6,$midHex), @(55,6,$outlineHex),
        @(44,5,$outlineHex), @(45,5,$tipHex), @(46,5,$lightHex), @(47,5,$midHex), @(48,5,$outlineHex), @(50,5,$outlineHex), @(51,5,$tipHex), @(52,5,$lightHex), @(53,5,$darkHex), @(54,5,$outlineHex),
        @(42,4,$outlineHex), @(43,4,$tipHex), @(44,4,$lightHex), @(45,4,$outlineHex), @(49,4,$outlineHex), @(50,4,$tipHex), @(51,4,$lightHex), @(52,4,$outlineHex),
        @(41,3,$outlineHex), @(42,3,$tipHex), @(43,3,$outlineHex), @(48,3,$outlineHex), @(49,3,$tipHex), @(50,3,$outlineHex)
    )
    # Right/Main Grand Wing sweeping across the long lid (x=66..99, y=1..12)
    $rightWing = @(
        # Base attachment along lid top ridge (x=66..88, y=11)
        @(66,11,$outlineHex), @(67,11,$darkHex), @(68,11,$midHex), @(69,11,$lightHex), @(70,11,$midHex), @(71,11,$darkHex), @(72,11,$outlineHex),
        @(67,10,$outlineHex), @(68,10,$midHex), @(69,10,$lightHex), @(70,10,$tipHex), @(71,10,$lightHex), @(72,10,$midHex), @(73,10,$darkHex), @(74,10,$outlineHex),
        # Primary feathers sweeping up & right toward tail (y=2..9)
        @(69,9,$outlineHex), @(70,9,$midHex), @(71,9,$lightHex), @(72,9,$tipHex), @(73,9,$lightHex), @(74,9,$midHex), @(75,9,$darkHex), @(76,9,$midHex), @(77,9,$lightHex), @(78,9,$outlineHex),
        @(71,8,$outlineHex), @(72,8,$midHex), @(73,8,$lightHex), @(74,8,$tipHex), @(75,8,$lightHex), @(76,8,$midHex), @(77,8,$darkHex), @(78,8,$midHex), @(79,8,$lightHex), @(80,8,$tipHex), @(81,8,$outlineHex),
        @(73,7,$outlineHex), @(74,7,$midHex), @(75,7,$lightHex), @(76,7,$tipHex), @(77,7,$lightHex), @(78,7,$midHex), @(79,7,$darkHex), @(80,7,$midHex), @(81,7,$lightHex), @(82,7,$tipHex), @(83,7,$lightHex), @(84,7,$outlineHex),
        @(76,6,$outlineHex), @(77,6,$midHex), @(78,6,$lightHex), @(79,6,$tipHex), @(80,6,$lightHex), @(81,6,$darkHex), @(82,6,$midHex), @(83,6,$lightHex), @(84,6,$tipHex), @(85,6,$lightHex), @(86,6,$midHex), @(87,6,$outlineHex),
        @(79,5,$outlineHex), @(80,5,$lightHex), @(81,5,$tipHex), @(82,5,$lightHex), @(83,5,$darkHex), @(84,5,$midHex), @(85,5,$lightHex), @(86,5,$tipHex), @(87,5,$lightHex), @(88,5,$midHex), @(89,5,$outlineHex),
        @(82,4,$outlineHex), @(83,4,$lightHex), @(84,4,$tipHex), @(85,4,$outlineHex), @(86,4,$outlineHex), @(87,4,$lightHex), @(88,4,$tipHex), @(89,4,$lightHex), @(90,4,$midHex), @(91,4,$outlineHex),
        @(88,3,$outlineHex), @(89,3,$lightHex), @(90,3,$tipHex), @(91,3,$lightHex), @(92,3,$outlineHex),
        @(90,2,$outlineHex), @(91,2,$tipHex), @(92,2,$lightHex), @(93,2,$outlineHex),
        # Secondary lower feather tips along tail (x=88..101, y=7..12)
        @(89,7,$outlineHex), @(90,7,$lightHex), @(91,7,$tipHex), @(92,7,$lightHex), @(93,7,$outlineHex),
        @(91,8,$outlineHex), @(92,8,$midHex), @(93,8,$lightHex), @(94,8,$tipHex), @(95,8,$outlineHex),
        @(93,9,$outlineHex), @(94,9,$midHex), @(95,9,$lightHex), @(96,9,$tipHex), @(97,9,$outlineHex),
        @(95,10,$outlineHex), @(96,10,$midHex), @(97,10,$lightHex), @(98,10,$tipHex), @(99,10,$outlineHex)
    )
    foreach ($p in ($leftWing + $rightWing)) {
        SetPx $bmp $p[0] $p[1] $p[2]
    }
    # Wing hinge core gem (x=59..60, y=10..11) and tail gem (x=69..70, y=11)
    SetPx $bmp 59 10 $gemHex; SetPx $bmp 60 10 $tipHex; SetPx $bmp 59 11 $gemHex
    SetPx $bmp 69 11 $gemHex; SetPx $bmp 70 11 $tipHex
}

# Helper to draw 4-point sparkle star at (cx, cy)
function Draw-Sparkle($bmp, [int]$cx, [int]$cy, [string]$coreHex, [string]$rayHex) {
    SetPx $bmp $cx $cy $coreHex
    SetPx $bmp ($cx - 1) $cy $rayHex
    SetPx $bmp ($cx + 1) $cy $rayHex
    SetPx $bmp $cx ($cy - 1) $rayHex
    SetPx $bmp $cx ($cy + 1) $rayHex
}

$outDirWorking = "I:\Shared drives\Pitch Quest Shared Drive\Working Files\Music Store\Export\Instruments"
$outDirFinalized = "I:\Shared drives\Pitch Quest Shared Drive\Finalized Files\Instruments"
$localOutDir = "C:\Users\leero\Source\Repos\aseprite2\ref_instruments\grand_piano"
New-Item -ItemType Directory -Force -Path $localOutDir | Out-Null

$generatedBitmaps = @()

foreach ($v in $variations) {
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
                    # Ornate border + emblem core on plaque
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
                    # RIM (outer curved case body)
                    # Add a top bevel highlight & lower trim molding along the S-curve
                    $adjLum = [Math]::Min(1.0, $lum * 3.2)
                    $r = $v.Rim
                    $c = PaletteRamp $r[0] $r[1] $r[2] $r[3] $r[4] $adjLum $a
                }
            }
            $bmp.SetPixel($x, $y, $c)
        }

        # Add crisp S-Curve Rim Molding / Accent Line for Rare, Epic, and Legendary (and subtle wood bevel for Common 2-4)
        for ($ox = 24; $ox -le 94; $ox++) {
            # Find bottom-most non-shadow pixel of the piano body (excluding legs x=36..43 and x=87..92)
            $botY = -1
            $maxSearchY = 53
            for ($oy = $maxSearchY; $oy -ge 20; $oy--) {
                $pMatch = $bmp.GetPixel($ox + $offX, $oy + $offY)
                if ($pMatch.A -eq 255) {
                    $botY = $oy
                    break
                }
            }
            if ($botY -gt 20) {
                if ($v.RarityIdx -eq 2) {
                    # Rare: Glowing neon LED strip along the lower rim edge & top lid edge
                    SetPx $bmp ($ox + $offX) ($botY + $offY - 1) $v.RimHi
                }
                elseif ($v.RarityIdx -eq 3) {
                    # Epic: Ornate double baroque gold filigree molding on S-rim (top & bottom border of rim)
                    SetPx $bmp ($ox + $offX) ($botY + $offY) $v.Rim[3]
                    SetPx $bmp ($ox + $offX) ($botY + $offY - 1) $v.RimHi
                    if (($ox % 4) -eq 0) {
                        SetPx $bmp ($ox + $offX) ($botY + $offY - 3) $v.RimHi
                    }
                }
                elseif ($v.RarityIdx -eq 4) {
                    # Legendary: Celestial golden armor trim & gem studs along S-rim
                    SetPx $bmp ($ox + $offX) ($botY + $offY) $v.Lid[1]
                    SetPx $bmp ($ox + $offX) ($botY + $offY - 1) $v.Lid[3]
                    SetPx $bmp ($ox + $offX) ($botY + $offY - 2) $v.RimHi
                    if (($ox % 6) -eq 0 -and $ox -ge 52) {
                        SetPx $bmp ($ox + $offX) ($botY + $offY - 4) $v.Studs
                    }
                }
            }
        }

        # Rarity-specific structural pixel-art details:
        if ($v.RareMode -eq "CYAN_LED") {
            # Glowing cyan LED equalizer bars on music desk & lid rim
            for ($oy = 15; $oy -le 35; $oy += 3) {
                SetPx $bmp (37 + $offX) ($oy + $offY) "00E5FF"
                SetPx $bmp (38 + $offX) ($oy + $offY) "7DD3FC"
            }
        }
        elseif ($v.RareMode -eq "SAPPHIRE_ICE") {
            # Frost-crystal nodes along lid hinge & rim
            for ($ox = 44; $ox -le 92; $ox += 6) {
                $ly = (Get-LidBottomY $ox) + $offY
                SetPx $bmp ($ox + $offX) $ly "E0F2FE"
                SetPx $bmp ($ox + $offX + 1) $ly "38BDF8"
            }
        }
        elseif ($v.RareMode -eq "EMERALD_MATRIX") {
            # Cyber emerald circuit nodes on lid & desk
            for ($ox = 46; $ox -le 90; $ox += 5) {
                SetPx $bmp ($ox + $offX) (6 + $offY) "34D399"
                SetPx $bmp ($ox + $offX + 1) (6 + $offY) "10B981"
            }
        }
        elseif ($v.RareMode -eq "CRYSTAL_PEARL") {
            # Diamond crystal glints on white pearl lid
            Draw-Sparkle $bmp (64 + $offX) (7 + $offY) "FFFFFF" "38BDF8"
            Draw-Sparkle $bmp (82 + $offX) (6 + $offY) "FFFFFF" "00E5FF"
        }

        if ($v.RarityIdx -eq 3) {
            # Epic: Baroque gold lid border & gemstone crown on music desk (x=32..37, y=4..6)
            for ($ox = 43; $ox -le 93; $ox++) {
                $ly = (Get-LidBottomY $ox) + $offY
                SetPx $bmp ($ox + $offX) $ly "FBBF24"
                if (($ox % 5) -eq 0) { SetPx $bmp ($ox + $offX) ($ly - 1) "FEF08A" }
            }
            # Royal Crown Crest at top of Music Desk (ox=32..38, oy=2..4)
            $crownGem = $(if ($v.EpicMode -eq "CRIMSON_BAROQUE") { "EF4444" } elseif ($v.EpicMode -eq "ARCANE_RUNES") { "E879F9" } else { "9333EA" })
            $crownPx = @(
                @(33,4,"B45309"), @(34,4,"FBBF24"), @(35,4,$crownGem), @(36,4,"FBBF24"), @(37,4,"B45309"),
                @(33,3,"FBBF24"), @(34,3,"FEF08A"), @(35,3,"FBBF24"), @(36,3,"FEF08A"), @(37,3,"FBBF24"),
                @(33,2,"FEF08A"), @(35,2,"FFFBEB"), @(37,2,"FEF08A")
            )
            foreach ($cp in $crownPx) { SetPx $bmp ($cp[0] + $offX) ($cp[1] + $offY) $cp[2] }
            if ($v.EpicMode -eq "ARCANE_RUNES") {
                Draw-Sparkle $bmp (62 + $offX) (2 + $offY) "FFFFFF" "E879F9"
                Draw-Sparkle $bmp (84 + $offX) (1 + $offY) "FEF08A" "C084FC"
            }
        }

        if ($v.RarityIdx -eq 4) {
            # Legendary: Sweeping Wings + Royal Crest + Halo & Sparkle Aura
            if ($v.LegendaryMode -eq "SERAPHIM") {
                Draw-Wings $bmp "78350F" "B45309" "F59E0B" "FEF08A" "FFFFFF" "EF4444"
                # Golden Halo over Music Desk (x=35..41, y=7..9)
                foreach ($hx in 32..38) { SetPx $bmp ($hx + $offX) (2 + $offY) "FEF08A" }
                SetPx $bmp (35 + $offX) (1 + $offY) "FFFFFF"
                Draw-Sparkle $bmp 40 4 "FFFFFF" "FEF08A"
                Draw-Sparkle $bmp 68 2 "FFFFFF" "FEF08A"
                Draw-Sparkle $bmp 98 4 "FFFFFF" "FDE047"
                Draw-Sparkle $bmp 18 32 "FFFFFF" "FEF08A"
            }
            elseif ($v.LegendaryMode -eq "PHOENIX") {
                Draw-Wings $bmp "7F1D1D" "C2410C" "F97316" "FDE047" "FFFFFF" "EF4444"
                # Solar Phoenix Flame Crest over Music Desk
                $crest = @(
                    @(34,3,"EA580C"), @(35,3,"FEF08A"), @(36,3,"EA580C"),
                    @(33,2,"EF4444"), @(34,2,"FDE047"), @(35,2,"FFFFFF"), @(36,2,"FDE047"), @(37,2,"EF4444"),
                    @(35,1,"FEF08A"), @(35,0,"FFFFFF")
                )
                foreach ($cp in $crest) { SetPx $bmp ($cp[0] + $offX) ($cp[1] + $offY) $cp[2] }
                Draw-Sparkle $bmp 38 3 "FFFFFF" "F97316"
                Draw-Sparkle $bmp 65 2 "FFFFFF" "FEF08A"
                Draw-Sparkle $bmp 96 3 "FEF08A" "EF4444"
                Draw-Sparkle $bmp 16 33 "FEF08A" "F97316"
            }
            elseif ($v.LegendaryMode -eq "ARCHANGEL") {
                Draw-Wings $bmp "0369A1" "38BDF8" "BAE6FD" "FEF08A" "FFFFFF" "00E5FF"
                # Levitating Astral Sapphire-Gold Halo
                foreach ($hx in 31..39) { SetPx $bmp ($hx + $offX) (2 + $offY) "38BDF8" }
                foreach ($hx in 33..37) { SetPx $bmp ($hx + $offX) (1 + $offY) "FFFFFF" }
                Draw-Sparkle $bmp 39 3 "FFFFFF" "38BDF8"
                Draw-Sparkle $bmp 64 2 "FFFFFF" "FEF08A"
                Draw-Sparkle $bmp 85 1 "FFFFFF" "38BDF8"
                Draw-Sparkle $bmp 99 5 "FFFFFF" "FEF08A"
                Draw-Sparkle $bmp 17 32 "FFFFFF" "38BDF8"
            }
            elseif ($v.LegendaryMode -eq "DRAGON") {
                Draw-Wings $bmp "064E3B" "78350F" "D97706" "FDE047" "6EE7B7" "10B981"
                # Imperial Dragon Horned Emerald Crown
                $dragonCrown = @(
                    @(32,3,"78350F"), @(33,3,"F59E0B"), @(34,3,"10B981"), @(35,3,"6EE7B7"), @(36,3,"10B981"), @(37,3,"F59E0B"), @(38,3,"78350F"),
                    @(31,2,"F59E0B"), @(32,2,"FEF08A"), @(35,2,"FFFFFF"), @(38,2,"FEF08A"), @(39,2,"F59E0B"),
                    @(31,1,"FEF08A"), @(35,1,"34D399"), @(39,1,"FEF08A")
                )
                foreach ($cp in $dragonCrown) { SetPx $bmp ($cp[0] + $offX) ($cp[1] + $offY) $cp[2] }
                Draw-Sparkle $bmp 40 4 "FFFFFF" "34D399"
                Draw-Sparkle $bmp 66 2 "FEF08A" "10B981"
                Draw-Sparkle $bmp 97 4 "FFFFFF" "6EE7B7"
                Draw-Sparkle $bmp 17 32 "FEF08A" "10B981"
            }
        }
    }

    $id = $v.Id
    $bmp.Save("$localOutDir\GrandPiano_Var$id.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Save("$outDirWorking\Grand Piano - Variation $id ($($v.Rarity) $($v.VarIdx)).png", [System.Drawing.Imaging.ImageFormat]::Png)

    # Also save primary 4 rarity tier files (Var 1 = Common 1, Var 5 = Rare 1, Var 9 = Epic 1, Var 13 = Legendary 1)
    if ($v.VarIdx -eq 1) {
        $rNum = $v.RarityIdx
        $bmp.Save("$outDirWorking\Grand Piano$rNum.png", [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Save("$localOutDir\Grand Piano$rNum.png", [System.Drawing.Imaging.ImageFormat]::Png)

        # 4x Nearest-Neighbor Preview for primary rarity representative
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

# Build 3x Nearest-Neighbor 4x4 Master Grid Preview Sheet (4 Rarities Rows x 4 Variations Columns)
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

    # Also build a per-rarity 4-variation strip (3x upscaled)
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

        # Manual pixel-perfect nearest-neighbor blit onto both sheet and rarity strip
        for ($sy = 0; $sy -lt $H; $sy++) {
            for ($sx = 0; $sx -lt $W; $sx++) {
                $px = $curBmp.GetPixel($sx, $sy)
                if ($px.A -gt 0) {
                    # Alpha composite over cell bg
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
