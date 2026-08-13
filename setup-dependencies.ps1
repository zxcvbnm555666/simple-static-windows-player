[CmdletBinding()]
param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$releaseVersion = "v1.0.0"
$assetName = "ffmpeg-sdl-static-win64.zip"
$assetUrl = "https://github.com/zxcvbnm555666/simple-static-windows-player/releases/download/$releaseVersion/$assetName"
$expectedSha256 = "871dbe80e70950956859c36180d7051d96671838d55ad4bc4fba442758367661"
$libraryDirectory = Join-Path $PSScriptRoot "msvc\lib\x64"
$requiredLibraries = @(
    "libavdevice.lib",
    "libavfilter.lib",
    "libavformat.lib",
    "libavcodec.lib",
    "libpostproc.lib",
    "libswresample.lib",
    "libswscale.lib",
    "libavutil.lib",
    "libsdl2.lib"
)

$allLibrariesPresent = Test-Path $libraryDirectory
if ($allLibrariesPresent) {
    foreach ($library in $requiredLibraries) {
        if (-not (Test-Path (Join-Path $libraryDirectory $library))) {
            $allLibrariesPresent = $false
            break
        }
    }
}

if ($allLibrariesPresent -and -not $Force) {
    Write-Host "Static dependencies are already installed."
    exit 0
}

$archivePath = Join-Path ([System.IO.Path]::GetTempPath()) $assetName

try {
    Write-Host "Downloading static FFmpeg and SDL2 libraries..."
    Invoke-WebRequest -Uri $assetUrl -OutFile $archivePath -UseBasicParsing

    $actualSha256 = (Get-FileHash $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualSha256 -ne $expectedSha256) {
        throw "SHA-256 mismatch. Expected $expectedSha256 but received $actualSha256."
    }

    Write-Host "Extracting dependencies..."
    Expand-Archive -Path $archivePath -DestinationPath $PSScriptRoot -Force

    foreach ($library in $requiredLibraries) {
        $libraryPath = Join-Path $libraryDirectory $library
        if (-not (Test-Path $libraryPath)) {
            throw "The release archive does not contain $library."
        }
    }

    Write-Host "Static dependencies installed successfully."
}
finally {
    if (Test-Path $archivePath) {
        Remove-Item $archivePath -Force
    }
}
