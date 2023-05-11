# ESP-Miner

ESP-Miner is bitcoin miner software designed to run on the ESP32. It mines on ASICs such as the Bitmain BM1397. The [Bitaxe](https://github.com/skot/bitaxe/) is a handy board for this!

## Overview

- ESP-Miner connects to your pool or stratum server and subscribes to get the latest work.
    - This is done with [JSON-RPC](https://www.jsonrpc.org)
    - Via the [Stratum](https://braiins.com/stratum-v1/docs) protocol
    - [cJSON](https://github.com/DaveGamble/cJSON) seems like a good embedded library for doing this.
- ESP-Miner gets the latest work and formats it to be sent to the mining ASIC.
    - There isn't much change here except for computing the midstates, and shifting some bytes around
        - Beware of endianess!
        - How do we do this? Examples in cgminer Kano edition.
        - I have started on this.. [check this](bm1397_protocol.md)
- ESP-Miner sends this work to the mining ASIC over serial.

### Hardware Required

To run this example, you should have one ESP32, ESP32-S or ESP32-C based development board as well as a MPU9250. MPU9250 is a inertial measurement unit, which contains a accelerometer, gyroscope as well as a magnetometer, for more information about it, you can read the [PDF](https://invensense.tdk.com/wp-content/uploads/2015/02/PS-MPU-9250A-01-v1.1.pdf) of this sensor.

#### Pin Assignment:

**Note:** The following pin assignments are used by default, you can change these in the `menuconfig` .

|                  | SDA             | SCL           |
| ---------------- | -------------- | -------------- |
| ESP I2C Master   | I2C_MASTER_SDA | I2C_MASTER_SCL |
| MPU9250 Sensor   | SDA            | SCL            |


For the actual default value of `I2C_MASTER_SDA` and `I2C_MASTER_SCL` see `Example Configuration` in `menuconfig`.

**Note: ** There’s no need to add an external pull-up resistors for SDA/SCL pin, because the driver will enable the internal pull-up resistors.

### Build and Flash

Enter `idf.py -p PORT flash monitor` to build, flash and monitor the project.

(To exit the serial monitor, type ``Ctrl-]``.)

See the [Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html) for full steps to configure and use ESP-IDF to build projects.

