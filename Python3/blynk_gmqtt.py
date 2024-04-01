#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2024 Volodymyr Shymanskyy for Blynk Technologies Inc.
# SPDX-License-Identifier: Apache-2.0
#
# The software is provided "as is", without any warranties or guarantees (explicit or implied).
# This includes no assurances about being fit for any specific purpose.

import asyncio
import sys, time
import signal
import config, demo

from gmqtt import Client as MQTTClient
from gmqtt.mqtt.constants import MQTTv311, MQTTv50
from gmqtt.mqtt.handler import MQTTConnectError

STOP = asyncio.Event()
mqtt = MQTTClient(None)
device = demo.Device(mqtt)

def on_connect(mqtt, flags, rc, properties):
    mqtt.subscribe("downlink/#", qos=0)
    print("Connected [secure]")
    device.connected()

def on_message(mqtt, topic, payload, qos, properties):
    payload = payload.decode("utf-8")
    if topic == "downlink/redirect":
        print("Redirecting...")
        pass
    elif topic == "downlink/reboot":
        print("Reboot command received!")
        pass
    elif topic == "downlink/ping":
        # MQTT client library automagically sends the QOS1 response
        pass
    elif topic == "downlink/diag":
        print("Server says:", payload)
    else:
        print(f"Got {topic}, value: {payload}")
        device.process_message(topic, payload)

def on_disconnect(mqtt, packet, exc=None):
    print("Disconnected")

def ask_exit(*args):
    STOP.set()

async def main():
    mqtt.on_connect = on_connect
    mqtt.on_message = on_message
    mqtt.on_disconnect = on_disconnect
    mqtt.set_auth_credentials("device", config.BLYNK_AUTH_TOKEN)

    try:
        await mqtt.connect(config.BLYNK_MQTT_BROKER, port=8883, ssl=True,
                           version=MQTTv50, keepalive=45)
    except MQTTConnectError as e:
        if e.args[0] in (4, 5):
            print("Invalid BLYNK_AUTH_TOKEN")
            return
        raise e

    async def periodic():
        while True:
            device.update()
            await asyncio.sleep(1)

    asyncio.create_task(periodic())

    await STOP.wait()
    await mqtt.disconnect()


if __name__ == "__main__":
    try:
        import uvloop
        asyncio.set_event_loop_policy(uvloop.EventLoopPolicy())
        print("Using libuv asyncio event loop")
    except:
        pass

    loop = asyncio.get_event_loop()
    loop.add_signal_handler(signal.SIGINT, ask_exit)
    loop.add_signal_handler(signal.SIGTERM, ask_exit)
    loop.run_until_complete(main())
