#include "storage.h"
#include "config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <stdint.h>

static const char *TAG = "STORAGE";
#define NVS_NAMESPACE "sys_cfg"

void storage_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Phan vung NVS can dinh dang lai, dang xoa...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void storage_set_defaults(sys_config_t *cfg)
{
    if (!cfg) return;
    cfg->temp_fan_on  = (int)DEFAULT_TEMP_FAN_ON;
    cfg->temp_fan_max = (int)DEFAULT_TEMP_FAN_MAX;
    cfg->temp_alarm   = (int)DEFAULT_TEMP_ALARM;
    cfg->smoke_alarm  = DEFAULT_SMOKE_ALARM;
}

void storage_load(sys_config_t *cfg)
{
    if (!cfg) return;
    storage_set_defaults(cfg);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Chua co cau hinh trong NVS, dung gia tri mac dinh");
        return;
    }

    int32_t val = 0;
    if (nvs_get_i32(h, "t_on", &val) == ESP_OK)    cfg->temp_fan_on  = (int)val;
    if (nvs_get_i32(h, "t_max", &val) == ESP_OK)   cfg->temp_fan_max = (int)val;
    if (nvs_get_i32(h, "t_alm", &val) == ESP_OK)   cfg->temp_alarm   = (int)val;
    if (nvs_get_i32(h, "gas_alm", &val) == ESP_OK) cfg->smoke_alarm  = (int)val;
    nvs_close(h);

    if (cfg->temp_fan_on < 15 || cfg->temp_fan_max <= cfg->temp_fan_on ||
        cfg->temp_alarm <= cfg->temp_fan_max || cfg->temp_alarm > 80 ||
        cfg->smoke_alarm < 500 || cfg->smoke_alarm > 3000) {
        ESP_LOGW(TAG, "Nguong trong NVS khong hop le, khoi phuc mac dinh");
        storage_set_defaults(cfg);
    }

    ESP_LOGI(TAG, "Da tai NVS: T_ON=%dC | T_MAX=%dC | T_ALM=%dC | GAS=%dmV",
             cfg->temp_fan_on, cfg->temp_fan_max, cfg->temp_alarm, cfg->smoke_alarm);
}

esp_err_t storage_save(const sys_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Loi mo NVS: %s", esp_err_to_name(err));
        return err;
    }

    nvs_set_i32(h, "t_on",    (int32_t)cfg->temp_fan_on);
    nvs_set_i32(h, "t_max",   (int32_t)cfg->temp_fan_max);
    nvs_set_i32(h, "t_alm",   (int32_t)cfg->temp_alarm);
    nvs_set_i32(h, "gas_alm", (int32_t)cfg->smoke_alarm);

    err = nvs_commit(h);
    nvs_close(h);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Da luu NVS: T_ON=%dC | T_MAX=%dC | T_ALM=%dC | GAS=%dmV",
                 cfg->temp_fan_on, cfg->temp_fan_max, cfg->temp_alarm, cfg->smoke_alarm);
    }
    return err;
}
