"""A built level played through in the game: the level patched into a
Level_01.NMO, the game run headless (`nmo behavior run`, physics by ivp)
with the autopilot steering the ball along the builder's points, and what
came of it read back: when the checkpoints were reached, the level
finished or the ball fell off, and where the ball stood still.

The game starts level 1 as soon as its main menu takes Enter, and Q skips
the tutorial; the ball is steered from frame STEER_FROM.
"""

from __future__ import annotations

import json
import math
import re
import subprocess
from dataclasses import dataclass, field
from pathlib import Path

from ..core.nmo import Nmo

KEYS = "Enter@700-705,Enter@725-730,Q@860-1000"
STEER_FROM = 1000
BALLS = "Ball_Wood,Ball_Stone,Ball_Paper"
REGISTRY = r"Software\Ballance\Settings\Language=1"
STILL = 0.02       # the ball moves less than this in a frame: it stands still
STALL = 60         # frames standing still worth telling of
EVENTS = ("Checkpoint reached", "Level_Finish", "Ball Off")
SENT = re.compile(r'^\[(\d+)\]\s+Send Message#\d+ sends "([^"]+)" to All_Gameplay')


@dataclass
class Stall:
    start: int
    end: int
    where: tuple[float, float, float]
    point: int  # the steer point the ball was heading for
    waits: str  # what that point waits for, if anything

    def __str__(self) -> str:
        x, y, z = self.where
        why = f", waiting {self.waits}" if self.waits else ""
        return (f"still {self.end - self.start} frames from {self.start} at ({x:.1f}, {y:.1f}, "
                f"{z:.1f}), heading for point {self.point}{why}")


@dataclass
class Play:
    events: list[tuple[int, str]] = field(default_factory=list)
    stalls: list[Stall] = field(default_factory=list)
    frames: int = 0

    @property
    def finished(self) -> int | None:
        return next((frame for frame, what in self.events if what == "Level_Finish"), None)

    @property
    def falls(self) -> list[int]:
        finish = self.finished
        return [frame for frame, what in self.events
                if what == "Ball Off" and (finish is None or frame < finish)]


def steer_text(points: list[list]) -> str:
    """The builder's points as `--steer` takes them."""
    def value(v) -> str:
        return "" if v is None else v if isinstance(v, str) else f"{v:g}"
    return ";".join(",".join(value(v) for v in p) for p in points)


def _waits(point: list) -> str:
    if len(point) > 4:
        return f"for {point[4]} to come by"
    if len(point) > 3 and point[3] == "rush":
        return ""
    if len(point) > 3 and point[3]:
        return f"{point[3]} frames"
    if len(point) > 2 and point[2] is not None:
        return f"to be lifted to {point[2]:g}"
    return ""


def play(folder: Path, frames: int, nmo: Nmo, game: Path) -> Play:
    """Plays the level built in folder for frames, writing Level_01.NMO,
    run.log and steer.csv there."""
    folder = folder.resolve()
    nmo.run("patch", "apply", "--project", str(folder / "manifest.json"),
            "-o", str(folder / "Level_01.NMO"))
    points = json.loads((folder / "route_points.json").read_text(encoding="utf-8"))
    with open(folder / "run.log", "wb") as log:
        subprocess.run([nmo.executable, "behavior", "run", "--physics", "ivp",
                        "--path", str(folder), "-n", str(frames), "--keys", KEYS,
                        "--steer", f"{steer_text(points)}@{STEER_FROM}",
                        "--steer-object", BALLS, "--steer-log", str(folder / "steer.csv"),
                        "--registry", REGISTRY, "base.cmo"],
                       cwd=game, stdout=log, stderr=subprocess.STDOUT, check=False)
    result = Play(frames=frames)
    with open(folder / "run.log", encoding="utf-8", errors="replace") as log:
        for line in log:
            sent = SENT.match(line)
            if sent and sent.group(2) in EVENTS:
                result.events.append((int(sent.group(1)), sent.group(2)))
    still_from, last = None, None
    for line in (folder / "steer.csv").read_text(encoding="utf-8").splitlines():
        if not line[:1].isdigit():
            continue
        values = line.split(",")
        frame, where, point = int(values[0]), tuple(float(v) for v in values[1:4]), int(values[5])
        if last is not None and math.dist(where, last[1]) < STILL:
            still_from = still_from or last
        else:
            if still_from and last[0] - still_from[0] >= STALL:
                index = still_from[2]
                result.stalls.append(Stall(still_from[0], last[0], still_from[1], index,
                                           _waits(points[index]) if index < len(points) else ""))
            still_from = None
        last = (frame, where, point)
    if still_from and last[0] - still_from[0] >= STALL:
        result.stalls.append(Stall(still_from[0], last[0], still_from[1], still_from[2], ""))
    return result
