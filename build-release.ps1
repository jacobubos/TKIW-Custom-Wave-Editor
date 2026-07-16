[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root 'build'
$dependencies = Join-Path $build 'dependencies'
$payload = Join-Path $build 'payload'
$dist = Join-Path $root 'dist'
$setup = Join-Path $dist 'TKIW-Custom-Wave-Editor-Setup.exe'
$manualZip = Join-Path $dist 'TKIW-Custom-Wave-Editor-Manual-Install.zip'

function Download-VerifiedFile {
    param(
        [Parameter(Mandatory = $true)][string]$Uri,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string]$Sha256
    )
    Invoke-WebRequest -UseBasicParsing -Uri $Uri -OutFile $Destination
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $Destination).Hash
    if ($actual -ne $Sha256) {
        throw "Hash mismatch for $Destination. Expected $Sha256 but got $actual."
    }
}

Remove-Item -LiteralPath $build,$dist -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $dependencies,$payload,$dist | Out-Null

Download-VerifiedFile `
    'https://github.com/AurieFramework/Aurie/releases/download/v2.0.2/AuriePatcher.exe' `
    (Join-Path $dependencies 'AuriePatcher.exe') `
    '4D3AEC439DBBA5209AD48FB0A40D6324406247FE678541DF9031F5B89363D536'
Download-VerifiedFile `
    'https://github.com/AurieFramework/Aurie/releases/download/v2.0.2/AurieCore.dll' `
    (Join-Path $dependencies 'AurieCore.dll') `
    '18E3A1DE980F487A6B3858B673D2030E96984DD96DE3A047B43A263A5BA829AE'
Download-VerifiedFile `
    'https://github.com/AurieFramework/YYToolkit/releases/download/v4.0.1/YYToolkit.dll' `
    (Join-Path $dependencies '00_YYToolkit.dll') `
    'D031963A41C177D2B97B0F1870D2613428ACA336AC21E41517D7D96C8FA9CECF'

$msbuildCandidates = @(
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe',
    (Get-Command msbuild.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1)
)
$msbuild = $msbuildCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $msbuild) { throw 'Visual Studio 2022 MSBuild was not found.' }

& $msbuild (Join-Path $root 'CustomWaveEditor.vcxproj') /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m
if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE." }

$builtDll = Join-Path $root 'x64\Release\CustomWaveEditor.dll'
if (-not (Test-Path -LiteralPath $builtDll)) { throw 'CustomWaveEditor.dll was not produced.' }
Copy-Item -LiteralPath (Join-Path $dependencies 'AuriePatcher.exe') -Destination $payload
Copy-Item -LiteralPath (Join-Path $dependencies 'AurieCore.dll') -Destination $payload
Copy-Item -LiteralPath (Join-Path $dependencies '00_YYToolkit.dll') -Destination $payload
Copy-Item -LiteralPath $builtDll -Destination (Join-Path $payload '10_CustomWaveEditor.dll')
Copy-Item -LiteralPath (Join-Path $root 'assets') -Destination (Join-Path $payload 'CustomWaveEditorAssets') -Recurse
New-Item -ItemType Directory -Path (Join-Path $payload 'licenses') | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination (Join-Path $payload 'licenses\CustomWaveEditor-AGPL-3.0.txt')
Copy-Item -LiteralPath (Join-Path $root 'THIRD_PARTY_NOTICES.md') -Destination (Join-Path $payload 'licenses\THIRD_PARTY_NOTICES.md')
Copy-Item -LiteralPath (Join-Path $root 'third-party\Aurie-LICENSE.txt') -Destination (Join-Path $payload 'licenses\Aurie-AGPL-3.0.txt')
Copy-Item -LiteralPath (Join-Path $root 'third-party\YYToolkit-LICENSE.txt') -Destination (Join-Path $payload 'licenses\YYToolkit-AGPL-3.0.txt')

$manualPackage = Join-Path $build 'manual-package'
$manualMods = Join-Path $manualPackage 'mods\aurie'
New-Item -ItemType Directory -Path $manualMods | Out-Null
Copy-Item -LiteralPath (Join-Path $dependencies '00_YYToolkit.dll') -Destination $manualMods
Copy-Item -LiteralPath $builtDll -Destination (Join-Path $manualMods '10_CustomWaveEditor.dll')
Copy-Item -LiteralPath (Join-Path $root 'assets') -Destination (Join-Path $manualMods 'CustomWaveEditorAssets') -Recurse
Copy-Item -LiteralPath (Join-Path $payload 'licenses') -Destination (Join-Path $manualMods 'CustomWaveEditorLicenses') -Recurse
Copy-Item -LiteralPath (Join-Path $root 'MANUAL_INSTALL.txt') -Destination $manualPackage
Compress-Archive -Path (Join-Path $manualPackage '*') -DestinationPath $manualZip -CompressionLevel Optimal

$payloadZip = Join-Path $build 'payload.zip'
Compress-Archive -Path (Join-Path $payload '*') -DestinationPath $payloadZip -CompressionLevel Optimal

$cscCandidates = @(
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\Roslyn\csc.exe',
    'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe'
)
$csc = $cscCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $csc) { throw 'The C# compiler was not found.' }

& $csc /nologo /target:winexe /platform:anycpu /optimize+ `
    "/win32manifest:$root\installer\app.manifest" `
    "/resource:$payloadZip,TKIWCustomWaveEditor.Payload.zip" `
    /reference:System.dll /reference:System.Core.dll /reference:System.Windows.Forms.dll `
    /reference:System.IO.Compression.dll /reference:System.IO.Compression.FileSystem.dll `
    "/out:$setup" (Join-Path $root 'installer\Installer.cs')
if ($LASTEXITCODE -ne 0) { throw "Installer compilation failed with exit code $LASTEXITCODE." }

$hashLines = foreach ($releaseFile in @($setup, $manualZip)) {
    $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $releaseFile
    "$($hash.Hash)  $([IO.Path]::GetFileName($releaseFile))"
}
$hashLines | Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt') -Encoding ascii
Write-Host "Built $setup"
Write-Host "Built $manualZip"
Write-Host ($hashLines -join [Environment]::NewLine)
