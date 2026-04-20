#ifndef ESP_HC_SR04_H
#define ESP_HC_SR04_H

#ifdef __cplusplus
extern "C" {
#endif

void hc_sr04_init(void);
uint32_t hc_sr04_read_measurement(void);

#ifdef __cplusplus
}
#endif

#endif //ESP_HC_SR04_H