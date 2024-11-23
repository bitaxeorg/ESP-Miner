[![](https://dcbadge.vercel.app/api/server/3E8ca2dkcC)](https://discord.gg/3E8ca2dkcC)

![GitHub Downloads (all assets, all releases)](https://img.shields.io/github/downloads/skot/esp-miner/total)
![GitHub commit activity](https://img.shields.io/github/commit-activity/t/skot/esp-miner)
![GitHub contributors](https://img.shields.io/github/contributors/skot/esp-miner)


# ESP-Miner
esp-miner is open source ESP32 firmware for the [Bitaxe](https://github.com/skot/bitaxe)

If you are looking for premade images to load on your Bitaxe, check out the [releases](https://github.com/skot/ESP-Miner/releases) page. Maybe you want [instructions](https://github.com/skot/ESP-Miner/blob/master/flashing.md) for loading factory images.

# Bitaxetool
We also have a command line python tool for flashing Bitaxe and updating the config called Bitaxetool 

**Bitaxetool Requires Python3.4 or later and pip**

Install bitaxetool from pip. pip is included with Python 3.4 but if you need to install it check <https://pip.pypa.io/en/stable/installation/>

```
pip install --upgrade bitaxetool
```
The bitaxetool includes all necessary library for flashing the binary file to the Bitaxe Hardware.

You need to provide a config.cvs file (see repo for examples) and the appropiate firmware.bin file in it's executed directory.

- Flash with the bitaxetool

```
bitaxetool --config ./config.cvs --firmware ./esp-miner-factory-v2.0.3.bin
```

## AxeOS API
The esp-miner UI is called AxeOS and provides an API to expose actions and information.

For more details take a look at `main/http_server/http_server.c`.

Things that can be done are:
  
  - Get System Info
  - Get Swarm Info
  - Update Swarm
  - Swarm Options
  - System Restart Action
  - Update System Settings Action
  - System Options
  - Update OTA Firmware
  - Update OTA WWW
  - WebSocket

Some API examples in curl:
  ```bash
  # Get system information
  curl http://YOUR-BITAXE-IP/api/system/info
  ```
  ```bash
  # Get swarm information
  curl http://YOUR-BITAXE-IP/api/swarm/info
  ```
  ```bash
  # System restart action
  curl -X POST http://YOUR-BITAXE-IP/api/system/restart
  ```

## Administration

The firmware hosts a small web server on port 80 for administrative purposes. Once the Bitaxe device is connected to the local network, the admin web front end may be accessed via a web browser connected to the same network at `http://<IP>`, replacing `IP` with the LAN IP address of the Bitaxe device, or `http://bitaxe`, provided your network supports mDNS configuration.

### Recovery

In the event that the admin web front end is inaccessible, for example because of an unsuccessful firmware update (`www.bin`), a recovery page can be accessed at `http://<IP>/recovery`.
