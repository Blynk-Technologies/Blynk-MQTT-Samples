#!/usr/bin/env python3
#
# Copyright (c) 2024 Blynk Technologies Inc.
# SPDX-License-Identifier: Apache-2.0
#
# The software is provided "as is", without any warranties or guarantees (explicit or implied).
# This includes no assurances about being fit for any specific purpose.
#

import asyncio
import sys, time
import signal

from gmqtt import Client as MQTTClient
from gmqtt.mqtt.constants import MQTTv311, MQTTv50

try:
    import uvloop
    asyncio.set_event_loop_policy(uvloop.EventLoopPolicy())
except:
    pass

STOP = asyncio.Event()

def on_connect(client, flags, rc, properties):
    print('Connected (secure)')

def on_message(client, topic, payload, qos, properties):
    payload = payload.decode('utf-8')
    print(f'Got {topic}, value: {payload}')

    if topic == 'downlink/ds/terminal':
        reply = f"Your command: {payload}"
        client.publish('ds/terminal', reply)

def on_disconnect(client, packet, exc=None):
    print('Disconnected')

def ask_exit(*args):
    STOP.set()

async def main(auth_token):
    client = MQTTClient(None)

    client.on_connect = on_connect
    client.on_message = on_message
    client.on_disconnect = on_disconnect
    client.set_auth_credentials('device', auth_token)

    start_time = time.time()

    await client.connect('blynk.cloud', port=8883, ssl=True, version=MQTTv50, keepalive=45)
    client.subscribe('downlink/#', qos=0)

    async def periodic():
        while True:
            uptime = int(time.time() - start_time)
            client.publish('ds/uptime', uptime)
            await asyncio.sleep(1)

    loop = asyncio.get_event_loop()
    loop.create_task(periodic())

    await STOP.wait()
    await client.disconnect()


if __name__ == '__main__':
    auth_token = sys.argv[1]

    loop = asyncio.get_event_loop()
    loop.add_signal_handler(signal.SIGINT, ask_exit)
    loop.add_signal_handler(signal.SIGTERM, ask_exit)
    loop.run_until_complete(main(auth_token))
