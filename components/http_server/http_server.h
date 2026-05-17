#ifndef ESP_MAIN_SENSORS_BOARD_HTTP_SERVER_H
#define ESP_MAIN_SENSORS_BOARD_HTTP_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#define SENSORS_DATA_TYPE_MM 0
#define SENSORS_DATA_TYPE_ID 1

#define SENSORS_DATA_TYPE (SENSORS_DATA_TYPE_ID) /* Change this to SENSORS_DATA_TYPE_ID to send sensor classification IDs instead of measurements */

typedef struct {
    int tof_mm;
    int us_mm;
    int ir_mm;

#if (SENSORS_DATA_TYPE == SENSORS_DATA_TYPE_ID)
    int tof_id;
    int us_id;
    int ir_id;
#endif

    int camera_id;
    float camera_score;
} sensor_data_t;

void http_server_init(void);
void put_sensors_data_to_csv(sensor_data_t* sensors_data);

#ifdef __cplusplus
}
#endif

#endif //ESP_MAIN_SENSORS_BOARD_HTTP_SERVER_H