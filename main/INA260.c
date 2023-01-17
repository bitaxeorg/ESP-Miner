//INA260 -- Power meter
//address: 0x40
#define INA260_SENSOR_ADDR                  0x40        //Slave address of the INA260
#define INA260_MANFID_REG                   0xFE        //should be 0x5449

    // /* Read the INA260 WHO_AM_I register, on power up the register should have the value 0x5449 */
    // ESP_ERROR_CHECK(register_read(INA260_SENSOR_ADDR, INA260_MANFID_REG, data, 2));
    // ESP_LOGI(TAG, "INA260 MANF ID = 0x%02X%02X", data[0], data[1]);