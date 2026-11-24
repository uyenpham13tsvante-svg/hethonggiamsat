#ifndef SENSORS_H
#define SENSORS_H

#include <stdbool.h>

void sensors_init(void);
bool ds18b20_start_convert(void);       // Phat lenh do nhiet do (can ~750ms)
bool ds18b20_read_temp(float *temp_c);  // Doc ket qua + kiem tra CRC-8
int  mq2_read_mv(void);                 // Doc dien ap MQ-2 tai GPIO 35 (mV)

#endif