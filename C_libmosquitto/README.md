 
# Blynk MQTT client for C

C programming on Linux is a foundational approach for developing high-performance IoT and embedded systems. C, being close to the operating system and hardware level, allows for efficient manipulation of system resources and is ideal for developing operating system kernels, device drivers, and resource-constrained IoT applications. Linux, with its robustness, scalability, and support for a wide range of architectures, provides a powerful platform for running C applications. This combination is critical for building complex IoT systems that require custom hardware interaction, advanced networking capabilities, and high reliability.

## Build on Debian/Ubuntu

```sh
sudo apt install libmosquitto-dev

gcc -o blynk_libmosquitto blynk_libmosquitto.c -lmosquitto

./blynk_libmosquitto $BLYNK_AUTH_TOKEN
```
