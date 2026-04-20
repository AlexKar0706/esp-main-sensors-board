/* ESPNOW Driver

   This driver code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_crc.h"
#include "esp_timer.h"
#include "espnow_driver.h"

#define ESPNOW_MAXDELAY 512

static const char *TAG = "espnow_driver";

static QueueHandle_t s_espnow_event_queue = NULL;
#if defined(CONFIG_ESPNOW_SENDER)
static QueueHandle_t s_espnow_sender_queue = NULL;
#elif defined(CONFIG_ESPNOW_RECEIVER)
static QueueHandle_t s_espnow_receiver_queue = NULL;
#endif

static uint8_t s_broadcast_mac[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static void espnow_driver_stop(TaskHandle_t *driver_task_handle);

/* ESPNOW sending or receiving callback function is called in WiFi task.
 * Users should not do lengthy operations from this task. Instead, post
 * necessary data to a queue and handle it from a lower priority task. */
static void espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    espnow_event_t evt;
    espnow_event_send_cb_t *send_cb = &evt.info.send_cb;

    if (tx_info == NULL) {
        ESP_LOGE(TAG, "Send cb arg error");
        return;
    }

    evt.id = ESPNOW_SEND_CB;
    memcpy(send_cb->mac_addr, tx_info->des_addr, ESP_NOW_ETH_ALEN);
    send_cb->status = status;
    if (xQueueSend(s_espnow_event_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send send queue fail");
    }
}

static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    espnow_event_t evt;
    espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;
    uint8_t * src_addr = recv_info->src_addr;
    uint8_t * dest_addr = recv_info->des_addr;

    if (src_addr == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }

    if (IS_BROADCAST_ADDR(dest_addr)) {
        /* If added a peer with encryption before, the receive packets may be
         * encrypted as peer-to-peer message or unencrypted over the broadcast channel.
         * Users can check the destination address to distinguish it.
         */
        ESP_LOGD(TAG, "Receive broadcast ESPNOW data");
    } else {
        ESP_LOGD(TAG, "Receive unicast ESPNOW data");
    }

    evt.id = ESPNOW_RECV_CB;
    memcpy(recv_cb->src_mac, src_addr, ESP_NOW_ETH_ALEN);
    memcpy(recv_cb->dest_mac, dest_addr, ESP_NOW_ETH_ALEN);
    recv_cb->data = malloc(len);
    if (recv_cb->data == NULL) {
        ESP_LOGE(TAG, "Malloc receive data fail");
        return;
    }
    memcpy(recv_cb->data, data, len);
    recv_cb->data_len = len;
    if (xQueueSend(s_espnow_event_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send receive queue fail");
        free(recv_cb->data);
    }
}

/* Parse received ESPNOW data. */
esp_err_t espnow_driver_data_parse(uint8_t *data, uint16_t data_len)
{
    espnow_data_t *buf = (espnow_data_t *)data;
    uint16_t crc, crc_cal = 0;

    if (data_len < sizeof(espnow_data_t)) {
        ESP_LOGE(TAG, "Receive ESPNOW data too short, len:%d", data_len);
        return -1;
    }

    crc = buf->crc;
    buf->crc = 0;
    crc_cal = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, data_len);

    if (crc_cal == crc) {
        return ESP_OK;
    }

    return ESP_FAIL;
}

/* Prepare ESPNOW data to be sent. */
void espnow_driver_data_prepare(espnow_data_t *data)
{
    data->crc = 0;
    data->crc = esp_crc16_le(UINT16_MAX, (uint8_t const *)data, sizeof(espnow_data_t));
}

#if defined(CONFIG_ESPNOW_SENDER)
static void espnow_driver_task(void *pvParameter)
{
    espnow_event_t evt;
    espnow_sender_queue_param_t sender_queue_param;
    esp_err_t ret;

    TaskHandle_t *driver_task_handle = (TaskHandle_t *)pvParameter;
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Sender task started");

    while (1) {

        vTaskDelay(1);

        /* Process requested events */
        if (xQueueReceive(s_espnow_event_queue, &evt, 0) == pdTRUE) {
            switch (evt.id) {
                case ESPNOW_SEND_CB:
                {
                    espnow_event_send_cb_t *send_cb = &evt.info.send_cb;

                    ESP_LOGD(TAG, "Send data to "MACSTR", status1: %d", MAC2STR(send_cb->mac_addr), send_cb->status);

                    /* Check, if it is a broadcast message. */
                    if (IS_BROADCAST_ADDR(send_cb->mac_addr)) {
                        break;
                    }

                    /* Check, if transmission failed */
#if (0)
                    /* Ignore scenario with failed ACK, keep message delays anyway */
                    if (send_cb->status != ESP_NOW_SEND_SUCCESS) {
                        ESP_LOGI(TAG, "Failed to send data to "MACSTR"", MAC2STR(send_cb->mac_addr));
                        if (xQueuePeek(s_espnow_sender_queue, &sender_queue_param, 0) == pdTRUE) {
                            espnow_data_t* data = &sender_queue_param.data;
                            for(int i = 0; i < sender_queue_param.num_receivers; i++) {
                                espnow_receiver_param_t* receiver = &sender_queue_param.receivers[i];
                                if (receiver->done == false) {  
                                    /* Find failed receiver mac address in list */
                                    if (memcmp(receiver->dest_mac, send_cb->mac_addr, sizeof(receiver->dest_mac)) == 0) {
                                        if (receiver->retries == 0) {
                                            receiver->done = true;
                                            ESP_LOGI(TAG, "Receiver didn't respond: "MACSTR". Removing from peer list", MAC2STR(receiver->dest_mac));
                                            ESP_ERROR_CHECK( esp_now_del_peer(receiver->dest_mac) );
                                            continue;
                                        }

                                        if (esp_now_send(receiver->dest_mac, (uint8_t *)data, sizeof(espnow_data_t)) != ESP_OK) {
                                            ESP_LOGE(TAG, "Send error");
                                            espnow_driver_stop(driver_task_handle);
                                        }
                                        else {
                                            receiver->timestamp = esp_timer_get_time()/1000;
                                            receiver->retries--;
                                        }

                                        /* Correct mac address was found, exit loop */
                                        break;
                                    }
                                }
                            }
                        }
                    }
#endif
                    break;
                }
                case ESPNOW_RECV_CB:
                {
                    espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;

                    ret = espnow_driver_data_parse(recv_cb->data, recv_cb->data_len);
                    free(recv_cb->data);
                    if (ret == ESP_OK) {
                        if (IS_BROADCAST_ADDR(recv_cb->dest_mac)) {
                            ESP_LOGI(TAG, "Receive broadcast data from: "MACSTR", len: %d", MAC2STR(recv_cb->src_mac), recv_cb->data_len);

                            if (esp_now_is_peer_exist(recv_cb->src_mac) == false) {
                                /* If MAC address does not exist in peer list, add it to peer list. */
                                esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
                                if (peer == NULL) {
                                    ESP_LOGE(TAG, "Malloc peer information fail");
                                    espnow_driver_stop(driver_task_handle);
                                }
                                memset(peer, 0, sizeof(esp_now_peer_info_t));
                                peer->channel = CONFIG_ESPNOW_CHANNEL;
                                peer->ifidx = ESPNOW_WIFI_IF;
                                peer->encrypt = false;
                                memcpy(peer->lmk, CONFIG_ESPNOW_LMK, ESP_NOW_KEY_LEN);
                                memcpy(peer->peer_addr, recv_cb->src_mac, ESP_NOW_ETH_ALEN);
                                ESP_ERROR_CHECK( esp_now_add_peer(peer) );
                                free(peer);

                                ESP_LOGI(TAG, "New peer added: "MACSTR"", MAC2STR(recv_cb->src_mac));
                            } else {
                                /* If MAC address exists in peer list, but peer is sending broadcast data, most probably peer was reseted. */
                                ESP_LOGI(TAG, "Existing peer send broadcast data: "MACSTR"", MAC2STR(recv_cb->src_mac));

                                esp_now_peer_info_t* peer = malloc(sizeof(esp_now_peer_info_t));
                                if (peer == NULL) {
                                    ESP_LOGE(TAG, "Malloc peer information fail");
                                    espnow_driver_stop(driver_task_handle);
                                }
                                memset(peer, 0, sizeof(esp_now_peer_info_t));
                                esp_now_get_peer(recv_cb->src_mac, peer);

                                if (peer->encrypt) {
                                    ESP_LOGI(TAG, "Switching peer "MACSTR" to non-encrypted mode", MAC2STR(recv_cb->src_mac));
                                    peer->encrypt = false;
                                    ESP_ERROR_CHECK( esp_now_mod_peer(peer) );
                                }
                                free(peer);
                            }

                            /* Make random payload */
                            espnow_data_t data = { 0 };
                            esp_fill_random(data.payload, sizeof(data.payload));
                            espnow_driver_data_prepare(&data);

                            ESP_LOGI(TAG, "Send unicast verification data to: "MACSTR"", MAC2STR(recv_cb->src_mac));

                            /* When sender receives peer, it indicates, that receiver has been saved */
                            if (esp_now_send(recv_cb->src_mac, (uint8_t *)&data, sizeof(espnow_data_t)) != ESP_OK) {
                                ESP_LOGE(TAG, "Send error");
                                espnow_driver_stop(driver_task_handle);
                            }
                        } else {
                            /* If unicast data was sent, then just indicate to a receiver, that sender is active */

                            ESP_LOGI(TAG, "Receive unicast data from: "MACSTR", len: %d", MAC2STR(recv_cb->src_mac), recv_cb->data_len);

                            /* Ignore peers, that do not exists in a list */
                            if (esp_now_is_peer_exist(recv_cb->src_mac) == false) {
                                break;
                            }

                            /* If peer is not encrypted, switch it to encrypted mode. */
                            esp_now_peer_info_t* peer = malloc(sizeof(esp_now_peer_info_t));
                            if (peer == NULL) {
                                ESP_LOGE(TAG, "Malloc peer information fail");
                                espnow_driver_stop(driver_task_handle);
                            }
                            memset(peer, 0, sizeof(esp_now_peer_info_t));
                            ESP_ERROR_CHECK( esp_now_get_peer(recv_cb->src_mac, peer) );

                            if (!peer->encrypt) {
                                ESP_LOGI(TAG, "Switching peer "MACSTR" to encrypted mode", MAC2STR(recv_cb->src_mac));
                                peer->encrypt = true;
                                memcpy(peer->lmk, CONFIG_ESPNOW_LMK, ESP_NOW_KEY_LEN);
                                ESP_ERROR_CHECK( esp_now_mod_peer(peer) );
                                free(peer);
                                ESP_LOGI(TAG, "Handshake with peer "MACSTR" completed", MAC2STR(recv_cb->src_mac));
                                break;
                            }
                            free(peer);

                            /* Find receiver in list and mark as done */
                            if (xQueuePeek(s_espnow_sender_queue, &sender_queue_param, 0) == pdTRUE) {
                                espnow_receiver_param_t* receiver = NULL;
                                for(int i = 0; i < sender_queue_param.num_receivers; i++) {
                                    if (memcmp(sender_queue_param.receivers[i].dest_mac, recv_cb->src_mac, ESP_NOW_ETH_ALEN) == 0) {
                                        receiver = &sender_queue_param.receivers[i];
                                        break;
                                    }
                                }

                                if (receiver != NULL) {
                                    /* Mark receiver as done, and do not send anything back */
                                    receiver->done = true;
                                    ESP_LOGI(TAG, "Receiver responded: "MACSTR"", MAC2STR(receiver->dest_mac));
                                    break;
                                }
                            }

                            /* Make random payload */
                            espnow_data_t data = { 0 };
                            esp_fill_random(data.payload, sizeof(data.payload));
                            espnow_driver_data_prepare(&data);

                            ESP_LOGI(TAG, "Send unicast acknowledgment data to: "MACSTR"", MAC2STR(recv_cb->src_mac));

                            /* Send keep-alive data with dummy information */
                            if (esp_now_send(recv_cb->src_mac, (uint8_t *)&data, sizeof(espnow_data_t)) != ESP_OK) {
                                ESP_LOGE(TAG, "Send error");
                                espnow_driver_stop(driver_task_handle);
                            }
                        }
                    }
                    break;
                }
                default:
                    ESP_LOGE(TAG, "Callback type error: %d", evt.id);
                    break;
            }
        }

        /* Process requested data to be transmitted */
        if (xQueuePeek(s_espnow_sender_queue, &sender_queue_param, 0) == pdTRUE) {
            espnow_data_t* data = &sender_queue_param.data;
            bool all_receivers_done = true;
            for(int i = 0; i < sender_queue_param.num_receivers; i++) {
                espnow_receiver_param_t* receiver = &sender_queue_param.receivers[i];
                if (receiver->done == false) {

                    /* Check for the receiver, that didn't respond */
                    if (receiver->retries == 0) {
                        receiver->done = true;
                        ESP_LOGI(TAG, "Receiver didn't respond: "MACSTR". Removing from peer list", MAC2STR(receiver->dest_mac));
                        ESP_ERROR_CHECK( esp_now_del_peer(receiver->dest_mac) );
                        continue;
                    }

                    all_receivers_done = false;
                    if ((receiver->timestamp + CONFIG_ESPNOW_SENDER_MSG_DELAY_MS) < esp_timer_get_time()/1000) {
                        ESP_LOGI(TAG, "Send model results to "MACSTR"", MAC2STR(receiver->dest_mac));
                        if (esp_now_send(receiver->dest_mac, (uint8_t *)data, sizeof(espnow_data_t)) != ESP_OK) {
                            ESP_LOGE(TAG, "Send error");
                            espnow_driver_stop(driver_task_handle);
                        }
                        else {
                            receiver->timestamp = esp_timer_get_time()/1000;
                            receiver->retries--;
                        }
                    }
                }
            }

            if (all_receivers_done) {
                free(sender_queue_param.receivers);
                xQueueReceive(s_espnow_sender_queue, &sender_queue_param, portMAX_DELAY);
            }
        }
    }
}
#endif  /* CONFIG_ESPNOW_SENDER */

#if defined(CONFIG_ESPNOW_RECEIVER)
static void espnow_driver_task(void *pvParameter)
{
    espnow_event_t evt;
    espnow_payload_t payload;
    esp_err_t ret;
    int64_t receiver_timestamp = esp_timer_get_time()/1000;
    uint8_t receiver_retries = 0;
    uint8_t sender_mac[ESP_NOW_ETH_ALEN];
    bool is_sender_found = false;
    bool is_sender_keep_alive = false;
    bool handshake_message = false;

    TaskHandle_t *driver_task_handle = (TaskHandle_t *)pvParameter;
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Receiver task started");

    while (1) {

        vTaskDelay(1);

        /* Process requested events */
        if (xQueueReceive(s_espnow_event_queue, &evt, 0) == pdTRUE) {
            switch (evt.id) {
                case ESPNOW_SEND_CB:
                {
                    espnow_event_send_cb_t *send_cb = &evt.info.send_cb;

                    ESP_LOGD(TAG, "Send data to "MACSTR", status1: %d", MAC2STR(send_cb->mac_addr), send_cb->status);

                    if (memcmp(send_cb->mac_addr, sender_mac, ESP_NOW_ETH_ALEN) == 0) {
                        if (handshake_message) {
                            if (send_cb->status == ESP_NOW_SEND_SUCCESS) {
                                ESP_LOGI(TAG, "Send data to "MACSTR" successfully", MAC2STR(send_cb->mac_addr));
                                ESP_LOGI(TAG, "Switching peer "MACSTR" to encrypted mode", MAC2STR(send_cb->mac_addr));
                                esp_now_peer_info_t* peer = malloc(sizeof(esp_now_peer_info_t));
                                if (peer == NULL) {
                                    ESP_LOGE(TAG, "Malloc peer information fail");
                                    espnow_driver_stop(driver_task_handle);
                                }
                                memset(peer, 0, sizeof(esp_now_peer_info_t));
                                ESP_ERROR_CHECK( esp_now_get_peer(send_cb->mac_addr, peer) );
                                peer->encrypt = true;
                                memcpy(peer->lmk, CONFIG_ESPNOW_LMK, ESP_NOW_KEY_LEN);
                                ESP_ERROR_CHECK( esp_now_mod_peer(peer) );
                                free(peer);
                            } else {
                                ESP_LOGW(TAG, "Send data to "MACSTR" fail", MAC2STR(send_cb->mac_addr));
                                ESP_ERROR_CHECK( esp_now_del_peer(send_cb->mac_addr) );
                                is_sender_found = false;
                            }
                            ESP_LOGI(TAG, "Handshake with peer "MACSTR" completed", MAC2STR(send_cb->mac_addr));
                        }
                        handshake_message = false;
                    }

                    break;
                }
                case ESPNOW_RECV_CB:
                {
                    espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;

                    ret = espnow_driver_data_parse(recv_cb->data, recv_cb->data_len);
                    memcpy((uint8_t *)&payload, ((espnow_data_t*)recv_cb->data)->payload, sizeof(payload));
                    free(recv_cb->data);
                    if (ret == ESP_OK) {
                        if (!IS_BROADCAST_ADDR(recv_cb->dest_mac)) {
                            ESP_LOGI(TAG, "Receive unicast data from: "MACSTR", len: %d", MAC2STR(recv_cb->src_mac), recv_cb->data_len);

                            receiver_timestamp = esp_timer_get_time()/1000;
                            receiver_retries = CONFIG_ESPNOW_RECEIVER_TRIES;
                            is_sender_found = true;

                            /* If MAC address does not exist in peer list, add it to peer list. */
                            if (esp_now_is_peer_exist(recv_cb->src_mac) == false) {
                                esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
                                if (peer == NULL) {
                                    ESP_LOGE(TAG, "Malloc peer information fail");
                                    espnow_driver_stop(driver_task_handle);
                                }
                                memset(peer, 0, sizeof(esp_now_peer_info_t));
                                peer->channel = CONFIG_ESPNOW_CHANNEL;
                                peer->ifidx = ESPNOW_WIFI_IF;
                                peer->encrypt = false;
                                memcpy(peer->lmk, CONFIG_ESPNOW_LMK, ESP_NOW_KEY_LEN);
                                memcpy(peer->peer_addr, recv_cb->src_mac, ESP_NOW_ETH_ALEN);
                                memcpy(sender_mac, recv_cb->src_mac, ESP_NOW_ETH_ALEN);
                                ESP_ERROR_CHECK( esp_now_add_peer(peer) );
                                free(peer);
                                is_sender_keep_alive = false;
                                handshake_message = true;

                                ESP_LOGI(TAG, "New peer added: "MACSTR"", MAC2STR(recv_cb->src_mac));
                            }

                            /* If receiver sends keep-alive data, ignore first message */
                            if (is_sender_keep_alive) {
                                is_sender_keep_alive = false;
                                break;
                            }

                            /* Save payload data in a queue, if it's not a handshake message */
                            if (!handshake_message) {
                                if (xQueueSend(s_espnow_receiver_queue, &payload, ESPNOW_MAXDELAY) != pdTRUE) {
                                    ESP_LOGW(TAG, "Send receiver queue fail");
                                    break;
                                }
                            }

                            espnow_data_t data = { 0 };
                            esp_fill_random(data.payload, sizeof(data.payload));
                            espnow_driver_data_prepare(&data);

                            ESP_LOGI(TAG, "Write data accepted to: "MACSTR"", MAC2STR(recv_cb->src_mac));

                            /* When receiver gets the data, it indicates, that data has been saved */
                            if (esp_now_send(recv_cb->src_mac, (uint8_t *)&data, sizeof(espnow_data_t)) != ESP_OK) {
                                ESP_LOGE(TAG, "Send error");
                                espnow_driver_stop(driver_task_handle);
                            }
                        }
                    }
                    break;
                }
                default:
                    ESP_LOGE(TAG, "Callback type error: %d", evt.id);
                    break;
            }
        }

        /* Check if receiver has responded within the delay, and send ack data, if needed */
        if ((receiver_timestamp + CONFIG_ESPNOW_RECEIVER_MSG_DELAY_MS) < esp_timer_get_time()/1000) {

            if (is_sender_found && receiver_retries == 0) {
                is_sender_found = false;
                ESP_LOGW(TAG, "Receiver didn't respond: "MACSTR". Removing from peer list", MAC2STR(sender_mac));
                ESP_ERROR_CHECK( esp_now_del_peer(sender_mac) );
                continue;
            }

            espnow_data_t data = { 0 };
            esp_fill_random(data.payload, sizeof(data.payload));
            espnow_driver_data_prepare(&data);

            uint8_t dest_mac[ESP_NOW_ETH_ALEN];
            if (is_sender_found) {
                memcpy(dest_mac, sender_mac, ESP_NOW_ETH_ALEN);
                receiver_retries--;
                is_sender_keep_alive = true;
            }
            else {
                memcpy(dest_mac, s_broadcast_mac, ESP_NOW_ETH_ALEN);
            }

            receiver_timestamp = esp_timer_get_time()/1000;

            ESP_LOGI(TAG, "Write keep-alive data to: "MACSTR"", MAC2STR(dest_mac));

            /* When receiver gets the data, it indicates, that data has been saved */
            if (esp_now_send(dest_mac, (uint8_t *)&data, sizeof(espnow_data_t)) != ESP_OK) {
                ESP_LOGE(TAG, "Send error");
                espnow_driver_stop(driver_task_handle);
            }
        }
    }
}
#endif  /* CONFIG_ESPNOW_RECEIVER */

static esp_err_t espnow_driver_start(TaskHandle_t *driver_task_handle)
{
    if (*driver_task_handle != NULL) {
        ESP_LOGE(TAG, "Task handle is not NULL");
        return ESP_FAIL;
    }

    s_espnow_event_queue = xQueueCreate(ESPNOW_EVENT_QUEUE_SIZE, sizeof(espnow_event_t));
    if (s_espnow_event_queue == NULL) {
        ESP_LOGE(TAG, "Create event queue fail");
        return ESP_FAIL;
    }

#if defined(CONFIG_ESPNOW_SENDER)
    s_espnow_sender_queue = xQueueCreate(ESPNOW_SENDER_QUEUE_SIZE, sizeof(espnow_sender_queue_param_t));
    if (s_espnow_sender_queue == NULL) {
        ESP_LOGE(TAG, "Create sender queue fail");
        return ESP_FAIL;
    }
#endif  /* CONFIG_ESPNOW_SENDER */

#if defined(CONFIG_ESPNOW_RECEIVER)
    s_espnow_receiver_queue = xQueueCreate(ESPNOW_RECEIVER_QUEUE_SIZE, sizeof(espnow_payload_t));
    if (s_espnow_receiver_queue == NULL) {
        ESP_LOGE(TAG, "Create receiver queue fail");
        return ESP_FAIL;
    }
#endif  /* CONFIG_ESPNOW_RECEIVER */

    /* Initialize ESPNOW and register sending and receiving callback function. */
    ESP_ERROR_CHECK( esp_now_init() );
    ESP_ERROR_CHECK( esp_now_register_send_cb(espnow_send_cb) );
    ESP_ERROR_CHECK( esp_now_register_recv_cb(espnow_recv_cb) );
#if CONFIG_ESPNOW_ENABLE_POWER_SAVE
    ESP_ERROR_CHECK( esp_now_set_wake_window(CONFIG_ESPNOW_WAKE_WINDOW) );
    ESP_ERROR_CHECK( esp_wifi_connectionless_module_set_wake_interval(CONFIG_ESPNOW_WAKE_INTERVAL) );
#endif
    /* Set primary master key. */
    ESP_ERROR_CHECK( esp_now_set_pmk((uint8_t *)CONFIG_ESPNOW_PMK) );

    /* Add broadcast peer information to peer list. */
#if defined(CONFIG_ESPNOW_RECEIVER)
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL) {
        ESP_LOGE(TAG, "Malloc peer information fail");
        vQueueDelete(s_espnow_event_queue);
        s_espnow_event_queue = NULL;
        esp_now_deinit();
        return ESP_FAIL;
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = CONFIG_ESPNOW_CHANNEL;
    peer->ifidx = ESPNOW_WIFI_IF;
    peer->encrypt = false;
    memcpy(peer->peer_addr, s_broadcast_mac, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK( esp_now_add_peer(peer) );
    free(peer);
#endif  /* CONFIG_ESPNOW_RECEIVER */

#if defined(CONFIG_ESPNOW_SENDER) || defined(CONFIG_ESPNOW_RECEIVER)
     /* Create a task to handle sending and receiving ESPNOW data. */
    xTaskCreate(espnow_driver_task, "espnow_driver_task", 4096, driver_task_handle, 4, driver_task_handle);
#endif  /* CONFIG_ESPNOW_SENDER || CONFIG_ESPNOW_RECEIVER */

    return ESP_OK;
}

static void espnow_driver_stop(TaskHandle_t *driver_task_handle)
{
    if (*driver_task_handle != NULL) {
        vQueueDelete(s_espnow_event_queue);
        s_espnow_event_queue = NULL;
#if defined(CONFIG_ESPNOW_SENDER)
        vQueueDelete(s_espnow_sender_queue);
        s_espnow_sender_queue = NULL;
#endif  /* CONFIG_ESPNOW_SENDER */
#if defined(CONFIG_ESPNOW_RECEIVER)
        vQueueDelete(s_espnow_receiver_queue);
        s_espnow_receiver_queue = NULL;
#endif  /* CONFIG_ESPNOW_RECEIVER */
        esp_now_deinit();
        vTaskDelete(*driver_task_handle);
        *driver_task_handle = NULL;
    }
}

static void espnow_driver_start_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    TaskHandle_t *driver_task_handle = (TaskHandle_t *)event_data;
    espnow_driver_start(driver_task_handle);
}

static void espnow_driver_stop_handler(void* arg, esp_event_base_t event_base,
                                  int32_t event_id, void* event_data)
{
    TaskHandle_t *driver_task_handle = (TaskHandle_t *)event_data;
    espnow_driver_stop(driver_task_handle);
}

void espnow_driver_init(void)
{
    static TaskHandle_t driver_task_handle = NULL;

    /* Register event handlers to stop the server when Wi-Fi Soft AP is stopped,
     * and re-start it upon Soft AP start.
     */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_START, &espnow_driver_start_handler, &driver_task_handle));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STOP, &espnow_driver_stop_handler, &driver_task_handle));

    espnow_driver_start(&driver_task_handle);
}

#if defined(CONFIG_ESPNOW_SENDER)
esp_err_t espnow_driver_send(espnow_payload_t* payload)
{
    esp_now_peer_num_t peer_num;
    ESP_ERROR_CHECK(esp_now_get_peer_num(&peer_num));

    if (peer_num.total_num > 0) {
        espnow_sender_queue_param_t sender_queue_param;
        esp_now_peer_info_t peer;
        bool from_head = true;

        memcpy(sender_queue_param.data.payload, payload, sizeof(espnow_payload_t));
        espnow_driver_data_prepare(&sender_queue_param.data);
        sender_queue_param.num_receivers = 0;
        sender_queue_param.receivers = malloc(peer_num.total_num * sizeof(espnow_receiver_param_t));
        if (sender_queue_param.receivers == NULL) {
            ESP_LOGE(TAG, "Malloc sender queue fail");
            return ESP_FAIL;
        }

        while (esp_now_fetch_peer(from_head, &peer) == ESP_OK) {
            from_head = false;

            int current_receiver_index = sender_queue_param.num_receivers;
            espnow_receiver_param_t* receiver = &sender_queue_param.receivers[current_receiver_index];

            memcpy(receiver->dest_mac, peer.peer_addr, ESP_NOW_ETH_ALEN);
            receiver->retries = CONFIG_ESPNOW_SENDER_TRIES;
            receiver->done = false;
            receiver->timestamp = 0;

            sender_queue_param.num_receivers++;
        }
        
        if (xQueueSend(s_espnow_sender_queue, &sender_queue_param, ESPNOW_MAXDELAY) != pdTRUE) {
            ESP_LOGW(TAG, "Send sender queue fail");
            free(sender_queue_param.receivers);
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}
#endif  /* CONFIG_ESPNOW_SENDER */

#if defined(CONFIG_ESPNOW_RECEIVER)
esp_err_t espnow_driver_receive(espnow_payload_t* payload)
{
    if (xQueueReceive(s_espnow_receiver_queue, payload, 0) == pdTRUE) {
        return ESP_OK;
    }

    return ESP_ERR_NOT_FINISHED;
}
#endif  /* CONFIG_ESPNOW_RECEIVER */
