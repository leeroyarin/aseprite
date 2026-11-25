Add-Type -AssemblyName System.Drawing
$b = [System.Drawing.Bitmap]::FromFile('C:\Users\leero\.gemini\antigravity\brain\a90a33a4-cdd3-4ace-b0c9-7187f6a90745\Cello1.png')
$c = $b.GetPixel(0, 0)
Write-Host "Pixel 0,0: R=$($c.R), G=$($c.G), B=$($c.B), A=$($c.A)"
$b.Dispose()
