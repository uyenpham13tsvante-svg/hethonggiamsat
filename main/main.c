#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "config.h"
#include "lcd_btn.h"
#include "actuators.h"
#include "sensors.h"
#include "storage.h"
#include "wifi_tg.h"

static const char *TAG = "MAIN";

#define MQ2_WARMUP_SEC      20
#define MENU_ITEM_COUNT     6
#define TG_REPEAT_SEC       60

typedef enum {
    SCR_MAIN = 0,
    SCR_MENU,
    SCR_EDIT,
    SCR_TEST
} app_screen_t;

static int calc_auto_fan(float temp_c, const sys_config_t *cfg, bool *fan_on)
{
    float t_on  = (float)cfg->temp_fan_on;
    float t_max = (float)cfg->temp_fan_max;

    if (temp_c >= t_on) {
        *fan_on = true;
    } else if (temp_c <= (t_on - DEFAULT_TEMP_HYST)) {
        *fan_on = false;
    }

    if (!(*fan_on)) return 0;
    if (temp_c >= t_max || t_max <= t_on) return 100;

    float ratio = (temp_c - t_on) / (t_max - t_on);
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    return FAN_MIN_PCT + (int)(ratio * (100 - FAN_MIN_PCT));
}

static void draw_menu(int cursor, const sys_config_t *cfg)
{
    char item_txt[32];
    char line[64];

    lcd_print_line(0, "== CAI DAT NGUONG ==");
    int top = (cursor >= 3) ? (cursor - 2) : 0;

    for (int r = 0; r < 3; r++) {
        int idx = top + r;
        switch (idx) {
            case 0: snprintf(item_txt, sizeof(item_txt), "Quat bat  : %2d C",   cfg->temp_fan_on);  break;
            case 1: snprintf(item_txt, sizeof(item_txt), "Quat max  : %2d C",   cfg->temp_fan_max); break;
            case 2: snprintf(item_txt, sizeof(item_txt), "Bao nhiet : %2d C",   cfg->temp_alarm);   break;
            case 3: snprintf(item_txt, sizeof(item_txt), "Bao khoi  :%4dmV",    cfg->smoke_alarm);  break;
            case 4: snprintf(item_txt, sizeof(item_txt), "Che do TEST");                            break;
            case 5: snprintf(item_txt, sizeof(item_txt), "Khoi phuc mac dinh");                     break;
            default: item_txt[0] = '\0'; break;
        }
        snprintf(line, sizeof(line), "%c%s", (idx == cursor) ? '>' : ' ', item_txt);
        lcd_print_line((uint8_t)(r + 1), line);
    }
}

static void draw_edit(int item, int val)
{
    char line[64];
    static const char *titles[4] = {
        "Nguong bat quat:",
        "Nguong quat 100%:",
        "Nguong bao nhiet:",
        "Nguong bao khoi:"
    };

    lcd_print_line(0, "== CHINH GIA TRI ===");
    lcd_print_line(1, (item >= 0 && item < 4) ? titles[item] : "");

    if (item == 3) {
        snprintf(line, sizeof(line), "   < %4d mV  >", val);
    } else {
        snprintf(line, sizeof(line), "   <   %2d C   >", val);
    }
    lcd_print_line(2, line);
    lcd_print_line(3, "OK:Luu   MENU:Huy");
}

static void get_edit_limits(int item, const sys_config_t *cfg, int *min_v, int *max_v, int *step)
{
    switch (item) {
        case 0: *min_v = 15;                    *max_v = cfg->temp_fan_max - 2; *step = 1;  break;
        case 1: *min_v = cfg->temp_fan_on + 2;  *max_v = cfg->temp_alarm - 1;   *step = 1;  break;
        case 2: *min_v = cfg->temp_fan_max + 1; *max_v = 80;                    *step = 1;  break;
        case 3: *min_v = 500;                   *max_v = 3000;                  *step = 50; break;
        default: *min_v = 0; *max_v = 100; *step = 1; break;
    }
}

void app_main(void)
{
    storage_init();
    sys_config_t cfg;
    storage_load(&cfg);

    wifi_tg_init();
    btn_init();
    actuators_init();
    sensors_init();
    bool lcd_ok = (lcd_init() == ESP_OK);

    ds18b20_start_convert();

    app_screen_t screen = SCR_MAIN;
    int   menu_cursor   = 0;
    int   edit_val      = 0;
    int   test_fan      = 0;
    bool  test_alm      = false;

    bool  fan_on         = false;
    bool  alarm_temp     = false;
    bool  alarm_gas      = false;
    bool  prev_alm_temp  = false;
    bool  prev_alm_gas   = false;
    bool  prev_temp_ok   = true;
    bool  buzzer_muted   = false;
    bool  blink_state    = false;
    int   warmup_sec     = MQ2_WARMUP_SEC;

    float temp_c         = 0.0f;
    bool  temp_ok        = false;
    int   gas_mv         = 0;
    char  st_buf[32]     = "WARMUP";
    char  tg_buf[220];

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
    TickType_t last_tg_tick     = 0;

    while (1) {
        TickType_t now = xTaskGetTickCount();
        bool need_lcd_update = false;

        button_id_t btn = btn_read();
        if (btn != BTN_NONE) {
            if (screen == SCR_MAIN) {
                if (btn == BTN_MENU) {
                    screen = SCR_MENU;
                    menu_cursor = 0;
                    need_lcd_update = true;
                } else if (btn == BTN_OK) {
                    if (warmup_sec > 0) {
                        warmup_sec = 0;
                        need_lcd_update = true;
                    } else if (alarm_temp || alarm_gas) {
                        buzzer_muted = true;
                        buzzer_set(false);
                    }
                }
            } else if (screen == SCR_MENU) {
                if (btn == BTN_UP) {
                    menu_cursor = (menu_cursor + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT;
                    need_lcd_update = true;
                } else if (btn == BTN_DOWN) {
                    menu_cursor = (menu_cursor + 1) % MENU_ITEM_COUNT;
                    need_lcd_update = true;
                } else if (btn == BTN_MENU) {
                    screen = SCR_MAIN;
                    need_lcd_update = true;
                } else if (btn == BTN_OK) {
                    if (menu_cursor < 4) {
                        if (menu_cursor == 0)      edit_val = cfg.temp_fan_on;
                        else if (menu_cursor == 1) edit_val = cfg.temp_fan_max;
                        else if (menu_cursor == 2) edit_val = cfg.temp_alarm;
                        else                       edit_val = cfg.smoke_alarm;
                        screen = SCR_EDIT;
                        need_lcd_update = true;
                    } else if (menu_cursor == 4) {
                        screen   = SCR_TEST;
                        test_fan = 0;
                        test_alm = false;
                        fan_set_percent(0);
                        buzzer_set(false);
                        led_alarm_set(false);
                        need_lcd_update = true;
                    } else if (menu_cursor == 5) {
                        storage_set_defaults(&cfg);
                        storage_save(&cfg);
                        if (lcd_ok) {
                            lcd_print_line(2, ">> DA KHOI PHUC! <<");
                            vTaskDelay(pdMS_TO_TICKS(700));
                        }
                        need_lcd_update = true;
                    }
                }
            } else if (screen == SCR_EDIT) {
                int min_v, max_v, step;
                get_edit_limits(menu_cursor, &cfg, &min_v, &max_v, &step);

                if (btn == BTN_UP) {
                    if (edit_val + step <= max_v) edit_val += step;
                    need_lcd_update = true;
                } else if (btn == BTN_DOWN) {
                    if (edit_val - step >= min_v) edit_val -= step;
                    need_lcd_update = true;
                } else if (btn == BTN_MENU) {
                    screen = SCR_MENU;
                    need_lcd_update = true;
                } else if (btn == BTN_OK) {
                    if (menu_cursor == 0)      cfg.temp_fan_on  = edit_val;
                    else if (menu_cursor == 1) cfg.temp_fan_max = edit_val;
                    else if (menu_cursor == 2) cfg.temp_alarm   = edit_val;
                    else                       cfg.smoke_alarm  = edit_val;

                    esp_err_t err = storage_save(&cfg);
                    if (lcd_ok) {
                        lcd_print_line(3, (err == ESP_OK) ? ">> DA LUU VAO NVS <<" : ">> LOI LUU NVS!   <<");
                        vTaskDelay(pdMS_TO_TICKS(700));
                    }
                    screen = SCR_MENU;
                    need_lcd_update = true;
                }
            } else if (screen == SCR_TEST) {
                if (btn == BTN_MENU) {
                    buzzer_set(false);
                    led_alarm_set(false);
                    screen = SCR_MAIN;
                    need_lcd_update = true;
                } else if (btn == BTN_UP && test_fan < 100) {
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
            }
        }

        if (now - last_sensor_tick >= pdMS_TO_TICKS(1000)) {
            last_sensor_tick = now;

            temp_ok = ds18b20_read_temp(&temp_c);
            ds18b20_start_convert();
            gas_mv = mq2_read_mv();

            if (warmup_sec > 0) warmup_sec--;

            if (temp_ok) {
                if (temp_c >= (float)cfg.temp_alarm) alarm_temp = true;
                else if (temp_c <= (float)cfg.temp_alarm - DEFAULT_TEMP_HYST) alarm_temp = false;
            } else {
                alarm_temp = false;
            }

            if (warmup_sec == 0) {
                if (gas_mv >= cfg.smoke_alarm) alarm_gas = true;
                else if (gas_mv <= cfg.smoke_alarm - DEFAULT_SMOKE_HYST) alarm_gas = false;
            } else {
                alarm_gas = false;
            }

            if (!alarm_temp && !alarm_gas) {
                buzzer_muted = false;
            }

            if (screen != SCR_TEST) {
                if (!temp_ok || alarm_temp || alarm_gas) {
                    fan_set_percent(100);
                } else {
                    fan_set_percent(calc_auto_fan(temp_c, &cfg, &fan_on));
                }
            }

            bool new_danger = (!temp_ok && prev_temp_ok) ||
                              (alarm_temp && !prev_alm_temp) ||
                              (alarm_gas && !prev_alm_gas);
            bool repeat_danger = (!temp_ok || alarm_temp || alarm_gas) &&
                                 ((now - last_tg_tick) >= pdMS_TO_TICKS(TG_REPEAT_SEC * 1000));

            if (new_danger || repeat_danger) {
                last_tg_tick = now;
                if (!temp_ok) {
                    snprintf(tg_buf, sizeof(tg_buf),
                             "[CANH BAO] Mat tin hieu cam bien nhiet DS18B20! Quat dang chay 100%% (SAFE).");
                } else {
                    snprintf(tg_buf, sizeof(tg_buf),
                             "[CANH BAO] %s%s%s | T=%.1fC (Nguong %dC) | Gas=%dmV (Nguong %dmV) | Quat=%d%%",
                             alarm_temp ? "QUA NHIET" : "",
                             (alarm_temp && alarm_gas) ? " + " : "",
                             alarm_gas ? "KHOI/GAS" : "",
                             temp_c, cfg.temp_alarm, gas_mv, cfg.smoke_alarm, fan_get_percent());
                }
                wifi_tg_send_alert(tg_buf);
            } else if ((prev_alm_temp || prev_alm_gas) && !alarm_temp && !alarm_gas && temp_ok) {
                snprintf(tg_buf, sizeof(tg_buf),
                         "[AN TOAN] He thong da tro lai binh thuong (T=%.1fC, Gas=%dmV).", temp_c, gas_mv);
                wifi_tg_send_alert(tg_buf);
            }

            prev_alm_temp = alarm_temp;
            prev_alm_gas  = alarm_gas;
            prev_temp_ok  = temp_ok;

            ESP_LOGI(TAG, "T=%.1f(%s) | GAS=%dmV | FAN=%d%% | WIFI=%s",
                     temp_c, temp_ok ? "OK" : "ERR", gas_mv, fan_get_percent(),
                     wifi_tg_is_connected() ? "OK" : "--");

            if (screen == SCR_MAIN || screen == SCR_TEST) {
                need_lcd_update = true;
            }
        }

        if (screen != SCR_TEST && (now - last_blink_tick >= pdMS_TO_TICKS(500))) {
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

        if (need_lcd_update && lcd_ok) {
            if (screen == SCR_MENU) {
                draw_menu(menu_cursor, &cfg);
            } else if (screen == SCR_EDIT) {
                draw_edit(menu_cursor, edit_val);
            } else {
                ui.temp    = temp_c;
                ui.temp_ok = temp_ok;
                ui.gas_mv  = gas_mv;
                ui.fan_pct = fan_get_percent();
                ui.wifi    = wifi_tg_is_connected() ? "OK" : "--";

                if (screen == SCR_TEST) {
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
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}