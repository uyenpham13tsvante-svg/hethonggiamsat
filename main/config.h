#ifndef CONFIG_H
#define CONFIG_H

#include "driver/gpio.h"
#include "hal/adc_types.h"
#include "hal/ledc_types.h"

#define PIN_DS18B20         GPIO_NUM_32
#define PIN_MQ2_AO          GPIO_NUM_35
#define MQ2_ADC_UNIT        ADC_UNIT_1
#define MQ2_ADC_CHANNEL     ADC_CHANNEL_7
#define MQ2_ADC_ATTEN       ADC_ATTEN_DB_12

#define PIN_I2C_SDA         GPIO_NUM_21
#define PIN_I2C_SCL         GPIO_NUM_22
#define LCD_ADDR            0x27
#define LCD_COLS            20
#define LCD_ROWS            4

#define PIN_BTN_MENU        GPIO_NUM_13
#define PIN_BTN_UP          GPIO_NUM_16
#define PIN_BTN_DOWN        GPIO_NUM_17
#define PIN_BTN_OK          GPIO_NUM_4

#define PIN_FAN_PWM         GPIO_NUM_25
#define PIN_BUZZER          GPIO_NUM_19
#define PIN_LED_ALARM       GPIO_NUM_18

#define FAN_PWM_FREQ        5000
#define FAN_PWM_RES         LEDC_TIMER_10_BIT
#define FAN_MIN_PCT         40

#define DEFAULT_TEMP_FAN_ON     30.0f
#define DEFAULT_TEMP_FAN_MAX    40.0f
#define DEFAULT_TEMP_ALARM      45.0f
#define DEFAULT_TEMP_HYST       1.0f
#define DEFAULT_SMOKE_ALARM     1500
#define DEFAULT_SMOKE_HYST      100

#endif
