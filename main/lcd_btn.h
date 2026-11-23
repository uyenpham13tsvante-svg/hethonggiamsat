#ifndef LCD_BTN_H
#define LCD_BTN_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// 4 nut nhan vat ly
typedef enum {
    BTN_NONE = 0,
    BTN_MENU,
    BTN_UP,
    BTN_DOWN,
    BTN_OK
} button_id_t;

// Du lieu hien thi len man hinh chinh 20x4
typedef struct {
    float       temp;       // Nhiet do (C), vd: 32.5
    bool        temp_ok;    // false neu loi cam bien DS18B20
    int         gas_mv;     // Dien ap MQ-2 (mV), vd: 1250
    int         fan_pct;    // Toc do quat (0..100%)
    const char *mode;       // "AUTO" hoac "SAFE"
    const char *status;     // "NORMAL", "WARMUP", "ALM:TEMP", "ALM:GAS", "ERR"
    const char *wifi;       // "--", "OK", "ERR"...
} lcd_data_t;

// Ham dieu khien LCD
esp_err_t   lcd_init(void);
void        lcd_clear(void);
void        lcd_backlight(bool on);
void        lcd_print_line(uint8_t row, const char *text);
void        lcd_show_main(const lcd_data_t *d);

// Ham doc nut nhan
void        btn_init(void);
button_id_t btn_read(void);

#endif