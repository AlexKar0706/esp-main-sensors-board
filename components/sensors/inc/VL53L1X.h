#ifndef ESP_VL53L1X_H
#define ESP_VL53L1X_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vl53l1_api.h"

void vl53l1x_init(void);
uint32_t vl53l1x_read_measurement(void);

#ifdef __cplusplus
}
#endif

#endif //ESP_VL53L1X_H