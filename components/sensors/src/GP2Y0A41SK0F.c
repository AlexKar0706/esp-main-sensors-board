#include <stdio.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"

// Configuration - adjust pin based on your needs
#define GP2Y0A41SK0F_ADC_CHANNEL ADC_CHANNEL_2  // Change to your ADC channel
#define GP2Y0A41SK0F_ADC_UNIT ADC_UNIT_1


static const char *TAG = "gp2y0a41sk0f";

static adc_oneshot_unit_handle_t adc_handle;

/**
 * Initialize the GP2Y0A41SK0F sensor
 */
void gp2y0a41sk0f_init(void) 
{
    //-------------ADC1 Init---------------//
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = GP2Y0A41SK0F_ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc_handle));

    //-------------ADC1 Config---------------//
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, GP2Y0A41SK0F_ADC_CHANNEL, &config));

    ESP_LOGI(TAG, "Sensor is ON");
}

/**
 * Read sensor measurement
 * @return distance in mm
 */
uint32_t gp2y0a41sk0f_read_measurements(void) 
{
    int adc_raw = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, GP2Y0A41SK0F_ADC_CHANNEL, &adc_raw));
    // ESP_LOGI(TAG, "Raw ADC value: %d", adc_raw);
    return (uint32_t)(400 - (adc_raw * 370) / 0xFFF);
}