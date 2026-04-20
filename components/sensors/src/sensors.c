#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "GP2Y0A41SK0F.h"
#include "HC-SR04.h"
#include "VL53L1X.h"

static const char *TAG = "Sensors";

static inline uint32_t clamp_u32(uint32_t x, uint32_t low, uint32_t high)
{
    if (x < low) return low;
    if (x > high) return high;
    return x;
}

void sensors_init(void)
{
    // Initialize IR sensor
    gp2y0a41sk0f_init();

    // Initialize ultrasonic sensor
    hc_sr04_init();

	// Initialize ToF sensor
	vl53l1x_init();
}

uint32_t sensor_tof_get_current_distance(void) 
{
    uint32_t distance_mm = clamp_u32(vl53l1x_read_measurement(), 0, 999);
    if (distance_mm == 999) {
        ESP_LOGW(TAG, "ToF sensor measurement is out of range");
        distance_mm = 0; // Return 0 for out of range instead of 999 to avoid confusion
    }
    return distance_mm;
}

uint32_t sensor_ir_get_current_distance(void) 
{
    uint32_t distance_mm = clamp_u32(gp2y0a41sk0f_read_measurements(), 0, 999);
    if (distance_mm == 999) {
        ESP_LOGW(TAG, "IR sensor measurement is out of range");
        distance_mm = 0; // Return 0 for out of range instead of 999 to avoid confusion
    }
    return distance_mm;
}

uint32_t sensor_ultrasonic_get_current_distance(void) 
{
    uint32_t distance_mm = clamp_u32(hc_sr04_read_measurement(), 0, 999);
    if (distance_mm == 999) {
        ESP_LOGW(TAG, "Ultrasonic sensor measurement is out of range");
        distance_mm = 0; // Return 0 for out of range instead of 999 to avoid confusion
    }
    return distance_mm;
}
