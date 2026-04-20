#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>

// Configuration - adjust these pins as needed
#define HC_SR04_TRIG_PIN GPIO_NUM_19
#define HC_SR04_ECHO_PIN GPIO_NUM_20

static const char *TAG = "hc-sr04";

static volatile uint32_t echo_time_us = 0;
static volatile uint32_t echo_start_time = 0;
static esp_timer_handle_t hc_sr04_start_measurement_timer;
static portMUX_TYPE echo_isr_mux = portMUX_INITIALIZER_UNLOCKED;

// ISR for Echo pin falling edge
static void IRAM_ATTR echo_isr_handler(void *arg) 
{
    uint32_t level = gpio_get_level(HC_SR04_ECHO_PIN);
    
    if (level) {
        // Rising edge - start measuring
        echo_start_time = esp_timer_get_time();
    } else {
        // Falling edge - stop measuring
        portENTER_CRITICAL_ISR(&echo_isr_mux);
        echo_time_us = esp_timer_get_time() - echo_start_time;
        portEXIT_CRITICAL_ISR(&echo_isr_mux);
        uint32_t next_measurement_start_us = 1000; // Default to 1ms if echo_time_us is very long (e.g., timeout)
        if (echo_time_us < 60000) next_measurement_start_us = 60000 - echo_time_us; // Schedule next measurement after 60ms from the start of the current measurement
        esp_timer_start_once(hc_sr04_start_measurement_timer, next_measurement_start_us);
    }
}

static void hc_sr04_start_measurement_timer_callback(void* arg) 
{
    // Send 10us pulse on TRIG pin
    gpio_set_level(HC_SR04_TRIG_PIN, 1);
    esp_rom_delay_us(10);
    gpio_set_level(HC_SR04_TRIG_PIN, 0);
}

void hc_sr04_init(void) 
{
    // Configure TRIG pin as output
    gpio_config_t trig_config = {
        .pin_bit_mask = (1ULL << HC_SR04_TRIG_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&trig_config);
    
    // Configure ECHO pin as input with interrupt
    gpio_config_t echo_config = {
        .pin_bit_mask = (1ULL << HC_SR04_ECHO_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&echo_config);
    
    const esp_timer_create_args_t hc_sr04_start_measurement_timer_args = {
            .callback = &hc_sr04_start_measurement_timer_callback,
            .name = "hc_sr04_start_measurement_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&hc_sr04_start_measurement_timer_args, &hc_sr04_start_measurement_timer));
    
    // Install ISR
    gpio_install_isr_service(0);
    gpio_isr_handler_add(HC_SR04_ECHO_PIN, echo_isr_handler, NULL);
    
    // Initialize TRIG pin to LOW
    gpio_set_level(HC_SR04_TRIG_PIN, 0);

    // Start first measurement after 1ms
    ESP_ERROR_CHECK(esp_timer_start_once(hc_sr04_start_measurement_timer, 1000));

    ESP_LOGI(TAG, "Sensor is ON");
}

uint32_t hc_sr04_read_measurement(void) 
{
    uint32_t distance_mm = 0;
    portENTER_CRITICAL(&echo_isr_mux);
    distance_mm = (echo_time_us * 10) / 58; // Convert time to distance in mm
    portEXIT_CRITICAL(&echo_isr_mux);
    return distance_mm; // Timeout
}