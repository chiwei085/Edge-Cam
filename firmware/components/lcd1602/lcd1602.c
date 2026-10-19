#include "lcd1602.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"

/* PCF8574 pin mapping on the common LCD1602 backpack */
#define PIN_RS 0x01
#define PIN_RW 0x02
#define PIN_EN 0x04
#define PIN_BL 0x08 /* backlight transistor */
/* P4..P7 drive D4..D7 */

#define CMD_CLEAR        0x01
#define CMD_ENTRY_MODE   0x04
#define CMD_DISPLAY_CTRL 0x08
#define CMD_FUNCTION_SET 0x20
#define CMD_SET_DDRAM    0x80

#define ENTRY_INCREMENT  0x02
#define DISPLAY_ON       0x04
#define FUNCTION_2LINE   0x08

#define I2C_TIMEOUT_MS 50

/* Send one nibble (upper 4 bits of `nibble`) with an EN high-low pulse.
 * Both expander writes go in one I2C transaction; at 100 kHz each byte
 * takes ~90 us, far above the 450 ns minimum EN pulse width. */
static esp_err_t write_nibble(lcd1602_t *lcd, uint8_t nibble, uint8_t rs)
{
    uint8_t base = (nibble & 0xF0) | lcd->backlight | rs;
    uint8_t seq[2] = { base | PIN_EN, base };
    return i2c_master_transmit(lcd->dev, seq, sizeof(seq), I2C_TIMEOUT_MS);
}

static esp_err_t write_byte(lcd1602_t *lcd, uint8_t value, uint8_t rs)
{
    esp_err_t err = write_nibble(lcd, value & 0xF0, rs);
    if (err != ESP_OK) {
        return err;
    }
    err = write_nibble(lcd, (uint8_t)(value << 4), rs);
    /* Most instructions need 37 us to execute */
    esp_rom_delay_us(50);
    return err;
}

static esp_err_t command(lcd1602_t *lcd, uint8_t cmd)
{
    return write_byte(lcd, cmd, 0);
}

esp_err_t lcd1602_init(lcd1602_t *lcd, i2c_master_bus_handle_t bus, uint8_t addr)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &lcd->dev);
    if (err != ESP_OK) {
        return err;
    }
    lcd->backlight = PIN_BL;

    /* Power-on wait: VCC must be above 2.7 V for more than 40 ms */
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Reset by instruction (HD44780 datasheet, figure 24). The controller
     * may be in 8-bit mode or halfway through a 4-bit transfer, so send
     * "8-bit function set" three times before switching to 4-bit. */
    err = write_nibble(lcd, 0x30, 0);
    esp_rom_delay_us(4500);
    err |= write_nibble(lcd, 0x30, 0);
    esp_rom_delay_us(150);
    err |= write_nibble(lcd, 0x30, 0);
    esp_rom_delay_us(150);
    err |= write_nibble(lcd, 0x20, 0);
    esp_rom_delay_us(150);
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    err = command(lcd, CMD_FUNCTION_SET | FUNCTION_2LINE);
    err |= command(lcd, CMD_DISPLAY_CTRL);
    err |= lcd1602_clear(lcd);
    err |= command(lcd, CMD_ENTRY_MODE | ENTRY_INCREMENT);
    err |= command(lcd, CMD_DISPLAY_CTRL | DISPLAY_ON);
    return err == ESP_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t lcd1602_clear(lcd1602_t *lcd)
{
    esp_err_t err = command(lcd, CMD_CLEAR);
    /* Clear takes 1.52 ms */
    esp_rom_delay_us(2000);
    return err;
}

esp_err_t lcd1602_set_cursor(lcd1602_t *lcd, uint8_t col, uint8_t row)
{
    static const uint8_t row_offset[LCD1602_ROWS] = { 0x00, 0x40 };
    if (col >= LCD1602_COLS || row >= LCD1602_ROWS) {
        return ESP_ERR_INVALID_ARG;
    }
    return command(lcd, CMD_SET_DDRAM | (row_offset[row] + col));
}

esp_err_t lcd1602_print(lcd1602_t *lcd, const char *s)
{
    for (; *s; s++) {
        esp_err_t err = write_byte(lcd, (uint8_t)*s, PIN_RS);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t lcd1602_write_row(lcd1602_t *lcd, uint8_t row, const char *s)
{
    char buf[LCD1602_COLS + 1];
    size_t n = strnlen(s, LCD1602_COLS);
    memcpy(buf, s, n);
    memset(buf + n, ' ', LCD1602_COLS - n);
    buf[LCD1602_COLS] = '\0';

    esp_err_t err = lcd1602_set_cursor(lcd, 0, row);
    if (err != ESP_OK) {
        return err;
    }
    return lcd1602_print(lcd, buf);
}
