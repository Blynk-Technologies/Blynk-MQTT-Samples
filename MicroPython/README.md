
# Blynk MQTT client for MicroPython

MicroPython is a lean and efficient implementation of the Python 3 programming language that includes a subset of its standard library and is optimized to run on microcontrollers and in constrained environments. It is designed to make programming digital electronics and IoT devices as simple and accessible as possible.

MicroPython allows developers to swiftly construct IoT applications using its straightforward syntax and interactive development environment, making it perfect for educational use, DIY projects, and rapid prototyping. Additionally, it maintains a level of reliability that suits professional settings.

Moreover, since MicroPython application code is very compact, it provides an excellent solution for OTA updates over bandwidth-constrained networks like Cellular, LoRaWAN or Satellite.

---

This example was verified to work with `MicroPython v1.22.2` on:

- `ESP32`, `ESP32-C3`, `ESP32-S3`
- `ESP8266`¹
- `Raspberry Pi Pico W` (RP2040)
- `Linux` (SBCs like `Raspberry Pi`, `Orange Pi`, etc)

¹ *insecure TCP connection only*

## Prerequisites

- Sign up/Log in to your [Blynk Account](https://blynk.cloud)
- Install Blynk IoT App for iOS or Android
- Use MQTT Sample Blueprint

## 1. Install `MicroPython`

https://micropython.org/download

## 2. Edit `config.py`

Set your WiFi and [Blynk device credentials](https://docs.blynk.io/en/getting-started/activating-devices/manual-device-activation#getting-auth-token).

## 3. Install required libraries

Make sure your board is connected via USB. It should **not** be opened by any serial monitor.
Run these commands on your development machine:

```sh
# Install mpremote utility
pip3 install --upgrade mpremote
# Install libraries
mpremote cp -r ./lib :
# Copy the example files to the device
mpremote cp *.py *.der :
```

## 4. Run

Reset you board and open MicroPython REPL:

```sh
mpremote repl
```

The device should get connected in a few seconds:

```log
      ___  __          __
     / _ )/ /_ _____  / /__
    / _  / / // / _ \/  '_/
   /____/_/\_, /_//_/_/\_\
          /___/

Connecting to WiFi_SSID... OK: 192.168.1.123
Connecting to MQTT broker...
Connected to Blynk.Cloud [secure]
```

# Running on linux

This will also work on Linux-based PCs or SBCs like Raspberry Pi:

```sh
micropython main.py
```

---

## Further reading

- [`Blynk MQTT API documentation`](https://docs.blynk.io/en/blynk.cloud-mqtt-api/device-mqtt-api)
- [`asyncio` documentation](https://docs.micropython.org/en/latest/library/asyncio.html)
- [`asyncio` tutorial](https://github.com/peterhinch/micropython-async/blob/master/v3/docs/TUTORIAL.md)
- [`mpremote` documentation](https://docs.micropython.org/en/latest/reference/mpremote.html)
- Alternative MQTT libraries like [mqtt_as](https://github.com/peterhinch/micropython-mqtt/tree/master/mqtt_as)

