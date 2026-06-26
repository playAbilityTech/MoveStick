param(
    [string]$Port,
    [string]$Bossac,
    [string]$Bin = (Join-Path $PSScriptRoot "build\zephyr\remapper.bin")
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $Bin)) {
    throw "Firmware image not found: $Bin. Run build-nano33-ble-sense.ps1 first."
}

if (-not $Bossac) {
    $bossacRoot = Join-Path $env:LOCALAPPDATA "Arduino15\packages\arduino\tools\bossac"
    $bossacCandidates = @(Get-ChildItem $bossacRoot -Recurse -Filter bossac.exe -ErrorAction SilentlyContinue | Sort-Object FullName -Descending)
    if ($bossacCandidates.Count -eq 0) {
        throw "bossac.exe was not found. Install Arduino IDE and the Arduino Mbed OS Nano Boards package, or pass -Bossac C:\path\to\bossac.exe."
    }
    $Bossac = $bossacCandidates[0].FullName
}

if (-not $Port) {
    Write-Host "Double-tap RESET on the Nano 33 BLE Sense now, then wait for the orange LED to pulse."
    Start-Sleep -Seconds 2

    $ports = @(Get-CimInstance Win32_SerialPort |
        Where-Object { $_.PNPDeviceID -match "VID_2341" -or $_.Description -match "Arduino" } |
        Select-Object -ExpandProperty DeviceID)

    if ($ports.Count -eq 1) {
        $Port = $ports[0]
    } elseif ($ports.Count -eq 0) {
        throw "Could not find an Arduino serial port. Double-tap RESET and rerun, or pass -Port COMx."
    } else {
        throw "Multiple Arduino serial ports found: $($ports -join ', '). Rerun with -Port COMx."
    }
}

& $Bossac -d "--port=$Port" -U -i -e -w $Bin -R
exit $LASTEXITCODE
