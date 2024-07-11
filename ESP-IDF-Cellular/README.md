
# Blynk MQTT client for ESP-IDF (Cellular only)

The ESP-IDF (Espressif IoT Development Framework) is a robust and versatile platform
designed for developing applications for Espressif's ESP32 series of microcontrollers.
Renowned for its extensive feature set and powerful libraries, ESP-IDF provides
a professional-grade environment suitable for complex IoT and embedded projects.

---

This example was verified to work with SIM7000, SIM7600, SIM800C cellular modems.

## Setup ESP-IDF v5.3

Follow the official [getting started guide](https://docs.espressif.com/projects/esp-idf/en/release-v5.3/esp32/get-started/index.html)

## Configure the firmware

```sh
source /opt/esp-idf-v5.3-rc1/export.sh
idf.py menuconfig
```

Setup your ESP chip, flash size as usual.

### Modem config

The default pin configuration should work with `LilyGO T-PCIE` and `TTGO T-Call SIM800C` boards.

For `TTGO T-Internet-COM`, you can use:
- TX pin: 33
- RX pin: 35
- Power Key: 0 (none)
- Power On switch: 32

### Blynk config

Fill in information from `Blynk Device Info` here.
Read More: https://bit.ly/BlynkSimpleAuth

## Build and Run

```sh
# Build firmware
idf.py build

# Erase flash (optional)
idf.py -p PORT erase-flash

# Flash firmware
idf.py -p PORT flash
```

## Configure the Device Auth Token

Open device terminal:
```sh
idf.py monitor
```

In the device terminal, issue a command to store the token:

```sh
BLYNK> set_auth_token YOUR-DEVICE-AUTH-TOKEN
```

## Verify connection

The device should be marked as `Online`in `Blynk.Cloud`, and the device terminal will show a bunch of MQTT logs like this:

```log
I (38510) [MQTTS]: MQTT_EVENT_CONNECTED
I (38730) [MQTTS]: MQTT_EVENT_SUBSCRIBED, msg_id=5258
I (39460) [MQTTS]: MQTT_EVENT_PUBLISHED, msg_id=23955
```

---

## Further reading

- [Blynk MQTT API documentation](https://docs.blynk.io/en/blynk.cloud-mqtt-api/device-mqtt-api)
- [Blynk Troubleshooting guide](https://docs.blynk.io/en/troubleshooting/general-issues)
- [Blynk Documentation](https://docs.blynk.io/en)

