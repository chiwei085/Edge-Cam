#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Connect to the configured access point. Blocks up to timeout_ms and
 * writes the dotted IPv4 address into ip on success. */
bool wifi_sta_connect(int timeout_ms, char *ip, size_t ip_len);
