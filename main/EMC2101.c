//EMC2101 -- Fan and temp sensor controller
//address: 0x4C
#define EMC2101_SENSOR_ADDR                 0x4C        //Slave address of the EMC2101
#define EMC2101_PRODUCTID_REG               0xFD        //should be 0x16 or 0x28

    // /* Read the EMC2101 WHO_AM_I register, on power up the register should have the value 0x16 or 0x28 */
    // ESP_ERROR_CHECK(register_read(EMC2101_SENSOR_ADDR, EMC2101_PRODUCTID_REG, data, 1));
    // ESP_LOGI(TAG, "EMC2101 PRODUCT ID = 0x%02X", data[0]);