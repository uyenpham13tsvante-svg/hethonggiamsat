#ifndef WIFI_TG_H
#define WIFI_TG_H

#include <stdbool.h>

// ================= CAU HINH WI-FI & TELEGRAM =================
// Ban code 2 dien thong tin that vao 4 dong duoi day truoc khi nap:
#define WIFI_SSID       "TEN_WIFI_CUA_BAN"
#define WIFI_PASS       "MAT_KHAU_WIFI"
#define TG_BOT_TOKEN    "123456789:ABCdefGHIjklMNOpqrsTUVwxyz"
#define TG_CHAT_ID      "123456789"
// =============================================================

void wifi_tg_init(void);
bool wifi_tg_is_connected(void);
void wifi_tg_send_alert(const char *msg);

#endif
