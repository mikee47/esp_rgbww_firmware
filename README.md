# ESP RGBWW Firmware
## Firmware for RGBWW controller
This repository provides an open-source firmware for ESP8266-based RGBWWCW controllers (up to 5 channels). The firmware is based on Sming (https://github.com/SmingHub/Sming).

This firmware is a fork of Patrick Jahns original firmware (https://github.com/patrickjahns/esp_rgbww_firmware). Thanks for founding it!

# General Notes
This is a firmware modification based on the great RGBWWLed firmware from Patrick Jahns (https://github.com/patrickjahns/esp_rgbww_firmware). Big thanks to Patrick for his work.

The firmware has generic APIs and can be integrated into various systems. For the home automation system `FHEM` a device module is readily available:
[https://github.com/verybadsoldier/esp_rgbww_fhemmodule](https://github.com/verybadsoldier/esp_rgbww_fhemmodule)


## Features
 * Smooth and programmable on-board fades and animations
 * Independent animation channels
 * Suitable for different PCBs (easily configurable by config options)
 * Highly configurable
 * Various network communication options: HTTP - MQTT - TCP (events only)
 * Highly accurate synchronization of multiple controllers
 * [Easy setup and configuration via a feature rich webapplication]
 * [OTA updates]
 * [Simple JSON API for configuration]
 * Security (change default AP password and Password for accessing API endpoints)
 * Hardware push button support
 
### Advanced Color Control
* Relative commands (+/- xxx)
* Command requeuing (enabling animation loops)
* Pausing and continuing of animations
* Independent color channels (e.g. send command to `hue` channel without affecting other channels)
* Multiple commands in a single request
* Different queue policies for animation commands
* Instant blink commands
* Ramp speed - ramp timing can be specified as ramp speed instead of just ramp time

# Installation
Initially the firmware has to be flashed using a serial flasher (e.g. `esptool`, refer to the Wiki for details). Further updates can be installed using the OTA update method (using the web interface).

Precompiled binaries are provided via GitHub. It is also possible to compile the firmware images yourself. 
For more information and instructions please see [the Wiki](https://github.com/verybadsoldier/esp_rgbww_firmware/wiki/1.1-Flashing)

## OTA Updates 

There are 2 different update channels available. The firmware can be updates using these update URLs.

Available channels:

**Stable**

`http://rgbww.dronezone.de/release/version.json`

**Testing**

`http://rgbww.dronezone.de/testing/version.json`

Be aware that this is currently not a secure HTTPS connection and MITM attacks are possible!

## Index
Most information about installation (flashing), setup and usage guides are provided via the Wiki
https://github.com/verybadsoldier/esp_rgbww_firmware/wiki


## Links

- [FHEM Forum](https://forum.fhem.de/index.php?topic=70738.0)
- [Sming Framework](https://github.com/SmingHub/Sming)
- [RGBWWLed Library](https://github.com/verybadsoldier/RGBWWLed)
