#include "ui.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/temperature_sensor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "link_stats.h"

static const char *TAG = "ui";

#define REFRESH_MS 500
#define MOTION_NOW_US (1000 * 1000)
#define OFFLINE_ROTATE_US (3000 * 1000)

typedef struct {
    lcd1602_t *lcd;
    char ip[16];
    temperature_sensor_handle_t tsens;
} ui_ctx_t;

static ui_ctx_t s_ui;

/* Only rewrite rows that changed: each row costs ~30 I2C transactions */
static void put_row(int row, const char *text)
{
    static char shown[LCD1602_ROWS][LCD1602_COLS + 1];
    char padded[LCD1602_COLS + 1];
    snprintf(padded, sizeof(padded), "%-16.16s", text);
    if (strcmp(shown[row], padded) != 0) {
        lcd1602_write_row(s_ui.lcd, row, padded);
        strcpy(shown[row], padded);
    }
}

static void render_online(const link_stats_t *now, const link_stats_t *prev, float dt_s)
{
    char row[32];
    int rssi = 0;
    if (esp_wifi_sta_get_rssi(&rssi) != ESP_OK) {
        rssi = 0;
    }
    int saved = now->online_total ? (int)(100 - 100ULL * now->online_sent / now->online_total) : 0;
    snprintf(row, sizeof(row), "RSSI%d save %d%%", rssi, saved);
    put_row(0, row);

    float fps = (now->frames_total - prev->frames_total) / dt_s;
    float kbps = (float)(now->bytes_sent - prev->bytes_sent) / 1024.0f / dt_s;
    float temp = 0;
    if (!s_ui.tsens || temperature_sensor_get_celsius(s_ui.tsens, &temp) != ESP_OK) {
        temp = 0;
    }
    snprintf(row, sizeof(row), "%.0ffps %.0fKB/s %.0fC", fps, kbps, temp);
    put_row(1, row);
}

static void render_offline(const link_stats_t *now, int64_t t_us)
{
    char row[32];
    bool moving = now->last_motion_us && t_us - now->last_motion_us < MOTION_NOW_US;
    put_row(0, moving ? "OFFLINE  MOTION!" : "OFFLINE  idle");

    /* Alternate between the motion log and the IP the PC should connect to */
    if ((t_us / OFFLINE_ROTATE_US) % 2 == 0) {
        if (now->last_motion_us) {
            int ago = (int)((t_us - now->last_motion_us) / 1000000);
            snprintf(row, sizeof(row), "evt %lu last %ds", (unsigned long)now->motion_events, ago);
        } else {
            snprintf(row, sizeof(row), "evt 0");
        }
        put_row(1, row);
    } else {
        put_row(1, s_ui.ip);
    }
}

static void ui_task(void *arg)
{
    link_stats_t prev, now;
    link_stats_get(&prev);
    int64_t prev_t = esp_timer_get_time();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(REFRESH_MS));
        link_stats_get(&now);
        int64_t t = esp_timer_get_time();
        float dt_s = (t - prev_t) / 1e6f;

        if (now.online) {
            render_online(&now, &prev, dt_s);
        } else {
            render_offline(&now, t);
        }
        prev = now;
        prev_t = t;
    }
}

void ui_start(lcd1602_t *lcd, const char *ip)
{
    s_ui.lcd = lcd;
    strlcpy(s_ui.ip, ip, sizeof(s_ui.ip));

    temperature_sensor_config_t tcfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 80);
    if (temperature_sensor_install(&tcfg, &s_ui.tsens) == ESP_OK) {
        temperature_sensor_enable(s_ui.tsens);
    } else {
        ESP_LOGW(TAG, "temperature sensor unavailable");
        s_ui.tsens = NULL;
    }

    /* Low priority on core 0: LCD refresh must never delay capture on core 1 */
    xTaskCreatePinnedToCore(ui_task, "ui", 4096, NULL, 2, NULL, 0);
}
