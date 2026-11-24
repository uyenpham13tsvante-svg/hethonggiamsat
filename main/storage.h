#ifndef STORAGE_H
#define STORAGE_H

#include "esp_err.h"

typedef struct {
    int temp_fan_on;    // Nhiet do bat quat (C), mac dinh 30
    int temp_fan_max;   // Nhiet do quat 100% (C), mac dinh 40
    int temp_alarm;     // Nhiet do bao dong (C), mac dinh 45
    int smoke_alarm;    // Dien ap bao khoi MQ-2 (mV), mac dinh 1500
} sys_config_t;

void      storage_init(void);
void      storage_set_defaults(sys_config_t *cfg);
void      storage_load(sys_config_t *cfg);
esp_err_t storage_save(const sys_config_t *cfg);

#endif
