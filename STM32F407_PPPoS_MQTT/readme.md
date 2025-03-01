
# Blynk MQTT client for STM32F4 (Cellular only)

The STM32Cube ecosystem is a flexible development platform designed for STM32 microcontrollers. Known for its extensive set of tools, middleware, and libraries, STM32Cube provides a professional-grade environment for developing embedded and IoT applications. It supports a wide range of features, including HAL/LL drivers, FreeRTOS integration,networking stacks (LwIP, mbedTLS, MQTT), and various communication protocols (like PPPoS), making it suitable for use together with Blynk platform.

---

This example was verified to work with SIM7000, however any generic AT modem can be used.

This example should also work with other STM32 microcontroller models. Important note: The microcontroller must have at least 128KB of RAM and more than 256KB of Flash.

## Setup STM32CubeIDE

Follow the official [getting started guide](https://www.st.com/en/development-tools/stm32cubeide.html) to set up STM32CubeIDE for STM32 development.

Note: The project is pre-configured as a Makefile project. This step is only necessary if you plan to modify project settings.

## Install ARM GCC Toolchain

Select the appropriate arm-none-eabi toolchain based on your host system.
https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads arm-none-eabi

### Modem config

The default project configuration pins are following:
- PC6 --> USART6_TX 
- PC7 --> USART6_RX

Connect external UART to USB converter to see the logs:
- PB10 --> USART3_TX
- PD9  --> USART3_RX

### Blynk config

Fill in information from `Blynk Device Info` here.
Read More: https://bit.ly/BlynkSimpleAuth

## Build and Run

This project is preconfigured as a Makefile-based STM32Cube project. While it is designed to be built using Make, alternative build methods are also available.

```sh
# Build firmware
make

# Flash firmware using JLink
make flash
```
Alternatively you can modify Makefile and add your flash target for another flashing tool, eg STLink, so

## Configure the Device Auth Token

Find MQTT_CLIENT_PASS define in client_blynk.h and change it to your device ID string

## Verify connection

Code is reach of logs and you may see the each step of program execution.

The device should be marked as `Online`in `Blynk.Cloud`, and the device terminal will show a bunch of MQTT logs like this:

```log
GSM: AT COMMAND: [ATD*99#..]
GSM: AT RESPONSE: [..CONNECT 150000000..]
GSM: GSM initialized.
GSM: ppp_status_cb: 0
GSM: status_cb: Connected
GSM:    ipaddr    = 100.78.214.252
GSM:    gateway   = 10.64.64.64
GSM:    netmask   = 255.255.255.255
GSM:    DNS1      = 10.74.227.4
GSM:    DNS2      = 10.74.32.6
GSM: LTE Init done
INF: mqtt_connection_cb(0x20012760, 0, 0)
INF: mqtt_connection_cb(0x20012760, 0, 256)
INF: Redirection to mqtts://fra1.blynk.cloud:8883
INF: mqtt_connection_cb(0x20012760, 0, 0)
Ping recieved
```

---

## Further reading

- [Blynk MQTT API documentation](https://docs.blynk.io/en/blynk.cloud-mqtt-api/device-mqtt-api)
- [Blynk Troubleshooting guide](https://docs.blynk.io/en/troubleshooting/general-issues)
- [Blynk Documentation](https://docs.blynk.io/en)
