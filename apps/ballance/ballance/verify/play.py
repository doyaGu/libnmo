"""A built level played through in the game: the level patched into a
Level_01.NMO, the game run headless (`nmo behavior run`, physics by ivp)
with the autopilot steering the ball along the builder's points, and what
came of it read back: when the checkpoints were reached, the level
finished or the ball fell off, where the ball stood still, and the rewards
(extra points and lives) it never came near enough to take.

The game starts level 1 as soon as its main menu takes Enter, and Q skips
the tutorial; the ball is steered from frame STEER_FROM.  A level can also
be watched in a window (watch), the autopilot playing it or a player.
"""

from __future__ import annotations

import json
import math
import re
import shutil
import subprocess
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

from ..core.nmo import Nmo

KEYS = "Enter@700-705,Enter@725-730,Q@860-1000"
STEER_FROM = 1000
FRAME_MS = "16.667"  # the time a frame steps, as headless runs step it
BALLS = "Ball_Wood,Ball_Stone,Ball_Paper"
REGISTRY = r"Software\Ballance\Settings\Language=1"
STILL = 0.02       # the ball moves less than this in a frame: it stands still
STALL = 60         # frames standing still worth telling of
REACH = 4.0        # the ball takes a reward its middle comes this near
REWARDS = ("P_Extra_Point", "P_Extra_Life")
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
    # rewards the ball never came within REACH of before the finish: name,
    # where it is, and how near the ball came
    missed: list[tuple[str, list[float], float]] = field(default_factory=list)

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


def watch(level: Path, route: Path | None, nmo: Nmo, game: Path, mute: bool = False) -> int:
    """Plays level (a Level_NN.NMO) as level 1 in a window until it is
    closed: with route (a builder's route_points.json) the autopilot rolls
    the ball, a fixed time a frame as the verifier's runs step, so that it
    plays as they did; without, the arrow keys do, in real time."""
    with tempfile.TemporaryDirectory() as folder:
        shutil.copyfile(level, Path(folder) / "Level_01.NMO")
        command = [nmo.executable, "behavior", "run", "--window", "--physics", "ivp",
                   "--path", folder, "--keys", KEYS, "--registry", REGISTRY]
        if route is not None:
            points = json.loads(route.read_text(encoding="utf-8"))
            command += ["--delta", FRAME_MS, "--steer", f"{steer_text(points)}@{STEER_FROM}",
                        "--steer-object", BALLS]
        if mute:
            command.append("--mute")
        return subprocess.run([*command, "base.cmo"], cwd=game, stdout=subprocess.DEVNULL,
                              check=False).returncode


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
    trace = []
    for line in (folder / "steer.csv").read_text(encoding="utf-8").splitlines():
        if not line[:1].isdigit():
            continue
        values = line.split(",")
        frame, where, point = int(values[0]), tuple(float(v) for v in values[1:4]), int(values[5])
        if result.finished is None or frame <= result.finished:
            trace.append(where)
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
    manifest = json.loads((folder / "manifest.json").read_text(encoding="utf-8"))
    for obj in manifest["scenes"][0]["objects"]:
        if obj["name"].startswith(REWARDS):
            at = obj["transform"]["position"]
            near = min((math.dist(at, where) for where in trace), default=math.inf)
            if near > REACH:
                result.missed.append((obj["name"], at, near))
    return result
