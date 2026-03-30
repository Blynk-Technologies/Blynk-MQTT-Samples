# Blynk nRF9151 MQTT Sample

Connects an [nRF9151](https://www.nordicsemi.com/Products/nRF9151) to [Blynk](https://blynk.io) over LTE-M using MQTT.

Works with either the [nRF9151 SMA DK](https://www.nordicsemi.com/Products/Development-hardware/nRF9151-SMA-DK), the [Thingy:91 X](https://www.nordicsemi.com/Products/Development-hardware/Nordic-Thingy-91-X), or the [Circuit Dojo nRF9151 Feather](https://www.circuitdojo.com/products/nrf9151-feather).

Features:

- Publishes an incrementing counter to Blynk datastream **V1** on a configurable interval
- Publishes the time of the last button press to datastream **V2** when the user button is pressed
- OTA firmware updates via Blynk
- Credentials (auth token, server, template ID) stored in flash — no recompile needed to configure a device

## Blynk Setup

In your Blynk template create two datastreams:
- **V1** — Integer — for the counter
- **V2** — String — for the button press timestamp

## Using the Pre-built Binaries

If you want to avoid setting up the build environment you can flash one if the pre-built binaries from the [binaries](binaries) directory.

### First-time flash

#### For the nRF9151 SMA DK or the Thingy:91 X:
1. Download a pre-built binary from the build directory
2. Install [nRF Connect for Desktop](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-Desktop) and open the **Programmer** app
3. Connect your board via USB
4. Add the binary file
5. Do an Erase & write to flash device

#### For the nRF9151 Feather:
1. Get [probe-rs](https://probe.rs/docs/getting-started/installation/)
2. flash the device with: ```probe-rs download --chip nRF9151_xxAA --binary-format hex feather_merged.hex --allow-erase-all```


### Provisioning credentials

On first boot the device has no credentials. Connect a serial terminal (e.g. PuTTY or nRF Serial Terminal) at **115200 baud** to the Feather's COM port. Press Enter to get the shell prompt:

```
uart:~$
```

Then enter your Blynk credentials:

```
cred server <your-blynk-server>   (optional, default: blynk.cloud)
cred template <your-template-id>
cred token <your-blynk-auth-token>
kernel reboot
```

Use `cred show` to check what is stored, or `cred clear` to erase and re-provision.

The device reboots and connects automatically. Credentials survive OTA updates.

> **Note:** The pre-built binary contains a hardcoded Blynk template ID in its firmware tag. This means Blynk's OTA firmware shipment UI will not work for your template — you need to build from source (see below) to use Blynk OTA. 

## Building from Source

You need VSCode with the Zephyr Tools extension installed.

1. In an empty workspace open Zephyr Tools. It will pop up a window asking to Init Repo or Create Project — choose **Init Repo**.

2. It will ask for a directory. This is the root for the entire SDK and where you will open VSCode from in future. Watch out for Windows path length limits — use a short name, e.g. `c:\nrf91`.

3. It will ask for a repository URL, use the Circuit Dojo repo:
```
https://github.com/circuitdojo/nrf9160-feather-examples-and-drivers.git
```
Hit Enter for the default branch. It will then install and initialise everything — this can take 10+ minutes.

4. Copy this sample into the `nfed\samples` directory:
```
c:\nrf91\nfed\samples
```

5. Set your Blynk template ID in `prj.conf`:
```
CONFIG_BLYNK_TEMPLATE_ID="TMPLxxxxxxxx"
```
The auth token and server are entered at runtime via the shell — do not put them in `prj.conf`.

6. In the Project Settings panel on the left set:
   
   - **Board:** `nrf9151dk/nrf9151/ns` for the nRF9151 SMA DK

   - **Board:** `thingy91x/nrf9151/ns` for the Thingy:91 X

   - **Board:** `circuitdojo_feather_nrf9151/nrf9151/ns` for the nRF9151 Feather

   And set the Project:
   - **Project:** the cloned directory, e.g. `c:\nrf91\nfed\samples\blynk_mqtt`

7. In Quick Actions click **Build**. The first build takes about 10 minutes; subsequent builds are faster.

8. Plug in the Feather USB cable. If you have previously done an OTA update, run a chip erase first:
```
probe-rs erase --chip nRF9151_xxAA
```
Then in Quick Actions click **Flash** and select `nRF9151_xxAA`.

9. Click **Monitor** to view the log output. If you miss the startup messages press the reset button on the Feather.

10. On first boot follow the credential provisioning steps above.

## OTA Firmware Updates

For your own builds you can use Blynk's built-in OTA firmware shipment — upload `zephyr.signed.bin` from `build/circuitdojo_feather_nrf9151/blynk_mqtt/zephyr/zephyr.signed.bin`.

To update the version before building, edit the `VERSION` file:
```
VERSION_MAJOR = 2
VERSION_MINOR = 0
PATCHLEVEL = 0
```
