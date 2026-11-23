#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lcd_btn.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    btn_init();
    bool lcd_ok = (lcd_init() == ESP_OK);

    // Du lieu mau de test giao dien (sau nay thay bang cam bien that)
    static const char *st_list[] = {"NORMAL", "WARMUP", "ALM:TEMP", "ALM:GAS", "ERR"};
    int st_idx = 0;
    bool bl_on = true;

    lcd_data_t ui = {
        .temp    = 32.5f,
        .temp_ok = true,
        .gas_mv  = 1250,
        .fan_pct = 40,
        .mode    = "AUTO",
        .status  = st_list[0],
        .wifi    = "--",
    };

    if (lcd_ok) lcd_show_main(&ui);

    while (1) {
        button_id_t btn = btn_read();
        if (btn != BTN_NONE) {
            if (btn == BTN_UP && ui.fan_pct <= 90) {
                ui.fan_pct += 10;                       // UP: tang % quat
            } else if (btn == BTN_DOWN && ui.fan_pct >= 10) {
                ui.fan_pct -= 10;                       // DOWN: giam % quat
            } else if (btn == BTN_MENU) {
                st_idx = (st_idx + 1) % 5;              // MENU: xoay vong trang thai ST
                ui.status = st_list[st_idx];
                ui.mode = (st_idx == 4) ? "SAFE" : "AUTO";
            } else if (btn == BTN_OK) {
                bl_on = !bl_on;                         // OK: bat/tat den nen
                if (lcd_ok) lcd_backlight(bl_on);
            }

            ESP_LOGI(TAG, "FAN=%d%% | MODE=%s | ST=%s", ui.fan_pct, ui.mode, ui.status);
            if (lcd_ok) lcd_show_main(&ui);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}