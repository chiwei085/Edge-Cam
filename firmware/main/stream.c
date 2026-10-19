#include "stream.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"

#include "gate.h"
#include "link_stats.h"
#include "protocol.h"

static const char *TAG = "stream";

#define RESULT_TIMEOUT_S 3

typedef struct {
    int sock; /* -1 while offline */
    uint32_t seq, frames_total, frames_sent, prev_rtt_us;
    int64_t last_sent_us;
} client_t;

static bool send_all(int sock, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    while (len > 0) {
        int n = send(sock, p, len, 0);
        if (n <= 0) {
            return false;
        }
        p += n;
        len -= n;
    }
    return true;
}

static bool recv_all(int sock, void *buf, size_t len)
{
    uint8_t *p = buf;
    while (len > 0) {
        int n = recv(sock, p, len, 0);
        if (n <= 0) {
            return false;
        }
        p += n;
        len -= n;
    }
    return true;
}

static int open_listener(void)
{
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    int yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(CONFIG_EDGECAM_TCP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(listener, 1) != 0) {
        ESP_LOGE(TAG, "listen failed, errno %d", errno);
        close(listener);
        return -1;
    }
    /* Polled once per frame so capture and gating keep running offline */
    fcntl(listener, F_SETFL, fcntl(listener, F_GETFL, 0) | O_NONBLOCK);
    return listener;
}

static void try_accept(int listener, client_t *c, gate_t *gate)
{
    struct sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);
    int sock = accept(listener, (struct sockaddr *)&peer, &peer_len);
    if (sock < 0) {
        return;
    }
    char peer_ip[16];
    inet_ntoa_r(peer.sin_addr, peer_ip, sizeof(peer_ip));
    ESP_LOGI(TAG, "client %s connected", peer_ip);

    /* The client socket does blocking sends and receives with timeouts */
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) & ~O_NONBLOCK);
    int yes = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    struct timeval tv = { .tv_sec = RESULT_TIMEOUT_S };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    *c = (client_t){ .sock = sock };
    gate_reset(gate);
    link_stats_set_online(true);
}

static void drop_client(client_t *c, const char *why)
{
    ESP_LOGW(TAG, "client dropped: %s, errno %d", why, errno);
    close(c->sock);
    c->sock = -1;
    link_stats_set_online(false);
}

/* Stop-and-wait: the next frame goes out only after the previous result
 * came back, so latency numbers are not inflated by queueing. */
static bool send_frame(client_t *c, camera_fb_t *fb, int diff, bool motion,
                       uint32_t grab_us, uint32_t gate_us)
{
    frame_hdr_t hdr = {
        .magic = { 'E', 'C' },
        .version = PROTO_VERSION,
        .flags = motion ? 0 : FRAME_FLAG_KEEPALIVE,
        .seq = ++c->seq,
        .jpeg_len = fb->len,
        .width = fb->width,
        .height = fb->height,
        .grab_us = grab_us,
        .gate_us = gate_us,
        .prev_rtt_us = c->prev_rtt_us,
        .frames_total = c->frames_total,
        .frames_sent = ++c->frames_sent,
        .diff_permille = diff < 0 ? 0xFFFF : (uint16_t)diff,
    };

    int64_t send_start = esp_timer_get_time();
    c->last_sent_us = send_start;
    if (!send_all(c->sock, &hdr, sizeof(hdr)) || !send_all(c->sock, fb->buf, fb->len)) {
        drop_client(c, "send failed");
        return false;
    }

    result_msg_t res;
    if (!recv_all(c->sock, &res, sizeof(res))) {
        drop_client(c, "no result");
        return false;
    }
    c->prev_rtt_us = (uint32_t)(esp_timer_get_time() - send_start);
    if (memcmp(res.magic, "ER", 2) != 0 || res.seq != hdr.seq) {
        drop_client(c, "bad result");
        return false;
    }
    return true;
}

static void stream_task(void *arg)
{
    gate_t gate;
    int listener = open_listener();
    if (!gate_init(&gate, 640, 480) || listener < 0) {
        ESP_LOGE(TAG, "init failed");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "listening on port %d", CONFIG_EDGECAM_TCP_PORT);

    client_t client = { .sock = -1 };
    int64_t last_ref_us = 0;

    for (;;) {
        if (client.sock < 0) {
            try_accept(listener, &client, &gate);
        }

        int64_t t0 = esp_timer_get_time();
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGW(TAG, "capture failed");
            continue;
        }
        int64_t t1 = esp_timer_get_time();
        int diff = gate_diff(&gate, fb->buf, fb->len);
        int64_t t2 = esp_timer_get_time();

        bool online = client.sock >= 0;
        if (online) {
            client.frames_total++;
        }
        bool motion = diff >= CONFIG_EDGECAM_GATE_PERMILLE;
        int64_t since = t2 - (online ? client.last_sent_us : last_ref_us);
        bool keepalive = since >= (int64_t)CONFIG_EDGECAM_KEEPALIVE_MS * 1000;
        /* A failed decode is not motion, but the PC may still want the frame */
        bool take = motion || keepalive || diff < 0;

        bool sent = false;
        if (take) {
            gate_accept(&gate);
            last_ref_us = t2;
            if (online) {
                sent = send_frame(&client, fb, diff, motion, (uint32_t)(t1 - t0), (uint32_t)(t2 - t1));
            }
        }
        link_stats_on_frame(sent, sent ? fb->len : 0, motion, t2);
        esp_camera_fb_return(fb);
    }
}

void stream_start(void)
{
    /* Core 1, away from the Wi-Fi stack which runs on core 0 */
    xTaskCreatePinnedToCore(stream_task, "stream", 8192, NULL, 5, NULL, 1);
}
