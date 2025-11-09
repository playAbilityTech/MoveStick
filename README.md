# OpenRudder — Open-source Balance Board that you can use as a joystick, a mouse or a WASD keyboard

**OpenRudder** turns a simple balance board into a USB **motion controller**.
Tilt with your feet to steer a **virtual joystick**, move the **mouse**, or press **keyboard/gamepad** buttons—no drivers, no proprietary boxes.

Built on the excellent [**HID Remapper by jfedor**](https://github.com/jfedor2/hid-remapper) project, with custom gyroscope support for the **Seeed XIAO BLE Sense**.
A collaboration between [**PlayAbility Adaptive Software**](https://www.playability.gg/) and [**Hitclic**](https://hitclic.shop/).

---

## Quick Links

* **Latest custom firmware (.uf2)** → [Download](https://github.com/squirelo/hid-remapper/actions/runs/16246750706/artifacts/3520810322)
* **Web configuration** → [https://playabilitytech.github.io/OpenRudder/](https://playabilitytech.github.io/OpenRudder/)

> Enumerates as standard **USB HID** (mouse, keyboard, gamepad) on Windows, macOS, and Linux.

---

## What You Can Do

* **Feet-controlled joystick** — map **Roll**/**Pitch** to a gamepad stick (e.g., left stick X/Y)
* **Motion mouse** — tilt to move the pointer, **tap** to click (shake threshold)
* **Hands-free buttons** — use tilt ranges or shakes to trigger **A/B/X/Y**, **L/R**, **space**, **clicks**, etc.
* **Adaptive setups** — pair with the Xbox Adaptive Controller, wheelchair joysticks, or other assistive gear

---

## How It Works

1. A **Seeed XIAO BLE Sense** reads **roll/pitch** (and detects **shake**) via its IMU.
2. **HID Remappe**by jfedo****r** firmware exposes those signals as s ****HID inputs** (mouse, keyboard, or gamepad).
3. You tailor mappings in the **Web Config** and tune **angle limit**, **smoothing**, and **inversion**.

---

## Hardware

Build OpenRudder with common parts:

* **Microcontroller**: Seeed **XIAO BLE Sense** (nRF52840, with IMU)
* **Balance board**:  buy one at your sport shop or on ama[zon ](https://amzn.to/47KK38L)[https://amzn.to/47KK38L](https://amzn.to/47KK38L)

> Tip: Place the IMU close to the **rotation center** for the smoothest feel.

---

## Flash the Firmware (.uf2)

1. **Enter bootloader** on the XIAO BLE Sense
   Hold **BOOT** while pressing Reset button (or double-tap ****RESET**). A USB drive appears.
2. **Drag & drop** the downloaded `.uf2` onto that drive.
3. The board reboots and enumerates as a **USB HID** device.

If the drive doesn’t appear, repeat the BOOT/RESET step or try another cable/port.

---

## Configure & Map

Open the **Web Confi**[**g**](https://playabilitytech.github.io/OpenRudder/)[ → ](https://playabilitytech.github.io/OpenRudder/)[https://playabilitytech.github.io/OpenRudder/](https://playabilitytech.github.io/OpenRudder/)

### Enable IMU

* **Settings → IMU support** → toggle **ON**

### Tune Feel

* **Angle limit** — caps max tilt so you don’t saturate a stick axis
* **Buffer size (smoothing)** — more = smoother, but slightly higher latency
* **Axis inversion** — flip X/Y without editing expressions
* **Shake threshold** — adjust tap sensitivity for clicks/buttons

### Map Signals → Outputs

* **Roll (X)** / **Pitch (Y)** → **mouse X/Y**, **left stick X/Y**, or **keys**
* **Shake** → **left click**, **A**, **space**, etc.

Screenshots:

---

## Ready-Made Profiles

* **IMU → Mouse**

  * Roll → Cursor **X**
  * Pitch → Cursor **Y**
  * Shake → **Left click**

* **IMU → Gamepad (Switch/XInput-style)**

  * Roll → **Left stick X**
  * Pitch → **Left stick Y**
  * Shake → **Button A**
