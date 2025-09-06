## Add gyroscopic control for Xiao BLE Sense Board 

### Overview

This change adds roll, pitch and shake support to the HID Remapper, enabling the Xiao BLE Sense IMU data as input sources. You can now:

* Control games with your feets on a balance board — similar to a 3D Rudder
* Add tilt‐ and shake‐based control to the Xbox Adaptive Controller and Adaptive joystick
* Trigger clicks via a quick tap (using shake threshold)

You can download the .uf2 firmware [here](https://github.com/squirelo/hid-remapper/actions/runs/16246750706/artifacts/3520810322)
You can try the new configuration website [here](https://squirelo.github.io/hid-remapper/)

### New Inputs

* **Roll** (X axis)
* **Pitch** (Y axis)
* **Shake** 

<img width="1472" height="387" alt="image" src="https://github.com/user-attachments/assets/db1512d2-5e78-44f0-89c4-ee5db99b7a04" />

### Configuration

1. **Enable IMU**
   Go to the “Settings” tab and toggle on **IMU support**.

2. **Adjust parameters**

   * **Angle limit**: Clamp maximum tilt angle
   * **Buffer size**: Smooth incoming data to reduce jitter
   * **Axis inversion**: Easy axis change without using expressions

<img width="1919" height="678" alt="image" src="https://github.com/user-attachments/assets/dbcfec3e-d6dc-47d2-bd06-bd1fb74eba64" />


### Example Profiles

Two new examples have been added in the config

* **IMU mouse control**

  * **Roll** → X cursor
  * **Pitch** → Y cursor
  * **Shake** → Left click

* **IMU Switch gamepad**

  * **Roll** → Left stick X-axis
  * **Pitch** → Left stick Y-axis
  * **Shake** → Button A
---



If anything looks off—or you’d like adjustments—please let me know!
