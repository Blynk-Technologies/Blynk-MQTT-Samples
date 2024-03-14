# Blynk MQTT client for Lua

Lua is a lightweight programming language that offers a flexible scripting solution for networked IoT devices. When used on OpenWRT it allows for the development of custom networking applications and device management scripts, leveraging Lua's simplicity and efficiency. OpenWRT's extensive support for various networking protocols and hardware makes it a robust platform for IoT devices requiring custom routing, firewall rules, and network monitoring, with Lua scripts enabling dynamic configuration and automation.

## Running on OpenWRT

```sh
opkg install lua libmosquitto-ssl lua-mosquitto
lua blynk_mqtt_client.lua $BLYNK_AUTH_TOKEN
```
