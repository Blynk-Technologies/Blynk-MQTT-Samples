# Blynk MQTT Samples

The sample project simulates a heater device:

- The room heats up when the set temperature exceeds the current temperature.
- The room cools down when the set temperature is lower than the current temperature or if the heater is off.
- The device can also be controlled via the **Terminal** widget (type `help` for a list of commands).

## Getting Started

1. Sign up/Log in to your [**Blynk Account**](https://blynk.cloud)
2. Install **Blynk IoT** App for [<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/googleplay.svg" width="18" height="18" /> Android](https://play.google.com/store/apps/details?id=cloud.blynk) or
[<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/apple.svg" width="18" height="18" /> iOS](https://apps.apple.com/us/app/blynk-iot/id1559317868)
3. Please **follow the instructions from [MQTT Sample blueprint](TODO)**

## What's in this repository?

The `MQTT Sample` blueprint is compatible with various MQTT clients, developed in different programming languages:

- [**Python 3**](Python3/README.md) - suitable for use on Single Board Computers (SBCs) like `Raspberry Pi` and some Industrial IoT gateways
- [**MicroPython**](MicroPython/README.md) - a fun and easy way of creating the device firmware
- [**Arduino / PlatformIO**](Arduino_Blynk_MQTT/README.md) - a pre-configured project that supports over 15 connectivity-enabled boards, including the `Espressif ESP32`, `Raspberry Pi Pico W`, `Nano 33 IoT`, `Nano RP2040 Connect`, `UNO R4 WiFi`, `Seeed Wio Terminal`, etc.
- [**Plain C**](C_libmosquitto/README.md) with the `Mosquitto` library - tailored for advanced use cases needing high performance, compact size, or system-level integration
- [**Lua**](Lua_OpenWrt/README.md) - ready for deployment on `OpenWrt`-based routers and SBCs like `Onion Omega2`
- [**HTML5 / JavaScript**](HTML5_WebSocket/README.md) - thanks to the `WebSocket` technology, this example can be used [directly from the browser](https://bit.ly/Blynk-HTML5-MQTT-Sample)

