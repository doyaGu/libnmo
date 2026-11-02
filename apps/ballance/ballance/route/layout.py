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


@dataclass
class Sector:
    """What one sector builds: its floors, one grid each; the pieces off the
    grid, as (function, (x, z), arguments...) for ballance.kit.pieces; and its
    rails."""
    floors: list[Floors] = field(default_factory=list)
    extras: list[tuple] = field(default_factory=list)
    rails: Rails = field(default_factory=Rails)

    def grid(self, origin: Point) -> Floors:
        """A new grid of floors, corner at origin."""
        floors = Floors(origin=origin)
        self.floors.append(floors)
        return floors


@dataclass
class Part:
    """What one piece of the route laid, or a sector's start or end: its
    step along the route, its sector, what it is, and the floors (each a
    grid's piece), pieces off the grid, cut floors, elements, modules and
    rail segments it made, and the pads and balloon (kind, place, turn)."""
    step: int
    sector: int
    what: str  # the piece's kind ("road", "rail"...), or "start" or "end"
    index: int = 0  # the piece's number in its sector, from 1
    floors: list[tuple[Floors, object]] = field(default_factory=list)
    extras: list[tuple] = field(default_factory=list)
    module_floors: list[tuple] = field(default_factory=list)
    elements: list[tuple] = field(default_factory=list)
    modules: list[tuple] = field(default_factory=list)
    segments: list[tuple[Ring, Ring]] = field(default_factory=list)
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
        part = Part(len(self.parts), sector_index, what, end=(track.p, track.h, track.d))
        self.parts.append(part)
        seen = self._claimed
        for name in ("elements", "modules", "module_floors"):
            items = getattr(self, name)
            getattr(part, name).extend(items[seen.get(name, 0):])
            seen[name] = len(items)
        key = f"extras{id(sector)}"
        part.extras.extend(sector.extras[seen.get(key, 0):])
        seen[key] = len(sector.extras)
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

    def plan(self) -> str:
        """Where each piece leaves the track, as a table: its sector and
        number, kind, point, height (of the floor's inside) and heading."""
        names = {d: name for name, d in HEADINGS.items()}
        lines = ["sector piece kind              x        z   height heading"]
        for part in self.parts:
            (x, z), h, d = part.end
            lines.append(f"{part.sector:>6} {part.index or '':>5} {part.what:<10} {x:8.2f} "
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
