#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "nvs_flash.h"
#include "driver/i2c_master.h"

#include "lcd1602.h"
#include "link_stats.h"
#include "stream.h"
#include "ui.h"
#include "wifi_sta.h"

static const char *TAG = "edge_cam";

/* LCD on its own bus; the camera's SCCB uses I2C_NUM_1 */
#define LCD_I2C_PORT I2C_NUM_0
#define LCD_I2C_SDA 14
#define LCD_I2C_SCL 21
#define LCD_I2C_ADDR 0x27

/* Freenove ESP32-S3 WROOM camera pins (same as ESP32-S3-EYE) */
static const camera_config_t cam_cfg = {
    .pin_pwdn = -1,
    .pin_reset = -1,
    .pin_xclk = 15,
    .pin_sccb_sda = 4,
    .pin_sccb_scl = 5,
    .pin_d7 = 16,
    .pin_d6 = 17,
    .pin_d5 = 18,
    .pin_d4 = 12,
    .pin_d3 = 10,
    .pin_d2 = 8,
    .pin_d1 = 9,
    .pin_d0 = 11,
    .pin_vsync = 6,
    .pin_href = 7,
    .pin_pclk = 13,
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_VGA,
    .jpeg_quality = 12,
    .fb_count = 2,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_LATEST,
};

static lcd1602_t s_lcd;

static void halt(const char *line1, const char *line2)
{
    lcd1602_write_row(&s_lcd, 0, line1);
    lcd1602_write_row(&s_lcd, 1, line2);
    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = LCD_I2C_PORT,
        .sda_io_num = LCD_I2C_SDA,
        .scl_io_num = LCD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));
    ESP_ERROR_CHECK(lcd1602_init(&s_lcd, bus, LCD_I2C_ADDR));
    lcd1602_write_row(&s_lcd, 0, "edge cam");
    lcd1602_write_row(&s_lcd, 1, "camera init");

    err = esp_camera_init(&cam_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "camera init failed: %s", esp_err_to_name(err));
        halt("camera init", "FAILED");
    }

    lcd1602_write_row(&s_lcd, 1, "wifi connecting");
    char ip[16];
    if (!wifi_sta_connect(20000, ip, sizeof(ip))) {
        ESP_LOGE(TAG, "wifi connect timed out");
        halt("wifi connect", "FAILED");
    }

    link_stats_init();
    ui_start(&s_lcd, ip);
    stream_start();
}
