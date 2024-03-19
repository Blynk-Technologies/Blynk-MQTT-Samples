 
# Blynk MQTT client for Python 3

Python 3, with its extensive library ecosystem and ease of use, is a popular choice for developing IoT Edge and Embedded Systems, particularly for prototyping and scripting applications. It supports rapid development cycles due to its high-level syntax and dynamic typing. Python is often used in IoT devices for data analysis, web server implementation, and interfacing with hardware components. Its versatility makes it suitable for a wide range of IoT applications, from simple sensor data collection to complex data processing and IoT gateway services.

## Getting Started

- Sign up/Log in to your [Blynk Account](https://blynk.cloud)
- Install Blynk IoT App for iOS or Android
- Use MQTT Sample Blueprint

## Edit `config.py`

Set your [Blynk device credentials](https://docs.blynk.io/en/getting-started/activating-devices/manual-device-activation#getting-auth-token).

## Run example using `paho-mqtt`

```sh
pip3 install paho-mqtt
python3 blynk_paho.py
```

## Run example using `gmqtt`

```sh
pip3 install gmqtt
python3 blynk_gmqtt.py
```
