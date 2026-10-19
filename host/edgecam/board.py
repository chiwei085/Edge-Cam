"""TCP client that talks to the board: receive frame, detect, send result."""

import asyncio
import base64
import logging
import time
from collections.abc import Awaitable, Callable

from .detector import Detector, summarize
from .protocol import FRAME_FLAG_KEEPALIVE, FRAME_HDR, FrameHeader, pack_result

log = logging.getLogger(__name__)

Publish = Callable[[dict], Awaitable[None]]

# The board sends a keepalive frame every 2 s even without motion. If it
# reboots, the old TCP connection never gets a FIN or RST because we only
# read from it, so silence is the only sign that the link is dead.
READ_TIMEOUT_S = 10


async def run_board_link(host: str, port: int, detector: Detector, publish: Publish) -> None:
    """Keep a connection to the board alive and process frames forever."""
    while True:
        try:
            reader, writer = await asyncio.wait_for(asyncio.open_connection(host, port), 5)
        except (OSError, asyncio.TimeoutError) as e:
            await publish({"type": "status", "connected": False, "detail": str(e)})
            await asyncio.sleep(2)
            continue

        log.info("connected to board %s:%d", host, port)
        await publish({"type": "status", "connected": True, "detail": f"{host}:{port}"})
        try:
            await _serve(reader, writer, detector, publish)
        except (OSError, asyncio.IncompleteReadError, asyncio.TimeoutError, ValueError) as e:
            log.warning("board link lost: %s", e)
        finally:
            writer.close()
        await publish({"type": "status", "connected": False, "detail": "link lost"})
        await asyncio.sleep(1)


async def _serve(reader: asyncio.StreamReader, writer: asyncio.StreamWriter,
                 detector: Detector, publish: Publish) -> None:
    prev_pc_us = 0
    last_result_t = None
    while True:
        raw = await asyncio.wait_for(reader.readexactly(FRAME_HDR.size), READ_TIMEOUT_S)
        hdr = FrameHeader.unpack(raw)
        t_hdr = time.perf_counter()
        jpeg = await asyncio.wait_for(reader.readexactly(hdr.jpeg_len), READ_TIMEOUT_S)
        t_rx = time.perf_counter()

        det = await asyncio.to_thread(detector.detect, jpeg)

        line1 = summarize(det.boxes)
        line2 = f"{len(det.boxes)} obj {det.infer_us // 1000}ms"
        pc_us = int((time.perf_counter() - t_rx) * 1e6)
        writer.write(pack_result(hdr.seq, len(det.boxes), pc_us, det.infer_us, line1, line2))
        await writer.drain()

        now = time.perf_counter()
        result_fps = 1 / (now - last_result_t) if last_result_t else 0.0
        last_result_t = now
        # prev_rtt covers send start -> result received on the board for the
        # previous frame; take out the PC's own time and the rest is Wi-Fi.
        prev_net_us = max(hdr.prev_rtt_us - prev_pc_us, 0) if hdr.prev_rtt_us else None
        prev_pc_us = pc_us

        await publish({
            "type": "frame",
            "seq": hdr.seq,
            "width": hdr.width,
            "height": hdr.height,
            "jpeg": base64.b64encode(jpeg).decode(),
            "jpeg_bytes": hdr.jpeg_len,
            "keepalive": bool(hdr.flags & FRAME_FLAG_KEEPALIVE),
            "diff_permille": hdr.diff_permille,
            "boxes": [b.__dict__ for b in det.boxes],
            "lcd": [line1[:16], line2[:16]],
            "timing_ms": {
                "grab": hdr.grab_us / 1000,
                "gate": hdr.gate_us / 1000,
                "payload_rx": (t_rx - t_hdr) * 1000,
                "infer": det.infer_us / 1000,
                "pre_post": det.pre_post_us / 1000,
                "pc_total": pc_us / 1000,
            },
            "prev_ms": {
                "rtt": hdr.prev_rtt_us / 1000 if hdr.prev_rtt_us else None,
                "network": prev_net_us / 1000 if prev_net_us is not None else None,
            },
            "stats": {
                "frames_total": hdr.frames_total,
                "frames_sent": hdr.frames_sent,
                "result_fps": result_fps,
            },
        })
