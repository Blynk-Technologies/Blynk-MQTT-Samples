# SPDX-FileCopyrightText: 2024 Volodymyr Shymanskyy for Blynk Technologies Inc.
# SPDX-License-Identifier: Apache-2.0
#
# The software is provided "as is", without any warranties or guarantees (explicit or implied).
# This includes no assurances about being fit for any specific purpose.

import sys, io, machine, time, asyncio
import config, blynk_mqtt

BLYNK_FIRMWARE_VERSION = "0.1.0"

#
# App logic
#

mqtt = blynk_mqtt.mqtt

def terminal_print(*args):
    msg = " ".join(map(str, args)) + "\n"
    mqtt.publish(b"ds/Terminal", msg.encode("utf-8"))

def run_terminal_command(cmd):
    if cmd[0] == "lamp":
        # TODO
        pass
    else:
        terminal_print("Unknown command:", *cmd)

async def publisher_task():
    started = time.ticks_ms()
    while True:
        try:
            # TODO: publish liminance
            mqtt.publish(b"ds/Luminance", str(uptime))
        except Exception as e:
            #print("Failed to publish:", e)
            pass

        await asyncio.sleep_ms(1000)

first_connection = True

def mqtt_connected():
    global first_connection
    if first_connection:
        terminal_print(blynk_mqtt.LOGO)
        terminal_print(f"{sys.platform} connected.")
        terminal_print(f"Firmware version: {BLYNK_FIRMWARE_VERSION}")
        first_connection = False
    else:
        print("MQTT connected")

    # Get Brightness value from Blynk.Cloud (read DataStream value)
    mqtt.publish("get/ds", "Brightness")

def mqtt_disconnected():
    print("MQTT disconnected")

def mqtt_callback(topic, payload):
    print(f"Got: {topic}, value: {payload}")

    if topic == "downlink/ds/Brightness":
        value = int(payload)
        # TODO
    elif topic == "downlink/ds/Terminal":
        try:
            run_terminal_command(payload.split(" "))
        except Exception as e:
            with io.StringIO() as buf:
                sys.print_exception(e, buf)
                terminal_print(buf.getvalue())

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
