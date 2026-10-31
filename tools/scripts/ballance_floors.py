"""Ballance floors built the way the original levels build them.

The original floors lie on a grid of 2.5-unit cells.  A floor's top is flat
inside and rises 0.7 to every edge of its outline over one cell, so the same
rule makes the narrow road (two cells wide: a V), the wide road (four cells:
two slopes and a flat middle), the square areas with their raised borders,
and the corners of turns.  Where a road meets an area, the area's edge stays
raised except for a gap two cells wide in front of the road's middle, so a
wide road narrows into the area through an X-shaped piece.  Walls hang 5
below the outline; floors have no bottom.

The materials are the levels' own (tools/ballance/level_materials.json):
Floor_Top_Boderless where a cell is flat, Floor_Top_Profile on the slopes of
roads, Floor_Top_Flat on the borders of areas, Floor_Top_ProfileFlat on the
corners and where roads open into areas, Floor_Side on the walls.  Textures
repeat every 5 units; across a slope they run from the brick band at the edge
to the fold in the middle (Profile) or across the whole border (Flat).

Floors.area() and Floors.road() add rectangles of cells; Floors.build()
writes the floor's triangles to a Mesh.  funnel() writes the piece that
flattens a narrow road into the rim of a start or checkpoint floor, or ends
it under the balloon's planks.
"""

from __future__ import annotations

from pathlib import Path

CELL = 2.5
RISE = 0.7
WALL = 5.0
REPEAT = 5.0
NARROW = 2 * CELL

UP = (0.0, 1.0, 0.0)


class Mesh:
    """An OBJ mesh of triangles, each wound to face a given way."""

    def __init__(self) -> None:
        self.lines: list[str] = []
        self.count = 0

    def triangle(self, material: str, facing: tuple[float, float, float],
                 *corners: tuple[tuple[float, float, float], tuple[float, float]]) -> None:
        (a, _), (b, _), (c, _) = corners
        ab = [b[i] - a[i] for i in range(3)]
        ac = [c[i] - a[i] for i in range(3)]
        normal = (ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2],
                  ab[0] * ac[1] - ab[1] * ac[0])
        if sum(normal[i] * facing[i] for i in range(3)) < 0:
            corners = (corners[0], corners[2], corners[1])
        self.lines.append(f"usemtl {material}")
        for (x, y, z), (u, v) in corners:
            self.lines.append(f"v {x:g} {y:g} {z:g}")
            self.lines.append(f"vt {u:g} {v:g}")
        refs = [self.count + i for i in (1, 2, 3)]
        self.lines.append("f " + " ".join(f"{r}/{r}" for r in refs))
        self.count += 3

    def quad(self, material: str, facing: tuple[float, float, float], *corners) -> None:
        self.triangle(material, facing, corners[0], corners[1], corners[2])
        self.triangle(material, facing, corners[0], corners[2], corners[3])

    def write(self, path: Path) -> None:
        path.write_text("\n".join(self.lines) + "\n", encoding="utf-8")


class Piece:
    def __init__(self, kind: str, rect: tuple[int, int, int, int], y0: float, y1: float,
                 axis: int, reverse: bool) -> None:
        self.kind = kind
        self.rect = rect  # i0, j0, i1, j1: cells i0 <= i < i1, j0 <= j < j1
        self.y0, self.y1 = y0, y1
        self.axis = axis  # 0: the road runs along X, 1: along Z
        self.reverse = reverse  # y0 is at the far end of the axis

    def contains_vertex(self, i: int, j: int) -> bool:
        i0, j0, i1, j1 = self.rect
        return i0 <= i <= i1 and j0 <= j <= j1

    def height(self, i: int, j: int) -> float:
        if self.y0 == self.y1:
            return self.y0
        i0, j0, i1, j1 = self.rect
        lo, hi, at = (i0, i1, i) if self.axis == 0 else (j0, j1, j)
        t = (at - lo) / (hi - lo)
        if self.reverse:
            t = 1.0 - t
        return self.y0 + (self.y1 - self.y0) * t

    def middle(self) -> float:
        """Twice the cell coordinate of the road's middle line, across it."""
        i0, j0, i1, j1 = self.rect
        return float(j0 + j1) if self.axis == 0 else float(i0 + i1)


class Floors:
    """Floors on a grid of 2.5-unit cells whose corner is at origin.  Heights
    are those of the flat insides; edges are 0.7 higher."""

    def __init__(self, origin: tuple[float, float] = (0.0, 0.0)) -> None:
        self.ox, self.oz = origin
        self.pieces: list[Piece] = []
        self.cells: dict[tuple[int, int], int] = {}
        self.open: set[tuple[int, int, int]] = set()

    def _grid(self, value: float, offset: float) -> int:
        cells = (value - offset) / CELL
        index = round(cells)
        if abs(cells - index) > 1e-6:
            raise ValueError(f"{value} is not on the 2.5-unit grid")
        return index

    def _rect(self, x0: float, z0: float, x1: float, z1: float) -> tuple[int, int, int, int]:
        i0, i1 = sorted((self._grid(x0, self.ox), self._grid(x1, self.ox)))
        j0, j1 = sorted((self._grid(z0, self.oz), self._grid(z1, self.oz)))
        if i0 == i1 or j0 == j1:
            raise ValueError("a floor needs at least one cell")
        return i0, j0, i1, j1

    def _add(self, piece: Piece) -> Piece:
        i0, j0, i1, j1 = piece.rect
        index = len(self.pieces)
        for i in range(i0, i1):
            for j in range(j0, j1):
                if (i, j) in self.cells:
                    raise ValueError(f"floors overlap at cell {(i, j)}")
                self.cells[(i, j)] = index
        self.pieces.append(piece)
        return piece

    def area(self, x0: float, z0: float, x1: float, z1: float, y: float) -> Piece:
        """A square or oblong area between the two corners, with raised
        borders, at height y."""
        return self._add(Piece("area", self._rect(x0, z0, x1, z1), y, y, 0, False))

    def road(self, x0: float, z0: float, x1: float, z1: float, y0: float,
             y1: float | None = None) -> Piece:
        """A straight road between the two corners, running along its longer
        side, from height y0 at the (x0, z0) end to y1 (default y0) at the
        other."""
        rect = self._rect(x0, z0, x1, z1)
        i0, j0, i1, j1 = rect
        axis = 0 if i1 - i0 >= j1 - j0 else 1
        reverse = (x0 > x1) if axis == 0 else (z0 > z1)
        return self._add(Piece("road", rect, y0, y0 if y1 is None else y1, axis, reverse))

    def open_edge(self, x0: float, z0: float, x1: float, z1: float) -> None:
        """Leaves the outline between the two points open, without a raised
        edge or a wall, where another piece (a funnel) goes on from it."""
        i0, i1 = sorted((self._grid(x0, self.ox), self._grid(x1, self.ox)))
        j0, j1 = sorted((self._grid(z0, self.oz), self._grid(z1, self.oz)))
        if i0 == i1:
            self.open.update((0, i0, j) for j in range(j0, j1))
        elif j0 == j1:
            self.open.update((1, i, j0) for i in range(i0, i1))
        else:
            raise ValueError("an open edge runs along X or Z")

    # An edge is (0, i, j): from vertex (i, j) to (i, j + 1), between cells
    # (i - 1, j) and (i, j); or (1, i, j): from (i, j) to (i + 1, j), between
    # cells (i, j - 1) and (i, j).

    @staticmethod
    def _sides(edge: tuple[int, int, int]) -> tuple[tuple[int, int], tuple[int, int]]:
        kind, i, j = edge
        return ((i - 1, j), (i, j)) if kind == 0 else ((i, j - 1), (i, j))

    def _edge_kind(self, edge: tuple[int, int, int]) -> str:
        """none, outline, open (left to another piece), seam (raised between
        an area and a road beside the road's gap), gap, or inside."""
        a, b = (self.cells.get(c) for c in self._sides(edge))
        if a is None and b is None:
            return "none"
        if a is None or b is None:
            return "open" if edge in self.open else "outline"
        if a == b:
            return "inside"
        pa, pb = self.pieces[a], self.pieces[b]
        if pa.kind == pb.kind:
            return "inside"
        road = pa if pa.kind == "road" else pb
        kind, i, j = edge
        if kind != road.axis:
            return "seam"  # the road runs beside the area, not into it
        # the edge's middle, twice over, across the road
        across = 2 * j + 1 if kind == 0 else 2 * i + 1
        if abs(across - road.middle()) < NARROW / CELL:
            return "gap"
        return "seam"

    def _vertex_edges(self, i: int, j: int) -> list[tuple[int, int, int]]:
        return [(0, i, j - 1), (0, i, j), (1, i - 1, j), (1, i, j)]

    def _raised(self, i: int, j: int) -> bool:
        return any(self._edge_kind(e) in ("outline", "seam") for e in self._vertex_edges(i, j))

    def _height(self, i: int, j: int) -> float:
        heights = [p.height(i, j) for p in self.pieces if p.contains_vertex(i, j)]
        if max(heights) - min(heights) > 1e-4:
            raise ValueError(f"floors meet at different heights at {self._point(i, j)}")
        return heights[0] + (RISE if self._raised(i, j) else 0.0)

    def _point(self, i: int, j: int) -> tuple[float, float]:
        return self.ox + i * CELL, self.oz + j * CELL

    def build(self, mesh: Mesh, pieces: list[Piece] | None = None) -> None:
        """Writes the floors' cells and walls (only those of pieces, if given)."""
        wanted = None if pieces is None else {self.pieces.index(p) for p in pieces}
        for (i, j), index in sorted(self.cells.items()):
            if wanted is None or index in wanted:
                self._cell(mesh, i, j, self.pieces[index])
        for (i, j), index in sorted(self.cells.items()):
            if wanted is not None and index not in wanted:
                continue
            for edge, outward in (((0, i, j), (-1, 0)), ((0, i + 1, j), (1, 0)),
                                  ((1, i, j), (0, -1)), ((1, i, j + 1), (0, 1))):
                if self._edge_kind(edge) == "outline":
                    self._wall(mesh, edge, outward)

    def _cell(self, mesh: Mesh, i: int, j: int, piece: Piece) -> None:
        corners = [(i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1)]
        raised = [self._raised(*c) for c in corners]
        count = sum(raised)
        edges = [(1, i, j), (0, i + 1, j), (1, i, j + 1), (0, i, j)]  # edge k joins corners k, k+1
        gap = any(self._edge_kind(e) == "gap" for e in edges)
        side = None  # the edge (0..3) whose two corners alone are raised
        if count == 2:
            for k in range(4):
                if raised[k] and raised[(k + 1) % 4]:
                    side = k
        if count == 0:
            material = "Floor_Top_Boderless"
        elif piece.kind == "area":
            material = "Floor_Top_ProfileFlat" if gap else "Floor_Top_Flat"
        elif count == 3 or (gap and side is None):
            material = "Floor_Top_ProfileFlat"
        else:
            material = "Floor_Top_Profile"
        across = 1.0 if material == "Floor_Top_Flat" else 0.5

        points = []
        for k, (ci, cj) in enumerate(corners):
            x, z = self._point(ci, cj)
            y = self._height(ci, cj)
            if count == 0:
                uv = (x / REPEAT, z / REPEAT)
            elif side is not None:
                # u from the raised edge (0, or 1 when it is the far one) to
                # the middle; v along the edge
                far = side in (1, 2)
                u = (1.0 if far else 0.0) if raised[k] else (1.0 - across if far else across)
                v = (z if side in (1, 3) else x) / REPEAT
                uv = (u, v)
            else:
                uv = (0.0 if raised[k] else across, (x + z) / REPEAT)
            points.append(((x, y, z), uv))

        # Split through the corner that differs from the others, as the
        # originals do; else along the (0, 2) diagonal.
        odd = 0
        if count in (1, 3):
            odd = raised.index(count == 1)
        a, b, c, d = (points[(odd + k) % 4] for k in range(4))
        mesh.triangle(material, UP, a, b, c)
        mesh.triangle(material, UP, a, c, d)

    def _wall(self, mesh: Mesh, edge: tuple[int, int, int], outward: tuple[int, int]) -> None:
        kind, i, j = edge
        ends = [(i, j), (i, j + 1)] if kind == 0 else [(i, j), (i + 1, j)]
        top = [self._height(*e) for e in ends]
        corners = []
        for (ei, ej), y in zip(ends, top):
            x, z = self._point(ei, ej)
            along = (z if kind == 0 else x) / REPEAT
            corners.append(((x, y, z), (0.0, along)))
        for (ei, ej), y in reversed(list(zip(ends, top))):
            x, z = self._point(ei, ej)
            along = (z if kind == 0 else x) / REPEAT
            corners.append(((x, y - WALL, z), (WALL / REPEAT, along)))
        mesh.quad("Floor_Side", (float(outward[0]), 0.0, float(outward[1])), *corners)


def funnel(mesh: Mesh, x: float, z: float, toward: tuple[int, int], edge: float,
           gap: float | None) -> None:
    """The 5-long piece that flattens a narrow road, whose edges are at height
    edge, at (x, z): the road's middle line runs through it toward the
    direction given (one of +-X, +-Z), and the road's channel starts 5 units
    away.  gap is the half width of the opening in the rim it meets, or None
    where it meets none (the balloon's end)."""
    dx, dz = toward
    half = NARROW / 2

    def at(a: float, c: float, y: float, u: float, v: float) -> tuple:
        # a along toward, c across (+Z or +X)
        return (x + dx * a + abs(dz) * c, y, z + dz * a + abs(dx) * c), (u, v)

    def flat(c: float) -> tuple:
        return at(0.0, c, edge, 0.5 - c / NARROW, 1.0)

    bottom = at(REPEAT, 0.0, edge - RISE, 0.5, 0.0)
    for c in (-half, half):
        mesh.triangle("Floor_Top_ProfileFlat", UP, at(REPEAT, c, edge, 0.5 - c / NARROW, 0.0),
                      bottom, flat(c))
    rim = [-half, half] if gap is None else [-half, -gap, gap, half]
    for c0, c1 in zip(rim, rim[1:]):
        mesh.triangle("Floor_Top_ProfileFlat", UP, flat(c0), flat(c1), bottom)
    for c in (-half, half):
        out = (abs(dz) * c, 0.0, abs(dx) * c)
        corners = []
        for a, y in ((0.0, edge), (REPEAT, edge), (REPEAT, edge - WALL), (0.0, edge - WALL)):
            point, _ = at(a, c, y, 0.0, 0.0)
            along = point[2] if dx == 0 else point[0]
            corners.append((point, ((edge - y) / REPEAT, along / REPEAT)))
        mesh.quad("Floor_Side", out, *corners)
