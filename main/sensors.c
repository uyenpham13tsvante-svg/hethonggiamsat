#include "sensors.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SENSORS";

static portMUX_TYPE ow_mux = portMUX_INITIALIZER_UNLOCKED;
static adc_oneshot_unit_handle_t adc_handle = NULL;
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
static adc_cali_handle_t cali_handle = NULL;
static bool cali_ok = false;
#endif

// ==================== 1. GIAO TIEP 1-WIRE (DS18B20) ====================
static bool ow_reset(void)
{
    gpio_set_level(PIN_DS18B20, 0);
    esp_rom_delay_us(480);

    taskENTER_CRITICAL(&ow_mux);
    gpio_set_level(PIN_DS18B20, 1);
    esp_rom_delay_us(70);
    int presence = gpio_get_level(PIN_DS18B20);
    taskEXIT_CRITICAL(&ow_mux);

    esp_rom_delay_us(410);
    return (presence == 0);
}

static void ow_write_bit(int bit)
{
    taskENTER_CRITICAL(&ow_mux);
    gpio_set_level(PIN_DS18B20, 0);
    if (bit) {
        esp_rom_delay_us(6);
        gpio_set_level(PIN_DS18B20, 1);
        taskEXIT_CRITICAL(&ow_mux);
        esp_rom_delay_us(64);
    } else {
        esp_rom_delay_us(60);
        gpio_set_level(PIN_DS18B20, 1);
        taskEXIT_CRITICAL(&ow_mux);
        esp_rom_delay_us(10);
    }
}

static int ow_read_bit(void)
{
    taskENTER_CRITICAL(&ow_mux);
    gpio_set_level(PIN_DS18B20, 0);
    esp_rom_delay_us(6);
    gpio_set_level(PIN_DS18B20, 1);
    esp_rom_delay_us(9);
    int bit = gpio_get_level(PIN_DS18B20);
    taskEXIT_CRITICAL(&ow_mux);

    esp_rom_delay_us(55);
    return bit;
}

static void ow_write_byte(uint8_t byte)
{
    for (int i = 0; i < 8; i++) {
        ow_write_bit((byte >> i) & 0x01);
    }
}

static uint8_t ow_read_byte(void)
{
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        if (ow_read_bit()) byte |= (1 << i);
    }
    return byte;
}

static uint8_t ow_crc8(const uint8_t *data, int len)
{
    uint8_t crc = 0;
    for (int i = 0; i < len; i++) {
        uint8_t in = data[i];
        for (int b = 0; b < 8; b++) {
            uint8_t mix = (crc ^ in) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            in >>= 1;
        }
    }
    return crc;
}

bool ds18b20_start_convert(void)
{
    if (!ow_reset()) return false;
    ow_write_byte(0xCC);    // Skip ROM (1 cam bien)
    ow_write_byte(0x44);    // Convert T
    return true;
}

bool ds18b20_read_temp(float *temp_c)
{
    if (!temp_c || !ow_reset()) return false;

    ow_write_byte(0xCC);    // Skip ROM
    ow_write_byte(0xBE);    // Read Scratchpad

    uint8_t sp[9];
    for (int i = 0; i < 9; i++) {
        sp[i] = ow_read_byte();
    }

    if (ow_crc8(sp, 8) != sp[8]) return false;

    int16_t raw = (int16_t)((sp[1] << 8) | sp[0]);
    float t = raw / 16.0f;
    if (t < -55.0f || t > 125.0f) return false;

    *temp_c = t;
    return true;
}

// ==================== 2. KHOI TAO & DOC MQ-2 (ADC1) ====================
void sensors_init(void)
{
    // DS18B20: Open-drain, can tro keo len 4.7k ngoai len 3.3V
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << PIN_DS18B20),
        .mode         = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_cfg));
    gpio_set_level(PIN_DS18B20, 1);

    // MQ-2: ADC1_CH7 (GPIO 35)
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = MQ2_ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten    = MQ2_ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, MQ2_ADC_CHANNEL, &chan_cfg));

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id  = MQ2_ADC_UNIT,
        .atten    = MQ2_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_line_fitting(&cali_cfg, &cali_handle) == ESP_OK) {
        cali_ok = true;
        ESP_LOGI(TAG, "ADC da bat hieu chuan Line Fitting");
    }
#endif
}

int mq2_read_mv(void)
{
    if (!adc_handle) return 0;

    int sum = 0, raw = 0;
    for (int i = 0; i < 16; i++) {
        if (adc_oneshot_read(adc_handle, MQ2_ADC_CHANNEL, &raw) == ESP_OK) {
            sum += raw;
        }
    }
    int avg_raw = sum / 16;

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (cali_ok) {
        int mv = 0;
        if (adc_cali_raw_to_voltage(cali_handle, avg_raw, &mv) == ESP_OK) {
            return mv;
        }
    }
#endif
    return (avg_raw * 3300) / 4095;
}