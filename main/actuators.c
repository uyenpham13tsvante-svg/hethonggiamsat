#include "actuators.h"
#include "config.h"
#include "driver/ledc.h"
#include "driver/gpio.h"

#define FAN_LEDC_MODE     LEDC_LOW_SPEED_MODE
#define FAN_LEDC_TIMER    LEDC_TIMER_0
#define FAN_LEDC_CHANNEL  LEDC_CHANNEL_0
#define FAN_MAX_DUTY      ((1 << FAN_PWM_RES) - 1)   // 10-bit = 1023

static int s_fan_pct = 0;

void actuators_init(void)
{
    // 1. Cau hinh PWM cho quat (GPIO 25 -> tro 1k -> TIP122)
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = FAN_LEDC_MODE,
        .duty_resolution = FAN_PWM_RES,
        .timer_num       = FAN_LEDC_TIMER,
        .freq_hz         = FAN_PWM_FREQ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch_cfg = {
        .gpio_num       = PIN_FAN_PWM,
        .speed_mode     = FAN_LEDC_MODE,
        .channel        = FAN_LEDC_CHANNEL,
        .intr_type      = LEDC_INTR_DISABLE,
        .timer_sel      = FAN_LEDC_TIMER,
        .duty           = 0,
        .hpoint         = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));

    // 2. Cau hinh GPIO cho coi (GPIO 19) va LED bao dong (GPIO 18)
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << PIN_BUZZER) | (1ULL << PIN_LED_ALARM),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_cfg));

    fan_set_percent(0);
    buzzer_set(false);
    led_alarm_set(false);
}

void fan_set_percent(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_fan_pct = pct;

    uint32_t duty = (uint32_t)((pct * FAN_MAX_DUTY) / 100);
    ledc_set_duty(FAN_LEDC_MODE, FAN_LEDC_CHANNEL, duty);
    ledc_update_duty(FAN_LEDC_MODE, FAN_LEDC_CHANNEL);
}

int fan_get_percent(void)
{
    return s_fan_pct;
}

void buzzer_set(bool on)
{
    gpio_set_level(PIN_BUZZER, on ? 1 : 0);
}

void led_alarm_set(bool on)
{
    gpio_set_level(PIN_LED_ALARM, on ? 1 : 0);
}