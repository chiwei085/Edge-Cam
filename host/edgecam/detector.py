"""Object detection backend. Swap this class to change the model or runtime."""

from collections import Counter
from dataclasses import dataclass

import cv2
import numpy as np
from ultralytics import YOLO


@dataclass
class Box:
    x1: float
    y1: float
    x2: float
    y2: float
    label: str
    conf: float


@dataclass
class Detection:
    boxes: list[Box]
    infer_us: int  # model forward pass only
    pre_post_us: int  # preprocess + postprocess inside the model wrapper


class Detector:
    def __init__(self, weights: str = "yolo11n.pt", conf: float = 0.35):
        self.model = YOLO(weights)
        self.conf = conf
        # First call pays for lazy init; keep it out of the measurements
        self.model(np.zeros((480, 640, 3), np.uint8), verbose=False)

    def detect(self, jpeg: bytes) -> Detection:
        img = cv2.imdecode(np.frombuffer(jpeg, np.uint8), cv2.IMREAD_COLOR)
        if img is None:
            raise ValueError("JPEG decode failed")
        result = self.model(img, conf=self.conf, verbose=False)[0]
        names = result.names
        boxes = [
            Box(*xyxy, label=names[int(cls)], conf=float(conf))
            for xyxy, cls, conf in zip(result.boxes.xyxy.tolist(),
                                       result.boxes.cls.tolist(),
                                       result.boxes.conf.tolist())
        ]
        speed = result.speed  # milliseconds
        return Detection(
            boxes=boxes,
            infer_us=int(speed["inference"] * 1000),
            pre_post_us=int((speed["preprocess"] + speed["postprocess"]) * 1000),
        )


def summarize(boxes: list[Box]) -> str:
    """Compact class count for a 16 character LCD row, most frequent first."""
    if not boxes:
        return "no object"
    counts = Counter(b.label for b in boxes).most_common()
    return " ".join(f"{label}{n}" if n > 1 else label for label, n in counts)
