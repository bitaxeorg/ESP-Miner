[![](https://dcbadge.vercel.app/api/server/3E8ca2dkcC)](https://discord.gg/3E8ca2dkcC)



# ESP-Miner

| Supported Targets | ESP32-S3 (BitAxe v2+) |
| ----------------- | --------------------- |


## Requires ESP-IDF v5.1

You can chose between 2 methods of installations:

### Manual Installation

Follow the official [instructions](https://docs.espressif.com/projects/esp-idf/en/release-v5.1/esp32s3/get-started/index.html#manual-installation).

### ESP-IDF Visual Studio Code Extension

Install the "Espressif IDF" extension, it will automate the IDF installation for you.

## Hardware Required

This firmware is designed to run on a BitAxe v2+

## Configure the project

Set the target

```
idf.py set-target esp32s3
```

Use menuconfig to set the stratum server address/port and WiFi SSID/Password

```
idf.py menuconfig
```

Set following parameters under Stratum Configuration Options, these will define the stratum server you connect to:

* Set `Stratum Address` to the stratum pool domain name. example "public-pool.io"

* Set `Stratum Port` to the stratum pool port. example "21496"

* Set `Stratum username` to the stratum pool username. example "<my_BTC_address>.bitaxe"

* Set `Stratum password` to the stratum pool password. example "x"

Set following parameters under Example Connection Configuration Options:

* Set `WiFi SSID` to your target wifi network SSID.

* Set `Wifi Password` to the password for your target SSID.

For more information about the example_connect() method used here, check out <https://github.com/espressif/esp-idf/blob/master/examples/protocols/README.md>.

## Build website

To build the website for viewing and OTA updates open the Angular project found in
```
ESP-Miner\main\http_server\axe-os
```

Then install dependencies and build.

```
npm i
npm run build
```


 When the esp-idf project is built it will bundle the website in www.bin


## Build and Flash

Build the project and flash it to the board, then run monitor tool to view serial output:

```
idf.py -p PORT flash monitor
```

(To exit the serial monitor, type ``Ctrl-]``.)

See the Getting Started Guide for full steps to configure and use ESP-IDF to build projects.

## Run Unit tests

The unit tests for the project use the unity test framework and currently require actual esp32 hardware to run.

They are located at <https://github.com/johnny9/esp-miner/tree/master/components/stratum/test>

```
cd ./test/
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```
