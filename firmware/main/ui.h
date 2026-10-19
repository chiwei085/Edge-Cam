#pragma once

#include "lcd1602.h"

/* Start the low priority task that renders link_stats onto the LCD:
 * a device health panel while a PC is connected, a local motion
 * monitor while offline. */
void ui_start(lcd1602_t *lcd, const char *ip);
