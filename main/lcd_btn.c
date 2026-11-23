#include "lcd_btn.h"
#include "config.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "LCD_BTN";

#define LCD_RS  0x01
#define LCD_EN  0x04
#define LCD_BL  0x08

static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t lcd_dev = NULL;
static uint8_t bl_mask = LCD_BL;

// ==================== 1. GIAO TIEP I2C VOI LCD ====================
static void lcd_write_4bit(uint8_t data)
{
    if (!lcd_dev) return;
    uint8_t hi = data | bl_mask;
    uint8_t en = hi | LCD_EN;
    i2c_master_transmit(lcd_dev, &hi, 1, 50);
    i2c_master_transmit(lcd_dev, &en, 1, 50);
    esp_rom_delay_us(1);
    i2c_master_transmit(lcd_dev, &hi, 1, 50);
    esp_rom_delay_us(50);
}

static void lcd_send(uint8_t val, uint8_t mode)
{
    lcd_write_4bit((val & 0xF0) | mode);
    lcd_write_4bit(((val << 4) & 0xF0) | mode);
}

// ==================== 2. HAM CO BAN CUA LCD ====================
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

    // Tu dong do dia chi 0x27 hoac 0x3F
    uint8_t addr = 0;
    if (i2c_master_probe(i2c_bus, LCD_ADDR, 50) == ESP_OK)      addr = LCD_ADDR;
    else if (i2c_master_probe(i2c_bus, 0x3F, 50) == ESP_OK)     addr = 0x3F;
    else {
        ESP_LOGE(TAG, "Khong tim thay LCD I2C (0x27/0x3F)");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Tim thay LCD tai 0x%02X", addr);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &lcd_dev));

    // Chuoi khoi tao chuan HD44780 che do 4-bit
    vTaskDelay(pdMS_TO_TICKS(50));
    lcd_write_4bit(0x30); esp_rom_delay_us(4500);
    lcd_write_4bit(0x30); esp_rom_delay_us(4500);
    lcd_write_4bit(0x30); esp_rom_delay_us(150);
    lcd_write_4bit(0x20);

    lcd_send(0x28, 0);  // 4-bit, 4 dong
    lcd_send(0x0C, 0);  // Bat hien thi, an con tro
    lcd_send(0x06, 0);  // Tu dong tang con tro
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
    bl_mask = on ? LCD_BL : 0x00;
    if (lcd_dev) i2c_master_transmit(lcd_dev, &bl_mask, 1, 50);
}

// Ghi du 20 ky tu len 1 dong (tu dien khoang trang de xoa chu cu, khong bi nhay)
void lcd_print_line(uint8_t row, const char *text)
{
    if (!lcd_dev || row >= LCD_ROWS) return;
    static const uint8_t row_addr[4] = {0x00, 0x40, 0x14, 0x54};

    char buf[LCD_COLS];
    memset(buf, ' ', LCD_COLS);
    if (text) {
        size_t len = strlen(text);
        if (len > LCD_COLS) len = LCD_COLS;
        memcpy(buf, text, len);
    }

    lcd_send(0x80 | row_addr[row], 0);
    for (int i = 0; i < LCD_COLS; i++) {
        lcd_send((uint8_t)buf[i], LCD_RS);
    }
}

// ==================== 3. VE MAN HINH CHINH (20x4) ====================
void lcd_show_main(const lcd_data_t *d)
{
    if (!d) return;
    char line[64];

    // Dong 1: |T:32.5C  GAS:1250mV |
    if (d->temp_ok) {
        int t10 = (int)(d->temp * 10.0f + 0.5f);
        if (t10 < 0) t10 = 0;
        snprintf(line, sizeof(line), "T:%2d.%dC  GAS:%4dmV", t10 / 10, t10 % 10, d->gas_mv);
    } else {
        snprintf(line, sizeof(line), "T: ERR   GAS:%4dmV", d->gas_mv);
    }
    lcd_print_line(0, line);

    // Dong 2: |FAN: 40%  MODE: AUTO|
    snprintf(line, sizeof(line), "FAN:%3d%%  MODE: %-4.4s",
             d->fan_pct, d->mode ? d->mode : "AUTO");
    lcd_print_line(1, line);

    // Dong 3: |ST: NORMAL          |
    snprintf(line, sizeof(line), "ST: %-16.16s",
             d->status ? d->status : "NORMAL");
    lcd_print_line(2, line);

    // Dong 4: |WIFI: --   [MENU]   |
    snprintf(line, sizeof(line), "WIFI: %-4.4s [MENU]",
             d->wifi ? d->wifi : "--");
    lcd_print_line(3, line);
}

// ==================== 4. DOC 4 NUT NHAN (CHONG DOI 50ms) ====================
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
    static bool last_state[4] = {false, false, false, false};
    static TickType_t last_tick[4] = {0, 0, 0, 0};

    TickType_t now = xTaskGetTickCount();
    for (int i = 0; i < 4; i++) {
        bool pressed = (gpio_get_level(pins[i]) == 0);
        if (pressed != last_state[i] && (now - last_tick[i] >= pdMS_TO_TICKS(50))) {
            last_state[i] = pressed;
            last_tick[i] = now;
            if (pressed) return (button_id_t)(i + 1);
        }
    }
    return BTN_NONE;
}