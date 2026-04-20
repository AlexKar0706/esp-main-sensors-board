#ifndef ESP_MAIN_SENSORS_BOARD_HTTP_SERVER_H
#define ESP_MAIN_SENSORS_BOARD_HTTP_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int tof_mm;
    int us_mm;
    int ir_mm;
    int camera_id;
    float camera_score;
} sensor_data_t;

void http_server_init(void);
void put_sensors_data_to_csv(sensor_data_t* sensors_data);

#ifdef __cplusplus
}
#endif

#endif //ESP_MAIN_SENSORS_BOARD_HTTP_SERVER_H