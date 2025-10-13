[![](https://dcbadge.vercel.app/api/server/3E8ca2dkcC)](https://discord.gg/osmu)

![GitHub Downloads (all assets, all releases)](https://img.shields.io/github/downloads/bitaxeorg/esp-miner/total)
![GitHub commit activity](https://img.shields.io/github/commit-activity/t/bitaxeorg/esp-miner)
![GitHub contributors](https://img.shields.io/github/contributors/bitaxeorg/esp-miner)
![Alt](https://repobeats.axiom.co/api/embed/70889479b1e002c18a184b05bc5cbf2ed3718579.svg "Repobeats analytics image")

# ESP-Miner
esp-miner is open source ESP32 firmware for the [Bitaxe](https://github.com/bitaxeorg/bitaxe)

If you are looking for premade images to load on your Bitaxe, check out the [latest release](https://github.com/bitaxeorg/ESP-Miner/releases/latest) page. Maybe you want [instructions](https://github.com/bitaxeorg/ESP-Miner/blob/master/flashing.md) for loading factory images.

# Bitaxetool
We also have a command line python tool for flashing Bitaxe and updating the config called Bitaxetool 

**Bitaxetool Requires Python3.4 or later and pip**

Install bitaxetool from pip. pip is included with Python 3.4 but if you need to install it check <https://pip.pypa.io/en/stable/installation/>

```
pip install --upgrade bitaxetool
```
The bitaxetool includes all necessary library for flashing the binaries to the Bitaxe Hardware.

**Notes**
 - The bitaxetool does not work properly with esptool v5.x.x, esptool v4.9.0 or earlier is required.
 - Bitaxetool v0.6.1 - locked to using esptool v4.9.0

```
pip install bitaxetool==0.6.1
```

- Flash a "factory" image to a Bitaxe to reset to factory settings. Make sure to choose an image built for your hardware version (401) in this case:

```
bitaxetool --firmware ./esp-miner-factory-401-v2.4.2.bin
```
- Flash just the NVS config to a bitaxe:

```
bitaxetool --config ./config-401.cvs
```
- Flash both a factory image _and_ a config to your Bitaxe: note the settings in the config file will overwrite the config already baked into the factory image:

```
bitaxetool --config ./config-401.cvs --firmware ./esp-miner-factory-401-v2.4.2.bin
```

## AxeOS API
The esp-miner UI is called AxeOS and provides an API to expose actions and information.

For more details take a look at [`main/http_server/openapi.yaml`](./main/http_server/openapi.yaml).

Available API endpoints:
  
**GET**

* `/api/system/info` Get system information
* `/api/system/asic` Get ASIC settings information
* `/api/system/statistics` Get system statistics (data logging should be activated)
* `/api/system/statistics/dashboard` Get system statistics for dashboard
* `/api/system/wifi/scan` Scan for available Wi-Fi networks

**POST**

* `/api/system/restart` Restart the system
* `/api/system/OTA` Update system firmware
* `/api/system/OTAWWW` Update AxeOS

**PATCH**

* `/api/system` Update system settings

### API examples in `curl`:

```bash
# Get system information
curl http://YOUR-BITAXE-IP/api/system/info

# Get ASIC settings information
curl http://YOUR-BITAXE-IP/api/system/asic

# Get system statistics
curl http://YOUR-BITAXE-IP/api/system/statistics

# Get dashboard statistics
curl http://YOUR-BITAXE-IP/api/system/statistics/dashboard

# Get available Wi-Fi networks
curl http://YOUR-BITAXE-IP/api/system/wifi/scan


# Restart the system
curl -X POST http://YOUR-BITAXE-IP/api/system/restart

# Update system firmware
curl -X POST \
     -H "Content-Type: application/octet-stream" \
     --data-binary "@esp-miner.bin" \
     http://YOUR-BITAXE-IP/api/system/OTA

# Update AxeOS
curl -X POST \
     -H "Content-Type: application/octet-stream" \
     --data-binary "@www.bin" \
     http://YOUR-BITAXE-IP/api/system/OTAWWW


# Update system settings
curl -X PATCH http://YOUR-BITAXE-IP/api/system \
     -H "Content-Type: application/json" \
     -d '{"fanspeed": "desired_speed_value"}'
```

## Administration

The firmware hosts a small web server on port 80 for administrative purposes. Once the Bitaxe device is connected to the local network, the admin web front end may be accessed via a web browser connected to the same network at `http://<IP>`, replacing `IP` with the LAN IP address of the Bitaxe device, or `http://bitaxe`, provided your network supports mDNS configuration.

### Recovery

In the event that the admin web front end is inaccessible, for example because of an unsuccessful firmware update (`www.bin`), a recovery page can be accessed at `http://<IP>/recovery`.

### Unlock Settings

In order to unlock the Input fields for ASIC Frequency and ASIC Core Voltage you need to append `?oc` to the end of the settings tab URL in your browser. Be aware that without additional cooling overclocking can overheat and/or damage your Bitaxe.

## Development

### Prerequisites

- Install the ESP-IDF toolchain from https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/
- Install nodejs/npm from https://nodejs.org/en/download
- (Optional) Install the ESP-IDF extension for VSCode from https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension

### Building

At the root of the repository, run:
```
idf.py build && ./merge_bin.sh ./esp-miner-merged.bin
```

Note: the merge_bin.sh script is a custom script that merges the bootloader, partition table, and the application binary into a single file.

Note: if using VSCode, you may have to configure the settings.json file to match your esp hardware version. For example, if your bitaxe has something other than an esp32-s3, you will need to change the version in the `.vscode/settings.json` file.

### Flashing

With the bitaxe connected to your computer via USB, run:

```
bitaxetool --config ./config-xxx.cvs --firmware ./esp-miner-merged.bin
```

where xxx is the config file for your hardware version. You can see the list of available config files in the root of the repository.

A custom board version is also possible with `config-custom.cvs`. A custom board needs to be based on an existing `devicemodel` and `asicmodel`.

**Notes:** 
  - If you are developing within a dev container, you will need to run the bitaxetool command from outside the container. Otherwise, you will get an error about the device not being found.
  - Some Bitaxe versions can't directly connect to a USB-C port. If yours is affected use a USB-A adapter as a workaround. More about it [here](https://github.com/bitaxeorg/bitaxeGamma/issues/37).
  - Only ESP32-S3-WROOM-1 module type N16R8 (16MB Flash, 8MB Octal SPI PSRAM) is supported. This model number should be visible on the ESP32 module. Other module types without PSRAM or with Quad SPI PSRAM will not work with the normal firmware. More about it [here](https://github.com/bitaxeorg/ESP-Miner/issues/826).

### Wi-Fi routers

There are some Wi-Fi routers that will block mining, ASUS Wi-Fi routers & some TP-Link Wi-Fi routers for example.
If you find that your not able to mine / have no hash rate you will need to check the Wi-Fi routers settings and disable the following;

1/ AiProtection

2/ IoT 

If your Wi-Fi router has both of these options you might have to disable them both.

If your still having problems here, check other settings within the Wi-Fi router and the bitaxe device, this includes the URL for
the Stratum Host and Stratum Port.

## Attributions

The display font is Portfolio 6x8 from https://int10h.org/oldschool-pc-fonts/ by VileR.
