[![](https://dcbadge.vercel.app/api/server/3E8ca2dkcC)](https://discord.gg/3E8ca2dkcC)

# ESP-Miner

| Supported Targets | ESP32-S3 (BitAxe v2+) |
| ----------------- | --------------------- |

## Requires Python3.4 or later and pip

Install bitaxetool from pip. pip is included with Python 3.4 but if you need to install it check <https://pip.pypa.io/en/stable/installation/>

```
pip install --upgrade bitaxetool
```

## Hardware Required

This firmware is designed to run on a BitAxe v2+

If you do have a Bitaxe with no USB connectivity make sure to establish a serial connection with either a JTAG ESP-Prog device or a USB-to-UART bridge

## Preconfiguration

Starting with v2.0.0, the ESP-Miner firmware requires some basic manufacturing data to be flashed in the NVS partition.

1. Download the esp-miner-factory-v2.0.3.bin file from the release tab.
   Click [here](https://github.com/skot/ESP-Miner/releases) for the release tab

2. Copy `config.cvs.example` to `config.cvs` and modify `asicfrequency`, `asicvoltage`, `asicmodel`, `devicemodel`, and `boardversion`

The following are recommendations but it is necessary that you do have all values in your `config.cvs`file to flash properly.

- recommended values for the Bitaxe 1368 (supra)

  ```
  key,type,encoding,value
  main,namespace,,
  asicfrequency,data,u16,490
  asicvoltage,data,u16,1200
  asicmodel,data,string,BM1368
  devicemodel,data,string,supra
  boardversion,data,string,400
  ```

- recommended values for the Bitaxe 1366 (ultra)

  ```
  key,type,encoding,value
  main,namespace,,
  asicfrequency,data,u16,485
  asicvoltage,data,u16,1200
  asicmodel,data,string,BM1366
  devicemodel,data,string,ultra
  boardversion,data,string,0.11
  ```

- recomended values for the Bitaxe 1397 (MAX)

  ```
  key,type,encoding,value
  main,namespace,,
  asicfrequency,data,u16,475
  asicvoltage,data,u16,1400
  asicmodel,data,string,BM1397
  devicemodel,data,string,max
  boardversion,data,string,2.2
  ```

## Flash

The bitaxetool includes all necessary library for flashing the binary file to the Bitaxe Hardware.

The bitaxetool requires a config.cvs preloaded file and the appropiate firmware.bin file in it's executed directory.

3. Flash with the bitaxetool

```
bitaxetool --config ./config.cvs --firmware ./esp-miner-factory-v2.0.3.bin
```
