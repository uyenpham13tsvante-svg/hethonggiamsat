#ifndef ACTUATORS_H
#define ACTUATORS_H

#include <stdbool.h>

void actuators_init(void);
void fan_set_percent(int pct);      // 0..100%
int  fan_get_percent(void);
void buzzer_set(bool on);
void led_alarm_set(bool on);

#endif