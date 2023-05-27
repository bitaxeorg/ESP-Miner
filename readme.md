# ESP-Miner

ESP-Miner is bitcoin miner software designed to run on the ESP32. It mines on ASICs such as the Bitmain BM1397. The [Bitaxe](https://github.com/skot/bitaxe/) is a handy board for this!

## Hardware Required

This example can be run on any commonly available ESP32 development board.

## Configure the project

Set the target

```
idf.py set-target esp32s3
```

Use menuconfig to set the stratum server address/port and WiFi SSID/Password
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

* Set `Stratum address` to the stratum pool domain name. example "solo.ckpool.org"

* Set `Stratum username` to the stratum pool username

* Set `Stratum password` to the stratum pool password

For more information about the example_connect() method used here, check out https://github.com/espressif/esp-idf/blob/master/examples/protocols/README.md.


## Build and Flash

Build the project and flash it to the board, then run monitor tool to view serial output:

```
idf.py -p PORT flash monitor
```

(To exit the serial monitor, type ``Ctrl-]``.)

See the Getting Started Guide for full steps to configure and use ESP-IDF to build projects.

## Run Unit tests

The unit tests for the project use the unity test framework and currently require actual esp32 hardware to run.

They are located at https://github.com/johnny9/esp-miner/tree/master/components/stratum/test

```
cd ./test/
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

