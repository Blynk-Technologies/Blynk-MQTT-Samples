#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2024 Volodymyr Shymanskyy for Blynk Technologies Inc.
# SPDX-License-Identifier: Apache-2.0
#
# The software is provided "as is", without any warranties or guarantees (explicit or implied).
# This includes no assurances about being fit for any specific purpose.

from paho.mqtt.client import Client, CallbackAPIVersion
import time, random
import ssl

def on_connect(client, obj, flags, reason_code, properties):
    print("Connected (secure)")
    client.subscribe("downlink/#", qos=1)

def on_message(client, obj, msg):
    payload = msg.payload.decode("utf-8")
    topic = msg.topic
    print(f"Got {topic}, value: {payload}")

    if topic == "downlink/ds/terminal":
        reply = f"Your command: {payload}"
        client.publish("ds/terminal", reply)

def main(auth_token):
    client = Client(CallbackAPIVersion.VERSION2)
    client.tls_set(tls_version=ssl.PROTOCOL_TLSv1_2)
    client.on_connect = on_connect
    client.on_message = on_message
    client.username_pw_set("device", auth_token)
    client.connect_async('blynk.cloud', 8883, 45)
    client.loop_start()

    start_time = time.time()
    while True:
        uptime = int(time.time() - start_time)
        client.publish("ds/uptime", uptime)
        time.sleep(1)

if __name__ == '__main__':
    import sys
    main(sys.argv[1])
