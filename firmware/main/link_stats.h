#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Counters the stream task produces and the UI task renders. The stream
 * task only bumps counters; rates are derived by the reader from deltas,
 * so the hot loop never does division or formatting.
 */
typedef struct {
    bool online;             /* a PC client is connected */
    uint32_t frames_total;   /* captured since boot */
    uint32_t frames_sent;    /* sent to the PC since boot */
    uint32_t online_total;   /* captured since the current client connected */
    uint32_t online_sent;    /* sent since the current client connected */
    uint64_t bytes_sent;     /* JPEG payload bytes since boot */
    uint32_t motion_events;  /* motion onsets while offline */
    int64_t last_motion_us;  /* esp_timer time of the latest motion frame, 0 if none */
} link_stats_t;

void link_stats_init(void);
void link_stats_get(link_stats_t *out);

/* Writers, called from the stream task only */
void link_stats_set_online(bool online);
void link_stats_on_frame(bool sent, uint32_t bytes, bool motion, int64_t now_us);
