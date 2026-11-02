"""A route laid out on the floors' grid: the track, sector by sector.

The Layout walks the route's pieces (ballance.route.pieces lays each one),
keeping where the track is (Track), and gathers what the level holds: each
sector's floors (one grid for each part off the others' grid), the funnels,
pads and roads off the grid, the rails, the elements and modules, the
checkpoints and the balloon, and the points along the middle of the track.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from ..core.geometry import HEADINGS, Heading, Point, across, at, back_of, turn_of
from ..game.measures import (BALLOON_BELOW_ROAD, BALLOON_REACH, CHECKPOINT_GAP,
                             CHECKPOINT_OPENING, FUNNEL, ROAD_ABOVE_MARKER, START_GAP,
                             START_OPENING)
from ..kit.floors import NARROW, RISE, Floors
from ..kit.pieces import funnel
from ..kit.rails import Rails, Ring
from .pieces import lay_piece


@dataclass
class Track:
    """Where the track is: its point and heading, the height of the floor's
    inside there (a narrow road's V bottom), the width of the road, what the
    last piece left the track on (road, wide, area, turn, trafo, rail,
    module), and the rail being laid."""
    p: Point
    d: Heading
    h: float
    width: float = NARROW
    floor: str = "road"
    run: list[Ring] | None = None
    trough: float = 0.0  # the length of the trough it is in
    railed: float = 0.0  # how long a road the railings of the rail it is on reach onto
    off_floor: bool = False  # the fall it is in is off a log's or a trough's end
    # branches to lay from the last piece: point, heading, height, grid,
    # branch, and what the branch starts from ("area" or "node")
    forks: list[tuple] = field(default_factory=list)
    step: int | None = None  # the last Part laid on it


@dataclass
class Sector:
    """What one sector builds: its floors, one grid each; the pieces off the
    grid, as (function, (x, z), arguments...) for ballance.kit.pieces; its
    rails; and its railings, each the points (x, rim, z) its top runs over
    (ballance.kit.railings)."""
    floors: list[Floors] = field(default_factory=list)
    extras: list[tuple] = field(default_factory=list)
    rails: Rails = field(default_factory=Rails)
    railings: list[list[tuple[float, float, float]]] = field(default_factory=list)

    def grid(self, origin: Point) -> Floors:
        """A new grid of floors, corner at origin."""
        floors = Floors(origin=origin)
        self.floors.append(floors)
        return floors


@dataclass
class Part:
    """What one piece of the route laid, or a sector's start or end: its
    step along the route, its sector, what it is, and the floors (each a
    grid's piece), pieces off the grid, cut floors, elements, modules, rail
    segments and railings it made, and the pads and balloon (kind, place,
    turn)."""
    step: int
    sector: int
    what: str  # the piece's kind ("road", "rail"...), or "start" or "end"
    index: int = 0  # the piece's number in its sector, from 1
    branch: int = 0  # in a branch off piece index, its number there, from 1
    after: int | None = None  # the step of the part it goes on from
    joins: int | None = None  # the step of the area a branch comes back to
    floors: list[tuple[Floors, object]] = field(default_factory=list)
    extras: list[tuple] = field(default_factory=list)
    module_floors: list[tuple] = field(default_factory=list)
    elements: list[tuple] = field(default_factory=list)
    modules: list[tuple] = field(default_factory=list)
    segments: list[tuple[Ring, Ring]] = field(default_factory=list)
    railings: list[list[tuple[float, float, float]]] = field(default_factory=list)
    marks: list[tuple[str, list[float], float]] = field(default_factory=list)
    # where it left the track: point, height of the floor's inside, heading
    end: tuple[Point, float, Heading] | None = None


class Layout:
    """A route laid out."""

    def __init__(self, route: dict) -> None:
        start = route.get("start", {})
        self.start: Point = (float(start.get("x", 0.0)), float(start.get("z", 0.0)))
        self.heading: Heading = HEADINGS[start.get("heading", "-x")]
        self.marker_y = -ROAD_ABOVE_MARKER  # the start's road edges are at 0
        self.sectors: list[Sector] = []
        self.housings: list[tuple[int, list[float]]] = []  # fans' (sector, position)
        self.elements: list[tuple[str, int, list[float]]] = []  # kind, sector, position
        self.modules: list[tuple[str, int, list[float], float]] = []  # and their turns
        # where the floors cut for a module kind stand: kind, sector, place, turn
        self.module_floors: list[tuple[str, int, list[float], float]] = []
        self.pillars: list[list[float]] = []
        self.checkpoints: list[tuple[list[float], Heading]] = []
        self.balloon: tuple[list[float], Heading] | None = None
        self.trafos: set[str] = set()
        self.last_ball = "Wood"  # the ball the track hands the player last
        self.points: list[list[float]] = [list(self.start)]
        self.warnings: list[str] = []
        self.floors: Floors | None = None  # the grid the track is on
        self.parts: list[Part] = []  # what each piece laid, in turn
        self._claimed: dict[str, int] = {}
        # named areas, for branches to come back to: grid, height, step, the
        # sides no track leaves by (each its middle and the way out), and the
        # index of its first point
        self.nodes: dict[str, tuple[Floors, float, int, list[tuple[Point, Heading]], int]] = {}
        self._waiting: dict[str, list[tuple]] = {}  # branches to areas not yet laid
        self._named: dict[str, tuple] = {}  # areas named by the piece being laid
        self._lay(route["sectors"])

    def _lay(self, sectors: list[list[dict]]) -> None:
        d = self.heading
        marker = self.start
        edge = self.marker_y + ROAD_ABOVE_MARKER
        for index, pieces in enumerate(sectors, start=1):
            opening, gap = (START_OPENING, START_GAP) if index == 1 else (CHECKPOINT_OPENING,
                                                                          CHECKPOINT_GAP)
            p = at(marker, d, opening + FUNNEL)
            sector = Sector()
            self.sectors.append(sector)
            self.floors = sector.grid(p)
            sector.extras.append((funnel, at(marker, d, opening), d, edge, gap))
            self.open(self.floors, p, d)
            self.points.append(list(p))
            track = Track(p, d, edge - RISE)
            start = self._claim(index, "start", sector, track)
            if index == 1:
                start.marks.append(("Floor_Start", [marker[0], self.marker_y, marker[1]],
                                    turn_of(d)))
            for number, piece in enumerate(pieces, start=1):
                lay_piece(self, index, sector, piece, track)
                what = piece.get("module", next(iter(piece)))
                self._claim(index, what, sector, track).index = number
                self._branch(index, sector, track, number)
            if self._waiting:
                names = ", ".join(sorted(self._waiting))
                raise ValueError(f"sector {index} has branches to no area of its own named {names}")
            if track.width != NARROW or track.run is not None:
                raise ValueError(f"sector {index} must end on a narrow road")
            p, d = track.p, track.d
            edge = track.h + RISE
            self.open(self.floors, p, d)
            last = index == len(sectors)
            # the last road flattens under the balloon's planks, its end walled
            sector.extras.append((funnel, at(p, d, FUNNEL), back_of(d), edge,
                                  None if last else CHECKPOINT_GAP, last))
            end = self._claim(index, "end", sector, track)
            if last:
                where = at(p, d, FUNNEL + BALLOON_REACH)
                self.balloon = ([where[0], edge - BALLOON_BELOW_ROAD, where[1]], d)
                self.points.append(list(where))
                end.marks.append(("PE_Balloon", self.balloon[0], turn_of(d) + 180))
            else:
                marker = at(p, d, FUNNEL + CHECKPOINT_OPENING)
                self.checkpoints.append(([marker[0], edge - ROAD_ABOVE_MARKER, marker[1]], d))
                self.points.append(list(marker))
                end.marks.append(("Floor_Checkpoint", self.checkpoints[-1][0], turn_of(d)))

    def _claim(self, sector_index: int, what: str, sector: Sector, track: Track) -> Part:
        """A Part with what was laid since the last was claimed."""
        part = Part(len(self.parts), sector_index, what, end=(track.p, track.h, track.d),
                    after=track.step)
        track.step = part.step
        self.parts.append(part)
        seen = self._claimed
        for name in ("elements", "modules", "module_floors"):
            items = getattr(self, name)
            getattr(part, name).extend(items[seen.get(name, 0):])
            seen[name] = len(items)
        for name in ("extras", "railings"):
            key = f"{name}{id(sector)}"
            items = getattr(sector, name)
            getattr(part, name).extend(items[seen.get(key, 0):])
            seen[key] = len(items)
        for grid in sector.floors:
            key = f"grid{id(grid)}"
            part.floors.extend((grid, piece) for piece in grid.pieces[seen.get(key, 0):])
            seen[key] = len(grid.pieces)
        for run in sector.rails.runs + ([track.run] if track.run else []):
            for a, b in zip(run, run[1:]):
                key = f"ring{id(b)}"
                if key not in seen:
                    part.segments.append((a, b))
                    seen[key] = 1
        return part

    def name_node(self, name: str, floors: Floors, h: float,
                  sides: list[tuple[Point, Heading]]) -> None:
        """Names the area being laid, whose free sides a branch may come
        back to."""
        if name in self.nodes or name in self._named:
            raise ValueError(f"two areas are named {name}")
        self._named[name] = (floors, h, sides, len(self.points))

    def _branch(self, sector_index: int, sector: Sector, track: Track, number: int) -> None:
        """Lays the branches the piece just laid left, and those waiting for
        an area it named."""
        for name, (floors, h, sides, first) in self._named.items():
            self.nodes[name] = (floors, h, track.step, sides, first)
        self._named.clear()
        forks, track.forks = track.forks, []
        waiting = []
        for fork in forks:
            target = fork[4].get("to")
            if target and target not in self.nodes:
                waiting.append((target, fork))
            else:
                self._lay_branch(sector_index, sector, number, fork, track.step)
        for target, fork in waiting:
            # a branch the ball takes leaves by the point the piece is left by
            self._waiting.setdefault(target, []).append(
                (sector_index, sector, number, fork, track.step, len(self.points) - 1))
        for name in [name for name in self._waiting if name in self.nodes]:
            for waiting in self._waiting.pop(name):
                self._lay_branch(*waiting)

    def _lay_branch(self, sector_index: int, sector: Sector, number: int, fork: tuple,
                    node: int, leave: int | None = None) -> None:
        """A branch off piece number (step node): its pieces from the side
        it leaves, on the area's grid.  A branch the ball goes down ("visit":
        true) and back up has its points in the route before the area's way
        out; another's are left out, and the ball it would turn the ball into.
        A branch to an area ("to": NAME) ends at the middle of one of its
        free sides, heading into it, at its height, and opens to it there.
        The ball takes such a branch ("take": true) instead of the track
        from the area it leaves (whose point there is leave) to that one: the
        branch's points stand for the track's, and the ball it ends as goes
        on (the track between changes none)."""
        p, d, h, floors, branch, floor = fork
        route, target, visit = branch.get("route"), branch.get("to"), branch.get("visit", False)
        take = branch.get("take", False)
        if not route:
            raise ValueError("a branch has a route of pieces")
        if visit and target:
            raise ValueError("the ball goes down a branch and back, or the branch comes back "
                             "to an area by itself, not both")
        if take and (not target or leave is None):
            raise ValueError("the ball takes a branch to an area laid after the one it leaves")
        main_floors, ball = self.floors, self.last_ball
        way_out = self.points.pop() if visit else None
        mark = len(self.points)
        self.floors = floors
        t = Track(p, d, h, floor=floor, step=node)
        self.points.append(list(p))
        for k, piece in enumerate(route, start=1):
            lay_piece(self, sector_index, sector, piece, t)
            what = piece.get("module", next(iter(piece)))
            part = self._claim(sector_index, what, sector, t)
            part.index, part.branch = number, k
            self._branch(sector_index, sector, t, number)
        if target:
            self._join(target, t, sector)
            self.parts[t.step].joins = self.nodes[target][2]
        elif t.run is not None or t.floor not in ("road", "wide", "area"):
            raise ValueError("a branch that comes back to no area ends on a road or an area")
        if visit:
            if t.floor == "area":
                self.points.pop()  # its far side is shut: the ball turns in its middle
            there = [point[:2] for point in self.points[mark:]]
            self.points += there[-2::-1] + [way_out]
        elif take:
            taken = self.points[mark:]
            del self.points[mark:]
            self.points[leave:self.nodes[target][4]] = taken
        else:
            del self.points[mark:]
            self.last_ball = ball
        self.floors = main_floors

    def _join(self, name: str, t: Track, sector: Sector) -> None:
        floors, h, _, sides, _ = self.nodes[name]
        for side in sides:
            q, out = side
            if abs(q[0] - t.p[0]) < 1e-6 and abs(q[1] - t.p[1]) < 1e-6 and out == back_of(t.d):
                break
        else:
            raise ValueError(f"a branch to {name} ends at ({t.p[0]:g}, {t.p[1]:g}) heading "
                             f"{t.d}, not at the middle of a free side heading into it")
        if abs(t.h - h) > 1e-6:
            raise ValueError(f"a branch to {name} comes to it {t.h - h:+g} off its height")
        sides.remove(side)
        if t.run is not None:
            self.dock(floors, q, t.d)
            sector.rails.runs.append(t.run)
            t.run = None
        else:
            self.open(floors, q, t.d, t.width)
            self.open(self.floors, q, t.d, t.width)

    def plan(self) -> str:
        """Where each piece leaves the track, as a table: its sector and
        number (N.K for a branch's piece K off piece N), kind, point, height
        (of the floor's inside) and heading."""
        names = {d: name for name, d in HEADINGS.items()}
        lines = ["sector piece kind              x        z   height heading"]
        for part in self.parts:
            (x, z), h, d = part.end
            index = f"{part.index}.{part.branch}" if part.branch else part.index or ""
            lines.append(f"{part.sector:>6} {index:>5} {part.what:<10} {x:8.2f} "
                         f"{z:8.2f} {h:8.2f} {names.get(d, '')}")
        return "\n".join(lines) + "\n"

    def warn(self, message: str) -> None:
        self.warnings.append(message)

    @staticmethod
    def open(floors: Floors, p: Point, d: Heading, width: float = NARROW) -> None:
        """Leaves a road's end at p open (a narrow one's unless width says),
        for a piece off the grid to go on from."""
        floors.open_edge(*across(p, d, width))

    @staticmethod
    def dock(floors: Floors, p: Point, d: Heading) -> None:
        """Opens the floor at p to a rail."""
        floors.dock(*across(p, d, NARROW))

    def bounds(self) -> tuple[float, float, float, float]:
        xs = [point[0] for point in self.points]
        zs = [point[1] for point in self.points]
        return min(xs), max(xs), min(zs), max(zs)
