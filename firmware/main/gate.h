#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Motion gate: decode the JPEG at 1/8 scale, convert to luma and compare
 * with the last frame that was sent. Only frames that changed enough are
 * worth the Wi-Fi airtime and the PC's inference time.
 */

typedef struct {
    int w, h;
    uint8_t *rgb565; /* scratch for the 1/8 decode */
    uint8_t *luma;   /* current frame */
    uint8_t *ref;    /* last sent frame */
    bool has_ref;
} gate_t;

bool gate_init(gate_t *g, int frame_w, int frame_h);
/* Returns changed pixels per mille against the reference, or -1 if decode failed.
 * 1000 is returned while there is no reference yet. */
int gate_diff(gate_t *g, const uint8_t *jpeg, size_t len);
/* Make the frame last passed to gate_diff the new reference. */
void gate_accept(gate_t *g);
void gate_reset(gate_t *g);
