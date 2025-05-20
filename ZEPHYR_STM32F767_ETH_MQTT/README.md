# Blynk MQTT client for STM32F767ZI (Ethernet + Zephyr)

This project demonstrates how to use **STM32F767ZI** (e.g., Nucleo-F767ZI) with **Zephyr RTOS** and **Ethernet** to connect securely to **Blynk.Cloud** using the MQTT protocol. It uses the built-in Zephyr networking stack, mbedTLS for encryption, and reconnects automatically on network failures or internet loss.

---

This example was verified to work with **Ethernet on STM32F767ZI**, but should work with other STM32 boards that have Ethernet support and enough RAM.

Important note: The microcontroller must have at least **128KB RAM** and **332KB Flash**.

## Setup Zephyr SDK

Follow the official [Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html) to set up your environment.

---

## Ethernet Configuration

This example uses native Ethernet via the on-board RMII interface on the Nucleo-F767ZI board. No external modem or Wi-Fi is needed.

Ethernet port is configured with DHCP by default.

---

## Blynk Configuration

To successfully connect to the Blynk we need to specify template id, template name, token and server. It can be done by adding appropriate configurations to **prj.conf** file. for example use next command (with your data):

````
echo 'CONFIG_CLOUD_BLYNK_TEMPLATE_ID="TMPLxxx"' >> prj.conf
echo 'CONFIG_CLOUD_BLYNK_AUTH_TOKEN="***"' >> prj.conf
echo 'CONFIG_CLOUD_BLYNK_TEMPLATE_NAME="sample"' >> prj.conf
echo 'CONFIG_CLOUD_BLYNK_SERVER_ADDR="blynk.cloud"' >> prj.conf
echo 'CONFIG_CLOUD_BLYNK_SERVER_PORT=8883' >> prj.conf

````

> Find this information in your **Blynk Device Info** screen.
> Read more: [https://bit.ly/BlynkSimpleAuth](https://bit.ly/BlynkSimpleAuth)

---

## Build and Run

This project uses the standard **Zephyr CMake+West** build system.

```sh
# Build the firmware
west build -b nucleo_f767zi --sysbuild .

# Flash firmware (ST-Link is built into the board)
west flash
```

You can also flash using STM32CubeProgrammer or OpenOCD if preferred.

---

## Logs and Debugging

Connect to the board via USB and open a terminal:

```sh
minicom -D /dev/ttyACM0 -b 115200
```

You will see logs such as:

```log

*** Booting Zephyr OS build v4.1.0-3524-g2bd70305449e ***
[00:00:00.065,000] <inf> net_config: Initializing network
[00:00:00.073,000] <inf> net_config: Waiting interface 1 (0x20021c90) to be up...
[00:00:03.552,000] <inf> phy_mii: PHY (0) Link speed 100 Mb, full duplex
[00:00:03.567,000] <inf> net_config: Interface 1 (0x20021c90) coming up
[00:00:03.576,000] <inf> net_config: Running dhcpv4 client...
[00:00:07.587,000] <inf> net_dhcpv4: Received: 192.168.88.132
[00:00:07.596,000] <inf> net_config: IPv4 address: 192.168.88.132
[00:00:07.604,000] <inf> net_config: Lease time: 1800 seconds
[00:00:07.613,000] <inf> net_config: Subnet: 255.255.255.0
[00:00:07.621,000] <inf> net_config: Router: 192.168.88.1
[00:00:07.629,000] <inf> mqtt_blynk: Firmware version: 1.1.7
[00:00:07.637,000] <inf> net_mgr: Network connected - DHCP bound
[00:00:07.647,000] <inf> mqtt: Connecting to blynk.cloud:8883
[00:00:10.512,000] <inf> net_mqtt: Connect completed
[00:00:10.540,000] <inf> mqtt: MQTT connected
[00:00:10.900,000] <inf> mqtt: Subscribed successfully
[00:00:11.004,000] <inf> mqtt_blynk: Received: downlink/ds/Power -> 1
[00:00:11.015,000] <inf> mqtt_blynk: Received: downlink/ds/Set Temperature -> 12

```

---

## Reconnect Handling

If internet or network is lost (e.g. router restarts), the system automatically:

* Detects disconnection via Zephyr network events
* Reconnects to the MQTT broker after internet is restored
* Resubscribes to Blynk topics

Example disconnection and recovery logs:

```log
<err> net_sock_tls: TLS recv error: -4e
<err> net_mqtt_rx: Transport read error: -5
<inf> mqtt_blynk: MQTT client disconnected -5
<inf> mqtt_blynk: Reconnecting in 5 seconds...
```

---

## OTA
OTA just works as usual. NOTE: you should use signed image **zephyr.signed.bin**.

```log

[00:00:13.011,000] <inf> mqtt: Starting OTA update
[00:00:13.012,000] <inf> ota: Parsing OTA JSON (length: 201)
[00:00:13.012,000] <err> utils: Key "version" not found in JSON
[00:00:13.012,000] <inf> utils: Parsing URL: https://fra1.blynk.cloud/static/fw_16955832961945420033_-838595071.bin?token=ve4vzUYfcTyz8zjCUZhPPW2YCzidmrfy
[00:00:13.012,000] <inf> utils: Parsed - Host: fra1.blynk.cloud, Port: 443, Path: /static/fw_16955832961945420033_-838595071.bin?token=ve4vzUYfcTyz8zjCUZhPPW2YCzidmrfy, TLS: yes
--- 1 messages dropped ---
[00:00:13.012,000] <inf> ota: Starting OTA download:
[00:00:13.012,000] <inf> ota:   URL: https://fra1.blynk.cloud/static/fw_16955832961945420033_-838595071.bin?token=ve4vzUYfcTyz8zjCUZhPPW2YCzidmrfy
[00:00:13.013,000] <inf> ota:   Host: fra1.blynk.cloud
[00:00:13.013,000] <inf> ota:   Port: 443
[00:00:13.013,000] <inf> ota:   TLS: enabled
[00:00:13.013,000] <inf> ota:   Expected size: 340528 bytes
[00:00:13.013,000] <inf> ota:   Version: 1.1.1
[00:00:15.833,000] <inf> ota: Sending HTTP GET request...
[00:00:16.003,000] <inf> ota: Received HTTP 200 OK, starting firmware download
[00:00:16.003,000] <inf> ota: Opening flash area for OTA update
[00:00:16.003,000] <inf> ota: Erasing flash area (size: 786432 bytes)
[00:00:16.204,000] <inf> ota: Flash area ready, starting download...
[00:00:16.205,000] <inf> ota: HTTP headers skipped, firmware data length: 1611 bytes
[00:00:17.013,000] <inf> ota: OTA Progress: 10% (34379/340528 bytes)
[00:00:17.825,000] <inf> ota: OTA Progress: 20% (69195/340528 bytes)
[00:00:18.638,000] <inf> ota: OTA Progress: 30% (104011/340528 bytes)
[00:00:19.416,000] <inf> ota: OTA Progress: 40% (136779/340528 bytes)
[00:00:20.226,000] <inf> ota: OTA Progress: 50% (171595/340528 bytes)
[00:00:20.992,000] <inf> ota: OTA Progress: 60% (204363/340528 bytes)
[00:00:21.802,000] <inf> ota: OTA Progress: 70% (239179/340528 bytes)
[00:00:22.610,000] <inf> ota: OTA Progress: 80% (273995/340528 bytes)
[00:00:23.380,000] <inf> ota: OTA Progress: 90% (306763/340528 bytes)
[00:00:24.182,000] <inf> ota: OTA Progress: 100% (341579/340528 bytes)
[00:00:24.227,000] <inf> ota: HTTP transfer completed
[00:00:24.227,000] <inf> ota: OTA download completed successfully: 343627 bytes
[00:00:24.228,000] <inf> ota: New firmware marked as pending. Rebooting in 5 seconds...
uart:~$ *** Booting MCUboot v2.1.0-rc1-300-g81315483fcbd ***
*** Using Zephyr OS build v4.1.0-3524-g2bd70305449e ***
I: Starting bootloader
I: Primary image: magic=good, swap_type=0x1, copy_done=0x3, image_ok=0x1
I: Scratch: magic=bad, swap_type=0x0, copy_done=0x2, image_ok=0x2
I: Boot source: primary slot
I: Image index: 0, Swap type: test
E: Image in the secondary slot is not valid!
I: Bootloader chainload address offset: 0x40000
I: Image version: v1.1.2
I: Jumping to the first image slot

```

---

## Device Behavior

This firmware:

* Subscribes to `downlink/ds/Power` and `downlink/ds/Set Temperature`
* Publishes current state regularly
* Controls internal "thermostat" logic using received parameters
* Logs device state and network status
* OTA updates
---

## Further Reading

* [Blynk MQTT API documentation](https://docs.blynk.io/en/blynk.cloud-mqtt-api/device-mqtt-api)
* [Blynk Troubleshooting Guide](https://docs.blynk.io/en/troubleshooting/general-issues)
* [Zephyr Networking API Reference](https://docs.zephyrproject.org/latest/connectivity/networking/index.html)
* [Zephyr Logging System](https://docs.zephyrproject.org/latest/services/logging/index.html)

