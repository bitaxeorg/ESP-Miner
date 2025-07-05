#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define ADC_ATTEN   ADC_ATTEN_DB_12
#define ADC_CHANNEL ADC_CHANNEL_1

static const char * TAG = "adc";

static adc_cali_handle_t adc1_cali_chan1_handle;
static adc_oneshot_unit_handle_t adc1_handle;


/*---------------------------------------------------------------
        ADC Calibration
---------------------------------------------------------------*/
static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

// Sets up the ADC to read Vcore. Run this before ADC_get_vcore()
void ADC_init(void)
{
    // adc1_config_channel_atten(ADC1_CHANNEL_1, ADC_ATTEN_DB_12);
    // esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_DEFAULT, 0, &adc1_chars);


    //-------------ADC1 Init---------------//
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    //-------------ADC1 Config---------------//
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL, &config));

    //-------------ADC1 Calibration Init---------------//
    adc_calibration_init(ADC_UNIT_1, ADC_CHANNEL, ADC_ATTEN, &adc1_cali_chan1_handle);

}

// returns the ADC voltage in mV
uint16_t ADC_get_vcore(void)
{
    int adc_raw = 0;
    int voltage = 0;

    if (adc_oneshot_read(adc1_handle, ADC_CHANNEL, &adc_raw) != ESP_OK) {
        ESP_LOGE(TAG, "ADC read failed");
        return 0;
    }
    if (adc_cali_raw_to_voltage(adc1_cali_chan1_handle, adc_raw, &voltage) != ESP_OK) {
        ESP_LOGE(TAG, "ADC calibration failed");
        return 0;
    }

    return voltage;
}