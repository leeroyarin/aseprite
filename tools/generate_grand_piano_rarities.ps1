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

# Define the 16 refined variation specifications
# Guidelines:
# - Common: Baseline concert materials & wood tones
# - Rare: Added physical elements (brass candlestick sconces, sheet music on stand, metronome, velvet pedal mat)
# - Epic: Glowing edge lights (soft cyber cyan, magenta neon, golden glow, emerald line) along lid & rim, grounded grand piano silhouette
# - Legendary: Reddish wood (Rosewood, Mahogany, Padauk, Cherry) with inlaid gold filigree borders, gold pedals, gold hinges, gold soundboard accents
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
        Pinblock = "FEF9C3"; PropStick = "F59E0B"; Studs = "FACC15"
        Plaque = @("713F12", "CA8A04", "FEF08A"); Caster = "F59E0B"
        KeyW = @("FEFCE8", "FFFFFF"); KeyB = @("292524", "44403C")
        BenchCushion = @("451A03", "78350F", "B45309", "D97706", "F59E0B")
        BenchTuft = "FEF08A"; BenchTrim = "FBBF24"
        ShadowRgb = @(28, 16, 8)
        WoodGrain = $true
    },
    @{
        Id = 4; Rarity = "Common"; RarityIdx = 1; VarIdx = 4; Name = "Common 4 - Studio Slate & Crimson Felt"
        Lid = @("0F172A", "1E293B", "334155", "475569", "64748B")
        Rim = @("020617", "0F172A", "1E293B", "334155", "475569")
        RimHi = "94A3B8"
        Soundboard = @("2A0A10", "450A0A", "7F1D1D", "991B1B", "DC2626")
        Strings = @("64748B", "94A3B8", "CBD5E1", "E2E8F0", "FFFFFF")
        Pinblock = "E2E8F0"; PropStick = "94A3B8"; Studs = "CBD5E1"
        Plaque = @("1E293B", "64748B", "CBD5E1"); Caster = "94A3B8"
        KeyW = @("F1F5F9", "FFFFFF"); KeyB = @("0F172A", "1E293B")
        BenchCushion = @("450A0A", "7F1D1D", "991B1B", "B91C1C", "DC2626")
        BenchTuft = "CBD5E1"; BenchTrim = "EF4444"
        ShadowRgb = @(12, 16, 26)
    },

    # ================= RARE (5 - 8) =================
    # Rare feature: Extra grounded elements (Candlesticks, Sheet Music on Desk, Metronome, Velvet Mat)
    @{
        Id = 5; Rarity = "Rare"; RarityIdx = 2; VarIdx = 1; Name = "Rare 1 - Studio Walnut with Sheet Music & Metronome"
        Lid = @("261208", "3D1D0D", "5C2E17", "7A3E20", "9C532D")
        Rim = @("1A0C05", "261208", "3D1D0D", "5C2E17", "7A3E20")
        RimHi = "C27845"
        Soundboard = @("3D1D0D", "5C2E17", "8A4B22", "B56832", "DE8B49")
        Strings = @("8A4B22", "B56832", "D49755", "F0C58A", "FFF0D4")
        Pinblock = "F0C58A"; PropStick = "9C532D"; Studs = "D49755"
        Plaque = @("3D1D0D", "8A4B22", "F0C58A"); Caster = "B56832"
        KeyW = @("F5F5F4", "FFFFFF"); KeyB = @("1C1917", "292524")
        BenchCushion = @("1E3A5F", "1D4ED8", "2563EB", "3B82F6", "60A5FA")
        BenchTuft = "F0C58A"; BenchTrim = "93C5FD"
        ShadowRgb = @(20, 14, 10)
        WoodGrain = $true
        RareItem = "SHEET_MUSIC_AND_METRONOME"
    },
    @{
        Id = 6; Rarity = "Rare"; RarityIdx = 2; VarIdx = 2; Name = "Rare 2 - Midnight Blue & Brass Candlestick Sconces"
        Lid = @("0B132B", "1C2541", "2D3A63", "3A506B", "5BC0BE")
        Rim = @("070B18", "0B132B", "1C2541", "2D3A63", "3A506B")
        RimHi = "5BC0BE"
        Soundboard = @("0B132B", "1C2541", "2E4057", "4A6572", "8EADC1")
        Strings = @("4A6572", "7292A6", "9EBCC9", "C7DCDE", "FFFFFF")
        Pinblock = "C7DCDE"; PropStick = "5BC0BE"; Studs = "FBBF24"
        Plaque = @("1C2541", "3A506B", "5BC0BE"); Caster = "FBBF24"
        KeyW = @("F0F4F8", "FFFFFF"); KeyB = @("0B132B", "1C2541")
        BenchCushion = @("1C2541", "2D3A63", "3A506B", "4E6785", "6F8695")
        BenchTuft = "FBBF24"; BenchTrim = "5BC0BE"
        ShadowRgb = @(10, 16, 32)
        RareItem = "BRASS_CANDLESTICKS"
    },
    @{
        Id = 7; Rarity = "Rare"; RarityIdx = 2; VarIdx = 3; Name = "Rare 3 - Forest Emerald & Gold Lyre Pedals"
        Lid = @("06281E", "0D3D2E", "165B45", "1F7A5E", "2CA580")
        Rim = @("031812", "06281E", "0D3D2E", "165B45", "1F7A5E")
        RimHi = "34D399"
        Soundboard = @("3B200C", "5C3214", "85491E", "B3662C", "D98A44")
        Strings = @("85491E", "B3662C", "D49755", "F5C887", "FFF2D6")
        Pinblock = "F5C887"; PropStick = "D49755"; Studs = "FBBF24"
        Plaque = @("0D3D2E", "1F7A5E", "34D399"); Caster = "FBBF24"
        KeyW = @("F0FDF4", "FFFFFF"); KeyB = @("06281E", "0D3D2E")
        BenchCushion = @("06281E", "0D3D2E", "165B45", "1F7A5E", "2CA580")
        BenchTuft = "FBBF24"; BenchTrim = "34D399"
        ShadowRgb = @(8, 26, 20)
        RareItem = "EMERALD_VELVET_MAT"
    },
    @{
        Id = 8; Rarity = "Rare"; RarityIdx = 2; VarIdx = 4; Name = "Rare 4 - Pure Ivory Pearl with Ornate Brass Desk"
        Lid = @("475569", "64748B", "94A3B8", "CBD5E1", "F1F5F9")
        Rim = @("334155", "475569", "64748B", "94A3B8", "CBD5E1")
        RimHi = "F8FAFC"
        Soundboard = @("38BDF8", "0284C7", "0369A1", "075985", "0C4A6E")
        Strings = @("0284C7", "38BDF8", "7DD3FC", "BAE6FD", "FFFFFF")
        Pinblock = "FFFFFF"; PropStick = "CBD5E1"; Studs = "FBBF24"
        Plaque = @("0369A1", "38BDF8", "FFFFFF"); Caster = "FBBF24"
        KeyW = @("FFFFFF", "FFFFFF"); KeyB = @("1E293B", "334155")
        BenchCushion = @("1E293B", "334155", "475569", "64748B", "94A3B8")
        BenchTuft = "FBBF24"; BenchTrim = "FBBF24"
        ShadowRgb = @(20, 26, 36)
        RareItem = "IVORY_SCORE_HOLDER"
    },

    # ================= EPIC (9 - 12) =================
    # Epic feature: Glowing lights along edges / lid rim / music desk, tasteful & grounded grand piano
    @{
        Id = 9; Rarity = "Epic"; RarityIdx = 3; VarIdx = 1; Name = "Epic 1 - Royal Amethyst with Violet Edge Glow"
        Lid = @("2A0845", "3D0E63", "5B188F", "7B24BD", "A13EE3")
        Rim = @("1B052D", "2A0845", "3D0E63", "5B188F", "7B24BD")
        RimHi = "C084FC"
        Soundboard = @("291509", "451A03", "78350F", "B45309", "F59E0B")
        Strings = @("78350F", "B45309", "D97706", "FBBF24", "FEF08A")
        Pinblock = "FEF08A"; PropStick = "FBBF24"; Studs = "C084FC"
        Plaque = @("3D0E63", "7B24BD", "C084FC"); Caster = "FBBF24"
        KeyW = @("FAF5FF", "FFFFFF"); KeyB = @("2A0845", "3D0E63")
        BenchCushion = @("2A0845", "3D0E63", "5B188F", "7B24BD", "A13EE3")
        BenchTuft = "FBBF24"; BenchTrim = "C084FC"
        ShadowRgb = @(22, 8, 36)
        GlowColor = "C084FC"
        GlowHi = "E879F9"
    },
    @{
        Id = 10; Rarity = "Epic"; RarityIdx = 3; VarIdx = 2; Name = "Epic 2 - Obsidian Black with Amber Gold Edge Glow"
        Lid = @("111116", "1C1C24", "2E2E3A", "454555", "626275")
        Rim = @("0A0A0D", "111116", "1C1C24", "2E2E3A", "454555")
        RimHi = "F59E0B"
        Soundboard = @("2A0E07", "451A03", "78350F", "A16207", "D97706")
        Strings = @("78350F", "A16207", "D97706", "FBBF24", "FEF08A")
        Pinblock = "FEF08A"; PropStick = "FBBF24"; Studs = "F59E0B"
        Plaque = @("1C1C24", "454555", "FBBF24"); Caster = "FBBF24"
        KeyW = @("F5F5F4", "FFFFFF"); KeyB = @("111116", "1C1C24")
        BenchCushion = @("450A0A", "7F1D1D", "991B1B", "B91C1C", "DC2626")
        BenchTuft = "FBBF24"; BenchTrim = "F59E0B"
        ShadowRgb = @(12, 12, 16)
        GlowColor = "F59E0B"
        GlowHi = "FDE047"
    },
    @{
        Id = 11; Rarity = "Epic"; RarityIdx = 3; VarIdx = 3; Name = "Epic 3 - Deep Sapphire with Ice-Cyan Edge Glow"
        Lid = @("0A1733", "122552", "1C3B7E", "2854AC", "3B73DC")
        Rim = @("060F21", "0A1733", "122552", "1C3B7E", "2854AC")
        RimHi = "38BDF8"
        Soundboard = @("0A1733", "122552", "1C3B7E", "0284C7", "38BDF8")
        Strings = @("0284C7", "38BDF8", "7DD3FC", "BAE6FD", "FFFFFF")
        Pinblock = "BAE6FD"; PropStick = "38BDF8"; Studs = "7DD3FC"
        Plaque = @("122552", "1C3B7E", "38BDF8"); Caster = "38BDF8"
        KeyW = @("F0F9FF", "FFFFFF"); KeyB = @("0A1733", "122552")
        BenchCushion = @("0A1733", "122552", "1C3B7E", "2854AC", "3B73DC")
        BenchTuft = "7DD3FC"; BenchTrim = "38BDF8"
        ShadowRgb = @(8, 16, 38)
        GlowColor = "00E5FF"
        GlowHi = "7DD3FC"
    },
    @{
        Id = 12; Rarity = "Epic"; RarityIdx = 3; VarIdx = 4; Name = "Epic 4 - Cyber Jade with Neon Emerald Edge Glow"
        Lid = @("07241A", "0E382A", "18543F", "257559", "349C77")
        Rim = @("041711", "07241A", "0E382A", "18543F", "257559")
        RimHi = "34D399"
        Soundboard = @("07241A", "0E382A", "18543F", "059669", "10B981")
        Strings = @("059669", "10B981", "34D399", "6EE7B7", "ECFDF5")
        Pinblock = "A7F3D0"; PropStick = "34D399"; Studs = "6EE7B7"
        Plaque = @("0E382A", "18543F", "34D399"); Caster = "10B981"
        KeyW = @("ECFDF5", "FFFFFF"); KeyB = @("07241A", "0E382A")
        BenchCushion = @("07241A", "0E382A", "18543F", "257559", "349C77")
        BenchTuft = "6EE7B7"; BenchTrim = "34D399"
        ShadowRgb = @(6, 24, 18)
        GlowColor = "10B981"
        GlowHi = "6EE7B7"
    },

    # ================= LEGENDARY (13 - 16) =================
    # Legendary rule: Reddish wood (Rosewood, Mahogany, Padauk, Cherry) with inlaid gold filigree & gold accents
    @{
        Id = 13; Rarity = "Legendary"; RarityIdx = 4; VarIdx = 1; Name = "Legendary 1 - Imperial Rosewood & Inlaid 24K Gold"
        Lid = @("3B0A0A", "5C1010", "851A1A", "B02525", "D93838") # Deep rich rosewood
        Rim = @("240606", "3B0A0A", "5C1010", "851A1A", "B02525")
        RimHi = "FEF08A" # Polished gold rim highlight
        Soundboard = @("3B1207", "5C1D0C", "852E14", "B3421E", "DE5C2E") # Warm natural spruce with amber undertone
        Strings = @("852E14", "B45309", "D97706", "FBBF24", "FEF08A") # Radiant gold strings
        Pinblock = "FEF08A"; PropStick = "FBBF24"; Studs = "FEF08A"
        Plaque = @("5C1010", "FBBF24", "FEF08A"); Caster = "FBBF24"
        KeyW = @("FFFBEB", "FFFFFF"); KeyB = @("240606", "3B0A0A")
        BenchCushion = @("3B0A0A", "5C1010", "851A1A", "B02525", "D93838")
        BenchTuft = "FEF08A"; BenchTrim = "FBBF24"
        ShadowRgb = @(32, 8, 8)
        WoodGrain = $true
        LegendaryStyle = "ROSEWOOD_GOLD"
    },
    @{
        Id = 14; Rarity = "Legendary"; RarityIdx = 4; VarIdx = 2; Name = "Legendary 2 - Royal Crimson Mahogany & Baroque Gold Filigree"
        Lid = @("450707", "6E0D0D", "9C1616", "CF2121", "F03A3A") # Crimson mahogany
        Rim = @("2B0404", "450707", "6E0D0D", "9C1616", "CF2121")
        RimHi = "FEF08A"
        Soundboard = @("451A03", "78350F", "B45309", "F59E0B", "FEF08A")
        Strings = @("B45309", "D97706", "FBBF24", "FEF08A", "FFFFFF")
        Pinblock = "FEF08A"; PropStick = "FEF08A"; Studs = "FEF08A"
        Plaque = @("6E0D0D", "FBBF24", "FEF08A"); Caster = "FEF08A"
        KeyW = @("FEFCE8", "FFFFFF"); KeyB = @("2B0404", "450707")
        BenchCushion = @("450707", "6E0D0D", "9C1616", "CF2121", "F03A3A")
        BenchTuft = "FEF08A"; BenchTrim = "FEF08A"
        ShadowRgb = @(36, 6, 6)
        WoodGrain = $true
        LegendaryStyle = "MAHOGANY_BAROQUE"
    },
    @{
        Id = 15; Rarity = "Legendary"; RarityIdx = 4; VarIdx = 3; Name = "Legendary 3 - Antique Padauk & Celestial Gold Inlay"
        Lid = @("421206", "691E0C", "942E15", "C24121", "E85C35") # Vibrant African Padauk reddish wood
        Rim = @("2B0A03", "421206", "691E0C", "942E15", "C24121")
        RimHi = "FEF08A"
        Soundboard = @("0F172A", "1E293B", "1E40AF", "0284C7", "38BDF8") # Royal lapis lazuli soundboard contrast
        Strings = @("D97706", "F59E0B", "FBBF24", "FEF08A", "FFFFFF")
        Pinblock = "FEF08A"; PropStick = "FBBF24"; Studs = "FEF08A"
        Plaque = @("691E0C", "FBBF24", "FEF08A"); Caster = "FBBF24"
        KeyW = @("FFFBEB", "FFFFFF"); KeyB = @("2B0A03", "421206")
        BenchCushion = @("1E3A8A", "1D4ED8", "2563EB", "3B82F6", "60A5FA")
        BenchTuft = "FEF08A"; BenchTrim = "FBBF24"
        ShadowRgb = @(34, 10, 6)
        WoodGrain = $true
        LegendaryStyle = "PADAUK_LAPIS"
    },
    @{
        Id = 16; Rarity = "Legendary"; RarityIdx = 4; VarIdx = 4; Name = "Legendary 4 - Imperial Cherrywood & Jade Gold Emblem"
        Lid = @("3B0D14", "5C1521", "852132", "B33045", "D9485F") # Dark cherry red wood
        Rim = @("24070C", "3B0D14", "5C1521", "852132", "B33045")
        RimHi = "FEF08A"
        Soundboard = @("064E3B", "047857", "059669", "10B981", "34D399") # Imperial jade soundboard contrast
        Strings = @("10B981", "34D399", "FBBF24", "FEF08A", "FFFFFF")
        Pinblock = "FEF08A"; PropStick = "FEF08A"; Studs = "FEF08A"
        Plaque = @("5C1521", "FBBF24", "FEF08A"); Caster = "FEF08A"
        KeyW = @("ECFDF5", "FFFFFF"); KeyB = @("24070C", "3B0D14")
        BenchCushion = @("064E3B", "047857", "059669", "10B981", "34D399")
        BenchTuft = "FEF08A"; BenchTrim = "FEF08A"
        ShadowRgb = @(30, 8, 12)
        WoodGrain = $true
        LegendaryStyle = "CHERRY_JADE"
    }
)

function SetPx($bmp, [int]$x, [int]$y, [string]$hex, [int]$a = 255) {
    if ($x -ge 0 -and $x -lt $bmp.Width -and $y -ge 0 -and $y -lt $bmp.Height) {
        $bmp.SetPixel($x, $y, (Hex $hex $a))
    }
}

# Helper to draw a tasteful Sheet Music booklet on the music desk
function Draw-SheetMusic($bmp) {
    # Music desk bounds approx x=32..38, y=18..30
    # Left page & Right page with delicate score lines
    $mx = 32 + $offX
    $my = 18 + $offY
    # Book backing
    for ($dy = 0; $dy -le 8; $dy++) {
        SetPx $bmp ($mx + 0) ($my + $dy) "292524"
        SetPx $bmp ($mx + 7) ($my + $dy) "292524"
    }
    # Open paper pages (white/cream)
    for ($dy = 1; $dy -le 7; $dy++) {
        for ($dx = 1; $dx -le 6; $dx++) {
            if ($dx -eq 3 -or $dx -eq 4) {
                SetPx $bmp ($mx + $dx) ($my + $dy) "D6D3D1" # Center fold shadow
            } else {
                SetPx $bmp ($mx + $dx) ($my + $dy) "FAFAF9" # Page
            }
        }
    }
    # Musical notation lines (dark grey 1px dashes)
    SetPx $bmp ($mx + 1) ($my + 2) "44403C"; SetPx $bmp ($mx + 2) ($my + 2) "44403C"
    SetPx $bmp ($mx + 1) ($my + 4) "44403C"; SetPx $bmp ($mx + 2) ($my + 4) "44403C"
    SetPx $bmp ($mx + 1) ($my + 6) "44403C"; SetPx $bmp ($mx + 2) ($my + 6) "44403C"
    SetPx $bmp ($mx + 5) ($my + 2) "44403C"; SetPx $bmp ($mx + 6) ($my + 2) "44403C"
    SetPx $bmp ($mx + 5) ($my + 4) "44403C"; SetPx $bmp ($mx + 6) ($my + 4) "44403C"
    SetPx $bmp ($mx + 5) ($my + 6) "44403C"; SetPx $bmp ($mx + 6) ($my + 6) "44403C"
}

# Helper to draw a wooden/brass metronome next to music desk
function Draw-Metronome($bmp) {
    # Place on case shelf (ox=40..43, oy=14..20)
    $mx = 41 + $offX
    $my = 14 + $offY
    # Pyramid casing (mahogany)
    SetPx $bmp ($mx + 1) ($my + 0) "78350F"
    SetPx $bmp ($mx + 1) ($my + 1) "92400E"
    SetPx $bmp ($mx + 0) ($my + 2) "78350F"; SetPx $bmp ($mx + 1) ($my + 2) "B45309"; SetPx $bmp ($mx + 2) ($my + 2) "78350F"
    SetPx $bmp ($mx + 0) ($my + 3) "78350F"; SetPx $bmp ($mx + 1) ($my + 3) "B45309"; SetPx $bmp ($mx + 2) ($my + 3) "78350F"
    SetPx $bmp ($mx + 0) ($my + 4) "451A03"; SetPx $bmp ($mx + 1) ($my + 4) "78350F"; SetPx $bmp ($mx + 2) ($my + 4) "451A03"
    # Pendulum arm & weight (brass/gold)
    SetPx $bmp ($mx + 1) ($my + 1) "FBBF24"
    SetPx $bmp ($mx + 2) ($my + 0) "FEF08A" # Pendulum tick weight
}

# Helper to draw twin brass candlestick sconces
function Draw-CandlestickSconces($bmp) {
    # Left sconce mounted near keyboard cheek (ox=23, oy=12..17)
    $lx = 23 + $offX
    $ly = 13 + $offY
    # Brass base & stem
    SetPx $bmp $lx ($ly + 3) "FBBF24"; SetPx $bmp ($lx + 1) ($ly + 3) "D97706"
    SetPx $bmp $lx ($ly + 2) "FBBF24"
    SetPx $bmp ($lx - 1) ($ly + 1) "FBBF24"; SetPx $bmp $lx ($ly + 1) "FEF08A"; SetPx $bmp ($lx + 1) ($ly + 1) "FBBF24"
    # White candle wax
    SetPx $bmp $lx $ly "F8FAFC"
    SetPx $bmp $lx ($ly - 1) "F8FAFC"
    # Warm flame
    SetPx $bmp $lx ($ly - 2) "F59E0B"
    SetPx $bmp $lx ($ly - 3) "FEF08A"

    # Right sconce on front case rim (ox=40, oy=10..15)
    $rx = 40 + $offX
    $ry = 11 + $offY
    SetPx $bmp $rx ($ry + 3) "FBBF24"; SetPx $bmp ($rx + 1) ($ry + 3) "D97706"
    SetPx $bmp $rx ($ry + 2) "FBBF24"
    SetPx $bmp ($rx - 1) ($ry + 1) "FBBF24"; SetPx $bmp $rx ($ry + 1) "FEF08A"; SetPx $bmp ($rx + 1) ($ry + 1) "FBBF24"
    SetPx $bmp $rx $ry "F8FAFC"
    SetPx $bmp $rx ($ry - 1) "F8FAFC"
    SetPx $bmp $rx ($ry - 2) "F59E0B"
    SetPx $bmp $rx ($ry - 3) "FEF08A"
}

# Helper to draw ornate music desk with brass filigree
function Draw-OrnateDesk($bmp, $colorHex) {
    # Music desk grid lines with elegant scrollwork notches
    $dx = 33 + $offX
    for ($dy = 14; $dy -le 34; $dy += 4) {
        SetPx $bmp $dx ($dy + $offY) $colorHex
        SetPx $bmp ($dx + 1) ($dy + $offY) "FEF08A"
    }
}

# Helper to draw velvet pedal lyre mat
function Draw-VelvetPedalMat($bmp, $matColorHex, $fringeHex) {
    # Floor area beneath pedals (x=36..42, y=63..66)
    $px = 37 + $offX
    $py = 63 + $offY
    for ($dy = 0; $dy -le 2; $dy++) {
        for ($dx = 0; $dx -le 6; $dx++) {
            SetPx $bmp ($px + $dx) ($py + $dy) $matColorHex
        }
    }
    # Gold fringe trim
    for ($dx = 0; $dx -le 6; $dx++) {
        SetPx $bmp ($px + $dx) ($py + 3) $fringeHex
    }
}

# Helper to apply clean glowing edge lights along the rim and lid for Epic
function Apply-EpicEdgeGlow($bmp, $glowColor, $glowHi) {
    # 1. Lid lower rim edge line
    for ($ox = 43; $ox -le 93; $ox++) {
        $ly = (Get-LidBottomY $ox) + $offY
        SetPx $bmp ($ox + $offX) $ly $glowColor
        # Soft accent dots along the contour
        if (($ox % 3) -eq 0) {
            SetPx $bmp ($ox + $offX) ($ly - 1) $glowHi
        }
    }
    # 2. Outer S-curve case rim lower molding line
    for ($ox = 24; $ox -le 94; $ox++) {
        $botY = -1
        for ($oy = 53; $oy -ge 20; $oy--) {
            $pMatch = $bmp.GetPixel($ox + $offX, $oy + $offY)
            if ($pMatch.A -eq 255) { $botY = $oy; break }
        }
        if ($botY -gt 20) {
            SetPx $bmp ($ox + $offX) ($botY + $offY) $glowColor
            if (($ox % 4) -eq 0) {
                SetPx $bmp ($ox + $offX) ($botY + $offY - 1) $glowHi
            }
        }
    }
    # 3. Soft desk edge line
    for ($oy = 14; $oy -le 36; $oy++) {
        SetPx $bmp (32 + $offX) ($oy + $offY) $glowColor
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

$outDirWorking = "I:\Shared drives\Pitch Quest Shared Drive\Working Files\Music Store\Export\Instruments"
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
                    $adjLum = [Math]::Min(1.0, $lum * 3.2)
                    $r = $v.Rim
                    $c = PaletteRamp $r[0] $r[1] $r[2] $r[3] $r[4] $adjLum $a
                }
            }
            $bmp.SetPixel($x, $y, $c)
        }

        # Subtly accent the lower rim edge for Common & Rare
        if ($v.RarityIdx -le 2) {
            for ($ox = 24; $ox -le 94; $ox++) {
                $botY = -1
                for ($oy = 53; $oy -ge 20; $oy--) {
                    $pMatch = $bmp.GetPixel($ox + $offX, $oy + $offY)
                    if ($pMatch.A -eq 255) { $botY = $oy; break }
                }
                if ($botY -gt 20) {
                    SetPx $bmp ($ox + $offX) ($botY + $offY - 1) $v.RimHi
                }
            }
        }

        # Apply Rare physical additions
        if ($v.RarityIdx -eq 2) {
            if ($v.RareItem -eq "SHEET_MUSIC_AND_METRONOME") {
                Draw-SheetMusic $bmp
                Draw-Metronome $bmp
            }
            elseif ($v.RareItem -eq "BRASS_CANDLESTICKS") {
                Draw-CandlestickSconces $bmp
                Draw-SheetMusic $bmp
            }
            elseif ($v.RareItem -eq "EMERALD_VELVET_MAT") {
                Draw-VelvetPedalMat $bmp "064E3B" "FBBF24"
                Draw-SheetMusic $bmp
            }
            elseif ($v.RareItem -eq "IVORY_SCORE_HOLDER") {
                Draw-OrnateDesk $bmp "FBBF24"
                Draw-Metronome $bmp
                Draw-SheetMusic $bmp
            }
        }

        # Apply Epic subtle glowing lights along the edges
        if ($v.RarityIdx -eq 3) {
            Apply-EpicEdgeGlow $bmp $v.GlowColor $v.GlowHi
        }

        # Apply Legendary reddish wood inlaid 24K gold filigree
        if ($v.RarityIdx -eq 4) {
            Apply-LegendaryGoldInlay $bmp
        }
    }

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
