#include "wifi_tg.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

static const char *TAG = "WIFI_TG";

#define TG_QUEUE_LEN    4
#define TG_MSG_MAX_LEN  220

static bool s_wifi_connected = false;
static QueueHandle_t s_tg_queue = NULL;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        ESP_LOGW(TAG, "Mat ket noi Wi-Fi, dang thu ket noi lai...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_wifi_connected = true;
        ESP_LOGI(TAG, "Da ket noi Wi-Fi! IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static void tg_post_message(const char *text)
{
    char url[160];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendMessage", TG_BOT_TOKEN);

    char escaped[TG_MSG_MAX_LEN * 2];
    size_t j = 0;
    for (size_t i = 0; text[i] != '\0' && j + 3 < sizeof(escaped); i++) {
        if (text[i] == '"' || text[i] == '\\') {
            escaped[j++] = '\\';
            escaped[j++] = text[i];
        } else if (text[i] == '\n') {
            escaped[j++] = '\\';
            escaped[j++] = 'n';
        } else {
            escaped[j++] = text[i];
        }
    }
    escaped[j] = '\0';

    char payload[TG_MSG_MAX_LEN * 2 + 96];
    snprintf(payload, sizeof(payload),
             "{\"chat_id\":\"%s\",\"text\":\"%s\"}", TG_CHAT_ID, escaped);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 8000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Khong khoi tao duoc HTTP client");
        return;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload, strlen(payload));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Da gui Telegram (HTTP %d)", status);
    } else {
        ESP_LOGE(TAG, "Loi gui Telegram: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

static void tg_sender_task(void *pvParameters)
{
    char msg[TG_MSG_MAX_LEN];
    while (1) {
        if (xQueueReceive(s_tg_queue, msg, portMAX_DELAY) == pdTRUE) {
            if (s_wifi_connected) {
                tg_post_message(msg);
            } else {
                ESP_LOGW(TAG, "Chua co Wi-Fi, bo qua tin nhan: %s", msg);
            }
        }
    }
}

void wifi_tg_init(void)
{
    s_tg_queue = xQueueCreate(TG_QUEUE_LEN, TG_MSG_MAX_LEN);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    xTaskCreate(tg_sender_task, "tg_sender", 6144, NULL, 3, NULL);
    ESP_LOGI(TAG, "Da khoi dong Wi-Fi STA (SSID: %s)", WIFI_SSID);
}

bool wifi_tg_is_connected(void)
{
    return s_wifi_connected;
}

void wifi_tg_send_alert(const char *msg)
{
    if (!s_tg_queue || !msg) return;
    char buf[TG_MSG_MAX_LEN];
    strncpy(buf, msg, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    xQueueSend(s_tg_queue, buf, 0);
}