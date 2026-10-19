import argparse
import logging

import uvicorn

from .server import create_app


def main() -> None:
    parser = argparse.ArgumentParser(description="Edge camera host")
    parser.add_argument("--board", required=True, help="board IP shown on the LCD")
    parser.add_argument("--board-port", type=int, default=5000)
    parser.add_argument("--weights", default="yolo11n.pt")
    parser.add_argument("--port", type=int, default=8000, help="dashboard port")
    args = parser.parse_args()

    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)s %(message)s")
    app = create_app(args.board, args.board_port, args.weights)
    # Dashboard is for this PC only
    uvicorn.run(app, host="127.0.0.1", port=args.port, log_level="warning")


if __name__ == "__main__":
    main()
