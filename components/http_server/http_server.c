/* Simple HTTP Server implementation of example, adapted from the original example code in esp-idf

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "esp_netif.h"
#include "esp_tls_crypto.h"
#include <esp_http_server.h>
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "esp_timer.h"
#include "esp_check.h"
#include <time.h>
#include <sys/time.h>
#if !CONFIG_IDF_TARGET_LINUX
#include <esp_wifi.h>
#include <esp_system.h>
#include "nvs_flash.h"
#include "esp_eth.h"
#endif  // !CONFIG_IDF_TARGET_LINUX
#include "http_server.h"

#define EXAMPLE_HTTP_QUERY_KEY_MAX_LEN  (64)

#define CSV_DATA_LINES       36000 /* 36000 lines of data for 1 hour of measurements with 100ms interval */
#define CSV_DATA_LINE_LENGTH 32 /* 7 digits timestamp + ToF(mm) 4 digits + Ultrasonic(mm) 4 digits + IR(mm) 4 digits + Camera(id) 1 digit + Camera(score) 6 digits + ',' for separation + '\n' */
#define CSV_DATA_META        53 /* "Boot Time(ms),ToF(mm),US(mm),IR(mm),Cm(id),Cm(score)\n" */

/* A simple example that demonstrates how to create GET and POST
 * handlers for the web server.
 */

static const char *TAG = "example";
static char* csv_data = NULL;
static size_t csv_data_index = 0;
static SemaphoreHandle_t csv_data_mutex = NULL;

/* An HTTP GET handler */
static esp_err_t data_get_handler(httpd_req_t *req)
{
    xSemaphoreTake(csv_data_mutex, portMAX_DELAY);
    httpd_resp_send(req, csv_data, csv_data_index);

    // End response
    httpd_resp_send_chunk(req, NULL, 0);
    xSemaphoreGive(csv_data_mutex);
    return ESP_OK;
}

static const httpd_uri_t data = {
    .uri       = "/data",
    .method    = HTTP_GET,
    .handler   = data_get_handler,
    .user_ctx  = NULL
};

/* An HTTP_ANY handler */
static esp_err_t any_handler(httpd_req_t *req)
{
    /* Send response with body set as the
     * string passed in user context*/
    const char* resp_str = (const char*) req->user_ctx;
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);

    // End response
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t any = {
    .uri       = "/any",
    .method    = HTTP_ANY,
    .handler   = any_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = "Hello World!"
};

/* This handler allows the custom error handling functionality to be
 * tested from client side. For that, when a PUT request 0 is sent to
 * URI /ctrl, the /hello and /echo URIs are unregistered and following
 * custom error handler http_404_error_handler() is registered.
 * Afterwards, when /hello or /echo is requested, this custom error
 * handler is invoked which, after sending an error message to client,
 * either closes the underlying socket (when requested URI is /echo)
 * or keeps it open (when requested URI is /hello). This allows the
 * client to infer if the custom error handler is functioning as expected
 * by observing the socket state.
 */
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    if (strcmp("/hello", req->uri) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/hello URI is not available");
        /* Return ESP_OK to keep underlying socket open */
        return ESP_OK;
    } else if (strcmp("/echo", req->uri) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/echo URI is not available");
        /* Return ESP_FAIL to close underlying socket */
        return ESP_FAIL;
    }
    /* For any other URI send 404 and close socket */
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Some 404 error message");
    return ESP_FAIL;
}

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
#if CONFIG_IDF_TARGET_LINUX
    // Setting port as 8001 when building for Linux. Port 80 can be used only by a privileged user in linux.
    // So when a unprivileged user tries to run the application, it throws bind error and the server is not started.
    // Port 8001 can be used by an unprivileged user as well. So the application will not throw bind error and the
    // server will be started.
    config.server_port = 8001;
#endif // !CONFIG_IDF_TARGET_LINUX
    config.lru_purge_enable = true;

    csv_data_mutex = xSemaphoreCreateMutex();
    if (csv_data_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex for CSV data");
        return NULL;
    }

    csv_data = (char*)heap_caps_malloc(CSV_DATA_META + CSV_DATA_LINES * CSV_DATA_LINE_LENGTH + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!csv_data) {
        ESP_LOGE(TAG, "Allocation CSV data failed");
        return NULL;
    }
    csv_data[0] = '\0'; // Initialize the string to be empty
    strcpy(csv_data, "Boot Time(ms),ToF(mm),US(mm),IR(mm),Cm(id),Cm(score)\n");
    csv_data_index = CSV_DATA_META;

    /* This is a workaround to avoid heap fragmentation in case of PSRAM enabled devices when the server is started before Wi-Fi connection is established. */
    if (heap_caps_check_integrity_all(true) == false) {
        ESP_LOGE(TAG, "Heap is fragmented, integrity check failed");
        heap_caps_free(csv_data);
        csv_data = NULL;
        return NULL;
    }
    
    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &data);
        httpd_register_uri_handler(server, &any);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

#if !CONFIG_IDF_TARGET_LINUX
static esp_err_t stop_webserver(httpd_handle_t server)
{
    if (csv_data) {
        heap_caps_free(csv_data);
        csv_data = NULL;
    }

    if (csv_data_mutex) {
        vSemaphoreDelete(csv_data_mutex);
        csv_data_mutex = NULL;
    }

    // Stop the httpd server
    return httpd_stop(server);
}

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        if (stop_webserver(*server) == ESP_OK) {
            *server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
}

void http_server_init(void)
{
    static httpd_handle_t server = NULL;

    /* Register event handlers to stop the server when Wi-Fi Soft AP is stopped,
     * and re-start it upon Soft AP start.
     */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_START, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STOP, &disconnect_handler, &server));

    server = start_webserver();
}

void put_sensors_data_to_csv(sensor_data_t* sensors_data)
{
    xSemaphoreTake(csv_data_mutex, portMAX_DELAY);
    if ((csv_data_index + CSV_DATA_LINE_LENGTH) < (CSV_DATA_META + CSV_DATA_LINES * CSV_DATA_LINE_LENGTH)) {
        int len = snprintf(csv_data + csv_data_index, CSV_DATA_LINE_LENGTH + 1, "%7d,%4d,%4d,%4d,%1d,%.4f\n", 
            (int)esp_timer_get_time() / 1000, sensors_data->tof_mm, sensors_data->us_mm, sensors_data->ir_mm, sensors_data->camera_id, sensors_data->camera_score);
        if (len < 0) {
            ESP_LOGE(TAG, "Failed to write data to CSV buffer");
            return;
        }
        csv_data_index += len;
    } else {
        ESP_LOGW(TAG, "CSV buffer is full. Cannot write more data.");
    }
    xSemaphoreGive(csv_data_mutex);
}

#endif // !CONFIG_IDF_TARGET_LINUX
