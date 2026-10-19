#include "link_stats.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* A motion frame starts a new event only after this much quiet time */
#define MOTION_EVENT_GAP_US (1500 * 1000)

static link_stats_t s_stats;
static SemaphoreHandle_t s_lock;

void link_stats_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    memset(&s_stats, 0, sizeof(s_stats));
}

void link_stats_get(link_stats_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_stats;
    xSemaphoreGive(s_lock);
}

void link_stats_set_online(bool online)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_stats.online = online;
    s_stats.online_total = 0;
    s_stats.online_sent = 0;
    xSemaphoreGive(s_lock);
}

void link_stats_on_frame(bool sent, uint32_t bytes, bool motion, int64_t now_us)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_stats.frames_total++;
    if (s_stats.online) {
        s_stats.online_total++;
    }
    if (sent) {
        s_stats.frames_sent++;
        s_stats.online_sent++;
        s_stats.bytes_sent += bytes;
    }
    if (motion) {
        bool new_event = s_stats.last_motion_us == 0 ||
                         now_us - s_stats.last_motion_us > MOTION_EVENT_GAP_US;
        if (new_event && !s_stats.online) {
            s_stats.motion_events++;
        }
        s_stats.last_motion_us = now_us;
    }
    xSemaphoreGive(s_lock);
}
