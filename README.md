| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- | -------- |


# ESP-miner

## Hardware Required

This example can be run on any commonly available ESP32 development board.

## Configure the project

```
idf.py menuconfig
```
Set following parameters under Example Configuration Options, these will define the stratum server you connect to:

* Set IP version of example to be IPV4 or IPV6.

* Set IPV4 Address in case your chose IP version IPV4 above.

* Set IPV6 Address in case your chose IP version IPV6 above.

    Set Port number that represents remote port the example will connect to.

Set following parameters under Example Connection Configuration Options:

* Set `WiFi SSID` to your target wifi network SSID.

* Set `Wifi Password` to the password for your target SSID.

For more information about the example_connect() method used here, check out https://github.com/espressif/esp-idf/blob/master/examples/protocols/README.md.

Username is currently hardcoded in miner.c. Modify the defines to change these.

```
#define STRATUM_USERNAME "johnny9.esp"
```



## Build and Flash

Build the project and flash it to the board, then run monitor tool to view serial output:

```
idf.py -p PORT flash monitor
```

(To exit the serial monitor, type ``Ctrl-]``.)

See the Getting Started Guide for full steps to configure and use ESP-IDF to build projects.
