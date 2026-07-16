$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Assert-True([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

$manifest = Import-Csv (Join-Path $root 'assets\palette_tiers.csv') |
    Where-Object { $_.group -and -not $_.group.StartsWith('#') -and $_.unit_id }
$icons = @(
    Import-Csv (Join-Path $root 'assets\unit_icon_map.csv')
    Import-Csv (Join-Path $root 'assets\unreleased_unit_icon_map.csv')
)
$published = Import-Csv (Join-Path $root 'assets\enemy_stats.csv')
$runtime = Import-Csv (Join-Path $root 'assets\enemy_runtime_stats.csv')

$expectedCounts = [ordered]@{
    village = 37
    graveyard = 41
    volcano = 38
    dark_realm = 43
    sands = 91
    boss = 30
    endless = 9
}

foreach ($group in $expectedCounts.Keys) {
    $rows = @($manifest | Where-Object group -eq $group)
    Assert-True ($rows.Count -eq $expectedCounts[$group]) "$group contains $($rows.Count) entries; expected $($expectedCounts[$group])."
    Assert-True (($rows.unit_id | Sort-Object -Unique).Count -eq $rows.Count) "$group contains duplicate unit IDs."
}

$bosses = @($manifest | Where-Object group -eq 'boss' | Select-Object -ExpandProperty unit_id)
Assert-True ($bosses -contains 'ogre_boss') 'Throg (ogre_boss) is missing from the Boss group.'
Assert-True (@($manifest | Where-Object { $_.unit_id -eq 'monkey_king_clone' -and $_.group -eq 'sands' }).Count -eq 1) 'Monkey King''s Clone is missing from Sands.'

$releasedGroups = @('village', 'graveyard', 'volcano', 'dark_realm', 'sands', 'boss', 'endless')
$released = @($manifest | Where-Object { $_.group -in $releasedGroups } | Select-Object -ExpandProperty unit_id | Sort-Object -Unique)
Assert-True ($released.Count -eq 172) "Released palette union contains $($released.Count) units; expected 172."

$iconByUnit = @{}
foreach ($row in $icons) { $iconByUnit[$row.unit_id] = $row.file }
$statsUnits = @($published.unit_id + $runtime.unit_id | Sort-Object -Unique)
foreach ($unit in $released) {
    Assert-True ($statsUnits -contains $unit) "No published or runtime stats exist for $unit."
    Assert-True ($iconByUnit.ContainsKey($unit)) "No icon mapping exists for $unit."
    Assert-True (Test-Path (Join-Path (Join-Path $root 'assets') $iconByUnit[$unit])) "Mapped icon file is missing for $unit."
}

$fallbackExpected = [ordered]@{
    corrupted_bumblebee = 6.25
    corrupted_peasant = 10.0
    fenec_spearman = 15.56
    moth_fencer = 20.0
    tentacle_hitter = 7.81
    monkey_king_clone = 31.25
}
foreach ($unit in $fallbackExpected.Keys) {
    $publishedRow = $published | Where-Object unit_id -eq $unit | Select-Object -First 1
    $runtimeRow = $runtime | Where-Object unit_id -eq $unit | Select-Object -First 1
    Assert-True ($null -ne $runtimeRow) "Runtime stats are missing for DPS fallback unit $unit."
    Assert-True ($null -eq $publishedRow -or [string]::IsNullOrWhiteSpace($publishedRow.dps)) "Published DPS should remain authoritative for $unit."
    $derived = [double]::Parse($runtimeRow.attack_damage, [Globalization.CultureInfo]::InvariantCulture) *
        [double]::Parse($runtimeRow.attacks_per_second, [Globalization.CultureInfo]::InvariantCulture)
    Assert-True ([Math]::Abs($derived - $fallbackExpected[$unit]) -lt 0.02) "Unexpected derived DPS for ${unit}: $derived."
}

Write-Host "Asset validation passed: $($released.Count) released units, $($bosses.Count) boss IDs, complete icons/stats, verified DPS fallbacks."
