#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "config.h"
#include "lcd_btn.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    btn_init();
    bool lcd_ok = (lcd_init() == ESP_OK);

    if (lcd_ok) {
        lcd_print_line(0, "TEST LCD & 4 NUT");
        lcd_print_line(1, "Nut nhan: NONE");
        lcd_print_line(2, "So lan nhan: 0");
        lcd_print_line(3, "OK: Bat/Tat den nen");
    }

    int count = 0;
    bool bl_on = true;
    char line[32];

    while (1) {
        button_id_t btn = btn_read();
        if (btn != BTN_NONE) {
            const char *name = "NONE";
            if (btn == BTN_MENU) name = "MENU";
            else if (btn == BTN_UP) name = "UP";
            else if (btn == BTN_DOWN) name = "DOWN";
            else if (btn == BTN_OK) {
                name = "OK";
                bl_on = !bl_on;
                if (lcd_ok) lcd_backlight(bl_on);
            }

            count++;
            ESP_LOGI(TAG, "Nut: %s | Dem: %d | Den nen: %s", name, count, bl_on ? "ON" : "OFF");

            if (lcd_ok) {
                snprintf(line, sizeof(line), "Nut nhan: %s", name);
                lcd_print_line(1, line);
                snprintf(line, sizeof(line), "So lan nhan: %d", count);
                lcd_print_line(2, line);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}