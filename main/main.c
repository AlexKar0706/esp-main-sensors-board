/*  WiFi softAP implementation of example, adapted from the original example code in esp-idf

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "softap.h"
#include "http_server.h"
#include "espnow_driver.h"
#include "sensors.h"

#include "lv_examples.h"
#include "bsp/esp-bsp.h"

#if (SENSORS_DATA_TYPE == SENSORS_DATA_TYPE_ID)
#define GLASS_HEIGHT_MM                     (75)
#define CLASSIFICATION_STEPS                (5)
#define CLASSIFICATION_STEP_MM              (GLASS_HEIGHT_MM / CLASSIFICATION_STEPS)
#define CLASSIFICATION_HYSTERESIS_MM        (2)
#define CLASSIFICATION_AVERAGE_WINDOW_SIZE  (5)
#endif

static const char *TAG = "Main";

static inline float clamp_f(float x, float low, float high)
{
    if (x < low) return low;
    if (x > high) return high;
    return x;
}

#if (SENSORS_DATA_TYPE == SENSORS_DATA_TYPE_ID)
static inline int clamp_i(int x, int low, int high)
{
    if (x < low) return low;
    if (x > high) return high;
    return x;
}

static inline void classify_sensor(int* p_id, int sensor_height_mm) 
{
    sensor_height_mm = clamp_i(sensor_height_mm, 0, GLASS_HEIGHT_MM);

    if (sensor_height_mm >= 0) {
        int sensor_calculated_id = sensor_height_mm / CLASSIFICATION_STEP_MM;
        
        if (*p_id == 9) {
            *p_id = sensor_calculated_id;
        } else {
            if (sensor_calculated_id > *p_id) {
                sensor_height_mm -= CLASSIFICATION_HYSTERESIS_MM; // Add hysteresis to avoid noise caused classification jumps
                sensor_height_mm = clamp_i(sensor_height_mm, 0, GLASS_HEIGHT_MM);
                *p_id = (sensor_height_mm / CLASSIFICATION_STEP_MM);
            } else if (sensor_calculated_id < *p_id) {
                sensor_height_mm += CLASSIFICATION_HYSTERESIS_MM; // Add hysteresis to avoid noise caused classification jumps
                sensor_height_mm = clamp_i(sensor_height_mm, 0, GLASS_HEIGHT_MM);
                *p_id = (sensor_height_mm / CLASSIFICATION_STEP_MM);
            }
        }
    }

}


static inline void classify_sensors_data(sensor_data_t* data) 
{
    static int tof_average_window[CLASSIFICATION_AVERAGE_WINDOW_SIZE] = {0};
    static int us_average_window[CLASSIFICATION_AVERAGE_WINDOW_SIZE] = {0};
    static int ir_average_window[CLASSIFICATION_AVERAGE_WINDOW_SIZE] = {0};
    static int tof_top_mm = 0;
    static int us_top_mm = 0;
    static int ir_top_mm = 0;
    static size_t captured_samples = 0;
    static bool is_top_calculated = false;

    // Update average windows
    memmove(tof_average_window, tof_average_window + 1, (sizeof(tof_average_window) / sizeof(tof_average_window[0]) - 1) * sizeof(tof_average_window[0]));
    memmove(us_average_window, us_average_window + 1, (sizeof(us_average_window) / sizeof(us_average_window[0]) - 1) * sizeof(us_average_window[0]));
    memmove(ir_average_window, ir_average_window + 1, (sizeof(ir_average_window) / sizeof(ir_average_window[0]) - 1) * sizeof(ir_average_window[0]));
    tof_average_window[sizeof(tof_average_window) / sizeof(tof_average_window[0]) - 1] = data->tof_mm;
    us_average_window[sizeof(us_average_window) / sizeof(us_average_window[0]) - 1] = data->us_mm;
    ir_average_window[sizeof(ir_average_window) / sizeof(ir_average_window[0]) - 1] = data->ir_mm;
    captured_samples++;
    
    int tof_average = 0;
    int us_average = 0;
    int ir_average = 0;
    for (size_t i = 0; i < sizeof(tof_average_window) / sizeof(tof_average_window[0]); i++) tof_average += tof_average_window[i];
    for (size_t i = 0; i < sizeof(us_average_window) / sizeof(us_average_window[0]); i++) us_average += us_average_window[i];
    for (size_t i = 0; i < sizeof(ir_average_window) / sizeof(ir_average_window[0]); i++) ir_average += ir_average_window[i];
    tof_average /= sizeof(tof_average_window) / sizeof(tof_average_window[0]);
    us_average /= sizeof(us_average_window) / sizeof(us_average_window[0]);
    ir_average /= sizeof(ir_average_window) / sizeof(ir_average_window[0]);
    tof_average = clamp_i(tof_average, 0, INT_MAX);
    us_average = clamp_i(us_average, 0, INT_MAX);
    ir_average = clamp_i(ir_average, 0, INT_MAX);

    if (captured_samples >= (sizeof(ir_average_window) / sizeof(ir_average_window[0]) - 1) && !is_top_calculated) {
        tof_top_mm = tof_average;
        us_top_mm = us_average;
        ir_top_mm = ir_average;
        data->tof_id = 9; // 9 means no object
        data->us_id = 9; // 9 means no object
        data->ir_id = 9; // 9 means no object
        is_top_calculated = true;

        ESP_LOGI(TAG, "Top values calculated: ToF=%dmm, US=%dmm, IR=%dmm", tof_top_mm, us_top_mm, ir_top_mm);
    }

    // ToF classification
    int tof_height_mm = tof_top_mm - tof_average;
    classify_sensor(&data->tof_id, (tof_top_mm != 0) ? tof_height_mm : -1);

    // Ultrasonic classification
    int us_height_mm = us_top_mm - us_average;
    classify_sensor(&data->us_id, (us_top_mm != 0) ? us_height_mm : -1);

    // IR classification
    int ir_height_mm = ir_top_mm - ir_average;
    classify_sensor(&data->ir_id, (ir_top_mm != 0) ? ir_height_mm : -1);
}
#endif

static void main_task(void *pvParameter) 
{
    static sensor_data_t sensors_data = {0};
    espnow_payload_t payload = {0};

    ESP_LOGI(TAG, "Main task started");
    while (1) {
        vTaskDelay(100 / portTICK_PERIOD_MS);

        /* Gather sensors data */
        sensors_data.tof_mm = sensor_tof_get_current_distance();
        sensors_data.ir_mm = sensor_ir_get_current_distance();
        sensors_data.us_mm = sensor_ultrasonic_get_current_distance();

#if (SENSORS_DATA_TYPE == SENSORS_DATA_TYPE_ID)
        classify_sensors_data(&sensors_data);
#endif

        if (espnow_driver_receive(&payload) == ESP_OK) {
            for (size_t i = 0; i < sizeof(payload.model_results) / sizeof(payload.model_results[0]); i++) {
                espnow_model_result_t* result = &payload.model_results[i];
                if (result->rect_size == 0) continue; // Skip empty results
                if (result->class_id > 5) {
                    ESP_LOGW(TAG, "Received invalid class_id %d from ESP-NOW payload, skipping", result->class_id);
                    continue; // Skip invalid class_id
                }
                // Otherwise accept object
                sensors_data.camera_id = result->class_id;
                sensors_data.camera_score = clamp_f(result->score, 0.0000f, 1.0000f);
                break;
            }
        } else {
            sensors_data.camera_id = 9; // Camera is busy now
            sensors_data.camera_score = 0.0f;
        }

        /* Put new data to CSV */
        put_sensors_data_to_csv(&sensors_data);
    }
}

void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    //bsp_display_start();
    ESP_LOGI("MEM", "PSRAM free: %u", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    sensors_init();
    softap_init();
    http_server_init();
    espnow_driver_init();

    xTaskCreate(main_task, "main_task", 4096, NULL, 5, NULL);

    // ESP_LOGI(TAG, "Display LVGL demo");
    // bsp_display_backlight_on();
    // bsp_display_lock(0);
    // lv_example_anim_timeline_1();
    // bsp_display_unlock();
}
