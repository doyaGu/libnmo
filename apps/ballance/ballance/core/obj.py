"""Wavefront OBJ meshes: reading their triangles, writing new ones."""

from __future__ import annotations

from pathlib import Path
from typing import NamedTuple

Vec3 = tuple[float, float, float]
UV = tuple[float, float]


class Triangle(NamedTuple):
    material: str | None
    points: list[Vec3]
    uvs: list[UV] | None  # all three, or None
    normals: list[Vec3] | None  # all three, or None


def read_obj(path: Path) -> list[Triangle]:
    """The OBJ's faces, fanned into triangles."""
    vs: list[Vec3] = []
    vts: list[UV] = []
    vns: list[Vec3] = []
    triangles: list[Triangle] = []
    material = None

    def ref(table: list, index: str):
        n = int(index)
        return table[n - 1 if n > 0 else len(table) + n]

    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        t = line.split()
        if not t:
            continue
        if t[0] == "v":
            vs.append((float(t[1]), float(t[2]), float(t[3])))
        elif t[0] == "vt":
            vts.append((float(t[1]), float(t[2])))
        elif t[0] == "vn":
            vns.append((float(t[1]), float(t[2]), float(t[3])))
        elif t[0] == "usemtl":
            material = t[1]
        elif t[0] == "f":
            corners = []
            for word in t[1:]:
                parts = word.split("/")
                corners.append((ref(vs, parts[0]),
                                ref(vts, parts[1]) if len(parts) > 1 and parts[1] else None,
                                ref(vns, parts[2]) if len(parts) > 2 and parts[2] else None))
            for k in range(1, len(corners) - 1):
                three = (corners[0], corners[k], corners[k + 1])
                uvs = [c[1] for c in three]
                normals = [c[2] for c in three]
                triangles.append(Triangle(material, [c[0] for c in three],
                                          None if None in uvs else uvs,
                                          None if None in normals else normals))
    return triangles


class Mesh:
    """Triangles to write as an OBJ, each wound to face a given way.  A corner
    is (point, uv) or (point, uv, normal); a mesh whose corners have normals
    writes them."""

    def __init__(self) -> None:
        self.lines: list[str] = []
        self.count = 0
        self.material: str | None = None

    def triangle(self, material: str, facing: Vec3, *corners: tuple) -> None:
        a, b, c = (corner[0] for corner in corners)
        ab = [b[i] - a[i] for i in range(3)]
        ac = [c[i] - a[i] for i in range(3)]
        normal = (ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2],
                  ab[0] * ac[1] - ab[1] * ac[0])
        if sum(normal[i] * facing[i] for i in range(3)) < 0:
            corners = (corners[0], corners[2], corners[1])
        if material != self.material:
            self.lines.append(f"usemtl {material}")
            self.material = material
        for corner in corners:
            (x, y, z), (u, v) = corner[0], corner[1]
            self.lines.append(f"v {x:g} {y:g} {z:g}")
            self.lines.append(f"vt {u:g} {v:g}")
            if len(corner) > 2:
                self.lines.append("vn {:g} {:g} {:g}".format(*corner[2]))
        refs = [self.count + i for i in (1, 2, 3)]
        form = "{0}/{0}/{0}" if len(corners[0]) > 2 else "{0}/{0}"
        self.lines.append("f " + " ".join(form.format(r) for r in refs))
        self.count += 3

    def quad(self, material: str, facing: Vec3, *corners: tuple) -> None:
        self.triangle(material, facing, corners[0], corners[1], corners[2])
        self.triangle(material, facing, corners[0], corners[2], corners[3])

    def write(self, path: Path) -> None:
        path.write_text("\n".join(self.lines) + "\n", encoding="utf-8")


Box = tuple[float, float, float, float, float, float]  # x0, x1, y0, y1, z0, z1


def write_boxes(path: Path, boxes: list[Box]) -> None:
    """An OBJ of boxes, quads with no material or UVs."""
    faces = [(3, 4, 8, 7), (1, 5, 6, 2), (1, 2, 4, 3), (5, 7, 8, 6), (1, 3, 7, 5), (2, 6, 8, 4)]
    lines = []
    for k, (x0, x1, y0, y1, z0, z1) in enumerate(boxes):
        corners = [(x, y, z) for x in (x0, x1) for y in (y0, y1) for z in (z0, z1)]
        lines += [f"v {x:g} {y:g} {z:g}" for x, y, z in corners]
        lines += ["f " + " ".join(str(8 * k + i) for i in face) for face in faces]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_box(path: Path, box: Box) -> None:
    write_boxes(path, [box])
