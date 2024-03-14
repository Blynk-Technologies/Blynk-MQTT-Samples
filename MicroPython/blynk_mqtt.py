#
# Copyright (c) 2024 Blynk Technologies Inc.
# SPDX-License-Identifier: Apache-2.0
#
# The software is provided "as is", without any warranties or guarantees (explicit or implied).
# This includes no assurances about being fit for any specific purpose.
#

import gc, sys, time, machine, json, asyncio
import config
from umqtt.simple import MQTTClient

def _dummy(*args):
    pass

on_connected = _dummy
on_disconnected = _dummy
on_message = _dummy
firmware_version = "0.0.1"

LOGO = r"""
      ___  __          __
     / _ )/ /_ _____  / /__
    / _  / / // / _ \/  '_/
   /____/_/\_, /_//_/_/\_\
          /___/
"""

print(LOGO)

def _on_message(topic, payload):
    topic = topic.decode("utf-8")
    payload = payload.decode("utf-8")

    if topic == "downlink/redirect":
        mqtt.server, mqtt.port = payload.split(":")
        mqtt.disconnect()
        mqtt.connect() # TODO: move to a separate function
    elif topic == "downlink/reboot":
        print("Rebooting...")
        machine.reset()
    else:
        on_message(topic, payload)

ssl_ctx = None
if sys.platform in ("esp32", "rp2", "linux"):
    import ssl
    #print(ssl.MBEDTLS_VERSION)
    ssl_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ssl_ctx.verify_mode = ssl.CERT_REQUIRED
    # ISRG Root X1, expires: Mon, 04 Jun 2035 11:04:38 GMT
    ssl_ctx.load_verify_locations(cafile="ISRG_Root_X1.der")

mqtt = MQTTClient(client_id="", server=config.BLYNK_MQTT_BROKER, ssl=ssl_ctx,
                  user="device", password=config.BLYNK_AUTH_TOKEN, keepalive=45)
mqtt.set_callback(_on_message)

async def task():
    connected = False
    while True:
        await asyncio.sleep_ms(10)
        if not connected:
            if ssl_ctx:
                while not update_ntp_time():
                    await asyncio.sleep(1)
            try:
                gc.collect()
                print(f"Connecting to MQTT broker...")
                mqtt.connect()
                conn_type = "[secure]" if ssl_ctx else "[insecure]"
                print("Connected to Blynk.Cloud", conn_type)
                mqtt.subscribe(b"downlink/#")

                info = {
                    "type": config.BLYNK_TEMPLATE_ID,
                    "tmpl": config.BLYNK_TEMPLATE_ID,
                    "ver":  firmware_version,
                    "rxbuff": 1024
                }
                # Send info to the server
                mqtt.publish(b"info/mcu", json.dumps(info))
                connected = True
                try:
                    on_connected()
                except:
                    pass
            except Exception as e:
                if e.value == 4 or e.value == 5:
                    print("Invalid BLYNK_AUTH_TOKEN")
                    return
                else:
                    print("Failed to connect:", e)
                    await asyncio.sleep(5)
        try:
            mqtt.check_msg()
        except Exception as e:
            connected = False
            try:
                on_disconnected()
            except:
                pass

def print_time():
    y, m, d, H, M, S, w, j = time.localtime()
    a = ("Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun")[w]
    print(f"Time: {a} {y}-{m:02d}-{d:02d} {H:02d}:{M:02d}:{S:02d}")

def update_ntp_time():
    Jan24 = 756_864_000 if (time.gmtime(0)[0] == 2000) else 1_704_067_200
    if time.time() > Jan24:
        return True

    import ntptime
    try:
        ntptime.timeout = 3
        ntptime.settime()
        if time.time() > Jan24:
            print_time()
            return True
    except Exception as e:
        print("NTP failed:", e)
    return False
