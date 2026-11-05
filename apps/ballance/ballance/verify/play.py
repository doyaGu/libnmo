"""A built level played through in the game: the level patched into a
Level_01.NMO, the game run headless (`nmo behavior run`, physics by ivp)
with the autopilot steering the ball along the builder's points, and what
came of it read back: when the checkpoints were reached, the level
finished or the ball fell off, where the ball stood still, and the rewards
(extra points and lives) it never came near enough to take.

The level is played in the harness (ballance.verify.harness) rather than
the game's base.cmo: it goes straight into the level, with no tutorial, and
the ball can be steered by frame STEER_FROM.  A level can also be watched
in a window (watch), the autopilot playing it or a player.
"""

from __future__ import annotations

import csv
import json
import math
import re
import shutil
import subprocess
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

from ..core.nmo import Nmo
from ..core.project import pack
from ..core import evidence
from ..route.motions import template_paths
from . import harness
from .resources import dependency_paths, check as resource_check

STEER_FROM = 360   # past the camera's flight in (the ball takes no keys before about 280)
FRAME_MS = "16.667"  # the time a frame steps, as headless runs step it
BALLS = "Ball_Wood,Ball_Stone,Ball_Paper"
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
    rewards: list[dict] = field(default_factory=list)
    trace: list[tuple[int, tuple[float, float, float]]] = field(default_factory=list)

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
    if len(point) > 3 and point[3] in ("rush", "precision"):
        return ""
    if len(point) > 3 and str(point[3]).startswith("push:"):
        return f"rocked {point[3][5:]} frames, or lifted to {point[2]:g}"
    if len(point) > 3 and point[3]:
        return f"{point[3]} frames"
    if len(point) > 2 and point[2] is not None:
        return f"to be lifted to {point[2]:g}"
    return ""


def sector_start(folder: Path, sector: int) -> str:
    """The ball the player has at sector's start, from the builder's
    route_sectors.json in folder."""
    sectors = json.loads((folder / "route_sectors.json").read_text(encoding="utf-8"))
    if not 1 <= sector <= len(sectors):
        raise ValueError(f"the level has sectors 1 to {len(sectors)}, not {sector}")
    return sectors[sector - 1]["ball"]


def steer_from(sector: int) -> int:
    """The frame the autopilot starts at: past the harness's setup of sector."""
    return STEER_FROM + math.ceil(harness.setup_ms(sector) / float(FRAME_MS))


def base_for(folder: Path, nmo: Nmo, sector: int) -> Path:
    """The harness that starts the level built in folder at sector."""
    return harness.ensure(nmo, sector, sector_start(folder, sector) if sector > 1 else "Wood")


def watch(level: Path, route: Path | None, nmo: Nmo, game: Path, mute: bool = False,
          sector: int = 1, *, handoff_frame: int | None = None, approach: bool = False,
          shot: Path | None = None, shot_frame: int = 4500) -> int:
    """Plays level (a Level_NN.NMO) as level 1, from sector, in a window until
    it is closed: with route (a builder's route_points.json) the autopilot
    rolls the ball, a fixed time a frame as the verifier's runs step, so that
    it plays as they did; without, the arrow keys do, in real time."""
    if sector < 1:
        raise ValueError('sector must be at least 1')
    if handoff_frame is not None and (route is None or handoff_frame <= steer_from(sector)):
        raise ValueError('handoff frame must follow autopilot start and needs a route')
    if approach and handoff_frame is None:
        raise ValueError('--approach needs --handoff-frame to return control to the player')
    if shot is not None and shot_frame < 1:
        raise ValueError('shot frame must be positive')
    level, game = level.resolve(), game.resolve()
    if resource_check(level.parent,game)["status"] == "failed":
        raise ValueError("Native resources changed; refresh and rebuild before preview")
    base = base_for(level.parent, nmo, sector) if sector > 1 else harness.ensure(nmo)
    provenance = None
    if shot is not None:
        provenance = evidence.capture(
            level.parent if (level.parent / 'manifest.json').is_file() else level,
            'native-preview', {'sector': sector, 'handoff_frame': handoff_frame,
                               'approach': approach, 'shot_frame': shot_frame,
                               'frame_ms': FRAME_MS if route else None, 'game': str(game)},
            [p for p in (level, route, Path(nmo.executable), base) if p and p.is_file()] +
            [p for p in dependency_paths(level.parent,game) if p.is_file()])
        shot.unlink(missing_ok=True)
        Path(str(shot) + '.evidence.json').unlink(missing_ok=True)
    with tempfile.TemporaryDirectory() as folder:
        shutil.copyfile(level, Path(folder) / "Level_01.NMO")
        from ..route.dynamics import copy_runtime
        copy_runtime(level.parent, folder)
        command = [nmo.executable, "behavior", "run", "--window", "--physics", "ivp",
                   "--write", folder, "--path", str(game)]
        if route is not None:
            points = json.loads(route.read_text(encoding="utf-8"))
            if approach:
                points = points[:-1]
            if not points:
                raise ValueError('the preview route has no points')
            end = f'-{handoff_frame}' if handoff_frame is not None else ''
            command += ["--delta", FRAME_MS,
                        "--steer", f"{steer_text(points)}@{steer_from(sector)}{end}",
                        "--steer-object", BALLS]
        if shot is not None:
            command += ['--shot', f'{shot.resolve()}@{shot_frame}']
        if mute:
            command.append("--mute")
        status = subprocess.run([*command, str(base)], cwd=game, stdout=subprocess.DEVNULL,
                                check=False).returncode
        if shot is not None and shot.is_file():
            evidence.attach(shot, provenance)
        return status


def play(folder: Path, frames: int, nmo: Nmo, game: Path, sector: int = 1) -> Play:
    """Plays the level built in folder for frames, from sector, writing
    Level_01.NMO, run.log and steer.csv there.  The rewards of the sectors
    before are not looked for."""
    if frames <= 0 or sector < 1:
        raise ValueError('frames and sector must be positive')
    folder, game = folder.resolve(), game.resolve()
    if resource_check(folder,game)["status"] == "failed":
        raise ValueError("Native resources changed; refresh and rebuild before gameplay")
    pack(folder, nmo)
    points = json.loads((folder / "route_points.json").read_text(encoding="utf-8"))
    base = base_for(folder, nmo, sector)
    provenance = evidence.capture(folder, 'ivp-play',
        {'frames': frames, 'sector': sector, 'frame_ms': FRAME_MS,
         'window': False, 'game': str(game)},
        [p for p in (Path(nmo.executable), base) if p.is_file()] +
        [p for p in [*template_paths(game), *dependency_paths(folder,game)] if p.is_file()])
    # A failed invocation must never reuse a previous successful trace.
    (folder / 'steer.csv').unlink(missing_ok=True)
    (folder / 'steer.csv.evidence.json').unlink(missing_ok=True)
    (folder / 'physics.csv').unlink(missing_ok=True)
    (folder / 'physics.csv.evidence.json').unlink(missing_ok=True)
    with open(folder / "run.log", "wb") as log:
        process = subprocess.run([nmo.executable, "behavior", "run", "--physics", "ivp",
                        "--write", str(folder), "--path", str(game), "-n", str(frames),
                        "--steer", f"{steer_text(points)}@{steer_from(sector)}",
                        "--steer-object", BALLS, "--steer-log", str(folder / "steer.csv"),
                        "--physics-log", str(folder / 'physics.csv'),
                        str(base)],
                       cwd=game, stdout=log, stderr=subprocess.STDOUT, check=False)
    evidence.attach(folder / 'run.log', provenance)
    if (folder / 'steer.csv').is_file():
        evidence.attach(folder / 'steer.csv', provenance)
    if (folder / 'physics.csv').is_file():
        evidence.attach(folder / 'physics.csv', provenance)
    if process.returncode:
        raise RuntimeError(f'gameplay exited with status {process.returncode}; see {folder / "run.log"}')
    return read_run(folder, frames, sector)


def read_run(folder: Path, frames: int, sector: int = 1) -> Play:
    """Summarize one completed invocation's log and trace, up to its finish."""
    points = json.loads((folder / 'route_points.json').read_text(encoding='utf-8'))
    result = Play(frames=frames)
    with open(folder / "run.log", encoding="utf-8", errors="replace") as log:
        for line in log:
            sent = SENT.match(line)
            if sent and sent.group(2) in EVENTS:
                result.events.append((int(sent.group(1)), sent.group(2)))
    still_from, last = None, None
    trace_path = folder / 'steer.csv'
    if not trace_path.is_file():
        raise RuntimeError(f'gameplay produced no trace: {trace_path}')
    with trace_path.open(encoding='utf-8', newline='') as stream:
        reader = csv.DictReader(stream)
        if not {'frame', 'x', 'y', 'z', 'next'} <= set(reader.fieldnames or []):
            raise ValueError(f'invalid gameplay trace header: {trace_path}')
        rows = list(reader)
    for row in rows:
        frame = int(row['frame'])
        if result.finished is not None and frame > result.finished:
            break
        where, point = tuple(float(row[k]) for k in ('x', 'y', 'z')), int(row['next'])
        if not all(math.isfinite(v) for v in where):
            raise ValueError(f'non-finite ball position at frame {frame}')
        result.trace.append((frame, where))
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
        index = still_from[2]
        result.stalls.append(Stall(still_from[0], last[0], still_from[1], index,
                                  _waits(points[index]) if index < len(points) else ''))
    if not result.trace:
        raise RuntimeError(f'gameplay produced an empty trace: {trace_path}')
    manifest = json.loads((folder / "manifest.json").read_text(encoding="utf-8"))
    skipped = {f"Sector_{n:02d}" for n in range(1, sector)}
    for obj in manifest["scenes"][0]["objects"]:
        if obj["name"].startswith(REWARDS) and not skipped & set(obj.get("groups", [])):
            at = obj["transform"]["position"]
            near = min(math.dist(at, where) for _, where in result.trace)
            result.rewards.append({'name': obj['name'], 'where': at, 'nearest': near,
                                   'reached': near <= REACH})
            if near > REACH:
                result.missed.append((obj["name"], at, near))
    return result
