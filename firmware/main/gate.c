#include "gate.h"

#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "jpeg_decoder.h"
#include "sdkconfig.h"

static const char *TAG = "gate";

/* Per-pixel luma change that counts as "changed"; above sensor noise */
#define PIXEL_THRESHOLD 24

/* Same sizes esp_jpeg uses internally: the Huffman lookup tables of
 * JD_FASTDECODE level 2 need far more workspace than the basic decoder */
#if defined(CONFIG_JD_FASTDECODE) && CONFIG_JD_FASTDECODE == 2
#define WORK_BUF_SIZE 65472
#else
#define WORK_BUF_SIZE 3100
#endif

/* Largest JPEG we expect at VGA, quality 12; bigger frames decode from PSRAM */
#define JPEG_COPY_MAX (64 * 1024)

static uint8_t *s_work;
static uint8_t *s_jpeg_copy;

bool gate_init(gate_t *g, int frame_w, int frame_h)
{
    g->w = frame_w / 8;
    g->h = frame_h / 8;
    size_t n = (size_t)g->w * g->h;
    /* Small and hot, keep in internal RAM */
    g->rgb565 = heap_caps_malloc(n * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    g->luma = heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    g->ref = heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_work = heap_caps_malloc(WORK_BUF_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#if CONFIG_EDGECAM_GATE_COPY_INTERNAL
    s_jpeg_copy = heap_caps_malloc(JPEG_COPY_MAX, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
    g->has_ref = false;
#if CONFIG_JD_USE_ROM
    ESP_LOGI(TAG, "decoder: ROM TJpgDec");
#else
    ESP_LOGI(TAG, "decoder: component TJpgDec, fastdecode level %d", CONFIG_JD_FASTDECODE);
#endif
    ESP_LOGI(TAG, "work buf %d B, jpeg copy to internal RAM: %s",
             WORK_BUF_SIZE, s_jpeg_copy ? "on" : "off");
    ESP_LOGI(TAG, "internal heap free after alloc: %u B",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return g->rgb565 && g->luma && g->ref && s_work;
}

int gate_diff(gate_t *g, const uint8_t *jpeg, size_t len)
{
    const uint8_t *src = jpeg;
    if (s_jpeg_copy && len <= JPEG_COPY_MAX) {
        memcpy(s_jpeg_copy, jpeg, len);
        src = s_jpeg_copy;
    }

    esp_jpeg_image_cfg_t cfg = {
        .indata = (uint8_t *)src,
        .indata_size = len,
        .outbuf = g->rgb565,
        .outbuf_size = (size_t)g->w * g->h * 2,
        .out_format = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale = JPEG_IMAGE_SCALE_1_8,
        .advanced.working_buffer = s_work,
        .advanced.working_buffer_size = WORK_BUF_SIZE,
    };
    esp_jpeg_image_output_t out;
    if (esp_jpeg_decode(&cfg, &out) != ESP_OK) {
        return -1;
    }

    size_t n = (size_t)g->w * g->h;
    for (size_t i = 0; i < n; i++) {
        /* swap_color_bytes = 0 leaves each pixel in native little-endian
         * order: low byte first */
        uint16_t p = g->rgb565[2 * i] | ((uint16_t)g->rgb565[2 * i + 1] << 8);
        uint32_t r = (p >> 11) << 3;
        uint32_t gr = ((p >> 5) & 0x3F) << 2;
        uint32_t b = (p & 0x1F) << 3;
        /* BT.601 luma, integer approximation */
        g->luma[i] = (uint8_t)((77 * r + 150 * gr + 29 * b) >> 8);
    }

    if (!g->has_ref) {
        return 1000;
    }
    size_t changed = 0;
    for (size_t i = 0; i < n; i++) {
        if (abs((int)g->luma[i] - (int)g->ref[i]) > PIXEL_THRESHOLD) {
            changed++;
        }
    }
    return (int)(changed * 1000 / n);
}

void gate_accept(gate_t *g)
{
    memcpy(g->ref, g->luma, (size_t)g->w * g->h);
    g->has_ref = true;
}

void gate_reset(gate_t *g)
{
    g->has_ref = false;
}
