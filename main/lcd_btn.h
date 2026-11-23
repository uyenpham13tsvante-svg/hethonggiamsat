#ifndef LCD_BTN_H
#define LCD_BTN_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    BTN_NONE = 0,
    BTN_MENU,
    BTN_UP,
    BTN_DOWN,
    BTN_OK
} button_id_t;

esp_err_t   lcd_init(void);
void        lcd_clear(void);
void        lcd_backlight(bool on);
void        lcd_print_line(uint8_t row, const char *text);

void        btn_init(void);
button_id_t btn_read(void);

#endif