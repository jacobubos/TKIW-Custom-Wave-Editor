$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$cases = @(
    @{ Name = '720p-150'; Width = 1280; Height = 720; Dpi = 144 },
    @{ Name = '1080p-100'; Width = 1920; Height = 1080; Dpi = 96 },
    @{ Name = '1080p-125'; Width = 1920; Height = 1080; Dpi = 120 },
    @{ Name = '1080p-150'; Width = 1920; Height = 1080; Dpi = 144 },
    @{ Name = '1080p-200'; Width = 1920; Height = 1080; Dpi = 192 },
    @{ Name = '1440p-100'; Width = 2560; Height = 1440; Dpi = 96 },
    @{ Name = '1440p-125'; Width = 2560; Height = 1440; Dpi = 120 },
    @{ Name = 'ultrawide-125'; Width = 3440; Height = 1440; Dpi = 120 },
    @{ Name = '4k-150'; Width = 3840; Height = 2160; Dpi = 144 },
    @{ Name = '4k-200'; Width = 3840; Height = 2160; Dpi = 192 }
)

$toolbarWidths = 24, 92, 168, 176, 176
$toolbarGap = 10
$toolbarRightMargin = 16
$titleRight = 378

foreach ($case in $cases) {
    $dpiScale = [Math]::Max(96, $case.Dpi) / 96.0
    $resolutionScale = [Math]::Min($case.Width / 1920.0, $case.Height / 1080.0)
    $desired = [Math]::Max($dpiScale, $resolutionScale)
    $canvasCap = [Math]::Min($case.Width / 1100.0, $case.Height / 650.0)
    $scale = [Math]::Min(2.0, [Math]::Max(0.75, [Math]::Min($desired, $canvasCap)))
    $logicalWidth = $case.Width / $scale
    $logicalHeight = $case.Height / $scale

    Assert-True ($logicalWidth -ge 1099.5) "$($case.Name): logical width is too small ($logicalWidth)."
    Assert-True ($logicalHeight -ge 649.5) "$($case.Name): logical height is too small ($logicalHeight)."
    $toolbarLeft = $logicalWidth - $toolbarRightMargin - ($toolbarWidths | Measure-Object -Sum).Sum -
        ($toolbarWidths.Count * $toolbarGap)
    Assert-True ($toolbarLeft -gt $titleRight) "$($case.Name): toolbar overlaps the editor title."

    $paletteWidth = [Math]::Min(800, [Math]::Max(400, $logicalWidth * 0.32))
    $timelineWidth = $logicalWidth - $paletteWidth - 54
    $waveColumns = [Math]::Min(6, [Math]::Max(1, [Math]::Floor(($timelineWidth - 13) / 160)))
    Assert-True ($waveColumns -ge 3) "$($case.Name): fewer than three wave columns fit."

    # Timeline title/page controls: 1..26; wave title: 34..51;
    # editable week controls: 55..74; wave cards: 78..
    Assert-True (26 -lt 34 -and 51 -lt 55 -and 74 -lt 78) "$($case.Name): timeline header bands overlap."

    $contentHeight = $logicalHeight - 157
    $columns = if ($paletteWidth -ge 700) { 5 } elseif ($paletteWidth -ge 520) { 4 } elseif ($paletteWidth -ge 360) { 3 } else { 2 }
    $tileWidth = ($paletteWidth - (($columns - 1) * 6)) / $columns
    $tileHeight = [Math]::Min(92, [Math]::Max(78, $tileWidth * 0.68))
    $paletteRows = [Math]::Floor(($contentHeight - 172) / ($tileHeight + 6))
    Assert-True ($paletteRows -ge 3) "$($case.Name): stat cards leave fewer than three palette rows."
}

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
$font = [Drawing.Font]::new('Segoe UI', 15, [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
try {
    $buttonWidths = @{ 'COPY CHALLENGE' = 176; 'PASTE CHALLENGE' = 176; 'RESTORE DEFAULTS' = 168 }
    foreach ($entry in $buttonWidths.GetEnumerator()) {
        $measured = [Windows.Forms.TextRenderer]::MeasureText($entry.Key, $font).Width
        Assert-True ($measured -le $entry.Value - 12) "$($entry.Key) needs $measured px plus padding but has only $($entry.Value) px."
    }
}
finally {
    $font.Dispose()
}

$source = Get-Content (Join-Path $PSScriptRoot '..\src\ModuleMain.cpp') -Raw
Assert-True ($source.Contains('"UNRELEASED"')) 'The full UNRELEASED tab label is missing.'
Assert-True ($source.Contains('"DMG "')) 'Raw damage is missing from enemy cards.'
Assert-True ($source.Contains('"DPS "')) 'DPS is missing from enemy cards.'

Write-Host "Responsive layout validation passed for $($cases.Count) resolution/DPI combinations."
