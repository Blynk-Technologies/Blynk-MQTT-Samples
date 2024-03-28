
# Blynk MQTT client for Lua

Lua is a lightweight programming language that offers a flexible scripting solution
for networked IoT devices. When used on OpenWRT it allows for the development of custom
networking applications and device management scripts, leveraging Lua's simplicity
and efficiency.

OpenWRT's extensive support for various networking protocols and hardware makes it
a robust platform for IoT devices requiring custom routing, firewall rules, and
network monitoring, with Lua scripts enabling dynamic configuration and automation.

---

## Prepare your Device in Blynk.Cloud

1. Create Blynk template based on the provided blueprint. Click the **`Use Blueprint`** button in [`MQTT Air Cooler/Heater Demo`](https://blynk.cloud/dashboard/blueprints/Library/TMPL4zGiS1A7l).
2. In the left panel, select `Devices`
3. Click `New Device` button
4. Select `From Template -> MQTT Demo`, and click **`Create`**

> [!NOTE]
> Please note the device credentials that appear in the upper right corner. You'll need them in the next step.


## Running on OpenWRT

```sh
opkg install lua libmosquitto-ssl lua-mosquitto
lua blynk_mqtt_client.lua $BLYNK_AUTH_TOKEN
```

---

## Further reading

- [Blynk MQTT API documentation](https://docs.blynk.io/en/blynk.cloud-mqtt-api/device-mqtt-api)
- [Blynk Troubleshooting guide](https://docs.blynk.io/en/troubleshooting/general-issues)
- [Blynk Documentation](https://docs.blynk.io/en)

