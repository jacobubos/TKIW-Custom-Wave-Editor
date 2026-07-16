$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root 'dist'
$zip = Join-Path $dist 'TKIW-Custom-Wave-Editor-Manual-Install.zip'
$setup = Join-Path $dist 'TKIW-Custom-Wave-Editor-Setup.exe'
$checksums = Join-Path $dist 'SHA256SUMS.txt'

function Assert-True([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

foreach ($file in @($zip, $setup, $checksums)) {
    Assert-True (Test-Path $file) "Release output is missing: $file"
    Assert-True ((Get-Item $file).Length -gt 0) "Release output is empty: $file"
}

$expectedHashes = @{}
foreach ($line in Get-Content $checksums) {
    if ($line -match '^([0-9A-Fa-f]{64})\s+(.+)$') { $expectedHashes[$matches[2]] = $matches[1].ToUpperInvariant() }
}
foreach ($file in @($zip, $setup)) {
    $name = Split-Path -Leaf $file
    Assert-True ($expectedHashes.ContainsKey($name)) "SHA256SUMS.txt has no entry for $name."
    Assert-True ((Get-FileHash $file -Algorithm SHA256).Hash -eq $expectedHashes[$name]) "Checksum mismatch for $name."
}

$setupPayloadRoot = Join-Path ([IO.Path]::GetTempPath()) ("TKIW-WaveEditor-SetupPayload-" + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $setupPayloadRoot | Out-Null
    $payloadZip = Join-Path $setupPayloadRoot 'payload.zip'
    $assembly = [Reflection.Assembly]::LoadFile((Resolve-Path $setup))
    $stream = $assembly.GetManifestResourceStream('TKIWCustomWaveEditor.Payload.zip')
    Assert-True ($null -ne $stream) 'Setup executable is missing its embedded payload.'
    try {
        $fileStream = [IO.File]::Create($payloadZip)
        try { $stream.CopyTo($fileStream) } finally { $fileStream.Dispose() }
    }
    finally { $stream.Dispose() }
    $payload = Join-Path $setupPayloadRoot 'payload'
    Expand-Archive $payloadZip -DestinationPath $payload
    foreach ($relative in @('AuriePatcher.exe', 'AurieCore.dll', '00_YYToolkit.dll', '10_CustomWaveEditor.dll', 'CustomWaveEditorAssets\palette_tiers.csv')) {
        Assert-True (Test-Path (Join-Path $payload $relative)) "Setup payload is missing $relative."
    }
    Assert-True ((Get-FileHash (Join-Path $payload '10_CustomWaveEditor.dll')).Hash -eq
        (Get-FileHash (Join-Path $root 'x64\Release\CustomWaveEditor.dll')).Hash) 'Setup embeds a stale Wave Editor DLL.'
}
finally {
    if (Test-Path $setupPayloadRoot) { Remove-Item $setupPayloadRoot -Recurse -Force }
}

$temporary = Join-Path ([IO.Path]::GetTempPath()) ("TKIW-WaveEditor-PackageTest-" + [guid]::NewGuid().ToString('N'))
try {
    Expand-Archive $zip -DestinationPath $temporary
    $modRoot = Join-Path $temporary 'mods\aurie'
    $required = @(
        '00_YYToolkit.dll',
        '10_CustomWaveEditor.dll',
        'CustomWaveEditorAssets\palette_tiers.csv',
        'CustomWaveEditorAssets\enemy_stats.csv',
        'CustomWaveEditorAssets\enemy_runtime_stats.csv',
        'CustomWaveEditorAssets\icons\monkey_king_clone.png',
        'CustomWaveEditorAssets\icons\sharpshooter_spike_1.png'
    )
    foreach ($relative in $required) {
        $path = Join-Path $modRoot $relative
        Assert-True (Test-Path $path) "Manual package is missing $relative."
        Assert-True ((Get-Item $path).Length -gt 0) "Manual package contains an empty required file: $relative."
    }
    Assert-True (-not (Test-Path (Join-Path $modRoot 'AurieCore.dll'))) 'Manual package must not silently bundle AurieCore.'
    Assert-True (-not (Get-ChildItem $temporary -Recurse -File | Where-Object Name -match '\.(pdb|lib|exp|obj)$|\.before-|\.backup$')) 'Manual package contains build or backup artifacts.'

    $packagedManifest = Import-Csv (Join-Path $modRoot 'CustomWaveEditorAssets\palette_tiers.csv') |
        Where-Object { $_.group -and -not $_.group.StartsWith('#') }
    Assert-True (@($packagedManifest | Where-Object group -eq 'boss').Count -eq 30) 'Packaged Boss group does not match validated source data.'
    Assert-True (@($packagedManifest | Where-Object group -eq 'sands').Count -eq 91) 'Packaged Sands group does not match validated source data.'
}
finally {
    if (Test-Path $temporary) { Remove-Item $temporary -Recurse -Force }
}

Write-Host 'Release package validation passed: hashes, minimal contents, DLL/assets, and packaged classification data.'
