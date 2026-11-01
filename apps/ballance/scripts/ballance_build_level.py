#!/usr/bin/env python3
"""Build a Ballance level from a route, as a libnmo project manifest.

A route is the track as the ball rolls it, sector by sector, in JSON:

    {
      "start": {"x": 0, "z": 0, "heading": "-x"},
      "sectors": [
        [{"road": 10},
         {"area": [25, 25], "exit": "left",
          "elements": [{"kind": "P_Extra_Point", "at": [12.5, 0]}]},
         {"road": 30, "width": "wide", "drop": 5},
         {"turn": "right"},
         {"trafo": "stone"},
         {"road": 10}],
        [...]
      ]
    }

The start's marker stands at (x, z), the edges of the road that leaves it at
height 0, and the track leaves toward heading (+x, -x, +z or -z).  Each
sector is a list of pieces, laid end to end on the floors' 2.5 grid:

- {"road": length, "width": "narrow" or "wide", "drop": dy} -- a straight
  road (narrow unless given), coming down dy over its length.
- {"area": [along, across], "exit": "straight", "left" or "right"} -- a
  square or oblong area entered at the middle of one side and left at the
  middle of the opposite side, or of the side to the left or right.  A
  pillar stands under each corner.
- {"turn": "left" or "right"} -- a square corner as wide as the road.
- {"trafo": "paper", "stone" or "wood"} -- a ball changer on its pad, on a
  narrow road.  The original levels all turn the ball back to wood before
  the balloon; the builder warns when a route does not.
- {"module": "P_Modul_25"} -- the drawbridge: an upright plank the ball
  pushes over a gap 10 long, between flat blocks a narrow road runs into
  through funnels, as Level_03's are.  The original levels bring a wood or
  stone ball to it.
- {"rail": length, "drop": dy} -- a straight rail, coming down dy.
- {"rail_turn": "left" or "right"} -- the originals' rail bend, with a
  straight 1.9 before and after it so that it ends 10 along and 10 aside.
  Rails run from a narrow road or an area to a narrow road or an area
  (ballance_rails.py).

An area may hold a fan, {"kind": "P_Modul_18", "at": [along, right]} among
its elements, on the corner of four of its flat inside cells, and then
"rise": dy: the fan lifts a paper ball onto the next piece, dy higher (at
most 16), which starts at the area's exit as Level_03's do.

Every piece may carry "elements": [{"kind": "P_Box", "at": [along, right]}],
placed from where the piece starts, along the track and to its right, and
resting on the floor there as the originals' do (see ELEMENT_LIFT).  Each
sector but the last ends in a checkpoint and the last in the balloon; both
are reached from a narrow road, and every sector starts with one.

The level is built as the original levels are (docs/ballance-level-format.md):
the floors with ballance_floors.py, the start, checkpoints and balloon turned
with the track, their lantern towers and the balloon's striped poles beside
it, the fall zone hidden under all of it.  With --elements, it uses the
meshes apps/ballance/scripts/ballance_level_elements.py exported from a Ballance
installation; without, boxes stand in for the markers, elements and the
start's and checkpoints' floors, and there is no scenery and no sky.

The output directory gets manifest.json, the OBJ meshes it names, and
route_points.json, the points along the middle of the track from the start to
the balloon, for driving a ball along it; a point over a fan has a third
value, the height to wait there for.  Turn it into a level file with:

    nmo patch apply --project <out>/manifest.json -o <out>/Level_01.NMO
"""

from __future__ import annotations

import argparse
import json
import math
import shutil
import sys
from pathlib import Path

from ballance_floors import CELL, NARROW, RISE, WALL, Floors, Mesh, funnel, pad
from ballance_rails import ABOVE_V, BEND_RADIUS, Rails, Ring, bend

PRESETS = Path(__file__).resolve().parent.parent / "data" / "level_materials.json"

# The groups the game reads by name.  Levelinit iterates the element groups
# (P_*) and the depth test groups by name too; a missing one costs a single
# load-time error, as in the original levels, so only the used ones are made.
LEVEL_GROUPS = [
    "PS_Levelstart",
    "PE_Levelende",
    "PC_Checkpoints",
    "PR_Resetpoints",
    "Phys_Floors",
    "Phys_FloorRails",
    "Phys_FloorStopper",
    "Shadow",
    "DepthTestCubes",
    "Sound_RollID_01",
    "Sound_HitID_01",
    "Sound_RollID_02",
    "Sound_HitID_02",
    "Sound_RollID_03",
    "Sound_HitID_03",
]

# A stone floor: physics, stone rolling and hit sounds, the ball's shadow.
STONE_FLOOR_GROUPS = ["Phys_Floors", "Sound_RollID_01", "Sound_HitID_01", "Shadow"]
# Rails: physics and the metal sounds, no shadow, as the originals' have.
RAIL_GROUPS = ["Phys_FloorRails", "Sound_RollID_03", "Sound_HitID_03"]

# The kinds whose meshes --elements supplies (<Kind>.obj).
MARKERS = ["PS_FourFlames", "PC_TwoFlames", "PR_Resetpoint", "PE_Balloon"]
ELEMENTS = ["P_Extra_Life", "P_Extra_Point", "P_Trafo_Paper", "P_Trafo_Stone",
            "P_Trafo_Wood", "P_Ball_Paper", "P_Ball_Stone", "P_Ball_Wood", "P_Box", "P_Dome"]
# The kinds that only --elements supplies, with nothing standing in for them.
GAME_ONLY = ["SkyLayer", "Floor_Start", "Floor_Checkpoint", "Floor_Modul_18", "Pillar", "Tower",
             "Stick"]
# The materials of the scenery.
SCENERY_MATERIALS = [
    "Colums_blue", "Colums_beige", "Columns_beige_fade", "Tower_Floor_Side",
    "Tower_Floor_Top_Flat_illu", "TowerRail", "TowerRoof", "TowerRoof_Brim", "Laterne_Glas",
    "Laterne_Metal", "Laterne_Schatten", "Laterne_Verlauf", "Stick_Bottom", "Stick_Green",
    "Stick_Rail", "Stick_Red", "Stick_Stripes"]

# How far above the floor under it each element stands, as in the originals:
# their meshes rest on it.  A ball changer stands 0.1 above its pad.
ELEMENT_LIFT = {"P_Extra_Point": 2.5, "P_Extra_Life": 2.56, "P_Box": 2.5,
                "P_Ball_Paper": 2.0, "P_Ball_Stone": 2.0, "P_Ball_Wood": 2.0, "P_Dome": 0.0,
                "P_Modul_18": -0.35}
TRAFO_LIFT = 0.1
# A fan (P_Modul_18) stands on the grille of its housing, 0.35 below the
# floor around it; its wind reaches 21.4 up, so it lifts a paper ball onto
# floors up to 16 higher (Level_03's lift it 13.5).
FAN_BELOW = 0.35
FAN_REACH = 16.0
# The modules that stand on the track: (the floors' length along it, how far
# the module stands from where they start).  P_Modul_25's floors run along its
# -X: a flat block 5 long, the gap 10 long its plank falls across, another
# block 5 long holding the stopper the plank lands on, all at a road's edge
# height; funnels lead a narrow road onto and off them.
MODULES = {"P_Modul_25": (20.0, 5.0)}
# The floors cut out for each module (ballance_level_elements.py), with their
# groups, and the stand-in boxes (x0, x1, y0, y1, z0, z1 in the module's frame)
# written without them.
MODULE_FLOORS = {"P_Modul_25": [
    ("Floor_Modul_25", STONE_FLOOR_GROUPS, [(-15, -10, -5, 0, -2.5, 2.5), (0, 5, -5, 0, -2.5, 2.5)]),
    ("Floor_Stopper_25", ["Phys_FloorStopper"], [(-10, -9, -1, -0.5, -2.5, 2.5)])]}

# The start and the checkpoints are 1.26 below the edges of the roads that
# meet their floors.  A narrow road meets those floors through a funnel 5
# long, from the gap in their rims; at the end, a funnel flattens it under the
# balloon's planks.  PR_01 is 3.65 above the start and the next reset point
# 3.33 above its checkpoint.
ROAD_ABOVE_MARKER = 1.26
FUNNEL = 5.0
START_OPENING, START_GAP = 7.39, 1.30
CHECKPOINT_OPENING, CHECKPOINT_GAP = 4.92, 0.87
FIRST_RESET_ABOVE, RESET_ABOVE = 3.65, 3.33
# The balloon's planks reach 22.08 along its +X, back to the road's end, which
# is 0.01 higher than the balloon.
BALLOON_REACH = 22.08
BALLOON_BELOW_ROAD = 0.01
WIDTHS = {"narrow": NARROW, "wide": 2 * NARROW}
# A rail bend, with its straights, ends 10 along and 10 aside: on the grid.
BEND_SPAN = 4 * CELL

# The scenery, placed as the original levels place it (ballance_level_elements
# exports one of each: Pillar, the Tower on top of one, Stick).  A pillar
# under a floor stands 2.5 in from its corner, its top 5 below the floor's
# edge, where the floor's walls end.  Lantern towers stand in pairs beside
# the track: (along the track, to its right, top of the pillar above the
# marker).  The start's are Level_01's, moved back beside the road ahead of
# it; the checkpoint's are the originals' usual pair 20 ahead.  Around the
# balloon, Level_01's towers and striped poles, along the balloon's +X (back
# toward the road).  A tower that would stand on a floor is left out.
PILLAR_INSET = 2.5
START_TOWERS = [(17.4, 13.0, -2.42), (17.4, -13.0, -2.42)]
CHECKPOINT_TOWERS = [(20.0, 12.5, -4.92), (20.0, -12.5, -4.92)]
BALLOON_TOWERS = [(-50.0, 32.0, -10.0), (-50.0, -32.0, -10.0),
                  (36.0, 26.0, -14.1), (36.0, -26.0, -14.1)]
BALLOON_STICKS = [(-56.0, 45.5, 3.7), (-56.0, -45.5, 3.7), (3.0, 30.5, 3.7),
                  (3.0, -30.5, 3.7), (52.0, 15.5, 3.7), (52.0, -15.5, 3.7)]
TOWER_HALF = 2.5

# The fall zone reaches this far beyond the floors, from 25 to 60 below them.
FALL_MARGIN = 60.0
FALL_TOP, FALL_BOTTOM = 25.0, 60.0
SKY_BELOW = 180.0

HEADINGS = {"+x": (1, 0), "-x": (-1, 0), "+z": (0, 1), "-z": (0, -1)}


def right_of(d: tuple[int, int]) -> tuple[int, int]:
    """The direction to the right of heading d, looking along it with Y up
    in Virtools' left-handed space (to the right of -X is +Z)."""
    return d[1], -d[0]


def turn_of(d: tuple[int, int]) -> float:
    """The turn about Y, in degrees, that points an object's X axis along d."""
    return math.degrees(math.atan2(-d[1], d[0])) % 360


def at(p: tuple[float, float], d: tuple[int, int], along: float,
       right: float = 0.0) -> tuple[float, float]:
    r = right_of(d)
    return p[0] + d[0] * along + r[0] * right, p[1] + d[1] * along + r[1] * right


class Builder:
    """Lays a route out: its floors, by sector, and where everything stands."""

    def __init__(self, route: dict) -> None:
        start = route.get("start", {})
        self.start = (float(start.get("x", 0.0)), float(start.get("z", 0.0)))
        self.heading = HEADINGS[start.get("heading", "-x")]
        self.marker_y = -ROAD_ABOVE_MARKER  # the start's road edges are at 0
        # per sector, its floors: one grid for each height a fan lifts the
        # ball to
        self.sectors: list[list[Floors]] = []
        self.housings: list[tuple[int, list[float]]] = []  # fans' (sector, position)
        self.rails: list[Rails] = []
        self.extras: list[list] = []  # per sector: (function, args) writing funnels and pads
        self.elements: list[tuple[str, int, list[float]]] = []
        self.modules: list[tuple[str, int, list[float], float]] = []  # and their turns
        self.towers: list[list[float]] = []
        self.pillars: list[list[float]] = []
        self.sticks: list[list[float]] = []
        self.checkpoints: list[tuple[list[float], tuple[int, int]]] = []
        self.balloon: tuple[list[float], tuple[int, int]] | None = None
        self.trafos: set[str] = set()
        self.last_ball = "Wood"  # the ball the track hands the player last
        self.points: list[list[float]] = [list(self.start)]
        self.lows: list[float] = []
        self._lay(route["sectors"])

    def _lay(self, sectors: list[list[dict]]) -> None:
        d = self.heading
        marker = self.start
        edge = self.marker_y + ROAD_ABOVE_MARKER
        for index, pieces in enumerate(sectors, start=1):
            opening, gap = (START_OPENING, START_GAP) if index == 1 else (CHECKPOINT_OPENING,
                                                                          CHECKPOINT_GAP)
            p = at(marker, d, opening + FUNNEL)
            self.floors = Floors(origin=p)
            extras = [(funnel, at(marker, d, opening), d, edge, gap)]
            self.sectors.append([self.floors])
            self.rails.append(Rails())
            self.extras.append(extras)
            self._open(self.floors, p, d)
            self.points.append(list(p))
            h = edge - RISE
            width = NARROW
            self.floor = "road"  # what the last piece left the track on
            self.run: list[Ring] | None = None
            for piece in pieces:
                p, d, h, width = self._piece(index, extras, piece, p, d, h, width)
            if width != NARROW or self.run is not None:
                raise ValueError(f"sector {index} must end on a narrow road")
            edge = h + RISE
            self._open(self.floors, p, d)
            back = (-d[0], -d[1])
            last = index == len(sectors)
            extras.append((funnel, at(p, d, FUNNEL), back, edge, None if last else CHECKPOINT_GAP))
            if last:
                where = at(p, d, FUNNEL + BALLOON_REACH)
                self.balloon = ([where[0], edge - BALLOON_BELOW_ROAD, where[1]], d)
                self.points.append(list(where))
            else:
                marker = at(p, d, FUNNEL + CHECKPOINT_OPENING)
                self.checkpoints.append(([marker[0], edge - ROAD_ABOVE_MARKER, marker[1]], d))
                self.points.append(list(marker))
            self.lows.append(min(y for floors in self.sectors[-1]
                                 for y in self._floor_heights(floors)))

    def _open(self, floors: Floors, p: tuple[float, float], d: tuple[int, int]) -> None:
        a, b = at(p, d, 0, -NARROW / 2), at(p, d, 0, NARROW / 2)
        floors.open_edge(a[0], a[1], b[0], b[1])

    def _rect(self, p: tuple[float, float], d: tuple[int, int], length: float,
              width: float) -> tuple[float, float, float, float]:
        a = at(p, d, 0, -width / 2)
        b = at(p, d, length, width / 2)
        return a[0], a[1], b[0], b[1]

    def _dock(self, floors: Floors, p: tuple[float, float], d: tuple[int, int]) -> None:
        a, b = at(p, d, 0, -NARROW / 2), at(p, d, 0, NARROW / 2)
        floors.dock(a[0], a[1], b[0], b[1])

    def _piece(self, sector: int, extras: list, piece: dict,
               p: tuple[float, float], d: tuple[int, int], h: float, width: float):
        floors = self.floors
        start, heading = p, d
        rail = "rail" in piece or "rail_turn" in piece
        if rail and self.run is None:
            # Rails meet floors as the originals' do: at a narrow road's end
            # or through a gap in an area's rim.
            if self.floor not in ("road", "area"):
                raise ValueError("a rail starts from a narrow road or an area")
            self._dock(floors, p, d)
            self.run = [Ring(p[0], h + ABOVE_V, p[1], d[0], d[1])]
        elif not rail and self.run is not None:
            if not ("area" in piece or ("road" in piece and
                                        piece.get("width", "narrow") == "narrow")):
                raise ValueError("a rail ends at a narrow road or an area")
            self._dock(floors, p, d)
            self.rails[sector - 1].runs.append(self.run)
            self.run = None
        if rail and piece.get("elements"):
            raise ValueError("elements stand on floors, not on rails")
        if "road" in piece:
            length = float(piece["road"])
            width = WIDTHS[piece.get("width", "narrow")]
            end = h - float(piece.get("drop", 0.0))
            floors.road(*self._rect(p, d, length, width), h, end,
                        along="x" if d[0] else "z")
            ramp = (h, end, length)
            p, h = at(p, d, length), end
            self.points.append(list(p))
            self.floor = "road" if width == NARROW else "wide"
        elif "rail" in piece:
            p, h = at(p, d, float(piece["rail"])), h - float(piece.get("drop", 0.0))
            self.run.append(Ring(p[0], h + ABOVE_V, p[1], d[0], d[1]))
            self.points.append(list(p))
            self.floor = "rail"
        elif "rail_turn" in piece:
            sign = 1 if piece["rail_turn"] == "right" else -1
            side = (right_of(d)[0] * sign, right_of(d)[1] * sign)
            a = at(p, d, BEND_SPAN - BEND_RADIUS)
            rings = bend(a[0], h + ABOVE_V, a[1], d, side)
            p = at(p, d, BEND_SPAN, sign * BEND_SPAN)
            d = side
            self.run += rings + [Ring(p[0], h + ABOVE_V, p[1], d[0], d[1])]
            self.points += [[r.x, r.z] for r in rings[3::3]] + [list(p)]
            self.floor = "rail"
        elif "area" in piece:
            along, across = (float(v) for v in piece["area"])
            floors.area(*self._rect(p, d, along, across), h)
            ramp = (h, h, along)
            for a in (PILLAR_INSET, along - PILLAR_INSET):
                for c in (-across / 2 + PILLAR_INSET, across / 2 - PILLAR_INSET):
                    x, z = at(p, d, a, c)
                    self.pillars.append([x, h + RISE - WALL, z])
            exit = piece.get("exit", "straight")
            middle = at(p, d, along / 2)
            if exit == "straight":
                p = at(p, d, along)
            else:
                d = right_of(d) if exit == "right" else (-right_of(d)[0], -right_of(d)[1])
                p = at(middle, d, across / 2)
            rise = float(piece.get("rise", 0.0))
            fans = [e for e in piece.get("elements", []) if e["kind"] == "P_Modul_18"]
            if rise:
                # The fan lifts the ball onto floors beside the area, rise
                # higher, as Level_03's do; they are floors of their own.
                if not fans:
                    raise ValueError("an area rises only with a fan (P_Modul_18) in it")
                if not 0 < rise <= FAN_REACH:
                    raise ValueError(f"a fan lifts the ball at most {FAN_REACH}")
                if self.last_ball != "Paper":
                    print("warning: only a paper ball rides a fan's wind; the ball reaches "
                          f"it as {self.last_ball.lower()}", file=sys.stderr)
                fan = at(start, heading, *(float(v) for v in fans[0]["at"]))
                # wait over the fan until the ball is above the floors ahead
                self.points.append([fan[0], fan[1], h + rise + RISE + 1.0])
                h += rise
                self.floors = Floors(origin=p)
                self.sectors[-1].append(self.floors)
            else:
                self.points.append(list(middle))
            self.points.append(list(p))
            self.floor = "area"
        elif "turn" in piece:
            floors.road(*self._rect(p, d, width, width), h, h, along="x" if d[0] else "z")
            ramp = (h, h, width)
            corner = at(p, d, width / 2)
            self.points.append(list(corner))
            side = right_of(d) if piece["turn"] == "right" else (-right_of(d)[0],
                                                                  -right_of(d)[1])
            d = side
            p = at(corner, d, width / 2)
            self.points.append(list(p))
            self.floor = "turn"
        elif "trafo" in piece:
            if width != NARROW:
                raise ValueError("a ball changer stands on a narrow road")
            kind = piece["trafo"].capitalize()
            edge = h + RISE
            self._open(floors, p, d)
            back = (-d[0], -d[1])
            middle = at(p, d, FUNNEL + NARROW / 2)
            extras.append((funnel, at(p, d, FUNNEL), back, edge, None))
            extras.append((pad, middle, edge, f"Floor_Trafo_{kind}"))
            extras.append((funnel, at(p, d, FUNNEL + NARROW), d, edge, None))
            self.elements.append((f"P_Trafo_{kind}", sector,
                                  [middle[0], edge + TRAFO_LIFT, middle[1]]))
            self.trafos.add(kind)
            self.last_ball = kind
            ramp = (h, h, 2 * FUNNEL + NARROW)
            p = at(p, d, 2 * FUNNEL + NARROW)
            self._open(floors, p, d)
            self.points.append(list(middle))
            self.points.append(list(p))
            self.floor = "trafo"
        elif "module" in piece:
            kind = piece["module"]
            if kind not in MODULES:
                raise ValueError(f"unknown module {kind}; known: {', '.join(MODULES)}")
            if width != NARROW or self.floor != "road":
                raise ValueError("a module stands on a narrow road")
            length, offset = MODULES[kind]
            edge = h + RISE
            self._open(floors, p, d)
            back = (-d[0], -d[1])
            extras.append((funnel, at(p, d, FUNNEL), back, edge, None))
            extras.append((funnel, at(p, d, FUNNEL + length), d, edge, None))
            where = at(p, d, FUNNEL + offset)
            # the ball rolls along the module's -X
            self.modules.append((kind, sector, [where[0], edge, where[1]], turn_of(back)))
            ramp = (h, h, 2 * FUNNEL + length)
            self.points += [list(where), list(at(p, d, FUNNEL + length))]
            p = at(p, d, 2 * FUNNEL + length)
            self._open(floors, p, d)
            self.points.append(list(p))
            self.floor = "module"
        else:
            raise ValueError(f"unknown piece {piece}")
        for element in piece.get("elements", []):
            a, c = (float(v) for v in element["at"])
            y0, y1, length = ramp
            x, z = at(start, heading, a, c)
            floor = y0 + (y1 - y0) * min(max(a / length, 0.0), 1.0)
            kind = element["kind"]
            if kind == "P_Modul_18":
                # A fan stands in a housing set into an area's flat inside,
                # two cells square, whose rim is flush with the floor.
                if "area" not in piece:
                    raise ValueError("a fan stands in an area")
                floors.hole(x - NARROW / 2, z - NARROW / 2, x + NARROW / 2, z + NARROW / 2)
                self.housings.append((sector, [x, floor - FAN_BELOW, z]))
            self.elements.append((kind, sector, [x, floor + ELEMENT_LIFT[kind], z]))
        return p, d, h, width

    @staticmethod
    def _floor_heights(floors: Floors) -> list[float]:
        return [p.y0 for p in floors.pieces] + [p.y1 for p in floors.pieces]

    def _on_floor(self, x: float, z: float, half: float) -> bool:
        """Whether a square of half width half at (x, z) lies over a floor
        cell, a pad or a start or checkpoint floor."""
        for floors in (f for sector in self.sectors for f in sector):
            for i, j in floors.cells:
                cx, cz = floors.ox + (i + 0.5) * CELL, floors.oz + (j + 0.5) * CELL
                if abs(cx - x) < half + CELL / 2 and abs(cz - z) < half + CELL / 2:
                    return True
        for marker in [(self.start[0], 0.0, self.start[1])] + [m for m, _ in self.checkpoints]:
            if math.hypot(marker[0] - x, marker[2] - z) < START_OPENING + half + 2:
                return True
        return False

    def scenery(self) -> None:
        """Stands the lantern towers and the balloon's poles."""
        spots = [(self.start, self.marker_y, self.heading, START_TOWERS, 1)]
        for marker, d in self.checkpoints:
            spots.append(((marker[0], marker[2]), marker[1], d, CHECKPOINT_TOWERS, 1))
        if self.balloon is not None:
            where, d = self.balloon
            spots.append(((where[0], where[2]), where[1], d, BALLOON_TOWERS, -1))
        for (px, pz), y, d, towers, sign in spots:
            for a, c, up in towers:
                x, z = at((px, pz), d, sign * a, c)
                if not self._on_floor(x, z, TOWER_HALF):
                    self.towers.append([x, y + up, z])
        if self.balloon is not None:
            where, d = self.balloon
            for a, c, up in BALLOON_STICKS:
                x, z = at((where[0], where[2]), d, -a, c)
                if not self._on_floor(x, z, 1.2):
                    self.sticks.append([x, where[1] + up, z])

    def write(self, out: Path, pads: bool) -> None:
        """The floors of each sector, with their funnels and pads, as
        sector<N>.obj, and its rails as rails<N>.obj; without pads, boxes
        as high as the floors' rims stand in for the start's and checkpoints'
        floors and the fans' housings (start_pad.obj, checkpoint_pad.obj,
        fan_pad.obj)."""
        for index, (sector, extras) in enumerate(zip(self.sectors, self.extras), start=1):
            mesh = Mesh()
            for floors in sector:
                floors.build(mesh)
            for function, *args in extras:
                if function is funnel:
                    (x, z), toward, edge, gap = args
                    funnel(mesh, x, z, toward, edge, gap)
                else:
                    (x, z), top, material = args
                    pad(mesh, x, z, top, material)
            mesh.write(out / f"sector{index}.obj")
            if self.rails[index - 1].runs:
                self.rails[index - 1].write(out / f"rails{index}.obj")
        if not pads:
            for name, half in (("start_pad", START_OPENING), ("checkpoint_pad", CHECKPOINT_OPENING)):
                write_box(out / f"{name}.obj", -half, half, -WALL, ROAD_ABOVE_MARKER, -half, half)
            # a fan's housing: its rim flush with the floor, 3 deep
            half = NARROW / 2
            write_box(out / "fan_pad.obj", -half, half, FAN_BELOW - 3.4, FAN_BELOW, -half, half)

    def bounds(self) -> tuple[float, float, float, float]:
        xs = [point[0] for point in self.points]
        zs = [point[1] for point in self.points]
        return min(xs), max(xs), min(zs), max(zs)


def level_object(name: str, mesh: str, position: list[float],
                 groups: list[str], visible: bool = True, turn: float = 0.0) -> dict:
    """turn: degrees about Y; 90 turns an object's X axis to -Z (CK's left-handed Y)."""
    obj = {
        "name": name,
        "class": "CK3dObject",
        "mesh": {"obj": mesh},
        "transform": {"position": position},
        "groups": groups,
    }
    if turn % 360:
        obj["transform"]["rotation_euler_deg"] = [0, turn % 360, 0]
    if not visible:
        obj["visible"] = False
    return obj


def write_box(path: Path, x0: float, x1: float, y0: float, y1: float,
              z0: float, z1: float) -> None:
    write_boxes(path, [(x0, x1, y0, y1, z0, z1)])


def write_boxes(path: Path, boxes: list) -> None:
    faces = [(3, 4, 8, 7), (1, 5, 6, 2), (1, 2, 4, 3), (5, 7, 8, 6), (1, 3, 7, 5), (2, 6, 8, 4)]
    lines = []
    for k, (x0, x1, y0, y1, z0, z1) in enumerate(boxes):
        corners = [(x, y, z) for x in (x0, x1) for y in (y0, y1) for z in (z0, z1)]
        lines += [f"v {x:g} {y:g} {z:g}" for x, y, z in corners]
        lines += ["f " + " ".join(str(8 * k + i) for i in face) for face in faces]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def build_manifest(presets: dict, meshes: dict[str, str], level: Builder, name: str) -> dict:
    # The turns are Level_03's, whose road runs along +X, turned with the
    # track: the start's floor opens along it and the start is turned 90
    # degrees from it; a reset point's -X looks along the track; the
    # checkpoints and their floors turn with it, and the balloon's planks
    # point back along it.
    heading = turn_of(level.heading)
    sx, sz = level.start
    objects = [
        level_object("PS_FourFlames_01", meshes["PS_FourFlames"], [sx, level.marker_y, sz],
                     ["PS_Levelstart"], turn=heading - 90),
        level_object("PR_Resetpoint_01", meshes["PR_Resetpoint"],
                     [sx, level.marker_y + FIRST_RESET_ABOVE, sz], ["PR_Resetpoints"],
                     turn=heading + 180),
        # Touching this box is what makes the ball fall off ("Ball Off"); the
        # original fall zones are hidden.
        level_object("Quader01", "depth.obj", [0, 0, 0], ["DepthTestCubes"], visible=False),
    ]
    for index in range(1, len(level.sectors) + 1):
        objects.append(level_object(f"A{index:02d}_Floor_02", f"sector{index}.obj", [0, 0, 0],
                                    STONE_FLOOR_GROUPS))
        if level.rails[index - 1].runs:
            objects.append(level_object(f"A{index:02d}_Rail_01", f"rails{index}.obj", [0, 0, 0],
                                        RAIL_GROUPS))
    for index, (sector, position) in enumerate(level.housings, start=1):
        objects.append(level_object(f"A{sector:02d}_Floor_Modul_18_{index:02d}",
                                    meshes.get("Floor_Modul_18", "fan_pad.obj"), position,
                                    STONE_FLOOR_GROUPS))
    for index, (marker, d) in enumerate(level.checkpoints, start=1):
        objects.append(level_object(f"PC_TwoFlames_{index:02d}", meshes["PC_TwoFlames"], marker,
                                    ["PC_Checkpoints"], turn=turn_of(d)))
        objects.append(level_object(f"PR_Resetpoint_{index + 1:02d}", meshes["PR_Resetpoint"],
                                    [marker[0], marker[1] + RESET_ABOVE, marker[2]],
                                    ["PR_Resetpoints"], turn=turn_of(d) + 180))
    where, d = level.balloon
    objects.append(level_object("PE_Balloon_01", meshes["PE_Balloon"], where, ["PE_Levelende"],
                                turn=turn_of(d) + 180))
    counts: dict[str, int] = {}
    for kind, sector, position, turn in [e + (0.0,) for e in level.elements] + level.modules:
        counts[kind] = counts.get(kind, 0) + 1
        objects.append(level_object(f"{kind}_{counts[kind]:02d}", meshes[kind], position,
                                    [kind, f"Sector_{sector:02d}"], turn=turn))
    for kind, sector, position, turn in level.modules:
        for floor, groups, _ in MODULE_FLOORS[kind]:
            counts[floor] = counts.get(floor, 0) + 1
            objects.append(level_object(f"A{sector:02d}_{floor}_{counts[floor]:02d}",
                                        meshes[floor], position, groups, turn=turn))
    used = {"Floor_Top_Profile", "Floor_Top_ProfileFlat", "Floor_Top_Flat",
            "Floor_Top_Boderless", "Floor_Side"}
    used.update(f"Floor_Trafo_{kind}" for kind in level.trafos)
    if level.modules:
        used.add("Floor_Stopper_Wood")
    if any(rails.runs for rails in level.rails):
        used.add("Rail")
    if level.housings and "Floor_Modul_18" in meshes:
        used.update(("Modul18_Top", "Modul18_Gitter"))
    pads = [("A01_Floor_01", (sx, level.marker_y, sz), heading, "Floor_Start", "start_pad.obj")]
    for index, (marker, d) in enumerate(level.checkpoints, start=2):
        pads.append((f"A{index:02d}_Floor_01", marker, turn_of(d), "Floor_Checkpoint",
                     "checkpoint_pad.obj"))
    for name_, position, turn, kind, box in pads:
        game = kind in meshes
        objects.append(level_object(name_, meshes[kind] if game else box, list(position),
                                    STONE_FLOOR_GROUPS, turn=turn if game else 0))
    if "Floor_Start" in meshes:
        used.add("Floor_Top_Checkpoint")
    if "Pillar" in meshes:
        # Lantern towers are floors the ball can hit, as in the originals;
        # pillars and poles are only seen.
        for index, position in enumerate(level.towers, start=1):
            objects.append(level_object(f"Tower_{index:02d}", meshes["Tower"], position,
                                        ["Phys_Floors"]))
        for index, position in enumerate(level.towers + level.pillars, start=1):
            objects.append(level_object(f"Floor_Columns_{index:02d}", meshes["Pillar"],
                                        position, []))
        for index, position in enumerate(level.sticks, start=1):
            objects.append(level_object(f"Stick_{index:02d}", meshes["Stick"], position, []))
        used.update(SCENERY_MATERIALS)
    if "SkyLayer" in meshes:
        # The cloud plane under the level, as far below as the originals'.
        x0, x1, z0, z1 = level.bounds()
        objects.append(level_object("SkyLayer", meshes["SkyLayer"],
                                    [(x0 + x1) / 2, -SKY_BELOW, (z0 + z1) / 2], []))
        used.add("SkyLayer")
    return {
        "version": 1,
        "document": {"name": name},
        "materials": [m for m in presets["materials"] if m["name"] in used],
        "groups": [{"name": group} for group in LEVEL_GROUPS],
        "scenes": [{"name": name, "objects": objects}],
    }


def build(route: dict, out: Path, elements: str | None, name: str) -> int:
    out.mkdir(parents=True, exist_ok=True)
    level = Builder(route)
    if level.last_ball != "Wood":
        # Every original level turns the ball back to wood before the
        # balloon; none lets a paper or stone ball reach its bridge.
        print(f"warning: the ball reaches the balloon as {level.last_ball.lower()}; "
              "the original levels always turn it back to wood first", file=sys.stderr)
    used_kinds = sorted({kind for kind, _, _ in level.elements} |
                        {kind for kind, _, _, _ in level.modules})
    floors = [floor for kind in sorted({m[0] for m in level.modules})
              for floor, _, _ in MODULE_FLOORS[kind]]
    meshes = {kind: "marker.obj" for kind in MARKERS + used_kinds}
    for kind in sorted({m[0] for m in level.modules}):
        for floor, _, boxes in MODULE_FLOORS[kind]:
            meshes[floor] = f"{floor}.obj"
            write_boxes(out / f"{floor}.obj", boxes)
    if elements:
        for kind in MARKERS + used_kinds + GAME_ONLY + floors:
            for suffix in (".obj", ".mtl"):
                source = Path(elements) / f"{kind}{suffix}"
                if suffix == ".obj" and not source.is_file():
                    print(f"no {source}", file=sys.stderr)
                    return 2
                if source.is_file():
                    shutil.copyfile(source, out / source.name)
            meshes[kind] = f"{kind}.obj"
        level.scenery()
    level.write(out, "Floor_Start" in meshes)
    write_box(out / "marker.obj", -0.5, 0.5, 0, 1, -0.5, 0.5)
    x0, x1, z0, z1 = level.bounds()
    low = min(level.lows)
    write_box(out / "depth.obj", x0 - FALL_MARGIN, x1 + FALL_MARGIN, low - FALL_BOTTOM,
              low - FALL_TOP, z0 - FALL_MARGIN, z1 + FALL_MARGIN)
    presets = json.loads(PRESETS.read_text(encoding="utf-8"))
    (out / "manifest.json").write_text(
        json.dumps(build_manifest(presets, meshes, level, name), indent=2) + "\n",
        encoding="utf-8")
    (out / "route_points.json").write_text(json.dumps(level.points) + "\n", encoding="utf-8")
    print(out / "manifest.json")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build a Ballance level from a route, as a project manifest.")
    parser.add_argument("route", help="The route, as JSON")
    parser.add_argument("out", help="Directory for manifest.json and the OBJ meshes")
    parser.add_argument("--elements",
                        help="Folder of <Kind>.obj meshes from ballance_level_elements.py")
    parser.add_argument("--name", default="Level", help="The document's name")
    args = parser.parse_args()
    route = json.loads(Path(args.route).read_text(encoding="utf-8"))
    return build(route, Path(args.out), args.elements, args.name)


if __name__ == "__main__":
    sys.exit(main())
