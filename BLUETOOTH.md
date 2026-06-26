# Bluetooth version

_(Please note this is experimental and hasn't been tested with a lot of devices.)_

There's a special version of the remapper that takes inputs from Bluetooth devices and translates them to USB. It's different from most Bluetooth USB dongles in that from the computer's point of view it is a USB mouse/keyboard, so it requires no special drivers. The remapping functionality works as usual. You can connect multiple devices to it at the same time.

**It only works with Bluetooth Low Energy devices, not Bluetooth Classic.** You can usually tell that a device uses Bluetooth LE if the documentation says it requires Bluetooth version 4 or 5, but some devices say they use Bluetooth 4 or 5 and still use Bluetooth Classic (which technically isn't wrong as Bluetooth Classic is still part of the newer spec).

![HID Remapper Bluetooth](images/bluetooth.jpg)

The Bluetooth version of the remapper is available for Nordic nRF52840 boards and for Raspberry Pi Pico W / Pico 2 W.

### nRF52840 boards (physical BLE gamepads)

Precompiled binaries are available for:

* [Adafruit Feather nRF52840 Express](https://www.adafruit.com/product/4062)
* [Seeed Studio Xiao nRF52840](https://www.seeedstudio.com/Seeed-XIAO-BLE-nRF52840-p-5201.html)

To flash the [nRF firmware](firmware-bluetooth) on Adafruit Feather or Seeed Xiao boards, first put the board in flashing mode by double clicking the reset button quickly. A drive should appear on your computer. Copy the [UF2 file that matches your board](https://github.com/jfedor2/hid-remapper/releases/latest) to that drive and that's it. If you want to flash a newer version of the firmware in the future, you can also put the board in firmware flashing mode using the HID Remapper [web configuration tool](https://www.remapper.org/config/).

Arduino Nano 33 BLE Sense boards use Arduino's bossac bootloader instead of a UF2 mass-storage drive. Double tap reset until the orange LED pulses, then flash the built `remapper.bin` with the Arduino version of `bossac.exe`.

For local Nano 33 BLE Sense builds on Windows:

```powershell
.\firmware-bluetooth\build-nano33-ble-sense.ps1
.\firmware-bluetooth\flash-nano33-ble-sense.ps1
```

The flash script expects Arduino IDE plus the Arduino Mbed OS Nano Boards package to be installed so it can find Arduino's `bossac.exe`.

To connect Bluetooth devices to the remapper, put the device in pairing mode, then put HID Remapper in pairing mode by pressing the user switch button or clicking **Pair new device** in the web configuration tool (on Seeed Xiao, short pin 0 to GND: short press to pair, hold > 3 s to forget all). The remapper automatically enters pairing mode if no devices are paired.

You can tell the remapper is in pairing mode if the onboard LED is lit constantly. When it's not in pairing mode, the LED blinks, with the number of blinks per cycle corresponding to the number of currently connected devices.

To make the remapper forget all currently paired devices, hold the user switch button for over 3 seconds, or click **Forget all devices** in the web configuration tool (or short the pairing pin to GND for over 3 seconds on the Seeed Xiao).

### Raspberry Pi Pico W / Pico 2 W (NUS only)

Precompiled binaries are available for:

* [Raspberry Pi Pico W](https://www.raspberrypi.com/products/raspberry-pi-pico/)
* [Raspberry Pi Pico 2 W](https://www.raspberrypi.com/products/raspberry-pi-pico-2/)

To flash the [Pico firmware](firmware), hold the BOOTSEL button, plug the board into USB, and release BOOTSEL. A drive should appear on your computer. Copy the `remapper_bluetooth_pico_w.uf2` or `remapper_bluetooth_pico2_w.uf2` file from the [latest release](https://github.com/jfedor2/hid-remapper/releases/latest) to that drive.

To build from source, initialize the Pico SDK nested submodules (BTstack, CYW43 driver, etc.) and Python 3 must be on your PATH (used to generate the GATT header):

```bash
git submodule update --init --recursive firmware/pico-sdk
cd firmware && mkdir -p build-pico_w && cd build-pico_w
PICO_BOARD=pico_w cmake .. && make remapper_bluetooth
```

For Pico 2 W, use `PICO_BOARD=pico2_w` instead.

The Pico W firmware provides **Web Bluetooth NUS input only**. It does not scan for or pair physical BLE gamepads — the CYW43439 radio cannot reliably run BLE central and BLE peripheral at the same time. Use an nRF52840 board for physical controller pairing.

The onboard LED flashes briefly when NUS input is received.

## BLE GATT peripheral input (NUS)

The Bluetooth firmware advertises a Nordic UART Service compatible GATT
peripheral. Reports written to this service are treated as a separate virtual
input device, so they can be remapped together with connected BLE HID devices
on nRF52840 boards.

* Service UUID: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
* Write characteristic: `6e400002-b5a3-f393-e0a9-e50e24dcca9e`
* Notify characteristic: `6e400003-b5a3-f393-e0a9-e50e24dcca9e`

The write characteristic requires an encrypted BLE connection, so clients may
need to pair before reports can be written.

Writes are SLIP-style framed packets with a little-endian CRC32 trailer:
start `0xc0`, escaped payload bytes, four CRC bytes, end `0xc0`.
The decoded payload is:

| Byte | Meaning |
| ---: | --- |
| 0 | Protocol version, currently `1` |
| 1 | Advisory output descriptor number |
| 2 | HID report payload length |
| 3 | HID report ID |
| 4..N | HID report payload bytes, without the report ID |

The descriptor number in the packet does not switch the USB descriptor at
runtime. Choose the USB descriptor through the normal HID Remapper configuration
so it is active when the board enumerates over USB.

The [web configuration tool](config-tool-web) has a **Bluetooth** tab with a NUS
tester that connects over Web Bluetooth and sends test gamepad reports. On
nRF52840 boards the same tab also has **Pair new device** and **Forget all
devices** for physical BLE controllers.

## Troubleshooting pairing (nRF52840 only)

### Onboard LED meaning

| LED pattern | Meaning |
|-------------|---------|
| **Solid ON** | Scanning for a **new** device to pair |
| **Blinking** | Reconnecting to bonded devices, or idle with no connections |
| **N blinks per cycle** | N BLE input devices currently connected |

If you click **Pair new device** and the LED **keeps blinking** instead of going solid, the remapper is not in new-device pairing mode. Click **Forget all devices**, unplug/replug the board, then try again.

### Stale bonds

If a previous pairing attempt failed or the controller address changed (common with Xbox controllers in Sync pairing mode), use **Forget all devices** in the web tool, or hold the pairing pin to GND for more than 3 seconds on the Seeed Xiao.

### Xbox controller (Bluetooth LE mode)

Use the controller's **Sync** button pairing mode (Xbox button blinking rapidly), not USB. Put the controller in pairing mode, click **Pair new device** on the remapper, and wait for the LED to go solid. After a successful connection the LED blinks once per cycle.

## Known issues

* Quirks mechanism for fixing broken report descriptors doesn't work.
* Reconnects could be faster if we cached attributes/report descriptor.
