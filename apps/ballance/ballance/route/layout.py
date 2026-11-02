"""A route laid out on the floors' grid: the track, sector by sector.

The Layout walks the route's pieces (ballance.route.pieces lays each one),
keeping where the track is (Track), and gathers what the level holds: each
sector's floors (one grid for each part off the others' grid), the funnels,
pads and roads off the grid, the rails, the elements and modules, the
checkpoints and the balloon, and the points along the middle of the track.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from ..core.geometry import HEADINGS, Heading, Point, across, at, back_of
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
        self.lows: list[float] = []  # each sector's lowest floor
        self.warnings: list[str] = []
        self.floors: Floors | None = None  # the grid the track is on
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
            for piece in pieces:
                lay_piece(self, index, sector, piece, track)
            if track.width != NARROW or track.run is not None:
                raise ValueError(f"sector {index} must end on a narrow road")
            p, d = track.p, track.d
            edge = track.h + RISE
            self.open(self.floors, p, d)
            last = index == len(sectors)
            # the last road flattens under the balloon's planks, its end walled
            sector.extras.append((funnel, at(p, d, FUNNEL), back_of(d), edge,
                                  None if last else CHECKPOINT_GAP, last))
            if last:
                where = at(p, d, FUNNEL + BALLOON_REACH)
                self.balloon = ([where[0], edge - BALLOON_BELOW_ROAD, where[1]], d)
                self.points.append(list(where))
            else:
                marker = at(p, d, FUNNEL + CHECKPOINT_OPENING)
                self.checkpoints.append(([marker[0], edge - ROAD_ABOVE_MARKER, marker[1]], d))
                self.points.append(list(marker))
            self.lows.append(min(y for floors in sector.floors for piece in floors.pieces
                                 for y in (piece.y0, piece.y1)))

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
