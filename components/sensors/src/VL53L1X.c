#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "esp_log.h"
#include "VL53L1X.h"

static const char *TAG = "vl53l1x";

static VL53L1_Dev_t dev;
static bool sensor_active = false;

/**
 * Initialize the VL53L1X sensor
 */
void vl53l1x_init(void) 
{
    // timing_budget should be within 0.02 and 1 s
    double timing_budget = 0.1;

    // The minimum inter-measurement period must be longer than the timing budget + 4 ms (*)
    double inter_measurement_period = timing_budget + 0.004;

	dev.I2cDevAddr = VL53L1_DEVICE_ADDRESS;
    VL53L1_CommsInitialise(&dev, VL53L1_I2C, 100);
	VL53L1_software_reset(&dev);
	VL53L1_WaitDeviceBooted(&dev);
	VL53L1_DataInit(&dev);
	VL53L1_StaticInit(&dev);
	VL53L1_SetPresetMode(&dev, VL53L1_PRESETMODE_AUTONOMOUS);

    // Print device info
	VL53L1_DeviceInfo_t device_info;
	VL53L1_CHECK_STATUS(VL53L1_GetDeviceInfo(&dev, &device_info));
	ESP_LOGI(TAG, "Device name: %." VL53L1_STR(VL53L1_DEVINFO_STRLEN) "s", device_info.Name);
	ESP_LOGI(TAG, "Device type: %." VL53L1_STR(VL53L1_DEVINFO_STRLEN) "s", device_info.Type);
	ESP_LOGI(TAG, "Product ID: %." VL53L1_STR(VL53L1_DEVINFO_STRLEN) "s", device_info.ProductId);
	ESP_LOGI(TAG, "Type: %u Version: %u.%u", device_info.ProductType,
	          device_info.ProductRevisionMajor, device_info.ProductRevisionMinor);

    // Setup sensor
	VL53L1_CHECK_STATUS(VL53L1_SetDistanceMode(&dev, VL53L1_DISTANCEMODE_SHORT));
	VL53L1_CHECK_STATUS(VL53L1_SetMeasurementTimingBudgetMicroSeconds(&dev, round(timing_budget * 1e6)));

    // double min_signal;
	// if (nh_priv.getParam("min_signal", min_signal)) {
	// 	CHECK_STATUS(VL53L1_SetLimitCheckValue(&dev, VL53L1_CHECKENABLE_SIGNAL_RATE_FINAL_RANGE, min_signal * 65536));
	// }

	// double max_sigma;
	// if (nh_priv.getParam("max_sigma", max_sigma)) {
	// 	CHECK_STATUS(VL53L1_SetLimitCheckValue(&dev, VL53L1_CHECKENABLE_SIGMA_FINAL_RANGE, max_sigma * 1000 * 65536));
	// }

    // Start sensor
    VL53L1_Error dev_error;
	for (int i = 0; i < 100; i++) {
		VL53L1_CHECK_STATUS(VL53L1_SetInterMeasurementPeriodMilliSeconds(&dev, round(inter_measurement_period * 1e3)));
		dev_error = VL53L1_StartMeasurement(&dev);
		if (dev_error == VL53L1_ERROR_INVALID_PARAMS) {
			inter_measurement_period += 0.001; // Increase inter_measurement_period to satisfy condition (*)
		} else break;
	}

    // Check for errors after start
	if (dev_error != VL53L1_ERROR_NONE) {
		ESP_LOGE(TAG, "Can't start measurement: error %d", dev_error);
        ESP_LOGE(TAG, "Device is disabled");
        VL53L1_StopMeasurement(&dev);
        VL53L1_CommsClose(&dev);
        return;
	}

	sensor_active = true;
    ESP_LOGI(TAG, "Sensor is ON");
}

/**
 * Read sensor measurement
 * @return distance in mm
 */
uint32_t vl53l1x_read_measurement(void) 
{
	static uint32_t distance_mm = 0;

	if (sensor_active) {
		// Check the data is ready
		uint8_t data_ready = 0;
		VL53L1_GetMeasurementDataReady(&dev, &data_ready);

		if (data_ready) {
			VL53L1_RangingMeasurementData_t measurement_data;

			// Read measurement
			VL53L1_GetRangingMeasurementData(&dev, &measurement_data);
			VL53L1_ClearInterruptAndStartMeasurement(&dev);

			if (measurement_data.RangeStatus != VL53L1_RANGESTATUS_RANGE_VALID) {
				char range_status[VL53L1_MAX_STRING_LENGTH];
				VL53L1_get_range_status_string(measurement_data.RangeStatus, range_status);
				ESP_LOGW(TAG, "Range measurement status is not valid: %s", range_status);
				measurement_data.RangeMilliMeter = 0;
			}

			distance_mm = (uint32_t)measurement_data.RangeMilliMeter;
		}

	}

    return distance_mm;
}