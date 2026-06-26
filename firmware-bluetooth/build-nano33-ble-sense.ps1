param(
    [string]$DockerImage = "nordicplayground/nrfconnect-sdk:v2.2-branch"
)

$ErrorActionPreference = "Stop"

$firmwareDir = $PSScriptRoot
$repoRoot = Split-Path -Parent $firmwareDir
$overlay = "boards/arm/arduino_nano_33_ble/arduino_nano_33_ble_sense_lsm9ds1.overlay"

$dockerArgs = @(
    "run",
    "--rm",
    "-v", "${repoRoot}:/workdir/project",
    "-w", "/workdir/project/firmware-bluetooth",
    $DockerImage,
    "west", "build",
    "-b", "arduino_nano_33_ble_sense",
    "-p", "always",
    "--",
    "-DDTC_OVERLAY_FILE=$overlay"
)

& docker @dockerArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$binPath = Join-Path $firmwareDir "build\zephyr\remapper.bin"
if (Test-Path $binPath) {
    Write-Host "Built $binPath"
} else {
    throw "Build finished, but $binPath was not found."
}
