"""The floor pieces off the grid: funnels, pads, flat roads and channels of
any length, written straight to a Mesh at given places.  Each runs along a
heading (one of +-X, +-Z) from a point, at the height of a narrow road's
edges."""

from __future__ import annotations

from ..core.obj import Mesh
from .floors import NARROW, REPEAT, RISE, UP, WALL


def funnel(mesh: Mesh, x: float, z: float, toward: tuple[int, int], edge: float,
           gap: float | None, end_wall: bool = False) -> None:
    """The 5-long piece that flattens a narrow road, whose edges are at height
    edge, at (x, z): the road's middle line runs through it toward the
    direction given (one of +-X, +-Z), and the road's channel starts 5 units
    away.  gap is the half width of the opening in the rim it meets, or None
    where it meets none (the balloon's end).  With end_wall, a wall 5 deep
    closes its flat end, where it stops at a gap."""
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
    if end_wall:
        corners = []
        for c, y in ((-half, edge), (half, edge), (half, edge - WALL), (-half, edge - WALL)):
            point, _ = at(0.0, c, y, 0.0, 0.0)
            along = point[2] if dx != 0 else point[0]
            corners.append((point, ((edge - y) / REPEAT, along / REPEAT)))
        mesh.quad("Floor_Side", (-float(dx), 0.0, -float(dz)), *corners)


def strip(mesh: Mesh, x: float, z: float, toward: tuple[int, int], length: float,
          top: float) -> None:
    """A flat road 5 wide at height top (a narrow road's edges), from (x, z)
    along toward (one of +-X, +-Z) for length, as the originals lay one under
    P_Modul_26: Floor_Top_Flat squares of 5, each one texture repeat (u
    across, v along), with walls 5 deep along its sides."""
    dx, dz = toward
    half = NARROW / 2

    def at(a: float, c: float, y: float) -> tuple[float, float, float]:
        return (x + dx * a + abs(dz) * c, y, z + dz * a + abs(dx) * c)

    squares = round(length / NARROW)
    for k in range(squares):
        a0, a1 = k * NARROW, (k + 1) * NARROW
        mesh.quad("Floor_Top_Flat", UP, (at(a0, -half, top), (0.0, 0.0)),
                  (at(a1, -half, top), (0.0, 1.0)), (at(a1, half, top), (1.0, 1.0)),
                  (at(a0, half, top), (1.0, 0.0)))
    for c in (-half, half):
        out = (abs(dz) * c, 0.0, abs(dx) * c)
        corners = []
        for a, y in ((0.0, top), (length, top), (length, top - WALL), (0.0, top - WALL)):
            point = at(a, c, y)
            along = point[2] if dx == 0 else point[0]
            corners.append((point, ((top - y) / REPEAT, along / REPEAT)))
        mesh.quad("Floor_Side", out, *corners)


def channel(mesh: Mesh, x: float, z: float, toward: tuple[int, int], length: float,
            edge: float) -> None:
    """A narrow road of any length, off the grid, whose edges are at height
    edge, from (x, z) along toward (one of +-X, +-Z): the two
    Floor_Top_Profile slopes of the grid's narrow road, mapped as they are
    (u 0 and 1 at the edges, 0.5 at the fold, v along), with walls 5 deep
    along its sides and its ends open, to join roads on other grids."""
    dx, dz = toward
    half = NARROW / 2

    def at(a: float, c: float, y: float) -> tuple[float, float, float]:
        return (x + dx * a + abs(dz) * c, y, z + dz * a + abs(dx) * c)

    def along(point: tuple[float, float, float]) -> float:
        return (point[2] if dx == 0 else point[0]) / REPEAT

    for c, u in ((-half, 0.0), (half, 1.0)):
        corners = [at(0.0, c, edge), at(length, c, edge), at(length, 0.0, edge - RISE),
                   at(0.0, 0.0, edge - RISE)]
        uvs = [u, u, 0.5, 0.5]
        mesh.quad("Floor_Top_Profile", UP,
                  *[(p, (uu, along(p))) for p, uu in zip(corners, uvs)])
        out = (abs(dz) * c, 0.0, abs(dx) * c)
        corners = []
        for a, y in ((0.0, edge), (length, edge), (length, edge - WALL), (0.0, edge - WALL)):
            point = at(a, c, y)
            corners.append((point, ((edge - y) / REPEAT, along(point))))
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
