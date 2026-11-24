#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "config.h"
#include "lcd_btn.h"
#include "actuators.h"
#include "sensors.h"

static const char *TAG = "MAIN";

#define MQ2_WARMUP_SEC   20     // Thoi gian lam nong MQ-2 (giay)

// Tinh % quat tuyen tinh tu 40%..100% theo nhiet do (co Hysteresis)
static int calc_auto_fan(float temp_c, bool *fan_on)
{
    if (temp_c >= DEFAULT_TEMP_FAN_ON) {
        *fan_on = true;
    } else if (temp_c <= (DEFAULT_TEMP_FAN_ON - DEFAULT_TEMP_HYST)) {
        *fan_on = false;
    }

    if (!(*fan_on)) return 0;
    if (temp_c >= DEFAULT_TEMP_FAN_MAX) return 100;

    float ratio = (temp_c - DEFAULT_TEMP_FAN_ON) / (DEFAULT_TEMP_FAN_MAX - DEFAULT_TEMP_FAN_ON);
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    return FAN_MIN_PCT + (int)(ratio * (100 - FAN_MIN_PCT));
}

void app_main(void)
{
    btn_init();
    actuators_init();
    sensors_init();
    bool lcd_ok = (lcd_init() == ESP_OK);

    // Kich do lan dau truoc khi vao vong lap
    ds18b20_start_convert();

    bool  test_mode    = false;
    int   test_fan     = 0;
    bool  test_alm     = false;

    bool  fan_on       = false;
    bool  alarm_temp   = false;
    bool  alarm_gas    = false;
    bool  buzzer_muted = false;
    bool  blink_state  = false;
    int   warmup_sec   = MQ2_WARMUP_SEC;

    float temp_c       = 0.0f;
    bool  temp_ok      = false;
    int   gas_mv       = 0;
    char  st_buf[32]   = "WARMUP";

    lcd_data_t ui = {
        .temp    = 0.0f,
        .temp_ok = false,
        .gas_mv  = 0,
        .fan_pct = 0,
        .mode    = "AUTO",
        .status  = "WARMUP",
        .wifi    = "--",
    };

    TickType_t last_sensor_tick = xTaskGetTickCount();
    TickType_t last_blink_tick  = xTaskGetTickCount();

    while (1) {
        TickType_t now = xTaskGetTickCount();
        bool need_lcd_update = false;

        // ==================== 1. XU LY 4 NUT NHAN ====================
        button_id_t btn = btn_read();
        if (btn == BTN_MENU) {
            test_mode = !test_mode;
            if (test_mode) {
                test_fan = 0;
                test_alm = false;
                fan_set_percent(0);
                buzzer_set(false);
                led_alarm_set(false);
            }
            need_lcd_update = true;
        } else if (test_mode) {
            // Che do TEST: tu chinh quat & thu coi/LED
            if (btn == BTN_UP && test_fan < 100) {
                test_fan += 20;
                fan_set_percent(test_fan);
                need_lcd_update = true;
            } else if (btn == BTN_DOWN && test_fan > 0) {
                test_fan -= 20;
                fan_set_percent(test_fan);
                need_lcd_update = true;
            } else if (btn == BTN_OK) {
                test_alm = !test_alm;
                buzzer_set(test_alm);
                led_alarm_set(test_alm);
                need_lcd_update = true;
            }
        } else {
            // Che do AUTO: OK de bo qua WARMUP hoac tat tieng coi tam thoi
            if (btn == BTN_OK) {
                if (warmup_sec > 0) {
                    warmup_sec = 0;
                    need_lcd_update = true;
                } else if (alarm_temp || alarm_gas) {
                    buzzer_muted = true;
                    buzzer_set(false);
                }
            }
        }

        // ==================== 2. DOC CAM BIEN & DIEU KHIEN (MOI 1 GIAY) ====================
        if (now - last_sensor_tick >= pdMS_TO_TICKS(1000)) {
            last_sensor_tick = now;

            temp_ok = ds18b20_read_temp(&temp_c);
            ds18b20_start_convert();        // Chuan bi cho giay tiep theo
            gas_mv = mq2_read_mv();

            if (warmup_sec > 0) warmup_sec--;

            // Kiem tra bao dong (co Hysteresis)
            if (temp_ok) {
                if (temp_c >= DEFAULT_TEMP_ALARM) alarm_temp = true;
                else if (temp_c <= DEFAULT_TEMP_ALARM - DEFAULT_TEMP_HYST) alarm_temp = false;
            } else {
                alarm_temp = false;
            }

            if (warmup_sec == 0) {
                if (gas_mv >= DEFAULT_SMOKE_ALARM) alarm_gas = true;
                else if (gas_mv <= DEFAULT_SMOKE_ALARM - DEFAULT_SMOKE_HYST) alarm_gas = false;
            } else {
                alarm_gas = false;
            }

            if (!alarm_temp && !alarm_gas) {
                buzzer_muted = false;
            }

            // Dieu khien quat khi o che do AUTO
            if (!test_mode) {
                if (!temp_ok) {
                    fan_set_percent(100);   // Che do an toan SAFE khi dut day DS18B20
                } else if (alarm_temp || alarm_gas) {
                    fan_set_percent(100);   // Chay toi da khi co bao dong
                } else {
                    fan_set_percent(calc_auto_fan(temp_c, &fan_on));
                }
            }

            ESP_LOGI(TAG, "T=%.1f(%s) | GAS=%dmV | FAN=%d%% | MODE=%s",
                     temp_c, temp_ok ? "OK" : "ERR", gas_mv, fan_get_percent(),
                     test_mode ? "TEST" : (temp_ok ? "AUTO" : "SAFE"));

            need_lcd_update = true;
        }

        // ==================== 3. NHAP NHAY COI & LED KHI BAO DONG (0.5 GIAY) ====================
        if (!test_mode && (now - last_blink_tick >= pdMS_TO_TICKS(500))) {
            last_blink_tick = now;
            if (alarm_temp || alarm_gas) {
                blink_state = !blink_state;
                led_alarm_set(blink_state);
                buzzer_set(!buzzer_muted && blink_state);
            } else {
                blink_state = false;
                led_alarm_set(false);
                buzzer_set(false);
            }
        }

        // ==================== 4. CAP NHAT MAN HINH LCD ====================
        if (need_lcd_update && lcd_ok) {
            ui.temp    = temp_c;
            ui.temp_ok = temp_ok;
            ui.gas_mv  = gas_mv;
            ui.fan_pct = fan_get_percent();

            if (test_mode) {
                ui.mode = "TEST";
                snprintf(st_buf, sizeof(st_buf), "ALM:%s UP/DN/OK", test_alm ? "ON " : "OFF");
            } else if (!temp_ok) {
                ui.mode = "SAFE";
                snprintf(st_buf, sizeof(st_buf), "ERR");
            } else {
                ui.mode = "AUTO";
                if (alarm_temp && alarm_gas) snprintf(st_buf, sizeof(st_buf), "ALM:TEMP+GAS");
                else if (alarm_temp)         snprintf(st_buf, sizeof(st_buf), "ALM:TEMP");
                else if (alarm_gas)          snprintf(st_buf, sizeof(st_buf), "ALM:GAS");
                else if (warmup_sec > 0)     snprintf(st_buf, sizeof(st_buf), "WARMUP (%ds)", warmup_sec);
                else                         snprintf(st_buf, sizeof(st_buf), "NORMAL");
            }

            ui.status = st_buf;
            lcd_show_main(&ui);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}