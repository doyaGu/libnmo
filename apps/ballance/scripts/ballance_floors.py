"""Ballance floors built the way the original levels build them.

The original floors lie on a grid of 2.5-unit cells.  A floor's top is flat
inside and rises 0.7 to every edge of its outline over one cell, so the same
rule makes the narrow road (two cells wide: a V), the wide road (four cells:
two slopes and a flat middle), the square areas with their raised borders,
and the corners of turns.  Where a road meets an area, or a road of another
width, the edge between them stays raised except for a gap two cells wide in
front of the narrower road's middle.  The cells on either side of the gap
flatten its V: they fan out from the V's bottom, with one texture repeat
across the gap and one along the cell, so a wide road meets a narrow one or
an area through an X-shaped piece, as in the originals.  Walls hang 5 below
the outline; floors have no bottom.

The materials are the levels' own (apps/ballance/data/level_materials.json):
Floor_Top_Boderless where a cell is flat, Floor_Top_Profile on the slopes of
roads, Floor_Top_Flat on the borders of areas, Floor_Top_ProfileFlat on the
corners and where roads open into areas, Floor_Side on the walls.  Textures
repeat every 5 units; across a slope they run from the brick band at the edge
to the fold in the middle (Profile) or across the whole border (Flat).

Floors.area() and Floors.road() add rectangles of cells; Floors.build()
writes the floor's triangles to a Mesh.  funnel() writes the piece that
flattens a narrow road into the rim of a start or checkpoint floor, into a
ball changer's pad (pad()), or ends it under the balloon's planks.
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

    def width(self) -> int:
        """In cells, across the road."""
        i0, j0, i1, j1 = self.rect
        return j1 - j0 if self.axis == 0 else i1 - i0

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
        self.docks: dict[tuple[int, int, int], Piece] = {}

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
             y1: float | None = None, along: str | None = None) -> Piece:
        """A straight road between the two corners, running along X or Z
        (along; by default its longer side), from height y0 at the (x0, z0)
        end to y1 (default y0) at the other."""
        rect = self._rect(x0, z0, x1, z1)
        i0, j0, i1, j1 = rect
        if along is None:
            axis = 0 if i1 - i0 >= j1 - j0 else 1
        else:
            axis = {"x": 0, "z": 1}[along]
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

    def dock(self, x0: float, z0: float, x1: float, z1: float) -> None:
        """Opens the outline between the two points, two cells, to a rail, as
        if a narrow road met the floor there: a narrow road's V runs on to its
        end, an area or a wide road opens through a gap.  The wall under it
        follows the V, its bottom level 5 below the edges."""
        i0, i1 = sorted((self._grid(x0, self.ox), self._grid(x1, self.ox)))
        j0, j1 = sorted((self._grid(z0, self.oz), self._grid(z1, self.oz)))
        if i0 == i1 and j1 - j0 == 2:
            road = Piece("road", (i0, j0, i0 + 1, j1), 0.0, 0.0, 0, False)
            self.docks.update(((0, i0, j), road) for j in range(j0, j1))
        elif j0 == j1 and i1 - i0 == 2:
            road = Piece("road", (i0, j0, i1, j0 + 1), 0.0, 0.0, 1, False)
            self.docks.update(((1, i, j0), road) for i in range(i0, i1))
        else:
            raise ValueError("a rail meets a floor across two cells, along X or Z")

    # An edge is (0, i, j): from vertex (i, j) to (i, j + 1), between cells
    # (i - 1, j) and (i, j); or (1, i, j): from (i, j) to (i + 1, j), between
    # cells (i, j - 1) and (i, j).

    @staticmethod
    def _sides(edge: tuple[int, int, int]) -> tuple[tuple[int, int], tuple[int, int]]:
        kind, i, j = edge
        return ((i - 1, j), (i, j)) if kind == 0 else ((i, j - 1), (i, j))

    def _join(self, edge: tuple[int, int, int]) -> tuple[str, Piece | None]:
        """What the edge is: none, outline, open (left to another piece),
        dock (where a narrow road ends at a rail), inside, gap (where a road
        opens into an area, or into a road of another width: the narrower
        road's two middle cells; or a rail into either) or seam (raised
        beside a gap); with the road whose middle a gap or seam is set by."""
        a, b = (self.cells.get(c) for c in self._sides(edge))
        if a is None and b is None:
            return "none", None
        if a is None or b is None:
            if edge in self.open:
                return "open", None
            rail = self.docks.get(edge)
            if rail is None:
                return "outline", None
            piece = self.pieces[a if b is None else b]
            if piece.kind == "road" and piece.axis == edge[0] and piece.width() == rail.width():
                return "dock", None
            return "gap", rail
        if a == b:
            return "inside", None
        pa, pb = self.pieces[a], self.pieces[b]
        kind, i, j = edge
        if pa.kind == "area" and pb.kind == "area":
            return "inside", None
        if pa.kind == "road" and pb.kind == "road":
            if kind != pa.axis or kind != pb.axis or pa.width() == pb.width():
                return "inside", None  # a turn, or a road going on
            road = pa if pa.width() < pb.width() else pb
        else:
            road = pa if pa.kind == "road" else pb
            if kind != road.axis:
                return "seam", road  # the road runs beside the area, not into it
        # the edge's middle, twice over, across the road
        across = 2 * j + 1 if kind == 0 else 2 * i + 1
        if abs(across - road.middle()) < NARROW / CELL:
            return "gap", road
        return "seam", road

    def _edge_kind(self, edge: tuple[int, int, int]) -> str:
        return self._join(edge)[0]

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
                elif edge in self.docks:
                    self._wall(mesh, edge, outward, level=True)

    def _cell(self, mesh: Mesh, i: int, j: int, piece: Piece) -> None:
        corners = [(i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1)]
        raised = [self._raised(*c) for c in corners]
        count = sum(raised)
        # edge k joins corners k and k + 1
        edges = [(1, i, j), (0, i + 1, j), (1, i, j + 1), (0, i, j)]
        joins = [self._join(e) for e in edges]
        gap = next((k for k in range(4) if joins[k][0] == "gap"), None)
        side = None  # the edge whose two corners alone are raised
        if count == 2:
            side = next((k for k in range(4) if raised[k] and raised[(k + 1) % 4]), None)
        # A gap cell whose far side is flat flattens the V at the gap: the
        # area's border cells there, and a wide road's first cells.
        if gap is not None and (raised[(gap + 2) % 4] or raised[(gap + 3) % 4]):
            gap = None
        if count == 0:
            material = "Floor_Top_Boderless"
        elif gap is not None:
            material = "Floor_Top_ProfileFlat"
        elif piece.kind == "area":
            material = "Floor_Top_Flat"  # its borders' corners too
        elif count == 3:
            material = "Floor_Top_ProfileFlat"
        else:
            material = "Floor_Top_Profile"
        across = 1.0 if material == "Floor_Top_Flat" else 0.5
        xyz = []
        for ci, cj in corners:
            x, z = self._point(ci, cj)
            xyz.append((x, self._height(ci, cj), z))

        def axis_of(k0: int, k1: int) -> int:
            """0 when corners k0 and k1 lie along X, 1 along Z."""
            return 0 if corners[k0][1] == corners[k1][1] else 1

        def side_uv(k: int, along: int, far: bool) -> tuple[float, float]:
            x, _, z = xyz[k]
            if raised[k]:
                u = 1.0 if far else 0.0
            else:
                u = 1.0 - across if far else across
            return u, (x if along == 0 else z) / REPEAT

        if count == 0:
            uv = [(x / REPEAT, z / REPEAT) for x, _, z in xyz]
            triangles = [((0, 1, 2), uv), ((0, 2, 3), uv)]
        elif gap is not None:
            # One repeat across the gap's 5 and along the cell: u across, v
            # from the gap's line to the far side.  The cell is split from the
            # gap's middle to the far corner beyond the gap's edge, as the
            # originals' joins fan out from the V's bottom.
            _, road = joins[gap]
            k0, k1 = gap, (gap + 1) % 4
            line = axis_of(k0, k1)
            offset = self.ox if line == 0 else self.oz
            start = offset + road.middle() * CELL / 2 - NARROW / 2
            uv = []
            for k in range(4):
                on_line = k in (k0, k1)
                at = xyz[k][0] if line == 0 else xyz[k][2]
                uv.append(((at - start) / NARROW, 0.0 if on_line else 1.0))
            middle = k0 if abs(uv[k0][0] - 0.5) < 1e-6 else k1
            edge_end = k1 if middle == k0 else k0
            # the far-side corner across from the gap's edge end
            far = next(k for k in range(4) if k not in (k0, k1) and
                       abs(uv[k][0] - uv[edge_end][0]) < 1e-6)
            rest = [k for k in range(4) if k not in (middle, far)]
            triangles = [((middle, far, rest[0]), uv), ((middle, far, rest[1]), uv)]
        elif side is not None:
            along = axis_of(side, (side + 1) % 4)
            far = side in (1, 2)
            uv = [side_uv(k, along, far) for k in range(4)]
            triangles = [((0, 1, 2), uv), ((0, 2, 3), uv)]
        elif count == 3 and material == "Floor_Top_ProfileFlat":
            # A road's raised corner, mapped as the originals' are: brick
            # band (u 0) along both raised edges, the fold (u 0.5) at the low
            # corner, and v all but constant, so the texture's V does not
            # show: 0.76 at the corner across from the low one, 0 elsewhere.
            odd = raised.index(False)
            far = (odd + 2) % 4
            uv = [(0.5 if k == odd else 0.0, 0.76 if k == far else 0.0) for k in range(4)]
            a, b, c, d = ((odd + k) % 4 for k in range(4))
            triangles = [((a, b, c), uv), ((a, c, d), uv)]
        elif count in (1, 3):
            # Split through the corner that differs from the others; each
            # half goes on mapped as the slope or border it continues, along
            # its edge away from that corner: for a corner of three, the
            # raised edge across from it; for an inner corner of one, the
            # low edge, so each half carries on its own road's slope.
            odd = raised.index(count == 1)
            a, b, c, d = ((odd + k) % 4 for k in range(4))
            triangles = []
            for tri in ((a, b, c), (a, c, d)):
                along = axis_of(tri[1], tri[2])
                uv = [side_uv(k, along, False) for k in range(4)]
                triangles.append((tri, uv))
        else:
            uv = [((0.0 if raised[k] else across), xyz[k][0] / REPEAT) for k in range(4)]
            triangles = [((0, 1, 2), uv), ((0, 2, 3), uv)]
        for tri, uv in triangles:
            mesh.triangle(material, UP, *[(xyz[k], uv[k]) for k in tri])

    def _wall(self, mesh: Mesh, edge: tuple[int, int, int], outward: tuple[int, int],
              level: bool = False) -> None:
        """A wall WALL deep under the edge; with level, its bottom is level,
        WALL below the edge's higher end."""
        kind, i, j = edge
        ends = [(i, j), (i, j + 1)] if kind == 0 else [(i, j), (i + 1, j)]
        top = [self._height(*e) for e in ends]
        bottom = [max(top) - WALL] * 2 if level else [y - WALL for y in top]
        corners = []
        for (ei, ej), y in zip(ends, top):
            x, z = self._point(ei, ej)
            along = (z if kind == 0 else x) / REPEAT
            corners.append(((x, y, z), (0.0, along)))
        for (ei, ej), y, low in reversed(list(zip(ends, top, bottom))):
            x, z = self._point(ei, ej)
            along = (z if kind == 0 else x) / REPEAT
            corners.append(((x, low, z), ((y - low) / REPEAT, along)))
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


def pad(mesh: Mesh, x: float, z: float, top: float, material: str) -> None:
    """The 5 by 5 block a ball changer stands on, centred on (x, z), its top
    at height top (a road's edges), its sides 5 deep, all in material (one of
    the Floor_Trafo_* materials), one texture repeat on each face.  A narrow
    road runs into it through funnels on either side."""
    half = NARROW / 2
    corners = [(x - half, z - half), (x + half, z - half), (x + half, z + half),
               (x - half, z + half)]
    mesh.quad(material, UP, *[((cx, top, cz), uv) for (cx, cz), uv in
                              zip(corners, ((0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)))])
    for k in range(4):
        (ax, az), (bx, bz) = corners[k], corners[(k + 1) % 4]
        out = ((ax + bx) / 2 - x, 0.0, (az + bz) / 2 - z)
        mesh.quad(material, out, ((ax, top, az), (0.0, 0.0)), ((bx, top, bz), (1.0, 0.0)),
                  ((bx, top - WALL, bz), (1.0, 1.0)), ((ax, top - WALL, az), (0.0, 1.0)))
