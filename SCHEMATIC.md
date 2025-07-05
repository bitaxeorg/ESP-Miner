48V PSU Input
    |
    +--[ASIC1]--+--[ASIC2]--+--[ASIC3]--+--...--+--[ASIC12]--GND
    |           |           |           |        |
    |         46.8V       45.6V       44.4V    1.2V
    |           |           |           |        |
    |        [LM358]     [LM358]     [LM358]  [LM358]
    |           |           |           |        |
    |           +-----+-----+-----+-----+--------+
    |                 |
    |           [MUX Stage 1]
    |           5x CD4051 (8:1)
    |                 |
    |           [MUX Stage 2]  
    |           1x CD4051 (8:1)
    |                 |
    |            [ADS1115]
    |            16-bit ADC
    |              I2C
    |                 |
    +------------[ESP32]
                   |
                GPIO4-9 (Mux Control)
                   |
                Web API (/api/voltage)

Signal Flow:
- Each ASIC: ~1.2V drop
- LM358: Unity gain buffer (high-Z input)
- Mux: Routes one voltage to ADC
- ADC: 0-4.096V range, 16-bit resolution
- ESP32: Scans all channels every 5 seconds
