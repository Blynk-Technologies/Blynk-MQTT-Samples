# SPDX-FileCopyrightText: 2024 Volodymyr Shymanskyy for Blynk Technologies Inc.
# SPDX-License-Identifier: Apache-2.0
#
# The software is provided "as is", without any warranties or guarantees (explicit or implied).
# This includes no assurances about being fit for any specific purpose.

import sys, io, machine, time, asyncio
if sys.platform == "linux": sys.path.append("lib")
import config, blynk_mqtt
import demo

BLYNK_FIRMWARE_VERSION = "0.1.0"

#
# App logic
#

mqtt = blynk_mqtt.mqtt

device = demo.Device(mqtt)

async def publisher_task():
    while True:
        try:
            device.update()
        except:
            pass
        await asyncio.sleep_ms(1000)

def mqtt_connected():
    print("MQTT connected")
    device.connected()

def mqtt_disconnected():
    print("MQTT disconnected")

def mqtt_callback(topic, payload):
    print(f"Got: {topic}, value: {payload}")
    device.process_message(topic, payload)

#
# Main loop
#

blynk_mqtt.on_connected = mqtt_connected
blynk_mqtt.on_disconnected = mqtt_disconnected
blynk_mqtt.on_message = mqtt_callback
blynk_mqtt.firmware_version = BLYNK_FIRMWARE_VERSION

def connect_wifi():
    import network
    sta = network.WLAN(network.STA_IF)
    if not sta.isconnected():
        print(f"Connecting to {config.WIFI_SSID}...", end="")
        sta.active(True)
        sta.disconnect()
        sta.config(reconnects=5)
        sta.connect(config.WIFI_SSID, config.WIFI_PASS)
        while not sta.isconnected():
            time.sleep(1)
            print(".", end="")
            if sta.status() == network.STAT_NO_AP_FOUND:
                raise Exception("WiFi Access Point not found")
            elif sta.status() == network.STAT_WRONG_PASSWORD:
                raise Exception("Wrong WiFi credentials")
        print(" OK:", sta.ifconfig()[0])

if sys.platform != "linux":
    connect_wifi()

try:
    asyncio.run(asyncio.gather(
        blynk_mqtt.task(),
        publisher_task()
    ))
except KeyboardInterrupt:
    print("Interrupted")
finally:
    # Clean up and create a new event loop
    asyncio.new_event_loop()
