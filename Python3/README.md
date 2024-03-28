
# Blynk MQTT client for Python 3

Python 3, with its extensive library ecosystem and ease of use, is a popular choice for developing
IoT Edge and Embedded Systems, particularly for prototyping and scripting applications.
It supports rapid development cycles due to its high-level syntax and dynamic typing.
Python is often used in IoT devices for data analysis, web server implementation, 
and interfacing with hardware components.

Its versatility makes it suitable for a wide range of IoT applications,
from simple sensor data collection to complex data processing and IoT gateway services.

---

## Prepare your Device in Blynk.Cloud

1. Create Blynk template based on the provided blueprint. Click the **`Use Blueprint`** button in [`MQTT Air Cooler/Heater Demo`](https://blynk.cloud/dashboard/blueprints/Library/TMPL4zGiS1A7l).
2. In the left panel, select `Devices`
3. Click `New Device` button
4. Select `From Template -> MQTT Demo`, and click **`Create`**

> [!NOTE]
> Please note the device credentials that appear in the upper right corner. You'll need them in the next step.

## Edit `config.py`

Set your [Blynk device credentials](https://docs.blynk.io/en/getting-started/activating-devices/manual-device-activation#getting-auth-token).

## Run the sample using `paho-mqtt`

```sh
pip3 install --upgrade paho-mqtt
python3 blynk_paho.py
```

## Run the sample using `gmqtt`

```sh
pip3 install --upgrade gmqtt uvloop
python3 blynk_gmqtt.py
```

---

## Further reading

- [Blynk MQTT API documentation](https://docs.blynk.io/en/blynk.cloud-mqtt-api/device-mqtt-api)
- [Blynk Troubleshooting guide](https://docs.blynk.io/en/troubleshooting/general-issues)
- [Blynk Documentation](https://docs.blynk.io/en)

