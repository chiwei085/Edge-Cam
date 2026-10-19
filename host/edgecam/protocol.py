"""Wire format shared with the board, mirrors firmware/main/protocol.h."""

import struct
from dataclasses import dataclass

PROTO_VERSION = 1
FRAME_FLAG_KEEPALIVE = 0x01

FRAME_HDR = struct.Struct("<2sBBIIHHIIIIIHH")
RESULT = struct.Struct("<2sBBIII16s16s")
assert FRAME_HDR.size == 40
assert RESULT.size == 48


@dataclass
class FrameHeader:
    flags: int
    seq: int
    jpeg_len: int
    width: int
    height: int
    grab_us: int
    gate_us: int
    prev_rtt_us: int
    frames_total: int
    frames_sent: int
    diff_permille: int

    @classmethod
    def unpack(cls, data: bytes) -> "FrameHeader":
        (magic, version, flags, seq, jpeg_len, width, height, grab_us, gate_us,
         prev_rtt_us, frames_total, frames_sent, diff_permille, _) = FRAME_HDR.unpack(data)
        if magic != b"EC" or version != PROTO_VERSION:
            raise ValueError(f"bad frame header magic={magic!r} version={version}")
        return cls(flags, seq, jpeg_len, width, height, grab_us, gate_us,
                   prev_rtt_us, frames_total, frames_sent, diff_permille)


def lcd_row(text: str) -> bytes:
    return text.encode("ascii", "replace")[:16].ljust(16)


def pack_result(seq: int, n_objects: int, pc_us: int, infer_us: int,
                line1: str, line2: str) -> bytes:
    return RESULT.pack(b"ER", PROTO_VERSION, min(n_objects, 255), seq,
                       pc_us, infer_us, lcd_row(line1), lcd_row(line2))
