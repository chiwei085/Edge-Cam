#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

/*
 * HD44780 16x2 character LCD behind a PCF8574 I2C GPIO expander.
 * The controller runs in 4-bit mode: each byte goes out as two nibbles,
 * and every nibble is latched by pulsing EN through the expander.
 */

#define LCD1602_COLS 16
#define LCD1602_ROWS 2

typedef struct {
    i2c_master_dev_handle_t dev;
    uint8_t backlight;
} lcd1602_t;

esp_err_t lcd1602_init(lcd1602_t *lcd, i2c_master_bus_handle_t bus, uint8_t addr);
esp_err_t lcd1602_clear(lcd1602_t *lcd);
esp_err_t lcd1602_set_cursor(lcd1602_t *lcd, uint8_t col, uint8_t row);
esp_err_t lcd1602_print(lcd1602_t *lcd, const char *s);
/* Overwrite a whole row, padding with spaces so stale characters disappear. */
esp_err_t lcd1602_write_row(lcd1602_t *lcd, uint8_t row, const char *s);
