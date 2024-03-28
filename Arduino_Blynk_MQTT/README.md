
# Blynk MQTT client for Arduino

Arduino Framework, often used in conjunction with PlatformIO, is a popular platform for developing IoT and embedded projects due to its simplicity and open-source nature. Arduino provides an accessible introduction to hardware programming with a simplified IDE and a vast library ecosystem for various sensors and actuators.

PlatformIO extends Arduino's capabilities by offering an advanced, cross-platform IDE that supports multiple boards and frameworks, improving code management, and facilitating complex deployments. This combination is particularly suited for hobbyists, educators, and professionals prototyping IoT devices and embedded systems.

---

This example was verified to work with these boards:

- Original Arduino boards:
  - `Nano ESP32`, `Nano RP2040 Connect`, `Nano 33 IoT`, `MKR WiFi 1010`, `UNO R4 WiFi`, `Portenta C33`, `Opta WiFi`
- Espressif:
  - `ESP32`, `ESP32-C3`, `ESP32-C6`, `ESP32-S2`, `ESP32-S3` via [ESP32 Arduino Core](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)
  - `ESP8266`, `NodeMCU`, `WeMos D1` etc. via [ESP8266 Arduino Core](https://github.com/esp8266/Arduino)
- Raspberry `Pi Pico W` (RP2040 + CYW43439) via [Arduino-Pico Core](https://arduino-pico.readthedocs.io/en/latest/install.html)
- Seeed `Wio Terminal` (ATSAMD51 + RTL8720DN) via [Seeed Arduino Core](https://wiki.seeedstudio.com/Wio-Terminal-Getting-Started/#software)

## Prepare your Device in Blynk.Cloud

1. Create Blynk template based on the provided blueprint. Click the **`Use Blueprint`** button in [`MQTT Air Cooler/Heater Demo`](https://blynk.cloud/dashboard/blueprints/Library/TMPL4zGiS1A7l).
2. In the left panel, select `Devices`
3. Click `New Device` button
4. Select `From Template -> MQTT Demo`, and click **`Create`**

> [!NOTE]
> Please note the device credentials that appear in the upper right corner. You'll need them in the next step.

## Run the sample using Arduino IDE

1. Install [Arduino IDE](https://www.arduino.cc/en/software)
2. Install the respective Arduino Core support package for your board
3. Open `Arduino_Blynk_MQTT/Arduino_Blynk_MQTT.ino` using the Arduino IDE
4. In `arduino_secrets.h` tab, fill-in your **WiFi** and **Blynk device** credentials
5. Click **Verify** and **Upload**

## Run the sample using PlatformIO

The provided `platformio.ini` contains pre-configured environments for each board type:

```
esp32, esp32c3, esp32c6, esp32s2, esp32s3, esp8266,
wio_terminal, unoR4wifi, rp2040connect, nano33iot,
mkrwifi1010, portentaC33, opta
```

In `Arduino_Blynk_MQTT/arduino_secrets.h` tab, fill-in your **WiFi** and **Blynk device** credentials.

Flash and open Serial Monitor:

```sh
pio run -e esp32 -t upload -t monitor
```

## Serial Monitor output

```log
    ___  __          __
   / _ )/ /_ _____  / /__
  / _  / / // / _ \/  '_/
 /____/_/\_, /_//_/_/\_\
        /___/

 Firmware ver:    0.0.1
 Build time:      Mar  3 2024 12:09:56
 Board:           esp32

Connecting to MyWiFiHotspot......... IP:192.168.1.123, RSSI:-45dBm
Connecting to blynk.cloud:8883... OK [secure]
```

## Troubleshooting

- On most **original Arduino** boards (Nano 33 IoT, MKR WiFi, etc.) you'll need to [update the connectivity module firmware][update-fw]
  - Also, you need to [upload SSL root certificates][root-ssl] for `blynk.cloud:8883`
  - On **Linux**, the **Arduino UNO R4** must be plugged into a **hub usb** to make the WiFi firmware/SSL certificates update process work. Otherwise, the board won’t reboot into the download mode.
- If secure connection fails, you can also disable the security and check it this way.
  - To disable security, uncomment `#define BLYNK_MQTT_UNSECURE` in `arduino_secrets.h`
  - **Warning:** Using plain TCP is susceptible to data interception and manipulation; please use only for testing purposes in controlled environments.

[update-fw]: https://support.arduino.cc/hc/en-us/articles/360013896579-Use-the-Firmware-Updater-in-Arduino-IDE
[root-ssl]: https://support.arduino.cc/hc/en-us/articles/360016119219-Upload-SSL-root-certificates
