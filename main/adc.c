#include "driver/adc.h"
#include "esp_adc_cal.h"

// static const char *TAG = "adc.c";
static esp_adc_cal_characteristics_t adc1_chars;

// Sets up the ADC to read Vcore. Run this before ADC_get_vcore()
void ADC_init(void)
{
    adc1_config_channel_atten(ADC1_CHANNEL_1, ADC_ATTEN_DB_11);
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_DEFAULT, 0, &adc1_chars);
}

// returns the ADC voltage in mV
uint16_t ADC_get_vcore(void)
{
    adc1_config_width(ADC_WIDTH_BIT_DEFAULT);
    return esp_adc_cal_raw_to_voltage(adc1_get_raw(ADC1_CHANNEL_1), &adc1_chars);
}