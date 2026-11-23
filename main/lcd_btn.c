#include "lcd_btn.h"
#include "config.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "LCD_BTN";

#define LCD_RS  0x01
#define LCD_EN  0x04
#define LCD_BL  0x08

static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t lcd_dev = NULL;
static uint8_t backlight_mask = LCD_BL;

static void pcf_write(uint8_t val)
{
    if (!lcd_dev) return;
    uint8_t data = val | backlight_mask;
    i2c_master_transmit(lcd_dev, &data, 1, 50);
}

static void lcd_send_nibble(uint8_t nibble)
{
    pcf_write(nibble);
    pcf_write(nibble | LCD_EN);
    esp_rom_delay_us(1);
    pcf_write(nibble & ~LCD_EN);
    esp_rom_delay_us(50);
}

static void lcd_send(uint8_t val, uint8_t mode)
{
    lcd_send_nibble((val & 0xF0) | mode);
    lcd_send_nibble(((val << 4) & 0xF0) | mode);
}

esp_err_t lcd_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = PIN_I2C_SCL,
        .sda_io_num = PIN_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus));

    uint8_t addr = 0;
    if (i2c_master_probe(i2c_bus, LCD_ADDR, 50) == ESP_OK) {
        addr = LCD_ADDR;
    } else if (i2c_master_probe(i2c_bus, 0x3F, 50) == ESP_OK) {
        addr = 0x3F;
    } else {
        ESP_LOGE(TAG, "Khong tim thay LCD I2C (0x27 hoac 0x3F)");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Tim thay LCD tai 0x%02X", addr);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &lcd_dev));

    vTaskDelay(pdMS_TO_TICKS(50));
    lcd_send_nibble(0x30); esp_rom_delay_us(4500);
    lcd_send_nibble(0x30); esp_rom_delay_us(4500);
    lcd_send_nibble(0x30); esp_rom_delay_us(150);
    lcd_send_nibble(0x20);

    lcd_send(0x28, 0);
    lcd_send(0x0C, 0);
    lcd_send(0x06, 0);
    lcd_clear();
    return ESP_OK;
}

void lcd_clear(void)
{
    if (!lcd_dev) return;
    lcd_send(0x01, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
}

void lcd_backlight(bool on)
{
    backlight_mask = on ? LCD_BL : 0x00;
    pcf_write(0x00);
}

void lcd_print_line(uint8_t row, const char *text)
{
    if (!lcd_dev) return;
    static const uint8_t offsets[4] = {0x00, 0x40, 0x14, 0x54};
    if (row >= LCD_ROWS) row = LCD_ROWS - 1;

    char buf[LCD_COLS + 1];
    memset(buf, ' ', LCD_COLS);
    if (text) {
        size_t len = strlen(text);
        if (len > LCD_COLS) len = LCD_COLS;
        memcpy(buf, text, len);
    }
    buf[LCD_COLS] = '\0';

    lcd_send(0x80 | offsets[row], 0);
    for (int i = 0; i < LCD_COLS; i++) {
        lcd_send((uint8_t)buf[i], LCD_RS);
    }
}

void btn_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_BTN_MENU) | (1ULL << PIN_BTN_UP) |
                        (1ULL << PIN_BTN_DOWN) | (1ULL << PIN_BTN_OK),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&cfg);
}

button_id_t btn_read(void)
{
    static const gpio_num_t pins[4] = {PIN_BTN_MENU, PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_OK};
    static bool was_pressed[4] = {false, false, false, false};
    static TickType_t last_time[4] = {0, 0, 0, 0};

    TickType_t now = xTaskGetTickCount();

    for (int i = 0; i < 4; i++) {
        bool is_pressed = (gpio_get_level(pins[i]) == 0);

        if (is_pressed && !was_pressed[i] && (now - last_time[i] >= pdMS_TO_TICKS(50))) {
            was_pressed[i] = true;
            last_time[i] = now;
            return (button_id_t)(i + 1);
        }
        if (!is_pressed && was_pressed[i] && (now - last_time[i] >= pdMS_TO_TICKS(50))) {
            was_pressed[i] = false;
            last_time[i] = now;
        }
    }
    return BTN_NONE;
}