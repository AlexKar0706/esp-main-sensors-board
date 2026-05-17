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

static const char *TAG = "Main";

static inline float clamp_f(float x, float low, float high)
{
    if (x < low) return low;
    if (x > high) return high;
    return x;
}

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
