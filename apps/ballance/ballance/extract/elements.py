"""The meshes of Ballance's level elements, exported from the original
levels as OBJ files for generated levels to use.

A level file places its start, checkpoints, reset points, finish and elements
as objects with meshes of their own, and the game uses some of these meshes
as they are: the start (PS_FourFlames) and the checkpoints (PC_TwoFlames)
have no mesh in their 3D Entities/PH templates, so a level draws them with
its own.  One mesh of each kind is taken from the original levels, and the
SkyLayer cloud plane every level has, as <Kind>.obj (with .mtl).

Some floors are cut out of the floors they are part of (Cut):

- around a marker (MarkerCut): the start's octagon under Level_03's start,
  which opens to a road on its +X side; the round floor with two flame bowls
  under its first checkpoint, which opens on both X sides; a fan's housing,
  the 5 by 5 pit with a grille set into an area 0.35 above the fan.  Placed at
  the marker's position, unturned, they stand as in Level_03.
- in a module's own frame (ModuleCut): the drawbridge's (P_Modul_25) two flat
  blocks and its stopper lip, from Level_03; the switch's (P_Modul_37) target
  block, cross piece and lip, from Level_05; the stone on a box's
  (P_Modul_34) whole plaza from Level_01: the walkway from a stone trafo's
  platform over the box's gap and on up out of the plaza, the pit on either
  side of it, the channel under the gap and the ramps down from the rim; the
  pushers' (P_Modul_01, a pair) plaza from Level_01, in the first one's
  frame: the corridor through both, the low ground south of it their targets
  are pushed from, the pit north of it, and the ring road round both with its
  ramps, cut where the corridor, narrowing into a narrow road's V, meets
  the platform beyond; the seesaw's (P_Modul_30) two stopper lips from
  Level_03, at the ends of the gap it spans; the rotating arm's (P_Modul_17)
  floors from Level_03, in its frame: the road to a drawbridge's gap with the
  loop beside it that leads to the arm, the stopper lip, and the gap's far
  block (cut on its own: the floor beyond it goes on elsewhere); the flaps'
  (P_Modul_19) two roads with their gaps and stopper lips from Level_09.
  Placed and turned as the module, they stand as in the level.
- the scenery from Level_01 (DecorCut): one of the pillars beside its start,
  with the shaft that fades out below it (Pillar); the lantern tower on top
  of it (Tower); one of the striped poles by its balloon (Stick).  Pillar and
  Stick have the middle of their tops at their origin, Tower Pillar's.

Where a cut floor meets the floors a level builds, its edges must lie exactly
where the builder puts theirs, or the joins show thin cracks.  The originals
miss by up to 0.04 (Level_03's checkpoint opens from -0.869 to 0.906 where
its road is 1.74 wide), so a cut's coordinates near the builder's values are
moved onto them (SNAP).

The meshes are the game's own, so they are written from a local installation
and not kept with libnmo.
"""

from __future__ import annotations

import math
import re
import tempfile
from pathlib import Path

from ..core.nmo import Nmo, field_values, into, matrix_of, place, turn
from ..core.obj import read_obj
from ..game.catalog import ELEMENT_KINDS, MARKERS

KINDS = MARKERS + ELEMENT_KINDS + ["SkyLayer"]

# Snapping: a cut's coordinates within SNAP_TOLERANCE of these are moved onto
# them -- per kind, the values along X, Y and Z in its own frame, mirrored to
# both signs; a value given with its own tolerance, (value, tolerance), uses
# that one.
SNAP_TOLERANCE = 0.06
# The walls closing a cut floor: as deep as the floors' (ballance.kit.floors),
# one texture repeat every 5.
WALL_DEPTH, REPEAT_UNITS = 5.0, 5.0
SNAP = {
    "Floor_Start": ([7.39], [1.26, 3.74], [2.5, 1.3]),
    "Floor_Checkpoint": ([4.92], [1.26, 3.74], [2.5, 0.87]),
    "Floor_Modul_18": ([2.5], [0.35], [2.5]),
    "Floor_Modul_25": ([5.0, 0.0, 10.0, 15.0], [0.0], [2.5]),
    # Level_05's target block lies from -2.4 to 2.6 across
    "Floor_Modul_37": ([3.73, 1.27, 11.27, 16.27], [0.0], [(2.5, 0.11), 4.75, 9.75]),
    "Floor_Modul_34": ([24.6, 30.4], [0.0, 2.1, 2.8], [2.5, 5.0]),
    "Floor_Modul_01": ([0.0, 2.5, 5.0], [0.0, 0.7], [28.15, 24.35]),
    "Floor_Stopper_30": ([7.4, 6.4], [0.0, 0.5], [2.5]),
    "Floor_Modul_17": ([11.21, 8.71, 13.71], [5.43, 6.13], [24.61]),
    "Floor_Modul_17_Far": ([11.21, 8.71, 13.71], [5.43, 6.13], [8.72, 1.28]),
    "Floor_Modul_19": ([7.53, 14.12, 9.12, 2.47, 3.32, 8.34], [0.0, 0.7], [2.5, 7.5, 12.5]),
    "Floor_Stopper_19": ([9.12, 8.12, 2.32, 3.32], [0.53, 1.03], [2.5, 7.5, 12.5]),
    "Modul27": ([5.0], [], [2.5]),
    "Modul42": ([35.16], [0.41], [2.5]),
    "Floor_Cradle": ([7.5, 5.0, 2.5], [2.5, 1.8, 1.1], [7.5, 5.0, 2.5]),
    "Floor_RailDock": ([5.0, 2.5], [0.7, 0.58], [5.0]),
    "Modul02": ([28.2], [11.84], [2.5]),
    "Floor_Modul02": ([42.49, 44.99, 47.49, 52.49, 54.99, 57.49], [13.18, 18.18],
                      [2.5, 5.0, 7.5]),
    "Floor_Modul11": ([1.88, 3.12, 0.62], [3.19, 8.19], [24.33, 26.83, 29.33]),
    "Floor_Modul_08": ([12.5, 10.0, 7.5], [3.5, 1.5], [0.0, 2.5]),
    # Level_07's platform lies 0.07 off the lift's middle, across
    "Floor_Modul_03": ([10.95, 15.94, 20.94, 25.94, 30.94, 35.94], [1.884, 6.884],
                       [(0.0, 0.1), (2.5, 0.1), (7.5, 0.1)]),
}

Face = tuple[str, list[tuple[tuple, tuple, tuple]]]  # material, (position, uv, normal) x 3


class Levels:
    """The original level files, with their objects exported once each."""

    def __init__(self, game: Path, nmo: Nmo) -> None:
        self.files = sorted((game / "3D Entities" / "Level").glob("Level_*.NMO"))
        self.by_name = {path.name.lower(): path for path in self.files}
        self.nmo = nmo
        self._objects: dict[Path, dict[str, dict]] = {}

    def objects(self, path: Path) -> dict[str, dict]:
        """The level's 3D objects' fields, by name."""
        if path not in self._objects:
            self._objects[path] = {obj["name"]: field_values(obj)
                                   for obj in self.nmo.objects(path, "CK3dObject")}
        return self._objects[path]

    def faces(self, path: Path, fields: dict) -> tuple[list[Face], str]:
        """An object's mesh placed by its world matrix, and the mesh's .mtl."""
        matrix = matrix_of(fields["world_matrix"])
        with tempfile.TemporaryDirectory() as work:
            self.nmo.export_mesh(path, fields["current_mesh"], Path(work))
            obj = next(Path(work).glob("*.obj"))
            mtl = next(Path(work).glob("*.mtl")).read_text(encoding="utf-8")
            faces = [(t.material, [(place(matrix, p), uv, turn(matrix, n))
                                   for p, uv, n in zip(t.points, t.uvs, t.normals)])
                     for t in read_obj(obj)]
        return faces, mtl


class Cut:
    """A floor cut out of a level: its kind (the name it is written as) and
    the level it comes from."""

    def __init__(self, kind: str, level: str) -> None:
        self.kind, self.level = kind, level

    def take(self, levels: Levels, path: Path, out: Path,
             centers: dict[str, tuple]) -> str | None:
        """Writes the cut; returns what it was cut from, or None."""
        raise NotImplementedError


class MarkerCut(Cut):
    """The part of a floor within half extents (X, Z) of a marker, around the
    marker."""

    def __init__(self, kind: str, level: str, floor: str, marker: str,
                 half_x: float, half_z: float) -> None:
        super().__init__(kind, level)
        self.floor, self.marker, self.half_x, self.half_z = floor, marker, half_x, half_z

    def take(self, levels, path, out, centers):
        objects = levels.objects(path)
        if self.floor not in objects or self.marker not in objects:
            return None
        m = matrix_of(objects[self.marker]["world_matrix"])
        faces, mtl = levels.faces(path, objects[self.floor])
        write_faces(near(faces, (m[12], m[14]), self.half_x, self.half_z), [mtl],
                    (m[12], m[13], m[14]), self.kind, out)
        return f"{path.name}:{self.floor}"


class ModuleCut(Cut):
    """The part of a floor within a box of a module's frame, in that frame
    (moved so origin, a point of it, is at its origin)."""

    def __init__(self, kind: str, level: str, floor: str, module: str, box: tuple,
                 origin: tuple = (0.0, 0.0, 0.0)) -> None:
        super().__init__(kind, level)
        self.floor, self.module, self.box, self.origin = floor, module, box, origin

    def take(self, levels, path, out, centers):
        objects = levels.objects(path)
        if self.floor not in objects or self.module not in objects:
            return None
        faces, mtl = levels.faces(path, objects[self.floor])
        matrix = matrix_of(objects[self.module]["world_matrix"])
        if self.kind.startswith("Floor_"):
            cut = clipped_in_frame(faces, matrix, self.box)
            cut += closing_walls(cut)
        else:
            cut = in_frame(faces, matrix, self.box)
        write_faces(cut, [mtl], self.origin, self.kind, out)
        return f"{path.name}:{self.floor}"


class ChainCut(Cut):
    """A floor that repeats along its X, as a log or a trough does: each
    length from one segment to `most`, as <kind>_<n>.obj, n segments of
    `step` (the part of the floor in segment, a box of its own frame) one
    after the other around the origin, with the face in end (a box there,
    facing X) at either end, so that each length is one mesh, without the
    seams between segments the ball would knock against."""

    def __init__(self, kind: str, level: str, floor: str, segment: tuple, end: tuple,
                 step: float, most: int) -> None:
        super().__init__(kind, level)
        self.floor, self.segment, self.end, self.step, self.most = floor, segment, end, step, most

    def take(self, levels, path, out, centers):
        objects = levels.objects(path)
        if self.floor not in objects:
            return None
        faces, mtl = levels.faces(path, objects[self.floor])
        matrix = matrix_of(objects[self.floor]["world_matrix"])
        segment = in_frame(faces, matrix, self.segment)
        end = in_frame(faces, matrix, self.end)
        middle = (self.segment[0] + self.segment[1]) / 2
        tip = (self.end[0] + self.end[1]) / 2
        for n in range(1, self.most + 1):
            chain = []
            for k in range(n):
                shift = self.step * (k - (n - 1) / 2) - middle
                chain += [(m, [((p[0] + shift, p[1], p[2]), uv, nm) for p, uv, nm in corners])
                          for m, corners in segment]
            half = self.step * n / 2
            for sign in (1.0, -1.0):
                # the far face as it is, the near one turned about Y
                chain += [(m, [((sign * (p[0] - tip + half), p[1], sign * p[2]), uv,
                                (sign * nm[0], nm[1], sign * nm[2])) for p, uv, nm in corners])
                          for m, corners in end]
            write_faces(chain, [mtl], (0.0, 0.0, 0.0), f"{self.kind}_{n}", out)
        return f"{path.name}:{self.floor}"


class DecorCut(Cut):
    """Objects' faces within reach of a point (X, Z), around the top of their
    middle, or around another cut's (on)."""

    def __init__(self, kind: str, level: str, names: list[str], point: tuple[float, float],
                 reach: float, on: str | None = None) -> None:
        super().__init__(kind, level)
        self.names, self.point, self.reach, self.on = names, point, reach, on

    def take(self, levels, path, out, centers):
        objects = levels.objects(path)
        if not all(name in objects for name in self.names):
            return None
        faces, mtls = [], []
        for name in self.names:
            part, mtl = levels.faces(path, objects[name])
            faces += near(part, self.point, self.reach, self.reach)
            mtls.append(mtl)
        if self.on is not None:
            center = centers[self.on]
        else:
            # the middle of its shaft, at its top
            xs = [p[0] for _, corners in faces for p, _, _ in corners]
            zs = [p[2] for _, corners in faces for p, _, _ in corners]
            ys = [p[1] for _, corners in faces for p, _, _ in corners]
            center = ((min(xs) + max(xs)) / 2, max(ys), (min(zs) + max(zs)) / 2)
            centers[self.kind] = center
        write_faces(faces, mtls, center, self.kind, out)
        return f"{path.name}:{'+'.join(self.names)}"


CUTS: list[Cut] = [
    MarkerCut("Floor_Start", "Level_03.NMO", "A01_Floor_01", "PS_FourFlames_01", 8.07, 9.3),
    MarkerCut("Floor_Checkpoint", "Level_03.NMO", "A02_Floor_01", "PC_TwoFlames_01", 4.93, 9.3),
    MarkerCut("Floor_Modul_18", "Level_03.NMO", "A05_Floor_02", "P_Modul_18_03", 2.52, 2.52),
    ModuleCut("Floor_Modul_25", "Level_03.NMO", "A05_Floor_02", "P_Modul_25_03",
              (-15.1, 5.1, -5.1, 0.1, -2.6, 2.6)),
    ModuleCut("Floor_Stopper_25", "Level_03.NMO", "A01_Floor_Stopper_01", "P_Modul_25_03",
              (-15.1, 5.1, -5.1, 0.1, -2.6, 2.6)),
    ModuleCut("Floor_Modul_37", "Level_05.NMO", "A02_Floor_02", "P_Modul_37_02",
              (-16.3, 3.8, -5.1, 0.1, -9.8, 9.8)),
    ModuleCut("Floor_Stopper_37", "Level_05.NMO", "A01_Floor_Stopper", "P_Modul_37_02",
              (-16.3, 3.8, -5.1, 0.1, -9.8, 9.8)),
    ModuleCut("Floor_Modul_34", "Level_01.NMO", "A02_Floor_03", "P_Modul_34_01",
              (-30.45, 24.65, -20.0, 3.0, -22.8, 22.8)),
    ModuleCut("Floor_Modul_01", "Level_01.NMO", "A03_Floor_02", "P_Modul_01_01",
              (-25.1, 30.1, -10.0, 0.2, -24.4, 28.2)),
    ModuleCut("Floor_Stopper_30", "Level_03.NMO", "A01_Floor_Stopper_01", "P_Modul_30_01",
              (-7.5, 7.5, -0.6, 0.1, -2.6, 2.6)),
    ModuleCut("Floor_Modul_17", "Level_03.NMO", "A04_Floor_01", "P_Modul_17_01",
              (-24.0, 15.5, -11.0, -0.5, -8.78, 24.62)),
    ModuleCut("Floor_Modul_17_Far", "Level_03.NMO", "A04_Floor_02", "P_Modul_17_01",
              (-13.8, -8.6, -11.0, -0.5, -8.78, 24.62)),
    ModuleCut("Floor_Modul_19", "Level_09.NMO", "A01_Floor_01", "P_Modul_19_01",
              (-14.2, 7.6, -5.1, 0.1, -12.6, 7.6)),
    ModuleCut("Floor_Stopper_19", "Level_09.NMO", "A01_Floor_Stopper", "P_Modul_19_01",
              (-14.2, 7.6, -1.1, -0.4, -12.6, 7.6)),
    ModuleCut("Modul27", "Level_03.NMO", "A04_Modul_27", "A04_Modul_27",
              (-5.1, 5.1, -3.0, 0.1, -2.6, 2.6)),
    ModuleCut("Modul42", "Level_08.NMO", "A02_Modul42", "A02_Modul42",
              (-35.3, 35.3, -2.2, 2.2, -2.6, 2.6)),
    # the cradle a rail drops the ball into: the block, the rail's end bent
    # down to it with the tubes holding the ball and the railing, and the
    # railing's clamps
    ModuleCut("Floor_Cradle", "Level_01.NMO", "A01_Floor_05", "A01_Floor_05",
              (-7.6, 7.6, -2.6, 2.6, -7.6, 7.6)),
    ModuleCut("Rail_Cradle", "Level_01.NMO", "A01_Rail_09", "A01_Floor_05",
              (-7.45, 8.0, -2.0, 9.6, -8.0, 3.84)),
    ModuleCut("Misc_Cradle", "Level_01.NMO", "A01_Misc_Schelle04", "A01_Floor_05",
              (-8.0, 8.0, -3.0, 3.0, -9.0, 9.0)),
    # the trough with holes in its bottom
    ModuleCut("Trough_Holes", "Level_04.NMO", "A01_Halfpipe_04", "A01_Halfpipe_04",
              (-175.1, 175.1, -3.0, 3.0, -9.4, 9.4)),
    # troughs and logs of 1 to 8 segments, each one mesh
    ChainCut("Trough", "Level_04.NMO", "A01_Halfpipe_01", (-50.05, 0.05, -3.0, 3.0, -9.4, 9.4),
             (99.95, 100.05, -3.0, 3.0, -9.4, 9.4), 50.0, 8),
    ChainCut("Log", "Level_12.NMO", "A08_Halfpipe_02", (-25.05, 25.05, -2.4, 2.4, -8.5, 8.5),
             (74.95, 75.05, -2.4, 2.4, -8.5, 8.5), 50.0, 8),
    # the fan's housing block a rail ends in, its bottom's middle at the origin
    ModuleCut("Floor_FanBlock", "Level_05.NMO", "A03_Floor_Modul18", "A03_Floor_Modul18",
              (-2.6, 2.6, -0.1, 5.1, -2.6, 2.6)),
    # a wide road's end narrowing to a rail, its flat middle at the origin
    ModuleCut("Floor_RailDock", "Level_01.NMO", "A01_Floor_03", "A01_Rail_07",
              (-5.1, 5.1, -6.0, 1.4, 12.8, 17.95), (0.0, 0.58, 17.88)),
    # the tubes parting at a rail's end, from its last straight ring on
    ModuleCut("Rail_Split", "Level_01.NMO", "A01_Rail_02_01", "A01_Rail_02_01",
              (-13.5, -3.0, -0.5, 0.4, -12.0, 12.0), (-3.089, -0.101, 0.0)),
    ModuleCut("Modul02", "Level_09.NMO", "A05_Modul02", "A05_Modul02",
              (-28.3, 28.3, -12.0, 12.0, -2.6, 2.6)),
    ModuleCut("Floor_Modul02", "Level_09.NMO", "A05_Floor_03", "A05_Modul02",
              (42.4, 57.6, -18.3, -13.1, -7.6, 7.6)),
    ModuleCut("Modul11", "Level_07.NMO", "A01_Modul11", "A01_Modul11",
              (-5.5, 6.7, -3.7, 3.7, -29.4, 29.4)),
    ModuleCut("Floor_Modul11", "Level_07.NMO", "A01_Floor_02", "A01_Modul11",
              (-1.9, 3.2, -8.3, -3.1, -29.4, 29.4)),
    ModuleCut("Floor_Modul_08", "Level_09.NMO", "A03_Floor_01", "P_Modul_08_01",
              (-12.6, 12.6, -3.6, 1.6, -2.6, 2.6)),
    ModuleCut("Floor_Modul_03", "Level_07.NMO", "A05_Floor_03", "P_Modul_03_01",
              (-36.0, -10.9, 1.8, 7.0, -7.7, 7.7)),
    ModuleCut("Floor_Stopper_17", "Level_03.NMO", "A01_Floor_Stopper_01", "P_Modul_17_01",
              (-24.0, 15.5, -11.0, -0.5, -8.78, 24.62)),
    DecorCut("Pillar", "Level_01.NMO", ["A01_Floor_Columns_Top", "A01_Floor_Colums_Bottom"],
             (31.8, -167.7), 3.0),
    DecorCut("Tower", "Level_01.NMO", ["A01_Tower"], (31.8, -167.7), 4.5, on="Pillar"),
    DecorCut("Stick", "Level_01.NMO", ["A04_Stick_Top", "A04_Stick_Bottom"], (680.2, -889.6), 1.5),
]


def snapped(value: float, targets: list) -> float:
    for target in targets:
        target, tolerance = target if isinstance(target, tuple) else (target, SNAP_TOLERANCE)
        for signed in (target, -target):
            if abs(value - signed) <= tolerance:
                return signed
    return value


def write_faces(faces: list[Face], mtls: list[str], center: tuple, kind: str, out: Path) -> None:
    """Writes faces moved so center is at the origin, as <kind>.obj and .mtl,
    their coordinates snapped as SNAP says for kind."""
    snap = SNAP.get(kind, ([], [], []))
    lines = [f"mtllib {kind}.mtl"]
    material = None
    for face_material, corners in faces:
        if face_material != material:
            lines.append(f"usemtl {face_material}")
            material = face_material
        for position, uv, normal in corners:
            lines.append("v %.6f %.6f %.6f" % tuple(snapped(position[i] - center[i], snap[i])
                                                    for i in range(3)))
            lines.append("vt %.6f %.6f" % uv)
            lines.append("vn %.6f %.6f %.6f" % normal)
        lines.append("f " + " ".join(f"{i}/{i}/{i}" for i in range(-len(corners), 0)))
    blocks, seen = [], set()
    for mtl in mtls:
        for block in mtl.split("newmtl ")[1:]:
            name = block.split()[0]
            if name not in seen:
                seen.add(name)
                blocks.append("newmtl " + block.rstrip() + "\n")
    (out / f"{kind}.mtl").write_text("\n".join(blocks), encoding="utf-8")
    (out / f"{kind}.obj").write_text("\n".join(lines) + "\n", encoding="utf-8")


def near(faces: list[Face], center: tuple[float, float], half_x: float,
         half_z: float) -> list[Face]:
    """The faces lying within half_x and half_z of center, on X and Z."""
    return [(m, corners) for m, corners in faces
            if all(abs(p[0] - center[0]) <= half_x and abs(p[2] - center[1]) <= half_z
                   for p, _, _ in corners)]


SLIVER = 0.15  # what is left of a face this close to a box's side is dropped


def clipped_in_frame(faces: list[Face], matrix: list[float], box: tuple) -> list[Face]:
    """The faces in a box of an object's frame, as in_frame(), but those
    across the box's sides cut at them rather than left out (all but the
    slivers less than SLIVER deep), so that a wall the box ends goes on to
    its side instead of breaking off with a triangle missing."""
    planes = [(axis, bound, sign) for axis in range(3)
              for bound, sign in ((box[2 * axis], 1.0), (box[2 * axis + 1], -1.0))]
    out = []
    for m, corners in faces:
        poly = [(into(matrix, p, 1.0), uv, into(matrix, n, 0.0)) for p, uv, n in corners]
        cut_by = []
        for axis, bound, sign in planes:
            inside = [sign * (p[axis] - bound) >= 0 for p, _, _ in poly]
            if all(inside):
                continue
            cut_by.append((axis, bound, sign))
            poly = _clip(poly, axis, bound, sign)
            if len(poly) < 3:
                break
        if len(poly) < 3:
            continue
        if any(max(sign * (p[axis] - bound) for p, _, _ in poly) < SLIVER
               for axis, bound, sign in cut_by):
            continue
        for k in range(1, len(poly) - 1):
            out.append((m, [poly[0], poly[k], poly[k + 1]]))
    return out


def _clip(poly: list, axis: int, bound: float, sign: float) -> list:
    """The part of a polygon (corners of position, uv, normal) on the side
    of the plane at bound on axis that sign points to (Sutherland-Hodgman)."""
    out = []
    for k, a in enumerate(poly):
        b = poly[(k + 1) % len(poly)]
        da, db = sign * (a[0][axis] - bound), sign * (b[0][axis] - bound)
        if da >= 0:
            out.append(a)
        if (da >= 0) != (db >= 0):
            t = da / (da - db)
            out.append(tuple(tuple(x + (y - x) * t for x, y in zip(u, v)) for u, v in zip(a, b)))
    return out


def closing_walls(cut: list[Face]) -> list[Face]:
    """Walls hanging WALL_DEPTH below the edges of a cut floor's top that
    end it with no wall: where the cut ends a floor the level carried on
    (in the same object or another); Floor_Side, facing away from the top
    face, one texture repeat every 5 as the floors' walls.  Where a piece
    of the track goes on from such an edge, its wall is hidden under it."""
    def key(p):
        return tuple(round(c, 2) for c in p)

    # each corner as it is, so that the walls snap as the faces do
    exact = {key(p): p for face in cut for p, _, _ in face[1]}

    def edges(face):
        corners = [p for p, _, _ in face[1]]
        return [tuple(sorted((key(corners[k]), key(corners[(k + 1) % 3])))) for k in range(3)]

    def is_top(face):
        return abs(face[1][0][2][1]) > 0.3

    used: dict[tuple, list] = {}
    walled = set()
    for face in cut:
        for e in edges(face):
            if is_top(face):
                used.setdefault(e, []).append(face)
            else:
                walled.add(e)
    walls = []
    for e, faces in used.items():
        if len(faces) != 1 or e in walled:
            continue
        (ax, ay, az), (bx, by, bz) = exact[e[0]], exact[e[1]]
        corners = [p for p, _, _ in faces[0][1]]
        cx, cz = sum(p[0] for p in corners) / 3, sum(p[2] for p in corners) / 3
        # outward: square to the edge, away from the face
        nx, nz = bz - az, -(bx - ax)
        length = math.hypot(nx, nz)
        if length < 1e-6:
            continue
        nx, nz = nx / length, nz / length
        if nx * ((ax + bx) / 2 - cx) + nz * ((az + bz) / 2 - cz) < 0:
            nx, nz = -nx, -nz
        normal = (nx, 0.0, nz)
        along = lambda x, z: (x * -nz + z * nx) / REPEAT_UNITS
        quad = [((ax, ay, az), (0.0, along(ax, az))), ((bx, by, bz), (0.0, along(bx, bz))),
                ((bx, by - WALL_DEPTH, bz), (1.0, along(bx, bz))),
                ((ax, ay - WALL_DEPTH, az), (1.0, along(ax, az)))]
        for tri in ((0, 1, 2), (0, 2, 3)):
            pts = [quad[i] for i in tri]
            (p0, _), (p1, _), (p2, _) = pts
            u = [p1[i] - p0[i] for i in range(3)]
            v = [p2[i] - p0[i] for i in range(3)]
            facing = (u[1] * v[2] - u[2] * v[1]) * nx + (u[0] * v[1] - u[1] * v[0]) * nz
            if facing < 0:
                pts = [pts[0], pts[2], pts[1]]
            walls.append(("Floor_Side", [(p, uv, normal) for p, uv in pts]))
    return walls


def in_frame(faces: list[Face], matrix: list[float], box: tuple) -> list[Face]:
    """The faces lying in box, in the frame of an object placed by matrix
    (unscaled), with their normals turned into it."""
    x0, x1, y0, y1, z0, z1 = box
    out = []
    for m, corners in faces:
        moved = [(into(matrix, p, 1.0), uv, into(matrix, n, 0.0)) for p, uv, n in corners]
        if all(x0 <= p[0] <= x1 and y0 <= p[1] <= y1 and z0 <= p[2] <= z1 for p, _, _ in moved):
            out.append((m, moved))
    return out


def kind_of(name: str) -> str | None:
    """The marker or element kind an object is one of, by its name."""
    if name == "SkyLayer":
        return name
    for kind in MARKERS + ELEMENT_KINDS:
        if re.fullmatch(re.escape(kind) + r"_\d+", name):
            return kind
    return None


def copy_mesh(nmo: Nmo, path: Path, mesh_id: int, kind: str, out: Path) -> None:
    """A mesh as it is, as <kind>.obj and .mtl."""
    with tempfile.TemporaryDirectory() as work:
        nmo.export_mesh(path, mesh_id, Path(work))
        obj = next(Path(work).glob("*.obj"))
        mtl = next(Path(work).glob("*.mtl"), None)
        text = obj.read_text(encoding="utf-8")
        if mtl is not None:
            text = text.replace(f"mtllib {mtl.name}", f"mtllib {kind}.mtl")
            (out / f"{kind}.mtl").write_text(mtl.read_text(encoding="utf-8"), encoding="utf-8")
        (out / f"{kind}.obj").write_text(text, encoding="utf-8")


def export_elements(levels: Levels, out: Path) -> dict[str, str]:
    """Writes every kind and cut to out; returns what each was taken from."""
    out.mkdir(parents=True, exist_ok=True)
    found: dict[str, str] = {}
    centers: dict[str, tuple] = {}
    for cut in CUTS:
        path = levels.by_name.get(cut.level.lower())
        if path is not None:
            source = cut.take(levels, path, out, centers)
            if source is not None:
                found[cut.kind] = source
    for path in levels.files:
        for name, fields in levels.objects(path).items():
            kind = kind_of(name)
            mesh = fields.get("current_mesh")
            if kind is None or kind in found or not mesh:
                continue
            copy_mesh(levels.nmo, path, mesh, kind, out)
            found[kind] = f"{path.name}:{name}"
        if all(kind in found for kind in KINDS):
            break
    return found


def every_kind() -> list[str]:
    """The names export_elements() writes, in the order it reports them."""
    marker_cuts = [c.kind for c in CUTS if isinstance(c, MarkerCut)]
    module_cuts = [c.kind for c in CUTS if isinstance(c, ModuleCut)]
    decor = [c.kind for c in CUTS if isinstance(c, DecorCut)]
    chains = [f"{c.kind}_{n}" for c in CUTS if isinstance(c, ChainCut)
              for n in range(1, c.most + 1)]
    return KINDS + marker_cuts + module_cuts + decor + chains
