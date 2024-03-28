
# Blynk MQTT client for C

C programming on Linux is a foundational approach for developing high-performance IoT
and embedded systems. C, being close to the operating system and hardware level, allows for
efficient manipulation of system resources and is ideal for developing operating system kernels,
device drivers, and resource-constrained IoT applications.

Linux, with its robustness, scalability, and support for a wide range of architectures,
provides a powerful platform for running C applications. This combination is critical for building
complex IoT systems that require custom hardware interaction, advanced networking capabilities,
and high reliability.

---

## Prepare your Device in Blynk.Cloud

1. Create Blynk template based on the provided blueprint. Click the **`Use Blueprint`** button in [`MQTT Air Cooler/Heater Demo`](https://blynk.cloud/dashboard/blueprints/Library/TMPL4zGiS1A7l).
2. In the left panel, select `Devices`
3. Click `New Device` button
4. Select `From Template -> MQTT Demo`, and click **`Create`**

> [!NOTE]
> Please note the device credentials that appear in the upper right corner. You'll need them in the next step.

## Build on Debian/Ubuntu

```sh
sudo apt install libmosquitto-dev

gcc -o blynk_libmosquitto blynk_libmosquitto.c -lmosquitto
```

## Run

```sh
./blynk_libmosquitto $BLYNK_AUTH_TOKEN
```

---

## Further reading

- [Blynk MQTT API documentation](https://docs.blynk.io/en/blynk.cloud-mqtt-api/device-mqtt-api)
- [Blynk Troubleshooting guide](https://docs.blynk.io/en/troubleshooting/general-issues)
- [Blynk Documentation](https://docs.blynk.io/en)

