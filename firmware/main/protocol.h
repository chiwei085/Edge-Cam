#pragma once

/*
 * Wire format between the board (TCP server) and the PC (client).
 * All fields are little endian; host/edgecam/protocol.py mirrors these.
 *
 * board -> PC: frame_hdr_t followed by jpeg_len bytes of JPEG
 * PC -> board: result_msg_t, one per frame, before the next frame is sent
 */

#include <stdint.h>

#define PROTO_VERSION 1

#define FRAME_FLAG_KEEPALIVE 0x01 /* sent by timer, not because of motion */

typedef struct __attribute__((packed)) {
    char magic[2]; /* "EC" */
    uint8_t version;
    uint8_t flags;
    uint32_t seq;
    uint32_t jpeg_len;
    uint16_t width;
    uint16_t height;
    uint32_t grab_us;      /* esp_camera_fb_get() */
    uint32_t gate_us;      /* 1/8 decode + frame difference */
    uint32_t prev_rtt_us;  /* send start of previous frame -> its result received */
    uint32_t frames_total; /* frames captured since the client connected */
    uint32_t frames_sent;
    uint16_t diff_permille;
    uint16_t reserved;
} frame_hdr_t;

typedef struct __attribute__((packed)) {
    char magic[2]; /* "ER" */
    uint8_t version;
    uint8_t n_objects;
    uint32_t seq;
    uint32_t pc_us;    /* frame fully received -> result sent, measured on the PC */
    uint32_t infer_us; /* model inference only */
    char line1[16];    /* LCD rows, space padded, not NUL terminated */
    char line2[16];
} result_msg_t;

_Static_assert(sizeof(frame_hdr_t) == 40, "frame_hdr_t layout");
_Static_assert(sizeof(result_msg_t) == 48, "result_msg_t layout");
