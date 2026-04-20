#ifndef ESP_SENSORS_H
#define ESP_SENSORS_H

#ifdef __cplusplus
extern "C" {
#endif

void sensors_init(void);

uint32_t sensor_tof_get_current_distance(void);
uint32_t sensor_ir_get_current_distance(void);
uint32_t sensor_ultrasonic_get_current_distance(void);

#ifdef __cplusplus
}
#endif

#endif //ESP_SENSORS_H