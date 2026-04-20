/* ESPNOW Driver

   This driver code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#ifndef ESPNOW_DRIVER_H
#define ESPNOW_DRIVER_H

#include "esp_now.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ESPNOW can work in both station and softap mode. Current driver expects softap mode */
#define ESPNOW_WIFI_MODE WIFI_MODE_AP
#define ESPNOW_WIFI_IF   ESP_IF_WIFI_AP

#define ESPNOW_EVENT_QUEUE_SIZE             6
#define ESPNOW_SENDER_QUEUE_SIZE            6
#define ESPNOW_RECEIVER_QUEUE_SIZE          6

#define IS_BROADCAST_ADDR(addr) (memcmp(addr, s_broadcast_mac, ESP_NOW_ETH_ALEN) == 0)

typedef enum {
    ESPNOW_SEND_CB,
    ESPNOW_RECV_CB,
} espnow_event_id_t;

typedef struct {
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    esp_now_send_status_t status;
} espnow_event_send_cb_t;

typedef struct {
    uint8_t src_mac[ESP_NOW_ETH_ALEN];
    uint8_t dest_mac[ESP_NOW_ETH_ALEN];
    uint8_t *data;
    int data_len;
} espnow_event_recv_cb_t;

typedef union {
    espnow_event_send_cb_t send_cb;
    espnow_event_recv_cb_t recv_cb;
} espnow_event_info_t;

/* When ESPNOW sending or receiving callback function is called, post event to ESPNOW task. */
typedef struct {
    espnow_event_id_t id;
    espnow_event_info_t info;
} espnow_event_t;

/* User define payload field. */
typedef struct {
    uint16_t x_start;                       //Model start X coordinate of the detected object.
    uint16_t y_start;                       //Model start Y coordinate of the detected object.
    uint16_t width;                         //Model width of the detected object.
    uint16_t height;                        //Model height of the detected object.
    uint16_t rect_size;                     //Model total rectangle size.
    uint8_t class_id;                       //Class ID of the detected object.
    float score;                            //Confidence score of the detected object.
} __attribute__((packed)) espnow_model_result_t;

typedef struct {
    uint64_t local_timestamp;                      //Local timestamp (since MCU is up) when model results are generated.
    espnow_model_result_t model_results[8];        //Detection model results (up to 8 objects).
} __attribute__((packed)) espnow_payload_t;

/* User defined field of ESPNOW data. */
typedef struct {
    uint16_t crc;                                   //CRC16 value of ESPNOW data.
    uint8_t payload[sizeof(espnow_payload_t)];      //Real payload of ESPNOW data.
} __attribute__((packed)) espnow_data_t;

/* Parameters of reciever for ESPNOW. */
typedef struct {
    uint8_t retries;                                //Number of retries for receiving ESPNOW data.
    uint8_t done;                                   //Indicate that if has received ESPNOW data from the receiver or not.
    int64_t timestamp;                              //Local timestamp when last message was transmitted to the receiver.
    uint8_t dest_mac[ESP_NOW_ETH_ALEN];             //MAC address of destination device.
} espnow_receiver_param_t;

/* Parameters of sending ESPNOW data. */
typedef struct {
    bool unicast;                                   //Send unicast ESPNOW data.
    bool broadcast;                                 //Send broadcast ESPNOW data.
    uint8_t state;                                  //Indicate that if has received broadcast ESPNOW data or not.
    uint32_t magic;                                 //Magic number which is used to determine which device to send unicast ESPNOW data.
    uint16_t delay;                                 //Delay between sending two ESPNOW data, unit: ms.
    int len;                                        //Length of ESPNOW data to be sent, unit: byte.
    uint8_t *buffer;                                //Buffer pointing to ESPNOW data.
    uint8_t dest_mac[ESP_NOW_ETH_ALEN];             //MAC address of destination device.
} espnow_send_param_t;

/* Parameters of sender queue for ESPNOW. */
typedef struct {
    espnow_data_t data;                             //ESPNOW data received.
    espnow_receiver_param_t* receivers;             //Pointer to array of receiver parameters.
    int num_receivers;                              //Number of receivers.
} espnow_sender_queue_param_t;


void espnow_driver_init(void);
esp_err_t espnow_driver_send(espnow_payload_t* payload);
esp_err_t espnow_driver_receive(espnow_payload_t* payload);

#ifdef __cplusplus
}
#endif

#endif // ESPNOW_DRIVER_H
