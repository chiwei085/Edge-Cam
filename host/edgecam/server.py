"""FastAPI app: serves the dashboard and pushes board frames over WebSocket."""

import asyncio
import json
import logging
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse

from .board import run_board_link
from .detector import Detector

log = logging.getLogger(__name__)
STATIC = Path(__file__).parent / "static"


class Hub:
    """Fan out messages to every open dashboard."""

    def __init__(self) -> None:
        self.clients: set[WebSocket] = set()
        self.last_status: dict | None = None

    async def publish(self, msg: dict) -> None:
        if msg["type"] == "status":
            self.last_status = msg
        data = json.dumps(msg)
        for ws in list(self.clients):
            try:
                await ws.send_text(data)
            except Exception:
                self.clients.discard(ws)


def create_app(board_host: str, board_port: int, weights: str) -> FastAPI:
    hub = Hub()

    @asynccontextmanager
    async def lifespan(app: FastAPI):
        log.info("loading %s", weights)
        detector = await asyncio.to_thread(Detector, weights)
        task = asyncio.create_task(run_board_link(board_host, board_port, detector, hub.publish))
        yield
        task.cancel()

    app = FastAPI(lifespan=lifespan)

    @app.get("/")
    async def index() -> FileResponse:
        return FileResponse(STATIC / "index.html")

    @app.websocket("/ws")
    async def ws(websocket: WebSocket) -> None:
        await websocket.accept()
        hub.clients.add(websocket)
        if hub.last_status:
            await websocket.send_text(json.dumps(hub.last_status))
        try:
            while True:
                await websocket.receive_text()
        except WebSocketDisconnect:
            hub.clients.discard(websocket)

    return app
