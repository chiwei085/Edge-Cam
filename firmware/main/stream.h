#pragma once

/* Start the task that captures and gates frames continuously and, while a
 * PC client is connected, streams the gated frames to it. */
void stream_start(void);
